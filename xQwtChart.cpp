// ***************************************************************
//  xQwtChart   version:  1.0   -  date:  2025/08/13
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************
#include "xQwtChart.h"

#include <qwt_plot_curve.h>
#include <qwt_series_data.h>

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStack>
#include <QPen>
#include <QTimer>

#include <algorithm>

namespace {

// 按量程画曲线时包在原数据外面的代理: 读点时把 y 换算成量程百分比, 原数据不复制。
// 持有原数据, 析构时一并删除 (曲线删掉或换数据时随代理一起走)。
class xScaledSeriesData : public QwtSeriesData<QPointF> {
  public:
    xScaledSeriesData(QwtSeriesData<QPointF>* original, const xValueRange& range)
        : m_original(original), m_range(range) {}
    ~xScaledSeriesData() override { delete m_original; }

    size_t size() const override { return m_original ? m_original->size() : 0; }

    QPointF sample(size_t i) const override {
        const QPointF p = m_original->sample(i);
        return QPointF(p.x(), m_range.toPercent(p.y()));
    }

    QRectF boundingRect() const override {
        if (!m_original) return QRectF(1.0, 1.0, -2.0, -2.0);   // 与 Qwt 的空数据约定一致
        const QRectF r = m_original->boundingRect();
        if (r.width() < 0.0 || r.height() < 0.0) return r;
        const double y0 = m_range.toPercent(r.top());
        const double y1 = m_range.toPercent(r.bottom());
        return QRectF(QPointF(r.left(), std::min(y0, y1)), QPointF(r.right(), std::max(y0, y1)));
    }

    const QwtSeriesData<QPointF>* original() const { return m_original; }
    QwtSeriesData<QPointF>* takeOriginal() {
        QwtSeriesData<QPointF>* original = m_original;
        m_original = nullptr;
        return original;
    }
    void setRange(const xValueRange& range) { m_range = range; }

  private:
    QwtSeriesData<QPointF>* m_original;
    xValueRange m_range;
};

// 由曲线原数据 (包了代理就取代理里的原数据) 的纵向范围算出的自动量程
xValueRange autoRange(const QwtPlotCurve* curve) {
    const QwtSeriesData<QPointF>* data = curve->data();
    if (const auto* scaled = dynamic_cast<const xScaledSeriesData*>(data)) data = scaled->original();
    if (!data) return {};
    const QRectF r = data->boundingRect();
    if (r.width() < 0.0 || r.height() < 0.0) return {};
    return xValueRange::fromData(r.top(), r.bottom());
}

}  // namespace

