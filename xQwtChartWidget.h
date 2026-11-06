#pragma once
// ***************************************************************
//  xQwtChartWidget   version:  1.0   -  date:  2026/11/06
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// 基于 QWT 的趋势图部件：一个 xQwtPlot 加上曲线登记（整型 id）、画布内标题叠层、
// 限值线 / 竖线标记、平移 / 滚轮缩放 / 坐标跟踪的装配，以及两条防崩溃兜底
// （见 setCurveData 与 setAxisScale）。它从 KeelDeck 的
// applications/TaijiMPC/widgets/ChartWidget_QWT 搬来，去掉了与具体工程类型的绑定；
// 工程相关的派生（限值线、X 轴同步、工程指针）由使用方自己继承。
//
// 与 libQTExt 的其它控件一样没有导出宏：使用方把 .cpp 编进自己的二进制。同一进程里
// 多个模块各编一份是允许的（本类没有进程级状态），实例不跨模块传递。
// ***************************************************************

#include <QWidget>
#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTimer>
#include <QVector>
#include <QColor>
#include <QRect>
#include <array>
#include <memory>

// QWT includes
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_zoomer.h>
#include <qwt_plot_magnifier.h>
#include <qwt_legend.h>
#include <qwt_curve_fitter.h>
#include <qwt_point_data.h>
#include <qwt_painter.h>
#include <qwt_plot_marker.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>
#include <qwt_plot_layout.h>
#include <qwt_plot_picker.h>
#include <qwt_picker_machine.h>

class xQwtPlot;

/**
 * @brief QWT-based chart widget
 *
 * A QwtPlot wrapped with:
 * - Multiple curves addressed by integer id
 * - Interactive panning and wheel magnification
 * - Periodic replot timer for real-time data
 * - Grid, in-canvas legend and an in-canvas title marker
 * - Horizontal / vertical marker lines with stable keys
 * - PNG / PDF export and mouse coordinate tracking
 */
class xQwtChartWidget : public QWidget {
    Q_OBJECT

  public:
    explicit xQwtChartWidget(QWidget* parent = nullptr);
    ~xQwtChartWidget();

    // 提供给子类访问内部plot的方法
    xQwtPlot* getPlot() const { return m_plot; }

    // Plot configuration
    void setTitle(const QString& title);
    void setXAxisTitle(const QString& title);
    void setYAxisTitle(const QString& title);
    void setAxisScale(QwtAxisId axisId, double min, double max);
    void enableGrid(bool enable = true);
    void enableLegend(bool enable = true);
    /**
     * 关掉四个轴的轴线（backbone），让画布边框兼任轴线。
     *
     * 不关的话画面上是两条线：画布用的是 QwtPlotCanvas 默认的 Panel|Sunken 边框，而轴线
     * 画在刻度控件那一侧，两者差开两个像素，看上去像套了双层框。
     *
     * setAxisScaleDraw() 会把刻度绘制对象整个换掉、各组件恢复成默认（轴线又开着），
     * 所以**每次换过 scaleDraw 之后都要再调一次**。
     */
    void hideAxisBackbones();

    // Curve management
    QwtPlotCurve* getCurve(int curveId) const;
    int addCurve(const QString& title, const QColor& color = Qt::blue);
    void removeCurve(int curveId);
    /** Remove all curves whose id is >= minId; recompute next curve id from remaining curves. */
    void removeCurvesFromId(int minId);
    void removeAllCurves();
    void setCurveData(int curveId, const QVector<double>& xData, const QVector<double>& yData);
    void setCurveColor(int curveId, const QColor& color);
    void setCurveStyle(int curveId, QwtPlotCurve::CurveStyle style);
    void setCurveVisible(int curveId, bool visible);
    void setCurveWidth(int curveId, int pixel);

    // Real-time updates
    void setAutoUpdate(bool enable, int intervalMs = 1000);
    void updatePlot();

    // Plot interaction
    void enableZooming(bool enable = true);
    void enablePanning(bool enable = true);
    void enableMagnifier(bool enable = true);
    void resetZoom();

