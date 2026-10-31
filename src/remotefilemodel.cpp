#include "remotefilemodel.h"

#include "location.h"
#include "messagebox.h"

#include <QApplication>
#include <QDir>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QFont>
#include <QPalette>

struct RemoteFileModel::Node
{
    enum Kind { Entry, Loading, Error, Notice };

    quint64 id = 0;
    Node *parent = nullptr;
    int row = 0;
    Kind kind = Entry;
    FileInfo info;
    QString path;
    QString text; // for placeholders
    bool loaded = false;
    bool loading = false;
    int request = 0; // so an outdated listing is ignored
    std::vector<std::unique_ptr<Node>> children;

    bool isFolder() const { return kind == Entry && info.isFolder(); }
};

RemoteFileModel::RemoteFileModel(std::shared_ptr<FileSystem> fileSystem, const QString &rootPath,
                                 QObject *parent)
    : QAbstractItemModel(parent)
    , m_fileSystem(std::move(fileSystem))
    , m_root(makeNode(nullptr))
{
    m_root->path = rootPath;
    m_root->info.type = FileInfo::Folder;
    m_root->info.name = Location::remoteFileName(rootPath);
}

RemoteFileModel::~RemoteFileModel() = default;

std::unique_ptr<RemoteFileModel::Node> RemoteFileModel::makeNode(Node *parent)
{
    auto node = std::make_unique<Node>();
    node->id = m_nextId++;
    node->parent = parent;
    m_nodes.insert(node->id, node.get());
    return node;
}

QString RemoteFileModel::rootPath() const
{
    return m_root->path;
}

RemoteFileModel::Node *RemoteFileModel::nodeFor(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<Node *>(index.internalPointer()) : m_root.get();
}

QModelIndex RemoteFileModel::indexFor(Node *node) const
{
    if (!node || node == m_root.get())
        return {};
    return createIndex(node->row, 0, node);
}

QString RemoteFileModel::path(const QModelIndex &index) const
{
    return nodeFor(index)->path;
}

bool RemoteFileModel::isFolder(const QModelIndex &index) const
{
    return nodeFor(index)->isFolder();
}

bool RemoteFileModel::isPlaceholder(const QModelIndex &index) const
{
    return nodeFor(index)->kind != Node::Entry;
}

QModelIndex RemoteFileModel::index(int row, int column, const QModelIndex &parent) const
{
    const Node *node = nodeFor(parent);
    if (column != 0 || row < 0 || row >= int(node->children.size()))
        return {};
    return createIndex(row, column, node->children[size_t(row)].get());
}

QModelIndex RemoteFileModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};
    return indexFor(nodeFor(index)->parent);
}

int RemoteFileModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    return int(nodeFor(parent)->children.size());
}

int RemoteFileModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant RemoteFileModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const Node *node = nodeFor(index);
    const bool placeholder = node->kind != Node::Entry;
    switch (role) {
    case Qt::DisplayRole:
        return placeholder ? node->text : node->info.name;
    case Qt::ToolTipRole:
        return plainToolTip(placeholder ? node->text : Location::displayString(m_fileSystem->urlFor(node->path)));
    case Qt::DecorationRole: {
        if (placeholder)
            return {};
        static const QFileIconProvider icons;
        return icons.icon(node->isFolder() ? QFileIconProvider::Folder : QFileIconProvider::File);
    }
    case Qt::ForegroundRole:
        if (placeholder)
            return QApplication::palette().color(QPalette::Disabled, QPalette::Text);
        return {};
    case Qt::FontRole:
        if (placeholder) {
            QFont font;
            font.setItalic(true);
            return font;
        }
        return {};
    case PathRole:
        return node->path;
    case IsFolderRole:
        return node->isFolder();
    case IsPlaceholderRole:
        return placeholder;
    default:
        return {};
    }
}

Qt::ItemFlags RemoteFileModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    if (isPlaceholder(index))
        return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool RemoteFileModel::hasChildren(const QModelIndex &parent) const
{
    return nodeFor(parent)->isFolder();
}

bool RemoteFileModel::canFetchMore(const QModelIndex &parent) const
{
    const Node *node = nodeFor(parent);
    return node->isFolder() && !node->loaded && !node->loading;
}

void RemoteFileModel::fetchMore(const QModelIndex &parent)
{
    Node *node = nodeFor(parent);
    if (!node->isFolder() || node->loading)
        return;
    node->loading = true;
    const int request = ++node->request;
    const quint64 id = node->id;

    std::vector<std::unique_ptr<Node>> loading;
    loading.push_back(makeNode(node));
    loading.back()->kind = Node::Loading;
    loading.back()->text = tr("Loading…");
    setChildren(node, std::move(loading));

    m_fileSystem->list(node->path).then(this, [this, id, request](const ListResult &result) {
        Node *node = m_nodes.value(id);
        if (!node || node->request != request)
            return;
        node->loading = false;
        node->loaded = true;

        std::vector<std::unique_ptr<Node>> children;
        auto addPlaceholder = [&](Node::Kind kind, const QString &text) {
            children.push_back(makeNode(node));
            children.back()->kind = kind;
            children.back()->text = text;
        };
        if (!result.ok()) {
            addPlaceholder(Node::Error, result.message);
        } else {
            children.reserve(size_t(result.entries.size()) + 2);
            for (const FileInfo &info : result.entries) {
                // Names were checked by the file system; paths are only ever
                // built from those checked names.
                children.push_back(makeNode(node));
                children.back()->info = info;
                children.back()->path = Location::joinRemotePath(node->path, info.name);
            }
            if (result.truncated) {
                addPlaceholder(Node::Notice,
                               tr("This folder is too large to show completely: only the first "
                                  "%L1 items are listed.").arg(FileLimits::MaxListingEntries));
            }
            if (result.rejected > 0) {
                addPlaceholder(Node::Notice,
                               tr("%n item(s) with names that can't be used safely were left out.",
                                  nullptr, result.rejected));
            }
            if (result.entries.isEmpty() && !result.truncated && result.rejected == 0)
                addPlaceholder(Node::Notice, tr("Empty folder"));
        }
        setChildren(node, std::move(children));
        emit folderLoaded(indexFor(node), result.ok());
    });
}