void xQwtPlot::replot() {
    QwtPlot::replot();

    emit scalesChanged();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

XAxisOnlyZoomer::XAxisOnlyZoomer(QWidget* canvas) : QwtPlotZoomer(canvas) {
    // Only allow X-axis selection
    setRubberBandPen(QPen(Qt::blue, 1, Qt::DashLine));
    setTrackerPen(QPen(Qt::blue));
    // 禁用右键复位
    setMousePattern(QwtEventPattern::MouseSelect2, Qt::NoButton);
}

bool XAxisOnlyZoomer::accept(QPolygon& pa) const {
    if (pa.count() < 2) return false;

    // Get the rectangle from the polygon
    QRect polyRect = pa.boundingRect();

    // 单击 / 微小拖动过滤：橡皮筋矩形的像素宽度小于阈值时，
    // 视为误触（既包括"鼠标单击未拖动"的零像素情况，也包括只拖了几个像素），
    // 直接拒绝缩放。注意这里只看 X 像素宽度，因为本类只做 X 轴缩放，
    // 用户哪怕画了一个很高但非常窄的矩形，缩出来也是没意义的一小段 X 区间。
    if (m_minSelectionPixels > 0 && polyRect.width() < m_minSelectionPixels) {
        return false;
    }

    QRectF rect = invTransform(polyRect);

    if (plot()) {
        // Preserve Y-axis range - get current plot's Y-axis range
        QwtScaleDiv yDiv = plot()->axisScaleDiv(QwtPlot::yLeft);
        rect.setTop(yDiv.lowerBound());
        rect.setBottom(yDiv.upperBound());

        // Ensure X-axis coordinates are not less than 0
        if (rect.left() < 0.0) {
            double width = rect.width();
            rect.setLeft(0.0);
            rect.setRight(width);
        }
        if (rect.right() < 0.0) {
            rect.setRight(0.0);
        }

        // 确保X轴范围最小不小于10，并且端点为整数
        double range = rect.right() - rect.left();
        if (range < 10.0) {
            // 如果范围小于10，扩展范围到10
            double center = (rect.left() + rect.right()) / 2.0;
            rect.setLeft(center - 5.0);
            rect.setRight(center + 5.0);
        }

        // 确保端点为整数
        rect.setLeft(floor(rect.left()));
        rect.setRight(ceil(rect.right()));

        // Transform back to polygon
        QRect newPolyRect = transform(rect);
        pa.clear();
        pa << newPolyRect.topLeft() << newPolyRect.bottomRight();
    }

    return QwtPlotZoomer::accept(pa);
}

// Override end to emit our custom signal
bool XAxisOnlyZoomer::end(bool ok) {
    bool result = QwtPlotZoomer::end(ok);
    if (ok && result) {
        emit xAxisZoomed(zoomRect());
    }
    return result;
}

void XAxisOnlyZoomer::rescale() {
    // Re-entrancy from setZoomStack() while syncing Y into the zoom stack.
    if (m_resyncingZoomStackY)
        return;

    QwtPlot* plt = plot();
    if (!plt)
        return;

    const QwtScaleDiv& xDiv = plt->axisScaleDiv(xAxis());
    const QwtScaleDiv& yDiv = plt->axisScaleDiv(yAxis());

    const QRectF rect = zoomRect();
    double x1 = rect.left();
    double x2 = rect.right();
    if (!xDiv.isIncreasing())
        qSwap(x1, x2);

    // Only touch X. Keep the current Y scale (manual limits / auto-Y) intact.
    if (xDiv.lowerBound() != x1 || xDiv.upperBound() != x2) {
        const bool doReplot = plt->autoReplot();
        plt->setAutoReplot(false);
        plt->setAxisScale(xAxis(), x1, x2);
        plt->setAutoReplot(doReplot);
        plt->replot();
    }

    // Keep zoomRect()/zoomed(QRectF) Y in sync with the live axis so consumers
    // of the base Qwt signal do not see a construction-time Y range.
    QStack<QRectF> stack = zoomStack();
    const int idx = static_cast<int>(zoomRectIndex());
    if (idx < 0 || idx >= stack.size())
        return;

    QRectF entry = stack.at(idx);
    entry.setLeft(x1);
    entry.setRight(x2);
    entry.setTop(yDiv.lowerBound());
    entry.setBottom(yDiv.upperBound());
    if (entry == zoomRect())
        return;

    stack[idx] = entry;
    m_resyncingZoomStackY = true;
    setZoomStack(stack, idx);
    m_resyncingZoomStackY = false;
}

void XAxisOnlyZoomer::widgetMouseDoubleClickEvent(QMouseEvent* event) {
    // 左键双击 = 一键还原视图。
    // 双击事件序列是 Press → Release → Press → DblClick → Release，
    // 第二次 Press 已经把 picker 推到 active 状态、rubber band 已经开始画了，
    // 所以这里先 reset() 清掉这次还没完成的选择，再发 resetRequested 信号。
    // 不调用基类，避免基类把双击再当成一次额外的 begin()。
    if (event && event->button() == Qt::LeftButton) {
        reset();
        emit resetRequested();
        return;
    }
    QwtPlotZoomer::widgetMouseDoubleClickEvent(event);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
XAxisOnlyMagnifier::XAxisOnlyMagnifier(QWidget* canvas) : QwtPlotMagnifier(canvas) {
    setWheelFactor(1.1);
}

void XAxisOnlyMagnifier::rescale(double factor) {
    if (!plot()) return;

    // Get current scale divisions
    QwtScaleDiv xDiv = plot()->axisScaleDiv(QwtPlot::xBottom);
    QwtScaleDiv yDiv = plot()->axisScaleDiv(QwtPlot::yLeft);

    // Only rescale X-axis
    double xCenter = (xDiv.upperBound() + xDiv.lowerBound()) / 2.0;
    double xRange = xDiv.upperBound() - xDiv.lowerBound();
    double newXRange = xRange / factor;

    double newXLower = xCenter - newXRange / 2.0;
    double newXUpper = xCenter + newXRange / 2.0;

    // Ensure X-axis coordinates are not less than 0
    if (newXLower < 0.0) {
        newXUpper += (0.0 - newXLower);  // Shift the range to the right
        newXLower = 0.0;
    }

    // 确保X轴范围最小不小于10，并且端点为整数
    double range = newXUpper - newXLower;
    if (range < 10.0) {
        // 如果范围小于10，扩展范围到10
        double center = (newXLower + newXUpper) / 2.0;
        newXLower = center - 5.0;
        newXUpper = center + 5.0;
    }

    // 确保端点为整数
    newXLower = floor(newXLower);
    newXUpper = ceil(newXUpper);

    // Set new X-axis range while preserving Y-axis range
    plot()->setAxisScale(QwtPlot::xBottom, newXLower, newXUpper);
    // plot()->setAxisScale(QwtPlot::yLeft, yDiv.lowerBound(), yDiv.upperBound());
    plot()->replot();

    // Emit synchronization signal
    QRectF newRect(newXLower, yDiv.lowerBound(), newXUpper - newXLower,
                   yDiv.upperBound() - yDiv.lowerBound());
    emit xAxisRescaled(newRect);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

xQwtCurveScaling::xQwtCurveScaling(QwtPlot* plot, QwtAxisId yAxis)
    : QObject(plot), m_plot(plot), m_yAxis(yAxis), m_canvas(plot->canvas()) {
    m_box = new QFrame(m_canvas);
    m_box->setObjectName(QStringLiteral("xQwtCurveScalingBox"));
    // 与 xQwtAttachCanvasLegend 的图例同一套外观
    m_box->setStyleSheet(QStringLiteral(
        "#xQwtCurveScalingBox { background: rgba(255, 255, 255, 215);"
        " border: 1px solid #d5dde7; border-radius: 6px; }"));
    auto* layout = new QHBoxLayout(m_box);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(10);
    // 同一个父控件里的单选框默认互斥
    m_absoluteButton = new QRadioButton(tr("Absolute"), m_box);
    m_perCurveButton = new QRadioButton(tr("Per-curve range"), m_box);
    m_absoluteButton->setChecked(true);
    layout->addWidget(m_absoluteButton);
    layout->addWidget(m_perCurveButton);
    connect(m_perCurveButton, &QRadioButton::toggled, this,
            [this](bool on) { setMode(on ? Mode::PerCurve : Mode::Absolute); });

    connect(plot, &QwtPlot::itemAttached, this, &xQwtCurveScaling::onItemAttached);
    m_canvas->installEventFilter(this);
    updateOverlay();
}

// 随 plot 一起销毁 (parent 是 plot), 那时 plot 已析构一半, 不再去恢复曲线和纵轴
xQwtCurveScaling::~xQwtCurveScaling() = default;

void xQwtCurveScaling::setMode(Mode mode) {
    if (mode == m_mode) return;
    m_mode = mode;
    {
        const QSignalBlocker block_absolute(m_absoluteButton);
        const QSignalBlocker block_per_curve(m_perCurveButton);
        (mode == Mode::PerCurve ? m_perCurveButton : m_absoluteButton)->setChecked(true);
    }
    if (mode == Mode::PerCurve) {
        // 记下切换前的纵轴, 切回绝对值时原样恢复
        m_axisTitle = m_plot->axisTitle(m_yAxis);
        m_axisAutoScale = m_plot->axisAutoScale(m_yAxis);
        m_axisInterval = m_plot->axisInterval(m_yAxis);
        for (QwtPlotCurve* curve : curves()) applyToCurve(curve);
    } else {
        for (QwtPlotCurve* curve : curves()) restoreCurve(curve);
    }
    applyAxis();
    updateOverlay();
    m_plot->replot();
    emit modeChanged(mode);
}

void xQwtCurveScaling::setModeLabels(const QString& absolute, const QString& perCurve) {
    m_absoluteButton->setText(absolute);
    m_perCurveButton->setText(perCurve);
    updateOverlay();
}

void xQwtCurveScaling::setCurveRange(QwtPlotCurve* curve, const xValueRange& range) {
    if (!curve) return;
    if (!range.isValid()) {
        clearCurveRange(curve);
        return;
    }
    m_ranges.insert(curve, range);
    if (m_mode == Mode::PerCurve && curve->plot() == m_plot && curve->yAxis() == m_yAxis) {
        applyToCurve(curve);
        scheduleReplot();
    }
}

void xQwtCurveScaling::clearCurveRange(QwtPlotCurve* curve) {
    if (!curve) return;
    m_ranges.remove(curve);
    if (m_mode == Mode::PerCurve && curve->plot() == m_plot && curve->yAxis() == m_yAxis) {
        applyToCurve(curve);
        scheduleReplot();
    }
}

xValueRange xQwtCurveScaling::curveRange(const QwtPlotCurve* curve) const {
    if (!curve) return {0.0, 1.0};
    const auto it = m_ranges.constFind(curve);
    if (it != m_ranges.constEnd()) return *it;
    const xValueRange range = autoRange(curve);
    return range.isValid() ? range : xValueRange{0.0, 1.0};   // 空曲线也给个有效量程, 换算不除零
}

double xQwtCurveScaling::toValue(const QwtPlotCurve* curve, double percent) const {
    return curveRange(curve).fromPercent(percent);
}

void xQwtCurveScaling::refresh() {
    if (m_mode == Mode::PerCurve) {
        for (QwtPlotCurve* curve : curves()) applyToCurve(curve);
    }
    updateOverlay();
    scheduleReplot();
}

bool xQwtCurveScaling::eventFilter(QObject* watched, QEvent* event) {
    // plot 析构时 QwtPlot 的私有数据先删, 画布随后才拆, 拆的时候还会收到 Destroy 等事件:
    // 这里先看事件类型, 也不经 m_plot 取画布, 摆放单选框只碰画布和单选框自己。
    if (event->type() == QEvent::Resize && watched == m_canvas) positionBox();
    return QObject::eventFilter(watched, event);
}

void xQwtCurveScaling::positionBox() {
    const int margin = 8;
    m_box->adjustSize();
    m_box->move(m_canvas->width() - m_box->width() - margin, margin);
    m_box->raise();
}

void xQwtCurveScaling::onItemAttached(QwtPlotItem* item, bool on) {
    // 曲线被删除时是在 QwtPlotItem 的析构函数里摘下的, 那时动态类型已退回 QwtPlotItem,
    // rtti() 不再是 Rtti_PlotCurve: 只清记录, 不碰它 (它的数据连同代理由 QwtPlotCurve 删掉)。
    const int rtti = item->rtti();
    if (!on) {
        if (rtti == QwtPlotItem::Rtti_PlotCurve) restoreCurve(static_cast<QwtPlotCurve*>(item));
        m_ranges.remove(item);
        m_titles.remove(item);
        m_legendOffsets.remove(item);
        updateOverlay();
        return;
    }
    if (rtti == QwtPlotItem::Rtti_PlotCurve && m_mode == Mode::PerCurve && item->yAxis() == m_yAxis) {
        applyToCurve(static_cast<QwtPlotCurve*>(item));
        scheduleReplot();
    }
    if (rtti == QwtPlotItem::Rtti_PlotCurve || rtti == QwtPlotItem::Rtti_PlotLegend) updateOverlay();
}

QList<QwtPlotCurve*> xQwtCurveScaling::curves() const {
    QList<QwtPlotCurve*> list;
    for (QwtPlotItem* item : m_plot->itemList(QwtPlotItem::Rtti_PlotCurve)) {
        if (item->yAxis() == m_yAxis) list.append(static_cast<QwtPlotCurve*>(item));
    }
    return list;
}

void xQwtCurveScaling::applyToCurve(QwtPlotCurve* curve) {
    const xValueRange range = curveRange(curve);   // 先算: 自动量程要读原数据
    if (auto* scaled = dynamic_cast<xScaledSeriesData*>(curve->data())) {
        scaled->setRange(range);
    } else {
        // swapData 换下原数据而不删除它, 原数据改由代理持有
        curve->swapData(new xScaledSeriesData(curve->data(), range));
    }
    if (!m_titles.contains(curve)) m_titles.insert(curve, curve->title());
    QwtText title = m_titles.value(curve);
    title.setText(QStringLiteral("%1  [%2, %3]").arg(title.text(),
                                                     QString::number(range.lower, 'g', 5),
                                                     QString::number(range.upper, 'g', 5)));
    curve->setTitle(title);
    curve->itemChanged();
}

void xQwtCurveScaling::restoreCurve(QwtPlotCurve* curve) {
    if (auto* scaled = dynamic_cast<xScaledSeriesData*>(curve->data())) {
        curve->swapData(scaled->takeOriginal());
        delete scaled;
        curve->itemChanged();
    }
    const auto it = m_titles.find(curve);
    if (it != m_titles.end()) {
        curve->setTitle(*it);
        m_titles.erase(it);
    }
}

void xQwtCurveScaling::applyAxis() {
    if (m_mode == Mode::PerCurve) {
        QwtText title = m_axisTitle;   // 沿用原标题的字体和颜色
        title.setText(QStringLiteral("%"));
        m_plot->setAxisTitle(m_yAxis, title);
        m_plot->setAxisScale(m_yAxis, 0.0, 100.0);
    } else {
        m_plot->setAxisTitle(m_yAxis, m_axisTitle);
        if (m_axisAutoScale)
            m_plot->setAxisAutoScale(m_yAxis, true);
        else
            m_plot->setAxisScale(m_yAxis, m_axisInterval.minValue(), m_axisInterval.maxValue());
    }
}

void xQwtCurveScaling::updateOverlay() {
    // 绝对值模式下列在图例里的曲线少于 2 条时没什么可比的, 隐藏; 按量程模式下一直显示, 好切回去
    int listed = 0;
    for (QwtPlotCurve* curve : curves())
        if (curve->testItemAttribute(QwtPlotItem::Legend)) ++listed;
    const bool visible = m_mode == Mode::PerCurve || listed >= 2;
    m_box->setVisible(visible);
    positionBox();

    // 右上角的叠加图例让到单选框下面
    for (QwtPlotItem* item : m_plot->itemList(QwtPlotItem::Rtti_PlotLegend)) {
        auto* legend = static_cast<QwtPlotLegendItem*>(item);
        const Qt::Alignment alignment = legend->alignmentInCanvas();
        if (!(alignment & Qt::AlignTop) || !(alignment & Qt::AlignRight)) continue;
        if (!m_legendOffsets.contains(item))
            m_legendOffsets.insert(item, legend->offsetInCanvas(Qt::Vertical));
        const int base = m_legendOffsets.value(item);
        legend->setOffsetInCanvas(Qt::Vertical, visible ? base + m_box->height() + 6 : base);
    }
}

void xQwtCurveScaling::scheduleReplot() {
    if (m_replotPending) return;
    m_replotPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_replotPending = false;
        m_plot->replot();
    });
}
