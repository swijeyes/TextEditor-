// SFTP integration tests. They need servers, so they are skipped unless the
// environment describes them; tests/sftp/run-integration-tests.sh starts a
// throwaway OpenSSH server and a deliberately hostile one and runs this.
//
//   TEXTEDITOR_TEST_SFTP_PORT      OpenSSH server on 127.0.0.1 (key login)
//   TEXTEDITOR_TEST_SFTP_USER      user name for it
//   TEXTEDITOR_TEST_SFTP_KEY       private key it accepts
//   TEXTEDITOR_TEST_SFTP_KEY_ENC   encrypted private key it accepts
//                                  (passphrase "test passphrase")
//   TEXTEDITOR_TEST_SFTP_DIR       a folder the server can write, which this
//                                  process can also see (same machine)
//   TEXTEDITOR_TEST_OTHER_HOSTKEY  a public host key that isn't the server's
//   TEXTEDITOR_TEST_HOSTILE_PORT   the hostile server (tests/sftp/hostile_server.py)
//   TEXTEDITOR_TEST_WEAK_PORTS     servers offering only outdated algorithms

#include "location.h"
#include "sftp/connectionmanager.h"
#include "sftp/sftpfilesystem.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace {

class ScriptedPrompter : public SftpPrompter
{
public:
    bool trustHostKey(const HostKeyInfo &info) override
    {
        ++trustCalls;
        lastHostKey = info;
        return trust;
    }
    void reportChangedHostKey(const HostKeyInfo &info) override
    {
        ++changedCalls;
        lastHostKey = info;
    }
    bool askSecrets(const ServerAddress &, const AuthPrompt &prompt, QStringList *answers) override
    {
        prompts.append(prompt);
        if (secrets.isEmpty())
            return false;
        *answers = secrets.takeFirst();
        return true;
    }

    bool trust = true;
    int trustCalls = 0;
    int changedCalls = 0;
    HostKeyInfo lastHostKey;
    QList<AuthPrompt> prompts;
    QList<QStringList> secrets; // one entry per expected prompt
};

template<typename T>
T await(QFuture<T> future, int timeout = 60000)
{
    (void)QTest::qWaitFor([&future] { return future.isFinished(); }, timeout);
    T result;
    if (!future.isFinished()) {
        result.error = FsError::Failed;
        result.message = QStringLiteral("test timed out");
    } else if (future.resultCount() == 0) {
        result.error = FsError::Cancelled;
        result.message = QStringLiteral("future cancelled");
    } else {
        result = future.result();
    }
    return result;
}

QString env(const char *name)
{
    return qEnvironmentVariable(name);
}

void writeLocal(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(data);
}

