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

    QList<int> width_ratios_;

  public:
    explicit xTreeView(QWidget* parent = nullptr);

    void setColumnWidthRatios(const QList<int>& ratios);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    void applyRatios();
};
