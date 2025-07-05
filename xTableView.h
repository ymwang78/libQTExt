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
#include <QFont>
#include <QAbstractTableModel>
#include <optional>

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
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xTableViewTopRowsFilter;

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

  private slots:
    void commitAndCloseEditor();
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xAbstractTableModel : public QAbstractTableModel {
    friend class xTableViewSortFilter;  // 允许 xTableViewSortFilter 访问私有成员baseRowCount

    Q_OBJECT
    bool append_mode_ = false;  // 控制是否开启追加模式的标志

  public:
    explicit xAbstractTableModel(QObject* parent = nullptr);

    ~xAbstractTableModel() override = default;

    bool appendMode() const { return append_mode_; }

    // --- 公共接口 ---
public slots:
    // 设置是否开启追加模式
    void setAppendMode(bool enabled);

public:
    
    // --- QAbstractTableModel 的重写函数 ---
    // 这些函数封装了通用逻辑，并调用子类必须实现的 base/insert 函数
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override final;
    
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override final;
    
    Qt::ItemFlags flags(const QModelIndex& index) const override final;
    
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override final;

protected:
    // --- 子类必须实现的纯虚函数 ---

    // 返回不包含占位符的“真实”行数
    virtual int baseRowCount(const QModelIndex &parent = QModelIndex()) const = 0;

    // 提供“真实”行的数据
    virtual QVariant baseData(const QModelIndex& index, int role) const = 0;

    // 提供“真实”行的标志
    virtual Qt::ItemFlags baseFlags(const QModelIndex& index) const = 0;

    // 设置“真实”行的数据
    virtual bool baseSetData(const QModelIndex& index, const QVariant& value, int role) = 0;

    // 在指定行位置，向底层数据源中插入一条新的空记录。成功返回 true。
    // 这个函数只负责在数据结构中增加记录，begin/endInsertRows由基类管理。
    virtual bool insertNewBaseRow(int row) = 0;

private:
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
    int current_sort_col_ = -1;  //  -1 表示当前无排序
    Qt::SortOrder current_sort_order_ = Qt::AscendingOrder;
    bool is_stretch_to_fill_ = false;
    QList<int> column_width_ratios_;

  public:

    static constexpr int ConditionRole = Qt::UserRole + 101;
    static constexpr int ComboBoxItemsRole = Qt::UserRole + 102;
    static constexpr int StringListEditRole = Qt::UserRole + 103; 
    static constexpr int StringListDialogFactoryRole = Qt::UserRole + 104;

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
   
    // 配合 xTableStringListEditor 
    // 函数签名：(父窗口, 当前已选项) -> std::optional<选择结果>
    // 使用 std::optional 来优雅地处理用户点击“取消”的情况
    using StringListDialogFactory = std::function<std::optional<QStringList>(
        QWidget *, const QStringList &)>;

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

  private:
    // Copy / Paste / Delete --------------------------------------------------------------

    void copySelection();

    void paste();

    void removeSelectedCells();

    // Freeze implementation -------------------------------------------------------------

    void syncFrozen();

    void createFrozenColView();

    void createFrozenRowView();

    void updateFrozenGeometry();
};