void RemoteFileModel::clearChildren(Node *node)
{
    for (const std::unique_ptr<Node> &child : node->children) {
        clearChildren(child.get());
        m_nodes.remove(child->id);
    }
    node->children.clear();
}

void RemoteFileModel::setChildren(Node *node, std::vector<std::unique_ptr<Node>> children)
{
    const QModelIndex parent = indexFor(node);
    if (!node->children.empty()) {
        beginRemoveRows(parent, 0, int(node->children.size()) - 1);
        clearChildren(node);
        endRemoveRows();
    }
    if (children.empty())
        return;
    beginInsertRows(parent, 0, int(children.size()) - 1);
    for (size_t i = 0; i < children.size(); ++i)
        children[i]->row = int(i);
    node->children = std::move(children);
    endInsertRows();
}

void RemoteFileModel::refresh(const QModelIndex &folder)
{
    Node *node = nodeFor(folder);
    if (!node->isFolder())
        node = node->parent ? node->parent : m_root.get();
    node->loaded = false;
    node->loading = false;
    fetchMore(indexFor(node));
}

RemoteFileModel::Node *RemoteFileModel::findFolder(Node *node, const QString &path) const
{
    if (node->path == path)
        return node;
    for (const std::unique_ptr<Node> &child : node->children) {
        if (child->isFolder() && (child->loaded || child->loading)
            && (path == child->path || path.startsWith(child->path + QLatin1Char('/')))) {
            if (Node *found = findFolder(child.get(), path))
                return found;
        }
    }
    return nullptr;
}

void RemoteFileModel::refreshPath(const QString &folderPath)
{
    if (Node *node = findFolder(m_root.get(), folderPath))
        refresh(indexFor(node));
}

bool RemoteFileModel::containsName(const QModelIndex &folder, const QString &name) const
{
    const Node *node = nodeFor(folder);
    return std::any_of(node->children.cbegin(), node->children.cend(),
                       [&name](const std::unique_ptr<Node> &child) {
                           return child->kind == Node::Entry && child->info.name == name;
                       });
}

// --- FileTreeProxyModel ----------------------------------------------------------

void FileTreeProxyModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    m_showHidden = show;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    m_showHidden = show;
    invalidateFilter();
#endif
}

void FileTreeProxyModel::setRootPath(const QString &path)
{
    m_rootPath = QDir::cleanPath(path);
}

bool FileTreeProxyModel::isFolder(const QModelIndex &sourceIndex) const
{
    if (const auto *model = qobject_cast<const QFileSystemModel *>(sourceModel()))
        return model->isDir(sourceIndex);
    return sourceIndex.data(RemoteFileModel::IsFolderRole).toBool();
}

bool FileTreeProxyModel::isPlaceholder(const QModelIndex &sourceIndex) const
{
    if (qobject_cast<const QFileSystemModel *>(sourceModel()))
        return false;
    return sourceIndex.data(RemoteFileModel::IsPlaceholderRole).toBool();
}

QString FileTreeProxyModel::name(const QModelIndex &sourceIndex) const
{
    if (const auto *model = qobject_cast<const QFileSystemModel *>(sourceModel()))
        return model->fileName(sourceIndex);
    return sourceIndex.data(Qt::DisplayRole).toString();
}

bool FileTreeProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (m_showHidden || isPlaceholder(index) || !name(index).startsWith(QLatin1Char('.')))
        return true;
    // QFileSystemModel's tree starts at the top of the disk: never hide the
    // open folder itself or the folders above it, even if they're hidden.
    if (const auto *model = qobject_cast<const QFileSystemModel *>(sourceModel())) {
        const QString path = QDir::cleanPath(model->filePath(index));
        if (!m_rootPath.isEmpty()
            && (m_rootPath == path || m_rootPath.startsWith(path + QLatin1Char('/'))))
            return true;
    }
    return false;
}

bool FileTreeProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // Notices go last; folders before files; then by name.
    const bool leftPlaceholder = isPlaceholder(left);
    if (leftPlaceholder != isPlaceholder(right))
        return !leftPlaceholder;
    const bool leftFolder = isFolder(left);
    if (leftFolder != isFolder(right))
        return leftFolder;
    const QString a = name(left);
    const QString b = name(right);
    const int byName = a.compare(b, Qt::CaseInsensitive);
    return byName != 0 ? byName < 0 : a < b;
}
