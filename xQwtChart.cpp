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
#include <qwt_scale_engine.h>
#include <qwt_scale_map.h>
#include <qwt_scale_widget.h>
#include <qwt_series_data.h>

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStack>
#include <QPen>
#include <QTimer>
#include <QToolTip>

#include <algorithm>
#include <functional>
#include <limits>

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

// 坐标轴两端的拖动把手要做什么, 由使用方给 (纵轴: xQwtCurveScaling; 横轴: XAxisOnlyZoomer)
struct xBoundHandlesTarget {
    std::function<void()> begin;                 // 按住把手, 开始拖之前 (可空)
    std::function<void(double, double)> set;     // 拖动中实时设这条轴的 (下界, 上界)
    std::function<void()> end;                   // 真的拖动过, 松手之后 (可空)
    std::function<void()> reset;                 // 双击这条轴
};

// 坐标轴上下界 (横轴为左右界) 的拖动把手: 叠在轴控件 (QwtScaleWidget) 上的透明子控件, 两端各画一个
// 指向画布的小三角。按住把手沿轴拖, 三角跟着鼠标走, 那一界随之实时改、画图区跟着变, 并提示当前
// 界值 (日期轴显示成日期时间); 松手后三角回到轴端 (新界值所在处)。只点不拖不改。双击轴恢复。
// 界值一直按按下时的刻度映射换算, 轴边拖边变也不会反过来带偏鼠标对应的值。
class xScaleBoundHandles : public QWidget {
  public:
    xScaleBoundHandles(QwtScaleWidget* axis, xBoundHandlesTarget target)
        : QWidget(axis), m_axis(axis), m_target(std::move(target)) {
        setMouseTracking(true);   // 悬停在把手上换成沿轴拖动的光标
        setGeometry(axis->rect());
        axis->installEventFilter(this);
        connect(axis, &QwtScaleWidget::scaleDivChanged, this, qOverload<>(&QWidget::update));
        show();
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        // 先看事件类型、只比指针: 轴控件析构时也会收到事件
        if (event->type() == QEvent::Resize && watched == m_axis) setGeometry(m_axis->rect());
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        const double extent = vertical() ? height() : width();
        for (Handle h : {Lower, Upper}) {
            const bool active = h == m_drag || h == m_hover;
            p.setBrush(QColor(0x2f, 0x6b, 0xed, active ? 255 : 160));
            // 拖着的那个画在鼠标处 (拖出控件就停在边上)
            const double t = h == m_drag ? std::clamp(m_dragPos, 0.0, extent) : endPos(h);
            p.drawPolygon(triangle(t));
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        m_drag = event->button() == Qt::LeftButton ? handleAt(event->position()) : None;
        if (m_drag == None) {
            QWidget::mousePressEvent(event);
            return;
        }
        m_map = m_axis->scaleDraw()->scaleMap();
        const QwtInterval iv = m_axis->scaleDraw()->scaleDiv().interval().normalized();
        m_lower = iv.minValue();
        m_upper = iv.maxValue();
        m_pressPos = along(event->position());
        m_dragPos = endPos(m_drag);
        m_moved = false;
        if (m_target.begin) m_target.begin();
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_drag == None) {
            const Handle h = handleAt(event->position());
            if (h != m_hover) {
                m_hover = h;
                if (h == None)
                    unsetCursor();
                else
                    setCursor(vertical() ? Qt::SizeVerCursor : Qt::SizeHorCursor);
                update();
            }
            return;
        }
        // 三角跟着鼠标, 轴和画图区实时跟着改
        const double pos = along(event->position());
        const double value = draggedValue(m_drag, pos);
        m_dragPos = m_map.transform(value);
        update();
        applyDrag(m_drag, pos);
        QToolTip::showText(event->globalPosition().toPoint(), valueText(value), this);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_drag == None) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        applyDrag(m_drag, along(event->position()));
        m_drag = None;   // 三角回到轴端
        QToolTip::hideText();
        update();
        if (m_moved && m_target.end) m_target.end();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_target.reset) m_target.reset();
    }

    void leaveEvent(QEvent* event) override {
        if (m_hover != None) {
            m_hover = None;
            unsetCursor();
            update();
        }
        QWidget::leaveEvent(event);
    }

  private:
    enum Handle { None, Lower, Upper };

    bool vertical() const { return m_axis->scaleDraw()->orientation() == Qt::Vertical; }
    double along(const QPointF& p) const { return vertical() ? p.y() : p.x(); }

    // 把手在控件里沿轴的坐标: 轴那一端的位置
    double endPos(Handle h) const {
        const QwtInterval iv = m_axis->scaleDraw()->scaleDiv().interval().normalized();
        return m_axis->scaleDraw()->scaleMap().transform(h == Upper ? iv.maxValue() : iv.minValue());
    }

    Handle handleAt(const QPointF& p) const {
        for (Handle h : {Upper, Lower})
            if (std::abs(along(p) - endPos(h)) <= 7.0) return h;
        return None;
    }

    // 鼠标沿轴坐标对应的、正在拖的那一界的新值: 上界始终比下界大出原跨度的 1%, 拖不反
    double draggedValue(Handle h, double pos) const {
        const double value = m_map.invTransform(pos);
        const double min_span = (m_upper - m_lower) * 0.01;
        return h == Upper ? std::max(value, m_lower + min_span) : std::min(value, m_upper - min_span);
    }

    // 把拖到 pos 处的界值设上去; 离按下处不到 2 像素算没拖, 不改
    void applyDrag(Handle h, double pos) {
        if (std::abs(pos - m_pressPos) < 2.0) return;
        m_moved = true;
        const double value = draggedValue(h, pos);
        if (h == Upper)
            m_target.set(m_lower, value);
        else
            m_target.set(value, m_upper);
    }

    // 提示文字: 日期轴 (Qwt 毫秒) 显示成日期时间, 其他按数值
    QString valueText(double value) const {
        if (const auto* date = dynamic_cast<const QwtDateScaleDraw*>(m_axis->scaleDraw()))
            return date->toDateTime(value).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        return QString::number(value, 'g', 6);
    }

    // 尖端顶在刻度主干上、指向画布的小三角; t 是沿轴的坐标
    QPolygonF triangle(double t) const {
        const QPointF bb = m_axis->scaleDraw()->pos();
        switch (m_axis->alignment()) {
        case QwtScaleDraw::LeftScale:     // 主干在右边, 画布在右
            return QPolygonF{QPointF(bb.x() - 10.0, t - 5.5), QPointF(bb.x() - 10.0, t + 5.5), QPointF(bb.x() - 1.0, t)};
        case QwtScaleDraw::RightScale:    // 主干在左边, 画布在左
            return QPolygonF{QPointF(bb.x() + 10.0, t - 5.5), QPointF(bb.x() + 10.0, t + 5.5), QPointF(bb.x() + 1.0, t)};
        case QwtScaleDraw::TopScale:      // 主干在下边, 画布在下
            return QPolygonF{QPointF(t - 5.5, bb.y() - 10.0), QPointF(t + 5.5, bb.y() - 10.0), QPointF(t, bb.y() - 1.0)};
        case QwtScaleDraw::BottomScale:   // 主干在上边, 画布在上
        default:
            return QPolygonF{QPointF(t - 5.5, bb.y() + 10.0), QPointF(t + 5.5, bb.y() + 10.0), QPointF(t, bb.y() + 1.0)};
        }
    }

    QwtScaleWidget* m_axis;
    xBoundHandlesTarget m_target;
    Handle m_drag = None;
    Handle m_hover = None;
    QwtScaleMap m_map;   // 按下时的刻度映射
    double m_lower = 0.0;
    double m_upper = 1.0;
    double m_pressPos = 0.0;   // 按下时鼠标沿轴的坐标
    double m_dragPos = 0.0;    // 拖着的三角现在画在哪
    bool m_moved = false;      // 这次按下后真的拖动过
};

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
    // 禁用右键复位 (Qwt 的右键退回构造时的缩放起点); 要右键恢复全图用 setResetTriggers(RightClick)
    setMousePattern(QwtEventPattern::MouseSelect2, Qt::NoButton);
}

