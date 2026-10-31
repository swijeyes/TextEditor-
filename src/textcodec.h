#pragma once

#include <QByteArray>
#include <QString>

// Converting between a file's bytes and the editor's text. These functions
// don't touch the UI, so local and remote files share them and they can be
// unit-tested.

struct DecodedText
{
    QString text;        // with '\n' line endings
    bool crlf = false;   // the file used "\r\n"
    bool latin1 = false; // the file isn't valid UTF-8, so it was read as Latin-1
    bool isBinary = false; // the file contains NUL bytes
};

// Decodes a file as UTF-8, falling back to Latin-1 when it isn't valid UTF-8.
DecodedText decodeText(const QByteArray &bytes);

// True if every character can be stored in Latin-1. When it can't, saving a
// Latin-1 file needs the user's permission to switch it to UTF-8.
bool fitsLatin1(const QString &text);

// Encodes text for saving, restoring "\r\n" line endings when crlf is set.
// latin1 must only be set when fitsLatin1(text) is true.
QByteArray encodeText(const QString &text, bool crlf, bool latin1);
