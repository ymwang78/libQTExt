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

    void setStringList(const QStringList& list);

    QStringList getStringList() const;

  signals:
    void editingFinished();

  private slots:
    void onButtonClicked();

  private:
    QLineEdit* line_edit_;
    QPushButton* button_;
    QStringList current_list_;
    xTableView::StringListDialogFactory dialog_factory_;
};