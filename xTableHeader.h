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
    // store which columns are boolean columns
    QSet<int> bool_columns_;

    // store the check status of every boolean column
    QMap<int, Qt::CheckState> check_states_;

public:
    explicit xCheckableHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

    void setBoolColumn(int column, bool isBool);

    void setCheckState(int column, Qt::CheckState state);

  signals:

    void checkboxToggled(int column, Qt::CheckState newState);

  protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;

    void mousePressEvent(QMouseEvent *event) override;

  private:

    // This function calculates the rectangle area for the checkbox
    QRect checkBoxRect(const QRect &sourceRect) const;

};
