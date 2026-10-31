// Fuzzes the TeX preview's LaTeX to HTML converter, looking for crashes,
// runaway recursion and slow inputs (run libFuzzer with -timeout).
#include "latexhtml.h"

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(data), qsizetype(size));
    LatexHtml::toHtml(text);
    return 0;
}
