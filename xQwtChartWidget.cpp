#include "xQwtChartWidget.h"
// 本文件没有导出宏，每个使用它的模块各编一份进自己的二进制（KeelDeck 的核心与
// TaijiMPC 插件就是这样）。所以这里不能有带非平凡析构的外部链接全局对象：ELF 上同名
// 符号会被 interpose 成同一个对象，却被两个模块各注册一次析构。DEFAULT_COLORS 用
// std::array<QRgb> 而不是 QVector<QColor> 就是这个原因。
#include <QPainter>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QMessageBox>
#include <qwt_scale_draw.h>
#include <QFileDialog>
#include <QApplication>
#include <algorithm>
#include <cmath>
#include <functional>
#include <qwt_plot_renderer.h>
#include <qwt_plot_item.h>
#include <qwt_plot_curve.h>
#include <qwt_text.h>
#include <qwt_scale_map.h>
#include <qwt_legend_label.h>
#include <qwt_event_pattern.h>
#include "xQwtChart.h"

namespace {
class CoordinatePlotPicker final : public QwtPlotPicker {
  public:
    explicit CoordinatePlotPicker(QWidget* canvas) : QwtPlotPicker(canvas) {}

    void setXAxisDecimals(int decimals) { m_xAxisDecimals = std::max(0, decimals); }

  protected:
    QwtText trackerTextF(const QPointF& position) const override {
        QString text;
        switch (rubberBand()) {
            case HLineRubberBand:
                text = QString::number(position.y(), 'f', 4);
                break;
            case VLineRubberBand:
                text = QString::number(position.x(), 'f', m_xAxisDecimals);
                break;
            default:
                text = QString::number(position.x(), 'f', m_xAxisDecimals) + ", " +
                       QString::number(position.y(), 'f', 4);
                break;
        }
        return QwtText(text);
    }

  private:
    int m_xAxisDecimals = 4;
};
}  // namespace

// Keep the palette free of heap ownership so ELF symbol interposition cannot
// register two destructors for it (see the note at the top of this file).
const std::array<QRgb, 12> xQwtChartWidget::DEFAULT_COLORS = {
    qRgb(31, 119, 180),   // Blue
    qRgb(255, 127, 14),   // Orange
    qRgb(44, 160, 44),    // Green
    qRgb(214, 39, 40),    // Red
    qRgb(148, 103, 189),  // Purple
    qRgb(140, 86, 75),    // Brown
    qRgb(227, 119, 194),  // Pink
    qRgb(127, 127, 127),  // Gray
    qRgb(188, 189, 34),   // Olive
    qRgb(23, 190, 207),   // Cyan
    qRgb(255, 152, 150),  // Light Red
    qRgb(174, 199, 232),  // Light Blue
};

xQwtChartWidget::xQwtChartWidget(QWidget* parent)
    : QWidget(parent),
      m_mainLayout(nullptr),
      m_plot(nullptr),
      m_grid(nullptr),
      m_legend(nullptr),
      m_panner(nullptr),
      m_zoomer(nullptr),
      m_magnifier(nullptr),
      m_titleMarker(nullptr),
      m_nextCurveId(0),
      m_updateTimer(nullptr),
      m_autoUpdate(false),
      m_gridEnabled(false),
      m_legendEnabled(false),
      m_zoomingEnabled(false),
      m_panningEnabled(true),
      m_magnifierEnabled(true),
      m_autoAxisScaling(false),
      m_mouseTrackingEnabled(false),
      m_coordinateFormat("X: %.2f, Y: %.2f"),
      m_coordinateMarker(nullptr),
      m_picker(nullptr) {
    setupUI();
    setupPlot();
    setupInteraction();
    connectSignals();
}

xQwtChartWidget::~xQwtChartWidget() {
    if (m_updateTimer) {
        m_updateTimer->stop();
        delete m_updateTimer;
    }

    // Clear title marker
    if (m_titleMarker) {
        m_titleMarker->detach();
        delete m_titleMarker;
        m_titleMarker = nullptr;
    }

    // Clear coordinate marker
    if (m_coordinateMarker) {
        m_coordinateMarker->detach();
        delete m_coordinateMarker;
        m_coordinateMarker = nullptr;
    }

    // Clear picker
    if (m_picker) {
        delete m_picker;
        m_picker = nullptr;
    }

    // Clear curves
    removeAllCurves();
    removeAllMarkers();
}

