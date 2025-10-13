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
#include "xTableView.h"
#include "xItemDelegate.h"
#include "xTableEditor.h"
#include "xTableHeader.h"
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
#include <QMetaType>
#include <QString>
#include <cstdio>  // For snprintf

/**
 * @brief 将浮点数格式化为7位有效数字的科学计数法字符串。
 * @param value 要格式化的浮点数。
 * @return 格式化后的字符串，例如 "+1.234560e+01"。
 */
static QString formatScientific(double value, int precision) {
    char fmt[32] = {0};

    snprintf(fmt, sizeof(fmt), "%%+%d.e", precision);
    // 缓冲区要足够大以容纳格式化后的字符串
    char buffer[32];

    // 步骤1: 使用 snprintf 完成大部分格式化工作
    // %      - 开始格式化说明
    // +      - 始终显示符号 (正号或负号)
    // .6     - 小数点后保留6位数字
    // e      - 使用小写'e'的科学计数法
    snprintf(buffer, sizeof(buffer), fmt, value);

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
    if (result.length() == e_pos + 3) {  // 例如 "e+1", "e-5"
        result.insert(e_pos + 2, '0');
    }

    return result;
}

xItemDelegate::xItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent),
      realnum_showmode_(xTableView::MODE_GENERAL),
      realnum_precision_(0) {}