void XAxisOnlyZoomer::setResetTriggers(ResetTriggers triggers, int key) {
    m_resetTriggers = triggers;
    m_resetKey = key;
}

void XAxisOnlyZoomer::resetToFullView() {
    QwtPlot* plt = plot();
    if (!plt) return;
    // 正拖着框选时 (例如拖到一半按了 Esc / 右键) 先取消这次选择, 否则松开左键时
    // 这个框会按恢复后的刻度再放大一次
    reset();
    // 横轴交回自动范围按全部数据重算, 再以此为缩放起点; 纵轴 (自动 / 固定) 一概不动
    plt->setAxisAutoScale(xAxis(), true);
    setZoomBase(true);
    emit resetRequested();
}

void XAxisOnlyZoomer::setXBoundHandles(bool on) {
    if (on == (m_xHandles != nullptr)) return;
    if (!on) {
        delete m_xHandles.data();
        m_xHandles = nullptr;
        return;
    }
    QwtPlot* plt = plot();
    if (!plt) return;
    xBoundHandlesTarget target;
    target.begin = [this]() {
        // 同 end(): 打开了恢复全图时, 在缩放起点上开始改动前先把起点更新成当前视图
        if (m_resetTriggers.toInt() != 0 && zoomRectIndex() == 0) setZoomBase(false);
    };
    target.set = [this](double lower, double upper) {
        QwtPlot* p = plot();
        if (!p) return;
        p->setAxisScale(xAxis(), lower, upper);
        p->replot();
    };
    target.end = [this]() {
        // 拖完记进缩放记录, 中键 / 减号可退回拖之前的范围
        QwtPlot* p = plot();
        if (!p) return;
        const QwtInterval iv = p->axisInterval(xAxis()).normalized();
        QRectF rect = zoomRect();
        rect.setLeft(iv.minValue());
        rect.setRight(iv.maxValue());
        zoom(rect);
        emit xAxisZoomed(zoomRect());
    };
    target.reset = [this]() { resetToFullView(); };
    m_xHandles = new xScaleBoundHandles(plt->axisWidget(xAxis()), std::move(target));
}

