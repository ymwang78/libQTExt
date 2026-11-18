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
#include <QStyleOptionHeader>
#include <QtGlobal>

namespace {

// Qt 6 的 QHeaderView::initialize()（qheaderview.cpp:336，基于 Qt 6.10.3 验证）会显式把
// d->textElideMode 重置为 Qt::ElideNone（覆盖 QAbstractItemView 默认的 Qt::ElideRight），导致
// QCommonStyle::CE_HeaderLabel 不做省略、直接按 Qt::AlignCenter 绘制完整文本并向两侧同时裁剪
// （窄列只露出中间字符，如 "g High Li"）。
// 这里不在 initStyleOptionForIndex() 中无条件改为 AlignLeft，原因有三：
// 1. 列宽充足时必须保留调用方配置的居中/右对齐（initStyleOptionForIndex 调用时 opt.rect
// 尚未写入）；
// 2. 极窄列下 QFontMetrics::elidedText(..., Qt::ElideRight) 若连首字符都放不下会返回空串或单独的
//    "…"，此时回退到左对齐硬裁剪（textElideMode = ElideNone）仍能保留首字符；
// 3. 布尔列（bool_columns_）在窄列下还需把文本区左边界推到复选框右侧，避免文字画到复选框下方。

int headerIconReserveWidth(const QStyle* style, const QStyleOptionHeader* option,
                           const QPainter* painter, const QWidget* widget) {
    if (!style || !option || option->icon.isNull()) {
        return 0;
    }
    const int iconExtent = style->pixelMetric(QStyle::PM_SmallIconSize, option, widget);
    const qreal dpr = (painter && painter->device()) ? painter->device()->devicePixelRatio()
                                                     : (widget ? widget->devicePixelRatio() : 1.0);
    const QPixmap pixmap = option->icon.pixmap(
        QSize(iconExtent, iconExtent), dpr,
        (option->state & QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled);
    if (pixmap.isNull()) {
        return 0;
    }
    const qreal pmDpr = pixmap.devicePixelRatio() > 0.0 ? pixmap.devicePixelRatio() : 1.0;
    const int pixw = static_cast<int>(pixmap.width() / pmDpr);
    const int margin = style->pixelMetric(QStyle::PM_HeaderMargin, option, widget);
    return pixw + margin;
}

QFontMetrics effectiveHeaderFontMetrics(const QStyleOptionHeader* option, const QPainter* painter) {
    if (option && (option->state & QStyle::State_On) && painter) {
        QFont fnt = painter->font();
        if (!(fnt.resolveMask() & QFont::WeightResolved)) {
            fnt.setBold(true);
            return QFontMetrics(fnt);
        }
    }
    return option ? option->fontMetrics : QFontMetrics(QFont());
}

void elideHeaderOptionRight(QStyleOptionHeaderV2* option, const QFontMetrics& fm, int availWidth) {
    if (!option || option->text.isEmpty()) {
        return;
    }
    const int textWidth = fm.horizontalAdvance(option->text);
    if (textWidth <= availWidth) {
        return;
    }
    const Qt::Alignment verticalAlign = (option->textAlignment & Qt::AlignVertical_Mask)
                                            ? (option->textAlignment & Qt::AlignVertical_Mask)
                                            : Qt::AlignVCenter;
    option->textAlignment = Qt::AlignLeft | verticalAlign;
    option->textElideMode = Qt::ElideNone;
    if (availWidth > 0) {
        const QString elided = fm.elidedText(option->text, Qt::ElideRight, availWidth);
        const bool onlyEllipsis =
            (elided == QStringView(u"\u2026") || elided == QStringView(u"...") ||
             elided == QStringView(u"\u200d\u2026") || elided == QStringView(u"\u200d..."));
        if (!elided.isEmpty() && !onlyEllipsis && elided.at(0) == option->text.at(0)) {
            option->text = elided;
        }
    }
}

}  // namespace

xCheckableHeaderView::xCheckableHeaderView(Qt::Orientation orientation, QWidget* parent)
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

void xCheckableHeaderView::paintSection(QPainter* painter, const QRect& rect,
                                        int logicalIndex) const {
    if (!rect.isValid()) {
        return;
    }

    if (orientation() != Qt::Horizontal) {
        painter->save();
        QHeaderView::paintSection(painter, rect, logicalIndex);
        painter->restore();
        if (bool_columns_.contains(logicalIndex)) {
            paintCheckBox(painter, rect, logicalIndex);
        }
        return;
    }

    QStyleOptionHeaderV2 headerOpt;
    initStyleOption(&headerOpt);

    const QBrush oBrushButton = headerOpt.palette.brush(QPalette::Button);
    const QBrush oBrushWindow = headerOpt.palette.brush(QPalette::Window);

    initStyleOptionForIndex(&headerOpt, logicalIndex);
    // 与 QHeaderView::paintSection 保持一致：在 initStyleOptionForIndex 之后设置节矩形。
    headerOpt.rect = rect;

    const QBrush nBrushButton = headerOpt.palette.brush(QPalette::Button);
    const QBrush nBrushWindow = headerOpt.palette.brush(QPalette::Window);

    const QRect labelRect = style()->subElementRect(QStyle::SE_HeaderLabel, &headerOpt, this);
    const int iconReserve = headerIconReserveWidth(style(), &headerOpt, painter, this);
    const QFontMetrics fm = effectiveHeaderFontMetrics(&headerOpt, painter);

    if (!bool_columns_.contains(logicalIndex)) {
        if (labelRect.isValid()) {
            elideHeaderOptionRight(&headerOpt, fm, labelRect.width() - iconReserve);
        }
        painter->save();
        if (oBrushButton != nBrushButton || oBrushWindow != nBrushWindow) {
            painter->setBrushOrigin(headerOpt.rect.topLeft());
        }
        style()->drawControl(QStyle::CE_Header, &headerOpt, painter, this);
        painter->restore();
        return;
    }

    // 布尔列左侧带有复选框：当列较窄导致居中内容与复选框重叠或超出列宽时，在复选框右侧区域左对齐截断绘制
    const QRect cbRect = checkBoxRect(rect);
    const int minTextLeft = cbRect.right() + 4;
    const int textWidth = fm.horizontalAdvance(headerOpt.text);
    const int labelAvailWidth = labelRect.isValid() ? (labelRect.width() - iconReserve) : 0;
    const int centeredLeft = labelRect.left() + iconReserve + (labelAvailWidth - textWidth) / 2;
    const bool iconOverlapsCheckbox =
        labelRect.isValid() && !headerOpt.icon.isNull() && labelRect.left() < minTextLeft;
    const bool textNeedsShiftOrElide =
        !headerOpt.text.isEmpty() &&
        (!labelRect.isValid() || (headerOpt.icon.isNull() && centeredLeft < minTextLeft) ||
         textWidth > labelAvailWidth);

    painter->save();
    if (oBrushButton != nBrushButton || oBrushWindow != nBrushWindow) {
        painter->setBrushOrigin(headerOpt.rect.topLeft());
    }
    if (iconOverlapsCheckbox || textNeedsShiftOrElide) {
        painter->setClipRect(rect, Qt::IntersectClip);
        QStyleOptionHeaderV2 sectionOpt = headerOpt;
        sectionOpt.text.clear();
        sectionOpt.icon = QIcon();
        style()->drawControl(QStyle::CE_HeaderSection, &sectionOpt, painter, this);
        if (labelRect.isValid()) {
            QRect availRect = labelRect;
            if (availRect.left() < minTextLeft) {
                availRect.setLeft(minTextLeft);
            }
            if (availRect.isValid()) {
                QStyleOptionHeaderV2 labelOpt = headerOpt;
                labelOpt.rect = availRect;
                if (headerOpt.icon.isNull() && centeredLeft < minTextLeft) {
                    const Qt::Alignment verticalAlign =
                        (labelOpt.textAlignment & Qt::AlignVertical_Mask)
                            ? (labelOpt.textAlignment & Qt::AlignVertical_Mask)
                            : Qt::AlignVCenter;
                    labelOpt.textAlignment = Qt::AlignLeft | verticalAlign;
                    labelOpt.textElideMode = Qt::ElideNone;
                }
                elideHeaderOptionRight(&labelOpt, fm, availRect.width() - iconReserve);
                painter->save();
                painter->setClipRect(availRect, Qt::IntersectClip);
                style()->drawControl(QStyle::CE_HeaderLabel, &labelOpt, painter, this);
                painter->restore();
            }
        }
        if (headerOpt.sortIndicator != QStyleOptionHeader::None) {
            QStyleOptionHeaderV2 arrowOpt = headerOpt;
            arrowOpt.rect = style()->subElementRect(QStyle::SE_HeaderArrow, &headerOpt, this);
            if (arrowOpt.rect.isValid()) {
                style()->drawPrimitive(QStyle::PE_IndicatorHeaderArrow, &arrowOpt, painter, this);
            }
        }
    } else {
        style()->drawControl(QStyle::CE_Header, &headerOpt, painter, this);
    }
    painter->restore();

    paintCheckBox(painter, rect, logicalIndex);
}

void xCheckableHeaderView::paintCheckBox(QPainter* painter, const QRect& rect,
                                         int logicalIndex) const {
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

void xCheckableHeaderView::mousePressEvent(QMouseEvent* event) {
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

QRect xCheckableHeaderView::checkBoxRect(const QRect& sourceRect) const {
    // 计算复选框的矩形区域，使其在表头节内垂直居中，并靠左显示
    QStyleOptionButton opt;
    QRect cbRect = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt);
    int y = sourceRect.top() + (sourceRect.height() - cbRect.height()) / 2;
    // 留出一些边距
    return QRect(sourceRect.left() + 5, y, cbRect.width(), cbRect.height());
}