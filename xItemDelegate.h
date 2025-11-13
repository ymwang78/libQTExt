#pragma once
// ***************************************************************
//  xItemDelegate   version:  1.0   -  date:  2025/07/06
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************
#include <QStyledItemDelegate>

class QEvent;

class xItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

    int realnum_showmode_; // xTableView::REALNUMBER_SHOWMODE

    int realnum_precision_;

  public:

    explicit xItemDelegate(QObject *parent = nullptr);

    int getRealNumberMode() const { return realnum_showmode_; }

    int getRealNumberPrecision() const { return realnum_precision_; }

    void setRealNumberShowMode(int mode, int precision) {
        realnum_showmode_ = mode;
        realnum_precision_ = precision;
    };

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override;

    void setEditorData(QWidget *editor, const QModelIndex &idx) const override;

    void setModelData(QWidget *editor, QAbstractItemModel *mdl,
                      const QModelIndex &idx) const override;

    QString displayText(const QVariant &value, const QLocale &locale) const override;

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;

    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;
  private slots:

    void commitAndCloseEditor();
};
