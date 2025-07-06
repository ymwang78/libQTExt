// ***************************************************************
//  xxTreeItem   version:  1.0   -  date:  2025/07/06
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************
#include "xTreeItem.h"

xTreeItem::xTreeItem(const QList<QVariant> &data, xTreeItem *parent)
    : m_itemData(data), m_parentItem(parent) {}

xTreeItem::~xTreeItem() {
    // 递归删除所有子节点，防止内存泄漏
    qDeleteAll(m_childItems);
}

void xTreeItem::appendChild(xTreeItem *child) { m_childItems.append(child); }

xTreeItem *xTreeItem::child(int row) {
    if (row < 0 || row >= m_childItems.size()) return nullptr;
    return m_childItems.at(row);
}

int xTreeItem::childCount() const { return m_childItems.count(); }

int xTreeItem::columnCount() const { return m_itemData.count(); }

QVariant xTreeItem::data(int column) const {
    if (column < 0 || column >= m_itemData.size()) return QVariant();
    return m_itemData.at(column);
}

xTreeItem *xTreeItem::parentItem() { return m_parentItem; }

int xTreeItem::row() const {
    if (m_parentItem) return m_parentItem->m_childItems.indexOf(const_cast<xTreeItem *>(this));

    // 如果没有父节点，说明是根节点下的顶层节点，行号即为它在父节点子列表中的位置
    return 0;
}
