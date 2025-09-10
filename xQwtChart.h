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
#include <QString>
#include <cmath>  // for std::abs

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

  protected:
    // Override to validate zoom rectangle - only allow X-axis changes
    bool accept(QPolygon& pa) const override;

    // Override end to emit our custom signal
    bool end(bool ok = true) override;

  signals:
    void xAxisZoomed(const QRectF& rect);  // Signal for synchronization
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