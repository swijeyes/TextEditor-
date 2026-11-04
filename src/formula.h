#pragma once

#include <QString>
#include <QStringList>

// A small spreadsheet formula engine: arithmetic, comparison and text
// operators, A1-style cell and range references, and a comprehensive set of
// maths, statistical, financial, logical and text functions. Pure (no
// widgets) and exception-free (every error is returned as an Error value),
// so the grid viewer and the tests share it.
namespace Formula {

enum class ValueType { Empty, Number, Text, Boolean, Error };

// One cell or intermediate value. Errors carry an Excel-style code in text()
// (for example "#DIV/0!", "#VALUE!", "#NAME?", "#NUM!", "#REF!", "#CIRC!").
class Value
{
public:
    Value() = default;

    static Value empty() { return Value(); }
    static Value number(double n);
    static Value text(const QString &s);
    static Value boolean(bool b);
    static Value error(const QString &code);

    ValueType type() const { return m_type; }
    bool isError() const { return m_type == ValueType::Error; }
    bool isEmpty() const { return m_type == ValueType::Empty; }

    // Reads the value as a number, coercing text and booleans. Empty is 0.
    double toNumber(bool *ok = nullptr) const;
    // Reads the value as a boolean, coercing numbers (0 is false) and the
    // words "true"/"false".
    bool toBoolean(bool *ok = nullptr) const;
    // The value as it should be shown in a cell (an error shows its code).
    QString toText() const;

    double rawNumber() const { return m_number; }
    const QString &rawText() const { return m_text; }

private:
    ValueType m_type = ValueType::Empty;
    double m_number = 0.0;
    QString m_text;
};

// Supplies cell values to the evaluator. Cells are addressed 0-based. A cell
// that is itself a formula is evaluated by the implementation, which also
// detects reference cycles and returns a "#CIRC!" error for them.
class Sheet
{
public:
    virtual ~Sheet() = default;
    virtual int rowCount() const = 0;
    virtual int columnCount() const = 0;
    virtual Value cellValue(int row, int column) const = 0;
};

// Column letters to a 0-based index and back: "A" is 0, "Z" is 25, "AA" is
// 26. Returns -1 for anything that is not all letters.
int columnFromLetters(const QString &letters);
QString columnToLetters(int index);

// Evaluates a formula: the text after the leading "=". Never throws; a
// malformed formula or a runtime problem comes back as an Error value.
Value evaluate(const QString &expression, const Sheet &sheet);

// Every function the engine knows, upper-case and sorted, for the help text.
QStringList functionNames();

} // namespace Formula
