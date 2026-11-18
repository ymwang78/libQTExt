// ***************************************************************
//  GTest coverage: xCheckableHeaderView 窄列左对齐右侧省略、布尔列复选框与垂直表头绘制
// ***************************************************************
#include "xTableHeader.h"
#include "xTableView.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QImage>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOptionButton>
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
    QStandardItemModel model(1, 5, &table);
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

    QStandardItemModel model(2, 1, &table);
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
    QStandardItemModel model(1, 1, &table);
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
    QStandardItemModel model(1, 1, &table);
    model.setHorizontalHeaderLabels({QStringLiteral("WWW_ICON_HEADER_TEXT")});
    QPixmap iconPixmap(16, 16);
    iconPixmap.fill(QColor(0, 200, 0));
    model.setHeaderData(0, Qt::Horizontal, QIcon(iconPixmap), Qt::DecorationRole);
    model.setItem(0, 0, new QStandardItem(QStringLiteral("1")));
    table.setSourceModel(&model);

    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    header->setStretchLastSection(false);
    header->resizeSection(0, 68);

    table.resize(220, 120);
    table.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&table));
    QCoreApplication::processEvents();

    const QImage headerImg = renderWidget(header);
    int greenIconPixels = 0;
    int rightTextPixels = 0;
    const QColor bg = headerImg.pixelColor(header->sectionSize(0) - 3, 2);
    for (int y = 3; y <= header->height() - 4; ++y) {
        for (int x = 2; x <= 24 && x < headerImg.width(); ++x) {
            const QColor c = headerImg.pixelColor(x, y);
            if (c.green() > 150 && c.red() < 80 && c.blue() < 80) {
                ++greenIconPixels;
            }
        }
        for (int x = 25; x <= header->sectionSize(0) - 5 && x < headerImg.width(); ++x) {
            const QColor c = headerImg.pixelColor(x, y);
            const int diff = qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) +
                             qAbs(c.blue() - bg.blue());
            if (diff > 60) {
                ++rightTextPixels;
            }
        }
    }
    EXPECT_GT(greenIconPixels, 40);
    EXPECT_GT(rightTextPixels, 15);
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
