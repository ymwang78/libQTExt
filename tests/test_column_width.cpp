// ***************************************************************
//  GTest coverage: xTableView 的列宽模式与 stretchLastSection 的互斥
// ***************************************************************
//
// 默认列宽模式没有改动（Interactive + Qt 默认 section 宽度），这里盖住的是四个配套修复：
//
//  1. 比例模式期间强制关掉 stretchLastSection。只要比例算出来的总宽没铺满视口，
//     stretchLastSection 就把末列拉去填掉那块空余，末列的比例被吃掉（实测 1/4 变 3/4）。
//     调用方自己设过 setStretchLastSection(true) 的地方不少（xOptCon 里有十几处），
//     比例模式期间关掉，退出时还原调用方的原值。
//  2. resizeEvent 无条件先调基类。基类负责 updateGeometries()，比例模式以前把它整个跳过，
//     只改高度的 resize 不会触发 sectionResized，滚动条范围就留在旧值上。
//  3. saveUiState 升到 version 2，单独存一份调用方原本的 stretchLastSection：比例模式下
//     表头自己的 saveState() 里存的是被改成 false 的那个值。v1 的老状态行为不变。
//  4. autoFitColumns() 按内容量一次列宽，但不在构造函数里调（构造时模型还是空的），
//     并且量完要还原 resizeContentsPrecision —— 它会被 saveState() 序列化进 UI 状态。
#include "xTableView.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTest>

#include <memory>
#include <cmath>

namespace {

void ensureApplication() {
    if (QApplication::instance() != nullptr) {
        return;
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    static int argc = 1;
    static char arg0[] = "test_column_width";
    static char* argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
}

class ColumnWidth : public ::testing::Test {
  protected:
    void SetUp() override { ensureApplication(); }

    void TearDown() override {
        view_.reset();
        model_.reset();
    }

    void setUpView(int row_count, int column_count, int width = 480, int height = 320) {
        model_ = std::make_unique<QStandardItemModel>(row_count, column_count);
        for (int row = 0; row < row_count; ++row) {
            for (int col = 0; col < column_count; ++col) {
                model_->setData(model_->index(row, col),
                                QStringLiteral("r%1c%2").arg(row).arg(col));
            }
        }
        view_ = std::make_unique<xTableView>();
        view_->setSourceModel(model_.get());
        view_->resize(width, height);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
    }

    QHeaderView* header() const { return view_->horizontalHeader(); }

    QList<int> columnWidths() const {
        QList<int> widths;
        for (int col = 0; col < model_->columnCount(); ++col) {
            widths.append(view_->columnWidth(col));
        }
        return widths;
    }

    int viewportWidth() const { return view_->viewport()->width(); }

    int totalColumnWidth() const {
        int total = 0;
        for (int width : columnWidths()) total += width;
        return total;
    }

    // 某列占视口的比例。等宽可调模式的断言都盯着比例，不是像素值。
    double columnShare(int col) const {
        return static_cast<double>(view_->columnWidth(col)) / viewportWidth();
    }

    // 模拟用户拖表头分隔条：从外部调 resizeSection，走的信号路径和用户拖拽一样
    // （本类自己改列宽时会先置守卫位，sectionResized 槽里据此区分）。
    void dragColumnTo(int col, int width) {
        header()->resizeSection(col, width);
        QCoreApplication::processEvents();
    }

    void resizeViewTo(int width, int height = 320) {
        view_->resize(width, height);
        QCoreApplication::processEvents();
    }

    std::unique_ptr<QStandardItemModel> model_;
    std::unique_ptr<xTableView> view_;
};

// --- 1. 比例模式 vs stretchLastSection -----------------------------------------------

TEST_F(ColumnWidth, RatioModeTurnsOffStretchLastSection) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);

    view_->setColumnWidthRatios({1, 1, 1});

    EXPECT_FALSE(header()->stretchLastSection());
}

TEST_F(ColumnWidth, RatiosAreAppliedProportionally) {
    setUpView(4, 3);

    view_->setColumnWidthRatios({2, 1, 1});

    EXPECT_NEAR(view_->columnWidth(1), view_->columnWidth(0) / 2, 2);
    EXPECT_NEAR(view_->columnWidth(2), view_->columnWidth(0) / 2, 2);
}

