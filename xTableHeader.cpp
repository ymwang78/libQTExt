// ***************************************************************
//  xTableHeader   version:  1.0   -  date:  2025/07/06
//  -------------------------------------------------------------
//  Yongming Wang(wangym@gmail.com)
//  -------------------------------------------------------------
//  This file is a part of project libQTExt.
//  Copyright (C) 2025 - All Rights Reserved
// ***************************************************************
// 
// ***************************************************************
#include "xTableHeader.h"
#include <QApplication>
#include <QCursor>
#include <QStyleOptionHeader>
#include <QtGlobal>

namespace {

void elideHeaderOptionRight(QStyleOptionHeader *option, int availWidth) {
    if (!option || option->text.isEmpty()) {
        return;
    }
    const int textWidth = option->fontMetrics.horizontalAdvance(option->text);
    if (textWidth <= availWidth) {
        return;
    }
    const Qt::Alignment verticalAlign =
        (option->textAlignment & Qt::AlignVertical_Mask)
            ? (option->textAlignment & Qt::AlignVertical_Mask)
            : Qt::AlignVCenter;
    option->textAlignment = Qt::AlignLeft | verticalAlign;
    if (availWidth > 0) {
        const QString elided =
            option->fontMetrics.elidedText(option->text, Qt::ElideRight, availWidth);
        if (!elided.isEmpty() && elided.at(0) == option->text.at(0)) {
            option->text = elided;
        }
    }
}

}  // namespace

xCheckableHeaderView::xCheckableHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent) {
    // 开启 setSectionsClickable 以便接收 sectionClicked 信号，从而不影响原有的排序功能
    setSectionsClickable(true);
}

void xCheckableHeaderView::setBoolColumn(int column, bool isBool) {
    if (isBool) {
        bool_columns_.insert(column);
    } else {
        bool_columns_.remove(column);
        check_states_.remove(column);
    }
    // 触发重绘
    updateSection(column);
}

void xCheckableHeaderView::setCheckState(int column, Qt::CheckState state) {
    if (check_states_.value(column, Qt::Unchecked) != state) {
        check_states_[column] = state;
        // 触发该节的重绘
        updateSection(column);
    }
}