void xQwtChartWidget::setupUI() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    // Create the main plot widget
    m_plot = new xQwtPlot(this);
    m_mainLayout->addWidget(m_plot);

    setLayout(m_mainLayout);
}

void xQwtChartWidget::setupPlot() {
    // 不设置轴标题，保持简洁；标题走 setTitle() 的画布内标记，QwtPlot 自己的外部标题留空
    QFont yAxisFont;
    yAxisFont.setFamily("Consolas, Microsoft YaHei");
    yAxisFont.setPointSize(6);
    yAxisFont.setBold(false);
    m_plot->setAxisFont(QwtAxis::YLeft, yAxisFont);
    m_plot->setAxisFont(QwtAxis::XBottom, yAxisFont);

    auto* scaleDraw = new xQwtScaleDraw();
    scaleDraw->setMinimumExtent(20);
    m_plot->setAxisTitle(QwtAxis::XBottom, "");
    m_plot->setAxisTitle(QwtAxis::YLeft, "");
    m_plot->setAxisScaleDraw(QwtAxis::YLeft, scaleDraw);
    hideAxisBackbones();

    // Setup grid
    m_grid = new QwtPlotGrid();
    m_grid->setMajorPen(QPen(Qt::gray, 0, Qt::DotLine));
    m_grid->setMinorPen(QPen(Qt::lightGray, 0, Qt::DotLine));
    m_grid->attach(m_plot);
    m_grid->setVisible(m_gridEnabled);

    // Setup legend
    m_legend = new QwtLegend();
    // m_plot->insertLegend(m_legend, QwtPlot::RightLegend);
#if 0
    m_legend->setVisible(m_legendEnabled);
#else
    m_legend->setParent(m_plot->canvas());
    m_legend->setStyleSheet(
        "QwtLegend {"
        "    background-color: transparent;"
        "    color: black;"
        "}"
        "QwtLegendLabel {"
        "    color: black;"
        "}");
    // m_plot->plotLayout()->setLegendPosition(QwtPlot::RightLegend, 0.0);
    m_plot->plotLayout()->setCanvasMargin(0);            // 去除画布外边距
    m_plot->setContentsMargins(0, 0, 0, 0);              // QWidget 边距
    m_plot->axisWidget(QwtPlot::yLeft)->setMargin(0);    // Y轴刻度控件边距
    m_plot->plotLayout()->setAlignCanvasToScales(true);  // 画布紧贴刻度
    m_legend->show();
#endif
    // Set canvas background
    m_plot->setCanvasBackground(QBrush(Qt::white));

    // Enable auto-replot
    m_plot->setAutoReplot(true);

    // Setup update timer
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &xQwtChartWidget::updateRealTimeData);
}

void xQwtChartWidget::setupInteraction() {
#if 0
    // Zooming
    m_zoomer = new QwtPlotZoomer(m_plot->canvas());
    m_zoomer->setRubberBandPen(QPen(Qt::blue, 1, Qt::DashLine));
    m_zoomer->setTrackerPen(QPen(Qt::red));
    m_zoomer->setEnabled(m_zoomingEnabled);
    //// 禁用右键复位
    m_zoomer->setMousePattern(QwtEventPattern::MouseSelect2, Qt::NoButton);
    //// 保留左键框选缩放
    //m_zoomer->setMousePattern(QwtEventPattern::MouseSelect1, Qt::LeftButton);
#endif

    // Panning
    m_panner = new QwtPlotPanner(m_plot->canvas());
    m_panner->setMouseButton(Qt::MiddleButton);
    m_panner->setEnabled(m_panningEnabled);

    // Magnifier
    m_magnifier = new QwtPlotMagnifier(m_plot->canvas());
    m_magnifier->setWheelFactor(1.1);
    m_magnifier->setEnabled(m_magnifierEnabled);

    // Mouse tracking picker
    m_picker = new CoordinatePlotPicker(m_plot->canvas());
    m_picker->setStateMachine(new QwtPickerTrackerMachine());
    m_picker->setTrackerMode(QwtPicker::AlwaysOn);
    m_picker->setRubberBand(QwtPicker::NoRubberBand);
    m_picker->setEnabled(m_mouseTrackingEnabled);
}

