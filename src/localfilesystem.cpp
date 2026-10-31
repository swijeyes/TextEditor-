#include "localfilesystem.h"

#include "location.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

QString nativePath(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

FileInfo infoFor(const QFileInfo &fileInfo)
{
    FileInfo info;
    info.name = fileInfo.fileName();
    if (!fileInfo.exists())
        return info;
    info.type = fileInfo.isFile() ? FileInfo::File
              : fileInfo.isDir()  ? FileInfo::Folder
                                  : FileInfo::Other;
    info.isSymLink = fileInfo.isSymLink();
    info.size = fileInfo.size();
    info.modified = fileInfo.lastModified(QTimeZone::UTC);
#ifdef Q_OS_UNIX
    struct stat st;
    if (::stat(QFile::encodeName(fileInfo.absoluteFilePath()).constData(), &st) == 0)
        info.permissions = quint32(st.st_mode) & 07777u;
#endif
    return info;
}

} // namespace

StatResult LocalFileSystem::statNow(const QString &path)
{
    StatResult result;
    QFileInfo fileInfo(path);
    fileInfo.setCaching(false);
    result.info = infoFor(fileInfo);
    return result;
}

QString LocalFileSystem::openForReading(QFile &file, const QString &path)
{
    // On Unix the regular-file check is made on the opened file itself, so
    // the path can't be swapped for a FIFO or device after it was checked,
    // and O_NONBLOCK stops open() from waiting on a FIFO.
#ifdef Q_OS_UNIX
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return qt_error_string(errno);
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return tr("Not a regular file.");
    }
    // Reads from a regular file never block, so O_NONBLOCK can stay set.
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return file.errorString();
    }
    return {};
#else
    file.setFileName(path);
    return file.open(QIODevice::ReadOnly) ? QString() : file.errorString();
#endif
}

ReadResult LocalFileSystem::readNow(const QString &path, qint64 maxBytes)
{
    ReadResult result;
    const StatResult before = statNow(path);
    result.info = before.info;
    // Only open regular files: reading a device such as /dev/zero or a FIFO
    // would never finish.
    if (!before.info.exists()) {
        result.error = FsError::NotFound;
        result.message = tr("%1 does not exist.").arg(nativePath(path));
        return result;
    }
    if (!before.info.isFile()) {
        result.error = FsError::NotRegularFile;
        result.message = tr("%1 is not a regular file.").arg(nativePath(path));
        return result;
    }
    if (before.info.size > maxBytes) {
        result.error = FsError::TooLarge;
        result.message = tr("%1 is too large to open.").arg(nativePath(path));
        return result;
    }

    // Saving goes to the file a symbolic link points to; say so, rather
    // than following it silently.
    const QFileInfo linkInfo(path);
    if (linkInfo.isSymLink())
        result.resolvedPath = linkInfo.canonicalFilePath();

    QFile file;
    const QString openError = openForReading(file, path);
    if (!openError.isEmpty()) {
        result.error = FsError::Failed;
        result.message = tr("Cannot read %1:\n%2").arg(nativePath(path), openError);
        return result;
    }
    // Bounded read, in case the file grew after the checks above.
    result.data = file.read(maxBytes + 1);
    if (result.data.size() > maxBytes) {
        result.data.clear();
        result.error = FsError::TooLarge;
        result.message = tr("%1 is too large to open.").arg(nativePath(path));
        return result;
    }
    if (file.error() != QFileDevice::NoError) {
        result.data.clear();
        result.error = FsError::Failed;
        result.message = tr("Cannot read %1:\n%2").arg(nativePath(path), file.errorString());
        return result;
    }
    return result;
}

