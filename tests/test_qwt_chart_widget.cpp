// ***************************************************************
//  GTest coverage: xQwtChartWidget（从 KeelDeck 的 ChartWidget_QWT 搬来的趋势图部件）
// ***************************************************************
//
// 盖住的是它对使用方承诺过的四件事（前两件在 KeelDeck 里各有一个用例，随部件一起搬来）：
//
//  1. 四个轴的轴线（backbone）必须是关的：画布用的是 QwtPlotCanvas 默认的 Panel|Sunken
//     边框，轴线又在刻度控件那一侧各画一条，两者差开两个像素，界面上就是"双层框"。
//     真正容易回潮的是换过 scaleDraw 之后：setAxisScaleDraw() 把刻度绘制对象整个换掉，
//     各组件恢复成默认（轴线又开着），凡是换过的地方都得再调一次 hideAxisBackbones()。
//  2. updateTitlePosition() 的重入保护：标题标记 setValue() 会自动重绘并同步发
//     scalesChanged，再进本函数；没有保护就是递归到栈溢出。画布还没布局好（零尺寸）时
//     坐标换算出 NaN，要原地返回，而且返回时也得把保护放开。
//  3. setCurveData() 的 NaN 策略：头部成片的 NaN 跳过不丢点（多曲线时间戳对齐），
//     中段混进来的非有限值就地截断——否则 QPainter::drawPolyline 走到
//     QLineF::setLength() 的 Q_ASSERT(qIsFinite) 直接 abort 整个进程。
//  4. setAxisScale() 对非有限 / 零宽量程的兜底：零宽量程让 QwtScaleMap 的换算因子
//     变成 inf，画出来就是 NaN 坐标，后果同上。
#include "xQwtChartWidget.h"
#include "xQwtChart.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <qwt_abstract_scale_draw.h>
#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_curve.h>
#include <qwt_scale_div.h>
#include <qwt_scale_draw.h>
#include <qwt_scale_map.h>
#include <qwt_scale_widget.h>

#include <cmath>
#include <limits>

