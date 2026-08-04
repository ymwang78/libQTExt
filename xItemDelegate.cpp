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
#include <QHBoxLayout>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QCheckBox>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSizePolicy>
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

static QLineEdit *editorLineEdit(QWidget *editor) {
    if (!editor) return nullptr;
    if (auto *lineEdit = qobject_cast<QLineEdit *>(editor)) return lineEdit;
    return editor->findChild<QLineEdit *>();
}

// 剪贴板内容常来自终端 / Excel，往往带有 \r\n、制表符和尾部空白。
// 单元格编辑器是单行的，只取第一行第一格并去掉首尾空白，
// 否则带验证器的编辑器（如数值列的 QDoubleValidator）会把整次粘贴判为 Invalid 而全部丢弃。
static QString clipboardTextForCellEditor() {
    const QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) return QString();

    QString text = clipboard->text();
    if (text.isEmpty()) return text;

    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const int line_end = text.indexOf(QLatin1Char('\n'));
    if (line_end >= 0) text.truncate(line_end);
    const int cell_end = text.indexOf(QLatin1Char('\t'));
    if (cell_end >= 0) text.truncate(cell_end);
    return text.trimmed();
}

// 返回 true 表示粘贴已由编辑器处理，事件不再向表格冒泡
static bool pasteIntoEditor(QWidget *editor) {
    if (!editor) return false;

    const QString text = clipboardTextForCellEditor();

    // 不可编辑的下拉框自身不处理 Ctrl+V，这里按文本匹配选项
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        if (text.isEmpty()) return true;
        const int match = combo->findText(text, Qt::MatchFixedString);
        if (match >= 0) {
            combo->setCurrentIndex(match);
        } else if (combo->isEditable()) {
            combo->setCurrentText(text);
        }
        return true;
    }

    // 布尔列的编辑器是包着 QCheckBox 的容器
    QCheckBox *check = qobject_cast<QCheckBox *>(editor);
    if (!check) check = editor->findChild<QCheckBox *>();
    if (check) {
        if (text.isEmpty()) return true;
        const QString normalized = text.toLower();
        if (normalized == QLatin1String("true") || normalized == QLatin1String("1") ||
            normalized == QLatin1String("yes") || normalized == QLatin1String("on")) {
            check->setChecked(true);
        } else if (normalized == QLatin1String("false") || normalized == QLatin1String("0") ||
                   normalized == QLatin1String("no") || normalized == QLatin1String("off")) {
            check->setChecked(false);
        }
        return true;
    }

    if (auto *lineEdit = editorLineEdit(editor)) {
        if (lineEdit->isReadOnly()) return false;
        if (!text.isEmpty()) lineEdit->insert(text);
        return true;
    }

    return false;
}

static Qt::Alignment editorAlignmentFor(const QModelIndex &index) {
    QVariant alignmentData = index.data(Qt::TextAlignmentRole);
    if (alignmentData.isValid()) {
        return static_cast<Qt::Alignment>(alignmentData.toInt()) | Qt::AlignVCenter;
    }

    QVariant data = index.data(Qt::EditRole);
    if (data.typeId() == QMetaType::Double || data.typeId() == QMetaType::Float ||
        data.typeId() == QMetaType::Int || data.typeId() == QMetaType::LongLong) {
        return Qt::AlignRight | Qt::AlignVCenter;
    }
    if (data.typeId() == QMetaType::Bool) {
        return Qt::AlignCenter | Qt::AlignVCenter;
    }
    return Qt::AlignLeft | Qt::AlignVCenter;
}

