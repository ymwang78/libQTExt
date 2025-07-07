// ***************************************************************
//  xTreeView   version:  1.0   -  date:  2025/07/06
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
// 
// ***************************************************************
#include "xTreeView.h"
#include <QHeaderView>
#include <QResizeEvent>

xTreeView::xTreeView(QWidget* parent) : QTreeView(parent) {
    header()->setDefaultAlignment(Qt::AlignCenter);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(QHeaderView::Interactive);
}

void xTreeView::setColumnWidthRatios(const QList<int>& ratios) {
    width_ratios_ = ratios;
    // 设置后立即应用一次
    applyRatios();
}

void xTreeView::resizeEvent(QResizeEvent* event) {
    // 首先，调用基类的 resizeEvent 来处理标准的尺寸调整
    QTreeView::resizeEvent(event);

    // 然后，应用我们的自定义比例逻辑
    applyRatios();
}

void xTreeView::applyRatios() {
    if (width_ratios_.isEmpty() || !model()) {
        return;
    }

    // 计算总的可用宽度（减去滚动条等的宽度）
    int totalWidth = viewport()->width();
    int totalRatio = 0;
    for (int ratio : width_ratios_) {
        totalRatio += ratio;
    }

    if (totalRatio == 0) {
        return;
    }

    // 根据比例计算并设置每一列的宽度
    for (int i = 0; i < width_ratios_.size(); ++i) {
        if (i < model()->columnCount()) {
            int columnWidth = (totalWidth * width_ratios_[i]) / totalRatio;
            setColumnWidth(i, columnWidth);
        }
    }
}