QByteArray readLocal(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestSftp : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_port = env("TEXTEDITOR_TEST_SFTP_PORT").toInt();
        m_hostilePort = env("TEXTEDITOR_TEST_HOSTILE_PORT").toInt();
        m_dir = env("TEXTEDITOR_TEST_SFTP_DIR");
        QVERIFY(m_temp.isValid());
    }

    void init()
    {
        // Each test starts with an empty known_hosts.
        m_manager.reset();
        m_knownHosts = m_temp.filePath(QStringLiteral("known_hosts_%1").arg(++m_counter));
        m_prompter = ScriptedPrompter();
    }

    // --- Host keys ---------------------------------------------------------

    void unknownHostIsTrustedOnce()
    {
        requireServer();
        {
            ConnectionManager manager(&m_prompter, keyOptions());
            auto fs = manager.fileSystem(server());
            const StatResult st = await(fs->stat(m_dir));
            QVERIFY2(st.ok(), qPrintable(st.message));
            QCOMPARE(m_prompter.trustCalls, 1);
            QVERIFY(m_prompter.lastHostKey.fingerprint.startsWith(QLatin1String("SHA256:")));
            QVERIFY(!m_prompter.lastHostKey.keyType.isEmpty());
        }
        QVERIFY(readLocal(m_knownHosts).contains("127.0.0.1"));
        // Now known: no question the second time.
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        QVERIFY(await(fs->stat(m_dir)).ok());
        QCOMPARE(m_prompter.trustCalls, 1);
    }

    void declinedHostKey()
    {
        requireServer();
        m_prompter.trust = false;
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        const StatResult st = await(fs->stat(m_dir));
        QCOMPARE(st.error, FsError::Cancelled);
        QVERIFY(!QFile::exists(m_knownHosts) || readLocal(m_knownHosts).isEmpty());
    }

    void changedHostKeyIsRefused()
    {
        requireServer();
        const QString other = env("TEXTEDITOR_TEST_OTHER_HOSTKEY");
        if (other.isEmpty())
            QSKIP("TEXTEDITOR_TEST_OTHER_HOSTKEY not set");
        writeLocal(m_knownHosts, QStringLiteral("[127.0.0.1]:%1 %2\n").arg(m_port).arg(other).toUtf8());
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        const StatResult st = await(fs->stat(m_dir));
        QVERIFY(!st.ok());
        QCOMPARE(m_prompter.changedCalls, 1);
        QCOMPARE(m_prompter.trustCalls, 0); // no "connect anyway"
        QVERIFY(m_prompter.prompts.isEmpty()); // never got as far as logging in
        // known_hosts is left alone.
        QCOMPARE(readLocal(m_knownHosts), QStringLiteral("[127.0.0.1]:%1 %2\n").arg(m_port).arg(other).toUtf8());
        QVERIFY(!fs->connection()->isConnected());
    }

    // Servers that offer only CBC ciphers, SHA-1 MACs, SHA-1 key exchange or
    // SHA-1 RSA signatures are refused before any host key question.
    void weakAlgorithmsAreRefused()
    {
        const QStringList ports = env("TEXTEDITOR_TEST_WEAK_PORTS").split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (ports.isEmpty())
            QSKIP("TEXTEDITOR_TEST_WEAK_PORTS not set");
        for (const QString &port : ports) {
            ScriptedPrompter prompter;
            ConnectionManager manager(&prompter, keyOptions());
            auto fs = manager.fileSystem({QStringLiteral("127.0.0.1"), port.toInt(), env("TEXTEDITOR_TEST_SFTP_USER")});
            const StatResult st = await(fs->stat(m_dir));
            QVERIFY2(!st.ok(), qPrintable(QStringLiteral("connected to weak server on port ") + port));
            QCOMPARE(prompter.trustCalls, 0);
        }
    }

    // --- Logging in ----------------------------------------------------------

    void encryptedKeyAsksForPassphrase()
    {
        requireServer();
        const QString key = env("TEXTEDITOR_TEST_SFTP_KEY_ENC");
        if (key.isEmpty())
            QSKIP("TEXTEDITOR_TEST_SFTP_KEY_ENC not set");
        SftpOptions options = keyOptions();
        options.identityFiles = {key};
        m_prompter.secrets = {{QStringLiteral("wrong")}, {QStringLiteral("test passphrase")}};
        ConnectionManager manager(&m_prompter, options);
        auto fs = manager.fileSystem(server());
        const StatResult st = await(fs->stat(m_dir));
        QVERIFY2(st.ok(), qPrintable(st.message));
        QCOMPARE(m_prompter.prompts.size(), 2);
        QCOMPARE(m_prompter.prompts.at(0).kind, AuthPrompt::Passphrase);
        QVERIFY(m_prompter.prompts.at(1).retryMessage.contains(QLatin1String("Wrong")));
    }

    void passwordLogin()
    {
        requireHostile();
        m_prompter.secrets = {{QStringLiteral("nope")}, {QStringLiteral("secret")}};
        ConnectionManager manager(&m_prompter, hostileOptions());
        auto fs = manager.fileSystem(hostile(QStringLiteral("pw")));
        const StatResult st = await(fs->stat(QStringLiteral("/small.txt")));
        QVERIFY2(st.ok(), qPrintable(st.message));
        QCOMPARE(m_prompter.prompts.size(), 2);
        QCOMPARE(m_prompter.prompts.at(0).kind, AuthPrompt::Password);
        QVERIFY(!m_prompter.prompts.at(1).retryMessage.isEmpty());
    }

    void keyboardInteractiveLogin()
    {
        requireHostile();
        m_prompter.secrets = {{QStringLiteral("secret"), QStringLiteral("42")}};
        ConnectionManager manager(&m_prompter, hostileOptions());
        auto fs = manager.fileSystem(hostile(QStringLiteral("kbd")));
        const StatResult st = await(fs->stat(QStringLiteral("/small.txt")));
        QVERIFY2(st.ok(), qPrintable(st.message));
        QCOMPARE(m_prompter.prompts.size(), 1);
        const AuthPrompt prompt = m_prompter.prompts.constFirst();
        QCOMPARE(prompt.kind, AuthPrompt::KeyboardInteractive);
        QCOMPARE(prompt.prompts.size(), 2);
        QCOMPARE(prompt.echo, (QList<bool>{false, true}));
        // The server's control characters were removed.
        QVERIFY(!prompt.instruction.contains(QChar(0x1b)));
    }

    void cancelledLogin()
    {
        requireHostile();
        ConnectionManager manager(&m_prompter, hostileOptions());
        auto fs = manager.fileSystem(hostile(QStringLiteral("pw")));
        QCOMPARE(await(fs->stat(QStringLiteral("/small.txt"))).error, FsError::Cancelled);
    }

    // --- Files ----------------------------------------------------------------

    void openEditSaveKeepsPermissions()
    {
        requireServer();
        const QString local = m_dir + QStringLiteral("/perm.txt");
        writeLocal(local, "line 1\r\nline 2\r\n");
#ifdef Q_OS_UNIX
        QVERIFY(::chmod(QFile::encodeName(local).constData(), 0640) == 0);
#endif
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        const ReadResult r = await(fs->read(local, FileLimits::MaxFileSize));
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.data, QByteArray("line 1\r\nline 2\r\n"));
        QVERIFY(r.resolvedPath.isEmpty());

        const WriteResult w = await(fs->writeAtomic(local, "edited\r\n", WriteExpectation::mustMatch(r.info)));
        QVERIFY2(w.ok(), qPrintable(w.message));
        QCOMPARE(readLocal(local), QByteArray("edited\r\n"));
