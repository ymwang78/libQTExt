#pragma once
// ***************************************************************
//  xTheme  version:  1.0   -  date:  2025/08/13
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************
#include <QIcon>
#include <QPixmap>
#include <QPainter>

class xTheme {
    public:
    static QIcon createColorizedIcon(const QString& path, const QColor& color,
                                     const QSize& size = QSize(24, 24));
};