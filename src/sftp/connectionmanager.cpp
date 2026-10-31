#include "connectionmanager.h"

#include "sftpfilesystem.h"

ConnectionManager::ConnectionManager(SftpPrompter *prompter, const SftpOptions &options,
                                     QObject *parent)
    : QObject(parent)
    , m_prompter(prompter)
    , m_options(options)
{
}

ConnectionManager::~ConnectionManager()
{
    shutdownAll();
}

std::shared_ptr<SftpFileSystem> ConnectionManager::existing(const ServerAddress &server) const
{
    return m_fileSystems.value(server.key()).lock();
}

std::shared_ptr<SftpFileSystem> ConnectionManager::fileSystem(const ServerAddress &server)
{
    if (std::shared_ptr<SftpFileSystem> fs = existing(server))
        return fs;

    auto connection = std::make_unique<SftpConnection>(server, m_options, m_prompter);
    // Deleted later rather than at once: the last holder may be releasing
    // it from inside one of its own continuations.
    std::shared_ptr<SftpFileSystem> fs(new SftpFileSystem(std::move(connection)),
                                       [](SftpFileSystem *p) {
                                           p->connection()->shutdown();
                                           p->deleteLater();
                                       });
    m_fileSystems.insert(server.key(), fs);
    return fs;
}

void ConnectionManager::shutdownAll()
{
    for (const std::weak_ptr<SftpFileSystem> &weak : std::as_const(m_fileSystems)) {
        if (std::shared_ptr<SftpFileSystem> fs = weak.lock())
            fs->connection()->shutdown();
    }
    m_fileSystems.clear();
}
