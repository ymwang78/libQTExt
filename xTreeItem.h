#pragma once
// ***************************************************************
//  xTreeItem   version:  1.0   -  date:  2025/07/06
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************
#include <QList>
#include <QVariant>

class xTreeItem {
    QList<QVariant> item_data_;

    xTreeItem *parent_item_;

    QList<xTreeItem *> chile_items_;

  public:
    explicit xTreeItem(const QList<QVariant> &data, xTreeItem *parent = nullptr);

    ~xTreeItem();

    void appendChild(xTreeItem *child);

    xTreeItem *child(int row);

    int childCount() const;

    int columnCount() const;

    QVariant data(int column) const;

    bool setData(int column, const QVariant &value);

    xTreeItem *parentItem();

    int row() const;
};