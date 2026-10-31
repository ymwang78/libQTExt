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
#include <qwt_date_scale_draw.h>
#include <qwt_date_scale_engine.h>
#include <QString>
#include <cmath>  // for std::abs

#include "xTimeAxisFormat.h"

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
    explicit XAxisOnlyZoomer(QWidget* canvas);

    // 框选最小像素宽度阈值。鼠标在画布上单击或仅做微小拖动（拖出来的橡皮筋
    // 矩形宽度 < 该值）时，accept() 会直接返回 false，从而忽略这次缩放，
    // 避免"轻轻一点就把图缩到一小段"的误触。默认 8 像素，0 = 关闭该过滤。
    void setMinSelectionPixels(int px) { m_minSelectionPixels = qMax(0, px); }
    int minSelectionPixels() const { return m_minSelectionPixels; }

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

    // 在画布上双击鼠标 → 发出 resetRequested 信号（外部 chart 据此恢复原始范围）
    void widgetMouseDoubleClickEvent(QMouseEvent* event) override;

  signals:
    void xAxisZoomed(const QRectF& rect);  // Signal for synchronization
    void resetRequested();                 // 用户双击画布请求恢复原始视图

  private:
    int m_minSelectionPixels = 8;
    bool m_resyncingZoomStackY = false;
};


/**
 * @brief Custom QwtPlotMagnifier that only allows X-axis wheel zooming
 */
class XAxisOnlyMagnifier : public QwtPlotMagnifier {
    Q_OBJECT

  public:
    explicit XAxisOnlyMagnifier(QWidget* canvas);

  protected:
    // Override to only magnify X-axis
    void rescale(double factor) override;

  signals:
    void xAxisRescaled(const QRectF& rect);  // Signal for synchronization
};