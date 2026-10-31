// Fuzzes sftp:// address parsing, password stripping, folder-listing name
// checks and the cleaning of server text.
#include "location.h"

#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QByteArray bytes(reinterpret_cast<const char *>(data), qsizetype(size));
    const QString text = QString::fromUtf8(bytes);

    // Addresses: whatever is typed, a password never survives parsing.
    for (const QString &input : {text, QStringLiteral("sftp://") + text,
                                 QStringLiteral("sftp://user:hunter2@") + text}) {
        QString error;
        const QUrl url = Location::fromUserInput(input, &error);
        if (url.isValid()) {
            if (!url.password().isEmpty())
                abort();
            if (Location::displayString(url).contains(QLatin1String("hunter2"))
                && !input.mid(input.indexOf(QLatin1Char('@')) + 1).contains(QLatin1String("hunter2")))
                abort();
            if (Location::isRemote(url)) {
                Location::fileName(url);
                Location::parentFolder(url);
                ServerAddress::fromUrl(url).key();
            }
        } else if (error.contains(QLatin1String("hunter2"))
                   && !input.mid(input.indexOf(QLatin1Char('@')) + 1).contains(QLatin1String("hunter2"))) {
            abort();
        }
    }

    // A name that passes the check stays a single component of its folder.
    if (Location::isValidListingName(bytes)) {
        const QString name = QString::fromUtf8(bytes);
        const QString folder = QStringLiteral("/srv/files");
        const QString path = Location::joinRemotePath(folder, name);
        if (Location::remoteParent(path) != folder || Location::remoteFileName(path) != name)
            abort();
        if (name == QLatin1String("..") || name.contains(QLatin1Char('/')))
            abort();
    }

    // Server text loses control characters and is capped.
    const QString clean = Location::sanitizedServerText(text, 200);
    if (clean.size() > 201)
        abort();
    for (QChar ch : clean) {
        if (ch.category() == QChar::Other_Control && ch != QLatin1Char('\n') && ch != QLatin1Char('\t'))
            abort();
    }
    return 0;
}
