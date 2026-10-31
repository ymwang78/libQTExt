#pragma once
// ***************************************************************
//  xChart   version:  1.0   -  date:  2025/08/13
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************

#include <qwt_scale_draw.h>
#include <qwt_text.h>
#include <qwt_plot.h>
#include <qwt_plot_zoomer.h>
#include <qwt_plot_magnifier.h>
#include <qwt_scale_div.h>
#include <qwt_interval.h>
#include <qwt_date_scale_draw.h>
#include <qwt_date_scale_engine.h>
#include <qwt_plot_legenditem.h>
#include <QBrush>
#include <QColor>
#include <QHash>
#include <QPen>
#include <QPointer>
#include <QString>
#include <cmath>  // for std::abs

#include "xTimeAxisFormat.h"
#include "xValueRange.h"

class QFrame;
class QRadioButton;
class QwtPlotCurve;

class xQwtScaleDraw : public QwtScaleDraw {
  public:
    xQwtScaleDraw() = default;

    // 重写核心的 label() 方法
    virtual QwtText label(double value) const override {
        // 您可以自定义触发科学计数法的阈值
        // 例如，当数值的绝对值大于9999时，启用科学计数法
        if (std::abs(value) > 9999) {
            // 'e' 表示科学计数法，2 表示保留两位小数
            return QString::number(value, 'e', 2);
        }

        // 如果数值在阈值内，则使用QWT默认的格式
        return QwtScaleDraw::label(value);
    }
};

// 时间轴刻度: 文字走 xTimeAxisLabel 的精简格式 ("2016-10-06  10-13  10-20 ..."),
// 取代 QwtDateScaleDraw 默认的 "周四 06 10月 2016"; setDateFormat() 设的格式不再起作用。
// 轴上的值按 Qwt 约定是 1970 起的毫秒数。配 QwtDateScaleEngine 刻度才落在整天 / 整点上,
// 用 xQwtSetTimeAxis() 一次装好。写在头文件里, 不用它的使用者不会编出对它的引用。
class xQwtDateScaleDraw : public QwtDateScaleDraw {
  public:
    explicit xQwtDateScaleDraw(Qt::TimeSpec spec = Qt::LocalTime) : QwtDateScaleDraw(spec) {}

    QwtText label(double value) const override {
        // 只看会画出来的主刻度 (落在轴范围内), "第一个刻度"才是屏幕上最左边那个
        const QwtScaleDiv& div = scaleDiv();
        QList<double> ticks;
        for (double t : div.ticks(QwtScaleDiv::MajorTick))
            if (div.contains(t)) ticks.append(t);
        const int prev = xPreviousTickIndex(ticks, value);
        return xTimeAxisLabel(toDateTime(value), prev >= 0 ? toDateTime(ticks[prev]) : QDateTime(),
                              timeUnit(intervalType(div)));
    }

  private:
    static xTimeUnit timeUnit(QwtDate::IntervalType type) {
        switch (type) {
        case QwtDate::Millisecond: return xTimeUnit::Millisecond;
        case QwtDate::Second:      return xTimeUnit::Second;
        case QwtDate::Minute:      return xTimeUnit::Minute;
        case QwtDate::Hour:        return xTimeUnit::Hour;
        case QwtDate::Day:         return xTimeUnit::Day;
        case QwtDate::Week:        return xTimeUnit::Week;
        case QwtDate::Month:       return xTimeUnit::Month;
        case QwtDate::Year:        return xTimeUnit::Year;
        }
        return xTimeUnit::Second;
    }
};

// 把 plot 的 axis 设成时间轴: xQwtDateScaleDraw + 按日历对齐刻度的 QwtDateScaleEngine。
inline void xQwtSetTimeAxis(QwtPlot* plot, QwtAxisId axis = QwtAxis::XBottom,
                            Qt::TimeSpec spec = Qt::LocalTime) {
    if (!plot) return;
    plot->setAxisScaleDraw(axis, new xQwtDateScaleDraw(spec));
    plot->setAxisScaleEngine(axis, new QwtDateScaleEngine(spec));
}

