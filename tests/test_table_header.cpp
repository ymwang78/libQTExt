// ***************************************************************
//  GTest coverage: xCheckableHeaderView 窄列左对齐右侧省略、布尔列复选框与垂直表头绘制
// ***************************************************************
#include "xTableHeader.h"
#include "xTableView.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCursor>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QImage>
#include <QPalette>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QStyleOptionHeader>
#include <QTableView>
#include <QTest>

namespace {

void ensureApplication() {
    if (QApplication::instance() != nullptr) {
        return;
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    static int argc = 1;
    static char arg0[] = "test_table_header";
    static char* argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
    // Windows 下 offscreen QPA
    // 默认不扫描系统字体目录，未注册真实字体时所有字符都会画成相同的缺字方框。
    if (QFontDatabase::families().isEmpty()) {
        for (const char* fontPath : {
                 "C:/Windows/Fonts/consola.ttf",
                 "C:/Windows/Fonts/segoeui.ttf",
                 "C:/Windows/Fonts/arial.ttf",
             }) {
            const QString path = QString::fromLatin1(fontPath);
            if (QFileInfo::exists(path) && QFontDatabase::addApplicationFont(path) >= 0) {
                break;
            }
        }
    }
}

int countForegroundInBand(const QImage& img, int secLeft, int relX0, int relX1, int y0, int y1,
                          const QColor& bg, int threshold = 60) {
    int fgPixels = 0;
    for (int y = y0; y <= y1 && y < img.height(); ++y) {
        if (y < 0) continue;
        for (int x = secLeft + relX0; x <= secLeft + relX1 && x < img.width(); ++x) {
            if (x < 0) continue;
            const QColor c = img.pixelColor(x, y);
            const int diff = qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) +
                             qAbs(c.blue() - bg.blue());
            if (diff > threshold) {
                ++fgPixels;
            }
        }
    }
    return fgPixels;
}

int countSectionDiffInBand(const QImage& imgA, int secLeftA, const QImage& imgB, int secLeftB,
                           int relX0, int relX1, int y0, int y1, int threshold = 30) {
    int diffPixels = 0;
    for (int y = y0; y <= y1 && y < imgA.height() && y < imgB.height(); ++y) {
        if (y < 0) continue;
        for (int dx = relX0; dx <= relX1; ++dx) {
            const int xA = secLeftA + dx;
            const int xB = secLeftB + dx;
            if (xA < 0 || xB < 0 || xA >= imgA.width() || xB >= imgB.width()) continue;
            const QColor cA = imgA.pixelColor(xA, y);
            const QColor cB = imgB.pixelColor(xB, y);
            const int diff = qAbs(cA.red() - cB.red()) + qAbs(cA.green() - cB.green()) +
                             qAbs(cA.blue() - cB.blue());
            if (diff > threshold) {
                ++diffPixels;
            }
        }
    }
    return diffPixels;
}

QImage renderWidget(QWidget* widget) {
    QImage img(widget->size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    widget->render(&img);
    return img;
}

class TableHeader : public ::testing::Test {
  protected:
    void SetUp() override { ensureApplication(); }
};

TEST_F(TableHeader, NarrowHorizontalHeaderPreservesLeftPrefixAndKeepsResizeToContents) {
    xTableView table(nullptr, false);
    QStandardItemModel model(1, 5);
    model.setHorizontalHeaderLabels({
        QStringLiteral("No."),
        QStringLiteral("ALPHA_0000000000000000"),
        QStringLiteral("ALPHA_9999999999999999"),
        QStringLiteral("OMEGA_0000000000000000"),
        QStringLiteral("Eng High Limit"),
    });
    for (int col = 0; col < 5; ++col) {
        model.setItem(0, col, new QStandardItem(QString::number(col)));
    }
    table.setSourceModel(&model);

    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    header->setStretchLastSection(false);
    header->resizeSection(0, 120);
    header->resizeSection(1, 52);
    header->resizeSection(2, 52);
    header->resizeSection(3, 52);
    header->resizeSection(4, 52);

    table.resize(480, 160);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    const QImage headerImg = renderWidget(header);
    const int y0 = 3;
    const int y1 = qMax(y0, header->height() - 4);

    // 宽列（"No."，120px）应保持居中：左侧区域无字墨，中间区域有字墨。
    const int sec0Left = header->sectionViewportPosition(0);
    const QColor sec0Bg = headerImg.pixelColor(sec0Left + 6, header->height() / 2);
    EXPECT_EQ(countForegroundInBand(headerImg, sec0Left, 6, 24, y0, y1, sec0Bg), 0);
    EXPECT_GT(countForegroundInBand(headerImg, sec0Left, 40, 80, y0, y1, sec0Bg), 10);

    // 窄列 1 与窄列 2 前缀同为 "ALPHA_"、中后段不同（000... vs 999...）；
    // 窄列 1 与窄列 3 前缀不同（ALPHA_ vs OMEGA_）、中后段同为 "_0000000000000000"。
    // 左对齐并截断右侧时：列 1 与列 2 在左侧前缀带 [4, 20] 像素一致，而列 1 与列 3 明显不同。
    const int sec1Left = header->sectionViewportPosition(1);
    const int sec2Left = header->sectionViewportPosition(2);
    const int sec3Left = header->sectionViewportPosition(3);
    EXPECT_EQ(countSectionDiffInBand(headerImg, sec1Left, headerImg, sec2Left, 4, 20, y0, y1), 0);
    EXPECT_GT(countSectionDiffInBand(headerImg, sec1Left, headerImg, sec3Left, 4, 20, y0, y1), 15);

    // ResizeToContents 仍按未省略的完整表头文本测量列宽。
    const int fullTextWidth =
        header->fontMetrics().horizontalAdvance(QStringLiteral("Eng High Limit"));
    table.resizeColumnToContents(4);
    EXPECT_GE(table.columnWidth(4), fullTextWidth);
}

TEST_F(TableHeader, VerticalHeaderBooleanSectionDrawsAndTogglesCheckBox) {
    QTableView table;
    auto* vHeader = new xCheckableHeaderView(Qt::Vertical, &table);
    table.setVerticalHeader(vHeader);

    QStandardItemModel model(2, 1);
    model.setVerticalHeaderLabels({QString(), QStringLiteral("R1")});
    model.setItem(0, 0, new QStandardItem(QStringLiteral("a")));
    model.setItem(1, 0, new QStandardItem(QStringLiteral("b")));
    table.setModel(&model);
    vHeader->setFixedWidth(48);
    vHeader->setDefaultSectionSize(26);

    table.resize(240, 140);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    const QImage plainImg = renderWidget(vHeader);

    vHeader->setBoolColumn(0, true);
    QCoreApplication::processEvents();
    const QImage uncheckedImg = renderWidget(vHeader);

    vHeader->setCheckState(0, Qt::Checked);
    QCoreApplication::processEvents();
    const QImage checkedImg = renderWidget(vHeader);

    EXPECT_NE(plainImg, uncheckedImg);
    EXPECT_NE(uncheckedImg, checkedImg);

    vHeader->setCheckState(0, Qt::Unchecked);
    QSignalSpy spy(vHeader, &xCheckableHeaderView::checkboxToggled);
    QStyleOptionButton opt;
    const QRect cbIndicator = vHeader->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt);
    const int clickX = 5 + cbIndicator.width() / 2;
    const int clickY = vHeader->sectionViewportPosition(0) + vHeader->sectionSize(0) / 2;
    QTest::mouseClick(vHeader->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(clickX, clickY));
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), 0);
    EXPECT_EQ(spy.at(0).at(1).value<Qt::CheckState>(), Qt::Checked);
}

