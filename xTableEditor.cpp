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
#include <QPointer>
#include <QPushButton>

static QStringList splitStringListEditorText(const QString& text) {
    QStringList values;
    const QString raw = text.trimmed();
    if (raw.isEmpty()) {
        return values;
    }

    if (raw.contains(';')) {
        values = raw.split(';', Qt::SkipEmptyParts);
    } else if (raw.contains(',')) {
        values = raw.split(',', Qt::SkipEmptyParts);
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
    std::function<void(const QStringList&)> commit_callback,
    QWidget* parent)
    : QWidget(parent),
      dialog_factory_(std::move(factory)),
      commit_callback_(std::move(commit_callback)) {
    setObjectName(QStringLiteral("xTableStringListEditor"));
    setProperty("xTableStringListEditor", true);
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral(
        "QWidget#xTableStringListEditor {"
        "background: white;"
        "border: none;"
        "margin: 0px;"
        "padding: 0px;"
        "}"));

    line_edit_ = new QLineEdit(this);
    line_edit_->setProperty("xTableStringListEditor.childLineEdit", true);
    line_edit_->setFrame(false);  // 保持文本框无边框，与单元格融为一体
    line_edit_->setReadOnly(false);
    line_edit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    line_edit_->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "background: white;"
        "border: none;"
        "margin: 0px;"
        "padding: 0px 8px;"
        "selection-background-color: #1344B1;"
        "selection-color: white;"
        "}"
        "QLineEdit:focus {"
        "background: white;"
        "border: none;"
        "outline: none;"
        "}"));

    button_ = new QPushButton("...", this);

    button_->setFixedSize(22, 18);
    button_->setCursor(Qt::ArrowCursor);
    button_->setFocusPolicy(Qt::NoFocus);

    // --- 核心修改：使用详细的样式表来定义按钮外观 ---
    button_->setStyleSheet(
        "QPushButton {"
        "    background-color: #888888;"
        "    color: white;"
        "    border: 1px solid #555555;"
        "    padding: 0px;"
        "    border-radius: 3px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background-color: #999999;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #777777;"
        "    border-style: inset;"
        "}"
    );

    auto* layout = new QHBoxLayout(this);
    // 3. 边距设置为0，让编辑器能完全填满单元格，看起来更原生
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);  // 在文本框和按钮之间留出1像素的微小间距
    layout->addWidget(line_edit_);
    layout->addWidget(button_);  // 不再需要特殊对齐，布局会自动处理
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
    connect(button_, &QPushButton::pressed, this, [this]() {
        dialog_open_ = true;
    });
    connect(button_, &QPushButton::released, this, [this]() {
        if (!button_->underMouse()) {
            dialog_open_ = false;
        }
    });
    connect(button_, &QPushButton::clicked, this, &xTableStringListEditor::onButtonClicked);
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
    return splitStringListEditorText(line_edit_->text());
}

void xTableStringListEditor::onTextEdited(const QString& text) {
    current_list_ = splitStringListEditorText(text);
}

void xTableStringListEditor::onButtonClicked() {

    if (dialog_factory_) {
        // 调用工厂函数，它会创建、执行对话框并返回结果
        current_list_ = splitStringListEditorText(line_edit_->text());
        QPointer<xTableStringListEditor> guard(this);
        dialog_open_ = true;
        auto commitCallback = commit_callback_;
        std::optional<QStringList> result = dialog_factory_(this, current_list_);
        if (result.has_value() && commitCallback) {
            commitCallback(result.value());
        }
        if (!guard) {
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
