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
