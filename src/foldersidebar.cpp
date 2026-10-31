#include "foldersidebar.h"

#include "location.h"
#include "messagebox.h"
#include "remotefilemodel.h"

#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

// Shows a path shortened in the middle to fit, so both the start and the
// folder's own name stay visible.
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;

    void setFullText(const QString &text)
    {
        m_fullText = text;
        updateText();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }

private:
    void updateText()
    {
        setText(fontMetrics().elidedText(m_fullText, Qt::ElideMiddle, qMax(0, width() - 4)));
    }

    QString m_fullText;
};

} // namespace

FolderSidebar::FolderSidebar(QWidget *parent)
    : QDockWidget(tr("Folder"), parent)
    , m_header(new ElidedLabel(this))
    , m_stack(new QStackedWidget(this))
    , m_tree(new QTreeView(this))
    , m_connectLabel(new QLabel(this))
    , m_emptyPage(new QWidget(this))
    , m_connectPage(new QWidget(this))
    , m_proxy(new FileTreeProxyModel(this))
{
    setObjectName(QStringLiteral("folderSidebar"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_header->setTextFormat(Qt::PlainText);
    m_header->setMinimumWidth(40);
    m_header->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *menuButton = new QToolButton(this);
    menuButton->setText(QStringLiteral("⋯"));
    menuButton->setToolTip(tr("Folder options"));
    menuButton->setAutoRaise(true);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(menuButton);
    m_showHiddenAction = menu->addAction(tr("Show Hidden Files"));
    m_showHiddenAction->setCheckable(true);
    connect(m_showHiddenAction, &QAction::toggled, this, &FolderSidebar::setShowHidden);
    menu->addAction(tr("Refresh"), this, &FolderSidebar::refresh);
    menu->addSeparator();
    menu->addAction(tr("Close Folder"), this, &FolderSidebar::closeFolder);
    menuButton->setMenu(menu);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(6, 2, 2, 2);
    headerRow->addWidget(m_header, 1);
    headerRow->addWidget(menuButton);

    m_tree->setModel(m_proxy);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSortingEnabled(false);
    m_proxy->setDynamicSortFilter(true);
    m_proxy->sort(0);
    // A click opens a file or opens/closes a folder. Enter or a double-click
    // (activated) opens files only, so a double-click doesn't toggle twice.
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) { activate(index, true); });
    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex &index) { activate(index, false); });
    connect(m_tree, &QWidget::customContextMenuRequested, this, &FolderSidebar::showContextMenu);

    auto *emptyLabel = new QLabel(tr("No folder is open.\n\nUse File › Open Folder… or "
                                     "File › Connect to Server…"), m_emptyPage);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setWordWrap(true);
    auto *emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->addWidget(emptyLabel);

    m_connectLabel->setTextFormat(Qt::PlainText);
    m_connectLabel->setAlignment(Qt::AlignCenter);
    m_connectLabel->setWordWrap(true);
    auto *connectButton = new QPushButton(tr("Connect"), m_connectPage);
    connect(connectButton, &QPushButton::clicked, this, [this] { emit connectRequested(m_url); });
    auto *connectLayout = new QVBoxLayout(m_connectPage);
    connectLayout->addStretch();
    connectLayout->addWidget(m_connectLabel);
    connectLayout->addWidget(connectButton, 0, Qt::AlignCenter);
    connectLayout->addStretch();

    m_stack->addWidget(m_emptyPage);
    m_stack->addWidget(m_tree);
    m_stack->addWidget(m_connectPage);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(headerRow);
    layout->addWidget(m_stack, 1);
    setWidget(content);
    closeFolder();
}

bool FolderSidebar::showHidden() const
{
    return m_proxy->showHidden();
}

void FolderSidebar::setShowHidden(bool show)
{
    m_showHiddenAction->setChecked(show);
    if (m_proxy->showHidden() == show)
        return;
    m_proxy->setShowHidden(show);
    emit showHiddenChanged(show);
}

void FolderSidebar::setFolder(const QUrl &url)
{
    m_url = url;
    const QString location = Location::displayString(url);
    static_cast<ElidedLabel *>(m_header)->setFullText(location);
    m_header->setToolTip(plainToolTip(location));
    setWindowTitle(Location::fileName(url));
}

void FolderSidebar::closeFolder()
{
    m_proxy->setSourceModel(nullptr);
    delete m_localModel;
    m_localModel = nullptr;
    delete m_remoteModel;
    m_remoteModel = nullptr;
    m_fileSystem.reset();
    m_url.clear();
    static_cast<ElidedLabel *>(m_header)->setFullText(tr("No folder"));
    m_header->setToolTip(QString());
    setWindowTitle(tr("Folder"));
    m_stack->setCurrentWidget(m_emptyPage);
}