void xQwtChartWidget::connectSignals() {
    // Connect plot signals for interaction
    // Note: QWT doesn't provide direct click signals, would need custom implementation
    connect(m_plot, &xQwtPlot::scalesChanged, this, &xQwtChartWidget::updateTitlePosition);
    // connect(m_plot, &QwtPlot::itemAttached, this, &xQwtChartWidget::handleItemAttached);

    // Connect picker signals for mouse tracking
    if (m_picker) {
        connect(m_picker, QOverload<const QPointF&>::of(&QwtPlotPicker::moved), this,
                &xQwtChartWidget::onMouseMoved);
    }
}

void xQwtChartWidget::handleItemAttached(QwtPlotItem* plotItem, bool on) {
    // 本函数目前不用

    if (!on) {
        return;
    }

    // 檢查這個項目是不是一根曲線
    auto* curve = dynamic_cast<QwtPlotCurve*>(plotItem);
    if (!curve) {
        return;
    }

    QVariant itemInfo = QVariant::fromValue(curve);
    QList<QWidget*> legendWidgets = m_legend->legendWidgets(itemInfo);
    for (QWidget* widget : legendWidgets) {
        auto* legendLabel = dynamic_cast<QwtLegendLabel*>(widget);
        if (legendLabel) {
            // 取得曲線的顏色
            QColor curveColor = curve->pen().color();

            // 取得標籤目前的文字(QwtText)
            QwtText labelText = legendLabel->text();

            // 將文字的顏色設定為曲線的顏色
            labelText.setColor(curveColor);

            // 將修改後的文字設定回去
            legendLabel->setText(labelText);
        }
    }
}

void xQwtChartWidget::setTitle(const QString& title) {
    m_title = title;

    // 移除QWT plot的外部标题（不占用空间）
    m_plot->setTitle(QwtText(""));

    // 删除旧的标题标记
    if (m_titleMarker) {
        m_titleMarker->detach();
        delete m_titleMarker;
        m_titleMarker = nullptr;
    }

    // 创建内部标题标记（左上角）
    if (!title.isEmpty()) {
        m_titleMarker = new QwtPlotMarker();

        // 设置文字内容和样式
        QwtText titleText(title);
        titleText.setFont(QFont("Arial", 8));  // 增大字体
        titleText.setColor(Qt::black);
        titleText.setBackgroundBrush(QBrush(QColor(255, 255, 255, 220)));  // 更不透明的背景
        m_titleMarker->setLabel(titleText);

        // 设置标记位置和对齐方式
        m_titleMarker->setLabelAlignment(Qt::AlignCenter | Qt::AlignTop);
        m_titleMarker->setItemAttribute(QwtPlotItem::AutoScale, false);

        // 设置临时位置，稍后在updateTitlePosition中用像素坐标重新定位
        m_titleMarker->setValue(0, 0);

        // 附加到plot
        m_titleMarker->attach(m_plot);

        // 更新标题位置（使用像素坐标）
        updateTitlePosition();
    }

    m_plot->replot();
}

void xQwtChartWidget::setXAxisTitle(const QString& title) {
    m_xAxisTitle = title;
    m_plot->setAxisTitle(QwtAxis::XBottom, title);
    m_plot->replot();
}

void xQwtChartWidget::setYAxisTitle(const QString& title) {
    m_yAxisTitle = title;
    m_plot->setAxisTitle(QwtAxis::YLeft, title);
    m_plot->replot();
}

void xQwtChartWidget::setAxisScale(QwtAxisId axisId, double min, double max) {
    // 兜底防御：禁止把 axis range 设成非有限或零宽。
    // QwtScaleMap::transform 的换算因子 = (p2 - p1) / (s2 - s1)，
    //   s2 == s1  -> 因子 = inf  -> 后续 transform 会得到 inf；
    //   s2 == s1 且 val == s1 -> 0 * inf -> NaN。
    // 一旦绘制时把 NaN 坐标喂给 QPainter::drawPolyline，就会进入 QStroker
    // 的 QLineF::setLength()，那里 oldLength = sqrt(dx^2+dy^2) = NaN
    // 导致 Q_ASSERT(qIsFinite(oldLength)) 触发，整个进程被 qFatal abort。
    // 这条路径在"刚加完一条曲线、还没采到任何点"的瞬间最容易触发。
    if (!std::isfinite(min) || !std::isfinite(max)) {
        min = 0.0;
        max = 1.0;
    } else if (max <= min) {
        const double center = (min + max) * 0.5;
        const double half = std::max(std::abs(center) * 1e-6, 0.5);
        min = center - half;
        max = center + half;
    }
    m_plot->setAxisScale(axisId, min, max);
    m_plot->replot();
}

