#include "csvdata.h"

#include <QFileInfo>

namespace CsvData {

QChar delimiterChar(Delimiter delimiter)
{
    switch (delimiter) {
    case Delimiter::Tab:
        return QLatin1Char('\t');
    case Delimiter::Semicolon:
        return QLatin1Char(';');
    case Delimiter::Comma:
        break;
    }
    return QLatin1Char(',');
}

QString delimiterName(Delimiter delimiter)
{
    switch (delimiter) {
    case Delimiter::Tab:
        return QStringLiteral("Tab");
    case Delimiter::Semicolon:
        return QStringLiteral("Semicolon");
    case Delimiter::Comma:
        break;
    }
    return QStringLiteral("Comma");
}

bool isTabularFileName(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return suffix == QLatin1String("csv") || suffix == QLatin1String("tsv")
        || suffix == QLatin1String("tab");
}

namespace {

// Counts a candidate delimiter in the first few lines, ignoring the ones
// inside quotes, and rewards a count that stays the same on every line.
double consistencyScore(const QString &text, QChar delimiter)
{
    QList<int> counts;
    int count = 0;
    bool inQuotes = false;
    for (int i = 0; i < text.size() && counts.size() < 20; ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('"')) {
            inQuotes = !inQuotes;
        } else if (!inQuotes && (ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))) {
            if (ch == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            counts << count;
            count = 0;
        } else if (!inQuotes && ch == delimiter) {
            ++count;
        }
    }
    if (count > 0 || !counts.isEmpty())
        counts << count;

    int total = 0;
    int lines = 0;
    for (int c : std::as_const(counts)) {
        if (c > 0) {
            total += c;
            ++lines;
        }
    }
    if (lines == 0)
        return 0.0;
    // Average per line, with a small bonus for appearing on more lines.
    return double(total) / lines + lines * 0.001;
}

} // namespace

Delimiter detectDelimiter(const QString &text, const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == QLatin1String("tsv") || suffix == QLatin1String("tab"))
        return Delimiter::Tab;
    if (suffix == QLatin1String("csv"))
        return Delimiter::Comma;

    const double comma = consistencyScore(text, QLatin1Char(','));
    const double tab = consistencyScore(text, QLatin1Char('\t'));
    const double semicolon = consistencyScore(text, QLatin1Char(';'));
    if (tab >= comma && tab >= semicolon && tab > 0)
        return Delimiter::Tab;
    if (semicolon > comma && semicolon > 0)
        return Delimiter::Semicolon;
    return Delimiter::Comma;
}

QList<QStringList> parse(const QString &text, QChar delimiter)
{
    QList<QStringList> rows;
    QStringList fields;
    QString field;
    bool inQuotes = false;
    bool sawAny = false; // something on the current row (so trailing \n ends it cleanly)

    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar ch = text.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
                    field.append(QLatin1Char('"'));
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field.append(ch);
            }
            continue;
        }

        if (ch == QLatin1Char('"')) {
            inQuotes = true;
            sawAny = true;
        } else if (ch == delimiter) {
            fields << field;
            field.clear();
            sawAny = true;
        } else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
            if (ch == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            fields << field;
            field.clear();
            rows << fields;
            fields.clear();
            sawAny = false;
        } else {
            field.append(ch);
            sawAny = true;
        }
    }

    // A final field/row without a trailing line break.
    if (inQuotes || sawAny || !field.isEmpty() || !fields.isEmpty()) {
        fields << field;
        rows << fields;
    }
    return rows;
}

} // namespace CsvData
