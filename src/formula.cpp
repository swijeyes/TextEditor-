#include "formula.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Formula {

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

Value Value::number(double n)
{
    Value v;
    v.m_type = ValueType::Number;
    v.m_number = n;
    return v;
}

Value Value::text(const QString &s)
{
    Value v;
    v.m_type = ValueType::Text;
    v.m_text = s;
    return v;
}

Value Value::boolean(bool b)
{
    Value v;
    v.m_type = ValueType::Boolean;
    v.m_number = b ? 1.0 : 0.0;
    return v;
}

Value Value::error(const QString &code)
{
    Value v;
    v.m_type = ValueType::Error;
    v.m_text = code;
    return v;
}

double Value::toNumber(bool *ok) const
{
    if (ok)
        *ok = true;
    switch (m_type) {
    case ValueType::Number:
    case ValueType::Boolean:
        return m_number;
    case ValueType::Empty:
        return 0.0;
    case ValueType::Text: {
        const QString trimmed = m_text.trimmed();
        if (trimmed.isEmpty()) {
            return 0.0;
        }
        bool good = false;
        const double d = trimmed.toDouble(&good);
        if (good)
            return d;
        if (ok)
            *ok = false;
        return 0.0;
    }
    case ValueType::Error:
        break;
    }
    if (ok)
        *ok = false;
    return 0.0;
}

bool Value::toBoolean(bool *ok) const
{
    if (ok)
        *ok = true;
    switch (m_type) {
    case ValueType::Boolean:
    case ValueType::Number:
        return m_number != 0.0;
    case ValueType::Empty:
        return false;
    case ValueType::Text: {
        const QString t = m_text.trimmed().toLower();
        if (t == QLatin1String("true"))
            return true;
        if (t == QLatin1String("false") || t.isEmpty())
            return false;
        bool good = false;
        const double d = t.toDouble(&good);
        if (good)
            return d != 0.0;
        break;
    }
    case ValueType::Error:
        break;
    }
    if (ok)
        *ok = false;
    return false;
}

QString Value::toText() const
{
    switch (m_type) {
    case ValueType::Empty:
        return QString();
    case ValueType::Number: {
        if (std::isnan(m_number))
            return QStringLiteral("#NUM!");
        if (std::isinf(m_number))
            return m_number < 0 ? QStringLiteral("-∞") : QStringLiteral("∞");
        // Up to 15 significant digits, without a trailing ".0".
        QString s = QString::number(m_number, 'g', 15);
        return s;
    }
    case ValueType::Boolean:
        return m_number != 0.0 ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    case ValueType::Text:
    case ValueType::Error:
        return m_text;
    }
    return QString();
}

// ---------------------------------------------------------------------------
// Column letters
// ---------------------------------------------------------------------------

int columnFromLetters(const QString &letters)
{
    if (letters.isEmpty())
        return -1;
    int result = 0;
    for (const QChar ch : letters) {
        if (!ch.isLetter())
            return -1;
        const int d = ch.toUpper().unicode() - 'A' + 1;
        if (d < 1 || d > 26)
            return -1;
        result = result * 26 + d;
    }
    return result - 1;
}

