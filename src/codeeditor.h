#pragma once

#include "theme.h"

#include <QPlainTextEdit>

class QMenu;

// A plain-text editor with a line-number gutter, current-line highlight,
// auto-indent and spelling suggestions on right-click.
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    void setTheme(const Theme &theme);
    void setEditorFont(const QFont &font);
    void setLineNumbersVisible(bool visible);
    bool lineNumbersVisible() const { return m_lineNumbers; }

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

signals:
    void zoomRequested(int steps);
    // Files and folders dropped on the editor (file:// or sftp:// URLs).
    void urlsDropped(const QList<QUrl> &urls);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();
    // Adds suggestions for a misspelled word at pos to the top of menu.
    void addSpellingActions(QMenu *menu, const QPoint &pos);

    QWidget *m_lineNumberArea;
    Theme m_theme;
    int m_wheelDelta = 0;
    bool m_lineNumbers = true;
};
