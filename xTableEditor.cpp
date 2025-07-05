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
#include <QPushButton>

// 构造函数接收工厂函数
xTableStringListEditor::xTableStringListEditor(xTableView::StringListDialogFactory factory, QWidget* parent)
    : QWidget(parent), dialogFactory_(std::move(factory)) {
    lineEdit_ = new QLineEdit(this);
    lineEdit_->setFrame(false);  // 保持文本框无边框，与单元格融为一体
    lineEdit_->setReadOnly(true);
    lineEdit_->setAlignment(Qt::AlignVCenter);

    button_ = new QPushButton("...", this);

    button_->setFixedSize(22, 18);
    button_->setCursor(Qt::ArrowCursor);

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
    layout->addWidget(lineEdit_);
    layout->addWidget(button_);  // 不再需要特殊对齐，布局会自动处理
    setLayout(layout);

    setFocusProxy(button_);
    connect(button_, &QPushButton::clicked, this, &xTableStringListEditor::onButtonClicked);
}

void xTableStringListEditor::setStringList(const QStringList& list) {
    currentList_ = list;
    lineEdit_->setText(currentList_.join(", "));
}

QStringList xTableStringListEditor::getStringList() const { return currentList_; }

void xTableStringListEditor::onButtonClicked() {

    if (dialogFactory_) {
        // 调用工厂函数，它会创建、执行对话框并返回结果
        std::optional<QStringList> result = dialogFactory_(this, currentList_);

        // 如果用户点击了“OK”并且有返回结果
        if (result.has_value()) {
            setStringList(result.value());
            emit editingFinished();  // 发射信号
        }
        // 如果用户点击了“Cancel”，result 为 std::nullopt，我们什么都不做
    }
}
