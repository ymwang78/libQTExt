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
#include "xTableView.h"

// custom role for conditional formatting
constexpr int xTableViewCondRole = Qt::UserRole + 100;
///////////////////////////////////////////////////////////////////////////////////////////////////

xTableViewSortFilter::xTableViewSortFilter(QObject *parent) : QSortFilterProxyModel(parent) {}

void xTableViewSortFilter::setColumnFilter(int column, const QVariantMap &conditions) {
    auto rule = std::find_if(filters_.begin(), filters_.end(),
                             [&](const xTableViewFilterRule &r) { return r.column == column; });
    xTableViewFilterRule fr;
    fr.column = column;
    // regex
    if (conditions.contains("regex"))
        fr.regex = QRegularExpression(conditions.value("regex").toString(),
                                      QRegularExpression::CaseInsensitiveOption);
    // exact equality
    if (conditions.contains("equals")) fr.equals = conditions.value("equals");
    // bounds
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
            if (data.typeId() == QMetaType::Double) {
                double d = data.toDouble();
                if (d < fr.min || d > fr.max) return false;
            }
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

xTableViewItemDelegate::xTableViewItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QWidget *xTableViewItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                                              const QModelIndex &idx) const {
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
            e->setRange(-1e100, 1e100);
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
    QVariant v = idx.data(Qt::EditRole);
    if (v.typeId() == QMetaType::QDateTime) {
        QDateTimeEdit *e = qobject_cast<QDateTimeEdit *>(editor);
        e->setDateTime(v.toDateTime());
    } else if (v.typeId() == QMetaType::Double) {
        QDoubleSpinBox *e = qobject_cast<QDoubleSpinBox *>(editor);
        e->setValue(v.toDouble());
    } else if (v.typeId() == QMetaType::Int) {
        QSpinBox *e = qobject_cast<QSpinBox *>(editor);
        e->setValue(v.toInt());
    } else {
        QLineEdit *e = qobject_cast<QLineEdit *>(editor);
        e->setText(v.toString());
    }
}

void xTableViewItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *mdl,
                                          const QModelIndex &idx) const {
    QVariant out;
    if (auto *sb = qobject_cast<QSpinBox *>(editor))
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
    QVariant cond = idx.data(xTableViewCondRole);
    if (cond.isValid()) {
        if (cond.toString() == "error") o.palette.setColor(QPalette::Text, Qt::red);
    }
    QStyledItemDelegate::paint(p, o, idx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

xTableView::xTableView(QWidget *parent)
    : QTableView(parent),
      proxy_(new xTableViewSortFilter(this)),
      frozenRowView_(nullptr),
      frozenColView_(nullptr),
      freezeCols_(0),
      freezeRows_(0) {
    // view setup
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

    // header context menu
    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
            &xTableView::showHeaderMenu);

    // keyboard shortcuts handled by overriding keyPressEvent
}

// Provide source model externally so users can hold pointer
void xTableView::setSourceModel(QAbstractItemModel *m) {
    proxy_->setSourceModel(m);
    QTableView::setModel(proxy_);
    syncFrozen();
}

// Public filtering / sorting helpers --------------------------------------------------
void xTableView::setColumnFilter(int col, const QVariantMap &cond) {
    proxy_->setColumnFilter(col, cond);
}

void xTableView::clearFilters() { proxy_->clearFilters(); }

void xTableView::sortBy(int col, Qt::SortOrder ord) { sortByColumn(col, ord); }

// Freeze API -------------------------------------------------------------------------
void xTableView::freezeLeftColumns(int n) {
    freezeCols_ = n;
    syncFrozen();
}
void xTableView::freezeTopRows(int n) {
    freezeRows_ = n;
    syncFrozen();
}

// Theme -----------------------------------------------------------------------------
void xTableView::applyTheme(const QString &name) {
    if (name == "dark")
        setStyleSheet(darkQss());
    else
        setStyleSheet(lightQss());
}

// keyboard shortcuts ---------------------------------------------------------------
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
    QTableView::resizeEvent(e);
    updateFrozenGeometry();
}