TEST_F(TableHeader, NarrowBooleanHorizontalHeaderShiftsTextRightOfCheckBox) {
    xTableView table(nullptr, false);
    QStandardItemModel model(1, 1);
    model.setHorizontalHeaderLabels({QString()});
    model.setItem(0, 0, new QStandardItem(QStringLiteral("1")));
    table.setSourceModel(&model);
    table.setBoolColumn(0, true);

    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    header->setStretchLastSection(false);
    header->resizeSection(0, 68);

    table.resize(220, 120);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    const QImage emptyLabelImg = renderWidget(header);

    model.setHorizontalHeaderLabels({QStringLiteral("WWW_ENABLE_COLUMN")});
    QCoreApplication::processEvents();
    const QImage textLabelImg = renderWidget(header);

    QStyleOptionButton opt;
    const QRect cbIndicator = header->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt);
    const int cbLeft = 5;
    const int cbRight = cbLeft + cbIndicator.width() - 1;
    const int y0 = 3;
    const int y1 = qMax(y0, header->height() - 4);

    // 复选框区域像素不应被窄列文字覆盖，而复选框右侧区域应出现文字像素变化。
    EXPECT_EQ(countSectionDiffInBand(emptyLabelImg, 0, textLabelImg, 0, cbLeft, cbRight, y0, y1, 0),
              0);
    EXPECT_GT(countSectionDiffInBand(emptyLabelImg, 0, textLabelImg, 0, cbRight + 4,
                                     header->sectionSize(0) - 5, y0, y1),
              15);
}