WriteResult LocalFileSystem::writeNow(const QString &path, const QByteArray &data,
                                      const WriteExpectation &expected)
{
    WriteResult result;
    const FileInfo current = statNow(path).info;
    if (current.exists() && !current.isFile()) {
        result.error = FsError::NotRegularFile;
        result.message = tr("%1 is not a regular file.").arg(nativePath(path));
        return result;
    }
    if (expected.mode == WriteExpectation::MustNotExist && current.exists()) {
        result.error = FsError::AlreadyExists;
        result.message = tr("%1 already exists.").arg(nativePath(path));
        return result;
    }
    if (expected.mode == WriteExpectation::MustMatch && !current.sameVersionAs(expected.info)) {
        result.error = FsError::Conflict;
        result.message = current.exists()
            ? tr("%1 has been changed by another program since it was opened.").arg(nativePath(path))
            : tr("%1 has been deleted or moved by another program since it was opened.")
                  .arg(nativePath(path));
        return result;
    }

    if (expected.mode == WriteExpectation::MustNotExist) {
        // A new file: NewOnly creates it only if nothing has appeared there
        // since the check above.
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            result.error = file.exists() ? FsError::AlreadyExists : FsError::Failed;
            result.message = tr("Cannot create %1:\n%2").arg(nativePath(path), file.errorString());
            return result;
        }
        if (file.write(data) != data.size() || !file.flush()) {
            result.error = FsError::Failed;
            result.message = tr("Cannot write %1:\n%2").arg(nativePath(path), file.errorString());
            file.close();
            file.remove();
            return result;
        }
        file.close();
        result.info = statNow(path).info;
        return result;
    }

    // QSaveFile writes to a temporary file and swaps it in, so a failed save
    // never leaves a half-written file behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) < 0 || !file.commit()) {
        result.error = FsError::Failed;
        result.message = tr("Cannot write %1:\n%2").arg(nativePath(path), file.errorString());
        return result;
    }
    result.info = statNow(path).info;
    return result;
}

QFuture<StatResult> LocalFileSystem::stat(const QString &path)
{
    return readyFuture(statNow(path));
}

QFuture<ListResult> LocalFileSystem::list(const QString &folder)
{
    ListResult result;
    if (!QFileInfo(folder).isDir()) {
        result.error = FsError::NotFound;
        result.message = tr("%1 is not a folder.").arg(nativePath(folder));
        return readyFuture(result);
    }
    QDirIterator it(folder, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        if (result.entries.size() >= FileLimits::MaxListingEntries) {
            result.truncated = true;
            break;
        }
        it.next();
        const QFileInfo fileInfo = it.fileInfo();
        if (!Location::isValidListingName(fileInfo.fileName())) {
            ++result.rejected;
            continue;
        }
        result.entries.append(infoFor(fileInfo));
    }
    return readyFuture(result);
}

QFuture<ReadResult> LocalFileSystem::read(const QString &path, qint64 maxBytes)
{
    return QtConcurrent::run([path, maxBytes] { return readNow(path, maxBytes); });
}

QFuture<WriteResult> LocalFileSystem::writeAtomic(const QString &path, const QByteArray &data,
                                                  const WriteExpectation &expected)
{
    return QtConcurrent::run([path, data, expected] { return writeNow(path, data, expected); });
}

QFuture<FsStatus> LocalFileSystem::rename(const QString &from, const QString &to)
{
    if (QFileInfo::exists(to) || QFileInfo(to).isSymLink())
        return readyFuture(FsStatus::failure(FsError::AlreadyExists,
                                             tr("%1 already exists.").arg(nativePath(to))));
    if (!QDir().rename(from, to))
        return readyFuture(FsStatus::failure(
            FsError::Failed, tr("Cannot rename %1 to %2.").arg(nativePath(from), nativePath(to))));
    return readyFuture(FsStatus());
}

QFuture<FsStatus> LocalFileSystem::remove(const QString &path)
{
    // Moving to the trash means a mistaken delete can be undone.
    QFile file(path);
    if (!file.moveToTrash())
        return readyFuture(FsStatus::failure(
            FsError::Failed,
            tr("Cannot move %1 to the trash:\n%2").arg(nativePath(path), file.errorString())));
    return readyFuture(FsStatus());
}

QFuture<FsStatus> LocalFileSystem::makeFolder(const QString &path)
{
    if (QFileInfo::exists(path))
        return readyFuture(FsStatus::failure(FsError::AlreadyExists,
                                             tr("%1 already exists.").arg(nativePath(path))));
    if (!QDir().mkdir(path))
        return readyFuture(
            FsStatus::failure(FsError::Failed, tr("Cannot create %1.").arg(nativePath(path))));
    return readyFuture(FsStatus());
}

QUrl LocalFileSystem::urlFor(const QString &path) const
{
    return Location::fromLocalPath(path);
}