void xQwtChartWidget::enableGrid(bool enable) {
    m_gridEnabled = enable;
    if (m_grid) {
        m_grid->setVisible(enable);
        m_plot->replot();
    }
}

void xQwtChartWidget::enableLegend(bool enable) {
    m_legendEnabled = enable;
    if (m_legend) {
        m_legend->setVisible(enable);
        m_plot->replot();
    }
}

QwtPlotCurve* xQwtChartWidget::getCurve(int curveId) const {
    auto it = m_curves.find(curveId);
    if (it != m_curves.end()) {
        return it.value()->curve;
    }
    return nullptr;
}

int xQwtChartWidget::addCurve(const QString& title, const QColor& color) {
    int id = m_nextCurveId++;

    CurveData* curveData = new CurveData();
    curveData->curve = new QwtPlotCurve(title);
    curveData->name = title;
    curveData->color = color.isValid() ? color : getDefaultColor(m_curves.size());
    curveData->visible = true;
    curveData->id = id;

    // Configure curve appearance
    curveData->curve->setPen(QPen(curveData->color, 2));
    curveData->curve->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    curveData->curve->attach(m_plot);

    m_curves[id] = curveData;

    if (m_legend) {
        QList<QwtLegendData> legendData = curveData->curve->legendData();
        QVariant itemInfo = QVariant::fromValue(curveData->curve);
        m_legend->updateLegend(itemInfo, legendData);
        QList<QWidget*> legendWidgets = m_legend->legendWidgets(itemInfo);
        for (QWidget* widget : legendWidgets) {
            auto* legendLabel = dynamic_cast<QwtLegendLabel*>(widget);
            if (legendLabel) {
                QwtText labelText = legendLabel->text();
                labelText.setColor(curveData->color);  // 使用曲線顏色
                legendLabel->setText(labelText);
            }
        }
    }

    updateLegendPosition();

    m_plot->replot();

    return id;
}

void xQwtChartWidget::removeCurve(int curveId) {
    auto it = m_curves.find(curveId);
    if (it != m_curves.end()) {
        if (m_legend) {
            QVariant itemInfo = QVariant::fromValue(it.value()->curve);
            m_legend->updateLegend(itemInfo, {});
        }

        // CurveData destructor will handle curve deletion
        delete it.value();
        m_curves.erase(it);
        m_plot->replot();
    }
}

void xQwtChartWidget::removeCurvesFromId(int minId) {
    QVector<int> ids;
    ids.reserve(m_curves.size());
    for (auto it = m_curves.constBegin(); it != m_curves.constEnd(); ++it) {
        if (it.key() >= minId) {
            ids.append(it.key());
        }
    }
    std::sort(ids.begin(), ids.end(), std::greater<int>());
    for (int id : ids) {
        removeCurve(id);
    }
    m_nextCurveId = 0;
    for (auto it = m_curves.constBegin(); it != m_curves.constEnd(); ++it) {
        m_nextCurveId = qMax(m_nextCurveId, it.key() + 1);
    }
}

void xQwtChartWidget::removeAllCurves() {
    for (auto curvePtr : m_curves) {
        if (m_legend && curvePtr->curve) {
            QVariant itemInfo = QVariant::fromValue(curvePtr->curve);
            m_legend->updateLegend(itemInfo, {});
        }
        delete curvePtr;
    }
    m_curves.clear();
    // Curve IDs are used by setCurveData(). When we remove all curves and re-add,
    // we must reset the counter, otherwise new curves may not be 0/1 anymore.
    m_nextCurveId = 0;

    if (m_legend) {
        updateLegendPosition();
    }

    m_plot->replot();
}

