#pragma once
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
#include <QTreeView>
#include <QList>

class xTreeView : public QTreeView {
    Q_OBJECT

  public:
    explicit xTreeView(QWidget* parent = nullptr);

    // 设置列宽的比例，例如 {3, 1, 1} 代表第一列宽度是二三列的3倍
    void setColumnWidthRatios(const QList<int>& ratios);

  protected:
    // 重写 resizeEvent 来在每次视图大小改变时重新计算列宽
    void resizeEvent(QResizeEvent* event) override;

  private:
    void applyRatios();  // 应用比例的辅助函数

    QList<int> m_widthRatios;
};
