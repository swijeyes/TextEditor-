#include "sftpconnection.h"

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>

#include <fcntl.h>
#include <mutex>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

namespace {

// Only modern algorithms: no SHA-1 host keys or MACs, no CBC ciphers and no
// old key exchange methods. Names libssh doesn't know are ignored by it.
const char *const HostKeyAlgorithms =
    "ssh-ed25519,ecdsa-sha2-nistp521,ecdsa-sha2-nistp384,ecdsa-sha2-nistp256,"
    "rsa-sha2-512,rsa-sha2-256";
const char *const KeyExchangeAlgorithms =
    "mlkem768x25519-sha256,sntrup761x25519-sha512,sntrup761x25519-sha512@openssh.com,"
    "curve25519-sha256,curve25519-sha256@libssh.org,"
    "ecdh-sha2-nistp521,ecdh-sha2-nistp384,ecdh-sha2-nistp256,"
    "diffie-hellman-group18-sha512,diffie-hellman-group16-sha512,"
    "diffie-hellman-group-exchange-sha256,diffie-hellman-group14-sha256";
const char *const BasicKeyExchangeAlgorithms =
    "curve25519-sha256,curve25519-sha256@libssh.org,"
    "ecdh-sha2-nistp521,ecdh-sha2-nistp384,ecdh-sha2-nistp256,"
    "diffie-hellman-group18-sha512,diffie-hellman-group16-sha512,"
    "diffie-hellman-group14-sha256";
const char *const Ciphers =
    "chacha20-poly1305@openssh.com,aes256-gcm@openssh.com,aes128-gcm@openssh.com,"
    "aes256-ctr,aes192-ctr,aes128-ctr";
const char *const Macs =
    "hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com,hmac-sha2-256,hmac-sha2-512";
const char *const PublicKeyTypes =
    "ssh-ed25519,ecdsa-sha2-nistp521,ecdsa-sha2-nistp384,ecdsa-sha2-nistp256,"
    "rsa-sha2-512,rsa-sha2-256";

constexpr int MaxAuthAttempts = 3;
constexpr int MaxAuthSteps = 50;
constexpr int MaxPrompts = 10;
constexpr int MaxSymlinksResolved = 1000;
constexpr qint64 ChunkSize = 32LL * 1024;

// Overwrites a secret before its memory is released (best effort: Qt may
// have made copies along the way).
void secureZero(QByteArray &bytes)
{
    if (bytes.isEmpty())
        return;
    volatile char *p = bytes.data();
    for (qsizetype i = 0; i < bytes.size(); ++i)
        p[i] = 0;
    bytes.clear();
}

// Secrets are QStrings shared (implicitly) by every copy made on the way
// from the dialog to here. Calling data() would detach and wipe a fresh
// copy, so the shared buffer itself is overwritten: every holder is done
// with it by now. Only the worker calls this, after the last use.
void secureZero(QStringList &strings)
{
    for (QString &s : strings) {
        if (!s.isEmpty() && s.data_ptr().isMutable()) {
            volatile char16_t *p = const_cast<char16_t *>(reinterpret_cast<const char16_t *>(s.constData()));
            for (qsizetype i = 0; i < s.size(); ++i)
                p[i] = 0;
        }
        s.clear();
    }
    strings.clear();
}

QString sshError(ssh_session session)
{
    return Location::sanitizedServerText(QString::fromUtf8(ssh_get_error(session)));
}

QString randomSuffix()
{
    return QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, u'0');
}

// Pointers that free their libssh object when they go out of scope.
struct AttributesDeleter
{
    void operator()(sftp_attributes a) const { sftp_attributes_free(a); }
};
using Attributes = std::unique_ptr<sftp_attributes_struct, AttributesDeleter>;

QString expandIdentity(QString path, const ServerAddress &server, const QString &sshDir)
{
    QString result;
    for (qsizetype i = 0; i < path.size(); ++i) {
        const QChar ch = path.at(i);
        if (ch != u'%' || i + 1 >= path.size()) {
            result += ch;
            continue;
        }
        const QChar code = path.at(++i);
        if (code == u'd')
            result += sshDir;
        else if (code == u'h')
            result += server.host;
        else if (code == u'r')
            result += server.user;
        else if (code == u'u')
            result += qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
        else if (code == u'%')
            result += u'%';
        else
            return QString(); // something we can't expand; skip this file
    }
    if (result.startsWith(QLatin1String("~/")))
        result = QDir::homePath() + result.mid(1);
    return result;
}

} // namespace

// --- SftpConnection ---------------------------------------------------------

SftpConnection::SftpConnection(const ServerAddress &server, const SftpOptions &options,
                               SftpPrompter *prompter, QObject *parent)
    : QObject(parent)
    , m_server(server)
    , m_prompter(prompter)
    , m_thread(new QThread)
    , m_worker(new SftpWorker(server, options))
{
    static std::once_flag initialized;
    std::call_once(initialized, [] { ssh_init(); });

    m_thread->setObjectName(QStringLiteral("sftp ") + server.displayName());
    m_worker->moveToThread(m_thread);
    connect(m_worker, &SftpWorker::connectionLost, this, [this] { setConnected(false); });
    m_thread->start();
}

SftpConnection::~SftpConnection()
{
    shutdown();
}

void SftpConnection::shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    ++m_attempt;
    m_worker->cancel();

    QThread *thread = m_thread;
    SftpWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker,
        [worker, thread] {
            worker->disconnectFromServer();
            thread->quit();
        },
        Qt::QueuedConnection);
    if (thread->wait(5000)) {
        delete worker;
        delete thread;
    } else {
        // Something is stuck in a system call; let it finish on its own
        // rather than blocking the window. The worker is leaked.
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    }
    m_worker = nullptr;
    m_thread = nullptr;
    m_connected = false;
    if (m_connecting)
        finishConnecting(FsStatus::failure(FsError::Cancelled, tr("The connection has been closed.")));
}

void SftpConnection::cancel()
{
    if (m_shutDown)
        return;
    m_worker->cancel();
    setConnected(false);
    if (m_connecting) {
        ++m_attempt;
        finishConnecting(FsStatus::failure(FsError::Cancelled, tr("Cancelled.")));
    }
}