static void polishLineEditEditor(QLineEdit *editor, const QStyleOptionViewItem *option,
                                 const QModelIndex &index) {
    if (!editor) return;

    QFont editorFont = option ? option->font : editor->font();
    QString fontSizeRule;
    QString heightRule;
    if (editorFont.pixelSize() > 0) {
        fontSizeRule = QStringLiteral("font-size: %1px;").arg(editorFont.pixelSize());
    } else if (editorFont.pointSizeF() > 0) {
        fontSizeRule =
            QStringLiteral("font-size: %1pt;").arg(editorFont.pointSizeF(), 0, 'f', 1);
    }
    if (option && option->rect.height() > 0) {
        heightRule = QStringLiteral("min-height: 0px; max-height: %1px;")
                         .arg(option->rect.height());
    }

    editor->setFrame(false);
    editor->setContentsMargins(0, 0, 0, 0);
    editor->setTextMargins(8, 0, 8, 0);
    editor->setAutoFillBackground(false);
    editor->setAlignment(editorAlignmentFor(index));
    editor->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    editor->setMinimumSize(0, 0);
    if (option) {
        editor->setFixedHeight(option->rect.height());
    }
    if (option) {
        editor->setFont(editorFont);
        editor->setPalette(option->palette);
    }
    const QPalette palette = option ? option->palette : QApplication::palette();
    const xEditorTheme theme = editorThemeFromPalette(palette);
    const QString styleSheet =
        QStringLiteral(
            "QLineEdit {"
            "background: %1;"
            "color: %2;"
            "border: none;"
            "margin: 0px;"
            "padding: 0px;"
            "%3"
            "%4"
            "selection-background-color: %5;"
            "selection-color: %6;"
            "}"
            "QLineEdit:focus {"
            "background: %1;"
            "color: %2;"
            "border: none;"
            "outline: none;"
            "}")
            .arg(theme.background,
                 theme.textColor,
                 fontSizeRule,
                 heightRule,
                 theme.selectionBackground,
                 theme.selectionText);
    if (editor->styleSheet() != styleSheet) {
        editor->setStyleSheet(styleSheet);
    }
}

QWidget *xItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                                     const QModelIndex &idx) const {
    if (idx.data(xTableView::StringListEditRole).toBool()) {
        QVariant factoryData = idx.data(xTableView::StringListDialogFactoryRole);
        if (factoryData.isValid()) {
            auto factory = factoryData.value<xTableView::StringListDialogFactory>();
            if (factory) {
                QPointer<QAbstractItemModel> model =
                    const_cast<QAbstractItemModel*>(idx.model());
                QPersistentModelIndex persistentIndex(idx);
                auto commitSelection = [model, persistentIndex](const QStringList& selection) {
                    if (model && persistentIndex.isValid()) {
                        model->setData(persistentIndex, selection, Qt::EditRole);
                    }
                };
                auto *editor = new xTableStringListEditor(factory, parent, commitSelection);
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
    if (v.userType() == qMetaTypeId<zce::Any>()) {
        zce::Any a = v.value<zce::Any>();
        if (a.is_double() || a.is_i64()) {
            // 如果是浮点数或整数，创建 QLineEdit 进行编辑
            QLineEdit *e = new QLineEdit(parent);
            polishLineEditEditor(e, &opt, idx);
            return e;
        }
        if (a.is_boolean()) {
            // 如果是布尔类型，创建 QCheckBox
            QCheckBox *e = new QCheckBox(parent);
            return e;
        }
        if (a.is_string()) {
            // 如果是字符串，创建 QLineEdit
            QLineEdit *e = new QLineEdit(parent);
            polishLineEditEditor(e, &opt, idx);
            return e;
        }
        if (a.is_vector() || a.is_dict()) {
            // 如果是 vector 或 dict，创建一个文本编辑器或自定义编辑器
            QLineEdit *e = new QLineEdit(parent);
            polishLineEditEditor(e, &opt, idx);
            return e;  // 可根据需求使用更复杂的编辑器
        }
        {
            QLineEdit *e = new QLineEdit(parent);
            polishLineEditEditor(e, &opt, idx);
            return e;
        }
    } else {
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
                polishLineEditEditor(e, &opt, idx);

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
                polishLineEditEditor(e, &opt, idx);
                return e;
            }
        }
    }
}

void xItemDelegate::setEditorData(QWidget *editor, const QModelIndex &idx) const {
    if (auto *stringListEditor = qobject_cast<xTableStringListEditor *>(editor)) {
        QVariant data = idx.data(Qt::EditRole);
        if (data.canConvert<QStringList>() && data.userType() != QMetaType::QString) {
            stringListEditor->setStringList(data.toStringList());
        } else {
            stringListEditor->setText(data.toString());
        }
        return;
    }
    if (auto *cb = qobject_cast<QComboBox *>(editor)) {
        cb->setCurrentText(idx.data(Qt::DisplayRole).toString());
        return;
    }

    QVariant v = idx.data(Qt::EditRole);
    if (v.userType() == qMetaTypeId<zce::Any>()) {
        zce::Any a = v.value<zce::Any>();
        if (a.is_double()) {
            qobject_cast<QLineEdit *>(editor)->setText(QString::number(a.dbl()));
        } else if (a.is_i64()) {
            qobject_cast<QLineEdit *>(editor)->setText(QString::number(a.i64()));
        } else if (a.is_boolean()) {
            qobject_cast<QCheckBox *>(editor)->setChecked(a.boolean());
        } else if (a.is_string()) {
            qobject_cast<QLineEdit *>(editor)->setText(QString::fromStdString(a.str()));
        } else if (a.is_vector() || a.is_dict()) {
            qobject_cast<QLineEdit *>(editor)->setText(QString::fromStdString(a.toJsonString()));
        } else {
            QStyledItemDelegate::setEditorData(editor, idx);
        }
    } else {
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
                // QStyledItemDelegate::setEditorData(editor, idx);
                break;
        }
    }
}

void xItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                 const QModelIndex &idx) const {
    QVariant out;
    QVariant originalData = idx.data(Qt::EditRole);

    if (originalData.userType() == qMetaTypeId<zce::Any>()) {
        zce::Any newVal;
        if (auto e = qobject_cast<QDoubleSpinBox *>(editor))
            newVal = zce::Any(e->value());
        else if (auto e = qobject_cast<QSpinBox *>(editor))
            newVal = zce::Any((int64_t)e->value());
        else if (auto e = qobject_cast<QCheckBox *>(editor))
            newVal = zce::Any(e->isChecked());
        else if (auto e = qobject_cast<QLineEdit *>(editor))
            newVal = zce::Any::fromJsonString(e->text().toStdString());
        out = QVariant::fromValue(newVal);
        //model->setData(idx, out, Qt::UserRole);
        model->setData(idx, xTableView::anyToString(newVal), Qt::DisplayRole);
        model->setData(idx, out, Qt::EditRole);
    } else {
        switch (originalData.typeId()) {
            case QMetaType::Bool: {
                QCheckBox *chk = editor->findChild<QCheckBox *>();
                if (chk) {
                    out = chk->isChecked();
                }
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
            model->setData(idx, out, Qt::EditRole);
        }
    }
}

void xItemDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                                         const QModelIndex &idx) const {
    if (!editor) return;

    const QRect editorRect = option.rect;
    editor->setContentsMargins(0, 0, 0, 0);
    editor->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    editor->setMinimumSize(0, 0);
    editor->setFont(option.font);
    editor->setGeometry(editorRect);
    editor->setFixedHeight(editorRect.height());
    if (auto *layout = editor->layout()) {
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }

    if (auto *stringListEditor = qobject_cast<xTableStringListEditor *>(editor)) {
        stringListEditor->applyTheme(option.palette);
    }

    if (auto *lineEdit = editorLineEdit(editor)) {
        polishLineEditEditor(lineEdit, &option, idx);
    }
}

QString xItemDelegate::displayText(const QVariant &value, const QLocale &locale) const {
    if (value.userType() == qMetaTypeId<zce::Any>()) {
        zce::Any a = value.value<zce::Any>();
        return QString::fromStdString(a.toJsonString());
    } else {
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
}

void xItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const {
    QStyleOptionViewItem option = opt;
    initStyleOption(&option, idx);
    QVariant data = idx.data(Qt::EditRole);

    // Handle bool type specially
    if (data.typeId() == QMetaType::Bool) {
        QStyleOptionButton checkOpt;
        checkOpt.state |= QStyle::State_Enabled;
        checkOpt.state |= (data.toBool() ? QStyle::State_On : QStyle::State_Off);
        checkOpt.palette = option.palette;
        if (option.state & QStyle::State_HasFocus) checkOpt.state |= QStyle::State_HasFocus;
        int checkboxSize = qMax(18, static_cast<int>(option.rect.height() * 0.8));
        checkOpt.rect = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter,
                                            QSize(checkboxSize, checkboxSize), option.rect);
        if (option.state & QStyle::State_Selected) {
            p->fillRect(option.rect, option.palette.highlight());
        }

        QApplication::style()->drawControl(QStyle::CE_CheckBox, &checkOpt, p);
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

bool xItemDelegate::eventFilter(QObject *object, QEvent *event) {
    // view 打开编辑器时会把本代理安装为编辑器的事件过滤器，这里统一接管 Ctrl+V：
    // 下拉框 / 复选框自身不处理粘贴，若放任事件冒泡，表格会直接改写单元格，
    // 随后编辑器关闭提交旧值，用户看到的就是"粘贴没反应"。
    if (event && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Paste)) {
            if (pasteIntoEditor(qobject_cast<QWidget *>(object))) {
                return true;
            }
        }
    }
    return QStyledItemDelegate::eventFilter(object, event);
}

void xItemDelegate::commitAndCloseEditor() {
    auto *editor = qobject_cast<QWidget *>(sender());
    emit commitData(editor);
    emit closeEditor(editor);
}
