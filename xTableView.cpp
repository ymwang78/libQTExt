// ***************************************************************
//  xTableView   version:  1.4   -   date:  2025/07/04
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
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
#include <QCheckBox>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QSet>
#include "xTableEditor.h"
#include "xTableHeader.h"
#include <QMetaType>
#include <QString>
#include <cstdio> // For snprintf

/**
 * @brief 将浮点数格式化为7位有效数字的科学计数法字符串。
 * @param value 要格式化的浮点数。
 * @return 格式化后的字符串，例如 "+1.234560e+01"。
 */
QString formatScientific(double value)
{
    // 缓冲区要足够大以容纳格式化后的字符串
    char buffer[32];

    // 步骤1: 使用 snprintf 完成大部分格式化工作
    // %      - 开始格式化说明
    // +      - 始终显示符号 (正号或负号)
    // .6     - 小数点后保留6位数字
    // e      - 使用小写'e'的科学计数法
    snprintf(buffer, sizeof(buffer), "%+.6e", value);

    QString result = QString::fromLatin1(buffer);

    // 步骤2: 处理指数，确保它至少有两位数字
    // 查找 'e' 的位置
    int e_pos = result.lastIndexOf('e');
    if (e_pos == -1) {
        // 一般不会发生，但作为安全检查
        return result; 
    }

    // 指数符号位于 e_pos + 1
    // 指数数字部分从 e_pos + 2 开始
    // 检查指数数字部分的长度，如果只有1位，则在前面补'0'
    if (result.length() == e_pos + 3) { // 例如 "e+1", "e-5"
        result.insert(e_pos + 2, '0');
    }

    return result;
}

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

bool xTableViewSortFilter::lessThan(const QModelIndex &source_left,
                                    const QModelIndex &source_right) const {

        if (!source_left.isValid() || !source_right.isValid()) {
            return false;
        }
    
        auto source_model = qobject_cast<const xAbstractTableModel*>(sourceModel());
        
        // 检查是否开启了追加模式
        if (source_model && source_model->appendMode()) {
            int placeholderRow = source_model->baseRowCount();
    
            // 正确的做法：直接使用 source_left 和 source_right 的行号，因为它们已经是源模型的索引。
            bool leftIsPlaceholder = (source_left.row() == placeholderRow);
            bool rightIsPlaceholder = (source_right.row() == placeholderRow);
    
            // 如果两个索引中有一个是占位符行，则进入特殊处理逻辑
            if (leftIsPlaceholder || rightIsPlaceholder) {
                // 如果左边是占位符行
                if (leftIsPlaceholder) {
                    // 在降序时，占位符应视为“小于”真实行，以便排在最后，返回 true。
                    // 在升序时，应视为“不小于”真实行，返回 false。
                    return (sortOrder() == Qt::DescendingOrder);
                }
                
                // 如果右边是占位符行
                // (这里的逻辑与上面对称)
                if (rightIsPlaceholder) {
                    // 在升序时，真实行应视为“小于”占位符行，返回 true。
                    // 在降序时，返回 false。
                    return (sortOrder() == Qt::AscendingOrder);
                }
            }
        }
        
        // 对于所有其他情况（两个都是真实数据行，或未开启追加模式），使用默认的 Qt 比较逻辑
        return QSortFilterProxyModel::lessThan(source_left, source_right);
}

