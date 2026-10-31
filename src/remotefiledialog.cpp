#include "remotefiledialog.h"

#include "location.h"
#include "messagebox.h"
#include "remotefilemodel.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

RemoteFileDialog::RemoteFileDialog(std::shared_ptr<FileSystem> fileSystem, const QString &title,
                                   const QString &startFolder, Mode mode, QWidget *parent)
    : QDialog(parent)
    , m_fileSystem(std::move(fileSystem))
    , m_mode(mode)
    , m_folderEdit(new QLineEdit(this))
    , m_tree(new QTreeView(this))
    , m_proxy(new FileTreeProxyModel(this))
{
    setWindowTitle(title);

    auto *upButton = new QPushButton(tr("Up"), this);
    upButton->setToolTip(tr("Go to the enclosing folder"));
    connect(upButton, &QPushButton::clicked, this,
            [this] { setFolder(Location::remoteParent(m_folder)); });
    connect(m_folderEdit, &QLineEdit::returnPressed, this, [this] {
        const QString path = m_folderEdit->text().trimmed();
        if (!path.isEmpty() && !path.contains(QChar(0)))
            setFolder(path);
    });
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(new QLabel(tr("Folder:"), this));
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(upButton);

    m_tree->setModel(m_proxy);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_proxy->setDynamicSortFilter(true);
    m_proxy->sort(0);
    connect(m_tree, &QTreeView::activated, this, &RemoteFileDialog::activate);

    auto *showHidden = new QCheckBox(tr("Show hidden files"), this);
    connect(showHidden, &QCheckBox::toggled, m_proxy, &FileTreeProxyModel::setShowHidden);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(folderRow);
    layout->addWidget(m_tree, 1);
    layout->addWidget(showHidden);

    if (mode == SaveFile) {
        m_nameEdit = new QLineEdit(this);
        auto *form = new QFormLayout;
        form->addRow(tr("Name:"), m_nameEdit);
        layout->addLayout(form);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    const QString acceptText = mode == OpenFile ? tr("Open") : mode == SaveFile ? tr("Save") : tr("Choose");
    buttons->addButton(acceptText, QDialogButtonBox::AcceptRole)->setDefault(true);
    if (mode == SaveFile) {
        QPushButton *local = buttons->addButton(tr("This Computer…"), QDialogButtonBox::ActionRole);
        local->setToolTip(tr("Save on this computer instead"));
        connect(local, &QPushButton::clicked, this, [this] { done(SaveLocally); });
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &RemoteFileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    setFolder(startFolder);
    resize(560, 460);
}

void RemoteFileDialog::setFileName(const QString &name)
{
    if (m_nameEdit) {
        m_nameEdit->setText(name);
        m_nameEdit->selectAll();
        m_nameEdit->setFocus();
    }
}

void RemoteFileDialog::setFolder(const QString &path)
{
    m_folder = path;
    m_folderEdit->setText(path);
    RemoteFileModel *old = m_model;
    m_model = new RemoteFileModel(m_fileSystem, path, this);
    m_proxy->setSourceModel(m_model);
    delete old;
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current) {
                if (m_nameEdit && current.isValid() && !isFolderAt(current))
                    m_nameEdit->setText(Location::remoteFileName(pathAt(current)));
            });
    m_model->fetchMore(QModelIndex());
}

QString RemoteFileDialog::pathAt(const QModelIndex &proxyIndex) const
{
    return m_model->path(m_proxy->mapToSource(proxyIndex));
}

bool RemoteFileDialog::isFolderAt(const QModelIndex &proxyIndex) const
{
    return m_model->isFolder(m_proxy->mapToSource(proxyIndex));
}

void RemoteFileDialog::activate(const QModelIndex &index)
{
    if (!index.isValid() || m_model->isPlaceholder(m_proxy->mapToSource(index)))
        return;
    if (isFolderAt(index)) {
        setFolder(pathAt(index));
        return;
    }
    if (m_nameEdit)
        m_nameEdit->setText(Location::remoteFileName(pathAt(index)));
    accept();
}

void RemoteFileDialog::accept()
{
    const QModelIndex current = m_tree->currentIndex();
    const bool hasItem = current.isValid() && !m_model->isPlaceholder(m_proxy->mapToSource(current));

    switch (m_mode) {
    case OpenFile:
        if (!hasItem)
            return;
        if (isFolderAt(current)) {
            setFolder(pathAt(current));
            return;
        }
        m_selected = pathAt(current);
        break;
    case ChooseFolder:
        m_selected = hasItem && isFolderAt(current) ? pathAt(current) : m_folder;
        break;
    case SaveFile: {
        const QString name = m_nameEdit->text().trimmed();
        if (name.isEmpty())
            return;
        if (!Location::isValidListingName(name)) {
            showMessage(this, QMessageBox::Warning, windowTitle(),
                        tr("\"%1\" can't be used as a file name.").arg(name));
            return;
        }
        if (m_model->containsName(QModelIndex(), name)
            && showMessage(this, QMessageBox::Warning, windowTitle(),
                           tr("\"%1\" already exists. Do you want to replace it?").arg(name),
                           QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
                   != QMessageBox::Yes) {
            return;
        }
        m_selected = Location::joinRemotePath(m_folder, name);
        break;
    }
    }
    QDialog::accept();
}
