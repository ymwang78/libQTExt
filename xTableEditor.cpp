// ***************************************************************
//  xTableEditor   version:  1.0   -  date:  2025/07/05
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************
#include "xTableEditor.h"
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPalette>
#include <QPointer>
#include <QSizePolicy>
#include <QToolButton>

static QStringList splitStringListEditorTextBy(const QString& text, QChar separator) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    return text.split(separator, Qt::SkipEmptyParts);
#else
    return text.split(separator, QString::SkipEmptyParts);
#endif
}

// 直接输入使用 ',' 和 ';' 作为分隔符；弹窗结果以 current_list_ 保留精确值。
static QStringList splitStringListEditorText(const QString& text) {
    QStringList values;
    const QString raw = text.trimmed();
    if (raw.isEmpty()) {
        return values;
    }

    if (raw.contains(';')) {
        values = splitStringListEditorTextBy(raw, ';');
    } else if (raw.contains(',')) {
        values = splitStringListEditorTextBy(raw, ',');
    } else {
        values = {raw};
    }

    for (auto& value : values) {
        value = value.trimmed();
    }
    values.removeAll(QString());
    return values;
}

// 构造函数接收工厂函数
xTableStringListEditor::xTableStringListEditor(
    xTableView::StringListDialogFactory factory,
    QWidget* parent,
    std::function<void(const QStringList&)> commit_callback)
    : QWidget(parent),
      dialog_factory_(std::move(factory)),
      commit_callback_(std::move(commit_callback)) {
    setObjectName(QStringLiteral("xTableStringListEditor"));
    setProperty("xTableStringListEditor", true);
    setAutoFillBackground(true);

    line_edit_ = new QLineEdit(this);
    line_edit_->setProperty("xTableStringListEditor.childLineEdit", true);
    line_edit_->setFrame(false);  // 保持文本框无边框，与单元格融为一体
    line_edit_->setReadOnly(false);
    line_edit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    button_ = new QToolButton(this);
    button_->setText(QStringLiteral("..."));
    button_->setAutoRaise(true);
    button_->setFixedWidth(24);
    button_->setMinimumHeight(0);
    button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    button_->setCursor(Qt::ArrowCursor);
    // 保持焦点在 line edit，避免按钮按下/释放触发 editingFinished。
    button_->setFocusPolicy(Qt::NoFocus);
    applyTheme(palette());

    auto* layout = new QHBoxLayout(this);
    // 3. 边距设置为0，让编辑器能完全填满单元格，看起来更原生
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(line_edit_, 1);
    layout->addWidget(button_, 0);
    setLayout(layout);

    setFocusProxy(line_edit_);
    connect(line_edit_, &QLineEdit::textEdited, this, &xTableStringListEditor::onTextEdited);
    connect(line_edit_, &QLineEdit::editingFinished, this, [this]() {
        if (dialog_open_) {
            return;
        }
        current_list_ = splitStringListEditorText(line_edit_->text());
        emit editingFinished();
    });
    connect(button_, &QToolButton::pressed, this, [this]() {
        dialog_open_ = true;
    });
    connect(button_, &QToolButton::released, this, [this]() {
        if (!button_->underMouse()) {
            dialog_open_ = false;
        }
    });
    connect(button_, &QToolButton::clicked, this, &xTableStringListEditor::onButtonClicked);
}

void xTableStringListEditor::setStringList(const QStringList& list) {
    current_list_ = list;
    line_edit_->setText(current_list_.join(", "));
}

void xTableStringListEditor::setText(const QString& text) {
    line_edit_->setText(text);
    current_list_ = splitStringListEditorText(text);
}

QStringList xTableStringListEditor::getStringList() const {
    return current_list_;
}

void xTableStringListEditor::applyTheme(const QPalette& palette) {
    setPalette(palette);
    if (line_edit_) {
        line_edit_->setPalette(palette);
    }
    if (button_) {
        button_->setPalette(palette);
    }

    const xEditorTheme theme = editorThemeFromPalette(palette);
    setStyleSheet(QStringLiteral(
        "QWidget#xTableStringListEditor {"
        "background: %1;"
        "border: none;"
        "margin: 0px;"
        "padding: 0px;"
        "}"
        "QLineEdit {"
        "background: %1;"
        "color: %2;"
        "selection-background-color: %3;"
        "selection-color: %4;"
        "}").arg(theme.background,
                 theme.textColor,
                 theme.selectionBackground,
                 theme.selectionText));

    button_->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "background: %1;"
        "color: %2;"
        "border: 1px solid %3;"
        "border-left-color: %3;"
        "border-radius: 0px;"
        "padding: 0px;"
        "font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "background: %4;"
        "border-color: %5;"
        "color: %2;"
        "}"
        "QToolButton:pressed {"
        "background: %6;"
        "border-color: %7;"
        "}").arg(theme.buttonBackground,
                 theme.buttonText,
                 theme.buttonBorder,
                 theme.buttonHoverBackground,
                 theme.buttonHoverBorder,
                 theme.buttonPressedBackground,
                 theme.buttonPressedBorder));
}

void xTableStringListEditor::onTextEdited(const QString& text) {
    current_list_ = splitStringListEditorText(text);
}

void xTableStringListEditor::onButtonClicked() {

    if (dialog_factory_) {
        // 调用工厂函数，它会创建、执行对话框并返回结果
        QPointer<xTableStringListEditor> guard(this);
        dialog_open_ = true;
        auto commitCallback = commit_callback_;
        std::optional<QStringList> result = dialog_factory_(this, current_list_);
        if (!guard) {
            if (result.has_value() && commitCallback) {
                commitCallback(result.value());
            }
            return;
        }
        dialog_open_ = false;

        // 如果用户点击了“OK”并且有返回结果
        if (result.has_value()) {
            setStringList(result.value());
            emit editingFinished();  // 发射信号
        }
        // 如果用户点击了“Cancel”，result 为 std::nullopt，我们什么都不做
    } else {
        dialog_open_ = false;
    }
}