// stretchLastSection 真正会吃掉末列比例的场合：比例列表比列数长。
// applyColumnWidthRatios() 的 totalRatio 把列表里所有项都算进去，但只给存在的列设宽度，
// 于是视口有一截是空的 —— stretchLastSection 就把末列拉去填掉那一截。
// 比例列表长于列数很常见：setColumnWidthRatios() 多半在模型填数据之前就调了。
//
// 两个必要条件，少一个这个测试就测不出东西来（都是实测出来的）：
//  - 比例必须留下空余。比例覆盖了所有列时，它正好铺满视口，剩余空间恰好等于末列那一份，
//    开不开 stretchLastSection 结果一样；覆盖不全时总宽超出视口，Qt 会还原末列存着的尺寸，
//    也看不出差别。
//  - 套完比例之后必须真的 resize 一次。Qt 不在手工改别的 section 时重算末列，
//    stretchLastSection 是在 updateGeometries() 里重新套用的，也就是下一次 resize。
TEST_F(ColumnWidth, LastColumnKeepsItsRatioShareWhenStretchLastSectionWasOn) {
    setUpView(4, 2);
    header()->setStretchLastSection(true);

    // 四份比例、两列：每列拿四分之一，视口有一半空着。
    view_->setColumnWidthRatios({1, 1, 1, 1});
    view_->resize(620, 320);
    QCoreApplication::processEvents();

    // 没关掉 stretchLastSection 的话，末列会从 1/4 被拉成 3/4（实测 150 -> 450）。
    EXPECT_NEAR(view_->columnWidth(1), view_->columnWidth(0), 2);
    EXPECT_NEAR(view_->columnWidth(1), view_->viewport()->width() / 4, 2);
}

TEST_F(ColumnWidth, ClearingRatiosRestoresStretchLastSection) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);
    view_->setColumnWidthRatios({3, 1, 1});
    ASSERT_FALSE(header()->stretchLastSection());

    view_->setColumnWidthRatios({});

    EXPECT_TRUE(header()->stretchLastSection());
}

TEST_F(ColumnWidth, ClearingRatiosKeepsStretchLastSectionOffWhenCallerNeverSetIt) {
    setUpView(4, 3);
    ASSERT_FALSE(header()->stretchLastSection());  // QTableView 的默认值
    view_->setColumnWidthRatios({3, 1, 1});

    view_->setColumnWidthRatios({});

    EXPECT_FALSE(header()->stretchLastSection());
}

TEST_F(ColumnWidth, StretchToFillRestoresStretchLastSection) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);
    view_->setColumnWidthRatios({3, 1, 1});
    ASSERT_FALSE(header()->stretchLastSection());

    view_->setStretchToFill(true);

    EXPECT_TRUE(header()->stretchLastSection());
}

// --- 2. resizeEvent 必须先调基类 -----------------------------------------------------

TEST_F(ColumnWidth, HeightOnlyResizeInRatioModeUpdatesScrollBarRange) {
    setUpView(200, 2, 480, 600);
    view_->setColumnWidthRatios({1, 1});
    QCoreApplication::processEvents();
    const int tall_maximum = view_->verticalScrollBar()->maximum();

    // 只改高度：列宽不变，不会有 sectionResized，所以滚动条范围只能靠基类的
    // resizeEvent -> updateGeometries() 来更新。
    view_->resize(480, 120);
    QCoreApplication::processEvents();

    EXPECT_GT(view_->verticalScrollBar()->maximum(), tall_maximum);
}

TEST_F(ColumnWidth, WidthResizeInRatioModeKeepsColumnsFillingViewport) {
    setUpView(10, 4, 480, 320);
    view_->setColumnWidthRatios({1, 1, 1, 1});

    view_->resize(900, 320);
    QCoreApplication::processEvents();

    int total = 0;
    for (int width : columnWidths()) total += width;
    // 整除截断最多让总宽比视口少 (列数 - 1) 个像素。
    EXPECT_LE(total, view_->viewport()->width());
    EXPECT_GE(total, view_->viewport()->width() - 4);
}

// --- 4. autoFitColumns ---------------------------------------------------------------

TEST_F(ColumnWidth, AutoFitColumnsWidensColumnForLongContent) {
    setUpView(6, 2);
    model_->setData(model_->index(0, 0),
                    QStringLiteral("一个明显比表头和其它单元格都长得多的内容字符串"));
    const int before = view_->columnWidth(0);

    view_->autoFitColumns();

    EXPECT_GT(view_->columnWidth(0), before);
    EXPECT_GT(view_->columnWidth(0), view_->columnWidth(1));
}