void SftpConnection::dropConnectionForTesting()
{
    if (!m_shutDown)
        m_worker->dropConnectionForTesting();
}

void SftpConnection::setPosixRenameAllowedForTesting(bool allowed)
{
    if (!m_shutDown)
        m_worker->setPosixRenameAllowed(allowed);
}

void SftpConnection::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged(connected);
}

QFuture<FsStatus> SftpConnection::ensureConnected()
{
    if (m_connected)
        return readyFuture(FsStatus());
    if (m_shutDown)
        return readyFuture(FsStatus::failure(FsError::Cancelled, tr("The connection has been closed.")));
    if (m_connecting)
        return m_connectingFuture;

    m_connecting = std::make_shared<QPromise<FsStatus>>();
    m_connecting->start();
    m_connectingFuture = m_connecting->future();
    const int attempt = ++m_attempt;
    post<ConnectStep>([](SftpWorker &worker, const SftpTask &) { return worker.start(); })
        .then(this, [this, attempt](const ConnectStep &step) { handleStep(step, attempt); });
    return m_connectingFuture;
}

void SftpConnection::postAnswer(const ConnectAnswer &answer, int attempt)
{
    post<ConnectStep>([answer](SftpWorker &worker, const SftpTask &) {
        return worker.answer(answer);
    }).then(this, [this, attempt](const ConnectStep &step) { handleStep(step, attempt); });
}

void SftpConnection::handleStep(const ConnectStep &step, int attempt)
{
    if (attempt != m_attempt || !m_connecting)
        return;

    QPointer<SftpConnection> self(this);
    if (step.kind == ConnectStep::ChangedHost)
        m_prompter->reportChangedHostKey(step.hostKey);
    if (!self || attempt != m_attempt)
        return;
    if (!step.ok()) {
        finishConnecting(step);
        return;
    }

    switch (step.kind) {
    case ConnectStep::Connected:
        m_home = step.home;
        setConnected(true);
        finishConnecting(FsStatus());
        break;
    case ConnectStep::UnknownHost: {
        ConnectAnswer answer;
        answer.fingerprint = step.hostKey.fingerprint;
        answer.trustHost = m_prompter->trustHostKey(step.hostKey);
        answer.cancelled = !answer.trustHost;
        // The dialog ran an event loop; the connection may have gone.
        if (!self || attempt != m_attempt)
            return;
        postAnswer(answer, attempt);
        break;
    }
    case ConnectStep::NeedSecrets: {
        ConnectAnswer answer;
        answer.cancelled = !m_prompter->askSecrets(m_server, step.prompt, &answer.secrets);
        if (self && attempt == m_attempt)
            postAnswer(answer, attempt);
        // Only dropped here: the worker wipes the shared text after use.
        answer.secrets.clear();
        break;
    }
    case ConnectStep::ChangedHost:
        finishConnecting(FsStatus::failure(FsError::Failed, tr("The server's host key has changed.")));
        break;
    }
}

void SftpConnection::finishConnecting(const FsStatus &status)
{
    if (!m_connecting)
        return;
    std::shared_ptr<QPromise<FsStatus>> promise = std::move(m_connecting);
    m_connecting.reset();
    m_connectingFuture = QFuture<FsStatus>();
    promise->addResult(status);
    promise->finish();
}

// --- SftpWorker ---------------------------------------------------------------

SftpWorker::SftpWorker(const ServerAddress &server, const SftpOptions &options)
    : m_server(server)
    , m_options(options)
{
}

SftpWorker::~SftpWorker()
{
    disconnectFromServer();
}

void SftpWorker::enqueue(std::function<void()> task)
{
    {
        const std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push_back(std::move(task));
    }
    QMetaObject::invokeMethod(this, &SftpWorker::runNextTask, Qt::QueuedConnection);
}

void SftpWorker::runNextTask()
{
    std::function<void()> task;
    {
        const std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_queue.empty())
            return;
        task = std::move(m_queue.front());
        m_queue.pop_front();
    }
    task();
}

void SftpWorker::cancel()
{
    ++m_generation;
    // Shutting the socket down makes a blocked libssh call return at once.
    // The connection is lost, and the next operation reconnects.
    shutdownSocket();
}

void SftpWorker::dropConnectionForTesting()
{
    shutdownSocket();
}

void SftpWorker::shutdownSocket()
{
    const QMutexLocker locker(&m_socketMutex);
    if (m_socket >= 0) {
#ifdef Q_OS_WIN
        ::shutdown(SOCKET(m_socket), SD_BOTH);
#else
        ::shutdown(int(m_socket), SHUT_RDWR);
#endif
    }
}

bool SftpWorker::isConnected() const
{
    return m_session && m_sftp && ssh_is_connected(m_session);
}

void SftpWorker::disconnectFromServer()
{
    delete m_keepalive;
    m_keepalive = nullptr;
    {
        const QMutexLocker locker(&m_socketMutex);
        m_socket = -1;
    }
    if (m_sftp) {
        sftp_free(m_sftp);
        m_sftp = nullptr;
    }
    if (m_session) {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
    m_phase = AuthPhase::None;
    m_home.clear();
    m_shownFingerprint.clear();
}

ConnectStep SftpWorker::fail(FsError error, const QString &message)
{
    disconnectFromServer();
    ConnectStep step;
    step.error = error;
    step.message = message;
    return step;
}

QString SftpWorker::serverFingerprint(QString *keyType) const
{
    ssh_key key = nullptr;
    if (ssh_get_server_publickey(m_session, &key) != SSH_OK)
        return {};
    unsigned char *hash = nullptr;
    size_t length = 0;
    QString fingerprint;
    if (ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &length) == 0) {
        char *text = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, length);
        if (text) {
            fingerprint = QString::fromLatin1(text);
            ssh_string_free_char(text);
        }
        ssh_clean_pubkey_hash(&hash);
    }
    if (keyType)
        *keyType = QString::fromLatin1(ssh_key_type_to_char(ssh_key_type(key)));
    ssh_key_free(key);
    return fingerprint;
}

