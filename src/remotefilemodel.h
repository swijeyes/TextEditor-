#pragma once

#include "filesystem.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QSortFilterProxyModel>

#include <memory>
#include <vector>

// The files on a server, as a tree. Each folder is listed only when it is
// expanded, using FileSystem::list; a "Loading…" row shows while waiting,
// and any error appears inline. The server sends no change notifications,
// so folders are reloaded with refresh().
class RemoteFileModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        IsFolderRole,
        IsPlaceholderRole, // "Loading…", an error, or a notice
    };

    RemoteFileModel(std::shared_ptr<FileSystem> fileSystem, const QString &rootPath,
                    QObject *parent = nullptr);
    ~RemoteFileModel() override;

    QString rootPath() const;
    std::shared_ptr<FileSystem> fileSystem() const { return m_fileSystem; }
    QString path(const QModelIndex &index) const;
    bool isFolder(const QModelIndex &index) const;
    bool isPlaceholder(const QModelIndex &index) const;
    // Lists a folder again (the root for an invalid index).
    void refresh(const QModelIndex &folder = QModelIndex());
    // Reloads the folder with this path, if it has been loaded.
    void refreshPath(const QString &folderPath);
    // Whether a name appears in a loaded folder's listing.
    bool containsName(const QModelIndex &folder, const QString &name) const;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

signals:
    // A folder's listing arrived (or failed).
    void folderLoaded(const QModelIndex &folder, bool ok);

private:
    struct Node;
    Node *nodeFor(const QModelIndex &index) const;
    QModelIndex indexFor(Node *node) const;
    void clearChildren(Node *node);
    void setChildren(Node *node, std::vector<std::unique_ptr<Node>> children);
    std::unique_ptr<Node> makeNode(Node *parent);
    Node *findFolder(Node *node, const QString &path) const;

    std::shared_ptr<FileSystem> m_fileSystem;
    std::unique_ptr<Node> m_root;
    QHash<quint64, Node *> m_nodes; // by id, so late replies find their folder
    quint64 m_nextId = 1;
};

// Sorts folders before files and hides names starting with a dot unless
// showHidden is set. Works on top of RemoteFileModel and QFileSystemModel.
class FileTreeProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);
    // For QFileSystemModel: the open folder, which is never hidden.
    void setRootPath(const QString &path);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool isFolder(const QModelIndex &sourceIndex) const;
    bool isPlaceholder(const QModelIndex &sourceIndex) const;
    QString name(const QModelIndex &sourceIndex) const;

    bool m_showHidden = false;
    QString m_rootPath;
};
