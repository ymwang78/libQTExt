#pragma once
// ***************************************************************
//  xTableEditor   version:  1.0   -  date:  2025/07/05
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
// 
// ***************************************************************

#include <QWidget>
#include <QStringList>
#include <QDialog>
#include "xTableView.h"

class QLineEdit;
class QPushButton;

class xTableStringListEditor : public QWidget {
    Q_OBJECT
  public:

    explicit xTableStringListEditor(xTableView::StringListDialogFactory factory,
                                    QWidget* parent = nullptr);

    // 设置和获取当前值 (QStringList)
    void setStringList(const QStringList& list);

    QStringList getStringList() const;

  signals:
    // 当编辑完成时（即对话框点击OK后），发射此信号
    void editingFinished();

  private slots:
    void onButtonClicked();

  private:
    QLineEdit* lineEdit_;
    QPushButton* button_;
    QStringList currentList_;
    xTableView::StringListDialogFactory dialogFactory_;
};