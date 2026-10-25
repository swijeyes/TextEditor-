#pragma once

#include "theme.h"

#include <QPlainTextEdit>

// A plain-text editor with a line-number gutter, current-line highlight and
// auto-indent.
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    void setTheme(const Theme &theme);
    void setEditorFont(const QFont &font);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

signals:
    void zoomRequested(int steps);
    void fileDropped(const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();

    QWidget *m_lineNumberArea;
    Theme m_theme;
    int m_wheelDelta = 0;
};
