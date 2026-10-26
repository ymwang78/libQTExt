// ***************************************************************
//  GTest coverage: xTableView 的浮点显示模式与精度
// ***************************************************************
//
// 三档模式（xTableView::NUMBER_DISPLAY_MODE）都由 xItemDelegate::displayText() 落地，
// 视图这边只是把 (mode, precision) 透传给委托。这里盖住的是：
//
//  1. MODE_SCIENTIFIC 的精度参数真的生效。格式串以前拼成 "%+6.e"，6 被 printf 当成最小
//     字段宽度，而小数点后不跟数字等于精度 0，于是不管要几位，12.3456789 一律打成
//     "+1e+01"（只有一位有效数字）。位数要写在小数点后面。
//  2. MODE_FIXFLOAT 按小数位数走，MODE_GENERAL 按有效数字走，精度给 0 时各自回落到缺省。
//  3. 科学计数法始终带符号，指数至少两位，并且位数要几位给几位 —— 不像另外两档把精度
//     交给 QLocale，这一档自己管缓冲区，位数一大就有被 snprintf 悄悄截断的风险。
//  4. 非浮点类型不受模式影响。
//  5. setNumberDisplayMode() 只在值真的变了时发 numberDisplayChanged（调用方拿它触发
//     UI 状态落盘），saveUiState()/restoreUiState() 能把模式和精度原样带回来。
#include "xItemDelegate.h"
#include "xTableView.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QJsonObject>
#include <QLocale>
#include <QLineEdit>
#include <QWidget>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QVariant>

#include <memory>
#include <string>

