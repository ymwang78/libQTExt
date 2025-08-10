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
#include "xItemDelegate.h"
#include "xTableEditor.h"
#include "xTableView.h"
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
static QString formatScientific(double value) {
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
    if (result.length() == e_pos + 3) {  // 例如 "e+1", "e-5"
        result.insert(e_pos + 2, '0');
    }

    return result;
}


xItemDelegate::xItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

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

void xItemDelegate::setEditorData(QWidget *editor, const QModelIndex &idx) const {
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

void xItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *mdl,
                                          const QModelIndex &idx) const {
    QVariant out;
    if (auto *stringListEditor = qobject_cast<xTableStringListEditor *>(editor)) {
        out = stringListEditor->getStringList();
    } else if (auto *cb = qobject_cast<QComboBox *>(editor)) {
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

QString xItemDelegate::displayText(const QVariant &value, const QLocale &locale) const {
    QMetaType::Type type = static_cast<QMetaType::Type>(value.typeId());
    if (type == QMetaType::Double || type == QMetaType::Float) {
        return formatScientific(value.toDouble());
    }
    return QStyledItemDelegate::displayText(value, locale);
}

void xItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                                   const QModelIndex &idx) const {
    QStyleOptionViewItem option = opt;
    initStyleOption(&option, idx);
    QVariant data = idx.data(Qt::EditRole);

    // Handle bool type specially
    case QVariant::Bool:
		{
			QStyleOptionButton opt;
			opt.rect = option.rect;
			opt.state = QStyle::State_Enabled;
			opt.state |= (var.toBool() ? QStyle::State_On : QStyle::State_Off);
			if (option.state & QStyle::State_Selected) {
				painter->fillRect(option.rect, option.palette.highlight());
				opt.palette = option.palette;
			}
			QApplication::style()->drawControl(QStyle::CE_CheckBox, &opt, painter);
		}
		break;

    QVariant cond = idx.data(xTableView::ConditionRole);
    if (cond.isValid()) {
        if (cond.toString() == "error") option.palette.setColor(QPalette::Text, Qt::red);
    }

    QStyledItemDelegate::paint(p, option, idx);
}

void xItemDelegate::commitAndCloseEditor() {
    auto *editor = qobject_cast<QWidget *>(sender());
    emit commitData(editor);
    emit closeEditor(editor);
}
