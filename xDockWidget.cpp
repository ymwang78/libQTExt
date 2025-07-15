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
#include "xDockWidget.h"


#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QStyle>  // 用于获取标准图标

xDockWidgetTitleBar::xDockWidgetTitleBar(const QString &title, QWidget *parent) : QWidget(parent) {
    // 1. 创建标题标签和关闭按钮
    m_titleLabel = new QLabel(title, this);
    m_closeButton = new QToolButton(this);
    m_closeButton->setObjectName("DockTitleCloseButton");  // 设置对象名，方便QSS定位

    // 2. 将关闭按钮做得非常小，并设置图标
    // 使用Qt内置的标准图标
    QIcon closeIcon = style()->standardIcon(QStyle::SP_TitleBarCloseButton);
    m_closeButton->setIcon(closeIcon);
    m_closeButton->setFixedSize(16, 16);        // 设置一个很小的固定尺寸
    m_closeButton->setIconSize(QSize(10, 10));  // 图标尺寸比按钮本身更小
    m_closeButton->setAutoRaise(true);          // 设置为自动浮起，类似ToolButton的效果

    // 3. 设置布局
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 4, 0);  // 左边留空多一点，右边少一点
    layout->setSpacing(0);

    layout->addWidget(m_titleLabel);
    layout->addStretch();  // 添加弹簧，将关闭按钮推到最右边
    layout->addWidget(m_closeButton);

    this->setLayout(layout);

    // 4. 连接信号
    connect(m_closeButton, &QToolButton::clicked, this, &xDockWidgetTitleBar::closeRequested);
}

xDockWidgetTitleBar::~xDockWidgetTitleBar() {

}