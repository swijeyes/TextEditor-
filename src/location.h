#pragma once

#include <QString>
#include <QUrl>

// Files and folders are identified by URLs throughout: file:///path for local
// ones and sftp://user@host:port/path for ones on a server. Passwords are
// never kept in these URLs; every function here strips them.

// The server part of an sftp:// address. One connection is kept per server.
struct ServerAddress
{
    QString host;
    int port = 22;
    QString user; // empty: the local user name, or User from ~/.ssh/config

    bool isValid() const;
    // "user@host:port", used to share one connection per server.
    QString key() const;
    // "user@host" plus ":port" when it isn't 22, for showing to the user.
    QString displayName() const;
    QUrl url(const QString &path = QString()) const;

    static ServerAddress fromUrl(const QUrl &url);

    friend bool operator==(const ServerAddress &a, const ServerAddress &b)
    {
        return a.key() == b.key();
    }
};

namespace Location {

constexpr int DefaultSftpPort = 22;

// Parses a command-line argument, dropped text or typed address: an
// sftp:// or file:// URL, or a local path. Returns an invalid URL if the
// text can't be used, with the reason in *error.
QUrl fromUserInput(const QString &text, QString *error = nullptr);

QUrl fromLocalPath(const QString &path);
bool isLocal(const QUrl &url);
bool isRemote(const QUrl &url);

// Removes any password, normalises the host and path, and drops the default
// port. Returns an invalid URL for anything that isn't file:// or sftp://.
QUrl normalized(const QUrl &url);
QUrl stripPassword(const QUrl &url);

// A native path for local files, or an sftp:// address without a password.
QString displayString(const QUrl &url);
// The last part of the path, or the host for a server's root folder.
QString fileName(const QUrl &url);
// The path the file system layer uses: native local path, or remote path.
QString path(const QUrl &url);
QUrl parentFolder(const QUrl &url);
QUrl child(const QUrl &folder, const QString &name);
// True if url is folder itself or lies inside it.
bool isInside(const QUrl &url, const QUrl &folder);

// Remote paths always use '/'. Joins a folder and a name that has passed
// isValidListingName().
QString joinRemotePath(const QString &folder, const QString &name);
QString remoteParent(const QString &path);
QString remoteFileName(const QString &path);

// Whether a name from a folder listing (local or from a server) may be used
// as a single path component. Rejects "", ".", "..", and names containing
// '/' or NUL, so a hostile server can't point the app outside the folder
// being browsed.
bool isValidListingName(const QString &name);
// The same check for raw bytes from a server, which must also be valid
// UTF-8 so that the name can be sent back unchanged.
bool isValidListingName(const QByteArray &name);

// Text from a server (error messages, login prompts) for showing to the
// user: control characters removed and length capped.
QString sanitizedServerText(const QString &text, int maxLength = 500);

} // namespace Location
