#pragma once

#include "filesystem.h"

class QFile;

// Files on this computer. Reading and writing run on Qt's thread pool so a
// large file doesn't freeze the window; the other operations are quick and
// finish at once.
class LocalFileSystem : public FileSystem
{
    Q_OBJECT

public:
    using FileSystem::FileSystem;

    QFuture<StatResult> stat(const QString &path) override;
    QFuture<ListResult> list(const QString &folder) override;
    QFuture<ReadResult> read(const QString &path, qint64 maxBytes) override;
    QFuture<WriteResult> writeAtomic(const QString &path, const QByteArray &data,
                                     const WriteExpectation &expected) override;
    QFuture<FsStatus> rename(const QString &from, const QString &to) override;
    QFuture<FsStatus> remove(const QString &path) override;
    QFuture<FsStatus> makeFolder(const QString &path) override;

    QUrl urlFor(const QString &path) const override;

    // The same operations, run on the calling thread.
    static StatResult statNow(const QString &path);
    static ReadResult readNow(const QString &path, qint64 maxBytes);
    static WriteResult writeNow(const QString &path, const QByteArray &data,
                                const WriteExpectation &expected);
    // Opens path for reading and returns an error message, or an empty
    // string on success. The regular-file check is made on the opened file.
    static QString openForReading(QFile &file, const QString &path);
};