void xTableView::showHeaderMenu(const QPoint &pos) {
    int column = horizontalHeader()->logicalIndexAt(pos);
    QMenu menu(this);

    QAction *hideAct = menu.addAction(tr("Hide Column"));
    QAction *showAll = menu.addAction(tr("Show All Columns"));
    menu.addSeparator();
    QAction *freezeAct = menu.addAction(tr("Freeze To This Column"));
    QAction *unfreezeAct = menu.addAction(tr("Unfreeze Columns"));
    menu.addSeparator();
    QAction *exportAct = menu.addAction(tr("Export Selection (TSV)"));

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

// Copy / Paste / Delete --------------------------------------------------------------
void xTableView::copySelection() {
    QItemSelection sel = selectionModel()->selection();
    if (sel.isEmpty()) return;
    // assume contiguous selection
    QModelIndex topLeft = sel.first().topLeft();
    QModelIndex bottomRight = sel.last().bottomRight();
    QString text;
    for (int r = topLeft.row(); r <= bottomRight.row(); ++r) {
        QStringList line;
        for (int c = topLeft.column(); c <= bottomRight.column(); ++c) {
            line << model()->index(r, c).data(Qt::DisplayRole).toString();
        }
        text += line.join("\t");
        if (r != bottomRight.row()) text += "\n";
    }
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
            if (idx.flags() & Qt::ItemIsEditable) model()->setData(idx, cols[j], Qt::EditRole);
        }
    }
}

void xTableView::removeSelectedCells() {
    auto sel = selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : sel) {
        if (idx.flags() & Qt::ItemIsEditable) model()->setData(idx, QVariant(), Qt::EditRole);
    }
}

// Freeze implementation -------------------------------------------------------------
void xTableView::initFrozen(QTableView *&view) {
    if (!view) {
        view = new QTableView(this);
        view->setModel(model());
        view->verticalHeader()->hide();
        view->horizontalHeader()->hide();
        view->setFocusPolicy(Qt::NoFocus);
        view->setSelectionModel(selectionModel());
        view->setStyleSheet("QTableView { background: palette(window); }");
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setFrameShape(QFrame::NoFrame);
        view->show();
    }
}

void xTableView::syncFrozen() {
    if (freezeCols_ > 0)
        initFrozen(frozenColView_);
    else
        removeFrozen(&frozenColView_);
    if (freezeRows_ > 0)
        initFrozen(frozenRowView_);
    else
        removeFrozen(&frozenRowView_);
    updateFrozenGeometry();
}

void xTableView::removeFrozen(QTableView **view) {
    if (*view) {
        delete *view;
        *view = nullptr;
    }
}

void xTableView::updateFrozenGeometry() {
    if (frozenColView_) {
        int w = 0;
        for (int c = 0; c < freezeCols_; ++c) w += columnWidth(c);
        frozenColView_->setGeometry(frameWidth(), frameWidth() + horizontalHeader()->height(), w,
                                    viewport()->height());
        // hide / show columns accordingly
        for (int c = 0; c < model()->columnCount(); ++c)
            frozenColView_->setColumnHidden(c, c >= freezeCols_ || isColumnHidden(c));
        for (int r = 0; r < model()->rowCount(); ++r)
            frozenColView_->setRowHidden(r, isRowHidden(r));
    }
    if (frozenRowView_) {
        int h = 0;
        for (int r = 0; r < freezeRows_; ++r) h += rowHeight(r);
        frozenRowView_->setGeometry(frameWidth() + verticalHeader()->width(), frameWidth(),
                                    viewport()->width(), h);
        for (int r = 0; r < model()->rowCount(); ++r)
            frozenRowView_->setRowHidden(r, r >= freezeRows_ || isRowHidden(r));
        for (int c = 0; c < model()->columnCount(); ++c)
            frozenRowView_->setColumnHidden(c, isColumnHidden(c));
    }
}

// Themes ---------------------------------------------------------------------------
QString xTableView::darkQss() {
    return QLatin1String(
        "QTableView{background:#121212;color:#E0E0E0;gridline-color:#333;}"
        "QHeaderView::section{background:#1E1E1E;color:#E0E0E0;padding:4px;border:0px;}"
        "QTableView::item:selected{background:#2D5AA7;}");
}
QString xTableView::lightQss() {
    return QLatin1String(
        "QTableView{background:white;color:black;}"
        "QHeaderView::section{background:#F0F0F0;color:#333;padding:4px;border:0px;}"
        "QTableView::item:selected{background:#CCE8FF;}");
}