void FolderSidebar::openLocalFolder(const QUrl &url, std::shared_ptr<FileSystem> fileSystem)
{
    closeFolder();
    const QString path = Location::path(url);
    m_fileSystem = std::move(fileSystem);
    m_localModel = new QFileSystemModel(this);
    // Hidden files are always loaded; the proxy decides whether to show them.
    m_localModel->setFilter(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    m_localModel->setReadOnly(true);
    m_proxy->setRootPath(path);
    m_proxy->setSourceModel(m_localModel);
    const QModelIndex root = m_localModel->setRootPath(path);
    m_tree->setRootIndex(m_proxy->mapFromSource(root));
    for (int column = 1; column < m_localModel->columnCount(); ++column)
        m_tree->hideColumn(column);
    setFolder(url);
    m_stack->setCurrentWidget(m_tree);
}

void FolderSidebar::openRemoteFolder(const QUrl &url, std::shared_ptr<FileSystem> fileSystem)
{
    closeFolder();
    m_fileSystem = std::move(fileSystem);
    m_remoteModel = new RemoteFileModel(m_fileSystem, Location::path(url), this);
    m_proxy->setRootPath(QString());
    m_proxy->setSourceModel(m_remoteModel);
    m_tree->setRootIndex(QModelIndex());
    setFolder(url);
    m_stack->setCurrentWidget(m_tree);
    m_remoteModel->fetchMore(QModelIndex());
}

void FolderSidebar::showUnconnectedFolder(const QUrl &url)
{
    closeFolder();
    setFolder(url);
    m_connectLabel->setText(tr("%1\n\nThis folder is on %2.")
                                .arg(Location::displayString(url),
                                     ServerAddress::fromUrl(url).displayName()));
    m_stack->setCurrentWidget(m_connectPage);
}

void FolderSidebar::refresh()
{
    if (m_remoteModel)
        m_remoteModel->refresh();
    else if (m_localModel && hasFolder())
        openLocalFolder(m_url, m_fileSystem);
}

void FolderSidebar::refreshFolder(const QString &folderPath)
{
    // Local folders update themselves; the server sends no notifications.
    if (m_remoteModel)
        m_remoteModel->refreshPath(folderPath);
}

QString FolderSidebar::pathAt(const QModelIndex &proxyIndex) const
{
    const QModelIndex source = m_proxy->mapToSource(proxyIndex);
    if (m_localModel)
        return source.isValid() ? m_localModel->filePath(source) : Location::path(m_url);
    if (m_remoteModel)
        return m_remoteModel->path(source);
    return {};
}

QUrl FolderSidebar::urlAt(const QModelIndex &proxyIndex) const
{
    return m_fileSystem ? m_fileSystem->urlFor(pathAt(proxyIndex)) : QUrl();
}

bool FolderSidebar::isFolderAt(const QModelIndex &proxyIndex) const
{
    const QModelIndex source = m_proxy->mapToSource(proxyIndex);
    if (!source.isValid())
        return true; // the root
    if (m_localModel)
        return m_localModel->isDir(source);
    return m_remoteModel && m_remoteModel->isFolder(source);
}

QString FolderSidebar::targetFolder(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid() || isFolderAt(proxyIndex))
        return pathAt(proxyIndex);
    const QString path = pathAt(proxyIndex);
    return m_localModel ? QFileInfo(path).absolutePath() : Location::remoteParent(path);
}

void FolderSidebar::activate(const QModelIndex &index, bool clicked)
{
    const QModelIndex source = m_proxy->mapToSource(index);
    if (!source.isValid() || (m_remoteModel && m_remoteModel->isPlaceholder(source)))
        return;
    if (isFolderAt(index)) {
        if (clicked)
            m_tree->setExpanded(index, !m_tree->isExpanded(index));
        return;
    }
    emit fileActivated(urlAt(index));
}

