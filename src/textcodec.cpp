#include "textcodec.h"

#include <QStringDecoder>

#include <algorithm>

DecodedText decodeText(const QByteArray &bytes)
{
    DecodedText result;
    // NUL bytes mean a binary file, which saving as text would corrupt.
    result.isBinary = bytes.contains('\0');

    // Stateless, so an incomplete sequence at the very end counts as an
    // error. A stateful decoder holds those bytes back for more input, and
    // they would be silently dropped from the text (and lost on saving).
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    result.text = decoder.decode(bytes);
    result.latin1 = decoder.hasError();
    if (result.latin1)
        result.text = QString::fromLatin1(bytes);

    // Edit with '\n' internally; remember the file's style for saving.
    result.crlf = result.text.contains(QLatin1String("\r\n"));
    result.text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    return result;
}

bool fitsLatin1(const QString &text)
{
    return std::all_of(text.cbegin(), text.cend(), [](QChar ch) { return ch.unicode() <= 0xff; });
}

QByteArray encodeText(const QString &text, bool crlf, bool latin1)
{
    QString out = text;
    if (crlf)
        out.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    return latin1 ? out.toLatin1() : out.toUtf8();
}
