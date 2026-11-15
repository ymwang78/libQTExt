#include "PythonCodeEditor.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QTextBlock>
#include <QTextOption>
#include <QWidget>

namespace {
bool isDarkTheme(const QPalette& palette) {
    const QColor base = palette.color(QPalette::Base);
    return qGray(base.rgb()) < 128;
}

QColor blendColors(const QColor& base, const QColor& overlay, int overlayPercent) {
    const int clampedOverlay = qBound(0, overlayPercent, 100);
    const int basePercent = 100 - clampedOverlay;
    return QColor((base.red() * basePercent + overlay.red() * clampedOverlay) / 100,
                  (base.green() * basePercent + overlay.green() * clampedOverlay) / 100,
                  (base.blue() * basePercent + overlay.blue() * clampedOverlay) / 100);
}

QColor currentLineColor(const QPalette& palette, bool readOnly) {
    const QColor base = palette.color(QPalette::Base);
    if (isDarkTheme(palette)) {
        const QColor realBase = (qGray(base.rgb()) < 128) ? base : QColor(30, 30, 30);
        return readOnly ? realBase.lighter(120) : palette.color(QPalette::Highlight).darker(170);
    }

    const QColor alternate = palette.color(QPalette::AlternateBase);
    return readOnly ? blendColors(base, alternate, 55)
                    : blendColors(base, palette.color(QPalette::Highlight), 12);
}

QColor lineNumberBackgroundColor(const QPalette& palette) {
    const QColor base = palette.color(QPalette::Base);
    const QColor alternate = palette.color(QPalette::AlternateBase);
    if (isDarkTheme(palette)) {
        return (qGray(alternate.rgb()) < 128) ? alternate : QColor(37, 37, 38);
    }

    return blendColors(base, alternate, 70);
}

/// 出错行的底色：主题里的"错误"没有对应角色，按明暗各取一个够醒目又不盖住文字的红
QColor errorLineColor(const QPalette& palette) {
    const QColor base = palette.color(QPalette::Base);
    return isDarkTheme(palette) ? blendColors(base, QColor(220, 38, 38), 45)
                                : blendColors(base, QColor(248, 113, 113), 40);
}

QColor lineNumberTextColor(const QPalette& palette) {
    QColor text = palette.color(QPalette::Text);
    if (isDarkTheme(palette)) {
        if (qGray(text.rgb()) < 128) {
            text = QColor(240, 240, 240);
        }
        text.setAlpha(170);
        return text;
    }

    text.setAlpha(155);
    return text;
}

struct PythonSyntaxColors {
    QColor defaultText;
    QColor keyword;
    QColor builtin;
    QColor api;
    QColor number;
    QColor decorator;
    QColor definition;
    QColor operatorText;
    QColor comment;
    QColor string;
};

QColor readableDefaultTextColor(const QPalette& palette, bool darkTheme) {
    const QColor text = palette.color(QPalette::Text);
    if (darkTheme) {
        return qGray(text.rgb()) >= 160 ? text : QColor(229, 231, 235);
    }

    return qGray(text.rgb()) <= 96 ? text : QColor(31, 41, 55);
}

PythonSyntaxColors pythonSyntaxColors(const QPalette& palette) {
    const bool darkTheme = isDarkTheme(palette);
    if (darkTheme) {
        return {
            readableDefaultTextColor(palette, true),
            QColor(216, 180, 254),
            QColor(251, 113, 133),
            QColor(103, 232, 249),
            QColor(147, 197, 253),
            QColor(94, 234, 212),
            QColor(134, 239, 172),
            QColor(254, 202, 202),
            QColor(156, 163, 175),
            QColor(253, 211, 77)
        };
    }

    return {
        readableDefaultTextColor(palette, false),
        QColor(109, 40, 217),
        QColor(190, 18, 60),
        QColor(3, 105, 161),
        QColor(29, 78, 216),
        QColor(15, 118, 110),
        QColor(22, 101, 52),
        QColor(153, 27, 27),
        QColor(107, 114, 128),
        QColor(146, 64, 14)
    };
}
}  // namespace

class PythonLineNumberArea : public QWidget {
  public:
    explicit PythonLineNumberArea(PythonCodeEditor* editor) : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }

  protected:
    void paintEvent(QPaintEvent* event) override { m_editor->lineNumberAreaPaintEvent(event); }

  private:
    PythonCodeEditor* m_editor;
};

