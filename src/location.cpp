#include "location.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringDecoder>

#include <algorithm>

namespace {

constexpr QLatin1String SftpScheme("sftp");

// Characters that reorder text (bidirectional overrides and isolates) can
// make a server's text look like something else, so they are dropped too.
bool isBidiControl(char16_t ch)
{
    return (ch >= 0x202a && ch <= 0x202e) || (ch >= 0x2066 && ch <= 0x2069)
        || ch == 0x200e || ch == 0x200f || ch == 0x061c;
}

QString tr(const char *text)
{
    return QCoreApplication::translate("Location", text);
}

// Host and user names may end up in a shell command: ~/.ssh/config can say
// "ProxyCommand ssh -W %h:%p jumphost", and %h and %r are filled in from the
// address. An address from a link or a session file mustn't be able to add
// shell syntax (as in OpenSSH's CVE-2023-51385), so only plain names are
// accepted: letters, digits and "-._" for hosts (plus ":" and "%" inside an
// IPv6 address), and letters, digits and "-._+" for users.
bool isSafeName(const QString &name, QStringView extra)
{
    return std::all_of(name.cbegin(), name.cend(), [extra](QChar ch) {
        return ch.isLetterOrNumber() || ch == u'-' || ch == u'.' || ch == u'_' || extra.contains(ch);
    });
}

} // namespace

bool ServerAddress::isValid() const
{
    const bool ipv6 = host.contains(QLatin1Char(':'));
    return !host.isEmpty() && host.size() <= 255 && user.size() <= 255 && port > 0 && port <= 65535
        && !host.startsWith(QLatin1Char('-')) && !user.startsWith(QLatin1Char('-'))
        && isSafeName(host, ipv6 ? u":%" : u"") && isSafeName(user, u"+") && url().isValid();
}

QString ServerAddress::key() const
{
    return user + QLatin1Char('@') + host.toLower() + QLatin1Char(':') + QString::number(port);
}

QString ServerAddress::displayName() const
{
    QString name = user.isEmpty() ? host : user + QLatin1Char('@') + host;
    if (port != Location::DefaultSftpPort)
        name += QLatin1Char(':') + QString::number(port);
    return name;
}

QUrl ServerAddress::url(const QString &path) const
{
    QUrl url;
    url.setScheme(SftpScheme);
    url.setHost(host);
    if (port != Location::DefaultSftpPort)
        url.setPort(port);
    if (!user.isEmpty())
        url.setUserName(user);
    if (path.startsWith(QLatin1Char('~')))
        url.setPath(QLatin1Char('/') + path);
    else
        url.setPath(path);
    return url;
}

ServerAddress ServerAddress::fromUrl(const QUrl &url)
{
    ServerAddress address;
    address.host = url.host().toLower();
    address.port = url.port(Location::DefaultSftpPort);
    address.user = url.userName();
    return address;
}

namespace Location {

QUrl fromUserInput(const QString &text, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QUrl();
    };

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return fail(tr("No file or address given."));
    // A NUL would cut the path short in the system's file functions.
    if (trimmed.contains(QChar(0)))
        return fail(tr("The name contains an invalid character."));

    if (trimmed.startsWith(QLatin1String("sftp://"), Qt::CaseInsensitive)) {
        const QUrl url(trimmed, QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty())
            return fail(tr("%1 is not a valid sftp:// address.").arg(stripPassword(url).toString()));
        if (!ServerAddress::fromUrl(url).isValid())
            return fail(tr("%1 is not a valid sftp:// address.").arg(stripPassword(url).toString()));
        return normalized(url);
    }
    if (trimmed.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        const QUrl url(trimmed, QUrl::StrictMode);
        if (!url.isValid() || !url.isLocalFile())
            return fail(tr("%1 is not a valid file address.").arg(trimmed));
        return normalized(url);
    }
    return fromLocalPath(trimmed);
}