void xQwtChartWidget::setCurveData(int curveId, const QVector<double>& xData,
                                   const QVector<double>& yData) {
    auto it = m_curves.find(curveId);
    if (it == m_curves.end()) return;

    // ────────────────────────────────────────────────────────────────────────────
    // NaN 处理策略（重要，关系到多曲线时间戳对齐）：
    //
    // 上游约定：NaN 只会"成片地"出现在数组头部（这条曲线还没采到样的早期阶段
    // 用 NaN 占位），中间和尾部不会出现 NaN（断采时改成"沿用最后一个值"）。
    //
    // 因此正确做法是 *不修改原数据、不丢任何点*，只是从第一个 finite 点开始
    // 喂给 Qwt 绘制即可：x/y 数组下标和数值都不动，N 条曲线的时间戳天然对齐，
    // 头部 NaN 段对应的就是"曲线还不存在的那段时间"，视觉上是空白。
    //
    // 兜底：万一中间真的混进了非有限值（上游 bug），就在第一个 mid-NaN 处截断，
    // 既能避免 QPainter::drawPolyline -> QStroker -> QLineF::setLength() 的
    // Q_ASSERT(qIsFinite(oldLength)) 崩进程，又能 qWarning 提醒去查上游。
    // ────────────────────────────────────────────────────────────────────────────
    const int n = std::min(xData.size(), yData.size());
    const double* xPtr = xData.constData();
    const double* yPtr = yData.constData();

    int first = 0;
    while (first < n && (!std::isfinite(xPtr[first]) || !std::isfinite(yPtr[first]))) {
        ++first;
    }

    int last = n;  // exclusive
    for (int i = first; i < n; ++i) {
        if (!std::isfinite(xPtr[i]) || !std::isfinite(yPtr[i])) {
            qWarning("xQwtChartWidget::setCurveData: unexpected non-finite sample at "
                     "index %d (curveId=%d, total=%d, leading-nan=%d). Truncating here "
                     "to avoid QLineF::setLength assert; please fix the upstream source.",
                     i, curveId, n, first);
            last = i;
            break;
        }
    }

    const int count = last - first;
    if (count > 0) {
        it.value()->curve->setSamples(xPtr + first, yPtr + first, count);
    } else {
        // 全 NaN 或没有数据，清空曲线
        static const double kEmpty = 0.0;
        it.value()->curve->setSamples(&kEmpty, &kEmpty, 0);
    }
    m_plot->replot();
}

void xQwtChartWidget::setCurveColor(int curveId, const QColor& color) {
    auto it = m_curves.find(curveId);
    if (it != m_curves.end()) {
        it.value()->color = color;
        it.value()->curve->setPen(QPen(color, 2));
        m_plot->replot();
    }
}

void xQwtChartWidget::setCurveStyle(int curveId, QwtPlotCurve::CurveStyle style) {
    auto it = m_curves.find(curveId);
    if (it != m_curves.end()) {
        it.value()->curve->setStyle(style);
        m_plot->replot();
    }
}

void xQwtChartWidget::setCurveVisible(int curveId, bool visible) {
    auto it = m_curves.find(curveId);
    if (it != m_curves.end()) {
        it.value()->visible = visible;
        it.value()->curve->setVisible(visible);
        m_plot->replot();
    }
}

void xQwtChartWidget::setCurveWidth(int curveId, int pixel) {
    auto it = m_curves.find(curveId);
    if (it != m_curves.end()) {
        auto pen = it.value()->curve->pen();
        pen.setWidth(pixel);
        it.value()->curve->setPen(pen);
        m_plot->replot();
    }
}

void xQwtChartWidget::setAutoUpdate(bool enable, int intervalMs) {
    m_autoUpdate = enable;
    if (enable && m_updateTimer) {
        m_updateTimer->start(intervalMs);
    } else if (m_updateTimer) {
        m_updateTimer->stop();
    }
}

void xQwtChartWidget::updatePlot() {
    updateAxisScales();
    m_plot->replot();

    // 在replot之后更新标题位置，确保坐标转换正确
    QTimer::singleShot(0, this, [this]() { updateTitlePosition(); });
}

void xQwtChartWidget::enableZooming(bool enable) {
    m_zoomingEnabled = enable;
    if (m_zoomer) {
        m_zoomer->setEnabled(enable);
    }
}

void xQwtChartWidget::enablePanning(bool enable) {
    m_panningEnabled = enable;
    if (m_panner) {
        m_panner->setEnabled(enable);
    }
}