ConnectStep SftpWorker::start()
{
    disconnectFromServer();
    m_session = ssh_new();
    if (!m_session)
        return fail(FsError::Failed, tr("Couldn't start an SSH session."));

    // libssh never logs: its debug output could include sensitive details.
    const int verbosity = SSH_LOG_NOLOG;
    ssh_options_set(m_session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    const QByteArray host = m_server.host.toUtf8();
    const QByteArray user = m_server.user.toUtf8();
    const unsigned int port = unsigned(m_server.port);
    ssh_options_set(m_session, SSH_OPTIONS_HOST, host.constData());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &port);
    if (!user.isEmpty())
        ssh_options_set(m_session, SSH_OPTIONS_USER, user.constData());
    if (!m_options.sshDir.isEmpty())
        ssh_options_set(m_session, SSH_OPTIONS_SSH_DIR, QFile::encodeName(m_options.sshDir).constData());
    if (!m_options.knownHostsFile.isEmpty())
        ssh_options_set(m_session, SSH_OPTIONS_KNOWNHOSTS,
                        QFile::encodeName(m_options.knownHostsFile).constData());

    // Read ~/.ssh/config now (host aliases, key files, proxies), so that the
    // settings below override anything it says.
    const int processConfig = 0;
    ssh_options_set(m_session, SSH_OPTIONS_PROCESS_CONFIG, &processConfig);
    if (m_options.processConfig) {
        const QByteArray config = QFile::encodeName(m_options.configFile);
        ssh_options_parse_config(m_session, config.isEmpty() ? nullptr : config.constData());
    }
    // The address the user gave wins over the config file.
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &port);
    if (!user.isEmpty())
        ssh_options_set(m_session, SSH_OPTIONS_USER, user.constData());

    const long timeout = m_options.timeoutSeconds;
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout);
    if (ssh_options_set(m_session, SSH_OPTIONS_KEY_EXCHANGE, KeyExchangeAlgorithms) != SSH_OK)
        ssh_options_set(m_session, SSH_OPTIONS_KEY_EXCHANGE, BasicKeyExchangeAlgorithms);
    if (ssh_options_set(m_session, SSH_OPTIONS_HOSTKEYS, HostKeyAlgorithms) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_CIPHERS_C_S, Ciphers) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_CIPHERS_S_C, Ciphers) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_HMAC_C_S, Macs) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_HMAC_S_C, Macs) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_PUBLICKEY_ACCEPTED_TYPES, PublicKeyTypes) != SSH_OK) {
        return fail(FsError::Failed, tr("Couldn't set up secure algorithms: %1").arg(sshError(m_session)));
    }
    const int rsaMinSize = 2048;
    ssh_options_set(m_session, SSH_OPTIONS_RSA_MIN_SIZE, &rsaMinSize);

    if (ssh_connect(m_session) != SSH_OK)
        return fail(FsError::Failed, tr("Couldn't connect to %1:\n%2")
                                         .arg(m_server.displayName(), sshError(m_session)));
    {
        const QMutexLocker locker(&m_socketMutex);
        m_socket = qintptr(ssh_get_fd(m_session));
    }

    m_hostKey = HostKeyInfo();
    m_hostKey.server = m_server;
    m_hostKey.fingerprint = serverFingerprint(&m_hostKey.keyType);
    // The file libssh really uses: ~/.ssh/config can name another one.
    char *knownHosts = nullptr;
    if (ssh_options_get(m_session, SSH_OPTIONS_KNOWNHOSTS, &knownHosts) == SSH_OK && knownHosts) {
        m_hostKey.knownHostsFile = QDir::toNativeSeparators(QFile::decodeName(knownHosts));
        ssh_string_free_char(knownHosts);
    } else {
        m_hostKey.knownHostsFile = QDir::toNativeSeparators(QDir::homePath() + QStringLiteral("/.ssh/known_hosts"));
    }
    if (m_hostKey.fingerprint.isEmpty())
        return fail(FsError::Failed, tr("Couldn't read the server's host key."));

    switch (ssh_session_is_known_server(m_session)) {
    case SSH_KNOWN_HOSTS_OK:
        return beginAuth();
    case SSH_KNOWN_HOSTS_CHANGED:
    case SSH_KNOWN_HOSTS_OTHER: {
        // Possibly someone intercepting the connection: refuse, with no
        // way to connect anyway.
        ConnectStep step = fail(FsError::Failed,
                                tr("The host key for %1 has changed. TextEditor+ refused to "
                                   "connect.").arg(m_server.displayName()));
        step.kind = ConnectStep::ChangedHost;
        step.hostKey = m_hostKey;
        return step;
    }
    case SSH_KNOWN_HOSTS_UNKNOWN:
    case SSH_KNOWN_HOSTS_NOT_FOUND: {
        m_phase = AuthPhase::HostKey;
        m_shownFingerprint = m_hostKey.fingerprint;
        ConnectStep step;
        step.kind = ConnectStep::UnknownHost;
        step.hostKey = m_hostKey;
        return step;
    }
    case SSH_KNOWN_HOSTS_ERROR:
        break;
    }
    return fail(FsError::Failed, tr("Couldn't check the host key of %1:\n%2")
                                     .arg(m_server.displayName(), sshError(m_session)));
}

