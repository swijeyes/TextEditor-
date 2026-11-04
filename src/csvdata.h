#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Parsing of delimiter-separated text (CSV and TSV) into a grid of fields,
// following RFC 4180 quoting: fields may be wrapped in double quotes, a
// doubled "" is a literal quote, and a quoted field may contain the
// delimiter and line breaks. Pure (no widgets), so the grid viewer and the
// tests share it.
namespace CsvData {

enum class Delimiter { Comma, Tab, Semicolon };

QChar delimiterChar(Delimiter delimiter);
QString delimiterName(Delimiter delimiter);

// Guesses the delimiter from the file name (.tsv/.tab are tabs) and, failing
// that, from which candidate is the most consistent across the first lines.
Delimiter detectDelimiter(const QString &text, const QString &fileName = QString());

// Whether a file name looks like tabular data the grid viewer can open.
bool isTabularFileName(const QString &fileName);

// Splits text into rows of fields. Rows need not have the same length; the
// grid pads short rows. Handles \n, \r\n and \r line endings outside quotes.
QList<QStringList> parse(const QString &text, QChar delimiter);

} // namespace CsvData
