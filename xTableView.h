#pragma once
// ***************************************************************
//  xTableView   version:  1.0   -  date:  2025/07/03
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************
#include "libqtext_global.h"

#include <QTableView>
#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QTextStream>
#include <QRegularExpression>
#include <QAbstractTableModel>
#include <QtWidgets>

class QAbstractItemModel;

class QSortFilterProxyModel;

struct xTableViewFilterRule {
    int column = -1;           // which column; -1 == global (not used yet)
    QRegularExpression regex;  // regex match (string values)
    QVariant equals;           // exact equality test
    double min = std::numeric_limits<double>::lowest();
    double max = std::numeric_limits<double>::max();
    bool hasBounds() const { return !(std::isinf(min) && std::isinf(max)); }
    bool active() const {
        return regex.isValid() && !regex.pattern().isEmpty() || equals.isValid() || hasBounds();
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xTableViewSortFilter : public QSortFilterProxyModel {
    Q_OBJECT
    QVector<xTableViewFilterRule> filters_;

  public:
    explicit xTableViewSortFilter(QObject *parent = nullptr);

    // add / replace filter for column
    void setColumnFilter(int column, const QVariantMap &conditions);

    void clearFilters();

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xTableViewTopRowsFilter : public QSortFilterProxyModel {
    Q_OBJECT

  public:

    explicit xTableViewTopRowsFilter(QObject *p = nullptr) : QSortFilterProxyModel(p) {}

    void setLimit(int n) {
        limit_ = n;
        invalidateFilter();
    }

  protected:
    bool filterAcceptsRow(int r, const QModelIndex &) const override { return r < limit_; }

  private:
    int limit_{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xTableViewItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
  public:
    explicit xTableViewItemDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override;

    void setEditorData(QWidget *editor, const QModelIndex &idx) const override;

    void setModelData(QWidget *editor, QAbstractItemModel *mdl,
                      const QModelIndex &idx) const override;

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xTableView : public QTableView {
    Q_OBJECT
    xTableViewSortFilter *proxy_ = nullptr;
    QTableView *frozenRowView_ = nullptr;
    xTableViewTopRowsFilter *frozenRowProxy_ = nullptr;
    QTableView *frozenColView_ = nullptr;
    int freezeCols_ = 0;
    int freezeRows_ = 0;
    int currentSortCol_ = -1;  //  -1 表示当前无排序
    Qt::SortOrder currentSortOrd_ = Qt::AscendingOrder;

  public:
    explicit xTableView(QWidget *parent = nullptr);

    // Provide source model externally so users can hold pointer

    void setSourceModel(QAbstractItemModel *m);

    inline xTableViewSortFilter *proxyModel() const { return proxy_; }

    // Public filtering / sorting helpers --------------------------------------------------

    void setColumnFilter(int col, const QVariantMap &cond);

    void clearFilters();

    void sortBy(int col, Qt::SortOrder ord = Qt::AscendingOrder);

    // Freeze API -------------------------------------------------------------------------

    void freezeLeftColumns(int n);

    void freezeTopRows(int n);

    // Theme -----------------------------------------------------------------------------

    void applyTheme(const QString &name);

signals:

    void findRequested();

  protected:

    // keyboard shortcuts ---------------------------------------------------------------

    void keyPressEvent(QKeyEvent *ev);

    void resizeEvent(QResizeEvent *e);

  private slots:

    void showHeaderMenu(const QPoint &pos);

    void toggleSortColumn(int logicalCol);

  private:

    // Copy / Paste / Delete --------------------------------------------------------------

    void copySelection();

    void paste();

    void removeSelectedCells();

    // Freeze implementation -------------------------------------------------------------

    void syncFrozen();

    void createFrozenColView();

    void createFrozenRowView();

    void removeFrozen(QTableView **view);

    void updateFrozenGeometry();

    // Themes ---------------------------------------------------------------------------
    static QString darkQss();

    static QString lightQss();
};
