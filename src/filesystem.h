#pragma once

#include <QDateTime>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QUrl>

#include <optional>

// Files above the first limit need confirmation before opening; above the
// second they are refused outright, since the whole file is held in memory.
// The same limits apply to local and remote files.
namespace FileLimits {
constexpr qint64 LargeFileSize = 50LL * 1024 * 1024;
constexpr qint64 MaxFileSize = 512LL * 1024 * 1024;
// Folder listings stop here, so a huge or hostile folder can't use up memory.
constexpr int MaxListingEntries = 10000;
} // namespace FileLimits

struct FileInfo
{
    enum Type { Missing, File, Folder, Other };

    QString name;
    Type type = Missing;
    bool isSymLink = false; // the entry itself is a link; type is its target's
    qint64 size = -1;
    QDateTime modified;
    quint32 permissions = 0; // POSIX mode bits (07777), 0 if unknown

    bool exists() const { return type != Missing; }
    bool isFile() const { return type == File; }
    bool isFolder() const { return type == Folder; }
    // Size and modification time match: nobody changed the file in between.
    bool sameVersionAs(const FileInfo &other) const
    {
        return type == other.type && size == other.size && modified == other.modified;
    }
};

enum class FsError {
    None,
    NotFound,
    NotRegularFile,
    TooLarge,
    AlreadyExists,
    Conflict,       // the file changed since it was opened
    Cancelled,
    ConnectionLost, // the connection dropped; trying again reconnects
    Failed,
};

// Every result carries an error code and a message for the user. Messages
// may contain text from a server, so they are always shown as plain text.
struct FsStatus
{
    FsError error = FsError::None;
    QString message;

    bool ok() const { return error == FsError::None; }
    static FsStatus failure(FsError error, const QString &message) { return {error, message}; }
};

struct StatResult : FsStatus
{
    FileInfo info;
};

struct ListResult : FsStatus
{
    QList<FileInfo> entries;  // without "." and ".."
    bool truncated = false;   // the folder had more than MaxListingEntries
    int rejected = 0;         // names that failed Location::isValidListingName
};

struct ReadResult : FsStatus
{
    QByteArray data;
    FileInfo info;        // the file as it was when read
    QString resolvedPath; // the real file's path when the path was a symbolic link
};

struct WriteResult : FsStatus
{
    FileInfo info; // the saved file, for spotting the next conflict
};

// A future that has already finished with value.
template<typename T>
QFuture<T> readyFuture(T value)
{
    QPromise<T> promise;
    promise.start();
    promise.addResult(std::move(value));
    promise.finish();
    return promise.future();
}

// How writeAtomic checks for conflicts before replacing a file.
struct WriteExpectation
{
    enum Mode { Anything, MustNotExist, MustMatch };
    Mode mode = Anything;
    FileInfo info; // for MustMatch: the version recorded when it was opened

    static WriteExpectation anything() { return {}; }
    static WriteExpectation mustNotExist() { return {MustNotExist, {}}; }
    static WriteExpectation mustMatch(const FileInfo &info) { return {MustMatch, info}; }
};

// The operations the editor and the folder sidebar need, implemented for
// local files and for SFTP servers. Every operation is asynchronous; results
// are delivered through QFuture, and continuations attached with
// QFuture::then(context, ...) run back on the context object's thread.
class FileSystem : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual QFuture<StatResult> stat(const QString &path) = 0;
    virtual QFuture<ListResult> list(const QString &folder) = 0;
    // Reads a regular file, failing with TooLarge rather than reading more
    // than maxBytes, even if the file grows while it's read.
    virtual QFuture<ReadResult> read(const QString &path, qint64 maxBytes) = 0;
    // Replaces path with data in one step, so a failure never leaves a
    // half-written file, keeping the original's permissions.
    virtual QFuture<WriteResult> writeAtomic(const QString &path, const QByteArray &data,
                                             const WriteExpectation &expected) = 0;
    virtual QFuture<FsStatus> rename(const QString &from, const QString &to) = 0;
    // Deletes a file, or an empty folder. Local files go to the trash.
    virtual QFuture<FsStatus> remove(const QString &path) = 0;
    virtual QFuture<FsStatus> makeFolder(const QString &path) = 0;

    virtual QUrl urlFor(const QString &path) const = 0;
    // Stops whatever is running as soon as possible; it fails with Cancelled.
    virtual void cancel() {}
};
