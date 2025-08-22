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

  protected:
    bool append_mode_ = false;

  public:
    explicit xAbstractTableModel(QObject *parent = nullptr);

    ~xAbstractTableModel() override = default;

    bool appendMode() const { return append_mode_; }

  public slots:

    void setAppendMode(bool enabled);

  public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    Qt::ItemFlags flags(const QModelIndex &index) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

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
  public: // type or const definition
    static constexpr int ConditionRole = Qt::UserRole + 101;
    static constexpr int ComboBoxItemsRole = Qt::UserRole + 102;
    static constexpr int StringListEditRole = Qt::UserRole + 103;
    static constexpr int StringListDialogFactoryRole = Qt::UserRole + 104;
    static constexpr int BoolColumnRole = Qt::UserRole + 105;
    static constexpr int BoolColumnStateRole = Qt::UserRole + 106;
    static constexpr int StringMapRole = Qt::UserRole + 107;
    static constexpr int StringMapDialogFactoryRole = Qt::UserRole + 108;
    enum NUMBER_DISPLAY_MODE { MODE_GENERAL, MODE_FIXFLOAT, MODE_SCIENTIFIC };

  private: // data members

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
    
    // Edit state preservation
    struct EditState {
        QModelIndex index;
        QString currentText;
        int cursorPosition = 0;
        bool hasSelection = false;
        int selectionStart = 0;
        int selectionLength = 0;
        bool isValid() const { return index.isValid(); }
        void clear() { index = QModelIndex(); currentText.clear(); cursorPosition = 0; hasSelection = false; }
    };
    EditState saved_edit_state_;
    bool preserve_edit_state_ = true;

  public:

    explicit xTableView(QWidget *parent = nullptr);

    // Set the table view to stretch to fill the parent widget
    void setStretchToFill(bool enabled);

    void setColumnWidthRatios(const QList<int> &ratios);

    void setSourceModel(QAbstractItemModel *m);

    inline xTableViewSortFilter *proxyModel() const { return proxy_; }

    NUMBER_DISPLAY_MODE getNumberDisplayMode() const;

    int getNumberDisplayPrecision() const;

    void setNumberDisplayMode(NUMBER_DISPLAY_MODE mode, int precision);

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

  protected:
    // keyboard shortcuts ---------------------------------------------------------------

    void keyPressEvent(QKeyEvent *ev);

    void resizeEvent(QResizeEvent *e);

    void scrollContentsBy(int dx, int dy);
    
    // Data model change handlers
    void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>()) override;
    void rowsInserted(const QModelIndex &parent, int first, int last) override;
    void rowsRemoved(const QModelIndex &parent, int first, int last) override;
    void modelReset() override;

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
    
    // Edit state preservation helpers
    void restoreEditorContent(QWidget* editor);
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