bool xTableViewSortFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    auto source = qobject_cast<const xAbstractTableModel *>(sourceModel());
    if (source && source->appendMode()) {
        int placeholderRow = source->baseRowCount(sourceParent);
        if (sourceRow == placeholderRow) {
            return true;  // Always show the placeholder row
        }
    }

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
    if (idx.data(xTableView::StringListEditRole).toBool()) {
        QVariant factoryData = idx.data(xTableView::StringListDialogFactoryRole);
        if (factoryData.isValid()) {
            auto factory = factoryData.value<xTableView::StringListDialogFactory>();
            if (factory) {
                auto *editor = new xTableStringListEditor(factory, parent);
                connect(editor, &xTableStringListEditor::editingFinished, this,
                        &xTableViewItemDelegate::commitAndCloseEditor);
                return editor;
            }
        }
    }
    QVariant comboData = idx.data(xTableView::ComboBoxItemsRole);
    if (comboData.isValid()) {
        if (comboData.canConvert<QStringList>()) {
            QComboBox *e = new QComboBox(parent);
            e->addItems(comboData.toStringList());
            e->setFrame(false);
            return e;
        } else if (comboData.canConvert<std::vector<std::string>>()) {
            QComboBox *e = new QComboBox(parent);
            std::vector<std::string> lst = comboData.value<std::vector<std::string>>();
            for (const auto& item : lst) {
                e->addItem(QString::fromStdString(item));
            }
            e->setFrame(false);
            return e;
        }
    }
    QVariant v = idx.data(Qt::EditRole);
    switch (v.typeId()) {
        case QMetaType::Bool: {
            QCheckBox *e = new QCheckBox(parent);
            return e;
        }
        case QMetaType::Int: {
            QSpinBox *e = new QSpinBox(parent);
            e->setFrame(false);
            e->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            return e;
        }
        case QMetaType::Double: {
            QLineEdit *e = new QLineEdit(parent);
            e->setFrame(false);  // 保持无边框的嵌入式外观

            // 2. 创建一个浮点数验证器
            auto *validator = new QDoubleValidator(e);

            // 3. 【最关键的一步】设置验证器接受科学计数法
            //    (它同时也接受标准的小数表示法)
            validator->setNotation(QDoubleValidator::ScientificNotation);

            // 4. (可选) 设置范围和精度
            validator->setRange(-1.0e20, 1.0e20, 15);  // DBL_MAX, 15位小数

            // 5. 将验证器应用到行编辑器上
            e->setValidator(validator);
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
    if (auto *stringListEditor = qobject_cast<xTableStringListEditor *>(editor)) {
        // 从模型获取 QStringList 并设置给编辑器
        stringListEditor->setStringList(idx.data(Qt::EditRole).toStringList());
        return;
    }
    if (auto *cb = qobject_cast<QComboBox *>(editor)) {
        cb->setCurrentText(idx.data(Qt::DisplayRole).toString());
        return;
    }
    QVariant v = idx.data(Qt::EditRole);
    if (v.typeId() == QMetaType::Bool) {
        qobject_cast<QCheckBox *>(editor)->setChecked(v.toBool());
    } else if (v.typeId() == QMetaType::QDateTime) {
        qobject_cast<QDateTimeEdit *>(editor)->setDateTime(v.toDateTime());
    } else if (v.typeId() == QMetaType::Double) {
        qobject_cast<QLineEdit *>(editor)->setText(v.toString());
    } else if (v.typeId() == QMetaType::Int) {
        qobject_cast<QSpinBox *>(editor)->setValue(v.toInt());
    } else {
        qobject_cast<QLineEdit *>(editor)->setText(v.toString());
    }
}

void xTableViewItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *mdl,
                                          const QModelIndex &idx) const {
    QVariant out;
    if (auto *stringListEditor = qobject_cast<xTableStringListEditor *>(editor)) {
        out = stringListEditor->getStringList();
    }
    else if (auto *cb = qobject_cast<QComboBox *>(editor)) {
        out = cb->currentText();
    } else if (auto *chk = qobject_cast<QCheckBox *>(editor))
        out = chk->isChecked();
    else if (auto *sb = qobject_cast<QSpinBox *>(editor))
        out = sb->value();
    else if (auto *ds = qobject_cast<QDoubleSpinBox *>(editor))
        out = ds->value();
    else if (auto *dt = qobject_cast<QDateTimeEdit *>(editor))
        out = dt->dateTime();
    else if (auto *le = qobject_cast<QLineEdit *>(editor))
        out = le->text();
    mdl->setData(idx, out, Qt::EditRole);
}

QString xTableViewItemDelegate::displayText(const QVariant &value, const QLocale &locale) const {
    QMetaType::Type type = static_cast<QMetaType::Type>(value.typeId());
    if (type == QMetaType::Double || type == QMetaType::Float) {
        return formatScientific(value.toDouble());
    }
    return QStyledItemDelegate::displayText(value, locale);
}

void xTableViewItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                                   const QModelIndex &idx) const {
    QStyleOptionViewItem option = opt;
    initStyleOption(&option, idx);
    QVariant data = idx.data(Qt::EditRole); 
    
    // Handle bool type specially
    if (data.typeId() == QMetaType::Bool) {
        QStyleOptionButton checkboxOpt;
        checkboxOpt.rect = option.rect;
        checkboxOpt.state = option.state;
        checkboxOpt.state |= data.toBool() ? QStyle::State_On : QStyle::State_Off;
        checkboxOpt.state |= QStyle::State_Enabled;
        
        // Center the checkbox
        QSize checkboxSize = QApplication::style()->sizeFromContents(QStyle::CT_CheckBox, &checkboxOpt, QSize());
        checkboxOpt.rect = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, checkboxSize, option.rect);
        
        QApplication::style()->drawControl(QStyle::CE_CheckBox, &checkboxOpt, p);
        return;
    }
    
    switch (static_cast<QMetaType::Type>(data.typeId())) {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Double:
        case QMetaType::Float:
            option.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
            break;
        default:
            option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
            break;
    }

    QVariant cond = idx.data(xTableView::ConditionRole);
    if (cond.isValid()) {
        if (cond.toString() == "error") option.palette.setColor(QPalette::Text, Qt::red);
    }

    QStyledItemDelegate::paint(p, option, idx);
}

