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
#include <QSet>
#include <QMap>
#include <QVector>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QJsonObject>
#include <optional>
#include <zce/zce_any.h>

class QAbstractItemModel;

class QSortFilterProxyModel;

class xTableViewBoolHeader;

class xCheckableHeaderView;

class xTableView;

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

class xPaletteChangeWatcher : public QObject {
  public:
    explicit xPaletteChangeWatcher(xTableView *target);

    ~xPaletteChangeWatcher() override;
    
  protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

  private:
    xTableView *m_target = nullptr;
    bool m_lastDarkMode = false;
};
///////////////////////////////////////////////////////////////////////////////////////////////////

class xAbstractTableModel : public QAbstractTableModel {
    friend class xTableViewSortFilter;  // Allow xTableViewSortFilter to visit private member:
                                        // baseRowCount

    Q_OBJECT

  protected:
    bool append_mode_ = false;

    int hint_column_ = 0;

  public:
    explicit xAbstractTableModel(QObject *parent = nullptr);

    ~xAbstractTableModel() override;

    void resetModel();

    bool appendMode() const { return append_mode_; }

    void setAppendMode(bool enabled, int hint_col = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    Qt::ItemFlags flags(const QModelIndex &index) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

    // return the real number of rows in the base data source.

    virtual int baseRowCount(const QModelIndex &parent = QModelIndex()) const = 0;

    virtual QVariant baseData(const QModelIndex &index, int role) const = 0;

    virtual Qt::ItemFlags baseFlags(const QModelIndex &index) const = 0;

    virtual bool baseSetData(const QModelIndex &index, const QVariant &value, int role) = 0;

    // return true if new row is inserted successfully.
    // this function is called by the model when a new row is inserted.
    // begin/endInsertRows managed by the model, so you don't need to call them here.
    virtual bool insertNewBaseRow(int row, const QVariant &value) = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class xTableView : public QTableView {
  public:  // type or const definition
    static constexpr int ConditionRole = Qt::UserRole + 101;
    static constexpr int ComboBoxItemsRole = Qt::UserRole + 102;
    static constexpr int StringListEditRole = Qt::UserRole + 103;
    static constexpr int StringListDialogFactoryRole = Qt::UserRole + 104;
    static constexpr int BoolColumnRole = Qt::UserRole + 105;
    static constexpr int BoolColumnStateRole = Qt::UserRole + 106;
    static constexpr int StringMapRole = Qt::UserRole + 107;
    static constexpr int StringMapDialogFactoryRole = Qt::UserRole + 108;
    enum NUMBER_DISPLAY_MODE { MODE_GENERAL, MODE_FIXFLOAT, MODE_SCIENTIFIC };

  private:  // data members
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
    // 比例模式期间 stretchLastSection 被强制关掉，这里记住调用方原本的设置，退出时还原。
    bool stretch_last_section_saved_ = false;
    bool stretch_last_section_overridden_ = false;
    // 等宽可调模式（setProportionalColumns）：表头保持 Interactive，列宽按权重铺满视口。
    // 权重必须是 double —— 每次 resize 都从权重算宽度，而不是从取整后的宽度反推权重，
    // 否则整除误差一次次累积，反复缩放窗口后比例就漂了。
    bool is_proportional_columns_ = false;
    QVector<double> proportional_weights_;
    // 用户亲手拖过的列：自动逻辑（吸收别人拖出来的差额）不再碰它们。
    QSet<int> proportional_manual_columns_;
    // 自己 setColumnWidth 期间置位，用来在 sectionResized 里区分程序设置与用户拖拽。
    bool applying_proportional_columns_ = false;
    // 已排了一次延后重排，用来把连续的隐藏 / 显示（"显示所有列"）合并成一次。
    bool proportional_reflow_pending_ = false;
    QSet<int> bool_columns_;
    QMap<int, QVector<bool>> bool_column_memory_states_;
    xCheckableHeaderView *checkable_header_;

  public:
    static QString anyToString(const zce::Any &a);

    explicit xTableView(QWidget *parent = nullptr, bool is_column_sortable = true);

    bool isEditing() const;

    void setItemAny(int row, int col, const zce::Any &any);
    
    zce::Any getItemAny(int row, int col) const;

    // Set the table view to stretch to fill the parent widget
    void setStretchToFill(bool enabled);

    void setColumnWidthRatios(const QList<int> &ratios);

    // 等宽可调：表头保持 QHeaderView::Interactive（用户能拖），列宽初值为视口等分，
    // 之后窗口变化时按各列当前宽度的比例缩放 —— 既始终铺满视口，又不丢用户拖出来的比例。
    // 用户拖过的列会被记成"手动列"，别的列再被拖时不去动它们的宽度。
    // 与 setStretchToFill() / setColumnWidthRatios() 三者互斥，打开这个会关掉那两个。
    // 重复 setProportionalColumns(true) 不动已有布局；想回到等宽初值就先关再开。
    void setProportionalColumns(bool enabled);

    inline bool isProportionalColumns() const { return is_proportional_columns_; }

    // 按内容量一次列宽，之后用户仍可拖拽。
    // 必须在模型填完数据后调用：构造时模型还是空的，量出来只有表头文字的宽度。
    // setStretchToFill(true) / setProportionalColumns(true) / 设过列宽比例时
    // 列宽由本类接管，此函数不做任何事。
    // sample_rows 限制参与计算的行数（<= 0 表示用表头当前的 resizeContentsPrecision）。
    void autoFitColumns(int sample_rows = 64);

    void setSourceModel(QAbstractItemModel *m);

    inline xTableViewSortFilter *proxyModel() const { return proxy_; }

    NUMBER_DISPLAY_MODE getNumberDisplayMode() const;

    int getNumberDisplayPrecision() const;

    void setNumberDisplayMode(NUMBER_DISPLAY_MODE mode, int precision);

    QJsonObject saveUiState() const;

    // Call after the model has been populated and the view has completed layout.
    void restoreUiState(const QJsonObject &state);

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

    // Edit state preservation
    void setPreserveEditState(bool enabled);
    bool preserveEditState() const;
    void saveCurrentEditState();
    void restoreEditState();
    void clearSavedEditState();

  signals:

    void findRequested();

    void numberDisplayChanged(NUMBER_DISPLAY_MODE mode, int precision);

  protected:
    // keyboard shortcuts ---------------------------------------------------------------

    void keyPressEvent(QKeyEvent *ev);

    void resizeEvent(QResizeEvent *e);

    void scrollContentsBy(int dx, int dy);

    void mousePressEvent(QMouseEvent *event) override;

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

    void applyColumnWidthRatios();

    // 按当前是否处于自己铺满视口的模式（固定比例 / 等宽可调），
    // 关掉 / 还原 stretchLastSection。
    void updateStretchLastSectionForRatios();

    // 把权重铺到视口上（带重入守卫），顺带在列数变化后补齐权重。
    void applyProportionalColumns();

    // 排一次延后到下一轮事件循环的重排，多次调用合并成一次。
    void scheduleProportionalReflow();

    // 列数变了之后补齐 / 截断权重，并重新归一化。
    void syncProportionalWeights();

    // 让权重之和回到 1，这样"权重 == 占视口的比例"一直成立。
    void normalizeProportionalWeights();

    // horizontalHeader() 的 sectionResized 槽：只有用户拖拽才更新权重并打手动标记。
    void onProportionalSectionResized(int logical_index, int old_size, int new_size);

    // 模型列数变化的槽：按插入 / 删除的位置搬动权重与手动标记，再重排。
    void onProportionalColumnsInserted(const QModelIndex &parent, int first, int last);
    void onProportionalColumnsRemoved(const QModelIndex &parent, int first, int last);

    // 把上面两个槽接到视图当前的模型上（模型换了要重接）。
    void connectProportionalModelSignals(QAbstractItemModel *old_view_model);

    void updateBoolColumnHeaderState(int column);

    Qt::CheckState calculateBoolColumnState(int column) const;

    void saveBoolColumnMemoryState(int column);

    void restoreBoolColumnMemoryState(int column);

    // Edit state preservation helpers
    void restoreEditorContent(QWidget *editor);
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