TEST_F(ColumnWidth, AutoFitColumnsKeepsResizeContentsPrecision) {
    setUpView(6, 2);
    const int precision_before = header()->resizeContentsPrecision();
    ASSERT_NE(precision_before, 16);

    view_->autoFitColumns(16);

    EXPECT_EQ(header()->resizeContentsPrecision(), precision_before);
}

TEST_F(ColumnWidth, AutoFitColumnsKeepsColumnsInteractive) {
    setUpView(6, 2);

    view_->autoFitColumns();

    // 量宽是一次性的，不能把列锁成 ResizeToContents，否则用户拖不动。
    EXPECT_EQ(header()->sectionResizeMode(0), QHeaderView::Interactive);
}

TEST_F(ColumnWidth, AutoFitColumnsIsNoOpInRatioMode) {
    setUpView(6, 3);
    view_->setColumnWidthRatios({3, 1, 1});
    QCoreApplication::processEvents();
    model_->setData(model_->index(0, 1),
                    QStringLiteral("一个明显比表头和其它单元格都长得多的内容字符串"));
    const QList<int> before = columnWidths();

    view_->autoFitColumns();

    EXPECT_EQ(columnWidths(), before);
}

TEST_F(ColumnWidth, AutoFitColumnsIsNoOpInStretchToFillMode) {
    setUpView(6, 3);
    view_->setStretchToFill(true);
    QCoreApplication::processEvents();

    view_->autoFitColumns();

    EXPECT_EQ(header()->sectionResizeMode(0), QHeaderView::Stretch);
    EXPECT_NEAR(view_->columnWidth(0), view_->columnWidth(2), 2);
}

// --- 3. UI 状态升到 version 2 --------------------------------------------------------

TEST_F(ColumnWidth, SaveUiStateKeepsCallerStretchLastSectionInRatioMode) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);
    view_->setColumnWidthRatios({2, 1, 1});
    ASSERT_FALSE(header()->stretchLastSection());

    const QJsonObject state = view_->saveUiState();

    EXPECT_EQ(state.value("version").toInt(), 2);
    // 存的必须是调用方原本的 true，而不是比例模式临时改成的 false。
    EXPECT_TRUE(state.value("stretchLastSection").toBool());
}

TEST_F(ColumnWidth, RestoreUiStateReappliesStretchLastSection) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);
    const QJsonObject state = view_->saveUiState();
    ASSERT_TRUE(state.value("stretchLastSection").toBool());

    xTableView target;
    QStandardItemModel target_model(4, 3);
    target.setSourceModel(&target_model);
    ASSERT_FALSE(target.horizontalHeader()->stretchLastSection());

    target.restoreUiState(state);

    EXPECT_TRUE(target.horizontalHeader()->stretchLastSection());
}

TEST_F(ColumnWidth, RestoreUiStateInRatioModeStillHoldsCallerStretchLastSection) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);
    view_->setColumnWidthRatios({2, 1, 1});
    const QJsonObject state = view_->saveUiState();

    xTableView target;
    QStandardItemModel target_model(4, 3);
    target.setSourceModel(&target_model);
    target.resize(480, 320);
    target.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&target));

    target.restoreUiState(state);

    // 恢复出来仍在比例模式，所以表头上是关着的……
    EXPECT_FALSE(target.horizontalHeader()->stretchLastSection());
    // ……但调用方的原值没丢，退出比例模式就回来了。
    target.setColumnWidthRatios({});
    EXPECT_TRUE(target.horizontalHeader()->stretchLastSection());
}

TEST_F(ColumnWidth, RestoreUiStateFromVersionOneLeavesStretchLastSectionAlone) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);

    QJsonObject legacy;
    legacy["version"] = 1;
    legacy["freezeColumns"] = 0;
    view_->restoreUiState(legacy);

    // v1 的状态里没有 stretchLastSection 字段，升版本前后行为必须一致：不碰它。
    EXPECT_TRUE(header()->stretchLastSection());
}

TEST_F(ColumnWidth, RestoreUiStateIgnoresFutureVersions) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);

    QJsonObject future;
    future["version"] = 3;
    future["stretchLastSection"] = false;
    view_->restoreUiState(future);

    EXPECT_TRUE(header()->stretchLastSection());
}

// --- 5. 等宽可调（setProportionalColumns）-------------------------------------------------
//
// 语义：表头保持 Interactive（用户能拖），列宽初值是视口等分，resize 时按各列
// 当前宽度的比例缩放，所以用户拖出来的比例不会被自动逻辑覆盖，视口也不留白。