#ifdef Q_OS_UNIX
        struct stat st;
        QVERIFY(::stat(QFile::encodeName(local).constData(), &st) == 0);
        QCOMPARE(int(st.st_mode & 07777), 0640);
#endif
        QCOMPARE(w.info.permissions, 0640u);
        QVERIFY(leftoverTempFiles().isEmpty());
    }

    void fallbackSaveWithoutPosixRename()
    {
        requireServer();
        const QString local = m_dir + QStringLiteral("/fallback.txt");
        writeLocal(local, "before");
#ifdef Q_OS_UNIX
        QVERIFY(::chmod(QFile::encodeName(local).constData(), 0600) == 0);
#endif
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        QVERIFY(await(fs->stat(m_dir)).ok());
        fs->connection()->setPosixRenameAllowedForTesting(false);
        const ReadResult r = await(fs->read(local, 1000));
        const WriteResult w = await(fs->writeAtomic(local, "after", WriteExpectation::mustMatch(r.info)));
        QVERIFY2(w.ok(), qPrintable(w.message));
        QCOMPARE(readLocal(local), QByteArray("after"));
        QCOMPARE(w.info.permissions, 0600u);
        QVERIFY(leftoverTempFiles().isEmpty());
    }

    void newFileAndFolderCommands()
    {
        requireServer();
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        const QString folder = m_dir + QStringLiteral("/made");
        QVERIFY(await(fs->makeFolder(folder)).ok());
        QCOMPARE(await(fs->makeFolder(folder)).error, FsError::AlreadyExists);
        const QString file = folder + QStringLiteral("/new.txt");
        QVERIFY(await(fs->writeAtomic(file, QByteArray(), WriteExpectation::mustNotExist())).ok());
        QCOMPARE(await(fs->writeAtomic(file, QByteArray(), WriteExpectation::mustNotExist())).error,
                 FsError::AlreadyExists);
        const QString renamed = folder + QStringLiteral("/renamed.txt");
        QVERIFY(await(fs->rename(file, renamed)).ok());
        // Renaming onto an existing file is refused rather than replacing it.
        QVERIFY(await(fs->writeAtomic(file, "x", WriteExpectation::mustNotExist())).ok());
        QCOMPARE(await(fs->rename(file, renamed)).error, FsError::AlreadyExists);
        const ListResult listing = await(fs->list(folder));
        QVERIFY(listing.ok());
        QCOMPARE(listing.entries.size(), 2);
        QVERIFY(!await(fs->remove(folder)).ok()); // not empty
        QVERIFY(await(fs->remove(file)).ok());
        QVERIFY(await(fs->remove(renamed)).ok());
        QVERIFY(await(fs->remove(folder)).ok());
        QVERIFY(!QFileInfo::exists(folder));
    }

    void conflictWhenChangedOnServer()
    {
        requireServer();
        const QString local = m_dir + QStringLiteral("/conflict.txt");
        writeLocal(local, "v1");
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        const ReadResult r = await(fs->read(local, 1000));
        QVERIFY(r.ok());
        QTest::qWait(1100); // SFTP times have one-second resolution
        writeLocal(local, "changed by someone else");
        const WriteResult w = await(fs->writeAtomic(local, "mine", WriteExpectation::mustMatch(r.info)));
        QCOMPARE(w.error, FsError::Conflict);
        QCOMPARE(readLocal(local), QByteArray("changed by someone else"));
        QVERIFY(leftoverTempFiles().isEmpty());
    }

    void reconnectsAfterDrop()
    {
        requireServer();
        const QString local = m_dir + QStringLiteral("/drop.txt");
        writeLocal(local, "v1");
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        const ReadResult r = await(fs->read(local, 1000));
        QVERIFY(r.ok());
        fs->connection()->dropConnectionForTesting();
        const WriteResult w = await(fs->writeAtomic(local, "saved after drop", WriteExpectation::mustMatch(r.info)));
        QVERIFY2(w.ok(), qPrintable(w.message));
        QCOMPARE(readLocal(local), QByteArray("saved after drop"));
        QVERIFY(fs->connection()->isConnected());
    }

    void listingCap()
    {
        requireServer();
        const QString folder = m_dir + QStringLiteral("/many");
        QVERIFY(QDir().mkpath(folder));
        for (int i = 0; i < FileLimits::MaxListingEntries + 10; ++i)
            writeLocal(folder + QStringLiteral("/f%1").arg(i), QByteArray());
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        const ListResult listing = await(fs->list(folder));
        QVERIFY2(listing.ok(), qPrintable(listing.message));
        QVERIFY(listing.truncated);
        QCOMPARE(listing.entries.size(), FileLimits::MaxListingEntries);
    }

