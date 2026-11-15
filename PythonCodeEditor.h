#pragma once

// ***************************************************************
//  PythonCodeEditor —— 带行号与 Python 高亮的编辑器（纯 Qt）
//  -------------------------------------------------------------
//  This file is a part of project KeelDeck.
// ***************************************************************
//
// 原来住在 applications/TaijiMPC/views/scripts/ 下。SoftSensor 的 Scripts 页
// （#253）要用同一个编辑器，而仓库约定是"插件不跨 application 目录 include"
// （#222 第 8 项）——所以把它挪到 framework/sdk/：两边都不再 include 对方的
// application 目录，也不必 fork 六百行高亮规则出来慢慢漂移。
//
// 纯 Qt、无 Q_OBJECT、无进程级状态，因此核心（TaijiMPC）与插件 DLL 各编一份进
// 自己的二进制即可，不用像 IXocTrendChart 那样走 ABI 接口，也不会给
// check_plugin_symbols.ps1 添新的依赖面。

#include <QColor>
#include <QList>
#include <QPalette>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class PythonSyntaxHighlighter;
class QEvent;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QWidget;

class PythonCodeEditor : public QPlainTextEdit {
  public:
    explicit PythonCodeEditor(QWidget* parent = nullptr);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

    /**
     * @brief 标红若干行（1 基），用于把 traceback 的出错行贴回源码上
     *
     * 与"当前行"高亮共用 extraSelections——两者分开 setExtraSelections 会互相覆盖，
     * 所以统一由 refreshExtraSelections() 一次铺完。传空清除。
     */
    void setErrorLines(const QList<int>& lines);
    const QList<int>& errorLines() const { return m_errorLines; }
    void clearErrorLines() { setErrorLines(QList<int>()); }

    /**
     * @brief 宿主脚本 API 的名字，按"API"配色高亮
     *
     * Python 关键字与内建函数是语言的，两边一样；**宿主 API 不是**——TaijiMPC 的是
     * `getMV` / `ControlStatus` 那一批，SoftSensor 的是 `sensors` / `vars` / `cycle`。
     * 挪进 framework/sdk 的时候把这张表从组件里搬了出来：留在里面，就等于让框架组件
     * 认识某一个 application 的词汇表，而另一个 application 的词一个都不认。
     * 默认为空（只高亮语言本身）。
     */
    void setApiKeywords(const QStringList& keywords);

  protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);
    void refreshExtraSelections();
    void indentSelection();
    void unindentSelection();
    QString currentLineIndent() const;

    QWidget* m_lineNumberArea;
    PythonSyntaxHighlighter* m_highlighter;
    QList<int> m_errorLines;
};

class PythonSyntaxHighlighter : public QSyntaxHighlighter {
  public:
    explicit PythonSyntaxHighlighter(QTextDocument* parent = nullptr);
    void refreshFormats(const QPalette& palette);
    /// 宿主脚本 API 的名字（见 PythonCodeEditor::setApiKeywords）
    void setApiKeywords(const QStringList& keywords);

  protected:
    void highlightBlock(const QString& text) override;

  private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QTextCharFormat format(const QColor& color, bool bold = false, bool italic = false) const;
    void addKeywordRules(const QStringList& words, const QTextCharFormat& format);
    void highlightSingleLineStringsAndComment(const QString& text);

    QVector<HighlightRule> m_rules;
    QStringList m_apiKeywords;
    QPalette m_palette;  ///< setApiKeywords 要重建规则，得记住上一次的配色来源
    QTextCharFormat m_defaultFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_tripleStringFormat;
};
