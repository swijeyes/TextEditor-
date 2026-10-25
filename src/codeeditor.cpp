#include "codeeditor.h"

#include <QMimeData>
#include <QPainter>
#include <QTextBlock>

namespace {

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor)
        : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->lineNumberAreaPaintEvent(event); }

private:
    CodeEditor *m_editor;
};

bool hasLocalFiles(const QMimeData *source)
{
    if (!source->hasUrls())
        return false;
    const QList<QUrl> urls = source->urls();
    return !urls.isEmpty() && urls.first().isLocalFile();
}

} // namespace

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumberArea(new LineNumberArea(this))
    , m_theme(Theme::dracula())
{
    setFrameShape(QFrame::NoFrame);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

void CodeEditor::setTheme(const Theme &theme)
{
    m_theme = theme;

    QPalette p = palette();
    p.setColor(QPalette::Base, theme.background);
    p.setColor(QPalette::Text, theme.foreground);
    p.setColor(QPalette::Highlight, theme.selection);
    p.setColor(QPalette::HighlightedText, theme.foreground);
    setPalette(p);

    highlightCurrentLine();
}

void CodeEditor::setEditorFont(const QFont &font)
{
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    updateLineNumberAreaWidth();
    m_lineNumberArea->update();
}

int CodeEditor::lineNumberAreaWidth() const
{
    const int digits = qMax(3, int(QString::number(qMax(1, blockCount())).size()));
    return fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 20;
}

void CodeEditor::updateLineNumberAreaWidth()
{
    const int width = lineNumberAreaWidth();
    setViewportMargins(width, 0, 0, 0);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(cr.left(), cr.top(), width, cr.height());
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateLineNumberAreaWidth();
}

void CodeEditor::highlightCurrentLine()
{
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(m_theme.currentLine);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    setExtraSelections({selection});

    // The current line number is drawn in a brighter colour.
    m_lineNumberArea->update();
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_theme.gutterBackground);
    painter.setFont(font());

    const int currentBlock = textCursor().blockNumber();
    const int textWidth = m_lineNumberArea->width() - 10;
    const int lineHeight = fontMetrics().height();

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(blockNumber == currentBlock ? m_theme.gutterCurrent
                                                       : m_theme.gutterForeground);
            painter.drawText(0, top, textWidth, lineHeight, Qt::AlignRight,
                             QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    // Auto-indent: a new line starts with the previous line's indentation.
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (enter && modifiers == Qt::NoModifier) {
        QTextCursor cursor = textCursor();
        const QString line = cursor.block().text();
        int indent = 0;
        while (indent < line.size() && (line.at(indent) == u' ' || line.at(indent) == u'\t'))
            ++indent;
        indent = qMin(indent, cursor.positionInBlock());
        cursor.insertText(QLatin1Char('\n') + line.left(indent));
        setTextCursor(cursor);
        ensureCursorVisible();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::wheelEvent(QWheelEvent *event)
{
    // Ctrl/Cmd + wheel zooms through the main window so the size is saved.
    if (event->modifiers() & Qt::ControlModifier) {
        m_wheelDelta += event->angleDelta().y();
        while (qAbs(m_wheelDelta) >= 120) {
            const int step = m_wheelDelta > 0 ? 1 : -1;
            m_wheelDelta -= step * 120;
            emit zoomRequested(step);
        }
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

bool CodeEditor::canInsertFromMimeData(const QMimeData *source) const
{
    return hasLocalFiles(source) || QPlainTextEdit::canInsertFromMimeData(source);
}

void CodeEditor::insertFromMimeData(const QMimeData *source)
{
    // Dropping a file opens it rather than inserting its path.
    if (hasLocalFiles(source)) {
        emit fileDropped(source->urls().first().toLocalFile());
        return;
    }
    QPlainTextEdit::insertFromMimeData(source);
}