bool XAxisOnlyZoomer::eventFilter(QObject* object, QEvent* event) {
    if (m_resetTriggers.testFlag(ResetTrigger::KeyPress) && object == parentWidget() &&
        event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == m_resetKey) {
        resetToFullView();
        return true;   // 吃掉, 例如 Esc 不再传给外层弹窗把它关掉
    }
    return QwtPlotZoomer::eventFilter(object, event);
}

void XAxisOnlyZoomer::widgetMouseReleaseEvent(QMouseEvent* event) {
    if (m_resetTriggers.testFlag(ResetTrigger::RightClick) && event &&
        event->button() == Qt::RightButton) {
        resetToFullView();
        return;
    }
    QwtPlotZoomer::widgetMouseReleaseEvent(event);
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
    // 打开了恢复全图时: 在缩放起点上开始新的放大, 先把起点更新成当前视图。起点是构造时记下的,
    // 那时曲线往往还没画, 不更新的话中键后退 / 加减号会退到 Qwt 的默认范围。
    if (m_resetTriggers.toInt() != 0 && ok && zoomRectIndex() == 0) setZoomBase(false);
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
        if (m_resetTriggers.testFlag(ResetTrigger::DoubleClick))
            resetToFullView();   // 里面会发 resetRequested
        else
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
    // 横轴缩放 / 平移后, 打开了 setAbsoluteFitVisible 时按可见部分重定纵轴
    connect(plot->axisWidget(QwtAxis::XBottom), &QwtScaleWidget::scaleDivChanged, this, [this]() {
        if (m_fitVisible && m_mode == Mode::Absolute) scheduleFit();
    });
    m_canvas->installEventFilter(this);
    updateOverlay();
}

// 随 plot 一起销毁 (parent 是 plot), 那时 plot 已析构一半, 不再去恢复曲线和纵轴
xQwtCurveScaling::~xQwtCurveScaling() = default;

void xQwtCurveScaling::setMode(Mode mode) {
    if (mode == m_mode) return;
    m_mode = mode;
    // 切换模式即回到该模式的默认纵轴, 手动拖的范围作废
    const bool was_manual = m_manualY;
    m_manualY = false;
    {
        const QSignalBlocker block_absolute(m_absoluteButton);
        const QSignalBlocker block_per_curve(m_perCurveButton);
        (mode == Mode::PerCurve ? m_perCurveButton : m_absoluteButton)->setChecked(true);
    }
    if (mode == Mode::PerCurve) {
        // 记下切换前的纵轴, 切回绝对值时原样恢复 (手动拖的不算, 按自动记)
        m_axisTitle = m_plot->axisTitle(m_yAxis);
        m_axisAutoScale = was_manual || m_plot->axisAutoScale(m_yAxis);
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

void xQwtCurveScaling::setModeSwitchVisible(bool on) {
    if (on == m_switchVisible) return;
    m_switchVisible = on;
    updateOverlay();
    m_plot->replot();   // 叠加图例的纵向偏移跟着变
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
    scheduleFit();
}

void xQwtCurveScaling::setAbsoluteFitVisible(bool on) {
    if (on == m_fitVisible) return;
    m_fitVisible = on;
    if (m_mode != Mode::Absolute) {
        // 按量程模式下纵轴固定 0-100, 切回绝对值时再按它办。关掉时把切回用的存档改成自动范围:
        // 存档可能是拟合留下的固定区间, 照它恢复会把窗外的点裁掉
        if (!on) m_axisAutoScale = true;
        return;
    }
    if (on) {
        scheduleFit();
    } else if (!m_manualY) {   // 手动拖的范围留着, 直到 resetYBounds 或切模式
        m_plot->setAxisAutoScale(m_yAxis, true);
        scheduleReplot();
    }
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
        scheduleFit();   // 少了曲线, 可见部分的范围可能变了
        return;
    }
    if (rtti == QwtPlotItem::Rtti_PlotCurve && m_mode == Mode::PerCurve && item->yAxis() == m_yAxis) {
        applyToCurve(static_cast<QwtPlotCurve*>(item));
        scheduleReplot();
    }
    if (rtti == QwtPlotItem::Rtti_PlotCurve) scheduleFit();
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
        if (m_fitVisible && applyVisibleFit()) return;   // 按可见部分定; 可见范围里没有点就照原样恢复
        if (m_axisAutoScale)
            m_plot->setAxisAutoScale(m_yAxis, true);
        else
            m_plot->setAxisScale(m_yAxis, m_axisInterval.minValue(), m_axisInterval.maxValue());
    }
}