void xQwtChartWidget::enableMagnifier(bool enable) {
    m_magnifierEnabled = enable;
    if (m_magnifier) {
        m_magnifier->setEnabled(enable);
    }
}

void xQwtChartWidget::resetZoom() {
    if (m_zoomer) {
        m_zoomer->zoom(0);  // Zoom out to base rectangle
    }
}

void xQwtChartWidget::addHorizontalLine(const QString& label, double yValue, const QColor& color,
                                        int labelFontPointSize) {
    if (m_markers.contains(label)) {
        removeMarker(label);
    }

    QwtPlotMarker* marker = new QwtPlotMarker(label);
    marker->setLineStyle(QwtPlotMarker::HLine);
    // 确保线条使用相同的pen设置，使用CosmeticPen确保线条宽度不受变换影响
    QPen pen(color, 1, Qt::DashLine);
    pen.setCosmetic(true);  // 确保线条宽度始终是1像素，不受缩放影响
    marker->setLinePen(pen);
    marker->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    marker->setYValue(yValue);
    if (labelFontPointSize) {
        QwtText text(label);
        if (labelFontPointSize > 0) {
            QFont f = text.font();
            f.setPointSize(labelFontPointSize);
            text.setFont(f);
        }
        marker->setLabel(text);
    }
    marker->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    marker->attach(m_plot);

    m_markers[label] = marker;
    m_plot->replot();
}

void xQwtChartWidget::addVerticalLine(const QString& label, double xValue, const QColor& color) {
    addKeyedVerticalLine(label, label, xValue, color);
}

// key 与显示文字分开的版本。旧的单参数重载拿标签当字典 key，标注内容一变（例如段间隔从
// "2天3小时" 变成 "2天4小时"）旧 marker 就成了删不掉的孤儿，而两条内容恰好相同的竖线又会
// 互相顶掉。调用方自己维护稳定 key 时用这个。
void xQwtChartWidget::addKeyedVerticalLine(const QString& key, const QString& label, double xValue,
                                           const QColor& color) {
    if (m_markers.contains(key)) {
        removeMarker(key);
    }

    QwtPlotMarker* marker = new QwtPlotMarker(key);
    marker->setLineStyle(QwtPlotMarker::VLine);
    // 确保线条使用相同的pen设置，使用CosmeticPen确保线条宽度不受变换影响
    QPen pen(color, 1, Qt::DashLine);
    pen.setCosmetic(true);  // 确保线条宽度始终是1像素，不受缩放影响
    marker->setLinePen(pen);
    marker->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    marker->setXValue(xValue);
    marker->setLabel(QwtText(label));
    marker->setLabelAlignment(Qt::AlignTop | Qt::AlignRight);
    marker->attach(m_plot);

    m_markers[key] = marker;
    m_plot->replot();
}

void xQwtChartWidget::hideAxisBackbones() {
    if (!m_plot) return;
    for (int axis : {QwtAxis::YLeft, QwtAxis::YRight, QwtAxis::XBottom, QwtAxis::XTop}) {
        QwtScaleWidget* scaleWidget = m_plot->axisWidget(axis);
        if (!scaleWidget) continue;
        QwtScaleDraw* draw = scaleWidget->scaleDraw();
        if (!draw) continue;
        draw->enableComponent(QwtAbstractScaleDraw::Backbone, false);
    }
}

void xQwtChartWidget::updateKeyedVerticalLine(const QString& key, const QString& label,
                                              double xValue, const QColor& color) {
    auto it = m_markers.find(key);
    if (it == m_markers.end()) {
        // 第一次：按 add 的样式建出来（它自带一次 replot，只发生在首帧）。
        addKeyedVerticalLine(key, label, xValue, color);
        return;
    }
    QwtPlotMarker* marker = it.value();
    // x、文字、颜色三样都要跟着调用方走，不能只认 x：界面语言是可以在运行期切的，
    // 切完下一拍传进来的 label 就从 "Now" 变成 "当前"；只比 x 的话旧文字会一直挂到
    // 这条线被删掉为止。
    if (marker->xValue() != xValue) marker->setXValue(xValue);
    if (marker->label().text() != label) marker->setLabel(QwtText(label));
    if (marker->linePen().color() != color) {
        QPen pen(color, 1, Qt::DashLine);
        pen.setCosmetic(true);
        marker->setLinePen(pen);
    }
    // 刻意不 replot：调用方每拍末尾都会 updatePlot()，这里再来一次就是一拍两次全图重绘。
}

