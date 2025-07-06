#pragma once
// ***************************************************************
//  xTableHeader   version:  1.0   -  date:  2025/07/06
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
// 
// ***************************************************************
#include <QHeaderView>
#include <QPainter>
#include <QMouseEvent>
#include <QSet>
#include <QMap>

class xCheckableHeaderView : public QHeaderView {
    Q_OBJECT

  public:
    explicit xCheckableHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

    // 设置某列是否为“布尔列”
    void setBoolColumn(int column, bool isBool);

    // 更新指定列的复选框状态
    void setCheckState(int column, Qt::CheckState state);

  signals:
    // 当复选框被点击时发出信号，通知TableView
    void checkboxToggled(int column, Qt::CheckState newState);

  protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void mousePressEvent(QMouseEvent *event) override;

  private:
    // 获取指定section内复选框的绘制区域
    QRect checkBoxRect(const QRect &sourceRect) const;

    // 存储哪些列是布尔列
    QSet<int> bool_columns_;
    // 存储每个布尔列的复选框状态
    QMap<int, Qt::CheckState> check_states_;
};