#ifdef Q_OS_UNIX
    void symbolicLinks()
    {
        requireServer();
        const QString target = m_dir + QStringLiteral("/real.txt");
        const QString link = m_dir + QStringLiteral("/link.txt");
        writeLocal(target, "real");
        QFile::remove(link);
        QVERIFY(QFile::link(target, link));
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());

        const ReadResult r = await(fs->read(link, 1000));
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.data, QByteArray("real"));
        QCOMPARE(QFileInfo(r.resolvedPath).canonicalFilePath(), QFileInfo(target).canonicalFilePath());

        // Writing to the link itself is refused, never silently followed...
        QCOMPARE(await(fs->writeAtomic(link, "x", WriteExpectation::anything())).error,
                 FsError::NotRegularFile);
        // ...while saving to the resolved file keeps the link working.
        QVERIFY(await(fs->writeAtomic(r.resolvedPath, "via link", WriteExpectation::mustMatch(r.info))).ok());
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(readLocal(link), QByteArray("via link"));

        const ListResult listing = await(fs->list(m_dir));
        const auto it = std::find_if(listing.entries.cbegin(), listing.entries.cend(),
                                     [](const FileInfo &info) { return info.name == QLatin1String("link.txt"); });
        QVERIFY(it != listing.entries.cend());
        QVERIFY(it->isSymLink);
        QVERIFY(it->isFile());
    }

    void notRegularFile()
    {
        requireServer();
        const QString fifo = m_dir + QStringLiteral("/fifo");
        QFile::remove(fifo);
        QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        QCOMPARE(await(fs->read(fifo, 1000)).error, FsError::NotRegularFile);
        QCOMPARE(await(fs->read(m_dir, 1000)).error, FsError::NotRegularFile);
    }