TEST_F(TableHeader, HeaderDecorationIconReservesWidthWhenElidingText) {
    xTableView table(nullptr, false);
    const QString fullText = QStringLiteral("WWWWWWWWWWWWWWWWWWWW");
    QStandardItemModel model(1, 1);
    model.setHorizontalHeaderLabels({fullText});
    QPixmap iconPixmap(16, 16);
    iconPixmap.fill(QColor(0, 200, 0));
    model.setHeaderData(0, Qt::Horizontal, QIcon(iconPixmap), Qt::DecorationRole);
    model.setItem(0, 0, new QStandardItem(QStringLiteral("1")));
    table.setSourceModel(&model);

    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    header->setStretchLastSection(false);
    header->resizeSection(0, 84);

    table.resize(240, 120);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    const QImage actualImg = renderWidget(header);

    // 按 QCommonStyle::CE_HeaderLabel 口径算出真实 labelRect 与扣除图标+间距后的期望省略文本。
    QStyleOptionHeaderV2 opt;
    opt.initFrom(header);
    opt.state |= QStyle::State_Horizontal | QStyle::State_Enabled | QStyle::State_Raised;
    opt.section = 0;
    opt.orientation = Qt::Horizontal;
    opt.icon = QIcon(iconPixmap);
    opt.rect =
        QRect(header->sectionViewportPosition(0), 0, header->sectionSize(0), header->height());
    const QRect labelRect = header->style()->subElementRect(QStyle::SE_HeaderLabel, &opt, header);
    ASSERT_TRUE(labelRect.isValid());
    const int iconExtent = header->style()->pixelMetric(QStyle::PM_SmallIconSize, &opt, header);
    const qreal dpr = actualImg.devicePixelRatio() > 0.0 ? actualImg.devicePixelRatio() : 1.0;
    const QPixmap expectedPm = opt.icon.pixmap(QSize(iconExtent, iconExtent), dpr, QIcon::Normal);
    const qreal pmDpr = expectedPm.devicePixelRatio() > 0.0 ? expectedPm.devicePixelRatio() : 1.0;
    const int iconReserve = static_cast<int>(expectedPm.width() / pmDpr) +
                            header->style()->pixelMetric(QStyle::PM_HeaderMargin, &opt, header);

    const QFontMetrics fm = header->fontMetrics();
    const QString expectedElided =
        fm.elidedText(fullText, Qt::ElideRight, labelRect.width() - iconReserve);
    const QString unreservedElided = fm.elidedText(fullText, Qt::ElideRight, labelRect.width());
    ASSERT_FALSE(expectedElided.isEmpty());
    ASSERT_NE(expectedElided, fullText);
    ASSERT_NE(expectedElided, unreservedElided);

    // 将表头文本直接设为期望省略串并显式左对齐：此时文本已不超宽，直接走原生 CE_Header 绘制。
    // 若 paintSection 漏扣了 iconReserve，actualImg 会按 unreservedElided
    // 绘制并在右缘硬裁（缺省略号）， 与 expectedImg 在右侧字符区域必然产生像素差异。
    model.setHeaderData(0, Qt::Horizontal, int(Qt::AlignLeft | Qt::AlignVCenter),
                        Qt::TextAlignmentRole);
    model.setHeaderData(0, Qt::Horizontal, expectedElided, Qt::DisplayRole);
    QCoreApplication::processEvents();
    const QImage expectedImg = renderWidget(header);

    EXPECT_EQ(countSectionDiffInBand(actualImg, 0, expectedImg, 0, 0, header->sectionSize(0) - 1, 0,
                                     header->height() - 1, 0),
              0);
}

