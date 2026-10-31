#include "localfilesystem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace {

template<typename T>
T wait(QFuture<T> future)
{
    future.waitForFinished();
    return future.result();
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestLocalFileSystem : public QObject
{
    Q_OBJECT

private slots:
    void init() { QVERIFY(m_dir.isValid()); }

    void readAndStat()
    {
        const QString path = m_dir.filePath(QStringLiteral("a.txt"));
        writeFile(path, "hello\n");
        LocalFileSystem fs;
        const StatResult st = wait(fs.stat(path));
        QVERIFY(st.ok());
        QVERIFY(st.info.isFile());
        QCOMPARE(st.info.size, 6);
        const ReadResult r = wait(fs.read(path, 100));
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.data, QByteArray("hello\n"));
        QVERIFY(r.info.sameVersionAs(st.info));
    }

    void missing()
    {
        LocalFileSystem fs;
        const ReadResult r = wait(fs.read(m_dir.filePath(QStringLiteral("nope")), 100));
        QCOMPARE(r.error, FsError::NotFound);
        QVERIFY(!wait(fs.stat(m_dir.filePath(QStringLiteral("nope")))).info.exists());
    }

    void sizeLimit()
    {
        const QString path = m_dir.filePath(QStringLiteral("big.txt"));
        writeFile(path, QByteArray(1000, 'x'));
        LocalFileSystem fs;
        QCOMPARE(wait(fs.read(path, 999)).error, FsError::TooLarge);
        QVERIFY(wait(fs.read(path, 1000)).ok());
    }

    void notRegular()
    {
        LocalFileSystem fs;
        QCOMPARE(wait(fs.read(m_dir.path(), 100)).error, FsError::NotRegularFile);
#ifdef Q_OS_UNIX
        const QString fifo = m_dir.filePath(QStringLiteral("fifo"));
        QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
        QCOMPARE(wait(fs.read(fifo, 100)).error, FsError::NotRegularFile);
        // Even when the stat check is skipped, opening checks the file itself
        // and doesn't block on the FIFO.
        QFile file;
        QVERIFY(!LocalFileSystem::openForReading(file, fifo).isEmpty());
        QCOMPARE(wait(fs.read(QStringLiteral("/dev/zero"), 100)).error, FsError::NotRegularFile);
#endif
    }

    void atomicSave()
    {
        const QString path = m_dir.filePath(QStringLiteral("s.txt"));
        writeFile(path, "old");
#ifdef Q_OS_UNIX
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
#endif
        LocalFileSystem fs;
        const ReadResult opened = wait(fs.read(path, 100));
        const WriteResult w = wait(fs.writeAtomic(path, "new text", WriteExpectation::mustMatch(opened.info)));
        QVERIFY2(w.ok(), qPrintable(w.message));
        QCOMPARE(readFile(path), QByteArray("new text"));
        QCOMPARE(w.info.size, 8);
#ifdef Q_OS_UNIX
        QCOMPARE(w.info.permissions, 0700u);
#endif
        // No temporary files left behind.
        const QStringList left = QDir(m_dir.path()).entryList(QDir::Files | QDir::Hidden);
        QVERIFY2(std::none_of(left.cbegin(), left.cend(),
                              [](const QString &name) { return name.startsWith(QLatin1String("s.txt.")); }),
                 qPrintable(left.join(QLatin1Char(' '))));
    }

    void conflict()
    {
        const QString path = m_dir.filePath(QStringLiteral("c.txt"));
        writeFile(path, "v1");
        LocalFileSystem fs;
        const ReadResult opened = wait(fs.read(path, 100));
        writeFile(path, "changed elsewhere");
        const WriteResult w = wait(fs.writeAtomic(path, "mine", WriteExpectation::mustMatch(opened.info)));
        QCOMPARE(w.error, FsError::Conflict);
        QCOMPARE(readFile(path), QByteArray("changed elsewhere"));
        // Overwriting deliberately works.
        QVERIFY(wait(fs.writeAtomic(path, "mine", WriteExpectation::anything())).ok());
        QCOMPARE(readFile(path), QByteArray("mine"));

        // A deleted file is a conflict too.
        const ReadResult again = wait(fs.read(path, 100));
        QVERIFY(QFile::remove(path));
        QCOMPARE(wait(fs.writeAtomic(path, "x", WriteExpectation::mustMatch(again.info))).error,
                 FsError::Conflict);
    }

    void createNew()
    {
        const QString path = m_dir.filePath(QStringLiteral("new.txt"));
        LocalFileSystem fs;
        QVERIFY(wait(fs.writeAtomic(path, "", WriteExpectation::mustNotExist())).ok());
        QVERIFY(QFile::exists(path));
        QCOMPARE(wait(fs.writeAtomic(path, "", WriteExpectation::mustNotExist())).error,
                 FsError::AlreadyExists);
    }

    void folderCommands()
    {
        LocalFileSystem fs;
        const QString folder = m_dir.filePath(QStringLiteral("sub"));
        QVERIFY(wait(fs.makeFolder(folder)).ok());
        QCOMPARE(wait(fs.makeFolder(folder)).error, FsError::AlreadyExists);
        writeFile(folder + QStringLiteral("/f.txt"), "x");
        writeFile(folder + QStringLiteral("/g.txt"), "y");
        QCOMPARE(wait(fs.rename(folder + QStringLiteral("/f.txt"), folder + QStringLiteral("/g.txt"))).error,
                 FsError::AlreadyExists);
        QVERIFY(wait(fs.rename(folder + QStringLiteral("/f.txt"), folder + QStringLiteral("/h.txt"))).ok());
        const ListResult listing = wait(fs.list(folder));
        QVERIFY(listing.ok());
        QCOMPARE(listing.entries.size(), 2);
        QVERIFY(!listing.truncated);
    }

    void listingCap()
    {
        const QString folder = m_dir.filePath(QStringLiteral("many"));
        QVERIFY(QDir().mkdir(folder));
        for (int i = 0; i < FileLimits::MaxListingEntries + 5; ++i)
            writeFile(folder + QStringLiteral("/f%1").arg(i), QByteArray());
        LocalFileSystem fs;
        const ListResult listing = wait(fs.list(folder));
        QVERIFY(listing.truncated);
        QCOMPARE(listing.entries.size(), FileLimits::MaxListingEntries);
    }

#ifdef Q_OS_UNIX
    // Saving through a symbolic link replaces the file it points to and
    // leaves the link in place.
    void symlinkSave()
    {
        const QString target = m_dir.filePath(QStringLiteral("target.txt"));
        const QString link = m_dir.filePath(QStringLiteral("link.txt"));
        writeFile(target, "t");
        QVERIFY(QFile::link(target, link));
        LocalFileSystem fs;
        const ReadResult r = wait(fs.read(link, 100));
        QVERIFY(r.ok());
        QVERIFY(wait(fs.writeAtomic(link, "through link", WriteExpectation::mustMatch(r.info))).ok());
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(readFile(target), QByteArray("through link"));
    }
#endif

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TestLocalFileSystem)
#include "tst_localfilesystem.moc"
