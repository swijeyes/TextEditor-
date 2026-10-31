#pragma once

#include "filesystem.h"
#include "highlighter.h"
#include "settingsdialog.h"

#include <QFuture>
#include <QMainWindow>
#include <QUrl>

#include <memory>

class ConnectionManager;
class DialogPrompter;
class Document;
class FindBar;
class FolderSidebar;
class LocalFileSystem;
class QAction;
class QActionGroup;
class QComboBox;
class QFontComboBox;
class QLabel;
class QMenu;
class QTabWidget;
class QToolBar;
class QWidgetAction;
class Preview;
struct ServerAddress;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens a file in a tab (or switches to its tab), or a folder in the
    // sidebar. Accepts file:// and sftp:// URLs.
    bool openLocation(const QUrl &url);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum class RecentKind { File, Folder };

    void createUi();
    void createSidebar();
    void createPreview();
    // Shows the preview for a LaTeX, Markdown or HTML tab (unless it was
    // closed), and hides it for other tabs.
    void updatePreview();

    // Tabs
    Document *currentDocument() const;
    Document *documentAt(int index) const;
    QList<Document *> documents() const;
    Document *newTab();
    int findTab(const QUrl &url) const;
    bool closeTab(int index);
    bool closeAllTabs();
    void currentTabChanged();
    void updateTab(Document *document);
    void updateWindowState();

    // Files
    void open();
    bool loadInto(Document *target, const QUrl &url);
    bool save(Document *document);
    bool saveAs(Document *document);
    bool saveAsLocal(Document *document);
    bool saveToServer(Document *document);
    bool writeDocument(Document *document, const QUrl &url, bool sameFile);
    bool maybeSave(Document *document);
    void print();
    void openUrls(const QList<QUrl> &urls);
    void openRequested(const QUrl &url);
    void reveal();

    // Folders and servers
    void openFolder();
    bool openFolderUrl(const QUrl &url);
    void closeFolder();
    void connectToServer();
    void openRemoteFile();
    void openRemoteFolder();
    bool askForServer(const QString &title, const QString &acceptText, ServerAddress *server,
                      QString *folder);
    std::shared_ptr<FileSystem> fileSystemFor(const QUrl &url);
    // Connects if needed and turns "~" into the home folder's real path.
    QUrl resolveHome(const QUrl &url, FileSystem *fileSystem);
    void followRename(const QUrl &from, const QUrl &to);

    // Waits for a file operation while keeping the window responsive: after
    // a moment a progress dialog appears with a Cancel button.
    template<typename T>
    T waitFor(QFuture<T> future, const QString &label, FileSystem *fileSystem);

    // Recent files and folders
    void addRecent(const QUrl &url, RecentKind kind);
    void updateRecentMenu();

    void setLanguage(Language language);
    void updateLanguageUi(Language language);
    void setWordWrap(bool on);
    void setDarkTheme(bool dark);
    void showSettings();
    void applyPreferences(const Preferences &preferences);
    // Fills the toolbar with the buttons not hidden in Settings.
    void rebuildToolBar();
    QList<ToolBarButton> toolBarButtons() const;
    void changeFontSize(int delta);
    void resetFontSize();
    void applyFont();
    void applyEditorSettings(Document *document);
    void goToLine();
    void updateCursorPosition();

    void readSettings();
    void writeSettings() const;

    QTabWidget *m_tabs;
    FindBar *m_findBar;
    FolderSidebar *m_sidebar = nullptr;
    Preview *m_preview = nullptr;
    QAction *m_previewAction = nullptr;
    bool m_previewWanted = true;
    std::shared_ptr<LocalFileSystem> m_localFs;
#ifdef TEXTEDITOR_HAVE_SFTP
    std::unique_ptr<DialogPrompter> m_prompter;
    ConnectionManager *m_connections = nullptr;
#endif

    QFontComboBox *m_fontCombo = nullptr;
    QComboBox *m_languageCombo = nullptr;
    QLabel *m_positionLabel = nullptr;
    QLabel *m_lineEndingLabel = nullptr;
    QAction *m_wrapAction = nullptr;
    QAction *m_darkAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QToolBar *m_toolBar = nullptr;
    // The toolbar's groups, between separators, and the buttons in them
    // that Settings can hide (by id).
    QList<QList<QAction *>> m_toolBarGroups;
    QList<QPair<QString, QAction *>> m_hideableButtons;
    QActionGroup *m_languageGroup = nullptr;
    QMenu *m_recentMenu = nullptr;
    QList<QMetaObject::Connection> m_editorConnections;

    QString m_fontFamily;
    int m_fontSize = 0;
    bool m_wordWrap = true;
    Preferences m_preferences;
    int m_busy = 0; // nested waitFor() calls in progress
};
