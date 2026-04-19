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

#include <QMouseEvent>

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