void FolderSidebar::showContextMenu(const QPoint &pos)
{
    if (!hasFolder() || !m_fileSystem)
        return;
    QModelIndex index = m_tree->indexAt(pos);
    if (index.isValid() && m_remoteModel && m_remoteModel->isPlaceholder(m_proxy->mapToSource(index)))
        index = index.parent();
    const bool onItem = index.isValid();

    QMenu menu(this);
    menu.addAction(tr("New File…"), this, [this, index] { newFile(index); });
    menu.addAction(tr("New Folder…"), this, [this, index] { newFolder(index); });
    menu.addSeparator();
    QAction *rename = menu.addAction(tr("Rename…"), this, [this, index] { renameItem(index); });
    QAction *remove = menu.addAction(m_localModel ? tr("Move to Trash…") : tr("Delete…"), this,
                                     [this, index] { deleteItem(index); });
    rename->setEnabled(onItem);
    remove->setEnabled(onItem);
    menu.addSeparator();
    menu.addAction(tr("Copy Path"), this, [this, index] {
        QGuiApplication::clipboard()->setText(Location::displayString(urlAt(index)));
    });
    menu.addAction(tr("Refresh"), this, [this, index] {
        if (m_remoteModel)
            m_remoteModel->refresh(m_proxy->mapToSource(index));
        else
            refresh();
    });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

bool FolderSidebar::checkName(const QString &name)
{
    bool ok = Location::isValidListingName(name);
    if (m_localModel)
        ok = ok && !name.contains(QLatin1Char('\\')) && !name.contains(QLatin1Char(':'))
            && QDir::toNativeSeparators(name) == name;
    if (!ok)
        showMessage(this, QMessageBox::Warning, tr("Invalid Name"),
                    tr("\"%1\" can't be used as a name.").arg(name));
    return ok;
}

void FolderSidebar::newFile(const QModelIndex &at)
{
    const QString folder = targetFolder(at);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New File"), tr("Name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty() || !checkName(name))
        return;
    const QString path = m_localModel ? QDir(folder).filePath(name) : Location::joinRemotePath(folder, name);
    std::shared_ptr<FileSystem> fs = m_fileSystem;
    fs->writeAtomic(path, QByteArray(), WriteExpectation::mustNotExist())
        .then(this, [this, fs, folder, path](const WriteResult &result) {
            if (!result.ok()) {
                showMessage(this, QMessageBox::Warning, tr("New File"), result.message);
                return;
            }
            refreshFolder(folder);
            emit fileActivated(fs->urlFor(path));
        });
}

void FolderSidebar::newFolder(const QModelIndex &at)
{
    const QString folder = targetFolder(at);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty() || !checkName(name))
        return;
    const QString path = m_localModel ? QDir(folder).filePath(name) : Location::joinRemotePath(folder, name);
    m_fileSystem->makeFolder(path).then(this, [this, folder](const FsStatus &status) {
        if (!status.ok())
            showMessage(this, QMessageBox::Warning, tr("New Folder"), status.message);
        refreshFolder(folder);
    });
}

void FolderSidebar::renameItem(const QModelIndex &at)
{
    if (!at.isValid())
        return;
    const QString path = pathAt(at);
    const QString oldName = m_localModel ? QFileInfo(path).fileName() : Location::remoteFileName(path);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                               QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || name.isEmpty() || name == oldName || !checkName(name))
        return;
    const QString folder = m_localModel ? QFileInfo(path).absolutePath() : Location::remoteParent(path);
    const QString newPath = m_localModel ? QDir(folder).filePath(name) : Location::joinRemotePath(folder, name);
    std::shared_ptr<FileSystem> fs = m_fileSystem;
    fs->rename(path, newPath).then(this, [this, fs, folder, path, newPath](const FsStatus &status) {
        if (!status.ok()) {
            showMessage(this, QMessageBox::Warning, tr("Rename"), status.message);
            return;
        }
        refreshFolder(folder);
        emit itemRenamed(fs->urlFor(path), fs->urlFor(newPath));
    });
}

void FolderSidebar::deleteItem(const QModelIndex &at)
{
    if (!at.isValid())
        return;
    const QString path = pathAt(at);
    const QUrl url = urlAt(at);
    const QString name = Location::fileName(url);
    const QString question = m_localModel
        ? tr("Move \"%1\" to the Trash?").arg(name)
        : (isFolderAt(at) ? tr("Delete the folder \"%1\" from the server? Only empty folders can "
                               "be deleted. This can't be undone.").arg(name)
                          : tr("Delete \"%1\" from the server? This can't be undone.").arg(name));
    if (showMessage(this, QMessageBox::Warning, tr("Delete"), question,
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes) {
        return;
    }
    const QString folder = m_localModel ? QFileInfo(path).absolutePath() : Location::remoteParent(path);
    m_fileSystem->remove(path).then(this, [this, folder, url](const FsStatus &status) {
        if (!status.ok()) {
            showMessage(this, QMessageBox::Warning, tr("Delete"), status.message);
            return;
        }
        refreshFolder(folder);
        emit itemRemoved(url);
    });
}