// 把图例叠在画布角上 (QwtPlotLegendItem), 不像 insertLegend() 那样在图外另占一列。
// 单列、半透明白底加细边框, 底下的曲线仍隐约可见; 只列出带 QwtPlotItem::Legend 属性的项。
// 图例项归 plot 所有, 返回它是为了调用方再改位置 / 字体。
inline QwtPlotLegendItem* xQwtAttachCanvasLegend(
    QwtPlot* plot, Qt::Alignment alignment = Qt::AlignTop | Qt::AlignRight) {
    if (!plot) return nullptr;
    auto* legend = new QwtPlotLegendItem();
    legend->setAlignmentInCanvas(alignment);
    legend->setOffsetInCanvas(Qt::Horizontal | Qt::Vertical, 8);
    legend->setMaxColumns(1);
    legend->setMargin(6);        // 边框到图例项
    legend->setItemMargin(1);    // 图例项四周
    legend->setItemSpacing(6);   // 色块到文字
    legend->setFont(plot->font());
    legend->setBackgroundMode(QwtPlotLegendItem::LegendBackground);
    legend->setBackgroundBrush(QColor(255, 255, 255, 215));
    legend->setBorderPen(QPen(QColor(0xd5, 0xdd, 0xe7)));
    legend->setBorderRadius(6);
    legend->attach(plot);
    return legend;
}

// 多曲线图的两种纵坐标: 绝对值 (各曲线共用纵轴, 适合同一种量) / 按量程 (每条曲线按自己的
// 量程拉伸成 0-100%, 适合量纲不同的量叠在一起看形状)。开启: new xQwtCurveScaling(plot)。
//  - 画布右上角叠两个单选框切换, 右上角的叠加图例 (xQwtAttachCanvasLegend) 自动让到下面;
//    绝对值模式下一条曲线都没有时单选框隐藏, 单条曲线也能切。文字默认英文, 用 setModeLabels() 换。
//    只要绝对值的图 (各曲线是同一种量) 用 setModeSwitchVisible(false) 把单选框去掉, 仍可用
//    下面的可见部分拟合和纵轴拖动把手。
//  - 按量程时用代理包住每条曲线的数据, 读点时才换算, 不复制数据; 切回绝对值原样放回。
//    之后挂上 (attach) 的曲线自动包上; 曲线挂上之后又替换了数据 (setSamples / setData),
//    要调用 refresh()。纵轴固定 0-100、标题为 "%", 图例每项后面带上量程; 切回时恢复。
//  - 量程由使用方 setCurveRange() 给 (几条曲线要同一量程就设同一个值); 没给的按自己数据的
//    最小 / 最大值外扩 5%。选区之类的百分比用 toValue() 换回原值。
//  - 只处理挂在 yAxis 上的 QwtPlotCurve; 怎么编辑量程、量程从哪来由使用方决定。
class xQwtCurveScaling : public QObject {
    Q_OBJECT

  public:
    enum class Mode { Absolute, PerCurve };
    Q_ENUM(Mode)

    explicit xQwtCurveScaling(QwtPlot* plot, QwtAxisId yAxis = QwtAxis::YLeft);
    ~xQwtCurveScaling() override;

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    void setModeLabels(const QString& absolute, const QString& perCurve);
    // 画布右上角的模式单选框, 默认显示。关掉后不再显示, 右上角的叠加图例也不再为它让位;
    // 不改当前模式, 使用方仍可调 setMode()。
    void setModeSwitchVisible(bool on);
    bool modeSwitchVisible() const { return m_switchVisible; }

    // 量程无效 (上限不大于下限) 等同 clearCurveRange()
    void setCurveRange(QwtPlotCurve* curve, const xValueRange& range);
    void clearCurveRange(QwtPlotCurve* curve);
    // 生效的量程: 使用方给的, 否则由曲线数据算出
    xValueRange curveRange(const QwtPlotCurve* curve) const;
    // 按量程模式下的纵坐标 (百分比) → 这条曲线的原值
    double toValue(const QwtPlotCurve* curve, double percent) const;

    // 曲线数据在挂上之后被替换时调用: 重新包装、重算自动量程
    void refresh();

    // 绝对值模式下纵轴按可见部分 (横轴 xBottom 当前范围内的点) 定范围, 缩放 / 平移横轴、
    // 曲线增减后跟着重算 (按纵轴刻度引擎取整); 可见范围内没有点时纵轴不动。默认关闭:
    // 纵轴照常由 Qwt 按全部数据自动定。按量程模式不受影响 (固定 0-100)。
    void setAbsoluteFitVisible(bool on);
    bool absoluteFitVisible() const { return m_fitVisible; }

    // 纵轴上下界的拖动把手: 纵轴两端各一个小三角, 按住上下拖改上界 / 下界 (上界总大于下界,
    // 拖动时提示当前值), 双击纵轴恢复自动。默认关闭。
    void setYBoundHandles(bool on);
    bool yBoundHandles() const { return m_handles != nullptr; }

