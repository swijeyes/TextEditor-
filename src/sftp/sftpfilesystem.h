#pragma once

#include "filesystem.h"
#include "sftpconnection.h"

#include <memory>

// The files on one SFTP server. All operations go through the server's
// SftpConnection, which logs in when first needed and reconnects if the
// connection dropped.
class SftpFileSystem : public FileSystem
{
    Q_OBJECT

public:
    explicit SftpFileSystem(std::unique_ptr<SftpConnection> connection, QObject *parent = nullptr);
    ~SftpFileSystem() override;

    SftpConnection *connection() const { return m_connection.get(); }
    ServerAddress server() const { return m_connection->server(); }

    QFuture<StatResult> stat(const QString &path) override;
    QFuture<ListResult> list(const QString &folder) override;
    QFuture<ReadResult> read(const QString &path, qint64 maxBytes) override;
    QFuture<WriteResult> writeAtomic(const QString &path, const QByteArray &data,
                                     const WriteExpectation &expected) override;
    QFuture<FsStatus> rename(const QString &from, const QString &to) override;
    QFuture<FsStatus> remove(const QString &path) override;
    QFuture<FsStatus> makeFolder(const QString &path) override;

    QUrl urlFor(const QString &path) const override;
    void cancel() override;

private:
    std::unique_ptr<SftpConnection> m_connection;
};