TEST_F(ColumnWidth, ProportionalColumnsStartOutEqualAndFillViewport) {
    setUpView(6, 4, 480, 320);

    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    EXPECT_TRUE(view_->isProportionalColumns());
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
    for (int col = 0; col < 4; ++col) {
        EXPECT_NEAR(view_->columnWidth(col), viewportWidth() / 4, 1) << "col " << col;
    }
}

TEST_F(ColumnWidth, ProportionalColumnsKeepColumnsDraggable) {
    setUpView(6, 4, 480, 320);

    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    // 不能用 QHeaderView::Stretch：那下面用户拖不动，setColumnWidth 也静默无效。
    EXPECT_EQ(header()->sectionResizeMode(0), QHeaderView::Interactive);
    dragColumnTo(0, 240);
    EXPECT_NEAR(view_->columnWidth(0), 240, 2);
}

TEST_F(ColumnWidth, ProportionalColumnsTurnOffStretchLastSection) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);

    view_->setProportionalColumns(true);

    // 这个模式自己把列宽铺满视口，stretchLastSection 只会把末列拉变形。
    EXPECT_FALSE(header()->stretchLastSection());
}

TEST_F(ColumnWidth, LeavingProportionalColumnsRestoresStretchLastSection) {
    setUpView(4, 3);
    header()->setStretchLastSection(true);
    view_->setProportionalColumns(true);
    ASSERT_FALSE(header()->stretchLastSection());

    view_->setProportionalColumns(false);

    EXPECT_TRUE(header()->stretchLastSection());
}

TEST_F(ColumnWidth, ProportionalColumnsRefillViewportAfterResize) {
    setUpView(6, 4, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    // 903 除不尽 4：逐列各自取整会每列差一点，总宽就铺不满视口。
    resizeViewTo(903);

    EXPECT_EQ(totalColumnWidth(), viewportWidth());
    for (int col = 0; col < 4; ++col) {
        EXPECT_NEAR(view_->columnWidth(col), viewportWidth() / 4, 1) << "col " << col;
    }
}

TEST_F(ColumnWidth, ProportionalColumnsKeepUserProportionsAcrossResize) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    dragColumnTo(0, viewportWidth() / 2);  // 用户把第一列拖到半个视口
    const double share_before = columnShare(0);
    ASSERT_NEAR(share_before, 0.5, 0.02);

    resizeViewTo(900);

    // 固定比例模式的做法是每次 resize 都按设定值重算，用户拖的那下就没了；
    // 这个模式按"当前比例"缩放，所以 1/2 还是 1/2。
    EXPECT_NEAR(columnShare(0), share_before, 0.01);
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
}

TEST_F(ColumnWidth, ProportionalColumnsDoNotDriftAcrossRepeatedResizes) {
    setUpView(6, 5, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    dragColumnTo(0, viewportWidth() * 3 / 5);
    const double share_before = columnShare(0);
    ASSERT_NEAR(share_before, 0.6, 0.02);

    // 比例必须存 double：每次 resize 都从整数宽度反推比例的写法，整除误差会一次次
    // 累积（窄列每次少一两个像素，占比就慢慢往宽列跑），来回缩几十次就看得出来。
    for (int pass = 0; pass < 40; ++pass) {
        resizeViewTo(pass % 2 == 0 ? 907 : 503);
    }
    resizeViewTo(480);

    EXPECT_NEAR(columnShare(0), share_before, 0.005);
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
}

TEST_F(ColumnWidth, ProportionalColumnsKeepManuallySizedColumnWhenAnotherIsDragged) {
    setUpView(6, 4, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();
    const int total = viewportWidth();

    dragColumnTo(0, total / 2);
    const int manual_width = view_->columnWidth(0);
    ASSERT_NEAR(manual_width, total / 2, 2);

    dragColumnTo(1, total / 4);

    // 第 0 列是用户亲手拖过的，差额只能从没拖过的列（2、3）身上出。
    EXPECT_NEAR(view_->columnWidth(0), manual_width, 2);
    EXPECT_NEAR(view_->columnWidth(1), total / 4, 2);
    EXPECT_EQ(totalColumnWidth(), total);
}

TEST_F(ColumnWidth, ProportionalColumnsShareViewportWithColumnsAddedLater) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    model_->insertColumn(3);
    resizeViewTo(900);

    EXPECT_EQ(totalColumnWidth(), viewportWidth());
    for (int col = 0; col < 4; ++col) {
        EXPECT_NEAR(view_->columnWidth(col), viewportWidth() / 4, 1) << "col " << col;
    }

    model_->removeColumn(3);
    resizeViewTo(640);

    EXPECT_EQ(totalColumnWidth(), viewportWidth());
    for (int col = 0; col < 3; ++col) {
        EXPECT_NEAR(view_->columnWidth(col), viewportWidth() / 3, 1) << "col " << col;
    }
}

// --- 5b. 等宽可调：列数变化与隐藏列（PR #17 评审意见）------------------------------
//
// 这一组都刻意不在操作后面跟 resize —— 原来的实现要靠一次 resize 才会重排，
// 而插入列时恰好会因为总宽溢出、横向滚动条出现而间接触发一次 resize，
// 所以"操作完立刻 resize"的测试看不出问题来。

// 注意：这一条不是严格的回归测试 —— 不接模型信号时它也是绿的。插入列会让旧布局的总宽
// 溢出视口，横向滚动条一出现就带来一次 resize，于是间接触发了重排。真正能把"没接模型信号"
// 抓出来的是下面删除列和中间插列的几条。留着它是因为这个行为本身值得断言。
TEST_F(ColumnWidth, ProportionalColumnsRefillViewportAfterColumnInsertedWithoutResize) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    model_->insertColumn(3);
    QCoreApplication::processEvents();

    EXPECT_EQ(totalColumnWidth(), viewportWidth());
    for (int col = 0; col < 4; ++col) {
        EXPECT_NEAR(view_->columnWidth(col), viewportWidth() / 4, 2) << "col " << col;
    }
}