    // 手动定纵轴范围 (拖动把手也走这里)。手动之后纵轴不再自动重算 (绝对值模式下的可见部分
    // 拟合暂停), 直到 resetYBounds() 或切换模式; 无效范围 (上界不大于下界) 忽略。
    void setYBounds(double lower, double upper);
    bool yBoundsManual() const { return m_manualY; }
    // 纵轴回到当前模式的自动范围: 绝对值 = 按可见部分拟合 (打开时) 或全部数据自动, 按量程 = 0-100。
    // 使用方可把"双击画布恢复全图" (XAxisOnlyZoomer::resetRequested) 接到这里, 横纵一起恢复。
    void resetYBounds();

  signals:
    void modeChanged(xQwtCurveScaling::Mode mode);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void onItemAttached(QwtPlotItem* item, bool on);
    QList<QwtPlotCurve*> curves() const;
    void applyToCurve(QwtPlotCurve* curve);
    void restoreCurve(QwtPlotCurve* curve);
    void applyAxis();
    void updateOverlay();
    void positionBox();
    void scheduleReplot();
    bool applyVisibleFit();   // 按可见部分设好纵轴返回 true (不 replot); 没有可见点返回 false
    void scheduleFit();

    QwtPlot* m_plot;
    QwtAxisId m_yAxis;
    QWidget* m_canvas;   // 构造时记下: plot 析构时画布的事件里不能再经 m_plot 取
    Mode m_mode = Mode::Absolute;
    QFrame* m_box = nullptr;
    QRadioButton* m_absoluteButton = nullptr;
    QRadioButton* m_perCurveButton = nullptr;
    bool m_switchVisible = true;
    QHash<const QwtPlotItem*, xValueRange> m_ranges;  // 使用方给的量程
    QHash<const QwtPlotItem*, QwtText> m_titles;       // 按量程时改了标题的曲线的原标题
    QHash<const QwtPlotItem*, int> m_legendOffsets;    // 被让到单选框下面的图例原来的纵向偏移
    QwtText m_axisTitle;                               // 切到按量程前的纵轴标题与范围
    bool m_axisAutoScale = true;
    QwtInterval m_axisInterval;
    bool m_replotPending = false;
    bool m_fitVisible = false;
    bool m_fitPending = false;
    bool m_manualY = false;          // 纵轴是手动定的 (拖动把手 / setYBounds)
    QPointer<QWidget> m_handles;     // 纵轴上的拖动把手, 是纵轴控件的子控件
};

class xQwtPlot : public QwtPlot {
    Q_OBJECT

  public:
    explicit xQwtPlot(QWidget* parent = nullptr) : QwtPlot(parent) {}

    void replot() override;

  signals:
    void scalesChanged();
};

/**
 * @brief Custom QwtPlotZoomer that only allows X-axis zooming
 */
class XAxisOnlyZoomer : public QwtPlotZoomer {
    Q_OBJECT

  public:
    // 恢复全图的触发方式, 可组合 (DoubleClick | KeyPress ...)。
    enum class ResetTrigger { None = 0x0, DoubleClick = 0x1, RightClick = 0x2, KeyPress = 0x4 };
    Q_DECLARE_FLAGS(ResetTriggers, ResetTrigger)

    explicit XAxisOnlyZoomer(QWidget* canvas);

    // 哪些操作恢复全图 (resetToFullView)。默认 None: 与以前一样, 双击只发 resetRequested,
    // 由使用方自己恢复, 右键不响应。打开任一方式后:
    //  - 从缩放起点第一次框选放大时, 先把起点更新成当前视图, Qwt 自带的中键后退 / 加减号
    //    才退回真实视图, 不会退到构造时 (曲线还没画) 记下的默认范围;
    //  - RightClick 重新打开本类默认禁用的右键, 与画布右键菜单冲突由使用方权衡;
    //  - KeyPress 要画布有焦点 (点一下或滚一下滚轮), 该键被吃掉, 例如 Esc 不再传给外层弹窗。
    // 拖选模式等把缩放器 setEnabled(false) 时这些都不响应。
    void setResetTriggers(ResetTriggers triggers, int key = Qt::Key_Escape);
    ResetTriggers resetTriggers() const { return m_resetTriggers; }

