#pragma once

#include "filesystem.h"
#include "location.h"

#include <QFuture>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QPromise>
#include <QStringList>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

class QThread;
class QTimer;
class SftpWorker;

// A server's host key, as shown to the user.
struct HostKeyInfo
{
    ServerAddress server;
    QString keyType;     // e.g. "ssh-ed25519"
    QString fingerprint; // "SHA256:..."
    QString knownHostsFile;
};

// A request for something secret: a password, a key's passphrase, or the
// answers to a server's keyboard-interactive questions. The texts come from
// the server or the key file name, so they are shown as plain text.
struct AuthPrompt
{
    enum Kind { Password, Passphrase, KeyboardInteractive };
    Kind kind = Password;
    QString keyFile;     // Passphrase: the key file
    QString name;        // KeyboardInteractive: the server's title and
    QString instruction; // instructions, already sanitised
    QStringList prompts;
    QList<bool> echo;    // whether each answer may be shown as it's typed
    QString retryMessage; // e.g. "Wrong password." on a second try
};

// Asks the user about host keys and secrets while connecting. Called on the
// UI thread; the GUI version shows dialogs, tests answer from a script.
class SftpPrompter
{
public:
    virtual ~SftpPrompter() = default;
    // An unknown server: trust its key (and add it to known_hosts)?
    virtual bool trustHostKey(const HostKeyInfo &info) = 0;
    // The server's key has changed. The connection is refused; this only
    // tells the user why.
    virtual void reportChangedHostKey(const HostKeyInfo &info) = 0;
    // Returns false if the user cancelled. Answers are cleared by the caller
    // after use.
    virtual bool askSecrets(const ServerAddress &server, const AuthPrompt &prompt,
                            QStringList *answers) = 0;
};

struct SftpOptions
{
    QString knownHostsFile; // empty: ~/.ssh/known_hosts
    QString sshDir;         // empty: ~/.ssh
    QString configFile;     // empty: ~/.ssh/config
    bool processConfig = true;
    bool useAgent = true;
    QStringList identityFiles; // empty: from ~/.ssh/config or the standard ones
    int timeoutSeconds = 30;
    int keepaliveSeconds = 30;
};

// One step of connecting and logging in, as reported by the worker. A
// failure is reported through the FsStatus part.
struct ConnectStep : FsStatus
{
    enum Kind { Connected, UnknownHost, ChangedHost, NeedSecrets };
    Kind kind = Connected;
    HostKeyInfo hostKey;
    AuthPrompt prompt;
    QString home; // Connected: the home folder
};

struct ConnectAnswer
{
    bool cancelled = false;
    bool trustHost = false;
    QString fingerprint; // the fingerprint the user was shown
    QStringList secrets;
};

// Passed to every operation running on the worker thread.
struct SftpTask
{
    std::function<bool()> isCancelled;
    std::function<void(qint64 done, qint64 total)> setProgress;
};

// A connection to one server. libssh sessions aren't thread-safe, so each
// connection has its own worker thread and every libssh call happens there.
// This object lives on the UI thread: operations are posted to the worker
// and return QFutures whose continuations run back on the UI thread.
class SftpConnection : public QObject
{
    Q_OBJECT

public:
    SftpConnection(const ServerAddress &server, const SftpOptions &options,
                   SftpPrompter *prompter, QObject *parent = nullptr);
    ~SftpConnection() override;

    ServerAddress server() const { return m_server; }
    bool isConnected() const { return m_connected; }
    // The home folder, known once connected.
    QString homeFolder() const { return m_home; }

    // Connects and logs in if needed, asking the prompter as necessary.
    // Several callers share one attempt.
    QFuture<FsStatus> ensureConnected();

    // Connects if needed, then runs op on the worker thread. If the
    // connection had dropped, it reconnects and tries once more.
    template<typename R>
    QFuture<R> execute(std::function<R(SftpWorker &, const SftpTask &)> op);

