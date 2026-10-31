#include "codeeditor.h"

#include "spellchecker.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>

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

// Most suggestions worth showing in the menu.
constexpr int MaxSuggestions = 6;

// Dropped files and folders, local or on a server. Other URLs (such as web
// links) are inserted as text as before.
QList<QUrl> openableUrls(const QMimeData *source)
{
    QList<QUrl> result;
    if (!source->hasUrls())
        return result;
    for (const QUrl &url : source->urls()) {
        if (url.isLocalFile() || url.scheme().compare(QLatin1String("sftp"), Qt::CaseInsensitive) == 0)
            result << url;
    }
    return result;
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
    // The scroll bars match the theme rather than the system's appearance.
    setProperty("classicDark", theme.dark);
    verticalScrollBar()->update();
    horizontalScrollBar()->update();

    highlightCurrentLine();
}

void CodeEditor::setEditorFont(const QFont &font)
{
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    updateLineNumberAreaWidth();
    m_lineNumberArea->update();
}

void CodeEditor::setLineNumbersVisible(bool visible)
{
    m_lineNumbers = visible;
    m_lineNumberArea->setVisible(visible);
    updateLineNumberAreaWidth();
}

int CodeEditor::lineNumberAreaWidth() const
{
    const int digits = qMax(3, int(QString::number(qMax(1, blockCount())).size()));
    return fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 20;
}

void CodeEditor::updateLineNumberAreaWidth()
{
    const int width = m_lineNumbers ? lineNumberAreaWidth() : 0;
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
    return !openableUrls(source).isEmpty() || QPlainTextEdit::canInsertFromMimeData(source);
}

void CodeEditor::insertFromMimeData(const QMimeData *source)
{
    // Dropping a file opens it (and a folder shows it in the sidebar)
    // rather than inserting its path.
    const QList<QUrl> urls = openableUrls(source);
    if (!urls.isEmpty()) {
        emit urlsDropped(urls);
        return;
    }
    QPlainTextEdit::insertFromMimeData(source);
}

void CodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu(event->pos());
    addSpellingActions(menu, event->pos());
    menu->exec(event->globalPos());
    delete menu;
}

void CodeEditor::addSpellingActions(QMenu *menu, const QPoint &pos)
{
    // Only words the highlighter underlined: they are prose and misspelled.
    const QTextCursor clicked = cursorForPosition(pos);
    const QTextBlock block = clicked.block();
    const int column = clicked.positionInBlock();
    int start, end;
    if (!SpellChecker::wordAt(block.text(), column, &start, &end))
        return;
    bool underlined = false;
    for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
        if (start >= range.start && start < range.start + range.length
            && range.format.underlineStyle() == QTextCharFormat::SpellCheckUnderline)
            underlined = true;
    }
    if (!underlined)
        return;

    const QString word = block.text().mid(start, end - start);
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + start);
    cursor.setPosition(block.position() + end, QTextCursor::KeepAnchor);

    QAction *first = menu->actions().value(0);
    QList<QAction *> actions;
    const QStringList suggestions = SpellChecker::instance()->suggestions(word).mid(0, MaxSuggestions);
    for (const QString &suggestion : suggestions) {
        auto *action = new QAction(suggestion, menu);
        QFont font = action->font();
        font.setBold(true);
        action->setFont(font);
        connect(action, &QAction::triggered, this, [this, cursor, suggestion]() mutable {
            cursor.insertText(suggestion);
            setTextCursor(cursor);
        });
        actions << action;
    }
    if (suggestions.isEmpty()) {
        auto *none = new QAction(tr("No Guesses Found"), menu);
        none->setEnabled(false);
        actions << none;
    }
    auto *separator = new QAction(menu);
    separator->setSeparator(true);
    actions << separator;
    auto *ignore = new QAction(tr("Ignore Spelling"), menu);
    connect(ignore, &QAction::triggered, this, [word] { SpellChecker::instance()->ignore(word); });
    auto *learn = new QAction(tr("Add to Dictionary"), menu);
    connect(learn, &QAction::triggered, this, [word] { SpellChecker::instance()->learn(word); });
    auto *separator2 = new QAction(menu);
    separator2->setSeparator(true);
    actions << ignore << learn << separator2;
    menu->insertActions(first, actions);
}