TEST_F(TableHeader, FrozenColumnsPreserveLeftPrefixAndStayCenteredWhenWide) {
    xTableView table(nullptr, true);
    EXPECT_EQ(table.horizontalHeader()->textElideMode(), Qt::ElideRight);

    QStandardItemModel model(2, 5);
    model.setHorizontalHeaderLabels({
        QStringLiteral("No."),
        QStringLiteral("ALPHA_0000000000000000"),
        QStringLiteral("ALPHA_9999999999999999"),
        QStringLiteral("OMEGA_0000000000000000"),
        QStringLiteral("Unfrozen Tail"),
    });
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 5; ++c) {
            model.setItem(r, c, new QStandardItem(QString::number(r * 10 + c)));
        }
    }
    table.setSourceModel(&model);

    QHeaderView* mainHeader = table.horizontalHeader();
    ASSERT_NE(mainHeader, nullptr);
    mainHeader->setStretchLastSection(false);
    mainHeader->resizeSection(0, 120);
    mainHeader->resizeSection(1, 52);
    mainHeader->resizeSection(2, 52);
    mainHeader->resizeSection(3, 52);
    mainHeader->resizeSection(4, 120);

    table.freezeLeftColumns(4);

    table.resize(480, 160);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    QTableView* frozenView = nullptr;
    const QList<QTableView*> childTables =
        table.findChildren<QTableView*>(QString(), Qt::FindDirectChildrenOnly);
    for (QTableView* candidate : childTables) {
        if (candidate && candidate->isVisible() && !candidate->horizontalHeader()->isHidden()) {
            frozenView = candidate;
            break;
        }
    }
    ASSERT_NE(frozenView, nullptr);
    QHeaderView* frozenHeader = frozenView->horizontalHeader();
    ASSERT_NE(frozenHeader, nullptr);
    EXPECT_NE(qobject_cast<xCheckableHeaderView*>(frozenHeader), nullptr);
    EXPECT_EQ(frozenHeader->textElideMode(), Qt::ElideRight);

    const QImage frozenImg = renderWidget(frozenHeader);
    const int y0 = 3;
    const int y1 = qMax(y0, frozenHeader->height() - 4);

    const int sec0Left = frozenHeader->sectionViewportPosition(0);
    const QColor sec0Bg = frozenImg.pixelColor(sec0Left + 6, frozenHeader->height() / 2);
    EXPECT_EQ(countForegroundInBand(frozenImg, sec0Left, 6, 24, y0, y1, sec0Bg), 0);
    EXPECT_GT(countForegroundInBand(frozenImg, sec0Left, 40, 80, y0, y1, sec0Bg), 10);

    const int sec1Left = frozenHeader->sectionViewportPosition(1);
    const int sec2Left = frozenHeader->sectionViewportPosition(2);
    const int sec3Left = frozenHeader->sectionViewportPosition(3);
    EXPECT_EQ(countSectionDiffInBand(frozenImg, sec1Left, frozenImg, sec2Left, 4, 20, y0, y1), 0);
    EXPECT_GT(countSectionDiffInBand(frozenImg, sec1Left, frozenImg, sec3Left, 4, 20, y0, y1), 15);
}

