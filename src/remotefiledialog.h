#pragma once

#include "filesystem.h"

#include <QDialog>

#include <memory>

class FileTreeProxyModel;
class QLabel;
class QLineEdit;
class QTreeView;
class RemoteFileModel;

// A file picker for a server, used by Open Remote File…, Open Remote
// Folder… and saving to a server. It browses with RemoteFileModel, so
// folders load as they are opened.
class RemoteFileDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode { OpenFile, SaveFile, ChooseFolder };
    // Returned by exec() when the user asked to save on this computer instead.
    static constexpr int SaveLocally = 2;

    RemoteFileDialog(std::shared_ptr<FileSystem> fileSystem, const QString &title,
                     const QString &startFolder, Mode mode, QWidget *parent = nullptr);

    void setFileName(const QString &name);
    QString selectedPath() const { return m_selected; }

    void accept() override;

private:
    void setFolder(const QString &path);
    void activate(const QModelIndex &index);
    QString pathAt(const QModelIndex &proxyIndex) const;
    bool isFolderAt(const QModelIndex &proxyIndex) const;

    std::shared_ptr<FileSystem> m_fileSystem;
    Mode m_mode;
    QLineEdit *m_folderEdit;
    QTreeView *m_tree;
    QLineEdit *m_nameEdit = nullptr;
    FileTreeProxyModel *m_proxy;
    RemoteFileModel *m_model = nullptr;
    QString m_folder;
    QString m_selected;
};
