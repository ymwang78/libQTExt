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
    : item_data_(data), parent_item_(parent) {}

xTreeItem::~xTreeItem() {
    // 递归删除所有子节点，防止内存泄漏
    qDeleteAll(chile_items_);
}

void xTreeItem::appendChild(xTreeItem *child) { chile_items_.append(child); }

xTreeItem *xTreeItem::child(int row) {
    if (row < 0 || row >= chile_items_.size()) return nullptr;
    return chile_items_.at(row);
}

int xTreeItem::childCount() const { return chile_items_.count(); }

int xTreeItem::columnCount() const { return item_data_.count(); }

QVariant xTreeItem::data(int column) const {
    if (column < 0 || column >= item_data_.size()) return QVariant();
    return item_data_.at(column);
}

xTreeItem *xTreeItem::parentItem() { return parent_item_; }

int xTreeItem::row() const {
    if (parent_item_) return parent_item_->chile_items_.indexOf(const_cast<xTreeItem *>(this));

    // 如果没有父节点，说明是根节点下的顶层节点，行号即为它在父节点子列表中的位置
    return 0;
}