    // Stops the running operation and everything queued: the socket is
    // shut down, so even a blocked read returns at once.
    void cancel();
    // Disconnects and stops the worker thread. Called by the destructor.
    void shutdown();

    // Test hooks: cut the connection as a network failure would, and
    // pretend the server lacks posix-rename@openssh.com.
    void dropConnectionForTesting();
    void setPosixRenameAllowedForTesting(bool allowed);

signals:
    void connectedChanged(bool connected);

private:
    template<typename R>
    QFuture<R> post(std::function<R(SftpWorker &, const SftpTask &)> op);
    template<typename R>
    void executeInto(std::shared_ptr<QPromise<R>> promise,
                     std::function<R(SftpWorker &, const SftpTask &)> op, bool retry);
    void handleStep(const ConnectStep &step, int attempt);
    void postAnswer(const ConnectAnswer &answer, int attempt);
    void finishConnecting(const FsStatus &status);
    void setConnected(bool connected);

    ServerAddress m_server;
    SftpPrompter *m_prompter;
    QThread *m_thread;
    SftpWorker *m_worker;
    bool m_connected = false;
    bool m_shutDown = false;
    QString m_home;
    int m_attempt = 0; // so a cancelled attempt's late replies are ignored
    std::shared_ptr<QPromise<FsStatus>> m_connecting;
    QFuture<FsStatus> m_connectingFuture;
};

// The libssh side of a connection. Lives on the worker thread; only
// cancel() may be called from another thread.
class SftpWorker : public QObject
{
    Q_OBJECT

public:
    SftpWorker(const ServerAddress &server, const SftpOptions &options);
    ~SftpWorker() override;

    // Bumped by cancel(); operations started before it fail with Cancelled.
    quint64 generation() const { return m_generation.load(); }
    void cancel(); // thread-safe
    // Queues a task to run on the worker thread (thread-safe). Tasks pass
    // through a std::mutex, so the hand-over is visible to ThreadSanitizer;
    // a queued call only wakes the thread up.
    void enqueue(std::function<void()> task);

    ConnectStep start();
    ConnectStep answer(const ConnectAnswer &answer);
    void disconnectFromServer();
    bool isConnected() const;
    QString homeFolder() const { return m_home; }

    StatResult stat(const QString &path, const SftpTask &task);
    ListResult list(const QString &folder, const SftpTask &task);
    ReadResult read(const QString &path, qint64 maxBytes, const SftpTask &task);
    WriteResult writeAtomic(const QString &path, const QByteArray &data,
                            const WriteExpectation &expected, const SftpTask &task);
    FsStatus rename(const QString &from, const QString &to, const SftpTask &task);
    FsStatus remove(const QString &path, const SftpTask &task);
    FsStatus makeFolder(const QString &path, const SftpTask &task);

    // Test hooks; see SftpConnection.
    void dropConnectionForTesting(); // thread-safe
    void setPosixRenameAllowed(bool allowed) { m_posixRenameAllowed = allowed; }

signals:
    void connectionLost();

private:
    enum class AuthPhase {
        None, HostKey, Agent, Keys, KeyPassphrase, KbdInt, KbdIntAnswer, Password,
        PasswordAnswer, Failed
    };
    enum class KeyResult { Accepted, Partial, Skipped, NeedPassphrase, Error };

    ConnectStep fail(FsError error, const QString &message);
    ConnectStep beginAuth();
    ConnectStep continueAuth();
    ConnectStep finishAuth();
    ConnectStep tryKeyboardInteractive(int rc);
    ConnectStep askPassphrase(const QString &retryMessage);
    ConnectStep askPassword(const QString &retryMessage);
    KeyResult tryPrivateKey(const QString &path, const char *passphrase);
    QString serverFingerprint(QString *keyType) const;
    QStringList identityFiles() const;
    void keepalive();
    void shutdownSocket(); // thread-safe
    void runNextTask();