namespace {

void ensureApplication() {
    if (QApplication::instance() != nullptr) {
        return;
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    static int argc = 1;
    static char arg0[] = "test_number_display";
    static char* argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
}

class NumberDisplay : public ::testing::Test {
  protected:
    void SetUp() override {
        ensureApplication();
        delegate_ = std::make_unique<xItemDelegate>();
    }

    void TearDown() override { delegate_.reset(); }

    // 一律用 C locale 问委托：两档 locale 格式化会跟着机器的小数点/千分位设置漂，
    // 测试不该赌开发机的区域设置。返回 std::string 是为了断言失败时能直接看到字符串，
    // QString 会被 gtest 打成一串 "2-byte object"。
    std::string displayed(const QVariant& value, int mode, int precision) {
        delegate_->setRealNumberShowMode(mode, precision);
        return delegate_->displayText(value, QLocale::c()).toStdString();
    }

    std::unique_ptr<xItemDelegate> delegate_;
};

TEST_F(NumberDisplay, ScientificModeKeepsRequestedDecimalPlaces) {
    EXPECT_EQ(displayed(12.3456789, xTableView::MODE_SCIENTIFIC, 5), "+1.23457e+01");
    EXPECT_EQ(displayed(12.3456789, xTableView::MODE_SCIENTIFIC, 6), "+1.234568e+01");
    EXPECT_EQ(displayed(12.3456789, xTableView::MODE_SCIENTIFIC, 2), "+1.23e+01");
}

TEST_F(NumberDisplay, ScientificModeFallsBackToSixDecimalPlacesWhenPrecisionIsUnset) {
    // 委托构造出来 precision 就是 0，别让它退化成一位有效数字。
    EXPECT_EQ(displayed(12.3456789, xTableView::MODE_SCIENTIFIC, 0), "+1.234568e+01");
}

TEST_F(NumberDisplay, ScientificModeAlwaysShowsSignAndAtLeastTwoExponentDigits) {
    EXPECT_EQ(displayed(1.5, xTableView::MODE_SCIENTIFIC, 5), "+1.50000e+00");
    EXPECT_EQ(displayed(-0.000123456, xTableView::MODE_SCIENTIFIC, 5), "-1.23456e-04");
    // 三位指数不该被补零的那段代码碰坏。
    EXPECT_EQ(displayed(1.0e+100, xTableView::MODE_SCIENTIFIC, 5), "+1.00000e+100");
}

TEST_F(NumberDisplay, ScientificModeHonorsPrecisionBeyondAFixedBuffer) {
    // 1.5 在二进制里是精确值，所以第一位小数之后全是 0，这里盯的只是位数本身没被截断。
    // 缓冲区按位数算之前，这一档的位数先被砍到 24、缓冲区又写死成 64 字节：要 25 位实际
    // 只画 24 位，要 60 位则连指数都被截掉，而 getRealNumberPrecision() 照样报 25 / 60。
    EXPECT_EQ(displayed(1.5, xTableView::MODE_SCIENTIFIC, 25),
              "+1.5" + std::string(24, '0') + "e+00");
    EXPECT_EQ(displayed(1.5, xTableView::MODE_SCIENTIFIC, 60),
              "+1.5" + std::string(59, '0') + "e+00");
}

TEST_F(NumberDisplay, FixFloatModeKeepsRequestedDecimalPlaces) {
    EXPECT_EQ(displayed(12.3456789, xTableView::MODE_FIXFLOAT, 5), "12.34568");
    EXPECT_EQ(displayed(1.5, xTableView::MODE_FIXFLOAT, 5), "1.50000");
    // 比末位还小的量会被压成 0——固定小数位数的代价，不是 bug。
    EXPECT_EQ(displayed(0.000001, xTableView::MODE_FIXFLOAT, 5), "0.00000");
}

TEST_F(NumberDisplay, GeneralModeCountsSignificantDigitsAndFallsBackToEight) {
    EXPECT_EQ(displayed(12.3456789, xTableView::MODE_GENERAL, 0), "12.345679");
    EXPECT_EQ(displayed(12.3456789, xTableView::MODE_GENERAL, 3), "12.3");
}

TEST_F(NumberDisplay, FloatValuesAreFormattedLikeDoubles) {
    EXPECT_EQ(displayed(QVariant(1.5f), xTableView::MODE_FIXFLOAT, 5), "1.50000");
    EXPECT_EQ(displayed(QVariant(1.5f), xTableView::MODE_SCIENTIFIC, 5), "+1.50000e+00");
}

TEST_F(NumberDisplay, NonFloatingPointValuesAreNotReformatted) {
    EXPECT_EQ(displayed(QVariant(42), xTableView::MODE_FIXFLOAT, 5), "42");
    EXPECT_EQ(displayed(QVariant(QStringLiteral("12.3456789")), xTableView::MODE_FIXFLOAT, 5),
              "12.3456789");
}

TEST_F(NumberDisplay, NumericEditorUsesPrecisionWithoutSubmittingRounding) {
    QStandardItemModel model(1, 1);
    const QModelIndex index = model.index(0, 0);
    const double original = 32.517143969477;
    model.setData(index, original);
    delegate_->setRealNumberShowMode(xTableView::MODE_FIXFLOAT, 4);
    QWidget parent;
    std::unique_ptr<QWidget> editor(delegate_->createEditor(&parent, {}, index));
    auto* lineEdit = qobject_cast<QLineEdit*>(editor.get());
    ASSERT_NE(lineEdit, nullptr);
    delegate_->setEditorData(editor.get(), index);
    EXPECT_EQ(lineEdit->text(), QStringLiteral("32.5171"));

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    delegate_->setModelData(editor.get(), &model, index);
    EXPECT_EQ(changed.count(), 0);
    EXPECT_DOUBLE_EQ(index.data(Qt::EditRole).toDouble(), original);
    lineEdit->setText(QStringLiteral("33.1250"));
    delegate_->setModelData(editor.get(), &model, index);
    EXPECT_DOUBLE_EQ(index.data(Qt::EditRole).toDouble(), 33.125);
    EXPECT_EQ(changed.count(), 1);
}

TEST_F(NumberDisplay, UneditedRoundedTextDoesNotOverwriteNewServerValue) {
    QStandardItemModel model(1, 1);
    const QModelIndex index = model.index(0, 0);
    model.setData(index, 1.23456789);
    delegate_->setRealNumberShowMode(xTableView::MODE_FIXFLOAT, 4);
    QWidget parent;
    std::unique_ptr<QWidget> editor(delegate_->createEditor(&parent, {}, index));
    delegate_->setEditorData(editor.get(), index);
    model.setData(index, 9.87654321);
    delegate_->setModelData(editor.get(), &model, index);
    EXPECT_DOUBLE_EQ(index.data(Qt::EditRole).toDouble(), 9.87654321);
}

TEST_F(NumberDisplay, ScientificEditorKeepsTinyValuesWithoutGroupingSeparators) {
    QStandardItemModel model(1, 1);
    const QModelIndex index = model.index(0, 0);
    model.setData(index, 0.0000123456789);
    delegate_->setRealNumberShowMode(xTableView::MODE_SCIENTIFIC, 4);
    QWidget parent;
    std::unique_ptr<QWidget> editor(delegate_->createEditor(&parent, {}, index));
    delegate_->setEditorData(editor.get(), index);
    auto* lineEdit = qobject_cast<QLineEdit*>(editor.get());
    ASSERT_NE(lineEdit, nullptr);
    EXPECT_EQ(lineEdit->text(), QStringLiteral("+1.2346e-05"));
    EXPECT_TRUE(lineEdit->hasAcceptableInput());
    model.setData(index, 12345.67891);
    delegate_->setRealNumberShowMode(xTableView::MODE_FIXFLOAT, 4);
    delegate_->setEditorData(editor.get(), index);
    EXPECT_EQ(lineEdit->text(), QStringLiteral("12345.6789"));
}

TEST_F(NumberDisplay, ScientificAnyEditorPreservesDoubleTypeOnCommit) {
    QStandardItemModel model(1, 1);
    const auto index = model.index(0, 0);
    model.setData(index, QVariant::fromValue(zce::Any(1.23e-5)));
    delegate_->setRealNumberShowMode(xTableView::MODE_SCIENTIFIC, 4);
    QWidget parent;
    std::unique_ptr<QWidget> editor(delegate_->createEditor(&parent, {}, index));
    auto* lineEdit = qobject_cast<QLineEdit*>(editor.get());
    ASSERT_NE(lineEdit, nullptr);
    delegate_->setEditorData(editor.get(), index);
    EXPECT_EQ(lineEdit->text(), QStringLiteral("+1.2300e-05"));
    lineEdit->setText(QStringLiteral("+1.5000e-05"));
    delegate_->setModelData(editor.get(), &model, index);
    const auto value = index.data(Qt::EditRole).value<zce::Any>();
    ASSERT_TRUE(value.is_double());
    EXPECT_DOUBLE_EQ(value.dbl(), 1.5e-5);
}

TEST_F(NumberDisplay, FixedAnyEditorPreservesDoubleTypeForIntegralValues) {
    QStandardItemModel model(1, 1);
    const auto index = model.index(0, 0);
    model.setData(index, QVariant::fromValue(zce::Any(1.5)));
    delegate_->setRealNumberShowMode(xTableView::MODE_FIXFLOAT, 4);
    QWidget parent;
    std::unique_ptr<QWidget> editor(delegate_->createEditor(&parent, {}, index));
    delegate_->setEditorData(editor.get(), index);
    auto* lineEdit = qobject_cast<QLineEdit*>(editor.get());
    ASSERT_NE(lineEdit, nullptr);
    lineEdit->setText(QStringLiteral("2"));
    delegate_->setModelData(editor.get(), &model, index);
    const auto value = index.data(Qt::EditRole).value<zce::Any>();
    ASSERT_TRUE(value.is_double());
    EXPECT_DOUBLE_EQ(value.dbl(), 2.0);
}

TEST_F(NumberDisplay, HighPrecisionEditorAcceptsInPlaceNumericChanges) {
    for (const int mode : {xTableView::MODE_FIXFLOAT, xTableView::MODE_SCIENTIFIC}) {
        for (const int precision : {25, 60}) {
            SCOPED_TRACE(::testing::Message() << "mode=" << mode << ", precision=" << precision);
            QStandardItemModel model(1, 1);
            const auto index = model.index(0, 0);
            model.setData(index, 1.5);
            delegate_->setRealNumberShowMode(mode, precision);
            QWidget parent;
            std::unique_ptr<QWidget> editor(delegate_->createEditor(&parent, {}, index));
            auto* lineEdit = qobject_cast<QLineEdit*>(editor.get());
            ASSERT_NE(lineEdit, nullptr);
            delegate_->setEditorData(editor.get(), index);
            ASSERT_TRUE(lineEdit->hasAcceptableInput());
            QString edited = lineEdit->text();
            edited[edited.indexOf('.') + 1] = QLatin1Char('6');
            lineEdit->setText(edited);
            ASSERT_TRUE(lineEdit->hasAcceptableInput());
            delegate_->setModelData(editor.get(), &model, index);
            EXPECT_DOUBLE_EQ(index.data(Qt::EditRole).toDouble(), 1.6);
        }
    }
}

TEST_F(NumberDisplay, SetNumberDisplayModeForwardsToDelegateAndSignalsOnlyOnChange) {
    xTableView view;
    QSignalSpy spy(&view, &xTableView::numberDisplayChanged);

    view.setNumberDisplayMode(xTableView::MODE_FIXFLOAT, 5);
    EXPECT_EQ(view.getNumberDisplayMode(), xTableView::MODE_FIXFLOAT);
    EXPECT_EQ(view.getNumberDisplayPrecision(), 5);
    ASSERT_EQ(spy.count(), 1);

    auto* delegate = qobject_cast<xItemDelegate*>(view.itemDelegate());
    ASSERT_NE(delegate, nullptr);
    EXPECT_EQ(delegate->getRealNumberMode(), xTableView::MODE_FIXFLOAT);
    EXPECT_EQ(delegate->getRealNumberPrecision(), 5);

    // 同一组值再设一次不发信号，免得 UI 状态被反复写盘。
    view.setNumberDisplayMode(xTableView::MODE_FIXFLOAT, 5);
    EXPECT_EQ(spy.count(), 1);

    // 只改精度也算变了。
    view.setNumberDisplayMode(xTableView::MODE_FIXFLOAT, 3);
    EXPECT_EQ(spy.count(), 2);
}

TEST_F(NumberDisplay, UiStateRoundTripsModeAndPrecision) {
    xTableView source;
    QStandardItemModel source_model(2, 2);
    source.setSourceModel(&source_model);
    source.setNumberDisplayMode(xTableView::MODE_SCIENTIFIC, 5);

    const QJsonObject state = source.saveUiState();
    EXPECT_EQ(state.value("numberDisplayMode").toInt(),
              static_cast<int>(xTableView::MODE_SCIENTIFIC));
    EXPECT_EQ(state.value("numberDisplayPrecision").toInt(), 5);

    xTableView target;
    QStandardItemModel target_model(2, 2);
    target.setSourceModel(&target_model);
    target.restoreUiState(state);

    EXPECT_EQ(target.getNumberDisplayMode(), xTableView::MODE_SCIENTIFIC);
    EXPECT_EQ(target.getNumberDisplayPrecision(), 5);
}

TEST_F(NumberDisplay, RestoreUiStateIgnoresAnOutOfRangeMode) {
    xTableView view;
    QStandardItemModel model(2, 2);
    view.setSourceModel(&model);
    view.setNumberDisplayMode(xTableView::MODE_FIXFLOAT, 5);

    QJsonObject state = view.saveUiState();
    state["numberDisplayMode"] = 99;
    view.restoreUiState(state);

    EXPECT_EQ(view.getNumberDisplayMode(), xTableView::MODE_FIXFLOAT);
    EXPECT_EQ(view.getNumberDisplayPrecision(), 5);
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
