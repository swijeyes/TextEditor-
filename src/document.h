#pragma once

#include "filesystem.h"
#include "highlighter.h"

#include <QUrl>
#include <QWidget>

#include <memory>

class CodeEditor;
class QLabel;
class QStackedLayout;
struct DecodedText;

// One open file, shown in its own tab: its editor and highlighter, and the
// per-file state that 1.x kept in the main window.
class Document : public QWidget
{
    Q_OBJECT

public:
    explicit Document(QWidget *parent = nullptr);

    CodeEditor *editor() const { return m_editor; }
    Highlighter *highlighter() const { return m_highlighter; }

    // Where the file lives (file:// or sftp://), or empty for a new file.
    QUrl location() const { return m_location; }
    bool isUntitled() const { return m_location.isEmpty(); }
    bool isRemote() const;
    // The tab's name, and the full path or sftp:// address (no password).
    QString displayName() const;
    QString displayLocation() const;

    bool crlf() const { return m_crlf; }
    bool latin1() const { return m_latin1; }
    bool isModified() const;
    void setModified(bool modified);
    // An untitled tab nobody has typed in, which opening a file can reuse.
    bool isPristine() const;

    // The file as it was on disk when opened or last saved, for spotting
    // changes made by someone else before saving over them.
    FileInfo savedVersion() const { return m_savedVersion; }
    // The path to write to: the real file when the location was a symbolic
    // link on a server, otherwise the location's own path.
    QString writePath() const;
    QString resolvedPath() const { return m_resolvedPath; }

    // The file system the file was opened from. Holding it keeps a server's
    // connection open while the tab is.
    std::shared_ptr<FileSystem> fileSystem() const { return m_fileSystem; }
    void setFileSystem(std::shared_ptr<FileSystem> fileSystem) { m_fileSystem = std::move(fileSystem); }

    // Restored tabs for remote files wait for a click before connecting.
    bool isLoaded() const { return m_loaded; }
    void setUnloaded(const QUrl &location);

    void setContent(const QUrl &location, const DecodedText &decoded, const FileInfo &version,
                    const QString &resolvedPath = QString());
    void reset();
    // Records a successful save to location.
    void markSaved(const QUrl &location, bool latin1, const FileInfo &version,
                   const QString &resolvedPath = QString());
    // Points the tab at a new location after a rename in the sidebar.
    void relocate(const QUrl &location);

signals:
    // The name, location or modified state changed, so the tab needs updating.
    void stateChanged();
    void loadRequested();

private:
    CodeEditor *m_editor;
    Highlighter *m_highlighter;
    QStackedLayout *m_stack;
    QWidget *m_placeholder;
    QLabel *m_placeholderLabel;

    QUrl m_location;
    bool m_crlf = false;
    bool m_latin1 = false;
    bool m_loaded = true;
    FileInfo m_savedVersion;
    QString m_resolvedPath;
    std::shared_ptr<FileSystem> m_fileSystem;
};
