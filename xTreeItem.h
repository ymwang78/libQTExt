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

    QList<QVariant> m_itemData;  // 使用 QList<QVariant> 存储多列数据

    xTreeItem *m_parentItem;

    QList<xTreeItem *> m_childItems;

  public:
    // 构造函数现在接收一个QVariant列表，代表一行的数据
    explicit xTreeItem(const QList<QVariant> &data, xTreeItem *parent = nullptr);

    ~xTreeItem();

    void appendChild(xTreeItem *child);

    xTreeItem *child(int row);

    int childCount() const;

    int columnCount() const;                          // 节点现在知道自己有多少列数据

    QVariant data(int column) const;                  // 获取指定列的数据

    bool setData(int column, const QVariant &value);  // 设置指定列的数据

    xTreeItem *parentItem();

    int row() const;

};