void xQwtChartWidget::removeMarkersWithPrefix(const QString& prefix) {
    bool removedAny = false;
    for (auto it = m_markers.begin(); it != m_markers.end();) {
        if (!it.key().startsWith(prefix)) {
            ++it;
            continue;
        }
        it.value()->detach();
        delete it.value();
        it = m_markers.erase(it);
        removedAny = true;
    }
    if (removedAny) m_plot->replot();
}

void xQwtChartWidget::removeMarker(const QString& label) {
    if (m_markers.contains(label)) {
        QwtPlotMarker* marker = m_markers[label];
        marker->detach();
        delete marker;
        m_markers.remove(label);
        m_plot->replot();
    }
}

void xQwtChartWidget::removeAllMarkers() {
    for (auto it = m_markers.begin(); it != m_markers.end(); ++it) {
        it.value()->detach();
        delete it.value();
    }
    m_markers.clear();
    m_plot->replot();
}

void xQwtChartWidget::exportToPng(const QString& fileName, int width, int height) {
    QwtPlotRenderer renderer;
    // Convert pixels to millimeters (assuming 96 DPI)
    double widthMM = width * 25.4 / 96.0;
    double heightMM = height * 25.4 / 96.0;
    renderer.exportTo(m_plot, fileName, QSizeF(widthMM, heightMM));
}

void xQwtChartWidget::exportToPdf(const QString& fileName) {
    QwtPlotRenderer renderer;
    renderer.exportTo(m_plot, fileName, QSizeF(210, 297));  // A4 size in mm
}

void xQwtChartWidget::onDataChanged() {
    if (m_autoUpdate) {
        updatePlot();
    }
}

void xQwtChartWidget::updateRealTimeData() { onDataChanged(); }

QColor xQwtChartWidget::getDefaultColor(int index) const {
    if (index >= 0 && index < static_cast<int>(DEFAULT_COLORS.size())) {
        return QColor::fromRgb(DEFAULT_COLORS[index]);
    } else {
        // Generate a color based on index for cases with many curves
        int hue = (index * 137) % 360;  // Golden angle distribution
        return QColor::fromHsv(hue, 200, 200);
    }
}

void xQwtChartWidget::updateAxisScales() {
    // 只有在启用自动轴缩放时才进行自动缩放
    if (m_autoAxisScaling) {
        // Auto-scale axes based on current data
        m_plot->setAxisAutoScale(QwtAxis::XBottom);
        m_plot->setAxisAutoScale(QwtAxis::YLeft);
    }
}

void xQwtChartWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_plot) {
        m_plot->replot();

        // 在replot之后更新标题位置
        QTimer::singleShot(0, this, [this]() {
            updateTitlePosition();
            updateLegendPosition();
        });

        if (m_legend) {
            // 取得繪圖畫布的矩形區域
            const QRectF canvasRect = m_plot->canvas()->contentsRect();

            // 取得圖例為了容納所有項目所需要的最佳大小
            const QSize legendSize = m_legend->sizeHint();

            // 設定一個邊界值 (margin)
            const int margin = 10;

            // 計算圖例的左上角 x, y 座標
            const int x = canvasRect.right() - legendSize.width() - margin;
            const int y = canvasRect.top() + margin;

            // 將圖例移動到計算出的位置
            m_legend->move(x, y);
        }
    }
}

void xQwtChartWidget::updateTitlePosition() {
    if (!m_titleMarker || m_updatingTitlePosition) return;

    // setValue() can auto-replot and synchronously emit scalesChanged again.
    QScopedValueRollback<bool> updatingTitlePosition(m_updatingTitlePosition, true);

    const QwtScaleMap xMap = m_plot->canvasMap(QwtAxis::XBottom);
    const QwtScaleMap yMap = m_plot->canvasMap(QwtAxis::YLeft);

    double plotAreaLeftPixel = xMap.p1();
    double plotAreaRightPixel = xMap.p2();
    double plotAreaTopPixel = yMap.p2();

    int pixelX = (plotAreaLeftPixel + plotAreaRightPixel) / 2;  // 水平居中
    int pixelY = plotAreaTopPixel + titleTopOffsetPixels();     // 顶部稍微下移一点

    double dataX = xMap.invTransform(pixelX);
    double dataY = yMap.invTransform(pixelY);

    // A grid cell can have a zero-size canvas while its layout is being built.
    // NaN never compares equal in QwtPlotMarker::setValue(), so assigning it
    // repeatedly triggers auto-replot -> scalesChanged -> this slot until overflow.
    if (!std::isfinite(dataX) || !std::isfinite(dataY)) return;

    m_titleMarker->setValue(dataX, dataY);
}

