// ***************************************************************
//  xTableView   version:  1.4   -   date:  2025/07/04
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  (Revised by Gemini)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
#include "xTableView.h"
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QKeyEvent>
#include <QHeaderView>
#include <QPainter>

// custom role for conditional formatting

///////////////////////////////////////////////////////////////////////////////////////////////////

xTableViewSortFilter::xTableViewSortFilter(QObject *parent) : QSortFilterProxyModel(parent) {}

void xTableViewSortFilter::setColumnFilter(int column, const QVariantMap &conditions) {
    auto rule = std::find_if(filters_.begin(), filters_.end(),
                             [&](const xTableViewFilterRule &r) { return r.column == column; });
    xTableViewFilterRule fr;
    fr.column = column;
    if (conditions.contains("regex"))
        fr.regex = QRegularExpression(conditions.value("regex").toString(),
                                      QRegularExpression::CaseInsensitiveOption);
    if (conditions.contains("equals")) fr.equals = conditions.value("equals");
    if (conditions.contains("min")) fr.min = conditions.value("min").toDouble();
    if (conditions.contains("max")) fr.max = conditions.value("max").toDouble();
    if (rule != filters_.end())
        *rule = fr;
    else
        filters_.append(fr);
    invalidateFilter();
}

void xTableViewSortFilter::clearFilters() {
    filters_.clear();
    invalidateFilter();
}

bool xTableViewSortFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (filters_.isEmpty()) return true;
    for (const xTableViewFilterRule &fr : filters_) {
        QModelIndex idx = sourceModel()->index(sourceRow, fr.column, sourceParent);
        if (!idx.isValid()) continue;
        QVariant data = sourceModel()->data(idx, Qt::DisplayRole);
        if (fr.regex.isValid() && !fr.regex.pattern().isEmpty()) {
            if (!fr.regex.match(data.toString()).hasMatch()) return false;
        }
        if (fr.equals.isValid()) {
            if (data != fr.equals) return false;
        }
        if (fr.hasBounds()) {
            if (data.typeId() == QMetaType::Double || data.typeId() == QMetaType::Int) {
                double d = data.toDouble();
                if (d < fr.min || d > fr.max) return false;
            }
        }
    }
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
class xTableViewTopRowsFilter : public QSortFilterProxyModel {
  public:
    xTableViewTopRowsFilter(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}
    void setLimit(int limit) {
        limit_ = limit;
        invalidateFilter();
    }

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &) const override {
        return sourceRow < limit_;
    }

  private:
    int limit_ = 0;
};
///////////////////////////////////////////////////////////////////////////////////////////////////
xTableViewItemDelegate::xTableViewItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QWidget *xTableViewItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                                              const QModelIndex &idx) const {
    Q_UNUSED(opt);
    QVariant comboData = idx.data(xTableView::ComboBoxItemsRole);
    if (comboData.isValid() && comboData.canConvert<QStringList>()) {
        QComboBox *e = new QComboBox(parent);
        e->addItems(comboData.toStringList());
        e->setFrame(false);
        return e;
    }
    QVariant v = idx.data(Qt::EditRole);
    switch (v.typeId()) {
        case QMetaType::Int: {
            QSpinBox *e = new QSpinBox(parent);
            e->setFrame(false);
            e->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            return e;
        }
        case QMetaType::Double: {
            QDoubleSpinBox *e = new QDoubleSpinBox(parent);
            e->setDecimals(6);
            e->setRange(-1.0e100, 1.0e100);
            e->setFrame(false);
            return e;
        }
        case QMetaType::QDateTime: {
            QDateTimeEdit *e = new QDateTimeEdit(parent);
            e->setCalendarPopup(true);
            return e;
        }
        default: {
            QLineEdit *e = new QLineEdit(parent);
            return e;
        }
    }
}