void xQwtCurveScaling::setYBoundHandles(bool on) {
    if (on == (m_handles != nullptr)) return;
    if (on) {
        xBoundHandlesTarget target;
        target.set = [this](double lower, double upper) { setYBounds(lower, upper); };
        target.reset = [this]() { resetYBounds(); };
        m_handles = new xScaleBoundHandles(m_plot->axisWidget(m_yAxis), std::move(target));
    } else {
        delete m_handles.data();
        m_handles = nullptr;
    }
}

void xQwtCurveScaling::setYBounds(double lower, double upper) {
    if (!(std::isfinite(lower) && std::isfinite(upper) && upper > lower)) return;
    m_manualY = true;
    m_plot->setAxisScale(m_yAxis, lower, upper);
    scheduleReplot();
}

void xQwtCurveScaling::resetYBounds() {
    m_manualY = false;
    if (m_mode == Mode::PerCurve)
        m_plot->setAxisScale(m_yAxis, 0.0, 100.0);
    else if (!(m_fitVisible && applyVisibleFit()))
        m_plot->setAxisAutoScale(m_yAxis, true);
    m_plot->replot();
}

bool xQwtCurveScaling::applyVisibleFit() {
    if (!m_fitVisible || m_mode != Mode::Absolute || m_manualY) return false;   // 手动定的不动
    // 横轴当前范围内、显示中的曲线的点; 时间序列量大, 但只是顺序扫一遍取最小 / 最大值
    const QwtInterval visible = m_plot->axisInterval(QwtAxis::XBottom).normalized();
    double lower = std::numeric_limits<double>::infinity();
    double upper = -std::numeric_limits<double>::infinity();
    for (QwtPlotCurve* curve : curves()) {
        if (!curve->isVisible() || curve->xAxis() != QwtAxis::XBottom) continue;
        const QwtSeriesData<QPointF>* data = curve->data();
        const size_t n = data ? data->size() : 0;
        for (size_t i = 0; i < n; ++i) {
            const QPointF p = data->sample(i);
            if (p.x() < visible.minValue() || p.x() > visible.maxValue() || !std::isfinite(p.y())) continue;
            lower = std::min(lower, p.y());
            upper = std::max(upper, p.y());
        }
    }
    if (!(lower <= upper)) return false;
    // 照 Qwt 自动范围的做法交给纵轴的刻度引擎取整, 刻度和边界都是整齐的数
    double step = 0.0;
    m_plot->axisScaleEngine(m_yAxis)->autoScale(m_plot->axisMaxMajor(m_yAxis), lower, upper, step);
    m_plot->setAxisScale(m_yAxis, lower, upper, step);
    return true;
}

void xQwtCurveScaling::scheduleFit() {
    if (!m_fitVisible || m_mode != Mode::Absolute || m_manualY || m_fitPending) return;
    // 排到下一轮: 横轴的 scaleDivChanged 是在 replot 当中发的, 不能在里面再 replot
    m_fitPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_fitPending = false;
        if (applyVisibleFit()) m_plot->replot();
    });
}

void xQwtCurveScaling::updateOverlay() {
    // 单条曲线按自己的量程看也有用, 有曲线 (列在图例里的) 就显示; 一条都没有时隐藏,
    // 按量程模式下一直显示, 好切回去; 使用方关掉了就一直不显示
    int listed = 0;
    for (QwtPlotCurve* curve : curves())
        if (curve->testItemAttribute(QwtPlotItem::Legend)) ++listed;
    const bool visible = m_switchVisible && (m_mode == Mode::PerCurve || listed >= 1);
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