static int findTripleStringStart(const QString& text, int from, int* state) {
    for (int i = from; i < text.length(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('#')) {
            return -1;
        }

        if (ch != QLatin1Char('\'') && ch != QLatin1Char('"')) {
            continue;
        }

        if (i + 2 < text.length() && text.at(i + 1) == ch && text.at(i + 2) == ch) {
            *state = ch == QLatin1Char('\'') ? 1 : 2;
            return i;
        }

        ++i;
        while (i < text.length()) {
            if (text.at(i) == QLatin1Char('\\')) {
                i += 2;
                continue;
            }
            if (text.at(i) == ch) {
                break;
            }
            ++i;
        }
    }

    return -1;
}

PythonCodeEditor::PythonCodeEditor(QWidget* parent)
    : QPlainTextEdit(parent),
      m_lineNumberArea(new PythonLineNumberArea(this)),
      m_highlighter(new PythonSyntaxHighlighter(document())) {
    QFont editorFont("Consolas", 10);
    editorFont.setStyleHint(QFont::Monospace);
    setFont(editorFont);
    setTabStopDistance(QFontMetricsF(editorFont).horizontalAdvance(QLatin1Char(' ')) * 4.0);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setUndoRedoEnabled(true);
    setWordWrapMode(QTextOption::NoWrap);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &PythonCodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &PythonCodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &PythonCodeEditor::refreshExtraSelections);

    m_highlighter->refreshFormats(palette());
    updateLineNumberAreaWidth(0);
    refreshExtraSelections();
}

int PythonCodeEditor::lineNumberAreaWidth() const {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void PythonCodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    const QPalette editorPalette = palette();
    painter.fillRect(event->rect(), lineNumberBackgroundColor(editorPalette));
    painter.setPen(lineNumberTextColor(editorPalette));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const QString number = QString::number(blockNumber + 1);
            painter.drawText(0, top, m_lineNumberArea->width() - 5, fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void PythonCodeEditor::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);

    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange) {
        m_highlighter->refreshFormats(palette());
        refreshExtraSelections();
        m_lineNumberArea->update();
        viewport()->update();
    }
}

void PythonCodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);

    QRect content = contentsRect();
    m_lineNumberArea->setGeometry(QRect(content.left(), content.top(), lineNumberAreaWidth(),
                                        content.height()));
}

void PythonCodeEditor::keyPressEvent(QKeyEvent* event) {
    if (isReadOnly()) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        if (textCursor().hasSelection()) {
            indentSelection();
        } else {
            insertPlainText(QStringLiteral("    "));
        }
        return;
    }

    if (event->key() == Qt::Key_Backtab ||
        (event->key() == Qt::Key_Tab && event->modifiers() == Qt::ShiftModifier)) {
        unindentSelection();
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QString indent = currentLineIndent();
        QTextCursor cursor = textCursor();
        const QString leftText = cursor.block().text().left(cursor.positionInBlock()).trimmed();
        QPlainTextEdit::keyPressEvent(event);
        insertPlainText(leftText.endsWith(QLatin1Char(':')) ? indent + QStringLiteral("    ") : indent);
        return;
    }

    QPlainTextEdit::keyPressEvent(event);
}

void PythonCodeEditor::updateLineNumberAreaWidth(int) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void PythonCodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy != 0) {
        m_lineNumberArea->scroll(0, dy);
    } else {
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    }

    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth(0);
    }
}

void PythonCodeEditor::setApiKeywords(const QStringList& keywords) {
    m_highlighter->setApiKeywords(keywords);
}

void PythonCodeEditor::setErrorLines(const QList<int>& lines) {
    if (m_errorLines == lines) return;
    m_errorLines = lines;
    refreshExtraSelections();
}

