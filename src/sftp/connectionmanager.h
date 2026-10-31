#pragma once

#include "sftpconnection.h"

#include <QHash>
#include <QObject>

#include <memory>

class SftpFileSystem;

// Keeps one connection per user@host:port, shared by every tab, the folder
// sidebar and the remote file dialog using that server. A connection is
// closed when nothing holds its file system any more.
class ConnectionManager : public QObject
{
    Q_OBJECT

public:
    ConnectionManager(SftpPrompter *prompter, const SftpOptions &options = SftpOptions(),
                      QObject *parent = nullptr);
    ~ConnectionManager() override;

    std::shared_ptr<SftpFileSystem> fileSystem(const ServerAddress &server);
    // An existing file system for server, without creating one.
    std::shared_ptr<SftpFileSystem> existing(const ServerAddress &server) const;
    // Disconnects everything, for when the window closes.
    void shutdownAll();

    SftpOptions options() const { return m_options; }

private:
    SftpPrompter *m_prompter;
    SftpOptions m_options;
    QHash<QString, std::weak_ptr<SftpFileSystem>> m_fileSystems;
};
