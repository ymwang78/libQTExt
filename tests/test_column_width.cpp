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
#include <QJsonObject>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTest>

#include <memory>

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

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