ConnectStep SftpWorker::answer(const ConnectAnswer &answerIn)
{
    ConnectAnswer answer = answerIn;
    struct Cleanup
    {
        QStringList &secrets;
        ~Cleanup() { secureZero(secrets); }
    } cleanup{answer.secrets};

    if (!m_session)
        return fail(FsError::ConnectionLost, tr("The connection was lost while logging in."));
    if (++m_authSteps > MaxAuthSteps)
        return fail(FsError::Failed, tr("Logging in to %1 took too many steps.").arg(m_server.displayName()));

    switch (m_phase) {
    case AuthPhase::HostKey: {
        if (answer.cancelled || !answer.trustHost)
            return fail(FsError::Cancelled, tr("You chose not to trust %1.").arg(m_server.displayName()));
        // Trust exactly the key that was shown, and that is still in use.
        if (answer.fingerprint != m_shownFingerprint || serverFingerprint(nullptr) != m_shownFingerprint)
            return fail(FsError::Failed, tr("The host key changed while connecting."));
        // If known_hosts can't be written, the key is trusted for this
        // connection only, as ssh does.
        ssh_session_update_known_hosts(m_session);
        return beginAuth();
    }
    case AuthPhase::KeyPassphrase: {
        if (answer.cancelled || answer.secrets.isEmpty()) {
            m_phase = AuthPhase::Keys;
            return continueAuth();
        }
        QByteArray passphrase = answer.secrets.constFirst().toUtf8();
        const KeyResult result = tryPrivateKey(m_pendingKeyFile, passphrase.constData());
        secureZero(passphrase);
        switch (result) {
        case KeyResult::Accepted:
            return finishAuth();
        case KeyResult::NeedPassphrase:
            if (++m_attempts < MaxAuthAttempts)
                return askPassphrase(tr("Wrong passphrase. Try again."));
            break;
        case KeyResult::Error:
            return fail(FsError::Failed, tr("Login failed:\n%1").arg(sshError(m_session)));
        case KeyResult::Partial:
            m_methods = ssh_userauth_list(m_session, nullptr);
            break;
        case KeyResult::Skipped:
            break;
        }
        m_phase = AuthPhase::Keys;
        return continueAuth();
    }
    case AuthPhase::KbdIntAnswer: {
        if (answer.cancelled)
            return fail(FsError::Cancelled, tr("Login cancelled."));
        const int count = ssh_userauth_kbdint_getnprompts(m_session);
        for (int i = 0; i < count; ++i) {
            QByteArray value = i < answer.secrets.size() ? answer.secrets.at(i).toUtf8() : QByteArray();
            ssh_userauth_kbdint_setanswer(m_session, unsigned(i), value.constData());
            secureZero(value);
        }
        return tryKeyboardInteractive(ssh_userauth_kbdint(m_session, nullptr, nullptr));
    }
    case AuthPhase::PasswordAnswer: {
        if (answer.cancelled || answer.secrets.isEmpty())
            return fail(FsError::Cancelled, tr("Login cancelled."));
        QByteArray password = answer.secrets.constFirst().toUtf8();
        const int rc = ssh_userauth_password(m_session, nullptr, password.constData());
        secureZero(password);
        if (rc == SSH_AUTH_SUCCESS)
            return finishAuth();
        if (rc == SSH_AUTH_PARTIAL) {
            m_methods = ssh_userauth_list(m_session, nullptr);
            m_phase = AuthPhase::Agent;
            return continueAuth();
        }
        if (rc == SSH_AUTH_DENIED) {
            if (++m_attempts < MaxAuthAttempts)
                return askPassword(tr("Wrong password. Try again."));
            return fail(FsError::Failed, tr("Login to %1 failed: wrong password.").arg(m_server.displayName()));
        }
        return fail(FsError::Failed, tr("Login failed:\n%1").arg(sshError(m_session)));
    }
    default:
        return fail(FsError::Failed, tr("Unexpected reply while logging in."));
    }
}

ConnectStep SftpWorker::beginAuth()
{
    m_authSteps = 0;
    const int rc = ssh_userauth_none(m_session, nullptr);
    if (rc == SSH_AUTH_SUCCESS)
        return finishAuth();
    if (rc == SSH_AUTH_ERROR)
        return fail(FsError::Failed, tr("Login failed:\n%1").arg(sshError(m_session)));
    m_methods = ssh_userauth_list(m_session, nullptr);
    m_phase = AuthPhase::Agent;
    return continueAuth();
}

ConnectStep SftpWorker::continueAuth()
{
    // Order: ssh-agent, then key files, then keyboard-interactive, then
    // password.
    while (++m_authSteps <= MaxAuthSteps) {
        switch (m_phase) {
        case AuthPhase::Agent: {
            m_phase = AuthPhase::Keys;
            m_identities = identityFiles();
            m_identityIndex = 0;
            if ((m_methods & SSH_AUTH_METHOD_PUBLICKEY) && m_options.useAgent) {
                const int rc = ssh_userauth_agent(m_session, nullptr);
                if (rc == SSH_AUTH_SUCCESS)
                    return finishAuth();
                if (rc == SSH_AUTH_PARTIAL)
                    m_methods = ssh_userauth_list(m_session, nullptr);
                else if (rc == SSH_AUTH_ERROR && !ssh_is_connected(m_session))
                    return fail(FsError::Failed, tr("Login failed:\n%1").arg(sshError(m_session)));
            }
            break;
        }
        case AuthPhase::Keys: {
            if (!(m_methods & SSH_AUTH_METHOD_PUBLICKEY) || m_identityIndex >= m_identities.size()) {
                m_phase = AuthPhase::KbdInt;
                break;
            }
            const QString path = m_identities.at(m_identityIndex++);
            switch (tryPrivateKey(path, nullptr)) {
            case KeyResult::Accepted:
                return finishAuth();
            case KeyResult::Partial:
                m_methods = ssh_userauth_list(m_session, nullptr);
                break;
            case KeyResult::NeedPassphrase:
                m_pendingKeyFile = path;
                m_attempts = 0;
                return askPassphrase(QString());
            case KeyResult::Error:
                if (!ssh_is_connected(m_session))
                    return fail(FsError::Failed, tr("Login failed:\n%1").arg(sshError(m_session)));
                break;
            case KeyResult::Skipped:
                break;
            }
            break;
        }
        case AuthPhase::KbdInt:
            m_phase = AuthPhase::Password;
            m_attempts = 0;
            if (m_methods & SSH_AUTH_METHOD_INTERACTIVE)
                return tryKeyboardInteractive(ssh_userauth_kbdint(m_session, nullptr, nullptr));
            break;
        case AuthPhase::Password:
            m_phase = AuthPhase::Failed;
            if (m_methods & SSH_AUTH_METHOD_PASSWORD) {
                m_attempts = 0;
                return askPassword(QString());
            }
            break;
        default:
            return fail(FsError::Failed,
                        tr("Couldn't log in to %1: the server accepted none of the login methods "
                           "tried (ssh-agent, key files, password).").arg(m_server.displayName()));
        }
    }
    return fail(FsError::Failed, tr("Logging in to %1 took too many steps.").arg(m_server.displayName()));
}