TEST_F(ColumnWidth, ProportionalColumnsRefillViewportAfterColumnRemovedWithoutResize) {
    setUpView(6, 4, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    model_->removeColumn(3);
    QCoreApplication::processEvents();

    EXPECT_EQ(totalColumnWidth(), viewportWidth());
    for (int col = 0; col < 3; ++col) {
        EXPECT_NEAR(view_->columnWidth(col), viewportWidth() / 3, 2) << "col " << col;
    }
}

// 中间插列会让逻辑列号整段右移，权重必须跟着搬，否则用户拖出来的宽度会跑到隔壁列去。
TEST_F(ColumnWidth, ProportionalWeightsFollowTheirColumnWhenOneIsInsertedInTheMiddle) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    dragColumnTo(2, viewportWidth() * 3 / 5);  // 把最后一列拖到 3/5
    ASSERT_NEAR(columnShare(2), 0.6, 0.02);
    // 此时三列的比例是 1 : 1 : 3。

    model_->insertColumn(1);  // 插在中间：原来的 col1/col2 变成 col2/col3
    QCoreApplication::processEvents();

    // 新列要从大家身上分走宽度，所以各列的绝对占比都会缩（0.6 -> 0.45），
    // 但"原来那一列仍是最宽的、且是 col0 的 3 倍"这个相对关系必须跟着列走到 col3。
    // 权重没跟着搬的话，这 3 倍会留在 col2 上，col3 拿到的是新列的平均份。
    EXPECT_NEAR(columnShare(3), columnShare(0) * 3, 0.03);
    EXPECT_GT(view_->columnWidth(3), view_->columnWidth(2));
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
}

