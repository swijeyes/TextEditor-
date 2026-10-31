// Fuzzes the syntax highlighters, looking for crashes and slow inputs (run
// libFuzzer with -timeout, as was done for 1.1).
#include "highlighter.h"

#include <QGuiApplication>
#include <QTextDocument>

#include <cstdint>

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static QGuiApplication app(*argc, *argv);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(data), qsizetype(size));
    QTextDocument document;
    document.setPlainText(text);
    Highlighter highlighter(&document);
    for (Language language : Highlighter::languages())
        highlighter.setLanguage(language); // each rehighlights the whole text
    return 0;
}
