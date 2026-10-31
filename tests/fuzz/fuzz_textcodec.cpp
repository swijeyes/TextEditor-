// Fuzzes decodeText/encodeText with arbitrary file contents.
#include "textcodec.h"

#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QByteArray bytes(reinterpret_cast<const char *>(data), qsizetype(size));
    const DecodedText decoded = decodeText(bytes);

    if (decoded.isBinary != bytes.contains('\0'))
        abort();
    // Latin-1 text always fits Latin-1.
    if (decoded.latin1 && !fitsLatin1(decoded.text))
        abort();

    // Saving an unedited file must give back the same bytes whenever the
    // file's line endings were consistent (all "\r\n" or none).
    const bool consistent = decoded.crlf
        ? bytes.count("\r\n") == bytes.count('\n')
        : !bytes.contains("\r\n");
    if (consistent && encodeText(decoded.text, decoded.crlf, decoded.latin1) != bytes)
        abort();
    return 0;
}