QString columnToLetters(int index)
{
    if (index < 0)
        return QString();
    QString result;
    int n = index + 1;
    while (n > 0) {
        const int rem = (n - 1) % 26;
        result.prepend(QChar('A' + rem));
        n = (n - 1) / 26;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

namespace {

enum class Tok {
    End,
    Number,
    String,
    Ident,   // function name, boolean, or a cell/column reference
    Plus, Minus, Star, Slash, Caret, Percent, Amp,
    Eq, Ne, Lt, Gt, Le, Ge,
    LParen, RParen, Comma, Colon
};

struct Token
{
    Tok kind = Tok::End;
    double number = 0.0;
    QString text;

    Token() = default;
    Token(Tok k) : kind(k) {}
    Token(Tok k, double n, QString t) : kind(k), number(n), text(std::move(t)) {}
};

class Lexer
{
public:
    explicit Lexer(const QString &src)
        : m_src(src)
    {
        advance();
    }

    const Token &peek() const { return m_current; }
    Token take()
    {
        Token t = m_current;
        advance();
        return t;
    }
    bool ok() const { return m_ok; }

private:
    void advance()
    {
        while (m_pos < m_src.size() && m_src.at(m_pos).isSpace())
            ++m_pos;
        if (m_pos >= m_src.size()) {
            m_current = Token{Tok::End, 0.0, QString()};
            return;
        }
        const QChar ch = m_src.at(m_pos);

        if (ch == QLatin1Char('"')) {
            ++m_pos;
            QString s;
            while (m_pos < m_src.size()) {
                const QChar c = m_src.at(m_pos);
                if (c == QLatin1Char('"')) {
                    if (m_pos + 1 < m_src.size() && m_src.at(m_pos + 1) == QLatin1Char('"')) {
                        s.append(QLatin1Char('"'));
                        m_pos += 2;
                        continue;
                    }
                    ++m_pos;
                    m_current = Token{Tok::String, 0.0, s};
                    return;
                }
                s.append(c);
                ++m_pos;
            }
            m_ok = false; // unterminated string
            m_current = Token{Tok::End, 0.0, QString()};
            return;
        }

        if (ch.isDigit() || (ch == QLatin1Char('.') && m_pos + 1 < m_src.size()
                             && m_src.at(m_pos + 1).isDigit())) {
            const int start = m_pos;
            while (m_pos < m_src.size() && m_src.at(m_pos).isDigit())
                ++m_pos;
            if (m_pos < m_src.size() && m_src.at(m_pos) == QLatin1Char('.')) {
                ++m_pos;
                while (m_pos < m_src.size() && m_src.at(m_pos).isDigit())
                    ++m_pos;
            }
            if (m_pos < m_src.size()
                && (m_src.at(m_pos) == QLatin1Char('e') || m_src.at(m_pos) == QLatin1Char('E'))) {
                int save = m_pos;
                ++m_pos;
                if (m_pos < m_src.size()
                    && (m_src.at(m_pos) == QLatin1Char('+') || m_src.at(m_pos) == QLatin1Char('-')))
                    ++m_pos;
                if (m_pos < m_src.size() && m_src.at(m_pos).isDigit()) {
                    while (m_pos < m_src.size() && m_src.at(m_pos).isDigit())
                        ++m_pos;
                } else {
                    m_pos = save; // not really an exponent
                }
            }
            bool good = false;
            const double d = m_src.mid(start, m_pos - start).toDouble(&good);
            m_ok = m_ok && good;
            m_current = Token{Tok::Number, d, QString()};
            return;
        }

        // Identifier: letters, with optional '$' markers and trailing digits
        // (cell references such as $A$1). Kept as written; '$' is dropped
        // when the reference is interpreted.
        if (ch.isLetter() || ch == QLatin1Char('$') || ch == QLatin1Char('_')) {
            const int start = m_pos;
            while (m_pos < m_src.size()) {
                const QChar c = m_src.at(m_pos);
                if (c.isLetterOrNumber() || c == QLatin1Char('$') || c == QLatin1Char('_')
                    || c == QLatin1Char('.'))
                    ++m_pos;
                else
                    break;
            }
            m_current = Token{Tok::Ident, 0.0, m_src.mid(start, m_pos - start)};
            return;
        }

        ++m_pos;
        switch (ch.unicode()) {
        case '+': m_current = Token{Tok::Plus}; return;
        case '-': m_current = Token{Tok::Minus}; return;
        case '*': m_current = Token{Tok::Star}; return;
        case '/': m_current = Token{Tok::Slash}; return;
        case '^': m_current = Token{Tok::Caret}; return;
        case '%': m_current = Token{Tok::Percent}; return;
        case '&': m_current = Token{Tok::Amp}; return;
        case '(': m_current = Token{Tok::LParen}; return;
        case ')': m_current = Token{Tok::RParen}; return;
        case ',': m_current = Token{Tok::Comma}; return;
        case ':': m_current = Token{Tok::Colon}; return;
        case '=': m_current = Token{Tok::Eq}; return;
        case '<':
            if (m_pos < m_src.size() && m_src.at(m_pos) == QLatin1Char('>')) {
                ++m_pos;
                m_current = Token{Tok::Ne};
            } else if (m_pos < m_src.size() && m_src.at(m_pos) == QLatin1Char('=')) {
                ++m_pos;
                m_current = Token{Tok::Le};
            } else {
                m_current = Token{Tok::Lt};
            }
            return;
        case '>':
            if (m_pos < m_src.size() && m_src.at(m_pos) == QLatin1Char('=')) {
                ++m_pos;
                m_current = Token{Tok::Ge};
            } else {
                m_current = Token{Tok::Gt};
            }
            return;
        default:
            m_ok = false;
            m_current = Token{Tok::End};
            return;
        }
    }

    QString m_src;
    int m_pos = 0;
    Token m_current;
    bool m_ok = true;
};

// ---------------------------------------------------------------------------
// AST
// ---------------------------------------------------------------------------

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Node
{
    enum Kind { Num, Str, Bool, Cell, Range, Unary, Binary, Call } kind;

    double num = 0.0;
    QString str;
    bool boolean = false;

    // Cell / Range. For a whole column "A:A" the rows are -1 (unbounded); for
    // a whole row "1:1" the columns are -1.
    int r1 = 0, c1 = 0, r2 = 0, c2 = 0;

    Tok op = Tok::End;             // Unary / Binary
    QString func;                  // Call
    std::vector<NodePtr> children; // operands / arguments
};

NodePtr makeNode(Node::Kind kind)
{
    auto n = std::make_shared<Node>();
    n->kind = kind;
    return n;
}

// A cell or column/row reference parsed from an identifier. "A1" is a cell;
// "A" is a column (row -1); a bare number handled by the caller is a row.
struct Ref
{
    bool valid = false;
    bool hasColumn = false;
    bool hasRow = false;
    int column = -1;
    int row = -1;
};

Ref parseRef(const QString &ident)
{
    Ref ref;
    QString letters;
    QString digits;
    int i = 0;
    const QString s = ident;
    if (i < s.size() && s.at(i) == QLatin1Char('$'))
        ++i;
    while (i < s.size() && s.at(i).isLetter()) {
        letters.append(s.at(i));
        ++i;
    }
    if (i < s.size() && s.at(i) == QLatin1Char('$'))
        ++i;
    while (i < s.size() && s.at(i).isDigit()) {
        digits.append(s.at(i));
        ++i;
    }
    if (i != s.size() || letters.isEmpty())
        return ref; // leftover characters, or no column letters
    const int col = columnFromLetters(letters);
    if (col < 0)
        return ref;
    ref.valid = true;
    ref.hasColumn = true;
    ref.column = col;
    if (!digits.isEmpty()) {
        ref.hasRow = true;
        ref.row = digits.toInt() - 1; // A1 is row 0
        if (ref.row < 0)
            ref.valid = false;
    }
    return ref;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser
{
public:
    explicit Parser(const QString &src)
        : m_lexer(src)
    {
    }

    NodePtr parse()
    {
        NodePtr node = parseComparison();
        if (!node || !m_ok || !m_lexer.ok() || m_lexer.peek().kind != Tok::End)
            return nullptr;
        return node;
    }

private:
    NodePtr parseComparison()
    {
        NodePtr left = parseConcat();
        while (left) {
            const Tok k = m_lexer.peek().kind;
            if (k == Tok::Eq || k == Tok::Ne || k == Tok::Lt || k == Tok::Gt || k == Tok::Le
                || k == Tok::Ge) {
                m_lexer.take();
                NodePtr right = parseConcat();
                left = binary(k, left, right);
            } else {
                break;
            }
        }
        return left;
    }

    NodePtr parseConcat()
    {
        NodePtr left = parseAddSub();
        while (left && m_lexer.peek().kind == Tok::Amp) {
            m_lexer.take();
            NodePtr right = parseAddSub();
            left = binary(Tok::Amp, left, right);
        }
        return left;
    }

    NodePtr parseAddSub()
    {
        NodePtr left = parseMulDiv();
        while (left) {
            const Tok k = m_lexer.peek().kind;
            if (k == Tok::Plus || k == Tok::Minus) {
                m_lexer.take();
                NodePtr right = parseMulDiv();
                left = binary(k, left, right);
            } else {
                break;
            }
        }
        return left;
    }

    NodePtr parseMulDiv()
    {
        NodePtr left = parsePower();
        while (left) {
            const Tok k = m_lexer.peek().kind;
            if (k == Tok::Star || k == Tok::Slash) {
                m_lexer.take();
                NodePtr right = parsePower();
                left = binary(k, left, right);
            } else {
                break;
            }
        }
        return left;
    }

    // Exponent is right-associative and binds looser than the unary sign, so
    // "-2^2" is "(-2)^2" = 4 and "2^3^2" is "2^(3^2)", matching spreadsheets.
    NodePtr parsePower()
    {
        NodePtr base = parseUnary();
        if (base && m_lexer.peek().kind == Tok::Caret) {
            m_lexer.take();
            NodePtr exp = parsePower();
            return binary(Tok::Caret, base, exp);
        }
        return base;
    }

    NodePtr parseUnary()
    {
        const Tok k = m_lexer.peek().kind;
        if (k == Tok::Minus || k == Tok::Plus) {
            m_lexer.take();
            NodePtr operand = parseUnary();
            if (!operand)
                return nullptr;
            NodePtr node = makeNode(Node::Unary);
            node->op = k;
            node->children.push_back(operand);
            return node;
        }
        return parsePostfix();
    }

    NodePtr parsePostfix()
    {
        NodePtr node = parsePrimary();
        while (node && m_lexer.peek().kind == Tok::Percent) {
            m_lexer.take();
            NodePtr pct = makeNode(Node::Unary);
            pct->op = Tok::Percent;
            pct->children.push_back(node);
            node = pct;
        }
        return node;
    }

    NodePtr parsePrimary()
    {
        const Token tok = m_lexer.peek();
        switch (tok.kind) {
        case Tok::Number: {
            m_lexer.take();
            // A bare number may start a whole-row range, e.g. 2:5.
            if (m_lexer.peek().kind == Tok::Colon) {
                m_lexer.take();
                const Token end = m_lexer.peek();
                if (end.kind != Tok::Number)
                    return fail();
                m_lexer.take();
                NodePtr node = makeNode(Node::Range);
                node->c1 = -1;
                node->c2 = -1;
                node->r1 = int(tok.number) - 1;
                node->r2 = int(end.number) - 1;
                if (node->r1 < 0 || node->r2 < 0)
                    return fail();
                normalizeRange(node);
                return node;
            }
            NodePtr node = makeNode(Node::Num);
            node->num = tok.number;
            return node;
        }
        case Tok::String: {
            m_lexer.take();
            NodePtr node = makeNode(Node::Str);
            node->str = tok.text;
            return node;
        }
        case Tok::LParen: {
            m_lexer.take();
            NodePtr inner = parseComparison();
            if (!inner || m_lexer.peek().kind != Tok::RParen)
                return fail();
            m_lexer.take();
            return inner;
        }
        case Tok::Ident:
            return parseIdent();
        default:
            return fail();
        }
    }

    NodePtr parseIdent()
    {
        const Token tok = m_lexer.take();
        const QString upper = tok.text.toUpper();

        // Function call.
        if (m_lexer.peek().kind == Tok::LParen) {
            m_lexer.take();
            NodePtr node = makeNode(Node::Call);
            node->func = upper;
            if (m_lexer.peek().kind != Tok::RParen) {
                while (true) {
                    NodePtr arg = parseComparison();
                    if (!arg)
                        return fail();
                    node->children.push_back(arg);
                    if (m_lexer.peek().kind == Tok::Comma) {
                        m_lexer.take();
                        continue;
                    }
                    break;
                }
            }
            if (m_lexer.peek().kind != Tok::RParen)
                return fail();
            m_lexer.take();
            return node;
        }

        if (upper == QLatin1String("TRUE") || upper == QLatin1String("FALSE")) {
            NodePtr node = makeNode(Node::Bool);
            node->boolean = (upper == QLatin1String("TRUE"));
            return node;
        }

        // Reference, possibly the start of a range.
        const Ref ref = parseRef(tok.text);
        if (!ref.valid)
            return fail();
        if (m_lexer.peek().kind == Tok::Colon) {
            m_lexer.take();
            const Token endTok = m_lexer.peek();
            Ref endRef;
            if (endTok.kind == Tok::Ident) {
                endRef = parseRef(endTok.text);
                m_lexer.take();
            } else if (endTok.kind == Tok::Number) {
                // "A1:5" style is not supported; require a reference token.
                return fail();
            } else {
                return fail();
            }
            if (!endRef.valid)
                return fail();
            NodePtr node = makeNode(Node::Range);
            // Whole-column (A:A) if neither side carries a row.
            if (!ref.hasRow && !endRef.hasRow) {
                node->r1 = -1;
                node->r2 = -1;
                node->c1 = ref.column;
                node->c2 = endRef.column;
            } else {
                node->r1 = ref.hasRow ? ref.row : 0;
                node->r2 = endRef.hasRow ? endRef.row : 0;
                node->c1 = ref.column;
                node->c2 = endRef.column;
            }
            normalizeRange(node);
            return node;
        }

        if (!ref.hasRow) {
            // A bare column on its own is not a scalar value.
            return fail();
        }
        NodePtr node = makeNode(Node::Cell);
        node->r1 = ref.row;
        node->c1 = ref.column;
        return node;
    }

    static void normalizeRange(const NodePtr &node)
    {
        if (node->r1 >= 0 && node->r2 >= 0 && node->r1 > node->r2)
            std::swap(node->r1, node->r2);
        if (node->c1 >= 0 && node->c2 >= 0 && node->c1 > node->c2)
            std::swap(node->c1, node->c2);
    }

    NodePtr binary(Tok op, const NodePtr &left, const NodePtr &right)
    {
        if (!left || !right)
            return fail();
        NodePtr node = makeNode(Node::Binary);
        node->op = op;
        node->children.push_back(left);
        node->children.push_back(right);
        return node;
    }

    NodePtr fail()
    {
        m_ok = false;
        return nullptr;
    }

    Lexer m_lexer;
    bool m_ok = true;
};

// ---------------------------------------------------------------------------
// Evaluator
// ---------------------------------------------------------------------------

class Evaluator
{
public:
    explicit Evaluator(const Sheet &sheet)
        : m_sheet(sheet)
    {
    }

    Value eval(const NodePtr &node)
    {
        if (!node)
            return Value::error(QStringLiteral("#ERROR!"));
        switch (node->kind) {
        case Node::Num:
            return Value::number(node->num);
        case Node::Str:
            return Value::text(node->str);
        case Node::Bool:
            return Value::boolean(node->boolean);
        case Node::Cell:
            return cell(node->r1, node->c1);
        case Node::Range:
            // A range used where a single value is expected: a 1x1 range is
            // that cell; anything larger is an error.
            return rangeAsScalar(node);
        case Node::Unary:
            return evalUnary(node);
        case Node::Binary:
            return evalBinary(node);
        case Node::Call:
            return evalCall(node);
        }
        return Value::error(QStringLiteral("#ERROR!"));
    }

private:
    Value cell(int row, int col)
    {
        if (row < 0 || col < 0 || row >= m_sheet.rowCount() || col >= m_sheet.columnCount())
            return Value::empty();
        return m_sheet.cellValue(row, col);
    }

    Value rangeAsScalar(const NodePtr &node)
    {
        const int r1 = node->r1 < 0 ? 0 : node->r1;
        const int r2 = node->r2 < 0 ? m_sheet.rowCount() - 1 : node->r2;
        const int c1 = node->c1 < 0 ? 0 : node->c1;
        const int c2 = node->c2 < 0 ? m_sheet.columnCount() - 1 : node->c2;
        if (r1 == r2 && c1 == c2)
            return cell(r1, c1);
        return Value::error(QStringLiteral("#VALUE!"));
    }

    // Flattens an argument node into a list of values: a range becomes all of
    // its cells, anything else a single value.
    void collect(const NodePtr &node, std::vector<Value> &out)
    {
        if (node->kind == Node::Range) {
            const int r1 = node->r1 < 0 ? 0 : node->r1;
            const int r2 = node->r2 < 0 ? m_sheet.rowCount() - 1 : node->r2;
            const int c1 = node->c1 < 0 ? 0 : node->c1;
            const int c2 = node->c2 < 0 ? m_sheet.columnCount() - 1 : node->c2;
            for (int r = r1; r <= r2; ++r)
                for (int c = c1; c <= c2; ++c)
                    out.push_back(cell(r, c));
        } else {
            out.push_back(eval(node));
        }
    }

    // Numbers from a set of argument nodes. Text and empty cells inside a
    // range are ignored (as in spreadsheets); a non-numeric scalar argument
    // is an error. Returns false and sets err on error.
    bool collectNumbers(const std::vector<NodePtr> &args, std::vector<double> &out, Value *err)
    {
        for (const NodePtr &arg : args) {
            if (arg->kind == Node::Range) {
                std::vector<Value> cells;
                collect(arg, cells);
                for (const Value &v : cells) {
                    if (v.isError()) {
                        *err = v;
                        return false;
                    }
                    if (v.type() == ValueType::Number || v.type() == ValueType::Boolean)
                        out.push_back(v.rawNumber());
                    // text and empty cells in a range are skipped
                }
            } else {
                const Value v = eval(arg);
                if (v.isError()) {
                    *err = v;
                    return false;
                }
                if (v.isEmpty())
                    continue;
                bool ok = false;
                const double d = v.toNumber(&ok);
                if (!ok) {
                    *err = Value::error(QStringLiteral("#VALUE!"));
                    return false;
                }
                out.push_back(d);
            }
        }
        return true;
    }

    Value evalUnary(const NodePtr &node)
    {
        const Value v = eval(node->children[0]);
        if (v.isError())
            return v;
        if (node->op == Tok::Plus || node->op == Tok::Minus || node->op == Tok::Percent) {
            bool ok = false;
            double d = v.toNumber(&ok);
            if (!ok)
                return Value::error(QStringLiteral("#VALUE!"));
            if (node->op == Tok::Minus)
                d = -d;
            else if (node->op == Tok::Percent)
                d = d / 100.0;
            return Value::number(d);
        }
        return Value::error(QStringLiteral("#ERROR!"));
    }

    Value evalBinary(const NodePtr &node)
    {
        const Value a = eval(node->children[0]);
        if (a.isError())
            return a;
        const Value b = eval(node->children[1]);
        if (b.isError())
            return b;

        if (node->op == Tok::Amp)
            return Value::text(a.toText() + b.toText());

        if (node->op == Tok::Eq || node->op == Tok::Ne || node->op == Tok::Lt || node->op == Tok::Gt
            || node->op == Tok::Le || node->op == Tok::Ge)
            return compare(node->op, a, b);

        bool ok1 = false, ok2 = false;
        const double x = a.toNumber(&ok1);
        const double y = b.toNumber(&ok2);
        if (!ok1 || !ok2)
            return Value::error(QStringLiteral("#VALUE!"));
        switch (node->op) {
        case Tok::Plus:
            return Value::number(x + y);
        case Tok::Minus:
            return Value::number(x - y);
        case Tok::Star:
            return Value::number(x * y);
        case Tok::Slash:
            if (y == 0.0)
                return Value::error(QStringLiteral("#DIV/0!"));
            return Value::number(x / y);
        case Tok::Caret: {
            const double r = std::pow(x, y);
            if (std::isnan(r) || std::isinf(r))
                return Value::error(QStringLiteral("#NUM!"));
            return Value::number(r);
        }
        default:
            break;
        }
        return Value::error(QStringLiteral("#ERROR!"));
    }

    Value compare(Tok op, const Value &a, const Value &b)
    {
        int cmp = 0;
        // Numbers (and booleans) compare numerically; otherwise compare text
        // case-insensitively, as spreadsheets do.
        const bool aNum = a.type() == ValueType::Number || a.type() == ValueType::Boolean
            || a.isEmpty();
        const bool bNum = b.type() == ValueType::Number || b.type() == ValueType::Boolean
            || b.isEmpty();
        if (aNum && bNum) {
            const double x = a.toNumber();
            const double y = b.toNumber();
            cmp = x < y ? -1 : (x > y ? 1 : 0);
        } else {
            cmp = QString::compare(a.toText(), b.toText(), Qt::CaseInsensitive);
            cmp = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
        }
        switch (op) {
        case Tok::Eq:
            return Value::boolean(cmp == 0);
        case Tok::Ne:
            return Value::boolean(cmp != 0);
        case Tok::Lt:
            return Value::boolean(cmp < 0);
        case Tok::Gt:
            return Value::boolean(cmp > 0);
        case Tok::Le:
            return Value::boolean(cmp <= 0);
        case Tok::Ge:
            return Value::boolean(cmp >= 0);
        default:
            break;
        }
        return Value::error(QStringLiteral("#ERROR!"));
    }

    // -- functions ----------------------------------------------------------

    Value evalCall(const NodePtr &node);

    // Helpers shared by several functions.
    static double meanOf(const std::vector<double> &v)
    {
        double s = 0.0;
        for (double x : v)
            s += x;
        return v.empty() ? 0.0 : s / v.size();
    }
    static double sumOf(const std::vector<double> &v)
    {
        double s = 0.0;
        for (double x : v)
            s += x;
        return s;
    }
    // Sample (n-1) or population (n) variance.
    static double variance(const std::vector<double> &v, bool sample)
    {
        const int n = int(v.size());
        if (n < (sample ? 2 : 1))
            return std::nan("");
        const double m = meanOf(v);
        double s = 0.0;
        for (double x : v)
            s += (x - m) * (x - m);
        return s / (sample ? n - 1 : n);
    }

    // Financial primitives (standard annuity formulas; sign convention as in
    // spreadsheets: money in is positive, money out negative).
    static double finPmt(double rate, double nper, double pv, double fv, double type)
    {
        if (rate == 0.0)
            return -(pv + fv) / nper;
        const double p = std::pow(1 + rate, nper);
        return -(pv * p + fv) * rate / ((1 + rate * type) * (p - 1));
    }
    static double finFv(double rate, double nper, double pmt, double pv, double type)
    {
        if (rate == 0.0)
            return -(pv + pmt * nper);
        const double p = std::pow(1 + rate, nper);
        return -(pv * p + pmt * (1 + rate * type) * (p - 1) / rate);
    }
    static double finPv(double rate, double nper, double pmt, double fv, double type)
    {
        if (rate == 0.0)
            return -(fv + pmt * nper);
        const double p = std::pow(1 + rate, nper);
        return -(fv + pmt * (1 + rate * type) * (p - 1) / rate) / p;
    }

    const Sheet &m_sheet;
};

// Signatures for argument-count checks: a function wants between min and max
// arguments (max -1 means "any number").
struct ArgSpec
{
    int min;
    int max;
};

Value Evaluator::evalCall(const NodePtr &node)
{
    const QString &name = node->func;
    const std::vector<NodePtr> &args = node->children;
    const int argc = int(args.size());
    auto wrongArgs = [] { return Value::error(QStringLiteral("#VALUE!")); };
    auto numError = [] { return Value::error(QStringLiteral("#NUM!")); };

    // A scalar numeric argument at position i.
    auto numAt = [&](int i, bool *ok) -> double {
        const Value v = eval(args[size_t(i)]);
        if (v.isError()) {
            *ok = false;
            return 0.0;
        }
        bool good = false;
        const double d = v.toNumber(&good);
        *ok = good;
        return d;
    };

    // ---- aggregation / statistics ----
    if (name == QLatin1String("SUM") || name == QLatin1String("AVERAGE")
        || name == QLatin1String("MIN") || name == QLatin1String("MAX")
        || name == QLatin1String("MEDIAN") || name == QLatin1String("PRODUCT")
        || name == QLatin1String("STDEV") || name == QLatin1String("STDEVP")
        || name == QLatin1String("VAR") || name == QLatin1String("VARP")
        || name == QLatin1String("GEOMEAN") || name == QLatin1String("HARMEAN")
        || name == QLatin1String("MODE") || name == QLatin1String("AVEDEV")
        || name == QLatin1String("DEVSQ") || name == QLatin1String("SUMSQ")) {
        if (argc < 1)
            return wrongArgs();
        std::vector<double> v;
        Value err;
        if (!collectNumbers(args, v, &err))
            return err;

        if (name == QLatin1String("SUM"))
            return Value::number(sumOf(v));
        if (name == QLatin1String("SUMSQ")) {
            double s = 0.0;
            for (double x : v)
                s += x * x;
            return Value::number(s);
        }
        if (name == QLatin1String("PRODUCT")) {
            double p = 1.0;
            for (double x : v)
                p *= x;
            return Value::number(v.empty() ? 0.0 : p);
        }
        if (v.empty())
            return name == QLatin1String("MIN") || name == QLatin1String("MAX")
                ? Value::number(0.0)
                : Value::error(QStringLiteral("#DIV/0!"));
        if (name == QLatin1String("AVERAGE"))
            return Value::number(meanOf(v));
        if (name == QLatin1String("MIN"))
            return Value::number(*std::min_element(v.begin(), v.end()));
        if (name == QLatin1String("MAX"))
            return Value::number(*std::max_element(v.begin(), v.end()));
        if (name == QLatin1String("MEDIAN")) {
            std::sort(v.begin(), v.end());
            const int n = int(v.size());
            return Value::number(n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0);
        }
        if (name == QLatin1String("STDEV") || name == QLatin1String("VAR")) {
            const double var = variance(v, true);
            if (std::isnan(var))
                return Value::error(QStringLiteral("#DIV/0!"));
            return Value::number(name == QLatin1String("VAR") ? var : std::sqrt(var));
        }
        if (name == QLatin1String("STDEVP") || name == QLatin1String("VARP")) {
            const double var = variance(v, false);
            if (std::isnan(var))
                return Value::error(QStringLiteral("#DIV/0!"));
            return Value::number(name == QLatin1String("VARP") ? var : std::sqrt(var));
        }
        if (name == QLatin1String("GEOMEAN")) {
            double logs = 0.0;
            for (double x : v) {
                if (x <= 0.0)
                    return numError();
                logs += std::log(x);
            }
            return Value::number(std::exp(logs / v.size()));
        }
        if (name == QLatin1String("HARMEAN")) {
            double s = 0.0;
            for (double x : v) {
                if (x == 0.0)
                    return numError();
                s += 1.0 / x;
            }
            return Value::number(v.size() / s);
        }
        if (name == QLatin1String("MODE")) {
            QHash<double, int> freq;
            double best = v[0];
            int bestCount = 0;
            for (double x : v) {
                const int c = ++freq[x];
                if (c > bestCount) {
                    bestCount = c;
                    best = x;
                }
            }
            if (bestCount < 2)
                return Value::error(QStringLiteral("#N/A"));
            return Value::number(best);
        }
        if (name == QLatin1String("AVEDEV")) {
            const double m = meanOf(v);
            double s = 0.0;
            for (double x : v)
                s += std::fabs(x - m);
            return Value::number(s / v.size());
        }
        if (name == QLatin1String("DEVSQ")) {
            const double m = meanOf(v);
            double s = 0.0;
            for (double x : v)
                s += (x - m) * (x - m);
            return Value::number(s);
        }
    }

    if (name == QLatin1String("COUNT") || name == QLatin1String("COUNTA")) {
        int count = 0;
        for (const NodePtr &arg : args) {
            std::vector<Value> cells;
            collect(arg, cells);
            for (const Value &v : cells) {
                if (name == QLatin1String("COUNTA")) {
                    if (!v.isEmpty())
                        ++count;
                } else {
                    bool ok = false;
                    if (v.type() == ValueType::Number || v.type() == ValueType::Boolean) {
                        ++count;
                    } else if (v.type() == ValueType::Text) {
                        v.toNumber(&ok);
                        if (ok)
                            ++count;
                    }
                }
            }
        }
        return Value::number(count);
    }

    if (name == QLatin1String("PERCENTILE") || name == QLatin1String("QUARTILE")
        || name == QLatin1String("LARGE") || name == QLatin1String("SMALL")) {
        if (argc != 2)
            return wrongArgs();
        std::vector<double> v;
        Value err;
        if (!collectNumbers({args[0]}, v, &err))
            return err;
        if (v.empty())
            return numError();
        bool ok = false;
        const double k = numAt(1, &ok);
        if (!ok)
            return wrongArgs();
        std::sort(v.begin(), v.end());
        const int n = int(v.size());
        if (name == QLatin1String("LARGE") || name == QLatin1String("SMALL")) {
            const int rank = int(k);
            if (rank < 1 || rank > n)
                return numError();
            return Value::number(name == QLatin1String("LARGE") ? v[n - rank] : v[rank - 1]);
        }
        double frac = k;
        if (name == QLatin1String("QUARTILE")) {
            if (k < 0 || k > 4)
                return numError();
            frac = k / 4.0;
        } else if (frac < 0.0 || frac > 1.0) {
            return numError();
        }
        // Linear interpolation between closest ranks (inclusive method).
        const double pos = frac * (n - 1);
        const int lo = int(std::floor(pos));
        const double d = pos - lo;
        if (lo + 1 < n)
            return Value::number(v[lo] + d * (v[lo + 1] - v[lo]));
        return Value::number(v[lo]);
    }

    if (name == QLatin1String("CORREL") || name == QLatin1String("COVAR")
        || name == QLatin1String("SLOPE") || name == QLatin1String("INTERCEPT")
        || name == QLatin1String("RSQ")) {
        if (argc != 2)
            return wrongArgs();
        // Paired values: both ranges must be the same length.
        std::vector<Value> ca, cb;
        collect(args[0], ca);
        collect(args[1], cb);
        if (ca.size() != cb.size() || ca.empty())
            return Value::error(QStringLiteral("#N/A"));
        std::vector<double> xs, ys;
        for (size_t i = 0; i < ca.size(); ++i) {
            const bool an = ca[i].type() == ValueType::Number || ca[i].type() == ValueType::Boolean;
            const bool bn = cb[i].type() == ValueType::Number || cb[i].type() == ValueType::Boolean;
            if (ca[i].isError())
                return ca[i];
            if (cb[i].isError())
                return cb[i];
            if (an && bn) {
                // CORREL/COVAR: y first is not meaningful; keep pair order.
                xs.push_back(cb[i].rawNumber());
                ys.push_back(ca[i].rawNumber());
            }
        }
        const int n = int(xs.size());
        if (n == 0)
            return Value::error(QStringLiteral("#DIV/0!"));
        double mx = 0, my = 0;
        for (int i = 0; i < n; ++i) {
            mx += xs[i];
            my += ys[i];
        }
        mx /= n;
        my /= n;
        double sxx = 0, syy = 0, sxy = 0;
        for (int i = 0; i < n; ++i) {
            sxx += (xs[i] - mx) * (xs[i] - mx);
            syy += (ys[i] - my) * (ys[i] - my);
            sxy += (xs[i] - mx) * (ys[i] - my);
        }
        if (name == QLatin1String("COVAR"))
            return Value::number(sxy / n);
        if (name == QLatin1String("SLOPE")) {
            if (sxx == 0.0)
                return Value::error(QStringLiteral("#DIV/0!"));
            return Value::number(sxy / sxx);
        }
        if (name == QLatin1String("INTERCEPT")) {
            if (sxx == 0.0)
                return Value::error(QStringLiteral("#DIV/0!"));
            return Value::number(my - (sxy / sxx) * mx);
        }
        // CORREL / RSQ
        if (sxx == 0.0 || syy == 0.0)
            return Value::error(QStringLiteral("#DIV/0!"));
        const double r = sxy / std::sqrt(sxx * syy);
        return Value::number(name == QLatin1String("RSQ") ? r * r : r);
    }

    if (name == QLatin1String("RANK")) {
        if (argc < 2 || argc > 3)
            return wrongArgs();
        bool ok = false;
        const double x = numAt(0, &ok);
        if (!ok)
            return wrongArgs();
        std::vector<double> v;
        Value err;
        if (!collectNumbers({args[1]}, v, &err))
            return err;
        bool ascending = false;
        if (argc == 3) {
            bool ok2 = false;
            ascending = numAt(2, &ok2) != 0.0;
            if (!ok2)
                return wrongArgs();
        }
        int rank = 1;
        bool found = false;
        for (double y : v) {
            if (y == x)
                found = true;
            if (ascending ? (y < x) : (y > x))
                ++rank;
        }
        if (!found)
            return Value::error(QStringLiteral("#N/A"));
        return Value::number(rank);
    }

    // ---- logical ----
    if (name == QLatin1String("IF")) {
        if (argc < 2 || argc > 3)
            return wrongArgs();
        const Value cond = eval(args[0]);
        if (cond.isError())
            return cond;
        bool ok = false;
        const bool c = cond.toBoolean(&ok);
        if (!ok)
            return wrongArgs();
        if (c)
            return eval(args[1]);
        return argc == 3 ? eval(args[2]) : Value::boolean(false);
    }
    if (name == QLatin1String("IFERROR")) {
        if (argc != 2)
            return wrongArgs();
        const Value v = eval(args[0]);
        return v.isError() ? eval(args[1]) : v;
    }
    if (name == QLatin1String("AND") || name == QLatin1String("OR")) {
        if (argc < 1)
            return wrongArgs();
        bool result = (name == QLatin1String("AND"));
        for (const NodePtr &arg : args) {
            std::vector<Value> cells;
            collect(arg, cells);
            for (const Value &v : cells) {
                if (v.isError())
                    return v;
                if (v.isEmpty())
                    continue;
                bool ok = false;
                const bool b = v.toBoolean(&ok);
                if (!ok)
                    return wrongArgs();
                if (name == QLatin1String("AND"))
                    result = result && b;
                else
                    result = result || b;
            }
        }
        return Value::boolean(result);
    }
    if (name == QLatin1String("NOT")) {
        if (argc != 1)
            return wrongArgs();
        const Value v = eval(args[0]);
        if (v.isError())
            return v;
        bool ok = false;
        const bool b = v.toBoolean(&ok);
        if (!ok)
            return wrongArgs();
        return Value::boolean(!b);
    }
    if (name == QLatin1String("TRUE")) {
        if (argc != 0)
            return wrongArgs();
        return Value::boolean(true);
    }
    if (name == QLatin1String("FALSE")) {
        if (argc != 0)
            return wrongArgs();
        return Value::boolean(false);
    }

    // ---- maths ----
    auto oneArg = [&](std::function<double(double)> f, bool checkDomain = false,
                      std::function<bool(double)> domain = {}) -> Value {
        if (argc != 1)
            return wrongArgs();
        bool ok = false;
        const double x = numAt(0, &ok);
        if (!ok)
            return wrongArgs();
        if (checkDomain && domain && !domain(x))
            return numError();
        const double r = f(x);
        if (std::isnan(r) || std::isinf(r))
            return numError();
        return Value::number(r);
    };
    if (name == QLatin1String("ABS"))
        return oneArg([](double x) { return std::fabs(x); });
    if (name == QLatin1String("SQRT"))
        return oneArg([](double x) { return std::sqrt(x); }, true, [](double x) { return x >= 0; });
    if (name == QLatin1String("EXP"))
        return oneArg([](double x) { return std::exp(x); });
    if (name == QLatin1String("LN"))
        return oneArg([](double x) { return std::log(x); }, true, [](double x) { return x > 0; });
    if (name == QLatin1String("LOG10"))
        return oneArg([](double x) { return std::log10(x); }, true, [](double x) { return x > 0; });
    if (name == QLatin1String("INT"))
        return oneArg([](double x) { return std::floor(x); });
    if (name == QLatin1String("SIGN"))
        return oneArg([](double x) { return double((x > 0) - (x < 0)); });
    if (name == QLatin1String("SIN"))
        return oneArg([](double x) { return std::sin(x); });
    if (name == QLatin1String("COS"))
        return oneArg([](double x) { return std::cos(x); });
    if (name == QLatin1String("TAN"))
        return oneArg([](double x) { return std::tan(x); });

    if (name == QLatin1String("PI")) {
        if (argc != 0)
            return wrongArgs();
        return Value::number(M_PI);
    }
    if (name == QLatin1String("LOG")) {
        if (argc < 1 || argc > 2)
            return wrongArgs();
        bool ok = false;
        const double x = numAt(0, &ok);
        if (!ok || x <= 0)
            return x <= 0 ? numError() : wrongArgs();
        double base = 10.0;
        if (argc == 2) {
            bool ok2 = false;
            base = numAt(1, &ok2);
            if (!ok2)
                return wrongArgs();
            if (base <= 0 || base == 1.0)
                return numError();
        }
        return Value::number(std::log(x) / std::log(base));
    }
    if (name == QLatin1String("POWER")) {
        if (argc != 2)
            return wrongArgs();
        bool ok1 = false, ok2 = false;
        const double x = numAt(0, &ok1);
        const double y = numAt(1, &ok2);
        if (!ok1 || !ok2)
            return wrongArgs();
        const double r = std::pow(x, y);
        if (std::isnan(r) || std::isinf(r))
            return numError();
        return Value::number(r);
    }
    if (name == QLatin1String("MOD")) {
        if (argc != 2)
            return wrongArgs();
        bool ok1 = false, ok2 = false;
        const double x = numAt(0, &ok1);
        const double y = numAt(1, &ok2);
        if (!ok1 || !ok2)
            return wrongArgs();
        if (y == 0.0)
            return Value::error(QStringLiteral("#DIV/0!"));
        double r = std::fmod(x, y);
        if (r != 0.0 && ((r < 0) != (y < 0)))
            r += y; // result takes the divisor's sign, as in spreadsheets
        return Value::number(r);
    }
    if (name == QLatin1String("ROUND") || name == QLatin1String("ROUNDUP")
        || name == QLatin1String("ROUNDDOWN") || name == QLatin1String("TRUNC")) {
        if (argc < 1 || argc > 2)
            return wrongArgs();
        bool ok = false;
        const double x = numAt(0, &ok);
        if (!ok)
            return wrongArgs();
        int digits = 0;
        if (argc == 2) {
            bool ok2 = false;
            digits = int(numAt(1, &ok2));
            if (!ok2)
                return wrongArgs();
        } else if (name == QLatin1String("TRUNC")) {
            digits = 0;
        }
        const double factor = std::pow(10.0, digits);
        const double scaled = x * factor;
        double r;
        if (name == QLatin1String("ROUND"))
            r = std::round(scaled);
        else if (name == QLatin1String("ROUNDUP"))
            r = scaled < 0 ? std::floor(scaled) : std::ceil(scaled);
        else // ROUNDDOWN and TRUNC: toward zero
            r = std::trunc(scaled);
        return Value::number(r / factor);
    }
    if (name == QLatin1String("CEILING") || name == QLatin1String("FLOOR")) {
        if (argc < 1 || argc > 2)
            return wrongArgs();
        bool ok = false;
        const double x = numAt(0, &ok);
        if (!ok)
            return wrongArgs();
        double sig = 1.0;
        if (argc == 2) {
            bool ok2 = false;
            sig = numAt(1, &ok2);
            if (!ok2)
                return wrongArgs();
        }
        if (sig == 0.0)
            return Value::number(0.0);
        if (name == QLatin1String("CEILING"))
            return Value::number(std::ceil(x / sig) * sig);
        return Value::number(std::floor(x / sig) * sig);
    }
    if (name == QLatin1String("SUMPRODUCT")) {
        if (argc < 1)
            return wrongArgs();
        std::vector<std::vector<Value>> arrays;
        size_t len = 0;
        for (const NodePtr &arg : args) {
            std::vector<Value> cells;
            collect(arg, cells);
            if (arrays.empty())
                len = cells.size();
            else if (cells.size() != len)
                return wrongArgs();
            arrays.push_back(std::move(cells));
        }
        double total = 0.0;
        for (size_t i = 0; i < len; ++i) {
            double product = 1.0;
            for (const auto &arr : arrays) {
                if (arr[i].isError())
                    return arr[i];
                product *= arr[i].toNumber();
            }
            total += product;
        }
        return Value::number(total);
    }

    // ---- text ----
    if (name == QLatin1String("CONCAT") || name == QLatin1String("CONCATENATE")) {
        QString out;
        for (const NodePtr &arg : args) {
            std::vector<Value> cells;
            collect(arg, cells);
            for (const Value &v : cells) {
                if (v.isError())
                    return v;
                out += v.toText();
            }
        }
        return Value::text(out);
    }
    if (name == QLatin1String("LEN")) {
        if (argc != 1)
            return wrongArgs();
        const Value v = eval(args[0]);
        if (v.isError())
            return v;
        return Value::number(v.toText().size());
    }
    if (name == QLatin1String("UPPER") || name == QLatin1String("LOWER")
        || name == QLatin1String("TRIM")) {
        if (argc != 1)
            return wrongArgs();
        const Value v = eval(args[0]);
        if (v.isError())
            return v;
        QString s = v.toText();
        if (name == QLatin1String("UPPER"))
            s = s.toUpper();
        else if (name == QLatin1String("LOWER"))
            s = s.toLower();
        else
            s = s.simplified();
        return Value::text(s);
    }
    if (name == QLatin1String("LEFT") || name == QLatin1String("RIGHT")) {
        if (argc < 1 || argc > 2)
            return wrongArgs();
        const Value v = eval(args[0]);
        if (v.isError())
            return v;
        const QString s = v.toText();
        int n = 1;
        if (argc == 2) {
            bool ok = false;
            n = int(numAt(1, &ok));
            if (!ok)
                return wrongArgs();
            if (n < 0)
                return numError();
        }
        return Value::text(name == QLatin1String("LEFT") ? s.left(n) : s.right(n));
    }
    if (name == QLatin1String("MID")) {
        if (argc != 3)
            return wrongArgs();
        const Value v = eval(args[0]);
        if (v.isError())
            return v;
        const QString s = v.toText();
        bool ok1 = false, ok2 = false;
        const int start = int(numAt(1, &ok1));
        const int len = int(numAt(2, &ok2));
        if (!ok1 || !ok2)
            return wrongArgs();
        if (start < 1 || len < 0)
            return numError();
        return Value::text(s.mid(start - 1, len));
    }
    if (name == QLatin1String("SUBSTITUTE")) {
        if (argc < 3 || argc > 4)
            return wrongArgs();
        const Value v0 = eval(args[0]);
        const Value v1 = eval(args[1]);
        const Value v2 = eval(args[2]);
        if (v0.isError())
            return v0;
        if (v1.isError())
            return v1;
        if (v2.isError())
            return v2;
        QString s = v0.toText();
        const QString from = v1.toText();
        const QString to = v2.toText();
        if (from.isEmpty())
            return Value::text(s);
        if (argc == 4) {
            bool ok = false;
            const int which = int(numAt(3, &ok));
            if (!ok || which < 1)
                return which < 1 ? numError() : wrongArgs();
            int pos = 0, count = 0;
            while ((pos = s.indexOf(from, pos)) >= 0) {
                if (++count == which) {
                    s.replace(pos, from.size(), to);
                    break;
                }
                pos += from.size();
            }
            return Value::text(s);
        }
        return Value::text(s.replace(from, to));
    }

    // ---- finance ----
    if (name == QLatin1String("PMT") || name == QLatin1String("FV")
        || name == QLatin1String("PV") || name == QLatin1String("NPER")) {
        // Common shape: f(rate, a, b, [c], [type]).
        if (argc < 3 || argc > 5)
            return wrongArgs();
        bool ok = true;
        double a[5] = {0, 0, 0, 0, 0};
        for (int i = 0; i < argc; ++i) {
            bool good = false;
            a[i] = numAt(i, &good);
            ok = ok && good;
        }
        if (!ok)
            return wrongArgs();
        const double rate = a[0];
        const double type = argc >= 5 ? a[4] : 0.0;
        if (name == QLatin1String("PMT")) {
            const double extra = argc >= 4 ? a[3] : 0.0; // fv
            if (rate == 0.0 && a[1] == 0.0)
                return numError();
            return Value::number(finPmt(rate, a[1], a[2], extra, type));
        }
        if (name == QLatin1String("FV")) {
            const double extra = argc >= 4 ? a[3] : 0.0; // pv
            return Value::number(finFv(rate, a[1], a[2], extra, type));
        }
        if (name == QLatin1String("PV")) {
            const double extra = argc >= 4 ? a[3] : 0.0; // fv
            return Value::number(finPv(rate, a[1], a[2], extra, type));
        }
        // NPER(rate, pmt, pv, [fv], [type])
        const double pmt = a[1];
        const double pv = a[2];
        const double fv = argc >= 4 ? a[3] : 0.0;
        if (rate == 0.0) {
            if (pmt == 0.0)
                return numError();
            return Value::number(-(pv + fv) / pmt);
        }
        const double num = pmt * (1 + rate * type) - fv * rate;
        const double den = pv * rate + pmt * (1 + rate * type);
        if (den == 0.0 || (num / den) <= 0.0)
            return numError();
        return Value::number(std::log(num / den) / std::log(1 + rate));
    }
    if (name == QLatin1String("IPMT") || name == QLatin1String("PPMT")) {
        // f(rate, per, nper, pv, [fv], [type])
        if (argc < 4 || argc > 6)
            return wrongArgs();
        bool ok = true;
        double a[6] = {0, 0, 0, 0, 0, 0};
        for (int i = 0; i < argc; ++i) {
            bool good = false;
            a[i] = numAt(i, &good);
            ok = ok && good;
        }
        if (!ok)
            return wrongArgs();
        const double rate = a[0];
        const double per = a[1];
        const double nper = a[2];
        const double pv = a[3];
        const double fv = argc >= 5 ? a[4] : 0.0;
        const double type = argc >= 6 ? a[5] : 0.0;
        if (per < 1 || per > nper)
            return numError();
        const double pmt = finPmt(rate, nper, pv, fv, type);
        double ipmt;
        if (int(per) == 1) {
            ipmt = (type > 0) ? 0.0 : -pv;
        } else {
            if (type > 0)
                ipmt = finFv(rate, per - 2, pmt, pv, 1) - pmt;
            else
                ipmt = finFv(rate, per - 1, pmt, pv, 0);
        }
        ipmt *= rate;
        if (name == QLatin1String("IPMT"))
            return Value::number(ipmt);
        return Value::number(pmt - ipmt); // PPMT
    }
    if (name == QLatin1String("NPV")) {
        if (argc < 2)
            return wrongArgs();
        bool ok = false;
        const double rate = numAt(0, &ok);
        if (!ok)
            return wrongArgs();
        if (rate == -1.0)
            return Value::error(QStringLiteral("#DIV/0!"));
        std::vector<double> v;
        Value err;
        if (!collectNumbers({args.begin() + 1, args.end()}, v, &err))
            return err;
        double total = 0.0;
        double discount = 1.0;
        for (double cf : v) {
            discount *= (1 + rate);
            total += cf / discount;
        }
        return Value::number(total);
    }
    if (name == QLatin1String("IRR") || name == QLatin1String("RATE")) {
        if (name == QLatin1String("IRR")) {
            if (argc < 1 || argc > 2)
                return wrongArgs();
            std::vector<double> v;
            Value err;
            if (!collectNumbers({args[0]}, v, &err))
                return err;
            if (v.size() < 2)
                return numError();
            double guess = 0.1;
            if (argc == 2) {
                bool ok = false;
                guess = numAt(1, &ok);
                if (!ok)
                    return wrongArgs();
            }
            // Newton's method on NPV with cash flow 0 at period 0.
            auto npv = [&](double r) {
                double sum = 0.0;
                for (size_t i = 0; i < v.size(); ++i)
                    sum += v[i] / std::pow(1 + r, double(i));
                return sum;
            };
            double r = guess;
            for (int iter = 0; iter < 100; ++iter) {
                const double f = npv(r);
                // Numerical derivative.
                const double h = 1e-6;
                const double df = (npv(r + h) - npv(r - h)) / (2 * h);
                if (df == 0.0)
                    break;
                const double next = r - f / df;
                if (std::isnan(next) || std::isinf(next) || next <= -1.0)
                    break;
                if (std::fabs(next - r) < 1e-10) {
                    r = next;
                    if (std::fabs(npv(r)) < 1e-6)
                        return Value::number(r);
                    break;
                }
                r = next;
            }
            if (std::fabs(npv(r)) < 1e-6)
                return Value::number(r);
            return numError();
        }
        // RATE(nper, pmt, pv, [fv], [type], [guess])
        if (argc < 3 || argc > 6)
            return wrongArgs();
        bool ok = true;
        double a[6] = {0, 0, 0, 0, 0, 0.1};
        for (int i = 0; i < argc; ++i) {
            bool good = false;
            a[i] = numAt(i, &good);
            ok = ok && good;
        }
        if (!ok)
            return wrongArgs();
        const double nper = a[0];
        const double pmt = a[1];
        const double pv = a[2];
        const double fv = argc >= 4 ? a[3] : 0.0;
        const double type = argc >= 5 ? a[4] : 0.0;
        double r = argc >= 6 ? a[5] : 0.1;
        auto f = [&](double rate) {
            if (rate == 0.0)
                return pv + pmt * nper + fv;
            const double p = std::pow(1 + rate, nper);
            return pv * p + pmt * (1 + rate * type) * (p - 1) / rate + fv;
        };
        for (int iter = 0; iter < 100; ++iter) {
            const double y = f(r);
            const double h = 1e-6;
            const double dy = (f(r + h) - f(r - h)) / (2 * h);
            if (dy == 0.0)
                break;
            const double next = r - y / dy;
            if (std::isnan(next) || std::isinf(next) || next <= -1.0)
                break;
            if (std::fabs(next - r) < 1e-10) {
                r = next;
                break;
            }
            r = next;
        }
        if (std::fabs(f(r)) < 1e-6)
            return Value::number(r);
        return numError();
    }
    if (name == QLatin1String("SLN")) {
        if (argc != 3)
            return wrongArgs();
        bool ok = true;
        double cost = numAt(0, &ok), salvage = numAt(1, &ok), life = numAt(2, &ok);
        if (!ok)
            return wrongArgs();
        if (life == 0.0)
            return Value::error(QStringLiteral("#DIV/0!"));
        return Value::number((cost - salvage) / life);
    }
    if (name == QLatin1String("DDB")) {
        if (argc < 4 || argc > 5)
            return wrongArgs();
        bool ok = true;
        double cost = numAt(0, &ok), salvage = numAt(1, &ok), life = numAt(2, &ok),
               period = numAt(3, &ok);
        double factor = 2.0;
        if (argc == 5)
            factor = numAt(4, &ok);
        if (!ok)
            return wrongArgs();
        if (life <= 0 || period < 1 || period > life || cost < 0 || factor <= 0)
            return numError();
        const double rate = factor / life;
        double totalDep = 0.0;
        double dep = 0.0;
        for (int i = 1; i <= int(period); ++i) {
            dep = (cost - totalDep) * rate;
            const double floorDep = cost - salvage - totalDep;
            if (dep > floorDep)
                dep = floorDep < 0 ? 0.0 : floorDep;
            if (dep < 0)
                dep = 0.0;
            totalDep += dep;
        }
        return Value::number(dep);
    }

    return Value::error(QStringLiteral("#NAME?"));
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

Value evaluate(const QString &expression, const Sheet &sheet)
{
    Parser parser(expression);
    NodePtr ast = parser.parse();
    if (!ast)
        return Value::error(QStringLiteral("#ERROR!"));
    Evaluator evaluator(sheet);
    return evaluator.eval(ast);
}

QStringList functionNames()
{
    static const QStringList names = [] {
        QStringList list{
            // maths
            "ABS", "CEILING", "COS", "EXP", "FLOOR", "INT", "LN", "LOG", "LOG10", "MOD", "PI",
            "POWER", "ROUND", "ROUNDDOWN", "ROUNDUP", "SIGN", "SIN", "SQRT", "SUMPRODUCT", "SUMSQ",
            "TAN", "TRUNC",
            // statistics
            "AVEDEV", "AVERAGE", "CORREL", "COUNT", "COUNTA", "COVAR", "DEVSQ", "GEOMEAN", "HARMEAN",
            "INTERCEPT", "LARGE", "MAX", "MEDIAN", "MIN", "MODE", "PERCENTILE", "PRODUCT", "QUARTILE",
            "RANK", "RSQ", "SLOPE", "SMALL", "STDEV", "STDEVP", "SUM", "VAR", "VARP",
            // finance
            "DDB", "FV", "IPMT", "IRR", "NPER", "NPV", "PMT", "PPMT", "PV", "RATE", "SLN",
            // logical
            "AND", "FALSE", "IF", "IFERROR", "NOT", "OR", "TRUE",
            // text
            "CONCAT", "CONCATENATE", "LEFT", "LEN", "LOWER", "MID", "RIGHT", "SUBSTITUTE", "TRIM",
            "UPPER"};
        list.sort();
        return list;
    }();
    return names;
}

} // namespace Formula