void xTableViewItemDelegate::setEditorData(QWidget *editor, const QModelIndex &idx) const {
    if (auto *cb = qobject_cast<QComboBox *>(editor)) {
        cb->setCurrentText(idx.data(Qt::DisplayRole).toString());
        return;
    }
    QVariant v = idx.data(Qt::EditRole);
    if (v.typeId() == QMetaType::QDateTime) {
        qobject_cast<QDateTimeEdit *>(editor)->setDateTime(v.toDateTime());
    } else if (v.typeId() == QMetaType::Double) {
        qobject_cast<QDoubleSpinBox *>(editor)->setValue(v.toDouble());
    } else if (v.typeId() == QMetaType::Int) {
        qobject_cast<QSpinBox *>(editor)->setValue(v.toInt());
    } else {
        qobject_cast<QLineEdit *>(editor)->setText(v.toString());
    }
}

void xTableViewItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *mdl,
                                          const QModelIndex &idx) const {
    QVariant out;
    if (auto *cb = qobject_cast<QComboBox *>(editor)) {
        out = cb->currentText();
    } else if (auto *sb = qobject_cast<QSpinBox *>(editor))
        out = sb->value();
    else if (auto *ds = qobject_cast<QDoubleSpinBox *>(editor))
        out = ds->value();
    else if (auto *dt = qobject_cast<QDateTimeEdit *>(editor))
        out = dt->dateTime();
    else if (auto *le = qobject_cast<QLineEdit *>(editor))
        out = le->text();
    mdl->setData(idx, out, Qt::EditRole);
}

void xTableViewItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                                   const QModelIndex &idx) const {
    QStyleOptionViewItem o(opt);
    initStyleOption(&o, idx);

    QVariant data = idx.data(Qt::DisplayRole);
    switch (static_cast<QMetaType::Type>(data.typeId())) {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Double:
        case QMetaType::Float:
            o.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
            break;
        default:
            o.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
            break;
    }

    QVariant cond = idx.data(xTableView::ConditionRole);
    if (cond.isValid()) {
        if (cond.toString() == "error") o.palette.setColor(QPalette::Text, Qt::red);
    }

    QStyledItemDelegate::paint(p, o, idx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
xAbstractTableModel::xAbstractTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void xAbstractTableModel::setAppendMode(bool enabled) {
    if (appendMode_ == enabled) return;

    appendMode_ = enabled;

    int realRowCount = baseRowCount(QModelIndex());
    if (appendMode_) {
        // 开启模式：在末尾插入占位符行
        beginInsertRows(QModelIndex(), realRowCount, realRowCount);
        endInsertRows();
    } else {
        // 关闭模式：移除末尾的占位符行
        beginRemoveRows(QModelIndex(), realRowCount, realRowCount);
        endRemoveRows();
    }
}

int xAbstractTableModel::rowCount(const QModelIndex& parent) const {
    int realRowCount = baseRowCount(parent);
    // 真实行数 + 1个占位符行（如果开启）
    return realRowCount + (appendMode_ ? 1 : 0);
}

QVariant xAbstractTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};

    int realRowCount = baseRowCount(index.parent());
    // 检查是否是占位符行
    if (appendMode_ && index.row() == realRowCount) {
        if (role == Qt::DisplayRole && index.column() == 0) {
            return "* Click to add a new item...";
        }
        if (role == Qt::FontRole) {
            QFont font;
            font.setItalic(true);
            return font;
        }
        return {};
    }
    // 对于真实数据行，调用子类的实现
    return baseData(index, role);
}

Qt::ItemFlags xAbstractTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;

    int realRowCount = baseRowCount(index.parent());
    // 检查是否是占位符行
    if (appendMode_ && index.row() == realRowCount) {
        // 占位符行默认所有列都可编辑，子类可以在 baseFlags 中覆盖此行为
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    }
    // 对于真实数据行，调用子类的实现
    return baseFlags(index);
}

