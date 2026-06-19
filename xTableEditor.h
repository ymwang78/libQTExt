#pragma once
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

#include <QWidget>
#include <QStringList>
#include <QDialog>
#include <QPalette>
#include <functional>
#include "xTableView.h"

class QLineEdit;
class QToolButton;

struct xEditorTheme {
    bool darkMode = false;
    QString background;
    QString textColor;
    QString selectionBackground;
    QString selectionText;
    QString buttonBackground;
    QString buttonText;
    QString buttonBorder;
    QString buttonHoverBackground;
    QString buttonPressedBackground;
    QString buttonHoverBorder;
    QString buttonPressedBorder;
};

inline xEditorTheme editorThemeFromPalette(const QPalette& palette) {
    const bool darkMode = palette.color(QPalette::Base).lightness() < 128 ||
                          palette.color(QPalette::Window).lightness() < 128;
    xEditorTheme theme;
    theme.darkMode = darkMode;
    theme.background = palette.color(QPalette::Base).name();
    theme.textColor = palette.color(QPalette::Text).name();
    theme.selectionBackground = palette.color(QPalette::Highlight).name();
    theme.selectionText = palette.color(QPalette::HighlightedText).name();
    theme.buttonBackground =
        darkMode ? QStringLiteral("#2d2d30") : QStringLiteral("#F7F8FA");
    theme.buttonText = darkMode ? QStringLiteral("#f0f0f0") : QStringLiteral("#4B5563");
    theme.buttonBorder = darkMode ? QStringLiteral("#4a4a4a") : QStringLiteral("#C9CDD4");
    theme.buttonHoverBackground =
        darkMode ? QStringLiteral("#3a3a3d") : QStringLiteral("#EEF3FF");
    theme.buttonPressedBackground =
        darkMode ? QStringLiteral("#454549") : QStringLiteral("#DDE7FF");
    theme.buttonHoverBorder = QStringLiteral("#8CA8E8");
    theme.buttonPressedBorder = QStringLiteral("#6F8FD8");
    return theme;
}

class xTableStringListEditor : public QWidget {
    Q_OBJECT
    QLineEdit* line_edit_ = nullptr;
    QToolButton* button_ = nullptr;
    QStringList current_list_;
    xTableView::StringListDialogFactory dialog_factory_;
    std::function<void(const QStringList&)> commit_callback_;
    bool dialog_open_ = false;

  public:
    explicit xTableStringListEditor(xTableView::StringListDialogFactory factory,
                                    QWidget* parent = nullptr,
                                    std::function<void(const QStringList&)> commit_callback = {});

    void setStringList(const QStringList& list);

    void setText(const QString& text);

    QStringList getStringList() const;

    void applyTheme(const QPalette& palette);

  signals:
    void editingFinished();

  private slots:
    void onButtonClicked();

    void onTextEdited(const QString& text);
};
