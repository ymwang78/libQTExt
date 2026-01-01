
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
#include "xTheme.h"

/**
 * @brief 生成指定颜色的图标
 * @param path 图标资源路径 (e.g. ":/icons/close.svg")
 * @param color 想要显示的颜色 (e.g. Qt::white 或 QColor("#E0E0E0"))
 * @param size 图标大小 (默认 24x24)
 * @return 已经染好色的 QIcon
 */
QIcon xTheme::createColorizedIcon(const QString& path, const QColor& color, const QSize& size) {
    // 1. 加载 SVG 为 Pixmap
    QIcon sourceIcon(path);
    QPixmap pixmap = sourceIcon.pixmap(size);

    // 2. 如果图标加载失败，直接返回空
    if (pixmap.isNull()) return QIcon();

    // 3. 创建绘图工具
    QPainter painter(&pixmap);

    // 4. 【关键步骤】设置混合模式
    // CompositionMode_SourceIn 的意思是：
    // "只保留原图的不透明区域（形状），但把像素颜色替换成我笔刷的颜色"
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);

    // 5. 用目标颜色填充整个矩形（因为有了混合模式，只有线条会被染色）
    painter.fillRect(pixmap.rect(), color);

    painter.end();

    // 6. 返回新的图标
    return QIcon(pixmap);
}