void PythonCodeEditor::refreshExtraSelections() {
    QList<QTextEdit::ExtraSelection> selections;

    // 出错行先铺：当前行压在它上面，光标停在出错行时看到的是"当前行"色，
    // 移开之后红底重新露出来——两种信息都不丢。
    const QColor errorColor = errorLineColor(palette());
    for (int line : m_errorLines) {
        const QTextBlock block = document()->findBlockByNumber(line - 1);
        if (!block.isValid()) continue;  // traceback 指到文件外（脚本已被改过）
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(errorColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = QTextCursor(block);
        selection.cursor.clearSelection();
        selections.append(selection);
    }

    QTextEdit::ExtraSelection current;
    current.format.setBackground(currentLineColor(palette(), isReadOnly()));
    current.format.setProperty(QTextFormat::FullWidthSelection, true);
    current.cursor = textCursor();
    current.cursor.clearSelection();
    selections.append(current);

    setExtraSelections(selections);
}

void PythonCodeEditor::indentSelection() {
    QTextCursor cursor = textCursor();
    const int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    if (end > start) {
        QTextCursor endCursor(document());
        endCursor.setPosition(end);
        if (endCursor.positionInBlock() == 0) {
            --end;
        }
    }

    QTextBlock block = document()->findBlock(start);
    const QTextBlock endBlock = document()->findBlock(qMax(start, end));

    cursor.beginEditBlock();
    while (block.isValid()) {
        QTextCursor lineCursor(block);
        lineCursor.insertText(QStringLiteral("    "));
        if (block == endBlock) {
            break;
        }
        block = block.next();
    }
    cursor.endEditBlock();
}

void PythonCodeEditor::unindentSelection() {
    QTextCursor cursor = textCursor();
    const int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    if (end > start) {
        QTextCursor endCursor(document());
        endCursor.setPosition(end);
        if (endCursor.positionInBlock() == 0) {
            --end;
        }
    }

    QTextBlock block = document()->findBlock(start);
    const QTextBlock endBlock = document()->findBlock(qMax(start, end));

    cursor.beginEditBlock();
    while (block.isValid()) {
        const QString text = block.text();
        QTextCursor lineCursor(block);
        if (text.startsWith(QStringLiteral("    "))) {
            lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 4);
            lineCursor.removeSelectedText();
        } else if (text.startsWith(QLatin1Char('\t'))) {
            lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
            lineCursor.removeSelectedText();
        } else {
            int spaces = 0;
            while (spaces < text.size() && spaces < 4 && text.at(spaces) == QLatin1Char(' ')) {
                ++spaces;
            }
            if (spaces > 0) {
                lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, spaces);
                lineCursor.removeSelectedText();
            }
        }

        if (block == endBlock) {
            break;
        }
        block = block.next();
    }
    cursor.endEditBlock();
}

QString PythonCodeEditor::currentLineIndent() const {
    const QString blockText = textCursor().block().text();
    int index = 0;
    while (index < blockText.size() &&
           (blockText.at(index) == QLatin1Char(' ') || blockText.at(index) == QLatin1Char('\t'))) {
        ++index;
    }
    return blockText.left(index);
}

PythonSyntaxHighlighter::PythonSyntaxHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
    refreshFormats(QApplication::palette());
}

void PythonSyntaxHighlighter::setApiKeywords(const QStringList& keywords) {
    if (m_apiKeywords == keywords) return;
    m_apiKeywords = keywords;
    // 规则表是在 refreshFormats() 里一次建全的，所以换词表就得重建一遍。配色沿用
    // 上一次那份 palette——重建规则不该顺带把主题改回默认。
    refreshFormats(m_palette);
}

void PythonSyntaxHighlighter::refreshFormats(const QPalette& palette) {
    m_palette = palette;
    const PythonSyntaxColors colors = pythonSyntaxColors(palette);
    const QTextCharFormat keywordFormat = format(colors.keyword, true);
    const QTextCharFormat builtinFormat = format(colors.builtin);
    const QTextCharFormat apiFormat = format(colors.api, true);
    const QTextCharFormat numberFormat = format(colors.number);
    const QTextCharFormat decoratorFormat = format(colors.decorator);
    const QTextCharFormat defFormat = format(colors.definition, true);
    const QTextCharFormat operatorFormat = format(colors.operatorText);

    m_rules.clear();
    m_defaultFormat = format(colors.defaultText);
    m_commentFormat = format(colors.comment, false, true);
    m_stringFormat = format(colors.string);
    m_tripleStringFormat = m_stringFormat;

    addKeywordRules(QStringList{
        QStringLiteral("False"), QStringLiteral("None"), QStringLiteral("True"),
        QStringLiteral("and"), QStringLiteral("as"), QStringLiteral("assert"),
        QStringLiteral("async"), QStringLiteral("await"), QStringLiteral("break"),
        QStringLiteral("class"), QStringLiteral("continue"), QStringLiteral("def"),
        QStringLiteral("del"), QStringLiteral("elif"), QStringLiteral("else"),
        QStringLiteral("except"), QStringLiteral("finally"), QStringLiteral("for"),
        QStringLiteral("from"), QStringLiteral("global"), QStringLiteral("if"),
        QStringLiteral("import"), QStringLiteral("in"), QStringLiteral("is"),
        QStringLiteral("lambda"), QStringLiteral("nonlocal"), QStringLiteral("not"),
        QStringLiteral("or"), QStringLiteral("pass"), QStringLiteral("raise"),
        QStringLiteral("return"), QStringLiteral("try"), QStringLiteral("while"),
        QStringLiteral("with"), QStringLiteral("yield")
    }, keywordFormat);

    addKeywordRules(QStringList{
        QStringLiteral("abs"), QStringLiteral("all"), QStringLiteral("any"),
        QStringLiteral("bool"), QStringLiteral("dict"), QStringLiteral("enumerate"),
        QStringLiteral("filter"), QStringLiteral("float"), QStringLiteral("int"),
        QStringLiteral("len"), QStringLiteral("list"), QStringLiteral("map"),
        QStringLiteral("max"), QStringLiteral("min"), QStringLiteral("open"),
        QStringLiteral("print"), QStringLiteral("range"), QStringLiteral("round"),
        QStringLiteral("set"), QStringLiteral("sorted"), QStringLiteral("str"),
        QStringLiteral("sum"), QStringLiteral("tuple"), QStringLiteral("type"),
        QStringLiteral("zip")
    }, builtinFormat);

    // 宿主脚本 API 的名字由 setApiKeywords() 注入 —— 见头文件里的理由。
    addKeywordRules(m_apiKeywords, apiFormat);

    m_rules.append({QRegularExpression(QStringLiteral("@[A-Za-z_][A-Za-z0-9_\\.]*")), decoratorFormat});
    m_rules.append({QRegularExpression(QStringLiteral("\\b[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?\\b")),
                    numberFormat});
    m_rules.append({QRegularExpression(QStringLiteral("\\bclass\\s+([A-Za-z_][A-Za-z0-9_]*)")),
                    defFormat});
    m_rules.append({QRegularExpression(QStringLiteral("\\bdef\\s+([A-Za-z_][A-Za-z0-9_]*)")),
                    defFormat});
    m_rules.append({QRegularExpression(QStringLiteral("[+\\-*/%=<>!&|^~:]+")), operatorFormat});

    rehighlight();
}