TEST_F(TableHeader, SortableHeaderReservesSortArrowWidthUnderStyleSheet) {
    std::unique_ptr<QStyle> fusionStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    ASSERT_NE(fusionStyle, nullptr);

    xTableView table(nullptr, true);
    table.setStyle(fusionStyle.get());
    QStandardItemModel model(2, 1);
    model.setHorizontalHeaderLabels({QString()});
    model.setItem(0, 0, new QStandardItem(QStringLiteral("2")));
    model.setItem(1, 0, new QStandardItem(QStringLiteral("1")));
    table.setSourceModel(&model);

    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    header->setStyle(fusionStyle.get());
    header->setStretchLastSection(false);
    header->resizeSection(0, 76);
    table.sortBy(0, Qt::AscendingOrder);

    table.resize(240, 140);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    const QImage emptySortedImg = renderWidget(header);

    model.setHorizontalHeaderLabels({QStringLiteral("WWW_SORTED_LONG_HEADER_TEXT")});
    QCoreApplication::processEvents();
    const QImage textSortedImg = renderWidget(header);

    QStyleOptionHeaderV2 opt;
    opt.initFrom(header);
    opt.orientation = Qt::Horizontal;
    opt.state |= QStyle::State_Horizontal;
    opt.rect = QRect(0, 0, header->sectionSize(0), header->height());
    opt.sortIndicator = QStyleOptionHeader::SortDown;
    const QRect arrowRect = header->style()->subElementRect(QStyle::SE_HeaderArrow, &opt, header);
    ASSERT_TRUE(arrowRect.isValid());
    ASSERT_GE(arrowRect.bottom(), opt.rect.center().y());
    ASSERT_GT(arrowRect.left(), opt.rect.center().x());

    const int y0 = 3;
    const int y1 = qMax(y0, header->height() - 4);
    EXPECT_EQ(countSectionDiffInBand(emptySortedImg, 0, textSortedImg, 0, arrowRect.left(),
                                     arrowRect.right(), y0, y1, 0),
              0);
    EXPECT_GT(
        countSectionDiffInBand(emptySortedImg, 0, textSortedImg, 0, 4, arrowRect.left() - 3, y0, y1),
        15);
}

TEST_F(TableHeader, MultiLineHeaderFittingHorizontallyStaysCentered) {
    xTableView table(nullptr, false);
    const QString multiLine = QStringLiteral("ControllerGain\n(Milliseconds)");
    QStandardItemModel model(1, 1);
    model.setHorizontalHeaderLabels({multiLine});
    model.setItem(0, 0, new QStandardItem(QStringLiteral("1")));
    table.setSourceModel(&model);

    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    const QFontMetrics fm = header->fontMetrics();
    const int maxLineW = qMax(fm.horizontalAdvance(QStringLiteral("ControllerGain")),
                              fm.horizontalAdvance(QStringLiteral("(Milliseconds)")));
    const int secWidth = maxLineW + 64;
    ASSERT_GT(fm.horizontalAdvance(multiLine), secWidth);

    header->setStretchLastSection(false);
    header->setFixedHeight(44);
    header->resizeSection(0, secWidth);

    table.resize(secWidth + 80, 140);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    const QImage headerImg = renderWidget(header);
    const int y0 = 4;
    const int y1 = header->height() - 5;
    const QColor bg = headerImg.pixelColor(6, header->height() / 2);
    // 单行宽度未超列宽（尽管单行拼接总长超过列宽），不应被误判为溢出左对齐或被二次截断。
    EXPECT_EQ(countForegroundInBand(headerImg, 0, 4, 18, y0, y1, bg), 0);
    EXPECT_GT(countForegroundInBand(headerImg, 0, 26, secWidth - 26, y0, y1, bg), 15);

    // 布尔列变体：每行都放得下且居中起点在复选框右侧（走布尔列宽裕的 else 分支），
    // 第二行同样不应被 CE_HeaderLabel 按整串单行总长误截断。
    table.setBoolColumn(0, true);
    QCoreApplication::processEvents();
    ASSERT_EQ(header->textElideMode(), Qt::ElideRight);
    const QImage boolHeaderImg = renderWidget(header);

    header->setTextElideMode(Qt::ElideNone);
    QCoreApplication::processEvents();
    const QImage boolUnelidedRefImg = renderWidget(header);
    EXPECT_EQ(countSectionDiffInBand(boolHeaderImg, 0, boolUnelidedRefImg, 0, 0, secWidth - 1, 0,
                                     header->height() - 1, 0),
              0);

    // 垂直表头保持 Qt 默认的 ElideNone，不截断多行垂直标签。
    xCheckableHeaderView vHeader(Qt::Vertical);
    EXPECT_EQ(vHeader.textElideMode(), Qt::ElideNone);
}