#endif

    void sizeLimit()
    {
        requireServer();
        const QString local = m_dir + QStringLiteral("/big.txt");
        writeLocal(local, QByteArray(5000, 'x'));
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        QCOMPARE(await(fs->read(local, 4999)).error, FsError::TooLarge);
        QCOMPARE(await(fs->read(local, 5000)).data.size(), 5000);
    }

    void homeFolder()
    {
        requireServer();
        ConnectionManager manager(&m_prompter, keyOptions());
        auto fs = manager.fileSystem(server());
        const StatResult st = await(fs->stat(QStringLiteral("~")));
        QVERIFY2(st.ok(), qPrintable(st.message));
        QVERIFY(st.info.isFolder());
        QVERIFY(fs->connection()->homeFolder().startsWith(QLatin1Char('/')));
    }

    // --- A hostile server --------------------------------------------------------

    void hostileNamesAreRejected()
    {
        requireHostile();
        auto fs = hostileFs();
        const ListResult listing = await(fs->list(QStringLiteral("/bad")));
        QVERIFY2(listing.ok(), qPrintable(listing.message));
        QStringList names;
        for (const FileInfo &info : listing.entries)
            names << info.name;
        // libssh hands names over as C strings, so "nul\0name" arrives cut
        // short at the NUL, as "nul": still a plain name inside the folder.
        names.removeAll(QStringLiteral("nul"));
        QCOMPARE(names, QStringList{QStringLiteral("ok.txt")});
        QCOMPARE(listing.rejected, 3);
    }

    void hostileHugeListing()
    {
        requireHostile();
        auto fs = hostileFs();
        const ListResult listing = await(fs->list(QStringLiteral("/huge")));
        QVERIFY2(listing.ok(), qPrintable(listing.message));
        QVERIFY(listing.truncated);
        QCOMPARE(listing.entries.size(), FileLimits::MaxListingEntries);
    }

    void hostileGrowingFile()
    {
        requireHostile();
        auto fs = hostileFs();
        // It claims to be 10 bytes but never ends.
        const ReadResult r = await(fs->read(QStringLiteral("/grow.txt"), 1024 * 1024));
        QCOMPARE(r.error, FsError::TooLarge);
        QVERIFY(r.data.isEmpty());
    }

    void hostileSlowReplyCanBeCancelled()
    {
        requireHostile();
        auto fs = hostileFs();
        QVERIFY(await(fs->stat(QStringLiteral("/small.txt"))).ok());
        QFuture<ReadResult> future = fs->read(QStringLiteral("/slow.txt"), 1000);
        QTest::qWait(500);
        QElapsedTimer timer;
        timer.start();
        fs->cancel();
        const ReadResult r = await(future, 10000);
        QCOMPARE(r.error, FsError::Cancelled);
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
        // And the connection recovers.
        m_prompter.secrets = {{QStringLiteral("secret")}};
        const StatResult st = await(fs->stat(QStringLiteral("/small.txt")));
        QVERIFY2(st.ok(), qPrintable(st.message));
    }

    void hostileSlowReplyTimesOut()
    {
        requireHostile();
        SftpOptions options = hostileOptions();
        options.timeoutSeconds = 2;
        m_prompter.secrets = {{QStringLiteral("secret")}};
        ConnectionManager manager(&m_prompter, options);
        auto fs = manager.fileSystem(hostile(QStringLiteral("pw")));
        QVERIFY(await(fs->stat(QStringLiteral("/small.txt"))).ok());
        QElapsedTimer timer;
        timer.start();
        const ReadResult r = await(fs->read(QStringLiteral("/slow.txt"), 1000), 60000);
        QVERIFY(!r.ok());
        QVERIFY2(timer.elapsed() < 20000, qPrintable(QString::number(timer.elapsed())));
    }

private:
    void requireServer()
    {
        if (!m_port || m_dir.isEmpty())
            QSKIP("No SFTP test server (see tests/sftp/run-integration-tests.sh)");
    }
    void requireHostile()
    {
        if (!m_hostilePort)
            QSKIP("No hostile SFTP test server (see tests/sftp/run-integration-tests.sh)");
    }
    ServerAddress server() const
    {
        return {QStringLiteral("127.0.0.1"), m_port, env("TEXTEDITOR_TEST_SFTP_USER")};
    }
    ServerAddress hostile(const QString &user) const
    {
        return {QStringLiteral("127.0.0.1"), m_hostilePort, user};
    }
    SftpOptions keyOptions() const
    {
        SftpOptions options;
        options.knownHostsFile = m_knownHosts;
        options.processConfig = false;
        options.useAgent = false;
        options.identityFiles = {env("TEXTEDITOR_TEST_SFTP_KEY")};
        options.keepaliveSeconds = 0;
        return options;
    }
    SftpOptions hostileOptions() const
    {
        SftpOptions options = keyOptions();
        options.identityFiles = {QStringLiteral("/nonexistent")};
        return options;
    }
    std::shared_ptr<SftpFileSystem> hostileFs()
    {
        m_prompter.secrets = {{QStringLiteral("secret")}};
        m_manager.emplace(&m_prompter, hostileOptions());
        return m_manager->fileSystem(hostile(QStringLiteral("pw")));
    }
    QStringList leftoverTempFiles() const
    {
        return QDir(m_dir).entryList({QStringLiteral(".*.tetmp-*"), QStringLiteral(".*.tebak-*")},
                                     QDir::Files | QDir::Hidden);
    }

    int m_port = 0;
    int m_hostilePort = 0;
    int m_counter = 0;
    QString m_dir;
    QString m_knownHosts;
    QTemporaryDir m_temp;
    ScriptedPrompter m_prompter;
    std::optional<ConnectionManager> m_manager;
};

QTEST_GUILESS_MAIN(TestSftp)
#include "tst_sftp.moc"