QUrl fromLocalPath(const QString &path)
{
    return QUrl::fromLocalFile(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
}

bool isLocal(const QUrl &url)
{
    return url.isLocalFile();
}

bool isRemote(const QUrl &url)
{
    return url.scheme().compare(SftpScheme, Qt::CaseInsensitive) == 0 && !url.host().isEmpty();
}

QUrl stripPassword(const QUrl &url)
{
    QUrl result = url;
    result.setPassword(QString());
    return result;
}

QUrl normalized(const QUrl &url)
{
    if (isLocal(url))
        return fromLocalPath(url.toLocalFile());
    if (!isRemote(url))
        return {};

    const ServerAddress address = ServerAddress::fromUrl(url);
    QString path = url.path();
    if (!path.isEmpty()) {
        path = QDir::cleanPath(path);
        if (!path.startsWith(QLatin1Char('/')))
            path.prepend(QLatin1Char('/'));
    }
    // ServerAddress::url() never includes a password.
    QUrl result = address.url();
    result.setPath(path);
    return result;
}

QString displayString(const QUrl &url)
{
    if (isLocal(url))
        return QDir::toNativeSeparators(url.toLocalFile());
    return stripPassword(url).toString(QUrl::RemovePassword);
}

QString path(const QUrl &url)
{
    if (isLocal(url))
        return url.toLocalFile();
    // "/~/x" means x in the user's home folder; an empty path means the home
    // folder itself. The connection expands "~" once it knows the home folder.
    const QString p = url.path();
    if (p == QLatin1String("/~") || p.startsWith(QLatin1String("/~/")))
        return p.mid(1);
    return p;
}

QString fileName(const QUrl &url)
{
    if (isLocal(url))
        return QFileInfo(url.toLocalFile()).fileName();
    const QString name = remoteFileName(path(url));
    return name.isEmpty() || name == QLatin1String("~") ? url.host() : name;
}

QUrl parentFolder(const QUrl &url)
{
    if (isLocal(url))
        return fromLocalPath(QFileInfo(url.toLocalFile()).absolutePath());
    QUrl result = url;
    const QString parent = remoteParent(path(url));
    result.setPath(parent.startsWith(QLatin1Char('~')) ? QLatin1Char('/') + parent : parent);
    return result;
}

QUrl child(const QUrl &folder, const QString &name)
{
    if (isLocal(folder))
        return fromLocalPath(QDir(folder.toLocalFile()).filePath(name));
    QUrl result = folder;
    const QString childPath = joinRemotePath(path(folder), name);
    result.setPath(childPath.startsWith(QLatin1Char('~')) ? QLatin1Char('/') + childPath
                                                          : childPath);
    return result;
}

bool isInside(const QUrl &url, const QUrl &folder)
{
    if (isLocal(url) != isLocal(folder))
        return false;
    if (isRemote(url) && !(ServerAddress::fromUrl(url) == ServerAddress::fromUrl(folder)))
        return false;
    const QString p = path(url);
    QString f = path(folder);
    if (p == f)
        return true;
    if (!f.endsWith(QLatin1Char('/')))
        f += QLatin1Char('/');
    return p.startsWith(f);
}

QString joinRemotePath(const QString &folder, const QString &name)
{
    if (folder.isEmpty())
        return name;
    if (folder.endsWith(QLatin1Char('/')))
        return folder + name;
    return folder + QLatin1Char('/') + name;
}

QString remoteParent(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return path.startsWith(QLatin1Char('~')) ? QString() : path;
    if (slash == 0)
        return QStringLiteral("/");
    return path.left(slash);
}

QString remoteFileName(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? path : path.mid(slash + 1);
}

bool isValidListingName(const QString &name)
{
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..")
        && !name.contains(QLatin1Char('/')) && !name.contains(QChar(0));
}

bool isValidListingName(const QByteArray &name)
{
    if (name.contains('\0'))
        return false;
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = decoder.decode(name);
    return !decoder.hasError() && isValidListingName(text);
}

QString sanitizedServerText(const QString &text, int maxLength)
{
    QString result;
    result.reserve(qMin(text.size(), qsizetype(maxLength)));
    for (QChar ch : text) {
        if (result.size() >= maxLength) {
            result += QChar(0x2026); // …
            break;
        }
        const bool allowed = ch == QLatin1Char('\n') || ch == QLatin1Char('\t')
            || (ch.category() != QChar::Other_Control && !isBidiControl(ch.unicode()));
        if (allowed)
            result += ch;
    }
    return result.trimmed();
}

} // namespace Location