void xCheckableHeaderView::paintSection(QPainter *painter, const QRect &rect,
                                        int logicalIndex) const {
    if (!rect.isValid()) {
        return;
    }

    if (orientation() != Qt::Horizontal) {
        painter->save();
        QHeaderView::paintSection(painter, rect, logicalIndex);
        painter->restore();
        return;
    }

    QStyleOptionHeaderV2 headerOpt;
    initStyleOption(&headerOpt);
    QStyle::State extraState = QStyle::State_None;
    if (isEnabled()) {
        extraState |= QStyle::State_Enabled;
    }
    if (window() && window()->isActiveWindow()) {
        extraState |= QStyle::State_Active;
    }
    if (sectionsClickable() && isEnabled() && viewport()) {
        const QPoint cursorPos = viewport()->mapFromGlobal(QCursor::pos());
        if (rect.contains(cursorPos)) {
            extraState |= QStyle::State_MouseOver;
            if (QApplication::mouseButtons() & Qt::LeftButton) {
                extraState |= QStyle::State_Sunken;
            }
        }
    }
    headerOpt.rect = rect;
    QHeaderView::initStyleOptionForIndex(&headerOpt, logicalIndex);
    headerOpt.state |= extraState;

    QRect labelRect = style()->subElementRect(QStyle::SE_HeaderLabel, &headerOpt, this);
    if (!labelRect.isValid()) {
        const int margin = qMax(2, style()->pixelMetric(QStyle::PM_HeaderMargin, &headerOpt, this));
        labelRect = rect.adjusted(margin, 0, -margin, 0);
    }

    if (!bool_columns_.contains(logicalIndex)) {
        elideHeaderOptionRight(&headerOpt, labelRect.width());
        painter->save();
        style()->drawControl(QStyle::CE_Header, &headerOpt, painter, this);
        painter->restore();
        return;
    }

    // 布尔列左侧带有复选框：当列较窄导致居中文本与复选框重叠或超出列宽时，在复选框右侧区域左对齐截断绘制
    const QRect cbRect = checkBoxRect(rect);
    const int minTextLeft = cbRect.right() + 4;
    const int textWidth = headerOpt.fontMetrics.horizontalAdvance(headerOpt.text);
    const int centeredLeft = labelRect.left() + (labelRect.width() - textWidth) / 2;

    painter->save();
    if (!headerOpt.text.isEmpty() &&
        (centeredLeft < minTextLeft || textWidth > labelRect.width())) {
        QStyleOptionHeaderV2 sectionOpt = headerOpt;
        sectionOpt.text.clear();
        sectionOpt.icon = QIcon();
        style()->drawControl(QStyle::CE_HeaderSection, &sectionOpt, painter, this);
        if (headerOpt.sortIndicator != QStyleOptionHeader::None) {
            QStyleOptionHeaderV2 arrowOpt = headerOpt;
            arrowOpt.rect = style()->subElementRect(QStyle::SE_HeaderArrow, &headerOpt, this);
            if (arrowOpt.rect.isValid()) {
                style()->drawPrimitive(QStyle::PE_IndicatorHeaderArrow, &arrowOpt, painter, this);
            }
        }
        QRect availRect = labelRect;
        if (availRect.left() < minTextLeft) {
            availRect.setLeft(minTextLeft);
        }
        if (availRect.width() > 0) {
            QStyleOptionHeaderV2 labelOpt = headerOpt;
            labelOpt.rect = availRect;
            labelOpt.textAlignment = Qt::AlignLeft | Qt::AlignVCenter;
            elideHeaderOptionRight(&labelOpt, availRect.width());
            painter->setClipRect(availRect);
            style()->drawControl(QStyle::CE_HeaderLabel, &labelOpt, painter, this);
        }
    } else {
        style()->drawControl(QStyle::CE_Header, &headerOpt, painter, this);
    }
    painter->restore();

    // --- 开始绘制自定义内容 ---

    // 准备绘制复选框
    QStyleOptionButton option;
    option.rect = checkBoxRect(rect);  // 获取复选框的位置
    option.state = QStyle::State_Enabled | QStyle::State_Raised;

    // 根据存储的状态设置复选框是选中、未选中还是部分选中
    switch (check_states_.value(logicalIndex, Qt::Unchecked)) {
        case Qt::Checked:
            option.state |= QStyle::State_On;
            break;
        case Qt::Unchecked:
            option.state |= QStyle::State_Off;
            break;
        case Qt::PartiallyChecked:
            // 在Qt中，部分选中状态通常用 State_NoChange 配合特定样式实现
            option.state |= QStyle::State_NoChange;
            break;
    }

    // 使用当前应用的样式来绘制复选框控件
    style()->drawControl(QStyle::CE_CheckBox, &option, painter);
}

void xCheckableHeaderView::mousePressEvent(QMouseEvent *event) {
    // 获取被点击的列索引
    int logicalIndex = logicalIndexAt(event->pos());

    if (logicalIndex != -1 && bool_columns_.contains(logicalIndex)) {
        // --- 核心修改：手动构建 section 的矩形区域 ---
        QRect section_rect;
        if (orientation() == Qt::Horizontal) {
            // 对于水平表头，矩形是 (x, 0, width, height)
            section_rect =
                QRect(sectionPosition(logicalIndex), 0, sectionSize(logicalIndex), height());
        } else {
            // 对于垂直表头，矩形是 (0, y, width, height)
            section_rect =
                QRect(0, sectionPosition(logicalIndex), width(), sectionSize(logicalIndex));
        }

        // 现在用我们手动创建的 section_rect 来计算复选框的位置并进行判断
        if (checkBoxRect(section_rect).contains(event->pos())) {
            // 状态切换逻辑 (保持不变)
            Qt::CheckState currentState = check_states_.value(logicalIndex, Qt::Unchecked);
            Qt::CheckState newState;

            if (currentState == Qt::Unchecked || currentState == Qt::PartiallyChecked) {
                newState = Qt::Checked;
            } else {
                newState = Qt::Unchecked;
            }

            setCheckState(logicalIndex, newState);
            emit checkboxToggled(logicalIndex, newState);

            return;  // 事件已处理
        }
    }

    // 如果没点在我们的复选框上，则调用基类方法，保证排序等功能正常
    QHeaderView::mousePressEvent(event);
}

QRect xCheckableHeaderView::checkBoxRect(const QRect &sourceRect) const {
    // 计算复选框的矩形区域，使其在表头节内垂直居中，并靠左显示
    QStyleOptionButton opt;
    QRect cbRect = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt);
    int y = sourceRect.top() + (sourceRect.height() - cbRect.height()) / 2;
    // 留出一些边距
    return QRect(sourceRect.left() + 5, y, cbRect.width(), cbRect.height());
}