    // 恢复全图: 横轴按全部数据自动范围, 缩放起点重设为全图, 纵轴不动; 之后发 resetRequested。
    void resetToFullView();

    // 横轴左右界的拖动把手 (同 xQwtCurveScaling::setYBoundHandles): 横轴两端各一个小三角, 按住
    // 左右拖, 三角跟着鼠标、那一界实时改、画图区跟着变, 提示当前界值 (日期轴显示成日期时间);
    // 拖完记进缩放记录 (中键 / 减号可退回), 并发 xAxisZoomed; 双击横轴 = resetToFullView()。
    // 默认关闭。把手不受 setEnabled() 影响, 拖选模式下也能用。
    void setXBoundHandles(bool on);
    bool xBoundHandles() const { return m_xHandles != nullptr; }

    // 框选最小像素宽度阈值。鼠标在画布上单击或仅做微小拖动（拖出来的橡皮筋
    // 矩形宽度 < 该值）时，accept() 会直接返回 false，从而忽略这次缩放，
    // 避免"轻轻一点就把图缩到一小段"的误触。默认 8 像素，0 = 关闭该过滤。
    void setMinSelectionPixels(int px) { m_minSelectionPixels = qMax(0, px); }
    int minSelectionPixels() const { return m_minSelectionPixels; }

    // 框选出的横轴范围默认不小于 0 (时间轴、样本序号轴), 落到负值的部分整段挪回 0 起。
    // 横轴本来就有负值的图 (例如滞后 -τ…+τ) 打开它, 框到哪就是哪; 跨度不小于 10、端点取整照旧。
    void setAllowNegativeX(bool allow) { m_allowNegativeX = allow; }
    bool allowNegativeX() const { return m_allowNegativeX; }

  protected:
    // Override to validate zoom rectangle - only allow X-axis changes
    bool accept(QPolygon& pa) const override;

    // Override end to emit our custom signal
    bool end(bool ok = true) override;

    // X-only: never let the zoom stack rewrite Y-axis scales.
    // Base QwtPlotZoomer::rescale() always applies both axes from zoomRect(),
    // so zoom(0)/zoom(-1) would restore a stale construction-time Y range
    // (often the Qwt default 0..1000) and make trend charts look blown out.
    // Prefer xAxisZoomed for sync; after rescale(), zoomRect() Y is refreshed to
    // the live axis so QwtPlotZoomer::zoomed does not leak a stale Y range.
    void rescale() override;

    // 在画布上双击鼠标 → 发出 resetRequested 信号（外部 chart 据此恢复原始范围）;
    // 打开了 DoubleClick 时先恢复全图
    void widgetMouseDoubleClickEvent(QMouseEvent* event) override;
    // 打开了 RightClick 时右键松开恢复全图
    void widgetMouseReleaseEvent(QMouseEvent* event) override;
    // 打开了 KeyPress 时在画布的事件过滤里接住那个键并吃掉, 不让它再传给外层弹窗
    bool eventFilter(QObject* object, QEvent* event) override;

  signals:
    void xAxisZoomed(const QRectF& rect);  // Signal for synchronization
    void resetRequested();                 // 用户双击画布请求恢复原始视图 / 已恢复全图

  private:
    int m_minSelectionPixels = 8;
    bool m_allowNegativeX = false;
    bool m_resyncingZoomStackY = false;
    ResetTriggers m_resetTriggers = ResetTrigger::None;
    int m_resetKey = Qt::Key_Escape;
    QPointer<QWidget> m_xHandles;   // 横轴上的拖动把手, 是横轴控件的子控件
};

Q_DECLARE_OPERATORS_FOR_FLAGS(XAxisOnlyZoomer::ResetTriggers)


/**
 * @brief Custom QwtPlotMagnifier that only allows X-axis wheel zooming
 */
class XAxisOnlyMagnifier : public QwtPlotMagnifier {
    Q_OBJECT

  public:
    explicit XAxisOnlyMagnifier(QWidget* canvas);

    // 同 XAxisOnlyZoomer::setAllowNegativeX: 默认缩放后的下界不小于 0 (整段右移),
    // 横轴本来就有负值的图打开它。
    void setAllowNegativeX(bool allow) { m_allowNegativeX = allow; }
    bool allowNegativeX() const { return m_allowNegativeX; }

  protected:
    // Override to only magnify X-axis
    void rescale(double factor) override;

  signals:
    void xAxisRescaled(const QRectF& rect);  // Signal for synchronization

  private:
    bool m_allowNegativeX = false;
};