    FsStatus errorStatus(const QString &what, const SftpTask &task) const;
    QByteArray remotePath(const QString &path) const;
    FileInfo infoFor(const QString &path, struct sftp_attributes_struct *attributes) const;
    StatResult lstatNow(const QString &path, const SftpTask &task);
    FsStatus writeAll(struct sftp_file_struct *file, const QByteArray &data, const SftpTask &task);

    ServerAddress m_server;
    SftpOptions m_options;
    struct ssh_session_struct *m_session = nullptr;
    struct sftp_session_struct *m_sftp = nullptr;
    QString m_home;
    QTimer *m_keepalive = nullptr;

    AuthPhase m_phase = AuthPhase::None;
    int m_methods = 0;
    QStringList m_identities;
    int m_identityIndex = 0;
    QString m_pendingKeyFile;
    int m_attempts = 0;
    int m_authSteps = 0; // guards against a server that never finishes
    QString m_shownFingerprint;
    HostKeyInfo m_hostKey;
    std::atomic<bool> m_posixRenameAllowed{true};

    std::mutex m_queueMutex;
    std::deque<std::function<void()>> m_queue;
    std::atomic<quint64> m_generation{0};
    std::atomic<bool> m_busy{false};
    // The socket, so cancel() can shut it down from the UI thread. Guarded
    // by m_socketMutex so it is never shut down after being closed (when
    // the number could have been reused).
    QMutex m_socketMutex;
    qintptr m_socket = -1;
};

// ---------------------------------------------------------------------------

template<typename R>
QFuture<R> SftpConnection::post(std::function<R(SftpWorker &, const SftpTask &)> op)
{
    auto promise = std::make_shared<QPromise<R>>();
    promise->start();
    QFuture<R> future = promise->future();
    if (m_shutDown) {
        R result;
        result.error = FsError::Cancelled;
        result.message = tr("The connection has been closed.");
        promise->addResult(result);
        promise->finish();
        return future;
    }

    SftpWorker *worker = m_worker;
    const quint64 generation = worker->generation();
    worker->enqueue(
        [promise, op, worker, generation] {
            SftpTask task;
            task.isCancelled = [worker, generation] { return worker->generation() != generation; };
            QPromise<R> *p = promise.get();
            task.setProgress = [p](qint64 done, qint64 total) {
                p->setProgressRange(0, int(qMin<qint64>(total / 1024, INT_MAX)));
                p->setProgressValue(int(qMin<qint64>(done / 1024, INT_MAX)));
            };
            R result;
            if (task.isCancelled()) {
                result.error = FsError::Cancelled;
                result.message = SftpConnection::tr("Cancelled.");
            } else {
                result = op(*worker, task);
            }
            promise->addResult(std::move(result));
            promise->finish();
        });
    return future;
}

template<typename R>
QFuture<R> SftpConnection::execute(std::function<R(SftpWorker &, const SftpTask &)> op)
{
    auto promise = std::make_shared<QPromise<R>>();
    promise->start();
    QFuture<R> future = promise->future();
    executeInto(promise, std::move(op), true);
    return future;
}

template<typename R>
void SftpConnection::executeInto(std::shared_ptr<QPromise<R>> promise,
                                 std::function<R(SftpWorker &, const SftpTask &)> op, bool retry)
{
    auto finish = [promise](const R &result) {
        promise->addResult(result);
        promise->finish();
    };
    ensureConnected().then(this, [this, promise, op, retry, finish](const FsStatus &status) {
        if (!status.ok()) {
            R result;
            static_cast<FsStatus &>(result) = status;
            finish(result);
            return;
        }
        post<R>(op).then(this, [this, promise, op, retry, finish](const R &result) {
            if (result.error == FsError::Cancelled)
                setConnected(false);
            if (result.error == FsError::ConnectionLost) {
                setConnected(false);
                if (retry) {
                    executeInto(promise, op, false);
                    return;
                }
            }
            finish(result);
        });
    });
}