TEST_F(ColumnWidth, ProportionalManualColumnsFollowTheirColumnWhenOneIsRemovedInTheMiddle) {
    setUpView(6, 4, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    dragColumnTo(3, viewportWidth() / 2);  // 末列设成手动列
    ASSERT_NEAR(columnShare(3), 0.5, 0.02);
    // 此时四列的比例是 1 : 1 : 1 : 3。

    model_->removeColumn(1);  // 删中间一列：原来的 col3 变成 col2
    QCoreApplication::processEvents();
    // 删掉的那一列的宽度分给其余列，所以绝对占比会涨（0.5 -> 0.6）；
    // 要看的是 3 倍这个相对关系跟着列走到了 col2。
    ASSERT_NEAR(columnShare(2), columnShare(0) * 3, 0.03);

    // 手动标记也该跟着搬到 col2：拖别的列时它的宽度不该被动。
    const int manual_width = view_->columnWidth(2);
    dragColumnTo(0, viewportWidth() / 4);

    EXPECT_EQ(view_->columnWidth(2), manual_width);
}

// 隐藏一列之后，剩下的列要把空出来的宽度分掉，不能留一块空白。
TEST_F(ColumnWidth, ProportionalColumnsRefillViewportWhenAColumnIsHidden) {
    setUpView(6, 4, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    view_->setColumnHidden(1, true);
    QCoreApplication::processEvents();

    const int visible = view_->columnWidth(0) + view_->columnWidth(2) + view_->columnWidth(3);
    EXPECT_EQ(visible, viewportWidth());
    EXPECT_NEAR(view_->columnWidth(0), viewportWidth() / 3, 2);
}

// 重新显示不是拖拽：按拖拽处理会把这一列错记成手动列，从此不再参与自动分配。
TEST_F(ColumnWidth, ShowingAHiddenColumnDoesNotMarkItManual) {
    setUpView(6, 4, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    view_->setColumnHidden(1, true);
    QCoreApplication::processEvents();
    view_->setColumnHidden(1, false);
    QCoreApplication::processEvents();
    ASSERT_EQ(totalColumnWidth(), viewportWidth());

    // 把 col0 拖到半个视口：col1 没被拖过，应当参与吸收差额、跟着变窄。
    const int w1_before = view_->columnWidth(1);
    dragColumnTo(0, viewportWidth() / 2);

    EXPECT_LT(view_->columnWidth(1), w1_before);
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
}

// 隐藏期间权重留着，列回来还是原来那一份。
TEST_F(ColumnWidth, HidingAndShowingAColumnKeepsItsProportion) {
    setUpView(6, 4, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    dragColumnTo(0, viewportWidth() / 2);
    const double share_before = columnShare(0);
    ASSERT_NEAR(share_before, 0.5, 0.02);

    view_->setColumnHidden(2, true);
    QCoreApplication::processEvents();
    view_->setColumnHidden(2, false);
    QCoreApplication::processEvents();

    EXPECT_NEAR(columnShare(0), share_before, 0.02);
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
}

// 缩窗时必须先给后面每一列留出 minimumSectionSize，否则前面的宽列按权重把空间吃光、
// 后面的列被顶到最小宽度，多分出去的部分收不回来，总宽就超出视口冒出横向滚动条 ——
// 而那些最小宽度本来是放得下的。
TEST_F(ColumnWidth, ProportionalColumnsReserveMinimumWidthForLaterColumns) {
    setUpView(6, 4, 900, 320);
    header()->setMinimumSectionSize(50);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    dragColumnTo(0, viewportWidth() * 7 / 10);  // 第 0 列约占 70%
    ASSERT_NEAR(columnShare(0), 0.7, 0.03);

    resizeViewTo(420);
    ASSERT_GE(viewportWidth(), 50 * 4);  // 前提：四列的最小宽度放得下

    EXPECT_EQ(totalColumnWidth(), viewportWidth());
    for (int col = 0; col < 4; ++col) {
        EXPECT_GE(view_->columnWidth(col), 50) << "col " << col;
    }
}

// 隐藏期间缩放窗口，重新显示后该列的占比仍是隐藏前那一份。
TEST_F(ColumnWidth, HidingResizingThenShowingAColumnKeepsItsProportion) {
    setUpView(6, 4, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    dragColumnTo(0, viewportWidth() / 2);
    const double share_before = columnShare(0);
    ASSERT_NEAR(share_before, 0.5, 0.02);

    view_->setColumnHidden(2, true);
    QCoreApplication::processEvents();
    resizeViewTo(820);  // 隐藏期间缩放
    view_->setColumnHidden(2, false);
    QCoreApplication::processEvents();

    EXPECT_NEAR(columnShare(0), share_before, 0.02);
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
}

// --- 6. 三种列宽模式互斥 ---------------------------------------------------------

TEST_F(ColumnWidth, StretchToFillTakesOverFromProportionalColumns) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    view_->setStretchToFill(true);

    EXPECT_FALSE(view_->isProportionalColumns());
    EXPECT_EQ(header()->sectionResizeMode(0), QHeaderView::Stretch);
}

TEST_F(ColumnWidth, ProportionalColumnsTakeOverFromStretchToFill) {
    setUpView(6, 3, 480, 320);
    view_->setStretchToFill(true);
    QCoreApplication::processEvents();

    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    EXPECT_EQ(header()->sectionResizeMode(0), QHeaderView::Interactive);
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
    dragColumnTo(0, 240);
    EXPECT_NEAR(view_->columnWidth(0), 240, 2);
}

TEST_F(ColumnWidth, ProportionalColumnsTakeOverFromColumnWidthRatios) {
    setUpView(6, 3, 480, 320);
    view_->setColumnWidthRatios({6, 1, 1});
    QCoreApplication::processEvents();

    view_->setProportionalColumns(true);
    resizeViewTo(900);

    // 比例被清了：如果还在，resizeEvent 会把列宽压回 6:1:1。
    EXPECT_NEAR(view_->columnWidth(0), view_->columnWidth(1), 2);
    EXPECT_NEAR(view_->columnWidth(0), viewportWidth() / 3, 2);
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
    EXPECT_TRUE(view_->saveUiState().value("columnWidthRatios").toArray().isEmpty());

    // 是清掉，不是盖住：再关掉这个模式，老的 6:1:1 也不该再冒出来。
    const int equal_width = view_->columnWidth(0);
    view_->setProportionalColumns(false);
    resizeViewTo(700);

    EXPECT_NEAR(view_->columnWidth(0), equal_width, 2);
    EXPECT_NEAR(view_->columnWidth(1), equal_width, 2);
}

TEST_F(ColumnWidth, ProportionalColumnsClearStretchToFillInUiState) {
    setUpView(6, 3, 480, 320);
    view_->setStretchToFill(true);

    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    const QJsonObject state = view_->saveUiState();
    EXPECT_TRUE(state.value("proportionalColumns").toBool());
    // 三种模式互斥，状态里不能同时写着两个：restore 时的
    // setStretchToFill(true) 会把等宽可调模式又顶掉。
    EXPECT_FALSE(state.value("stretchToFill").toBool());
}

TEST_F(ColumnWidth, ColumnWidthRatiosTakeOverFromProportionalColumns) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();
    dragColumnTo(0, viewportWidth() / 2);
    ASSERT_NEAR(columnShare(0), 0.5, 0.02);

    view_->setColumnWidthRatios({1, 1, 1});
    resizeViewTo(900);

    EXPECT_FALSE(view_->isProportionalColumns());
    // 比例模式说了算：用户拖出来的 1/2 不再生效。
    EXPECT_NEAR(view_->columnWidth(0), view_->columnWidth(1), 2);
}

TEST_F(ColumnWidth, ClearingColumnWidthRatiosKeepsProportionalColumnsOn) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    // setColumnWidthRatios({}) 只是清比例，不该顺手把另一个模式也关掉。
    view_->setColumnWidthRatios({});
    resizeViewTo(900);

    EXPECT_TRUE(view_->isProportionalColumns());
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
}

TEST_F(ColumnWidth, AutoFitColumnsIsNoOpInProportionalColumnsMode) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();
    model_->setData(model_->index(0, 1),
                    QStringLiteral("一个明显比表头和其它单元格都长得多的内容字符串"));
    const QList<int> before = columnWidths();

    view_->autoFitColumns();

    // 量宽走的也是 setColumnWidth，在这个模式下会被当成用户拖拽，
    // 把每一列都打上手动标记，所以这里什么都不做。
    EXPECT_EQ(columnWidths(), before);
}

// v1 的状态里没有 stretchLastSection 字段，但它的 horizontalHeaderState 里带着这个标志。
// 恢复到一个已经处于比例模式的视图上时，记账（stretch_last_section_saved_）必须按刚恢复
// 出来的值重新捕获，否则以后退出比例模式时还原的是 restore 之前那个过期的设置。
TEST_F(ColumnWidth, RestoreUiStateFromVersionOneRefreshesSavedStretchLastSection) {
    // 源视图：stretchLastSection 开着，存一份状态再按 v1 的样子裁掉新字段，
    // 但保留一组列宽比例，这样恢复之后目标视图仍然留在比例模式里。
    setUpView(4, 3);
    header()->setStretchLastSection(true);
    QJsonObject legacy = view_->saveUiState();
    legacy.remove("stretchLastSection");
    legacy.remove("proportionalColumns");
    legacy.remove("proportionalColumnWeights");
    legacy.remove("proportionalManualColumns");
    legacy["version"] = 1;
    legacy["columnWidthRatios"] = QJsonArray({2, 1, 1});

    // 目标视图：stretchLastSection 原本是关的，并且已经进了比例模式
    // （于是记账里存着 false）。
    xTableView target;
    QStandardItemModel target_model(4, 3);
    target.setSourceModel(&target_model);
    target.resize(480, 320);
    target.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&target));
    ASSERT_FALSE(target.horizontalHeader()->stretchLastSection());
    target.setColumnWidthRatios({3, 1, 1});

    target.restoreUiState(legacy);
    ASSERT_FALSE(target.horizontalHeader()->stretchLastSection());  // 比例模式期间仍是关的

    target.setColumnWidthRatios({});  // 退出比例模式，还原"调用方原本的值"

    // 还原出来的必须是 v1 状态带进来的 true，而不是 restore 之前那个 false。
    EXPECT_TRUE(target.horizontalHeader()->stretchLastSection());
}

