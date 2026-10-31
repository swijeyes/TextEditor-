#pragma once

#include "texcompiler.h"
#include "theme.h"

#include <QDockWidget>
#include <QPointer>

#include <functional>

class Document;
class PagesView;
class PreviewBrowser;
class QComboBox;
class QLabel;
class QStackedWidget;
class QTextDocument;
class QTimer;
class QToolButton;

// The live preview, docked on the right of the window. It follows the
// current tab's text as it is typed:
//  - Markdown and HTML are shown as formatted text and as a web page, by
//    Qt's rich-text engine (no scripts, nothing fetched from the network);
//  - LaTeX is shown in one of two modes:
//    - Quick: converted straight away by LatexHtml, with no TeX needed;
//    - Typeset: typeset by a TeX engine on the computer (TexCompiler) a
//      moment after typing stops, showing the real pages.
class Preview : public QDockWidget
{
    Q_OBJECT

public:
    enum class Kind { Latex, Markdown, Html };

    explicit Preview(QWidget *parent = nullptr);

    // The tab to preview and what it is, or nullptr when the preview is
    // hidden.
    void setDocument(Document *document, Kind kind = Kind::Latex);
    Kind kind() const { return m_kind; }
    void setTheme(const Theme &theme);

    // How LaTeX is previewed: "quick", or the typesetting engine's name
    // such as "pdflatex".
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);

signals:
    // The close button was clicked (hiding it for another tab doesn't count).
    void closedByUser();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void scheduleUpdate();
    void updateNow();
    bool usesBrowser() const;
    void updateQuick(const QString &text);
    void updateMarkdown(const QString &text);
    void updateHtml(const QString &text);
    void updateTypeset(const QString &text);
    void typesetFinished(const QStringList &pages, const QString &pdfPath, const QString &warning);
    void updateControls();
    void showStatus(const QString &text, bool error = false);
    // Shows rich text, keeping the reading position.
    void showInBrowser(const std::function<void(QTextDocument *)> &load);
    const TexCompiler::Tool *engine() const;
    QString sourceFolder() const;

    QPointer<Document> m_document;
    QMetaObject::Connection m_textConnection;
    Kind m_kind = Kind::Latex;
    QString m_mode;
    QString m_lastText;      // what the current preview shows
    QString m_lastMode;
    bool m_pending = false;  // the text changed while typesetting

    QComboBox *m_modeCombo;
    QToolButton *m_openPdfButton;
    QLabel *m_status;
    QStackedWidget *m_stack;
    PreviewBrowser *m_browser;
    PagesView *m_pages;
    QTimer *m_timer;
    TexCompiler *m_compiler;
    QString m_pdfPath;
    Theme m_theme;
};