namespace {

void ensureApplication() {
    if (QApplication::instance() != nullptr) {
        return;
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    static int argc = 1;
    static char arg0[] = "test_qwt_chart_widget";
    static char* argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
}

class QwtChartWidget : public ::testing::Test {
  protected:
    void SetUp() override { ensureApplication(); }
};

const int kAxes[] = {QwtAxis::YLeft, QwtAxis::YRight, QwtAxis::XBottom, QwtAxis::XTop};

const char* axisName(int axis) {
    switch (axis) {
        case QwtAxis::YLeft: return "YLeft";
        case QwtAxis::YRight: return "YRight";
        case QwtAxis::XBottom: return "XBottom";
        case QwtAxis::XTop: return "XTop";
        default: return "?";
    }
}

bool backboneEnabled(QwtPlot* plot, int axis) {
    QwtScaleWidget* scaleWidget = plot->axisWidget(axis);
    EXPECT_NE(scaleWidget, nullptr) << axisName(axis);
    if (!scaleWidget) return false;
    const QwtScaleDraw* draw = scaleWidget->scaleDraw();
    EXPECT_NE(draw, nullptr) << axisName(axis);
    if (!draw) return false;
    return draw->hasComponent(QwtAbstractScaleDraw::Backbone);
}

// Keep the feedback finite even without the fix, so a regression fails an
// assertion instead of exhausting the process stack.
class RangeChangingMarker : public QwtPlotMarker {
  public:
    bool armed = false;
    int changes = 0;

    void itemChanged() override {
        if (armed && plot()) {
            ++changes;
            if (changes < 8) {
                const bool autoReplot = plot()->autoReplot();
                plot()->setAutoReplot(false);
                plot()->setAxisScale(QwtAxis::YLeft, 0.0, 100.0 + changes * 10.0);
                plot()->setAutoReplot(autoReplot);
            }
        }
        QwtPlotMarker::itemChanged();
    }
};

// 子类化是为了够到 protected 的 updateTitlePosition / m_titleMarker
class TitleChart : public xQwtChartWidget {
  public:
    using xQwtChartWidget::updateTitlePosition;

    RangeChangingMarker* installMarker() {
        const bool autoReplot = m_plot->autoReplot();
        m_plot->setAutoReplot(false);
        auto* marker = new RangeChangingMarker;
        marker->setItemAttribute(QwtPlotItem::AutoScale, false);
        marker->setValue(-100.0, -100.0);
        m_titleMarker = marker;
        marker->attach(m_plot);
        m_plot->setAutoReplot(autoReplot);
        return marker;
    }
};

void showWithUnitScales(xQwtChartWidget& chart) {
    chart.resize(600, 300);
    chart.show();
    chart.setAxisScale(QwtAxis::XBottom, 0.0, 100.0);
    chart.setAxisScale(QwtAxis::YLeft, 0.0, 100.0);
    QApplication::processEvents();
}

const double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

// ---- 1. 轴线 ----

TEST_F(QwtChartWidget, AxisBackbonesAreDisabledOnEveryAxisAfterConstruction) {
    xQwtChartWidget chart;
    ASSERT_NE(chart.getPlot(), nullptr);

    for (int axis : kAxes) {
        EXPECT_FALSE(backboneEnabled(chart.getPlot(), axis))
            << axisName(axis) << " 的轴线还开着，会和画布边框叠成双线";
    }
}

TEST_F(QwtChartWidget, AxisBackbonesStayDisabledAfterReplacingAScaleDraw) {
    xQwtChartWidget chart;
    ASSERT_NE(chart.getPlot(), nullptr);

    // 模拟使用方换时间轴刻度的那一步：新对象的 Backbone 是开着的
    chart.getPlot()->setAxisScaleDraw(QwtAxis::XBottom, new QwtScaleDraw());
    ASSERT_TRUE(backboneEnabled(chart.getPlot(), QwtAxis::XBottom))
        << "前提不成立：换上的新 scaleDraw 本来就没开轴线，这个用例证明不了什么";

    chart.hideAxisBackbones();

    for (int axis : kAxes) {
        EXPECT_FALSE(backboneEnabled(chart.getPlot(), axis))
            << axisName(axis) << " 的轴线在换过 scaleDraw 之后没被关回去";
    }
}

// ---- 2. 标题位置 ----

TEST_F(QwtChartWidget, TitleUpdateBoundsSynchronousReentry) {
    TitleChart chart;
    showWithUnitScales(chart);
    auto* marker = chart.installMarker();
    marker->armed = true;
    chart.updateTitlePosition();
    EXPECT_EQ(marker->changes, 1);
    EXPECT_TRUE(std::isfinite(marker->xValue()));
    EXPECT_TRUE(std::isfinite(marker->yValue()));
    EXPECT_TRUE(chart.getPlot()->autoReplot());

    // The injected replot changed the Y range. A later independent update must
    // still move the title, proving the guard was restored after the first call.
    const double oldY = marker->yValue();
    marker->changes = 0;
    chart.updateTitlePosition();
    EXPECT_EQ(marker->changes, 1);
    EXPECT_NE(marker->yValue(), oldY);
}

TEST_F(QwtChartWidget, TitleUpdateSkipsInvalidCanvasAndRecovers) {
    TitleChart chart;
    showWithUnitScales(chart);
    auto* marker = chart.installMarker();
    chart.getPlot()->setAxisVisible(QwtAxis::XBottom, false);
    chart.getPlot()->setAxisVisible(QwtAxis::YLeft, false);
    auto* canvas = static_cast<QwtPlotCanvas*>(chart.getPlot()->canvas());
    canvas->setFrameStyle(QFrame::NoFrame);
    canvas->setFixedSize(1, 1);
    const QwtScaleMap xMap = chart.getPlot()->canvasMap(QwtAxis::XBottom);
    ASSERT_EQ(xMap.p1(), xMap.p2());
    const QPointF oldValue = marker->value();
    chart.updateTitlePosition();
    EXPECT_EQ(marker->value(), oldValue);

    // An early return for invalid coordinates must also release the guard.
    canvas->setFixedSize(400, 200);
    chart.updateTitlePosition();
    EXPECT_NE(marker->value(), oldValue);
    EXPECT_TRUE(std::isfinite(marker->xValue()));
    EXPECT_TRUE(std::isfinite(marker->yValue()));
}

TEST_F(QwtChartWidget, TitleSitsTwentyPixelsBelowCanvasTop) {
    TitleChart chart;
    showWithUnitScales(chart);
    auto* marker = chart.installMarker();
    chart.updateTitlePosition();
    const QwtScaleMap yMap = chart.getPlot()->canvasMap(QwtAxis::YLeft);
    EXPECT_NEAR(yMap.transform(marker->yValue()) - yMap.p2(), 20.0, 1.0);
}

// ---- 3. setCurveData 的 NaN 策略 ----

TEST_F(QwtChartWidget, LeadingNanSamplesAreSkippedWithoutShiftingTheRest) {
    xQwtChartWidget chart;
    const int id = chart.addCurve("pv", Qt::blue);
    const QVector<double> xs = {0.0, 1.0, 2.0, 3.0, 4.0};
    const QVector<double> ys = {kNaN, kNaN, 20.0, 30.0, 40.0};
    chart.setCurveData(id, xs, ys);

    QwtPlotCurve* curve = chart.getCurve(id);
    ASSERT_NE(curve, nullptr);
    ASSERT_EQ(curve->dataSize(), 3u);
    // 从第一个有限点开始喂，x 不重排：第一个点仍是 x=2
    EXPECT_DOUBLE_EQ(curve->sample(0).x(), 2.0);
    EXPECT_DOUBLE_EQ(curve->sample(0).y(), 20.0);
    EXPECT_DOUBLE_EQ(curve->sample(2).x(), 4.0);
}

TEST_F(QwtChartWidget, NonFiniteSampleInTheMiddleTruncatesTheCurve) {
    xQwtChartWidget chart;
    const int id = chart.addCurve("pv", Qt::blue);
    const QVector<double> xs = {0.0, 1.0, 2.0, 3.0, 4.0};
    const QVector<double> ys = {0.0, 10.0, std::numeric_limits<double>::infinity(), 30.0, 40.0};
    chart.setCurveData(id, xs, ys);

    QwtPlotCurve* curve = chart.getCurve(id);
    ASSERT_NE(curve, nullptr);
    EXPECT_EQ(curve->dataSize(), 2u) << "中段的非有限值之后的点不能再喂给 QWT";
}

TEST_F(QwtChartWidget, AllNanSamplesLeaveTheCurveEmpty) {
    xQwtChartWidget chart;
    const int id = chart.addCurve("pv", Qt::blue);
    chart.setCurveData(id, {0.0, 1.0}, {kNaN, kNaN});

    QwtPlotCurve* curve = chart.getCurve(id);
    ASSERT_NE(curve, nullptr);
    EXPECT_EQ(curve->dataSize(), 0u);
}

TEST_F(QwtChartWidget, CurveIdsRestartFromZeroAfterRemoveAllCurves) {
    xQwtChartWidget chart;
    EXPECT_EQ(chart.addCurve("a", Qt::red), 0);
    EXPECT_EQ(chart.addCurve("b", Qt::green), 1);
    chart.removeAllCurves();
    EXPECT_EQ(chart.getCurve(0), nullptr);
    // 使用方按 0/1 固定下标重建曲线，计数器必须归零
    EXPECT_EQ(chart.addCurve("c", Qt::blue), 0);
    // 失效 id 的调用是空操作而不是崩溃
    chart.setCurveData(7, {0.0}, {0.0});
}

// ---- 4. setAxisScale 的量程兜底 ----

TEST_F(QwtChartWidget, DegenerateAxisRangeIsWidenedInsteadOfPassedThrough) {
    xQwtChartWidget chart;
    chart.setAxisScale(QwtAxis::YLeft, 5.0, 5.0);
    const QwtScaleDiv zeroWidth = chart.getPlot()->axisScaleDiv(QwtAxis::YLeft);
    EXPECT_LT(zeroWidth.lowerBound(), 5.0);
    EXPECT_GT(zeroWidth.upperBound(), 5.0);

    chart.setAxisScale(QwtAxis::YLeft, 10.0, 2.0);
    const QwtScaleDiv inverted = chart.getPlot()->axisScaleDiv(QwtAxis::YLeft);
    EXPECT_LT(inverted.lowerBound(), inverted.upperBound());
    EXPECT_TRUE(std::isfinite(inverted.lowerBound()));
    EXPECT_TRUE(std::isfinite(inverted.upperBound()));
}

TEST_F(QwtChartWidget, NonFiniteAxisRangeFallsBackToUnitRange) {
    xQwtChartWidget chart;
    chart.setAxisScale(QwtAxis::XBottom, kNaN, std::numeric_limits<double>::infinity());
    const QwtScaleDiv div = chart.getPlot()->axisScaleDiv(QwtAxis::XBottom);
    EXPECT_DOUBLE_EQ(div.lowerBound(), 0.0);
    EXPECT_DOUBLE_EQ(div.upperBound(), 1.0);
}