ConnectStep SftpWorker::tryKeyboardInteractive(int rc)
{
    while (++m_authSteps <= MaxAuthSteps) {
        if (rc == SSH_AUTH_INFO) {
            const int count = ssh_userauth_kbdint_getnprompts(m_session);
            if (count <= 0) {
                // OpenSSH sends an empty round before reporting success.
                rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
                continue;
            }
            if (count > MaxPrompts)
                return fail(FsError::Failed, tr("The server asked too many login questions."));
            ConnectStep step;
            step.kind = ConnectStep::NeedSecrets;
            step.prompt.kind = AuthPrompt::KeyboardInteractive;
            step.prompt.name = Location::sanitizedServerText(
                QString::fromUtf8(ssh_userauth_kbdint_getname(m_session)), 200);
            step.prompt.instruction = Location::sanitizedServerText(
                QString::fromUtf8(ssh_userauth_kbdint_getinstruction(m_session)), 1000);
            for (int i = 0; i < count; ++i) {
                char echo = 0;
                const char *prompt = ssh_userauth_kbdint_getprompt(m_session, unsigned(i), &echo);
                step.prompt.prompts << Location::sanitizedServerText(QString::fromUtf8(prompt), 200);
                step.prompt.echo << (echo != 0);
            }
            if (m_attempts > 0)
                step.prompt.retryMessage = tr("Login failed. Try again.");
            m_phase = AuthPhase::KbdIntAnswer;
            return step;
        }
        if (rc == SSH_AUTH_SUCCESS)
            return finishAuth();
        if (rc == SSH_AUTH_PARTIAL) {
            m_methods = ssh_userauth_list(m_session, nullptr);
            m_phase = AuthPhase::Agent;
            return continueAuth();
        }
        if (rc == SSH_AUTH_DENIED) {
            if (++m_attempts < MaxAuthAttempts) {
                rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
                continue;
            }
            m_phase = AuthPhase::Password;
            m_attempts = 0;
            return continueAuth();
        }
        return fail(FsError::Failed, tr("Login failed:\n%1").arg(sshError(m_session)));
    }
    return fail(FsError::Failed, tr("Logging in to %1 took too many steps.").arg(m_server.displayName()));
}

ConnectStep SftpWorker::askPassphrase(const QString &retryMessage)
{
    m_phase = AuthPhase::KeyPassphrase;
    ConnectStep step;
    step.kind = ConnectStep::NeedSecrets;
    step.prompt.kind = AuthPrompt::Passphrase;
    step.prompt.keyFile = QDir::toNativeSeparators(m_pendingKeyFile);
    step.prompt.prompts << tr("Passphrase:");
    step.prompt.echo << false;
    step.prompt.retryMessage = retryMessage;
    return step;
}

ConnectStep SftpWorker::askPassword(const QString &retryMessage)
{
    m_phase = AuthPhase::PasswordAnswer;
    ConnectStep step;
    step.kind = ConnectStep::NeedSecrets;
    step.prompt.kind = AuthPrompt::Password;
    step.prompt.prompts << tr("Password:");
    step.prompt.echo << false;
    step.prompt.retryMessage = retryMessage;
    return step;
}

SftpWorker::KeyResult SftpWorker::tryPrivateKey(const QString &path, const char *passphrase)
{
    if (!QFileInfo(path).isFile())
        return KeyResult::Skipped;

    // With a public key next to it, first ask whether the server would
    // accept the key at all, before reading the private key (and asking
    // for its passphrase).
    const QString publicPath = path + QStringLiteral(".pub");
    if (!passphrase && QFileInfo(publicPath).isFile()) {
        ssh_key publicKey = nullptr;
        if (ssh_pki_import_pubkey_file(QFile::encodeName(publicPath).constData(), &publicKey) == SSH_OK) {
            const int rc = ssh_userauth_try_publickey(m_session, nullptr, publicKey);
            ssh_key_free(publicKey);
            if (rc == SSH_AUTH_ERROR)
                return KeyResult::Error;
            if (rc != SSH_AUTH_SUCCESS)
                return KeyResult::Skipped;
        }
    }

    ssh_key key = nullptr;
    if (ssh_pki_import_privkey_file(QFile::encodeName(path).constData(), passphrase, nullptr,
                                    nullptr, &key) != SSH_OK) {
        // Most likely encrypted (or the passphrase was wrong).
        return KeyResult::NeedPassphrase;
    }
    const int rc = ssh_userauth_publickey(m_session, nullptr, key);
    ssh_key_free(key);
    switch (rc) {
    case SSH_AUTH_SUCCESS: return KeyResult::Accepted;
    case SSH_AUTH_PARTIAL: return KeyResult::Partial;
    case SSH_AUTH_ERROR: return KeyResult::Error;
    default: return KeyResult::Skipped;
    }
}

QStringList SftpWorker::identityFiles() const
{
    QStringList files;
    const QString sshDir = m_options.sshDir.isEmpty() ? QDir::homePath() + QStringLiteral("/.ssh")
                                                      : m_options.sshDir;
    if (!m_options.identityFiles.isEmpty()) {
        files = m_options.identityFiles;
    } else {
        // Key files named in ~/.ssh/config come first, then the standard
        // ones.
        char *identity = nullptr;
        while (ssh_options_get(m_session, SSH_OPTIONS_NEXT_IDENTITY, &identity) == SSH_OK && identity) {
            const QString expanded = expandIdentity(QString::fromUtf8(identity), m_server, sshDir);
            ssh_string_free_char(identity);
            identity = nullptr;
            if (!expanded.isEmpty())
                files << expanded;
        }
        for (const char *name : {"id_ed25519", "id_ecdsa", "id_rsa"})
            files << sshDir + QLatin1Char('/') + QLatin1String(name);
    }
    files.removeDuplicates();
    return files;
}

ConnectStep SftpWorker::finishAuth()
{
    m_phase = AuthPhase::None;
    m_sftp = sftp_new(m_session);
    if (!m_sftp)
        return fail(FsError::Failed, tr("Couldn't start SFTP on %1:\n%2")
                                         .arg(m_server.displayName(), sshError(m_session)));
    if (sftp_init(m_sftp) != SSH_OK)
        return fail(FsError::Failed, tr("%1 doesn't offer SFTP:\n%2")
                                         .arg(m_server.displayName(), sshError(m_session)));

    char *home = sftp_canonicalize_path(m_sftp, ".");
    if (home) {
        m_home = QString::fromUtf8(home);
        ssh_string_free_char(home);
    }
    if (m_home.isEmpty() || !m_home.startsWith(QLatin1Char('/')) || m_home.contains(QChar(0)))
        m_home = QStringLiteral("/");

    if (m_options.keepaliveSeconds > 0) {
        m_keepalive = new QTimer(this);
        m_keepalive->setInterval(m_options.keepaliveSeconds * 1000);
        connect(m_keepalive, &QTimer::timeout, this, &SftpWorker::keepalive);
        m_keepalive->start();
    }

    ConnectStep step;
    step.kind = ConnectStep::Connected;
    step.home = m_home;
    return step;
}

