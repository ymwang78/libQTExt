#pragma once
// ***************************************************************
//  xDockWidget   version:  1.0   -  date:  2025/07/13
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
// 
// ***************************************************************
#include <QWidget>

class QLabel;
class QToolButton;

class xDockWidgetTitleBar : public QWidget {
    Q_OBJECT

  public:
    explicit xDockWidgetTitleBar(const QString &title, QWidget *parent = nullptr);

    ~xDockWidgetTitleBar();

  signals:

    void closeRequested();

  private:
    QLabel *m_titleLabel;
    QToolButton *m_closeButton;
};