    // Limit lines and markers
    void addHorizontalLine(const QString& label, double yValue, const QColor& color = Qt::red,
                           int labelFontPointSize = -1);
    void addVerticalLine(const QString& label, double xValue, const QColor& color = Qt::red);
    /** Vertical line whose dictionary key is independent of the label text (see the .cpp note). */
    void addKeyedVerticalLine(const QString& key, const QString& label, double xValue,
                              const QColor& color = Qt::red);
    /**
     * Move a keyed vertical line in place, creating it on first use.
     *
     * addKeyedVerticalLine() rebuilds the marker and replots on every call; a line that moves
     * once per sampling tick (the current-time divider) would then cost a full redraw per tick
     * on top of the one its caller already does. This one only writes the x value and never
     * replots by itself — the caller's own updatePlot() publishes it.
     */
    void updateKeyedVerticalLine(const QString& key, const QString& label, double xValue,
                                 const QColor& color = Qt::red);
    void removeMarker(const QString& label);
    /** Drop every marker whose key starts with prefix; used to refresh a whole family at once. */
    void removeMarkersWithPrefix(const QString& prefix);
    void removeAllMarkers();

    // Export functionality
    void exportToPng(const QString& fileName, int width = 800, int height = 600);
    void exportToPdf(const QString& fileName);

    // Mouse tracking and coordinate display
    void enableMouseTracking(bool enable = true);
    void setCoordinateDisplayFormat(const QString& format = "X: %.2f, Y: %.2f");
    void setXAxisCoordinateDecimals(int decimals);

  public slots:
    void onDataChanged();
    void updateRealTimeData();

  signals:
    void plotClicked(const QPointF& position);
    void curveClicked(int curveId, const QPointF& position);
    void zoomChanged(const QRectF& zoomRect);

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

    // 设置是否使用自动轴缩放（子类可以禁用以使用手动设置）
    void setAutoAxisScaling(bool enable) { m_autoAxisScaling = enable; }

  private slots:
    void handleItemAttached(QwtPlotItem* plotItem, bool on);
    void onMouseMoved(const QPointF& position);

  protected:
    void setupUI();
    void setupPlot();
    void setupInteraction();
    void connectSignals();
    // Helper functions
    QColor getDefaultColor(int index) const;
    void updateAxisScales();
    virtual void updateTitlePosition();
    // Distance from the canvas top to the title anchor, in pixels.
    virtual int titleTopOffsetPixels() const { return 20; }
    void updateLegendPosition();

    // Curve management internals
    struct CurveData {
        QwtPlotCurve* curve;
        QString name;
        QColor color;
        bool visible;
        int id;

        CurveData() : curve(nullptr), visible(true), id(-1) {}
        ~CurveData() {
            if (curve) {
                curve->detach(); // Detach from plot before deletion
                delete curve;
            }
        }

        // Move constructor and assignment
        CurveData(CurveData&& other) noexcept
            : curve(other.curve),
              name(std::move(other.name)),
              color(other.color),
              visible(other.visible),
              id(other.id) {
            other.curve = nullptr;
        }

        CurveData& operator=(CurveData&& other) noexcept {
            if (this != &other) {
                if (curve) {
                    curve->detach();
                    delete curve;
                }
                curve = other.curve;
                name = std::move(other.name);
                color = other.color;
                visible = other.visible;
                id = other.id;
                other.curve = nullptr;
            }
            return *this;
        }

        // Disable copy constructor and assignment
        CurveData(const CurveData&) = delete;
        CurveData& operator=(const CurveData&) = delete;
    };

    // UI components
    QVBoxLayout* m_mainLayout;
    xQwtPlot* m_plot;
    QwtPlotGrid* m_grid;
    QwtLegend* m_legend;
    QwtPlotPanner* m_panner;
    QwtPlotZoomer* m_zoomer;
    QwtPlotMagnifier* m_magnifier;
    QwtPlotPicker* m_picker;  // Mouse tracking picker

    // Data management
    QMap<int, CurveData*> m_curves;
    QMap<QString, QwtPlotMarker*> m_markers;
    QwtPlotMarker* m_titleMarker;  // Internal title marker
    bool m_updatingTitlePosition = false;
    int m_nextCurveId;

    // Update timer for real-time data
    QTimer* m_updateTimer;
    bool m_autoUpdate;

    // Plot settings
    QString m_title;
    QString m_xAxisTitle;
    QString m_yAxisTitle;
    bool m_gridEnabled;
    bool m_legendEnabled;
    bool m_zoomingEnabled;
    bool m_panningEnabled;
    bool m_magnifierEnabled;
    bool m_autoAxisScaling;  // 控制是否使用自动轴缩放

    // Mouse tracking settings
    bool m_mouseTrackingEnabled;
    QString m_coordinateFormat;
    QwtPlotMarker* m_coordinateMarker;  // Marker to display coordinates

    // Default colors for curves
    static const std::array<QRgb, 12> DEFAULT_COLORS;
};