void PythonSyntaxHighlighter::highlightBlock(const QString& text) {
    if (!text.isEmpty()) {
        setFormat(0, text.length(), m_defaultFormat);
    }

    for (const HighlightRule& rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            if (match.lastCapturedIndex() >= 1 && match.capturedStart(1) >= 0) {
                setFormat(match.capturedStart(1), match.capturedLength(1), rule.format);
            } else {
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

    highlightSingleLineStringsAndComment(text);

    int start = 0;
    int state = previousBlockState();
    if (state < 0) {
        state = 0;
    }

    while (start < text.length()) {
        if (state == 1 || state == 2) {
            const QString delimiter = state == 1 ? QStringLiteral("'''") : QStringLiteral("\"\"\"");
            const int end = text.indexOf(delimiter, start);
            if (end < 0) {
                setFormat(start, text.length() - start, m_tripleStringFormat);
                break;
            }

            setFormat(start, end - start + delimiter.length(), m_tripleStringFormat);
            start = end + delimiter.length();
            state = 0;
            continue;
        }

        int nextState = 0;
        const int firstIndex = findTripleStringStart(text, start, &nextState);
        if (firstIndex < 0) {
            break;
        }

        const QString delimiter = nextState == 1 ? QStringLiteral("'''") : QStringLiteral("\"\"\"");
        const int end = text.indexOf(delimiter, firstIndex + delimiter.length());
        if (end < 0) {
            setFormat(firstIndex, text.length() - firstIndex, m_tripleStringFormat);
            state = nextState;
            break;
        }

        setFormat(firstIndex, end - firstIndex + delimiter.length(), m_tripleStringFormat);
        start = end + delimiter.length();
    }

    setCurrentBlockState(state);
}

QTextCharFormat PythonSyntaxHighlighter::format(const QColor& color, bool bold, bool italic) const {
    QTextCharFormat textFormat;
    textFormat.setForeground(color);
    if (bold) {
        textFormat.setFontWeight(QFont::Bold);
    }
    textFormat.setFontItalic(italic);
    return textFormat;
}

void PythonSyntaxHighlighter::addKeywordRules(const QStringList& words, const QTextCharFormat& format) {
    for (const QString& word : words) {
        m_rules.append({QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(word))),
                        format});
    }
}

void PythonSyntaxHighlighter::highlightSingleLineStringsAndComment(const QString& text) {
    for (int i = 0; i < text.length(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('#')) {
            setFormat(i, text.length() - i, m_commentFormat);
            return;
        }

        if (ch != QLatin1Char('\'') && ch != QLatin1Char('"')) {
            continue;
        }

        if (i + 2 < text.length() && text.at(i + 1) == ch && text.at(i + 2) == ch) {
            i += 2;
            continue;
        }

        const int stringStart = i;
        ++i;
        while (i < text.length()) {
            if (text.at(i) == QLatin1Char('\\')) {
                i += 2;
                continue;
            }
            if (i < text.length() && text.at(i) == ch) {
                break;
            }
            ++i;
        }

        const int stringEnd = qMin(i, text.length() - 1);
        setFormat(stringStart, stringEnd - stringStart + 1, m_stringFormat);
    }
}