// --- 7. UI 状态存取 ---------------------------------------------------------------

TEST_F(ColumnWidth, SaveUiStateKeepsProportionalColumnWeightsAsDoubles) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();
    dragColumnTo(0, viewportWidth() / 2);

    const QJsonObject state = view_->saveUiState();

    // 新字段都是可选的，版本号留在 2：升到 3 会被老代码的 version > 2 整份丢掉。
    EXPECT_EQ(state.value("version").toInt(), 2);
    EXPECT_TRUE(state.value("proportionalColumns").toBool());

    const QJsonArray weights = state.value("proportionalColumnWeights").toArray();
    ASSERT_EQ(weights.size(), 3);
    EXPECT_NEAR(weights.at(0).toDouble(), 0.5, 0.02);
    // 存的必须是 double 比例，不是取整过的像素宽度。
    EXPECT_LT(weights.at(0).toDouble(), 1.0);
    EXPECT_NE(weights.at(1).toDouble(), std::floor(weights.at(1).toDouble()));

    const QJsonArray manual = state.value("proportionalManualColumns").toArray();
    ASSERT_EQ(manual.size(), 1);
    EXPECT_EQ(manual.at(0).toInt(), 0);
}

TEST_F(ColumnWidth, RestoreUiStateReappliesProportionalColumns) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();
    dragColumnTo(0, viewportWidth() / 2);
    const QJsonObject state = view_->saveUiState();

    xTableView target;
    QStandardItemModel target_model(6, 3);
    target.setSourceModel(&target_model);
    target.resize(900, 320);
    target.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&target));

    target.restoreUiState(state);
    QCoreApplication::processEvents();

    EXPECT_TRUE(target.isProportionalColumns());
    EXPECT_EQ(target.horizontalHeader()->sectionResizeMode(0), QHeaderView::Interactive);
    EXPECT_FALSE(target.horizontalHeader()->stretchLastSection());
    // 存下来的是比例，换了一个更宽的表也该是半个视口。
    EXPECT_NEAR(static_cast<double>(target.columnWidth(0)) / target.viewport()->width(), 0.5,
                0.02);
    int total = 0;
    for (int col = 0; col < 3; ++col) total += target.columnWidth(col);
    EXPECT_EQ(total, target.viewport()->width());
}

