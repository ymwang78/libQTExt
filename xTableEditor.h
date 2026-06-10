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
#include <functional>
#include "xTableView.h"

class QLineEdit;
class QPushButton;

class xTableStringListEditor : public QWidget {
    Q_OBJECT
    QLineEdit* line_edit_;
    QPushButton* button_;
    QStringList current_list_;
    xTableView::StringListDialogFactory dialog_factory_;
    std::function<void(const QStringList&)> commit_callback_;
    bool dialog_open_ = false;

  public:
    explicit xTableStringListEditor(xTableView::StringListDialogFactory factory,
                                    std::function<void(const QStringList&)> commit_callback = {},
                                    QWidget* parent = nullptr);

    void setStringList(const QStringList& list);

    void setText(const QString& text);

    QStringList getStringList() const;

  signals:
    void editingFinished();

  private slots:
    void onButtonClicked();

    void onTextEdited(const QString& text);
};