bool xAbstractTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid()) {
        return false;
    }

    int realRowCount = baseRowCount(index.parent());
    // 检查是否是占位符行
    if (appendMode_ && index.row() == realRowCount) {
        // 是占位符行，触发新增逻辑
        
        // 1. 通知视图，即将在占位符的位置（即末尾）插入一个新行
        beginInsertRows(QModelIndex(), realRowCount, realRowCount);

        // 2. 调用子类实现在底层数据源中创建一条空记录
        bool success = insertNewBaseRow(realRowCount);

        // 3. 通知视图插入完成
        endInsertRows();

        if (!success) {
            return false; // 如果子类插入失败，则终止
        }

        // 4. 现在 index 指向的已经是新创建的真实行了，
        //    我们调用子类的 baseSetData 来设置用户刚刚输入的值。
        return baseSetData(index, value, role);

    } else {
        // 对于普通数据行，直接调用子类的实现
        return baseSetData(index, value, role);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

xTableView::xTableView(QWidget *parent)
    : QTableView(parent),
      proxy_(new xTableViewSortFilter(this)),
      frozenRowProxy_(nullptr),
      frozenRowView_(nullptr),
      frozenColView_(nullptr),
      freezeCols_(0),
      freezeRows_(0),
      currentSortCol_(-1),
      currentSortOrd_(Qt::AscendingOrder) {
    setSortingEnabled(true);
    setAlternatingRowColors(true);
    verticalHeader()->setDefaultSectionSize(22);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    setSelectionBehavior(SelectItems);
    setSelectionMode(ContiguousSelection);
    setEditTriggers(EditKeyPressed | DoubleClicked | SelectedClicked);
    setHorizontalScrollMode(ScrollPerPixel);
    setVerticalScrollMode(ScrollPerPixel);
    setItemDelegate(new xTableViewItemDelegate(this));

    horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    horizontalHeader()->setSectionsClickable(true);
    connect(horizontalHeader(), &QHeaderView::sectionClicked, this, &xTableView::toggleSortColumn);
    horizontalHeader()->setSortIndicatorShown(true);

    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
            &xTableView::showHeaderMenu);

    connect(horizontalScrollBar(), &QScrollBar::rangeChanged, this,
            &xTableView::updateFrozenGeometry);
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
            &xTableView::updateFrozenGeometry);
}


void xTableView::setStretchToFill(bool enabled) {
    stretchToFill_ = enabled;
    if (stretchToFill_) {
        // 设置拉伸模式，所有列将平分可用空间
        horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    } else {
        // 恢复为交互模式，用户可以手动调整列宽
        horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    }
}

void xTableView::setColumnWidthRatios(const QList<int> &ratios) {
    columnWidthRatios_ = ratios;
    setStretchToFill(false);
}

void xTableView::setSourceModel(QAbstractItemModel *m) {
    proxy_->setSourceModel(m);
    QTableView::setModel(proxy_);
    syncFrozen();
}

void xTableView::setColumnFilter(int col, const QVariantMap &cond) {
    proxy_->setColumnFilter(col, cond);
}

void xTableView::clearFilters() { proxy_->clearFilters(); }

void xTableView::sortBy(int col, Qt::SortOrder ord) { sortByColumn(col, ord); }

void xTableView::freezeLeftColumns(int n) {
    freezeCols_ = n > 0 ? n : 0;
    syncFrozen();
}
void xTableView::freezeTopRows(int n) {
    freezeRows_ = n > 0 ? n : 0;
    syncFrozen();
}

void xTableView::keyPressEvent(QKeyEvent *ev) {
    if (ev->matches(QKeySequence::Copy)) {
        copySelection();
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::Paste)) {
        paste();
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::Delete)) {
        removeSelectedCells();
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::Find)) {
        emit findRequested();
        ev->accept();
        return;
    }
    QTableView::keyPressEvent(ev);
}