TEST_F(TableHeader, MousePressInTableBodyDoesNotPaintHeaderSunkenAndDarkThemeWorks) {
    const QPalette origPalette = QApplication::palette();
    auto restorePalette = qScopeGuard([&origPalette]() { QApplication::setPalette(origPalette); });

    QPalette darkPalette = origPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 36));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    QApplication::setPalette(darkPalette);

    xTableView table(nullptr, true);
    QStandardItemModel model(2, 3);
    model.setHorizontalHeaderLabels({
        QStringLiteral("ALPHA_0000000000000000"),
        QStringLiteral("ALPHA_9999999999999999"),
        QStringLiteral("OMEGA_0000000000000000"),
    });
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 3; ++c) {
            model.setItem(r, c, new QStandardItem(QString::number(c)));
        }
    }
    table.setSourceModel(&model);

    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    header->setStretchLastSection(false);
    header->resizeSection(0, 52);
    header->resizeSection(1, 52);
    header->resizeSection(2, 52);

    table.resize(260, 150);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    // 暗色主题下窄列仍保留左侧前缀并截断右侧。
    const QImage darkHeaderImg = renderWidget(header);
    const int y0 = 3;
    const int y1 = qMax(y0, header->height() - 4);
    const int sec0Left = header->sectionViewportPosition(0);
    const int sec1Left = header->sectionViewportPosition(1);
    const int sec2Left = header->sectionViewportPosition(2);
    EXPECT_EQ(
        countSectionDiffInBand(darkHeaderImg, sec0Left, darkHeaderImg, sec1Left, 4, 20, y0, y1), 0);
    EXPECT_GT(
        countSectionDiffInBand(darkHeaderImg, sec0Left, darkHeaderImg, sec2Left, 4, 20, y0, y1),
        15);

    // 真实走 xCheckableHeaderView::paintSection 验证按下态：
    // 为表头配置显式的 :pressed 红色背景规则，确认真正被按下的列（列 1）呈现按下色，
    // 而在按住左键把指针移到列 0 上、或在表格视口按住左键时，列 0 不会被误画成按下态。
    QTableView pressTable;
    auto* pressHeader = new xCheckableHeaderView(Qt::Horizontal, &pressTable);
    pressTable.setHorizontalHeader(pressHeader);
    pressHeader->setHighlightSections(false);
    pressHeader->setSectionsMovable(false);
    pressHeader->setStyleSheet(
        "QHeaderView::section { background-color: rgb(230, 230, 230); color: black;"
        " border: 1px solid rgb(180, 180, 180); }"
        "QHeaderView::section:pressed { background-color: rgb(200, 40, 40); }");

    QStandardItemModel pressModel(3, 2);
    pressModel.setHorizontalHeaderLabels({QStringLiteral("Col0"), QStringLiteral("Col1")});
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 2; ++c) {
            pressModel.setItem(r, c, new QStandardItem(QString::number(r)));
        }
    }
    pressTable.setModel(&pressModel);
    pressHeader->setStretchLastSection(false);
    pressHeader->resizeSection(0, 80);
    pressHeader->resizeSection(1, 80);
    pressTable.resize(220, 140);
    pressTable.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&pressTable));
    QCoreApplication::processEvents();

    const QImage idleHeaderImg = renderWidget(pressHeader);
    const int pH = pressHeader->height();
    const QPoint sec0Pt(20, pH / 2);
    const QPoint sec1Pt(120, pH / 2);

    // 1) 正向对照：在表头列 1 按住左键时，列 1 呈现 :pressed 红色背景，列 0 保持不变。
    QTest::mousePress(pressHeader->viewport(), Qt::LeftButton, Qt::NoModifier, sec1Pt);
    QCoreApplication::processEvents();
    const QImage headerPressImg = renderWidget(pressHeader);
    EXPECT_GT(countSectionDiffInBand(idleHeaderImg, 80, headerPressImg, 80, 2, 77, 2, pH - 3), 50);
    EXPECT_EQ(countSectionDiffInBand(idleHeaderImg, 0, headerPressImg, 0, 2, 77, 2, pH - 3, 0), 0);
    QTest::mouseRelease(pressHeader->viewport(), Qt::LeftButton, Qt::NoModifier, sec1Pt);
    QCoreApplication::processEvents();

    // 2) 反向回归：在表格视口按住左键拖选并将全局光标移到表头列 0 上，列 0 不应误进入 :pressed 态。
    QTest::mousePress(pressTable.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(120, 40));
    QCursor::setPos(pressHeader->viewport()->mapToGlobal(sec0Pt));
    QCoreApplication::processEvents();
    const QImage bodyPressHeaderImg = renderWidget(pressHeader);
    EXPECT_EQ(countSectionDiffInBand(idleHeaderImg, 0, bodyPressHeaderImg, 0, 2, 77, 2, pH - 3, 0),
              0);
    QTest::mouseRelease(pressTable.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(120, 40));
    QCoreApplication::processEvents();
}

