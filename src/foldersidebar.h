#pragma once

#include "filesystem.h"

#include <QDockWidget>
#include <QUrl>

#include <memory>

class FileTreeProxyModel;
class QFileSystemModel;
class QLabel;
class QStackedWidget;
class QTreeView;
class RemoteFileModel;

// A dockable panel showing the files in one folder, local or on a server.
// Clicking a file opens it in a tab; the right-click menu creates, renames
// and deletes files and folders.
class FolderSidebar : public QDockWidget
{
    Q_OBJECT

public:
    explicit FolderSidebar(QWidget *parent = nullptr);

    void openLocalFolder(const QUrl &url, std::shared_ptr<FileSystem> fileSystem);
    void openRemoteFolder(const QUrl &url, std::shared_ptr<FileSystem> fileSystem);
    // A remote folder restored from the last session: shows a Connect
    // button rather than connecting straight away.
    void showUnconnectedFolder(const QUrl &url);
    void closeFolder();

    bool hasFolder() const { return !m_url.isEmpty(); }
    QUrl folderUrl() const { return m_url; }
    std::shared_ptr<FileSystem> fileSystem() const { return m_fileSystem; }

    bool showHidden() const;
    void setShowHidden(bool show);
    void refresh();

signals:
    void fileActivated(const QUrl &url);
    void connectRequested(const QUrl &folderUrl);
    void showHiddenChanged(bool show);
    // So open tabs can follow their files.
    void itemRenamed(const QUrl &from, const QUrl &to);
    void itemRemoved(const QUrl &url);

private:
    void setFolder(const QUrl &url);
    void showContextMenu(const QPoint &pos);
    void activate(const QModelIndex &index, bool clicked);
    QUrl urlAt(const QModelIndex &proxyIndex) const;
    QString pathAt(const QModelIndex &proxyIndex) const;
    bool isFolderAt(const QModelIndex &proxyIndex) const;
    // The folder new items go in: the item itself if it's a folder, its
    // parent if it's a file, or the root.
    QString targetFolder(const QModelIndex &proxyIndex) const;
    void refreshFolder(const QString &folderPath);
    bool checkName(const QString &name);

    void newFile(const QModelIndex &at);
    void newFolder(const QModelIndex &at);
    void renameItem(const QModelIndex &at);
    void deleteItem(const QModelIndex &at);

    QLabel *m_header;
    QStackedWidget *m_stack;
    QTreeView *m_tree;
    QLabel *m_connectLabel;
    QWidget *m_emptyPage;
    QWidget *m_connectPage;
    class QAction *m_showHiddenAction;
    FileTreeProxyModel *m_proxy;
    QFileSystemModel *m_localModel = nullptr;
    RemoteFileModel *m_remoteModel = nullptr;
    std::shared_ptr<FileSystem> m_fileSystem;
    QUrl m_url;
};