void xTableView::resizeEvent(QResizeEvent *e) {
    if (!stretchToFill_ && !columnWidthRatios_.isEmpty()) {
        if (columnWidthRatios_.isEmpty()) return;

        int totalWidth = viewport()->width();
        int totalRatio = 0;
        for (int ratio : columnWidthRatios_) {
            totalRatio += ratio;
        }

        if (totalRatio == 0) return;

        for (int i = 0; i < columnWidthRatios_.size(); ++i) {
            // 确保列存在
            if (i < model()->columnCount()) {
                int columnWidth = (totalWidth * columnWidthRatios_[i]) / totalRatio;
                setColumnWidth(i, columnWidth);
            }
        }

    } else {
        QTableView::resizeEvent(e);
    }
    updateFrozenGeometry();
}

void xTableView::scrollContentsBy(int dx, int dy) {
    QTableView::scrollContentsBy(dx, dy);
    updateFrozenGeometry();
}

void xTableView::showHeaderMenu(const QPoint &pos) {
    int column = horizontalHeader()->logicalIndexAt(pos);
    if (column < 0) return;
    QMenu menu(this);
    QAction *hideAct = menu.addAction(tr("Hide Column"));
    QAction *showAll = menu.addAction(tr("Show All Columns"));
    menu.addSeparator();
    QAction *freezeAct = menu.addAction(tr("Freeze To This Column"));
    QAction *unfreezeAct = menu.addAction(tr("Unfreeze Columns"));
    menu.addSeparator();
    QAction *exportAct = menu.addAction(tr("Export Selection (TSV)"));
    unfreezeAct->setEnabled(freezeCols_ > 0);
    QAction *ret = menu.exec(horizontalHeader()->viewport()->mapToGlobal(pos));
    if (!ret) return;
    if (ret == hideAct)
        hideColumn(column);
    else if (ret == showAll) {
        for (int c = 0; c < model()->columnCount(); ++c) showColumn(c);
    } else if (ret == freezeAct) {
        freezeLeftColumns(column + 1);
    } else if (ret == unfreezeAct) {
        freezeLeftColumns(0);
    } else if (ret == exportAct) {
        copySelection();
    }
}

void xTableView::toggleSortColumn(int logicalCol) {
    if (logicalCol < 0) return;
    Qt::SortOrder ord = Qt::AscendingOrder;
    if (logicalCol == currentSortCol_)
        ord = (currentSortOrd_ == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
    currentSortCol_ = logicalCol;
    currentSortOrd_ = ord;
    sortBy(logicalCol, ord);
    horizontalHeader()->setSortIndicator(logicalCol, ord);
}

void xTableView::copySelection() {
    QItemSelection sel = selectionModel()->selection();
    if (sel.isEmpty()) return;
    QModelIndexList indexes = sel.indexes();
    if (indexes.isEmpty()) return;
    QMap<int, QMap<int, QString>> rowData;
    for (const QModelIndex &index : indexes) {
        rowData[index.row()][index.column()] = index.data(Qt::DisplayRole).toString();
    }
    QString text;
    for (auto it = rowData.constBegin(); it != rowData.constEnd(); ++it) {
        QStringList line;
        for (auto jt = it.value().constBegin(); jt != it.value().constEnd(); ++jt) {
            line << jt.value();
        }
        text += line.join("\t");
        text += "\n";
    }
    text.chop(1);
    QApplication::clipboard()->setText(text);
}

void xTableView::paste() {
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) return;
    QStringList rows = text.split("\n");
    QModelIndex start = currentIndex();
    if (!start.isValid()) start = model()->index(0, 0);
    int r0 = start.row();
    int c0 = start.column();
    for (int i = 0; i < rows.size(); ++i) {
        QStringList cols = rows[i].split("\t");
        for (int j = 0; j < cols.size(); ++j) {
            QModelIndex idx = model()->index(r0 + i, c0 + j);
            if (idx.isValid() && (idx.flags() & Qt::ItemIsEditable))
                model()->setData(idx, cols[j], Qt::EditRole);
        }
    }
}

void xTableView::removeSelectedCells() {
    auto sel = selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : sel) {
        if (idx.isValid() && (idx.flags() & Qt::ItemIsEditable))
            model()->setData(idx, QVariant(), Qt::EditRole);
    }
}