TEST_F(TableHeader, ExplicitTextElideModeIsRespectedOnHorizontalHeader) {
    xTableView table(nullptr, false);
    QStandardItemModel model(1, 3);
    model.setHorizontalHeaderLabels({
        QStringLiteral("MW_000000000000000000_ALPHA"),
        QStringLiteral("WM_000000000000000000_ALPHA"),
        QStringLiteral("WM_000000000000000000_OMEGA"),
    });
    for (int col = 0; col < 3; ++col) {
        model.setItem(0, col, new QStandardItem(QString::number(col)));
    }
    table.setSourceModel(&model);

    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    header->setStretchLastSection(false);
    header->resizeSection(0, 56);
    header->resizeSection(1, 56);
    header->resizeSection(2, 56);

    table.resize(260, 140);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    const int y0 = 3;
    const int y1 = qMax(y0, header->height() - 4);
    const int sec0Left = header->sectionViewportPosition(0);
    const int sec1Left = header->sectionViewportPosition(1);
    const int sec2Left = header->sectionViewportPosition(2);

    // 默认 ElideRight：列 0 与列 1 首字符不同（MW vs WM），左侧前缀区域显著不同。
    ASSERT_EQ(header->textElideMode(), Qt::ElideRight);
    const QImage elideRightImg = renderWidget(header);
    EXPECT_GT(
        countSectionDiffInBand(elideRightImg, sec0Left, elideRightImg, sec1Left, 4, 18, y0, y1),
        10);

    // 显式设为 ElideNone：跳过自定义省略，回到原生居中无省略绘制（中段同为 0000...，列 0 与列 1
    // 一致）。
    header->setTextElideMode(Qt::ElideNone);
    QCoreApplication::processEvents();
    const QImage elideNoneImg = renderWidget(header);
    EXPECT_GT(
        countSectionDiffInBand(elideRightImg, sec0Left, elideNoneImg, sec0Left, 4, 50, y0, y1), 15);
    EXPECT_EQ(countSectionDiffInBand(elideNoneImg, sec0Left, elideNoneImg, sec1Left, 6, 48, y0, y1),
              0);

    // 显式设为 ElideLeft：截断左侧并保留右侧后缀（列 1 与列 2 后缀 ALPHA vs OMEGA 在右侧区域不同，
    // 列 0 与列 1 后缀同为 ALPHA 在右侧区域一致）。
    header->setTextElideMode(Qt::ElideLeft);
    QCoreApplication::processEvents();
    const QImage elideLeftImg = renderWidget(header);
    EXPECT_EQ(
        countSectionDiffInBand(elideLeftImg, sec0Left, elideLeftImg, sec1Left, 34, 51, y0, y1), 0);
    EXPECT_GT(
        countSectionDiffInBand(elideLeftImg, sec1Left, elideLeftImg, sec2Left, 34, 51, y0, y1), 10);

    // 显式设为 ElideMiddle：保留首尾、省略中段。按 SE_HeaderLabel 口径算出期望的中段省略串，再把
    // 模型文本直接设为该串（已放得下，不再省略）渲染基准图，与自动省略结果逐像素比对；未省略
    // （居中两头裁剪）或误按 ElideRight 左对齐省略都会产生像素差异。
    // QStyleOptionHeaderV2::textElideMode 是 2 位枚举位域，MSVC 下 ElideMiddle 会读回 -2，
    // 这里同时守住 paintSection 必须从 textElideMode() 读取省略模式。
    header->setTextElideMode(Qt::ElideMiddle);
    QCoreApplication::processEvents();
    const QImage elideMiddleImg = renderWidget(header);

    const QString middleFull = model.headerData(1, Qt::Horizontal).toString();
    QStyleOptionHeaderV2 middleOpt;
    middleOpt.initFrom(header);
    middleOpt.state |= QStyle::State_Horizontal | QStyle::State_Enabled | QStyle::State_Raised;
    middleOpt.section = 1;
    middleOpt.orientation = Qt::Horizontal;
    middleOpt.rect = QRect(sec1Left, 0, header->sectionSize(1), header->height());
    const QRect middleLabelRect =
        header->style()->subElementRect(QStyle::SE_HeaderLabel, &middleOpt, header);
    ASSERT_TRUE(middleLabelRect.isValid());
    const QString middleExpected = header->fontMetrics().elidedText(
        middleFull, Qt::ElideMiddle, middleLabelRect.width());
    ASSERT_NE(middleExpected, middleFull);
    ASSERT_TRUE(middleExpected.startsWith(middleFull.left(1)));
    ASSERT_TRUE(middleExpected.endsWith(middleFull.right(1)));

    model.setHeaderData(1, Qt::Horizontal, middleExpected, Qt::DisplayRole);
    QCoreApplication::processEvents();
    const QImage middleRefImg = renderWidget(header);
    EXPECT_EQ(countSectionDiffInBand(elideMiddleImg, sec1Left, middleRefImg, sec1Left, 0,
                                     header->sectionSize(1) - 1, 0, header->height() - 1, 0),
              0);

    // 冻结列视图创建时同步主表头的非默认 textElideMode（#39）：
    // 先在主表头设好 ElideMiddle 再冻结前 2 列，冻结表头应继承 ElideMiddle 并与主表头渲染一致。
    model.setHeaderData(1, Qt::Horizontal, middleFull, Qt::DisplayRole);
    header->setTextElideMode(Qt::ElideMiddle);
    table.freezeLeftColumns(2);
    QCoreApplication::processEvents();

    QTableView* frozenView = nullptr;
    for (QTableView* candidate :
         table.findChildren<QTableView*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (candidate && candidate->isVisible() && !candidate->horizontalHeader()->isHidden()) {
            frozenView = candidate;
            break;
        }
    }
    ASSERT_NE(frozenView, nullptr);
    QHeaderView* frozenHeader = frozenView->horizontalHeader();
    ASSERT_NE(frozenHeader, nullptr);
    EXPECT_EQ(frozenHeader->textElideMode(), Qt::ElideMiddle);

    const QImage frozenMiddleImg = renderWidget(frozenHeader);
    const int frozenSec1Left = frozenHeader->sectionViewportPosition(1);
    model.setHeaderData(1, Qt::Horizontal, middleExpected, Qt::DisplayRole);
    QCoreApplication::processEvents();
    const QImage frozenMiddleRefImg = renderWidget(frozenHeader);
    EXPECT_EQ(countSectionDiffInBand(frozenMiddleImg, frozenSec1Left, frozenMiddleRefImg,
                                     frozenSec1Left, 0, frozenHeader->sectionSize(1) - 1, 0,
                                     frozenHeader->height() - 1, 0),
              0);
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