QWidget *xItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                                     const QModelIndex &idx) const {
    Q_UNUSED(opt);
    if (idx.data(xTableView::StringListEditRole).toBool()) {
        QVariant factoryData = idx.data(xTableView::StringListDialogFactoryRole);
        if (factoryData.isValid()) {
            auto factory = factoryData.value<xTableView::StringListDialogFactory>();
            if (factory) {
                auto *editor = new xTableStringListEditor(factory, parent);
                connect(editor, &xTableStringListEditor::editingFinished, this,
                        &xItemDelegate::commitAndCloseEditor);
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
            for (const auto &item : lst) {
                e->addItem(QString::fromStdString(item));
            }
            e->setFrame(false);
            return e;
        }
    }
    QVariant v = idx.data(Qt::EditRole);
    switch (v.typeId()) {
        case QMetaType::Bool: {
            auto *container = new QWidget(parent);
            auto *layout = new QHBoxLayout(container);
            auto *editor = new QCheckBox(container);
            layout->addWidget(editor);
            layout->setAlignment(editor, Qt::AlignCenter);
            layout->setContentsMargins(0, 0, 0, 0);
            container->setLayout(layout);
            return container;
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

void xItemDelegate::setEditorData(QWidget *editor, const QModelIndex &idx) const {
    if (auto *stringListEditor = qobject_cast<xTableStringListEditor *>(editor)) {
        stringListEditor->setStringList(idx.data(Qt::EditRole).toStringList());
        return;
    }
    if (auto *cb = qobject_cast<QComboBox *>(editor)) {
        cb->setCurrentText(idx.data(Qt::DisplayRole).toString());
        return;
    }

    QVariant v = idx.data(Qt::EditRole);
    switch (v.typeId()) {
        case QMetaType::Bool: {
            QCheckBox *chk = editor->findChild<QCheckBox *>();
            if (chk) {
                chk->setChecked(v.toBool());
            }
            break;
        }
        case QMetaType::QDateTime:
            qobject_cast<QDateTimeEdit *>(editor)->setDateTime(v.toDateTime());
            break;
        case QMetaType::Double:
            qobject_cast<QLineEdit *>(editor)->setText(v.toString());
            break;
        case QMetaType::Int:
            qobject_cast<QSpinBox *>(editor)->setValue(v.toInt());
            break;
        default:
            // 默认处理，包括字符串
            if (auto *le = qobject_cast<QLineEdit *>(editor)) {
                le->setText(v.toString());
            }
            break;
    }
}

void xItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *mdl,
                                 const QModelIndex &idx) const {
    QVariant out;
    QVariant originalData = idx.data(Qt::EditRole);

    switch (originalData.typeId()) {
        case QMetaType::Bool: {
            // ==================== 【修复核心】 ====================
            // 编辑器是QWidget容器，我们需要从中找到真正的QCheckBox来获取数据。
            QCheckBox *chk = editor->findChild<QCheckBox *>();
            if (chk) {
                out = chk->isChecked();
            }
            // =====================================================
            break;
        }
        // 其他类型的处理保持不变
        default: {
            if (auto *stringListEditor = qobject_cast<xTableStringListEditor *>(editor)) {
                out = stringListEditor->getStringList();
            } else if (auto *cb = qobject_cast<QComboBox *>(editor)) {
                out = cb->currentText();
            } else if (auto *sb = qobject_cast<QSpinBox *>(editor)) {
                out = sb->value();
            } else if (auto *ds = qobject_cast<QDoubleSpinBox *>(editor)) {
                out = ds->value();
            } else if (auto *dt = qobject_cast<QDateTimeEdit *>(editor)) {
                out = dt->dateTime();
            } else if (auto *le = qobject_cast<QLineEdit *>(editor)) {
                out = le->text();
            }
        }
    }

    if (out.isValid()) {
        mdl->setData(idx, out, Qt::EditRole);
    }
}

QString xItemDelegate::displayText(const QVariant &value, const QLocale &locale) const {
    QMetaType::Type type = static_cast<QMetaType::Type>(value.typeId());
    if (type == QMetaType::Double || type == QMetaType::Float) {
        double val = value.toDouble();
        switch (realnum_showmode_) {
            case xTableView::MODE_FIXFLOAT:
                // 使用固定小数位数格式，精度由 realnum_precision_ 控制
                return locale.toString(val, 'f', realnum_precision_);

            case xTableView::MODE_SCIENTIFIC:
                // 使用您自定义的科学计数法格式
                return formatScientific(val, realnum_precision_);

            case xTableView::MODE_GENERAL:
            default:
                // 使用“通用”格式，自动在常规和小数之间切换
                return locale.toString(val, 'g', realnum_precision_ ? realnum_precision_ : 8);
        }
    }
    return QStyledItemDelegate::displayText(value, locale);
}

void xItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const {
    QStyleOptionViewItem option = opt;
    initStyleOption(&option, idx);
    QVariant data = idx.data(Qt::EditRole);

    // Handle bool type specially
    if (data.typeId() == QMetaType::Bool) {
        QStyleOptionButton opt;
        // 增大 checkbox 尺寸，从 16x16 改为行高的 80%，最小 18 像素
        int checkboxSize = qMax(18, static_cast<int>(option.rect.height() * 0.8));
        opt.rect = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter,
                                       QSize(checkboxSize, checkboxSize), option.rect);
        opt.state |= (data.toBool() ? QStyle::State_On : QStyle::State_Off);
        opt.state |= QStyle::State_Enabled;  // 确保 checkbox 处于启用状态

        // 如果单元格被选中，绘制选中背景
        if (option.state & QStyle::State_Selected) {
            p->fillRect(option.rect, option.palette.highlight());
        }

        QApplication::style()->drawControl(QStyle::CE_CheckBox, &opt, p);
        return;
    }

    // Add margin (1px on left and right)
    QRect adjustedRect = option.rect.adjusted(1, 0, -1, 0);
    option.rect = adjustedRect;

    // Handle text alignment - ensure numeric types are right-aligned and vertically centered
    QVariant alignmentData = idx.data(Qt::TextAlignmentRole);
    if (alignmentData.isValid()) {
        Qt::Alignment alignment = static_cast<Qt::Alignment>(alignmentData.toInt());

        // For numeric types (right-aligned), ensure vertical centering
        if (alignment & Qt::AlignRight) {
            // Ensure vertical centering is included
            alignment |= Qt::AlignVCenter;
            option.displayAlignment = alignment;
        } else if (alignment & Qt::AlignCenter) {
            // Bool type - already centered
            option.displayAlignment = alignment;
        } else {
            // String type - left-aligned with vertical centering
            alignment |= Qt::AlignVCenter;
            option.displayAlignment = alignment;
        }
    } else {
        // Default alignment based on data type
        if (data.typeId() == QMetaType::Double || data.typeId() == QMetaType::Float ||
            data.typeId() == QMetaType::Int || data.typeId() == QMetaType::LongLong) {
            option.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
        } else if (data.typeId() == QMetaType::Bool) {
            option.displayAlignment = Qt::AlignCenter | Qt::AlignVCenter;
        } else {
            option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        }
    }

    QVariant cond = idx.data(xTableView::ConditionRole);
    if (cond.isValid()) {
        if (cond.toString() == "error") option.palette.setColor(QPalette::Text, Qt::red);
    }

    QStyledItemDelegate::paint(p, option, idx);
}

bool xItemDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                const QStyleOptionViewItem &option, const QModelIndex &index) {
    // 确保是布尔类型的列
    if (index.data(Qt::EditRole).typeId() != QMetaType::Bool) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    // 处理鼠标按下和释放事件，提供更好的视觉反馈
    if (event->type() == QEvent::MouseButtonRelease) {
        // qDebug() << "QEvent::MouseButtonRelease";
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            // 计算 checkbox 的实际区域（与 paint() 方法中的逻辑一致）
            int checkboxSize = qMax(18, static_cast<int>(option.rect.height() * 0.8));
            QRect checkboxRect = QStyle::alignedRect(
                Qt::LeftToRight, Qt::AlignCenter, QSize(checkboxSize, checkboxSize), option.rect);

            // 扩大可点击区域，使其更容易点击（增加 4 像素的边距）
            checkboxRect = checkboxRect.adjusted(-4, -4, 4, 4);

            // 检查点击位置是否在 checkbox 区域内
            if (checkboxRect.contains(mouseEvent->pos())) {
                // qDebug() << "QEvent::MouseButtonRelease revert";
                bool currentValue = index.data(Qt::EditRole).toBool();
                model->setData(index, !currentValue, Qt::EditRole);
                return true;
            }
        }
    }

    // 对于其他事件，使用基类的默认行为
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

void xItemDelegate::commitAndCloseEditor() {
    auto *editor = qobject_cast<QWidget *>(sender());
    emit commitData(editor);
    emit closeEditor(editor);
}