void xTableView::syncFrozen() {
    if (freezeCols_ > 0 && !frozenColView_) {
        createFrozenColView();
    } else if (freezeCols_ == 0 && frozenColView_) {
        frozenColView_->deleteLater();
        frozenColView_ = nullptr;
    }
    if (frozenColView_) {
        for (int c = 0; c < model()->columnCount(); ++c) {
            frozenColView_->setColumnHidden(c, c >= freezeCols_);
        }
    }
    if (freezeRows_ > 0 && !frozenRowView_) {
        createFrozenRowView();
    } else if (freezeRows_ == 0 && frozenRowView_) {
        frozenRowView_->deleteLater();
        frozenRowView_ = nullptr;
        frozenRowProxy_ = nullptr;
    }
    if (frozenRowProxy_) {
        frozenRowProxy_->setLimit(freezeRows_);
    }
    updateFrozenGeometry();
}

void xTableView::createFrozenColView() {
    frozenColView_ = new QTableView(this);
    frozenColView_->setModel(proxy_);
    frozenColView_->setItemDelegate(itemDelegate());
    frozenColView_->setFocusPolicy(Qt::NoFocus);
    frozenColView_->verticalHeader()->hide();
    frozenColView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    frozenColView_->setSelectionModel(selectionModel());
    frozenColView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frozenColView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(verticalScrollBar(), &QAbstractSlider::valueChanged,
            frozenColView_->verticalScrollBar(), &QAbstractSlider::setValue);
    connect(frozenColView_->verticalScrollBar(), &QAbstractSlider::valueChanged,
            verticalScrollBar(), &QAbstractSlider::setValue);
    frozenColView_->show();
}

void xTableView::createFrozenRowView() {
    frozenRowProxy_ = new xTableViewTopRowsFilter(this);
    frozenRowProxy_->setSourceModel(proxy_);
    frozenRowView_ = new QTableView(this);
    frozenRowView_->setModel(frozenRowProxy_);
    frozenRowView_->setItemDelegate(itemDelegate());
    frozenRowView_->setFocusPolicy(Qt::NoFocus);
    frozenRowView_->horizontalHeader()->hide();
    frozenRowView_->verticalHeader()->hide();
    frozenRowView_->setSelectionModel(selectionModel());
    frozenRowView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frozenRowView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(horizontalScrollBar(), &QAbstractSlider::valueChanged,
            frozenRowView_->horizontalScrollBar(), &QAbstractSlider::setValue);
    connect(frozenRowView_->horizontalScrollBar(), &QAbstractSlider::valueChanged,
            horizontalScrollBar(), &QAbstractSlider::setValue);

    // *** NEW: Connect main header resize signal to sync frozen row's column width ***
    connect(horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int logicalIndex, int, int newSize) {
                if (frozenRowView_) {
                    frozenRowView_->setColumnWidth(logicalIndex, newSize);
                }
            });

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, frozenColView_,
            static_cast<void (QWidget::*)()>(&QTableView::update));

    frozenRowView_->show();
}

void xTableView::updateFrozenGeometry() {
    if (frozenColView_) {
        int w = 0;
        for (int c = 0; c < freezeCols_; ++c) {
            if (!isColumnHidden(c)) {
                w += columnWidth(c);
            }
        }
        frozenColView_->setGeometry(verticalHeader()->width() + frameWidth(),
                                    frameWidth() + horizontalHeader()->height(), w,
                                    viewport()->height());
    }
    if (frozenRowView_) {
        int h = 0;
        for (int r = 0; r < freezeRows_; ++r) {
            if (!isRowHidden(r)) {
                h += rowHeight(r);
            }
        }
        frozenRowView_->setGeometry(verticalHeader()->width() + frameWidth(),
                                    frameWidth() + horizontalHeader()->height(),
                                    viewport()->width(), h);
    }
}