void xQwtChartWidget::updateLegendPosition() {
    if (m_legend && m_legend->isVisible()) {
        // 取得繪圖畫布的矩形區域
        const QRectF canvasRect = m_plot->canvas()->contentsRect();

        // 取得圖例為了容納所有項目所需要的最佳大小
        const QSize legendSize = m_legend->sizeHint();

        // 設定一個邊界值 (margin)
        const int margin = 10;

        // 計算圖例的左上角 x, y 座標
        const int x = canvasRect.right() - legendSize.width() - margin;
        const int y = canvasRect.top() + margin;

        // 使用 setGeometry 同時設定位置和尺寸，確保圖例能正確顯示
        m_legend->setGeometry(x, y, legendSize.width(), legendSize.height());
    }
}

void xQwtChartWidget::enableMouseTracking(bool enable) {
    m_mouseTrackingEnabled = enable;
    if (m_picker) {
        m_picker->setEnabled(enable);
    }

    // 如果禁用鼠标跟踪，隐藏坐标标记
    if (!enable && m_coordinateMarker) {
        m_coordinateMarker->setVisible(false);
        m_plot->replot();
    }
}

void xQwtChartWidget::setCoordinateDisplayFormat(const QString& format) {
    m_coordinateFormat = format;
}

void xQwtChartWidget::setXAxisCoordinateDecimals(int decimals) {
    if (auto* picker = dynamic_cast<CoordinatePlotPicker*>(m_picker)) {
        picker->setXAxisDecimals(decimals);
    }
}

void xQwtChartWidget::onMouseMoved(const QPointF& position) {
    if (!m_mouseTrackingEnabled) {
        return;
    }

    //@TODO customize coordinateMarker
    return;

    // 创建坐标标记（如果不存在）
    if (!m_coordinateMarker) {
        m_coordinateMarker = new QwtPlotMarker();
        m_coordinateMarker->setItemAttribute(QwtPlotItem::AutoScale, false);
        m_coordinateMarker->setZ(1000);  // 确保在最上层显示
        m_coordinateMarker->attach(m_plot);
    }

    // 格式化坐标文本
    // QString coordinateText = QString(m_coordinateFormat).arg(position.x()).arg(position.y());

    // 设置标记文本和样式
    // QwtText labelText(coordinateText);
    // labelText.setFont(QFont("Arial", 6));
    // labelText.setColor(Qt::black);
    // labelText.setBackgroundBrush(QBrush(QColor(255, 255, 200, 200))); // 淡黄色半透明背景
    // labelText.setBorderPen(QPen(Qt::black, 1));

    // m_coordinateMarker->setLabel(labelText);

    // 计算标记位置（鼠标位置右上方偏移）
    const QwtScaleMap xMap = m_plot->canvasMap(QwtAxis::XBottom);
    const QwtScaleMap yMap = m_plot->canvasMap(QwtAxis::YLeft);

    // 将鼠标位置转换为像素坐标
    double mousePixelX = xMap.transform(position.x());
    double mousePixelY = yMap.transform(position.y());

    // 偏移标记位置（右上方）
    double offsetPixelX = mousePixelX + 10;
    double offsetPixelY = mousePixelY - 10;

    // 转换回数据坐标
    double offsetDataX = xMap.invTransform(offsetPixelX);
    double offsetDataY = yMap.invTransform(offsetPixelY);

    m_coordinateMarker->setValue(offsetDataX, offsetDataY);
    m_coordinateMarker->setLabelAlignment(Qt::AlignLeft | Qt::AlignBottom);
    m_coordinateMarker->setVisible(true);

    m_plot->replot();
}

void xQwtChartWidget::paintEvent(QPaintEvent* event) { QWidget::paintEvent(event); }