void SftpWorker::keepalive()
{
    if (!m_session || !m_sftp)
        return;
    // An SSH "ignore" message keeps idle connections open through routers,
    // and polling the channel answers the server's own keepalives.
    if (ssh_send_ignore(m_session, "") != SSH_OK
        || ssh_channel_poll_timeout(m_sftp->channel, 0, 0) == SSH_ERROR
        || !ssh_is_connected(m_session)) {
        disconnectFromServer();
        emit connectionLost();
    }
}

// --- File operations ------------------------------------------------------------

QByteArray SftpWorker::remotePath(const QString &path) const
{
    QString full = path;
    if (full.isEmpty() || full == QLatin1String("~"))
        full = m_home;
    else if (full.startsWith(QLatin1String("~/")))
        full = Location::joinRemotePath(m_home, full.mid(2));
    return full.toUtf8();
}

FsStatus SftpWorker::errorStatus(const QString &what, const SftpTask &task) const
{
    if (task.isCancelled())
        return FsStatus::failure(FsError::Cancelled, tr("Cancelled."));
    if (!m_session || !m_sftp || !ssh_is_connected(m_session))
        return FsStatus::failure(FsError::ConnectionLost,
                                 tr("The connection to %1 was lost.").arg(m_server.displayName()));
    switch (sftp_get_error(m_sftp)) {
    case SSH_FX_NO_SUCH_FILE:
    case SSH_FX_NO_SUCH_PATH:
        return FsStatus::failure(FsError::NotFound, tr("%1: no such file or folder.").arg(what));
    case SSH_FX_PERMISSION_DENIED:
        return FsStatus::failure(FsError::Failed, tr("%1: permission denied.").arg(what));
    case SSH_FX_FILE_ALREADY_EXISTS:
        return FsStatus::failure(FsError::AlreadyExists, tr("%1 already exists.").arg(what));
    case SSH_FX_CONNECTION_LOST:
    case SSH_FX_NO_CONNECTION:
        return FsStatus::failure(FsError::ConnectionLost,
                                 tr("The connection to %1 was lost.").arg(m_server.displayName()));
    case SSH_FX_OP_UNSUPPORTED:
        return FsStatus::failure(FsError::Failed, tr("%1: the server doesn't support this.").arg(what));
    default:
        return FsStatus::failure(FsError::Failed, tr("%1:\n%2").arg(what, sshError(m_session)));
    }
}

FileInfo SftpWorker::infoFor(const QString &path, sftp_attributes attributes) const
{
    FileInfo info;
    info.name = Location::remoteFileName(path);
    switch (attributes->type) {
    case SSH_FILEXFER_TYPE_REGULAR: info.type = FileInfo::File; break;
    case SSH_FILEXFER_TYPE_DIRECTORY: info.type = FileInfo::Folder; break;
    case SSH_FILEXFER_TYPE_SYMLINK: info.isSymLink = true; info.type = FileInfo::Other; break;
    default: info.type = FileInfo::Other; break;
    }
    if (attributes->flags & SSH_FILEXFER_ATTR_PERMISSIONS) {
        info.permissions = attributes->permissions & 07777u;
        switch (attributes->permissions & 0170000u) {
        case 0100000u: info.type = FileInfo::File; break;
        case 0040000u: info.type = FileInfo::Folder; break;
        case 0120000u: info.isSymLink = true; info.type = FileInfo::Other; break;
        default: break;
        }
    }
    if (attributes->flags & SSH_FILEXFER_ATTR_SIZE)
        info.size = qint64(qMin<quint64>(attributes->size, quint64(std::numeric_limits<qint64>::max())));
    const quint64 mtime = attributes->mtime64 ? attributes->mtime64 : attributes->mtime;
    if (mtime)
        info.modified = QDateTime::fromSecsSinceEpoch(qint64(qMin<quint64>(mtime, 1ULL << 40)),
                                                      QTimeZone::UTC);
    return info;
}

StatResult SftpWorker::lstatNow(const QString &path, const SftpTask &task)
{
    StatResult result;
    result.info.name = Location::remoteFileName(path);
    const Attributes attributes(sftp_lstat(m_sftp, remotePath(path).constData()));
    if (!attributes) {
        const FsStatus status = errorStatus(path, task);
        if (status.error != FsError::NotFound)
            static_cast<FsStatus &>(result) = status;
        return result; // missing: info.type is Missing
    }
    result.info = infoFor(path, attributes.get());
    return result;
}

StatResult SftpWorker::stat(const QString &path, const SftpTask &task)
{
    if (!isConnected())
        return {errorStatus(path, task), {}};
    if (path.contains(QChar(0)))
        return {FsStatus::failure(FsError::NotFound, tr("Invalid path.")), {}};

    StatResult result = lstatNow(path, task);
    if (!result.ok() || !result.info.isSymLink)
        return result;
    // A symbolic link: report what it points to.
    const Attributes target(sftp_stat(m_sftp, remotePath(path).constData()));
    if (target) {
        result.info = infoFor(path, target.get());
        result.info.isSymLink = true;
    } else {
        const FsStatus status = errorStatus(path, task);
        if (status.error != FsError::NotFound)
            static_cast<FsStatus &>(result) = status;
    }
    return result;
}