TEST_F(ColumnWidth, RestoreUiStateKeepsProportionalManualColumns) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();
    dragColumnTo(0, viewportWidth() / 2);
    const QJsonObject state = view_->saveUiState();

    xTableView target;
    QStandardItemModel target_model(6, 3);
    target.setSourceModel(&target_model);
    target.resize(900, 320);
    target.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&target));
    target.restoreUiState(state);
    QCoreApplication::processEvents();
    const int restored = target.columnWidth(0);

    // 手动标记也跟着恢复了的话，再拖别的列时第 0 列不当"吸收差额"的角色。
    target.horizontalHeader()->resizeSection(1, 100);
    QCoreApplication::processEvents();

    EXPECT_NEAR(target.columnWidth(0), restored, 2);
    EXPECT_NEAR(target.columnWidth(1), 100, 2);
}

TEST_F(ColumnWidth, RestoreUiStateTurnsProportionalColumnsOffWhenStateHasNone) {
    setUpView(6, 3, 480, 320);
    const QJsonObject state = view_->saveUiState();  // 默认模式存下来的状态
    ASSERT_FALSE(state.value("proportionalColumns").toBool());

    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();
    ASSERT_TRUE(view_->isProportionalColumns());

    view_->restoreUiState(state);

    EXPECT_FALSE(view_->isProportionalColumns());
}

TEST_F(ColumnWidth, RestoreUiStateFromVersionOneLeavesProportionalColumnsAlone) {
    setUpView(6, 3, 480, 320);
    view_->setProportionalColumns(true);
    QCoreApplication::processEvents();

    QJsonObject legacy;
    legacy["version"] = 1;
    legacy["freezeColumns"] = 0;
    view_->restoreUiState(legacy);
    resizeViewTo(900);

    // v1 的状态里没有这个字段，和 stretchLastSection 一样：不碰当前模式。
    EXPECT_TRUE(view_->isProportionalColumns());
    EXPECT_EQ(totalColumnWidth(), viewportWidth());
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
