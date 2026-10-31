#include "sftpfilesystem.h"

SftpFileSystem::SftpFileSystem(std::unique_ptr<SftpConnection> connection, QObject *parent)
    : FileSystem(parent)
    , m_connection(std::move(connection))
{
}

SftpFileSystem::~SftpFileSystem() = default;

QFuture<StatResult> SftpFileSystem::stat(const QString &path)
{
    return m_connection->execute<StatResult>(
        [path](SftpWorker &worker, const SftpTask &task) { return worker.stat(path, task); });
}

QFuture<ListResult> SftpFileSystem::list(const QString &folder)
{
    return m_connection->execute<ListResult>(
        [folder](SftpWorker &worker, const SftpTask &task) { return worker.list(folder, task); });
}

QFuture<ReadResult> SftpFileSystem::read(const QString &path, qint64 maxBytes)
{
    return m_connection->execute<ReadResult>([path, maxBytes](SftpWorker &worker, const SftpTask &task) {
        return worker.read(path, maxBytes, task);
    });
}

QFuture<WriteResult> SftpFileSystem::writeAtomic(const QString &path, const QByteArray &data,
                                                 const WriteExpectation &expected)
{
    return m_connection->execute<WriteResult>(
        [path, data, expected](SftpWorker &worker, const SftpTask &task) {
            return worker.writeAtomic(path, data, expected, task);
        });
}

QFuture<FsStatus> SftpFileSystem::rename(const QString &from, const QString &to)
{
    return m_connection->execute<FsStatus>(
        [from, to](SftpWorker &worker, const SftpTask &task) { return worker.rename(from, to, task); });
}

QFuture<FsStatus> SftpFileSystem::remove(const QString &path)
{
    return m_connection->execute<FsStatus>(
        [path](SftpWorker &worker, const SftpTask &task) { return worker.remove(path, task); });
}

QFuture<FsStatus> SftpFileSystem::makeFolder(const QString &path)
{
    return m_connection->execute<FsStatus>(
        [path](SftpWorker &worker, const SftpTask &task) { return worker.makeFolder(path, task); });
}

QUrl SftpFileSystem::urlFor(const QString &path) const
{
    return server().url(path);
}

void SftpFileSystem::cancel()
{
    m_connection->cancel();
}