ListResult SftpWorker::list(const QString &folder, const SftpTask &task)
{
    ListResult result;
    if (!isConnected()) {
        static_cast<FsStatus &>(result) = errorStatus(folder, task);
        return result;
    }
    const QByteArray folderPath = remotePath(folder);
    const sftp_dir dir = sftp_opendir(m_sftp, folderPath.constData());
    if (!dir) {
        static_cast<FsStatus &>(result) = errorStatus(folder, task);
        return result;
    }

    int symlinksResolved = 0;
    for (;;) {
        if (task.isCancelled()) {
            sftp_closedir(dir);
            return {{FsError::Cancelled, tr("Cancelled.")}, {}, false, 0};
        }
        const Attributes attributes(sftp_readdir(m_sftp, dir));
        if (!attributes) {
            if (!sftp_dir_eof(dir)) {
                static_cast<FsStatus &>(result) = errorStatus(folder, task);
                result.entries.clear();
                sftp_closedir(dir);
                return result;
            }
            break;
        }
        const QByteArray rawName = attributes->name ? QByteArray(attributes->name) : QByteArray();
        if (rawName == "." || rawName == "..")
            continue;
        // A hostile server could send names such as "../../x" or "a/b".
        if (!Location::isValidListingName(rawName)) {
            ++result.rejected;
            continue;
        }
        if (result.entries.size() >= FileLimits::MaxListingEntries) {
            result.truncated = true;
            break;
        }
        const QString name = QString::fromUtf8(rawName);
        FileInfo info = infoFor(name, attributes.get());
        if (info.isSymLink && symlinksResolved < MaxSymlinksResolved) {
            ++symlinksResolved;
            const QByteArray linkPath = Location::joinRemotePath(QString::fromUtf8(folderPath), name).toUtf8();
            const Attributes target(sftp_stat(m_sftp, linkPath.constData()));
            if (target) {
                info = infoFor(name, target.get());
                info.isSymLink = true;
            }
        }
        result.entries.append(info);
    }
    sftp_closedir(dir);
    return result;
}

ReadResult SftpWorker::read(const QString &path, qint64 maxBytes, const SftpTask &task)
{
    ReadResult result;
    auto failWith = [&result](const FsStatus &status) {
        static_cast<FsStatus &>(result) = status;
        result.data.clear();
        return result;
    };
    if (!isConnected())
        return failWith(errorStatus(path, task));

    // Resolve a symbolic link first, so the user can be told, and so saving
    // writes the real file rather than replacing the link.
    const StatResult link = lstatNow(path, task);
    if (!link.ok())
        return failWith(link);
    if (!link.info.exists())
        return failWith(FsStatus::failure(FsError::NotFound, tr("%1 does not exist.").arg(path)));
    QByteArray readPath = remotePath(path);
    if (link.info.isSymLink) {
        char *real = sftp_canonicalize_path(m_sftp, readPath.constData());
        if (!real)
            return failWith(errorStatus(path, task));
        readPath = QByteArray(real);
        ssh_string_free_char(real);
        result.resolvedPath = QString::fromUtf8(readPath);
    }

    const Attributes before(sftp_stat(m_sftp, readPath.constData()));
    if (!before)
        return failWith(errorStatus(path, task));
    FileInfo info = infoFor(path, before.get());
    if (!info.isFile())
        return failWith(FsStatus::failure(FsError::NotRegularFile, tr("%1 is not a regular file.").arg(path)));
    if (info.size > maxBytes)
        return failWith(FsStatus::failure(FsError::TooLarge, tr("%1 is too large to open.").arg(path)));

    const sftp_file file = sftp_open(m_sftp, readPath.constData(), O_RDONLY, 0);
    if (!file)
        return failWith(errorStatus(path, task));
    // Check the opened file too, in case the path was swapped meanwhile.
    const Attributes opened(sftp_fstat(file));
    if (opened) {
        info = infoFor(path, opened.get());
        if (!info.isFile()) {
            sftp_close(file);
            return failWith(FsStatus::failure(FsError::NotRegularFile, tr("%1 is not a regular file.").arg(path)));
        }
    }
    result.info = info;

    // Read at most maxBytes + 1, to notice a file that grew past the limit.
    const qint64 expected = qMax<qint64>(info.size, 0);
    result.data.reserve(qsizetype(qMin(expected, maxBytes) + 1));
    QByteArray chunk(ChunkSize, Qt::Uninitialized);
    for (;;) {
        if (task.isCancelled()) {
            sftp_close(file);
            return failWith(FsStatus::failure(FsError::Cancelled, tr("Cancelled.")));
        }
        const qint64 wanted = qMin<qint64>(ChunkSize, maxBytes + 1 - result.data.size());
        if (wanted <= 0)
            break;
        const ssize_t n = sftp_read(file, chunk.data(), size_t(wanted));
        if (n < 0) {
            const FsStatus status = errorStatus(path, task);
            sftp_close(file);
            return failWith(status);
        }
        if (n == 0)
            break;
        result.data.append(chunk.constData(), qsizetype(n));
        task.setProgress(result.data.size(), qMax(expected, qint64(result.data.size())));
    }
    sftp_close(file);
    if (result.data.size() > maxBytes)
        return failWith(FsStatus::failure(FsError::TooLarge, tr("%1 is too large to open.").arg(path)));
    return result;
}

FsStatus SftpWorker::writeAll(sftp_file file, const QByteArray &data, const SftpTask &task)
{
    qint64 written = 0;
    while (written < data.size()) {
        if (task.isCancelled())
            return FsStatus::failure(FsError::Cancelled, tr("Cancelled."));
        const qint64 count = qMin<qint64>(ChunkSize, data.size() - written);
        const ssize_t n = sftp_write(file, data.constData() + written, size_t(count));
        if (n <= 0)
            return errorStatus(tr("Writing"), task);
        written += n;
        task.setProgress(written, data.size());
    }
    return {};
}

