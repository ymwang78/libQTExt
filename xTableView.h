﻿#pragma once
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
#include <QFont>
#include <QSet>
#include <QMap>
#include <QVector>
#include <QPaintEvent>
#include <QMouseEvent>
#include <optional>

class QAbstractItemModel;

class QSortFilterProxyModel;

class xTableViewBoolHeader;

class xCheckableHeaderView;

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
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xTableViewTopRowsFilter;

///////////////////////////////////////////////////////////////////////////////////////////////////

class xAbstractTableModel : public QAbstractTableModel {
    friend class xTableViewSortFilter;  // Allow xTableViewSortFilter to visit private member:
                                        // baseRowCount

    Q_OBJECT
    bool append_mode_ = false;

  public:
    explicit xAbstractTableModel(QObject *parent = nullptr);

    ~xAbstractTableModel() override = default;

    bool appendMode() const { return append_mode_; }

  public slots:

    void setAppendMode(bool enabled);

  public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override final;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override final;

    Qt::ItemFlags flags(const QModelIndex &index) const override final;

    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override final;

  protected:
    // return the real number of rows in the base data source.

    virtual int baseRowCount(const QModelIndex &parent = QModelIndex()) const = 0;

    virtual QVariant baseData(const QModelIndex &index, int role) const = 0;

    virtual Qt::ItemFlags baseFlags(const QModelIndex &index) const = 0;

    virtual bool baseSetData(const QModelIndex &index, const QVariant &value, int role) = 0;

    // return true if new row is inserted successfully.
    // this function is called by the model when a new row is inserted.
    // begin/endInsertRows managed by the model, so you don't need to call them here.
    virtual bool insertNewBaseRow(int row) = 0;

};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xTableView : public QTableView {
    Q_OBJECT
    xTableViewSortFilter *proxy_ = nullptr;
    QTableView *frozen_row_view_ = nullptr;
    xTableViewTopRowsFilter *frozen_row_filter_ = nullptr;
    QTableView *frozen_col_view_ = nullptr;
    xTableViewTopRowsFilter *frozen_col_filter_ = nullptr;
    int freeze_cols_ = 0;
    int freeze_rows_ = 0;
    int current_sort_col_ = -1;  //  -1 if no column is sorted
    Qt::SortOrder current_sort_order_ = Qt::AscendingOrder;
    bool is_stretch_to_fill_ = false;
    QList<int> column_width_ratios_;
    QSet<int> bool_columns_;
    QMap<int, QVector<bool>> bool_column_memory_states_;
    xCheckableHeaderView *checkable_header_;

  public:
    static constexpr int ConditionRole = Qt::UserRole + 101;
    static constexpr int ComboBoxItemsRole = Qt::UserRole + 102;
    static constexpr int StringListEditRole = Qt::UserRole + 103;
    static constexpr int StringListDialogFactoryRole = Qt::UserRole + 104;
    static constexpr int BoolColumnRole = Qt::UserRole + 105;
    static constexpr int BoolColumnStateRole = Qt::UserRole + 106;

    explicit xTableView(QWidget *parent = nullptr);

    // Set the table view to stretch to fill the parent widget
    void setStretchToFill(bool enabled);

    void setColumnWidthRatios(const QList<int> &ratios);

    void setSourceModel(QAbstractItemModel *m);

    inline xTableViewSortFilter *proxyModel() const { return proxy_; }

    // Public filtering / sorting helpers --------------------------------------------------

    void setColumnFilter(int col, const QVariantMap &cond);

    void clearFilters();

    void sortBy(int col, Qt::SortOrder ord = Qt::AscendingOrder);

    // Freeze API -------------------------------------------------------------------------

    void freezeLeftColumns(int n);

    void freezeTopRows(int n);

    // Special Item Editors ---------------------------------------------------------------

    // coworking with xTableStringListEditor
    // function signature: (parent, current selection) -> std::optional<selection result>
    // using std::optional to handle user clicking "Cancel" gracefully
    using StringListDialogFactory =
        std::function<std::optional<QStringList>(QWidget *, const QStringList &)>;

    // Bool column management
    void setBoolColumn(int column, bool enabled);
    bool isBoolColumn(int column) const;
    QSet<int> getBoolColumns() const;

  signals:

    void findRequested();

  protected:
    // keyboard shortcuts ---------------------------------------------------------------

    void keyPressEvent(QKeyEvent *ev);

    void resizeEvent(QResizeEvent *e);

    void scrollContentsBy(int dx, int dy);

  private slots:

    void showHeaderMenu(const QPoint &pos);

    void toggleSortColumn(int logicalCol);

    void onHeaderCheckboxToggled(int column, Qt::CheckState state);

  private:
    // Copy / Paste / Delete --------------------------------------------------------------

    void copySelection();

    void paste();

    void removeSelectedCells();

    void removeSelectedRows();

    // Freeze implementation -------------------------------------------------------------

    void syncFrozen();

    void createFrozenColView();

    void createFrozenRowView();

    void updateFrozenGeometry();

    void updateBoolColumnHeaderState(int column);

    Qt::CheckState calculateBoolColumnState(int column) const;

    void saveBoolColumnMemoryState(int column);

    void restoreBoolColumnMemoryState(int column);
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xTableViewBoolHeader : public QWidget {
    Q_OBJECT

    QString title_;
    Qt::CheckState check_state_;
    QRect checkbox_rect_;
    QRect text_rect_;
    bool pressed_;

  public:
    explicit xTableViewBoolHeader(const QString &title, QWidget *parent = nullptr);

    void setCheckState(Qt::CheckState state);

    Qt::CheckState checkState() const;

  signals:
    void checkStateChanged(Qt::CheckState state);

  protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    QSize sizeHint() const override;

  private:
    void updateCheckState();

    QRect calculateCheckBoxRect() const;

    QRect calculateTextRect() const;
};