void xTableViewItemDelegate::commitAndCloseEditor() {
    auto *editor = qobject_cast<QWidget *>(sender());
    emit commitData(editor);
    emit closeEditor(editor);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
xAbstractTableModel::xAbstractTableModel(QObject *parent) : QAbstractTableModel(parent) {}

void xAbstractTableModel::setAppendMode(bool enabled) {
    if (append_mode_ == enabled) return;

    append_mode_ = enabled;

    int realRowCount = baseRowCount(QModelIndex());
    if (append_mode_) {
        // 开启模式：在末尾插入占位符行
        beginInsertRows(QModelIndex(), realRowCount, realRowCount);
        endInsertRows();
    } else {
        // 关闭模式：移除末尾的占位符行
        beginRemoveRows(QModelIndex(), realRowCount, realRowCount);
        endRemoveRows();
    }
}

int xAbstractTableModel::rowCount(const QModelIndex &parent) const {
    int realRowCount = baseRowCount(parent);
    // 真实行数 + 1个占位符行（如果开启）
    return realRowCount + (append_mode_ ? 1 : 0);
}

QVariant xAbstractTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return {};

    int realRowCount = baseRowCount(index.parent());
    // 检查是否是占位符行
    if (append_mode_ && index.row() == realRowCount) {
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

Qt::ItemFlags xAbstractTableModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;

    int realRowCount = baseRowCount(index.parent());
    // 检查是否是占位符行
    if (append_mode_ && index.row() == realRowCount) {
        // 占位符行默认所有列都可编辑，子类可以在 baseFlags 中覆盖此行为
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    }
    // 对于真实数据行，调用子类的实现
    return baseFlags(index);
}

bool xAbstractTableModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (role != Qt::EditRole || !index.isValid()) {
        return false;
    }

    int realRowCount = baseRowCount(index.parent());
    // 检查是否是占位符行
    if (append_mode_ && index.row() == realRowCount) {
        // 是占位符行，触发新增逻辑

        // 1. 通知视图，即将在占位符的位置（即末尾）插入一个新行
        beginInsertRows(QModelIndex(), realRowCount, realRowCount);

        // 2. 调用子类实现在底层数据源中创建一条空记录
        bool success = insertNewBaseRow(realRowCount);

        // 3. 通知视图插入完成
        endInsertRows();

        if (!success) {
            return false;  // 如果子类插入失败，则终止
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
      frozen_row_filter_(nullptr),
      frozen_row_view_(nullptr),
      frozen_col_view_(nullptr),
      freeze_cols_(0),
      freeze_rows_(0),
      current_sort_col_(-1),
      current_sort_order_(Qt::AscendingOrder) {

    checkable_header_ = new xCheckableHeaderView(Qt::Horizontal, this);
    setHorizontalHeader(checkable_header_);

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
    is_stretch_to_fill_ = enabled;
    if (is_stretch_to_fill_) {
        // 设置拉伸模式，所有列将平分可用空间
        horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    } else {
        // 恢复为交互模式，用户可以手动调整列宽
        horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    }
}

void xTableView::setColumnWidthRatios(const QList<int> &ratios) {
    column_width_ratios_ = ratios;
    setStretchToFill(false);
}

void xTableView::setSourceModel(QAbstractItemModel *m) {
    // 如果旧模型存在，先断开连接
    QAbstractItemModel *oldSourceModel = proxy_->sourceModel();

    // 2. 如果旧的源模型存在，则断开连接
    if (oldSourceModel) {
        disconnect(oldSourceModel, &QAbstractItemModel::dataChanged, this, nullptr);
    }

    proxy_->setSourceModel(m);
    QTableView::setModel(proxy_);

    // 连接新模型
    if (m) {
        connect(m, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
                    for (int col = topLeft.column(); col <= bottomRight.column(); ++col) {
                        if (isBoolColumn(col)) {
                            updateBoolColumnHeaderState(col);
                        }
                    }
                });
    }
    syncFrozen();
}

void xTableView::setColumnFilter(int col, const QVariantMap &cond) {
    proxy_->setColumnFilter(col, cond);
}

void xTableView::clearFilters() { proxy_->clearFilters(); }

void xTableView::sortBy(int col, Qt::SortOrder ord) { sortByColumn(col, ord); }

void xTableView::freezeLeftColumns(int n) {
    freeze_cols_ = n > 0 ? n : 0;
    syncFrozen();
}
void xTableView::freezeTopRows(int n) {
    freeze_rows_ = n > 0 ? n : 0;
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
    if (!is_stretch_to_fill_ && !column_width_ratios_.isEmpty()) {
        if (column_width_ratios_.isEmpty()) return;

        int totalWidth = viewport()->width();
        int totalRatio = 0;
        for (int ratio : column_width_ratios_) {
            totalRatio += ratio;
        }

        if (totalRatio == 0) return;

        for (int i = 0; i < column_width_ratios_.size(); ++i) {
            // 确保列存在
            if (i < model()->columnCount()) {
                int columnWidth = (totalWidth * column_width_ratios_[i]) / totalRatio;
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
    
    // Add bool column type menu
    QAction *setBoolAct = menu.addAction(tr("Set as Bool Column"));
    QAction *unsetBoolAct = menu.addAction(tr("Unset Bool Column"));
    setBoolAct->setEnabled(!isBoolColumn(column));
    unsetBoolAct->setEnabled(isBoolColumn(column));
    menu.addSeparator();
    
    QAction *exportAct = menu.addAction(tr("Export Selection (TSV)"));
    unfreezeAct->setEnabled(freeze_cols_ > 0);
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
    } else if (ret == setBoolAct) {
        setBoolColumn(column, true);
    } else if (ret == unsetBoolAct) {
        setBoolColumn(column, false);
    } else if (ret == exportAct) {
        copySelection();
    }
}

void xTableView::toggleSortColumn(int logicalCol) {
    if (logicalCol < 0) return;

    if (logicalCol == current_sort_col_) {
        // 第2次或第3次点击同一个已排序的列
        if (current_sort_order_ == Qt::AscendingOrder) {
            // 第2次点击：从升序变为降序
            current_sort_order_ = Qt::DescendingOrder;
            sortBy(current_sort_col_, current_sort_order_);
            horizontalHeader()->setSortIndicator(current_sort_col_, current_sort_order_);
        } else {
            // 第3次点击：从降序变为“未排序”
            current_sort_col_ = -1;  // 重置当前排序列
            // 调用 sort(-1) 来禁用代理模型的排序，恢复源模型顺序
            proxy_->sort(-1);
            // 隐藏表头的排序箭头
            horizontalHeader()->setSortIndicatorShown(false);
            // 再次设置以确保旧的箭头完全消失
            horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
        }
    } else {
        // 第1次点击一个新的列：总是升序
        current_sort_col_ = logicalCol;
        current_sort_order_ = Qt::AscendingOrder;
        // 确保排序箭头是可见的
        horizontalHeader()->setSortIndicatorShown(true);
        sortBy(current_sort_col_, current_sort_order_);
        horizontalHeader()->setSortIndicator(current_sort_col_, current_sort_order_);
    }
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
    if (freeze_cols_ > 0 && !frozen_col_view_) {
        createFrozenColView();
    } else if (freeze_cols_ == 0 && frozen_col_view_) {
        frozen_col_view_->deleteLater();
        frozen_col_view_ = nullptr;
    }
    if (frozen_col_view_) {
        for (int c = 0; c < model()->columnCount(); ++c) {
            frozen_col_view_->setColumnHidden(c, c >= freeze_cols_);
        }
    }
    if (freeze_rows_ > 0 && !frozen_row_view_) {
        createFrozenRowView();
    } else if (freeze_rows_ == 0 && frozen_row_view_) {
        frozen_row_view_->deleteLater();
        frozen_row_view_ = nullptr;
        frozen_row_filter_ = nullptr;
    }
    if (frozen_row_filter_) {
        frozen_row_filter_->setLimit(freeze_rows_);
    }
    updateFrozenGeometry();
}

void xTableView::createFrozenColView() {
    frozen_col_view_ = new QTableView(this);
    frozen_col_view_->setModel(proxy_);
    frozen_col_view_->setItemDelegate(itemDelegate());
    frozen_col_view_->setFocusPolicy(Qt::NoFocus);
    frozen_col_view_->verticalHeader()->hide();
    frozen_col_view_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    frozen_col_view_->setSelectionModel(selectionModel());
    frozen_col_view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frozen_col_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(verticalScrollBar(), &QAbstractSlider::valueChanged,
            frozen_col_view_->verticalScrollBar(), &QAbstractSlider::setValue);
    connect(frozen_col_view_->verticalScrollBar(), &QAbstractSlider::valueChanged,
            verticalScrollBar(), &QAbstractSlider::setValue);
    frozen_col_view_->show();
}

void xTableView::createFrozenRowView() {
    frozen_row_filter_ = new xTableViewTopRowsFilter(this);
    frozen_row_filter_->setSourceModel(proxy_);
    frozen_row_view_ = new QTableView(this);
    frozen_row_view_->setModel(frozen_row_filter_);
    frozen_row_view_->setItemDelegate(itemDelegate());
    frozen_row_view_->setFocusPolicy(Qt::NoFocus);
    frozen_row_view_->horizontalHeader()->hide();
    frozen_row_view_->verticalHeader()->hide();
    frozen_row_view_->setSelectionModel(selectionModel());
    frozen_row_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frozen_row_view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(horizontalScrollBar(), &QAbstractSlider::valueChanged,
            frozen_row_view_->horizontalScrollBar(), &QAbstractSlider::setValue);
    connect(frozen_row_view_->horizontalScrollBar(), &QAbstractSlider::valueChanged,
            horizontalScrollBar(), &QAbstractSlider::setValue);

    // *** NEW: Connect main header resize signal to sync frozen row's column width ***
    connect(horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int logicalIndex, int, int newSize) {
                if (frozen_row_view_) {
                    frozen_row_view_->setColumnWidth(logicalIndex, newSize);
                }
            });

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, frozen_col_view_,
            static_cast<void (QWidget::*)()>(&QTableView::update));

    frozen_row_view_->show();
}

void xTableView::updateFrozenGeometry() {
    if (frozen_col_view_) {
        int w = 0;
        for (int c = 0; c < freeze_cols_; ++c) {
            if (!isColumnHidden(c)) {
                w += columnWidth(c);
            }
        }
        frozen_col_view_->setGeometry(verticalHeader()->width() + frameWidth(),
                                    frameWidth() + horizontalHeader()->height(), w,
                                    viewport()->height());
    }
    if (frozen_row_view_) {
        int h = 0;
        for (int r = 0; r < freeze_rows_; ++r) {
            if (!isRowHidden(r)) {
                h += rowHeight(r);
            }
        }
        frozen_row_view_->setGeometry(verticalHeader()->width() + frameWidth(),
                                    frameWidth() + horizontalHeader()->height(),
                                    viewport()->width(), h);
    }
}

// Bool column management implementation
void xTableView::setBoolColumn(int column, bool enabled) {
    if (enabled) {
        bool_columns_.insert(column);
    } else {
        bool_columns_.remove(column);
        // 也清理内存状态
        bool_column_memory_states_.remove(column);
    }
    // 通知自定义的表头这一变化
    checkable_header_->setBoolColumn(column, enabled);
    // 如果设置为布尔列，立即计算并更新其初始状态
    if (enabled) {
        updateBoolColumnHeaderState(column);
    }
}

bool xTableView::isBoolColumn(int column) const {
    return bool_columns_.contains(column);
}

QSet<int> xTableView::getBoolColumns() const {
    return bool_columns_;
}

Qt::CheckState xTableView::calculateBoolColumnState(int column) const {
    if (!model()) return Qt::Unchecked;
    
    int totalRows = model()->rowCount();
    if (totalRows == 0) return Qt::Unchecked;
    
    int checkedCount = 0;
    int validCount = 0;
    
    for (int row = 0; row < totalRows; ++row) {
        QModelIndex idx = model()->index(row, column);
        if (idx.isValid()) {
            QVariant data = idx.data(Qt::EditRole);
            if (data.typeId() == QMetaType::Bool) {
                validCount++;
                if (data.toBool()) {
                    checkedCount++;
                }
            }
        }
    }
    
    if (validCount == 0) return Qt::Unchecked;
    if (checkedCount == 0) return Qt::Unchecked;
    if (checkedCount == validCount) return Qt::Checked;
    return Qt::PartiallyChecked;
}

void xTableView::onHeaderCheckboxToggled(int column, Qt::CheckState state) {
    if (!model()) return;

    // 注意：这里的逻辑与您原来的有些不同，因为点击逻辑已在Header中简化为“切换”
    // 我们根据收到的新状态(Checked 或 Unchecked)来执行操作

    if (state == Qt::Checked) {
        // 检查是否是从部分选中状态过来的，如果是，则保存之前的状态
        if (calculateBoolColumnState(column) == Qt::PartiallyChecked) {
            saveBoolColumnMemoryState(column);
        }
        // 将所有行设置为 true
        for (int row = 0; row < model()->rowCount(); ++row) {
            QModelIndex idx = model()->index(row, column);
            if (idx.isValid() && (idx.flags() & Qt::ItemIsEditable)) {
                model()->setData(idx, true, Qt::EditRole);
            }
        }
    } else {  // state == Qt::Unchecked
              // 将所有行设置为 false
        for (int row = 0; row < model()->rowCount(); ++row) {
            QModelIndex idx = model()->index(row, column);
            if (idx.isValid() && (idx.flags() & Qt::ItemIsEditable)) {
                model()->setData(idx, false, Qt::EditRole);
            }
        }
    }

    // 数据模型改变后，它会发出 dataChanged 信号，我们用它来更新表头最终的状态
}

void xTableView::updateBoolColumnHeaderState(int column) {
    if (isBoolColumn(column)) {
        Qt::CheckState state = calculateBoolColumnState(column);
        checkable_header_->setCheckState(column, state);
    }
}

void xTableView::saveBoolColumnMemoryState(int column) {
    if (!model()) return;
    
    QVector<bool> states;
    int totalRows = model()->rowCount();
    
    for (int row = 0; row < totalRows; ++row) {
        QModelIndex idx = model()->index(row, column);
        if (idx.isValid()) {
            QVariant data = idx.data(Qt::EditRole);
            if (data.typeId() == QMetaType::Bool) {
                states.append(data.toBool());
            } else {
                states.append(false); // default for non-bool items
            }
        } else {
            states.append(false);
        }
    }
    
    bool_column_memory_states_[column] = states;
}

void xTableView::restoreBoolColumnMemoryState(int column) {
    if (!model()) return;
    
    auto it = bool_column_memory_states_.find(column);
    if (it == bool_column_memory_states_.end()) {
        return; // No memory state saved
    }
    
    const QVector<bool>& states = it.value();
    int totalRows = model()->rowCount();
    int stateCount = states.size();
    
    for (int row = 0; row < totalRows && row < stateCount; ++row) {
        QModelIndex idx = model()->index(row, column);
        if (idx.isValid() && (idx.flags() & Qt::ItemIsEditable)) {
            QVariant data = idx.data(Qt::EditRole);
            if (data.typeId() == QMetaType::Bool) {
                model()->setData(idx, states[row], Qt::EditRole);
            }
        }
    }
}