WriteResult SftpWorker::writeAtomic(const QString &path, const QByteArray &data,
                                    const WriteExpectation &expected, const SftpTask &task)
{
    WriteResult result;
    auto failWith = [&result](const FsStatus &status) {
        static_cast<FsStatus &>(result) = status;
        return result;
    };
    if (!isConnected())
        return failWith(errorStatus(path, task));
    if (path.contains(QChar(0)))
        return failWith(FsStatus::failure(FsError::Failed, tr("Invalid path.")));

    const QByteArray target = remotePath(path);
    const QString targetPath = QString::fromUtf8(target);
    const StatResult current = lstatNow(path, task);
    if (!current.ok())
        return failWith(current);
    // Never follow a link silently: the editor saves to the file a link
    // points to (resolved when it was opened), so a link here is a surprise.
    if (current.info.isSymLink)
        return failWith(FsStatus::failure(FsError::NotRegularFile,
                                          tr("%1 is a symbolic link. Open the file it points to "
                                             "and save that instead.").arg(path)));
    if (current.info.exists() && !current.info.isFile())
        return failWith(FsStatus::failure(FsError::NotRegularFile, tr("%1 is not a regular file.").arg(path)));
    if (expected.mode == WriteExpectation::MustNotExist && current.info.exists())
        return failWith(FsStatus::failure(FsError::AlreadyExists, tr("%1 already exists.").arg(path)));
    if (expected.mode == WriteExpectation::MustMatch && !current.info.sameVersionAs(expected.info)) {
        return failWith(FsStatus::failure(
            FsError::Conflict,
            current.info.exists()
                ? tr("%1 has been changed on the server since it was opened.").arg(path)
                : tr("%1 has been deleted or moved on the server since it was opened.").arg(path)));
    }

    if (expected.mode == WriteExpectation::MustNotExist) {
        // A new file: O_EXCL fails if something appeared there meanwhile.
        const sftp_file file = sftp_open(m_sftp, target.constData(), O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (!file)
            return failWith(errorStatus(path, task));
        FsStatus status = writeAll(file, data, task);
        if (sftp_close(file) != SSH_OK && status.ok())
            status = errorStatus(path, task);
        if (!status.ok()) {
            sftp_unlink(m_sftp, target.constData());
            return failWith(status);
        }
        result.info = stat(path, task).info;
        return result;
    }

    // 1. Write the new text to a temporary file in the same folder, with a
    //    random name that can't collide or be guessed.
    const QString folder = Location::remoteParent(targetPath);
    const QString name = Location::remoteFileName(targetPath);
    const QByteArray temp =
        Location::joinRemotePath(folder, QLatin1Char('.') + name + QStringLiteral(".tetmp-") + randomSuffix())
            .toUtf8();
    const sftp_file file = sftp_open(m_sftp, temp.constData(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (!file)
        return failWith(errorStatus(tr("Creating a temporary file in %1").arg(folder), task));
    FsStatus status = writeAll(file, data, task);
    if (status.ok() && sftp_extension_supported(m_sftp, "fsync@openssh.com", "1"))
        sftp_fsync(file);
    if (sftp_close(file) != SSH_OK && status.ok())
        status = errorStatus(path, task);

    // 2. Give it the original file's permissions.
    if (status.ok()) {
        const mode_t mode = current.info.exists() && current.info.permissions
            ? mode_t(current.info.permissions & 07777u)
            : mode_t(0644);
        if (sftp_chmod(m_sftp, temp.constData(), mode) != SSH_OK)
            status = errorStatus(tr("Setting permissions"), task);
    }
    if (!status.ok()) {
        sftp_unlink(m_sftp, temp.constData());
        return failWith(status);
    }

    // 3. Swap it into place. posix-rename replaces the original in one step.
    const bool posixRename = m_posixRenameAllowed
        && sftp_extension_supported(m_sftp, "posix-rename@openssh.com", "1");
    if (posixRename || !current.info.exists()) {
        if (sftp_rename(m_sftp, temp.constData(), target.constData()) != SSH_OK) {
            status = errorStatus(path, task);
            sftp_unlink(m_sftp, temp.constData());
            return failWith(status);
        }
    } else {
        // Without the extension, a plain rename can't replace an existing
        // file: move the original aside, move the new one in, then delete
        // the original. If the second step fails, the original goes back.
        const QByteArray backup =
            Location::joinRemotePath(folder, QLatin1Char('.') + name + QStringLiteral(".tebak-") + randomSuffix())
                .toUtf8();
        if (sftp_rename(m_sftp, target.constData(), backup.constData()) != SSH_OK) {
            status = errorStatus(path, task);
            sftp_unlink(m_sftp, temp.constData());
            return failWith(status);
        }
        if (sftp_rename(m_sftp, temp.constData(), target.constData()) != SSH_OK) {
            status = errorStatus(path, task);
            sftp_rename(m_sftp, backup.constData(), target.constData());
            sftp_unlink(m_sftp, temp.constData());
            return failWith(status);
        }
        sftp_unlink(m_sftp, backup.constData());
    }
    result.info = stat(path, task).info;
    return result;
}

FsStatus SftpWorker::rename(const QString &from, const QString &to, const SftpTask &task)
{
    if (!isConnected())
        return errorStatus(from, task);
    // posix-rename would silently replace an existing file.
    const StatResult existing = lstatNow(to, task);
    if (!existing.ok())
        return existing;
    if (existing.info.exists())
        return FsStatus::failure(FsError::AlreadyExists, tr("%1 already exists.").arg(to));
    if (sftp_rename(m_sftp, remotePath(from).constData(), remotePath(to).constData()) != SSH_OK)
        return errorStatus(from, task);
    return {};
}

FsStatus SftpWorker::remove(const QString &path, const SftpTask &task)
{
    if (!isConnected())
        return errorStatus(path, task);
    const StatResult current = lstatNow(path, task);
    if (!current.ok())
        return current;
    const QByteArray p = remotePath(path);
    const int rc = current.info.isFolder() ? sftp_rmdir(m_sftp, p.constData())
                                           : sftp_unlink(m_sftp, p.constData());
    if (rc != SSH_OK) {
        FsStatus status = errorStatus(path, task);
        if (current.info.isFolder() && status.error == FsError::Failed)
            status.message = tr("Cannot delete %1. Only empty folders can be deleted on a server.").arg(path);
        return status;
    }
    return {};
}

FsStatus SftpWorker::makeFolder(const QString &path, const SftpTask &task)
{
    if (!isConnected())
        return errorStatus(path, task);
    const StatResult existing = lstatNow(path, task);
    if (!existing.ok())
        return existing;
    if (existing.info.exists())
        return FsStatus::failure(FsError::AlreadyExists, tr("%1 already exists.").arg(path));
    if (sftp_mkdir(m_sftp, remotePath(path).constData(), 0755) != SSH_OK)
        return errorStatus(path, task);
    return {};
}
