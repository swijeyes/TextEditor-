#include "latexhtml.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <array>
#include <tuple>

namespace {

// Limits that keep hostile or broken documents from hanging the preview.
constexpr int MaxDepth = 64;          // nested groups, environments and macros
constexpr int MaxExpansions = 20000;  // macro uses per document
constexpr int MaxOutput = 16 * 1024 * 1024;

// Placeholders resolved once the whole document has been seen, because a
// \ref can come before its \label. Private-use characters never appear in
// the escaped output otherwise.
const QChar RefOpen(0xE000);
const QChar RefClose(0xE001);
const QChar TocMark(0xE002);

QString esc(const QString &text)
{
    QString result = text.toHtmlEscaped();
    result.remove(RefOpen);
    result.remove(RefClose);
    result.remove(TocMark);
    return result;
}

QString escChar(QChar ch)
{
    switch (ch.unicode()) {
    case '<': return QStringLiteral("&lt;");
    case '>': return QStringLiteral("&gt;");
    case '&': return QStringLiteral("&amp;");
    case '"': return QStringLiteral("&quot;");
    default:
        if (ch == RefOpen || ch == RefClose || ch == TocMark)
            return QString();
        return QString(ch);
    }
}

bool isLetter(QChar ch)
{
    return (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z') || ch == u'@';
}

bool isBlank(QChar ch) { return ch == u' ' || ch == u'\t' || ch == u'\r'; }

// --- Symbols -------------------------------------------------------------------

const QHash<QString, QString> &mathSymbols()
{
    static const QHash<QString, QString> symbols = [] {
        QHash<QString, QString> s{
            // Greek
            {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"}, {"epsilon", "ϵ"},
            {"varepsilon", "ε"}, {"zeta", "ζ"}, {"eta", "η"}, {"theta", "θ"}, {"vartheta", "ϑ"},
            {"iota", "ι"}, {"kappa", "κ"}, {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"},
            {"pi", "π"}, {"varpi", "ϖ"}, {"rho", "ρ"}, {"varrho", "ϱ"}, {"sigma", "σ"},
            {"varsigma", "ς"}, {"tau", "τ"}, {"upsilon", "υ"}, {"phi", "ϕ"}, {"varphi", "φ"},
            {"chi", "χ"}, {"psi", "ψ"}, {"omega", "ω"}, {"Gamma", "Γ"}, {"Delta", "Δ"},
            {"Theta", "Θ"}, {"Lambda", "Λ"}, {"Xi", "Ξ"}, {"Pi", "Π"}, {"Sigma", "Σ"},
            {"Upsilon", "Υ"}, {"Phi", "Φ"}, {"Psi", "Ψ"}, {"Omega", "Ω"},
            // Big operators
            {"sum", "∑"}, {"prod", "∏"}, {"coprod", "∐"}, {"int", "∫"}, {"iint", "∬"},
            {"iiint", "∭"}, {"oint", "∮"}, {"bigcup", "⋃"}, {"bigcap", "⋂"}, {"bigoplus", "⨁"},
            {"bigotimes", "⨂"}, {"bigvee", "⋁"}, {"bigwedge", "⋀"},
            // Binary operators
            {"times", "×"}, {"cdot", "⋅"}, {"pm", "±"}, {"mp", "∓"}, {"div", "÷"}, {"ast", "∗"},
            {"star", "⋆"}, {"circ", "∘"}, {"bullet", "•"}, {"oplus", "⊕"}, {"ominus", "⊖"},
            {"otimes", "⊗"}, {"odot", "⊙"}, {"cup", "∪"}, {"cap", "∩"}, {"setminus", "∖"},
            {"wedge", "∧"}, {"land", "∧"}, {"vee", "∨"}, {"lor", "∨"}, {"dagger", "†"},
            {"ddagger", "‡"}, {"amalg", "⨿"}, {"wr", "≀"}, {"diamond", "⋄"},
            // Relations
            {"leq", "≤"}, {"le", "≤"}, {"geq", "≥"}, {"ge", "≥"}, {"neq", "≠"}, {"ne", "≠"},
            {"leqslant", "⩽"}, {"geqslant", "⩾"}, {"ll", "≪"}, {"gg", "≫"}, {"approx", "≈"},
            {"equiv", "≡"}, {"sim", "∼"}, {"simeq", "≃"}, {"cong", "≅"}, {"propto", "∝"},
            {"in", "∈"}, {"notin", "∉"}, {"ni", "∋"}, {"subset", "⊂"}, {"subseteq", "⊆"},
            {"supset", "⊃"}, {"supseteq", "⊇"}, {"subsetneq", "⊊"}, {"prec", "≺"}, {"succ", "≻"},
            {"preceq", "⪯"}, {"succeq", "⪰"}, {"perp", "⊥"}, {"parallel", "∥"}, {"mid", "∣"},
            {"vdash", "⊢"}, {"dashv", "⊣"}, {"models", "⊨"}, {"asymp", "≍"}, {"doteq", "≐"},
            {"coloneqq", "≔"}, {"triangleq", "≜"},
            // Arrows
            {"to", "→"}, {"rightarrow", "→"}, {"leftarrow", "←"}, {"gets", "←"},
            {"leftrightarrow", "↔"}, {"Rightarrow", "⇒"}, {"Leftarrow", "⇐"},
            {"Leftrightarrow", "⇔"}, {"implies", "⟹"}, {"impliedby", "⟸"}, {"iff", "⟺"},
            {"mapsto", "↦"}, {"longrightarrow", "⟶"}, {"longleftarrow", "⟵"},
            {"longmapsto", "⟼"}, {"Longrightarrow", "⟹"}, {"uparrow", "↑"}, {"downarrow", "↓"},
            {"Uparrow", "⇑"}, {"Downarrow", "⇓"}, {"nearrow", "↗"}, {"searrow", "↘"},
            {"hookrightarrow", "↪"}, {"rightharpoonup", "⇀"}, {"leadsto", "⇝"},
            // Delimiters
            {"langle", "⟨"}, {"rangle", "⟩"}, {"lfloor", "⌊"}, {"rfloor", "⌋"}, {"lceil", "⌈"},
            {"rceil", "⌉"}, {"lbrace", "{"}, {"rbrace", "}"}, {"lvert", "|"}, {"rvert", "|"},
            {"vert", "|"}, {"lVert", "‖"}, {"rVert", "‖"}, {"Vert", "‖"}, {"|", "‖"},
            {"backslash", "\\"}, {"{", "{"}, {"}", "}"},
            // Other symbols
            {"infty", "∞"}, {"partial", "∂"}, {"nabla", "∇"}, {"forall", "∀"}, {"exists", "∃"},
            {"nexists", "∄"}, {"neg", "¬"}, {"lnot", "¬"}, {"emptyset", "∅"}, {"varnothing", "∅"},
            {"aleph", "ℵ"}, {"hbar", "ℏ"}, {"ell", "ℓ"}, {"Re", "ℜ"}, {"Im", "ℑ"}, {"wp", "℘"},
            {"angle", "∠"}, {"triangle", "△"}, {"square", "□"}, {"Box", "□"}, {"top", "⊤"},
            {"bot", "⊥"}, {"prime", "′"}, {"degree", "°"}, {"cdots", "⋯"}, {"ldots", "…"},
            {"dots", "…"}, {"dotsc", "…"}, {"dotsb", "⋯"}, {"vdots", "⋮"}, {"ddots", "⋱"},
            {"therefore", "∴"}, {"because", "∵"}, {"checkmark", "✓"}, {"colon", ":"},
            {"surd", "√"}, {"flat", "♭"}, {"sharp", "♯"}, {"natural", "♮"}, {"clubsuit", "♣"},
            {"diamondsuit", "♢"}, {"heartsuit", "♡"}, {"spadesuit", "♠"}, {"imath", "ı"},
            {"jmath", "ȷ"},
        };
        return s;
    }();
    return symbols;
}

// Symbols that read better with space around them.
bool isRelation(const QString &symbol)
{
    static const QSet<QString> relations{
        "=", "<", ">", "≤", "≥", "≠", "⩽", "⩾", "≪", "≫", "≈", "≡", "∼", "≃", "≅", "∝", "∈",
        "∉", "∋", "⊂", "⊆", "⊃", "⊇", "⊊", "≺", "≻", "⪯", "⪰", "⊥", "∥", "∣", "⊢", "⊣",
        "⊨", "≍", "≐", "≔", "≜", "→", "←", "↔", "⇒", "⇐", "⇔", "⟹", "⟸", "⟺", "↦", "⟶",
        "⟵", "⟼", "↪", "⇝"};
    return relations.contains(symbol);
}

bool isBinaryOperator(const QString &symbol)
{
    static const QSet<QString> operators{"+", "−", "×", "⋅", "±", "∓", "÷", "∗", "∘", "⊕",
                                         "⊖", "⊗", "⊙", "∪", "∩", "∖", "∧", "∨"};
    return operators.contains(symbol);
}

const QHash<QString, QString> &textSymbols()
{
    static const QHash<QString, QString> symbols{
        {"LaTeX", "LaTeX"}, {"LaTeXe", "LaTeX2ε"}, {"TeX", "TeX"}, {"BibTeX", "BibTeX"},
        {"XeLaTeX", "XeLaTeX"}, {"LuaLaTeX", "LuaLaTeX"}, {"ldots", "…"}, {"dots", "…"},
        {"textellipsis", "…"}, {"textbackslash", "\\"}, {"textasciitilde", "~"},
        {"textasciicircum", "^"}, {"textbar", "|"}, {"textless", "<"}, {"textgreater", ">"},
        {"textunderscore", "_"}, {"textbraceleft", "{"}, {"textbraceright", "}"},
        {"textendash", "–"}, {"textemdash", "—"}, {"textbullet", "•"}, {"textperiodcentered", "·"},
        {"textquoteleft", "‘"}, {"textquoteright", "’"}, {"textquotedblleft", "“"},
        {"textquotedblright", "”"}, {"guillemotleft", "«"}, {"guillemotright", "»"},
        {"S", "§"}, {"P", "¶"}, {"copyright", "©"}, {"textcopyright", "©"},
        {"textregistered", "®"}, {"texttrademark", "™"}, {"pounds", "£"}, {"textsterling", "£"},
        {"euro", "€"}, {"texteuro", "€"}, {"textdegree", "°"}, {"dag", "†"}, {"ddag", "‡"},
        {"textdagger", "†"}, {"textdaggerdbl", "‡"}, {"ss", "ß"}, {"ae", "æ"}, {"AE", "Æ"},
        {"oe", "œ"}, {"OE", "Œ"}, {"o", "ø"}, {"O", "Ø"}, {"aa", "å"}, {"AA", "Å"},
        {"l", "ł"}, {"L", "Ł"}, {"i", "ı"}, {"j", "ȷ"}, {"quad", " "},
        {"qquad", "  "}, {"enspace", " "}, {"thinspace", " "},
        {"textvisiblespace", "␣"}, {"checkmark", "✓"}, {"textpm", "±"}, {"texttimes", "×"},
    };
    return symbols;
}

// Combining marks for accents: \'e, \"{o}, \hat{x}.
QChar accentMark(const QString &command)
{
    static const QHash<QString, ushort> marks{
        {"'", 0x0301}, {"`", 0x0300}, {"^", 0x0302}, {"\"", 0x0308}, {"~", 0x0303},
        {"=", 0x0304}, {".", 0x0307}, {"u", 0x0306}, {"v", 0x030C}, {"H", 0x030B},
        {"c", 0x0327}, {"d", 0x0323}, {"b", 0x0331}, {"r", 0x030A}, {"k", 0x0328},
        // Maths accents.
        {"hat", 0x0302}, {"widehat", 0x0302}, {"check", 0x030C}, {"tilde", 0x0303},
        {"widetilde", 0x0303}, {"acute", 0x0301}, {"grave", 0x0300}, {"dot", 0x0307},
        {"ddot", 0x0308}, {"dddot", 0x20DB}, {"breve", 0x0306}, {"bar", 0x0304},
        {"vec", 0x20D7}, {"overrightarrow", 0x20D7}, {"mathring", 0x030A}};
    return QChar(marks.value(command, 0));
}

// Letters in \mathbb, \mathcal and \mathfrak, from the Unicode
// mathematical alphanumeric symbols (with the older letterlike exceptions).
QString styledLetter(const QString &style, QChar ch)
{
    const ushort c = ch.unicode();
    const bool upper = c >= 'A' && c <= 'Z';
    const bool lower = c >= 'a' && c <= 'z';
    if (style == QLatin1String("mathbb")) {
        static const QHash<QChar, QString> exceptions{{u'C', "ℂ"}, {u'H', "ℍ"}, {u'N', "ℕ"},
                                                      {u'P', "ℙ"}, {u'Q', "ℚ"}, {u'R', "ℝ"},
                                                      {u'Z', "ℤ"}};
        if (exceptions.contains(ch))
            return exceptions.value(ch);
        if (c >= '0' && c <= '9')
            return QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D7D8 + c - '0')}.data(), 1);
        if (upper)
            return QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D538 + c - 'A')}.data(), 1);
        if (lower)
            return QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D552 + c - 'a')}.data(), 1);
    } else if (style == QLatin1String("mathcal") || style == QLatin1String("mathscr")) {
        static const QHash<QChar, QString> exceptions{{u'B', "ℬ"}, {u'E', "ℰ"}, {u'F', "ℱ"},
                                                      {u'H', "ℋ"}, {u'I', "ℐ"}, {u'L', "ℒ"},
                                                      {u'M', "ℳ"}, {u'R', "ℛ"}, {u'e', "ℯ"},
                                                      {u'g', "ℊ"}, {u'o', "ℴ"}};
        if (exceptions.contains(ch))
            return exceptions.value(ch);
        if (upper)
            return QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D49C + c - 'A')}.data(), 1);
        if (lower)
            return QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D4B6 + c - 'a')}.data(), 1);
    } else if (style == QLatin1String("mathfrak")) {
        static const QHash<QChar, QString> exceptions{{u'C', "ℭ"}, {u'H', "ℌ"}, {u'I', "ℑ"},
                                                      {u'R', "ℜ"}, {u'Z', "ℨ"}};
        if (exceptions.contains(ch))
            return exceptions.value(ch);
        if (upper)
            return QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D504 + c - 'A')}.data(), 1);
        if (lower)
            return QString::fromUcs4(std::array<char32_t, 1>{char32_t(0x1D51E + c - 'a')}.data(), 1);
    }
    return escChar(ch);
}

// Colours for \textcolor and \color: named ones, or #rrggbb built here, so
// nothing from the document reaches the style attribute unchecked.
QString namedColor(const QString &name)
{
    static const QHash<QString, QString> colors{
        {"red", "#ff0000"}, {"green", "#00a000"}, {"blue", "#0000ff"}, {"cyan", "#00b0b0"},
        {"magenta", "#ff00ff"}, {"yellow", "#c0c000"}, {"black", "#000000"},
        {"white", "#ffffff"}, {"gray", "#808080"}, {"darkgray", "#404040"},
        {"lightgray", "#bfbfbf"}, {"brown", "#bf8040"}, {"orange", "#ff8000"},
        {"purple", "#bf0040"}, {"teal", "#008080"}, {"violet", "#800080"}, {"olive", "#808000"},
        {"lime", "#bfff00"}, {"pink", "#ffbfbf"}};
    return colors.value(name.trimmed());
}

// --- Reading the source -----------------------------------------------------------

// The command name after a backslash at s[i]: letters, or one other character.
QString readCommandName(const QString &s, int &i)
{
    ++i; // the backslash
    if (i >= s.size())
        return QString();
    if (!isLetter(s.at(i)))
        return QString(s.at(i++));
    const int start = i;
    while (i < s.size() && isLetter(s.at(i)))
        ++i;
    return s.mid(start, i - start);
}

// Skips spaces and at most one line break, as TeX does after a command
// name or between arguments. A blank line (a new paragraph) is kept.
void skipSpace(const QString &s, int &i)
{
    while (i < s.size() && isBlank(s.at(i)))
        ++i;
    if (i < s.size() && s.at(i) == u'\n') {
        int j = i + 1;
        while (j < s.size() && isBlank(s.at(j)))
            ++j;
        if (j >= s.size() || s.at(j) != u'\n')
            i = j;
    }
}

// Reads a {group} starting at s[i] (which must be '{'), returning its
// contents. An unclosed group runs to the end.
QString readBraced(const QString &s, int &i)
{
    int depth = 0;
    const int start = i + 1;
    for (; i < s.size(); ++i) {
        const QChar ch = s.at(i);
        if (ch == u'\\') {
            ++i;
        } else if (ch == u'{') {
            ++depth;
        } else if (ch == u'}' && --depth == 0) {
            ++i;
            return s.mid(start, i - 1 - start);
        }
    }
    return s.mid(start);
}

// A command's argument: a {group}, a \command, or a single character.
QString readArg(const QString &s, int &i)
{
    skipSpace(s, i);
    if (i >= s.size())
        return QString();
    if (s.at(i) == u'{')
        return readBraced(s, i);
    if (s.at(i) == u'\\') {
        const int start = i;
        readCommandName(s, i);
        return s.mid(start, i - start);
    }
    if (s.at(i).isHighSurrogate() && i + 1 < s.size()) {
        i += 2;
        return s.mid(i - 2, 2);
    }
    return QString(s.at(i++));
}

// An optional [argument], if there is one.
bool readOptional(const QString &s, int &i, QString *out = nullptr)
{
    int j = i;
    skipSpace(s, j);
    if (j >= s.size() || s.at(j) != u'[')
        return false;
    int braces = 0;
    int brackets = 0;
    const int start = j + 1;
    for (; j < s.size(); ++j) {
        const QChar ch = s.at(j);
        if (ch == u'\\') {
            ++j;
        } else if (ch == u'{') {
            ++braces;
        } else if (ch == u'}') {
            --braces;
        } else if (ch == u'[' && braces == 0) {
            ++brackets;
        } else if (ch == u']' && braces == 0 && --brackets == 0) {
            if (out)
                *out = s.mid(start, j - start);
            i = j + 1;
            return true;
        }
    }
    if (out)
        *out = s.mid(start);
    i = int(s.size());
    return true;
}

void skipComment(const QString &s, int &i)
{
    const int end = s.indexOf(u'\n', i);
    i = end < 0 ? int(s.size()) : end + 1;
    while (i < s.size() && isBlank(s.at(i)))
        ++i;
}

// Finds the \end{name} matching a \begin{name} whose body starts at 'from',
// counting nested environments of the same name. Returns the position of
// the backslash, or -1.
int findEnd(const QString &s, int from, const QString &name)
{
    const QString begin = QStringLiteral("\\begin{") + name + u'}';
    const QString end = QStringLiteral("\\end{") + name + u'}';
    int depth = 1;
    for (int i = from; i < s.size(); ++i) {
        const QChar ch = s.at(i);
        if (ch == u'%') {
            skipComment(s, i);
            --i;
        } else if (ch == u'\\') {
            if (QStringView(s).mid(i).startsWith(begin)) {
                ++depth;
                i += int(begin.size()) - 1;
            } else if (QStringView(s).mid(i).startsWith(end)) {
                if (--depth == 0)
                    return i;
                i += int(end.size()) - 1;
            } else {
                ++i;
            }
        }
    }
    return -1;
}

// Splits s at top-level separators: '&' between table cells, or "\\"
// between rows (separator '\\'), outside braces and nested environments.
QStringList splitTopLevel(const QString &s, QChar separator)
{
    QStringList parts;
    int braces = 0;
    int environments = 0;
    int start = 0;
    for (int i = 0; i < s.size(); ++i) {
        const QChar ch = s.at(i);
        if (ch == u'%') {
            skipComment(s, i);
            --i;
        } else if (ch == u'{') {
            ++braces;
        } else if (ch == u'}') {
            --braces;
        } else if (ch == u'&' && separator == u'&' && braces == 0 && environments == 0) {
            parts << s.mid(start, i - start);
            start = i + 1;
        } else if (ch == u'\\') {
            if (i + 1 >= s.size())
                break;
            const QChar next = s.at(i + 1);
            if (separator == u'\\' && braces == 0 && environments == 0
                && (next == u'\\' || QStringView(s).mid(i + 1).startsWith(u"tabularnewline"))) {
                parts << s.mid(start, i - start);
                int j = i + (next == u'\\' ? 2 : 15);
                if (j < s.size() && s.at(j) == u'*')
                    ++j;
                readOptional(s, j);
                start = j;
                i = j - 1;
                continue;
            }
            if (isLetter(next)) {
                int j = i;
                const QString name = readCommandName(s, j);
                if (name == QLatin1String("begin"))
                    ++environments;
                else if (name == QLatin1String("end"))
                    --environments;
                i = j - 1;
            } else {
                ++i;
            }
        }
    }
    parts << s.mid(start);
    return parts;
}

// Splits a list body at top-level \item commands. The text before the
// first \item is dropped; each item's [label] is returned separately.
QList<QPair<QString, QString>> splitItems(const QString &s, bool *anyLabel)
{
    QList<QPair<QString, QString>> items;
    int braces = 0;
    int environments = 0;
    int contentStart = -1;
    QString label;
    for (int i = 0; i < s.size(); ++i) {
        const QChar ch = s.at(i);
        if (ch == u'%') {
            skipComment(s, i);
            --i;
        } else if (ch == u'{') {
            ++braces;
        } else if (ch == u'}') {
            --braces;
        } else if (ch == u'\\' && i + 1 < s.size()) {
            if (!isLetter(s.at(i + 1))) {
                ++i;
                continue;
            }
            const int at = i;
            int j = i;
            const QString name = readCommandName(s, j);
            if (name == QLatin1String("begin")) {
                ++environments;
            } else if (name == QLatin1String("end")) {
                --environments;
            } else if (name == QLatin1String("item") && braces == 0 && environments == 0) {
                if (contentStart >= 0)
                    items.append({label, s.mid(contentStart, at - contentStart)});
                label.clear();
                if (readOptional(s, j, &label) && anyLabel)
                    *anyLabel = true;
                contentStart = j;
            }
            i = j - 1;
        }
    }
    if (contentStart >= 0)
        items.append({label, s.mid(contentStart)});
    return items;
}

QString romanNumeral(int n)
{
    static const QList<QPair<int, const char *>> parts{
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
        {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}};
    QString result;
    for (const auto &[value, text] : parts) {
        while (n >= value && result.size() < 40) {
            result += QLatin1String(text);
            n -= value;
        }
    }
    return result;
}

// A length such as "0.5\textwidth", "3cm" or "120pt" in screen pixels, for
// image sizes; 0 if it can't be worked out.
int lengthToPixels(QString length)
{
    static const QRegularExpression pattern(
        QStringLiteral("^\\s*([0-9]*\\.?[0-9]+)?\\s*(\\\\[a-zA-Z]+|[a-z]{2})\\s*$"));
    const QRegularExpressionMatch match = pattern.match(length);
    if (!match.hasMatch())
        return 0;
    const double value = match.captured(1).isEmpty() ? 1.0 : match.captured(1).toDouble();
    const QString unit = match.captured(2);
    double pixels = 0;
    if (unit.startsWith(u'\\'))
        pixels = value * 560; // \textwidth, \linewidth, \columnwidth...
    else if (unit == QLatin1String("cm"))
        pixels = value * 37.8;
    else if (unit == QLatin1String("mm"))
        pixels = value * 3.78;
    else if (unit == QLatin1String("in"))
        pixels = value * 96;
    else if (unit == QLatin1String("pt") || unit == QLatin1String("bp"))
        pixels = value * 1.33;
    else if (unit == QLatin1String("em"))
        pixels = value * 16;
    return int(qBound(0.0, pixels, 4000.0));
}

// --- The converter ----------------------------------------------------------------

// Font changes such as \bfseries open HTML tags that stay open until the
// end of the group. Paragraph breaks and block elements close and reopen
// them, so the HTML stays properly nested.
struct Styles
{
    QStringList open;
    QStringList close;
    bool centered = false;

    QString closeAll() const
    {
        QString html;
        for (auto it = close.crbegin(); it != close.crend(); ++it)
            html += *it;
        return html;
    }
    QString paragraphBreak() const { return closeAll() + paragraph() + open.join(QString()); }
    QString block(const QString &html) const
    {
        return closeAll() + QStringLiteral("</p>") + html + startParagraph() + open.join(QString());
    }

private:
    QString startParagraph() const
    {
        return centered ? QStringLiteral("<p align=\"center\">") : QStringLiteral("<p>");
    }
    QString paragraph() const { return QStringLiteral("</p>") + startParagraph(); }
};

struct Macro
{
    int arguments = 0;
    bool hasDefault = false;
    QString defaultArgument;
    QString body;
    QString endBody; // environments only
    bool boxed = false; // tcolorbox environments
};

QString box(const QString &content)
{
    return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\"><tr><td class=\"box\">")
           + content + QStringLiteral("</td></tr></table>");
}

struct TheoremKind
{
    QString title;
    QString counter; // shared counter name, or empty if unnumbered
};

class Converter
{
public:
    QString run(const QString &source);

private:
    QString text(const QString &s);
    QString math(const QString &s);

    void command(const QString &s, int &i, QString &out, Styles &styles);
    void mathCommand(const QString &s, int &i, QString &out);
    QString environment(const QString &name, const QString &s, int &i);

    QString heading(int level, bool numbered, const QString &title);
    QString list(const QString &name, const QString &body);
    QString tabular(const QString &spec, const QString &body);
    QString displayMath(const QString &name, const QString &body);
    QString mathRows(const QString &body, bool aligned, bool numbered);
    QString matrix(const QString &name, const QString &body);
    QString inlineMath(const QString &body);
    QString image(const QString &options, const QString &path);
    QString link(const QString &url, const QString &label);
    QString expand(const Macro &macro, const QString &s, int &i);
    void define(const QString &s, int &i, const QString &kind);
    QString colorFor(const QString &spec);
    QString titleBlock();
    QString footnoteMark(const QString &content);
    QString reference(const QString &kind, const QString &key);
    QString nextEquationNumber();
    void setLabel(const QString &kind, const QString &key, const QString &value)
    {
        m_labels.insert(kind + u':' + esc(key.trimmed()), value);
    }
    bool budgetExceeded() { return ++m_expansions > MaxExpansions; }

    bool m_hasChapters = false;
    int m_depth = 0;
    int m_expansions = 0;
    bool m_display = false;     // inside display maths
    bool m_upright = false;     // inside \mathrm and friends
    bool m_noNumber = false;    // \nonumber or \notag in the current row
    QString m_tag;              // \tag{...} in the current row
    int m_listDepth = 0;
    QString m_floatKind;        // "Figure" or "Table" inside a float

    QString m_title, m_author, m_date;
    bool m_hasDate = false;
    int m_sectionCounters[6] = {};
    int m_equation = 0, m_figure = 0, m_table = 0;
    QHash<QString, int> m_counters;
    QString m_currentLabel;
    QHash<QString, QString> m_labels; // "r:key" or "c:key" -> text
    QStringList m_footnotes;
    QList<std::tuple<int, QString, QString, QString>> m_toc; // level, number, title, anchor
    QHash<QString, Macro> m_macros;
    QHash<QString, Macro> m_environments;
    QHash<QString, TheoremKind> m_theorems;
    QHash<QString, QString> m_colors;
    int m_bibItem = 0;
};

QString Converter::run(const QString &source)
{
    m_hasChapters = source.contains(QLatin1String("\\chapter"));

    QString body = source;
    const int begin = source.indexOf(QLatin1String("\\begin{document}"));
    if (begin >= 0) {
        // The preamble only defines things: the title, macros, theorems.
        text(source.left(begin));
        const int start = begin + 16;
        const int end = source.indexOf(QLatin1String("\\end{document}"), start);
        body = end < 0 ? source.mid(start) : source.mid(start, end - start);
    }

    QString html = QStringLiteral("<p>") + text(body) + QStringLiteral("</p>");

    if (!m_footnotes.isEmpty()) {
        html += QStringLiteral("<hr><div class=\"footnotes\">");
        for (int n = 0; n < m_footnotes.size(); ++n) {
            html += QStringLiteral("<p><a name=\"fn%1\"></a><sup>%1</sup> %2</p>")
                        .arg(n + 1)
                        .arg(m_footnotes.at(n));
        }
        html += QStringLiteral("</div>");
    }

    // The table of contents and references, now that everything is known.
    QString toc;
    if (html.contains(TocMark)) {
        toc = QStringLiteral("<h2>Contents</h2><div class=\"toc\">");
        for (const auto &[level, number, title, anchor] : std::as_const(m_toc)) {
            toc += QStringLiteral("<p style=\"margin-left:%1px; margin-top:0; margin-bottom:2px\">"
                                  "<a href=\"#%2\">%3%4</a></p>")
                       .arg(qMax(0, level - (m_hasChapters ? 0 : 1)) * 20)
                       .arg(anchor, number.isEmpty() ? QString() : number + QStringLiteral("&nbsp;&nbsp;"),
                            title);
        }
        toc += QStringLiteral("</div>");
    }

    QString result;
    result.reserve(html.size());
    for (int i = 0; i < html.size(); ++i) {
        const QChar ch = html.at(i);
        if (ch == TocMark) {
            result += QStringLiteral("</p>") + toc + QStringLiteral("<p>");
        } else if (ch == RefOpen) {
            const int close = html.indexOf(RefClose, i);
            if (close < 0)
                break;
            const QString key = html.mid(i + 1, close - i - 1);
            const auto found = m_labels.constFind(key);
            if (found != m_labels.cend())
                result += *found;
            else
                result += key.startsWith(QLatin1String("c:")) ? key.mid(2) : QStringLiteral("??");
            i = close;
        } else {
            result += ch;
        }
    }
    // Paragraph markers leave empty paragraphs around headings and lists.
    static const QRegularExpression empty(QStringLiteral("<p>\\s*</p>"));
    result.replace(empty, QString());
    return result;
}

// Converts text-mode LaTeX to HTML. Font changes such as \bfseries last
// until the end of the group, which is the end of this call.
QString Converter::text(const QString &s)
{
    if (m_depth >= MaxDepth)
        return esc(s);
    ++m_depth;
    QString out;
    Styles styles;
    const int n = int(s.size());
    int i = 0;
    while (i < n && out.size() < MaxOutput) {
        const QChar ch = s.at(i);
        switch (ch.unicode()) {
        case '\\':
            command(s, i, out, styles);
            break;
        case '{':
            out += text(readBraced(s, i));
            break;
        case '}':
            ++i;
            break;
        case '%':
            skipComment(s, i);
            break;
        case '$': {
            const bool display = i + 1 < n && s.at(i + 1) == u'$';
            const int start = i + (display ? 2 : 1);
            int end = -1;
            for (int j = start; j < n; ++j) {
                if (s.at(j) == u'\\') {
                    ++j;
                } else if (s.at(j) == u'$') {
                    end = j;
                    break;
                }
            }
            if (end < 0) {
                out += u'$';
                ++i;
                break;
            }
            const QString body = s.mid(start, end - start);
            out += display ? displayMath(QStringLiteral("displaymath"), body) : inlineMath(body);
            i = end + (display ? 2 : 1);
            break;
        }
        case '~':
            out += QStringLiteral("&nbsp;");
            ++i;
            break;
        case '\n': {
            int j = i + 1;
            bool paragraph = false;
            while (j < n && (isBlank(s.at(j)) || s.at(j) == u'\n')) {
                if (s.at(j) == u'\n')
                    paragraph = true;
                ++j;
            }
            out += paragraph ? styles.paragraphBreak() : QStringLiteral(" ");
            i = j;
            break;
        }
        case '-':
            if (QStringView(s).mid(i, 3) == u"---") {
                out += QStringLiteral("—");
                i += 3;
            } else if (QStringView(s).mid(i, 2) == u"--") {
                out += QStringLiteral("–");
                i += 2;
            } else {
                out += u'-';
                ++i;
            }
            break;
        case '`':
            if (i + 1 < n && s.at(i + 1) == u'`') {
                out += QStringLiteral("“");
                i += 2;
            } else {
                out += QStringLiteral("‘");
                ++i;
            }
            break;
        case '\'':
            if (i + 1 < n && s.at(i + 1) == u'\'') {
                out += QStringLiteral("”");
                i += 2;
            } else {
                out += QStringLiteral("’");
                ++i;
            }
            break;
        case '&':
            // Alignment outside a table: TeX would stop with an error.
            out += u' ';
            ++i;
            break;
        default:
            out += escChar(ch);
            ++i;
        }
    }
    out += styles.closeAll();
    if (styles.centered) {
        out.replace(QStringLiteral("<p>"), QStringLiteral("<p align=\"center\">"));
        out = QStringLiteral("</p><p align=\"center\">") + out + QStringLiteral("</p><p>");
    }
    --m_depth;
    return out;
}

void Converter::command(const QString &s, int &i, QString &out, Styles &styles)
{
    const QString name = readCommandName(s, i);
    const bool word = !name.isEmpty() && isLetter(name.at(0));
    bool starred = false;
    if (word && i < s.size() && s.at(i) == u'*') {
        starred = true;
        ++i;
    }
    if (word)
        skipSpace(s, i);

    // Single characters.
    if (!word) {
        if (name == QLatin1String("\\")) {
            if (i < s.size() && s.at(i) == u'*')
                ++i;
            readOptional(s, i);
            out += QStringLiteral("<br>");
        } else if (name == QLatin1String("(")) {
            const int end = s.indexOf(QLatin1String("\\)"), i);
            out += inlineMath(s.mid(i, end < 0 ? -1 : end - i));
            i = end < 0 ? int(s.size()) : end + 2;
        } else if (name == QLatin1String("[")) {
            const int end = s.indexOf(QLatin1String("\\]"), i);
            out += displayMath(QStringLiteral("displaymath"), s.mid(i, end < 0 ? -1 : end - i));
            i = end < 0 ? int(s.size()) : end + 2;
        } else if (const QChar mark = accentMark(name); !mark.isNull()) {
            const QString base = text(readArg(s, i));
            out += (base.isEmpty() ? QStringLiteral(" ") : base) + mark;
        } else if (name == QLatin1String(",") || name == QLatin1String(">")) {
            out += QStringLiteral(" ");
        } else if (name == QLatin1String(";") || name == QLatin1String(":")) {
            out += QStringLiteral(" ");
        } else if (name == QLatin1String(" ") || name == QLatin1String("\n")
                   || name == QLatin1String("\t")) {
            out += u' ';
        } else if (name == QLatin1String("-") || name == QLatin1String("/")
                   || name == QLatin1String("@") || name == QLatin1String("!")) {
            // Hyphenation and spacing hints.
        } else if (name == QLatin1String("|")) {
            out += QStringLiteral("‖");
        } else {
            out += esc(name); // \% \& \$ \# \_ \{ \}
        }
        return;
    }

    // Macros defined in the document.
    if (const auto macro = m_macros.constFind(name); macro != m_macros.cend()) {
        if (!budgetExceeded())
            out += text(expand(*macro, s, i));
        return;
    }

    static const QHash<QString, QPair<QString, QString>> styleCommands{
        {"textbf", {"<b>", "</b>"}}, {"textit", {"<i>", "</i>"}}, {"textsl", {"<i>", "</i>"}},
        {"emph", {"<i>", "</i>"}}, {"texttt", {"<tt>", "</tt>"}}, {"underline", {"<u>", "</u>"}},
        {"uline", {"<u>", "</u>"}}, {"sout", {"<s>", "</s>"}}, {"st", {"<s>", "</s>"}},
        {"textsuperscript", {"<sup>", "</sup>"}}, {"textsubscript", {"<sub>", "</sub>"}},
        {"textsc", {"<span style=\"font-variant:small-caps\">", "</span>"}},
        {"textsf", {"<span style=\"font-family:sans-serif\">", "</span>"}},
        {"textrm", {"", ""}}, {"textup", {"", ""}}, {"textmd", {"", ""}}, {"textnormal", {"", ""}},
        {"mbox", {"", ""}}, {"hbox", {"", ""}}, {"text", {"", ""}}, {"makebox", {"", ""}},
        {"fbox", {"<span style=\"border:1px solid\">", "</span>"}},
        {"framebox", {"", ""}}, {"centerline", {"", ""}}, {"hyperref", {"", ""}},
        {"enquote", {"“", "”"}}, {"foreignlanguage", {"", ""}},
        {"selectlanguage", {"", ""}}, {"titlefont", {"", ""}}};
    static const QHash<QString, QPair<QString, QString>> declarations{
        {"bfseries", {"<b>", "</b>"}}, {"bf", {"<b>", "</b>"}}, {"itshape", {"<i>", "</i>"}},
        {"it", {"<i>", "</i>"}}, {"em", {"<i>", "</i>"}}, {"slshape", {"<i>", "</i>"}},
        {"sl", {"<i>", "</i>"}}, {"ttfamily", {"<tt>", "</tt>"}}, {"tt", {"<tt>", "</tt>"}},
        {"scshape", {"<span style=\"font-variant:small-caps\">", "</span>"}},
        {"sc", {"<span style=\"font-variant:small-caps\">", "</span>"}},
        {"sffamily", {"<span style=\"font-family:sans-serif\">", "</span>"}},
        {"sf", {"<span style=\"font-family:sans-serif\">", "</span>"}},
        {"tiny", {"<span style=\"font-size:xx-small\">", "</span>"}},
        {"scriptsize", {"<span style=\"font-size:x-small\">", "</span>"}},
        {"footnotesize", {"<span style=\"font-size:small\">", "</span>"}},
        {"small", {"<span style=\"font-size:small\">", "</span>"}},
        {"large", {"<span style=\"font-size:large\">", "</span>"}},
        {"Large", {"<span style=\"font-size:x-large\">", "</span>"}},
        {"LARGE", {"<span style=\"font-size:xx-large\">", "</span>"}},
        {"huge", {"<span style=\"font-size:xx-large\">", "</span>"}},
        {"Huge", {"<span style=\"font-size:xx-large\">", "</span>"}}};
    // Commands whose arguments are dropped: [optional] then {required}.
    static const QHash<QString, int> ignored{
        {"documentclass", 1}, {"usepackage", 1}, {"RequirePackage", 1}, {"setlength", 2},
        {"addtolength", 2}, {"setcounter", 2}, {"addtocounter", 2}, {"stepcounter", 1},
        {"refstepcounter", 1}, {"pagestyle", 1}, {"thispagestyle", 1}, {"pagenumbering", 1},
        {"geometry", 1}, {"hypersetup", 1}, {"graphicspath", 1}, {"bibliographystyle", 1},
        {"vspace", 1}, {"addvspace", 1}, {"linespread", 1}, {"lstset", 1}, {"usetikzlibrary", 1},
        {"setminted", 1}, {"captionsetup", 1}, {"addbibresource", 1}, {"nocite", 1},
        {"input", 1}, {"include", 1}, {"includeonly", 1}, {"index", 1}, {"glossary", 1},
        {"markboth", 2}, {"markright", 1}, {"fancyhead", 1}, {"fancyfoot", 1}, {"lhead", 1},
        {"chead", 1}, {"rhead", 1}, {"lfoot", 1}, {"cfoot", 1}, {"rfoot", 1},
        {"newcounter", 1}, {"numberwithin", 2}, {"setstretch", 1}, {"titleformat", 5},
        {"titlespacing", 4}, {"setlist", 1}, {"renewenvironment", 3}, {"theoremstyle", 1},
        {"DeclareGraphicsExtensions", 1}, {"urlstyle", 1}, {"newlength", 1},
        {"settowidth", 2}, {"allowdisplaybreaks", 0}, {"phantom", 1}, {"hphantom", 1},
        {"vphantom", 1}, {"rule", 2}, {"raisebox", 1}, {"newpage", 0}, {"clearpage", 0},
        {"cleardoublepage", 0}, {"pagebreak", 0}, {"nopagebreak", 0}, {"noindent", 0},
        {"indent", 0}, {"centering", 0}, {"raggedright", 0}, {"raggedleft", 0},
        {"normalfont", 0}, {"rmfamily", 0}, {"upshape", 0}, {"mdseries", 0}, {"normalsize", 0},
        {"hline", 0}, {"toprule", 0}, {"midrule", 0}, {"bottomrule", 0}, {"cline", 1},
        {"cmidrule", 1}, {"protect", 0}, {"relax", 0}, {"makeatletter", 0}, {"makeatother", 0},
        {"frontmatter", 0}, {"mainmatter", 0}, {"backmatter", 0}, {"appendix", 0},
        {"onecolumn", 0}, {"twocolumn", 0}, {"sloppy", 0}, {"fussy", 0}, {"hfill", 0},
        {"vfill", 0}, {"null", 0}, {"strut", 0}, {"listoffigures", 0}, {"listoftables", 0},
        {"printindex", 0}, {"makeindex", 0}, {"maketitlepage", 0}, {"fontsize", 2}, {"selectfont", 0},
        {"hypertarget", 1}, {"newgeometry", 1}, {"restoregeometry", 0}, {"FloatBarrier", 0},
        {"balance", 0}, {"tcbset", 1}, {"newfloat", 3}};

    if (const auto style = styleCommands.constFind(name); style != styleCommands.cend()) {
        if (name == QLatin1String("makebox") || name == QLatin1String("framebox")) {
            readOptional(s, i);
            readOptional(s, i);
        }
        if (name == QLatin1String("hyperref") || name == QLatin1String("foreignlanguage"))
            readOptional(s, i);
        if (name == QLatin1String("foreignlanguage"))
            readArg(s, i);
        if (name == QLatin1String("selectlanguage")) {
            readArg(s, i);
            return;
        }
        out += style->first + text(readArg(s, i)) + style->second;
        return;
    }
    if (const auto declaration = declarations.constFind(name); declaration != declarations.cend()) {
        out += declaration->first;
        styles.open << declaration->first;
        styles.close << declaration->second;
        return;
    }
    if (const auto symbol = textSymbols().constFind(name); symbol != textSymbols().cend()) {
        out += esc(*symbol);
        return;
    }
    if (const QChar mark = accentMark(name); !mark.isNull() && name.size() == 1) {
        const QString base = text(readArg(s, i));
        out += (base.isEmpty() ? QStringLiteral(" ") : base) + mark;
        return;
    }

    // Sections.
    static const QHash<QString, int> levels{{"part", -1}, {"chapter", 0}, {"section", 1},
                                            {"subsection", 2}, {"subsubsection", 3},
                                            {"paragraph", 4}, {"subparagraph", 5}};
    if (const auto level = levels.constFind(name); level != levels.cend()) {
        readOptional(s, i);
        const QString html = heading(*level, !starred, text(readArg(s, i)));
        out += *level >= 4 ? styles.closeAll() + html + styles.open.join(QString()) : styles.block(html);
        return;
    }

    if (name == QLatin1String("begin")) {
        const QString env = readArg(s, i).trimmed();
        out += styles.block(environment(env, s, i));
        return;
    }
    if (name == QLatin1String("end")) {
        readArg(s, i); // unmatched: ignore
        return;
    }
    if (name == QLatin1String("item")) {
        // An \item outside a list.
        QString label;
        readOptional(s, i, &label);
        out += QStringLiteral("<br>• ") + text(label) + u' ';
        return;
    }

    if (name == QLatin1String("title") || name == QLatin1String("author")
        || name == QLatin1String("date")) {
        readOptional(s, i);
        const QString value = readArg(s, i);
        if (name == QLatin1String("title"))
            m_title = value;
        else if (name == QLatin1String("author"))
            m_author = value;
        else {
            m_date = value;
            m_hasDate = true;
        }
        return;
    }
    if (name == QLatin1String("maketitle")) {
        out += styles.block(titleBlock());
        return;
    }
    if (name == QLatin1String("and")) {
        out += QStringLiteral("<br>");
        return;
    }
    if (name == QLatin1String("today")) {
        out += QDate::currentDate().toString(QStringLiteral("MMMM d, yyyy"));
        return;
    }
    if (name == QLatin1String("tableofcontents")) {
        out += styles.block(TocMark);
        return;
    }
    if (name == QLatin1String("par")) {
        out += styles.paragraphBreak();
        return;
    }
    if (name == QLatin1String("centering")) {
        styles.centered = true;
        return;
    }
    if (name == QLatin1String("newline") || name == QLatin1String("linebreak")
        || name == QLatin1String("break")) {
        readOptional(s, i);
        out += QStringLiteral("<br>");
        return;
    }
    if (name == QLatin1String("smallskip") || name == QLatin1String("medskip")
        || name == QLatin1String("bigskip")) {
        out += QStringLiteral("<br>");
        return;
    }
    if (name == QLatin1String("hspace")) {
        readArg(s, i);
        out += QStringLiteral(" ");
        return;
    }

    if (name == QLatin1String("footnote")) {
        readOptional(s, i);
        out += footnoteMark(text(readArg(s, i)));
        return;
    }
    if (name == QLatin1String("thanks")) {
        out += footnoteMark(text(readArg(s, i)));
        return;
    }
    if (name == QLatin1String("label")) {
        const QString key = readArg(s, i).trimmed();
        setLabel(QStringLiteral("r"), key, m_currentLabel);
        out += QStringLiteral("<a name=\"%1\"></a>").arg(esc(key));
        return;
    }
    if (name == QLatin1String("ref") || name == QLatin1String("autoref")
        || name == QLatin1String("cref") || name == QLatin1String("Cref")
        || name == QLatin1String("pageref") || name == QLatin1String("nameref")) {
        out += reference(QStringLiteral("r"), readArg(s, i).trimmed());
        return;
    }
    if (name == QLatin1String("eqref")) {
        out += u'(' + reference(QStringLiteral("r"), readArg(s, i).trimmed()) + u')';
        return;
    }
    if (name == QLatin1String("cite") || name == QLatin1String("citep")
        || name == QLatin1String("citet") || name == QLatin1String("parencite")
        || name == QLatin1String("textcite") || name == QLatin1String("autocite")) {
        QString note;
        QString pre;
        if (readOptional(s, i, &note) && readOptional(s, i, &pre))
            std::swap(note, pre);
        QStringList refs;
        for (const QString &key : readArg(s, i).split(u',', Qt::SkipEmptyParts))
            refs << reference(QStringLiteral("c"), key.trimmed());
        if (!pre.isEmpty())
            refs.prepend(text(pre));
        if (!note.isEmpty())
            refs << text(note);
        out += u'[' + refs.join(QStringLiteral(", ")) + u']';
        return;
    }
    if (name == QLatin1String("caption") || name == QLatin1String("captionof")) {
        QString kind = m_floatKind;
        if (name == QLatin1String("captionof")) {
            const QString type = readArg(s, i).trimmed();
            kind = type == QLatin1String("table") ? QStringLiteral("Table") : QStringLiteral("Figure");
        }
        readOptional(s, i);
        const QString caption = readArg(s, i);
        QString prefix;
        if (!starred && !kind.isEmpty()) {
            const int number = kind == QLatin1String("Table") ? ++m_table : ++m_figure;
            m_currentLabel = QString::number(number);
            prefix = QStringLiteral("<b>%1 %2:</b> ").arg(kind).arg(number);
        }
        out += styles.block(QStringLiteral("<p class=\"caption\" align=\"center\">") + prefix
                            + text(caption) + QStringLiteral("</p>"));
        return;
    }
    if (name == QLatin1String("includegraphics")) {
        QString options;
        readOptional(s, i, &options);
        out += image(options, readArg(s, i));
        return;
    }
    if (name == QLatin1String("href")) {
        const QString url = readArg(s, i);
        out += link(url, text(readArg(s, i)));
        return;
    }
    if (name == QLatin1String("url") || name == QLatin1String("nolinkurl")) {
        // URLs are taken literally, apart from escaped characters.
        QString url = readArg(s, i);
        static const QRegularExpression escaped(QStringLiteral("\\\\([#%&_~$])"));
        url.replace(escaped, QStringLiteral("\\1"));
        out += name == QLatin1String("url") ? link(url, QStringLiteral("<tt>") + esc(url) + QStringLiteral("</tt>"))
                                             : QStringLiteral("<tt>") + esc(url) + QStringLiteral("</tt>");
        return;
    }
    if (name == QLatin1String("verb") || name == QLatin1String("lstinline")
        || name == QLatin1String("mintinline")) {
        if (name == QLatin1String("lstinline"))
            readOptional(s, i);
        if (name == QLatin1String("mintinline"))
            readArg(s, i);
        if (i >= s.size())
            return;
        const QChar delimiter = s.at(i);
        const QChar closing = delimiter == u'{' ? u'}' : delimiter;
        const int end = s.indexOf(closing, i + 1);
        const QString code = s.mid(i + 1, end < 0 ? -1 : end - i - 1);
        out += QStringLiteral("<tt>") + esc(code) + QStringLiteral("</tt>");
        i = end < 0 ? int(s.size()) : end + 1;
        return;
    }
    if (name == QLatin1String("tcbinline")) {
        out += QStringLiteral("<span class=\"key\">&nbsp;") + text(readArg(s, i)) + QStringLiteral("&nbsp;</span>");
        return;
    }
    if (name == QLatin1String("textcolor")) {
        QString model;
        readOptional(s, i, &model);
        const QString color = colorFor(model.isEmpty() ? readArg(s, i)
                                                       : model + u'|' + readArg(s, i));
        const QString content = text(readArg(s, i));
        out += color.isEmpty() ? content
                               : QStringLiteral("<span style=\"color:%1\">").arg(color) + content
                                     + QStringLiteral("</span>");
        return;
    }
    if (name == QLatin1String("color")) {
        QString model;
        readOptional(s, i, &model);
        const QString color = colorFor(model.isEmpty() ? readArg(s, i)
                                                       : model + u'|' + readArg(s, i));
        if (!color.isEmpty()) {
            const QString open = QStringLiteral("<span style=\"color:%1\">").arg(color);
            out += open;
            styles.open << open;
            styles.close << QStringLiteral("</span>");
        }
        return;
    }
    if (name == QLatin1String("colorbox") || name == QLatin1String("fcolorbox")) {
        readOptional(s, i);
        readArg(s, i);
        if (name == QLatin1String("fcolorbox"))
            readArg(s, i);
        out += text(readArg(s, i));
        return;
    }
    if (name == QLatin1String("definecolor")) {
        const QString colorName = readArg(s, i).trimmed();
        const QString model = readArg(s, i).trimmed();
        const QString value = colorFor(model + u'|' + readArg(s, i));
        if (!value.isEmpty())
            m_colors.insert(colorName, value);
        return;
    }
    if (name == QLatin1String("newcommand") || name == QLatin1String("renewcommand")
        || name == QLatin1String("providecommand") || name == QLatin1String("def")
        || name == QLatin1String("newenvironment") || name == QLatin1String("DeclareMathOperator")
        || name == QLatin1String("newtheorem") || name == QLatin1String("newtcolorbox")
        || name == QLatin1String("newtcbox")) {
        define(s, i, starred ? name + u'*' : name);
        return;
    }
    if (name == QLatin1String("bibliography") || name == QLatin1String("printbibliography")) {
        if (name == QLatin1String("bibliography"))
            readArg(s, i);
        else
            readOptional(s, i);
        out += styles.block(QStringLiteral("<p class=\"note\">[Bibliography: shown when typeset]</p>"));
        return;
    }
    if (name == QLatin1String("bibitem")) {
        QString label;
        readOptional(s, i, &label);
        const QString key = readArg(s, i).trimmed();
        ++m_bibItem;
        const QString shown = label.isEmpty() ? QString::number(m_bibItem) : text(label);
        setLabel(QStringLiteral("c"), key, shown);
        out += QStringLiteral("<br>[%1] ").arg(shown);
        return;
    }

    if (const auto it = ignored.constFind(name); it != ignored.cend()) {
        readOptional(s, i);
        for (int k = 0; k < *it; ++k) {
            readArg(s, i);
            readOptional(s, i);
        }
        return;
    }
    // Maths symbols used outside maths, such as \alpha or \times.
    if (const auto symbol = mathSymbols().constFind(name); symbol != mathSymbols().cend()) {
        out += esc(*symbol);
        return;
    }
    // Anything else: drop the command and keep its arguments' text, which
    // shows the words of most unknown commands.
}

QString Converter::expand(const Macro &macro, const QString &s, int &i)
{
    QStringList arguments;
    int first = 0;
    if (macro.hasDefault) {
        QString optional = macro.defaultArgument;
        readOptional(s, i, &optional);
        arguments << optional;
        first = 1;
    }
    for (int k = first; k < macro.arguments; ++k)
        arguments << readArg(s, i);

    QString result;
    const QString &body = macro.body;
    for (int k = 0; k < body.size(); ++k) {
        const QChar ch = body.at(k);
        if (ch == u'#' && k + 1 < body.size() && body.at(k + 1).isDigit()) {
            const int index = body.at(k + 1).digitValue() - 1;
            if (index >= 0 && index < arguments.size())
                result += arguments.at(index);
            ++k;
        } else {
            result += ch;
        }
    }
    return result;
}

// \newcommand, \def, \newenvironment, \DeclareMathOperator, \newtheorem.
void Converter::define(const QString &s, int &i, const QString &kind)
{
    if (kind.startsWith(QLatin1String("newtheorem"))) {
        const QString name = readArg(s, i).trimmed();
        QString shared;
        readOptional(s, i, &shared);
        const QString title = readArg(s, i);
        readOptional(s, i);
        const bool numbered = !kind.endsWith(u'*');
        m_theorems.insert(name, {title, numbered ? (shared.isEmpty() ? name : shared.trimmed()) : QString()});
        return;
    }
    if (kind.startsWith(QLatin1String("DeclareMathOperator"))) {
        QString name = readArg(s, i).trimmed();
        if (name.startsWith(u'\\'))
            name.remove(0, 1);
        Macro macro;
        macro.body = QStringLiteral("\\operatorname{") + readArg(s, i) + u'}';
        if (!name.isEmpty())
            m_macros.insert(name, macro);
        return;
    }
    // tcolorbox: \newtcolorbox{name}[n][default]{options} makes a boxed
    // environment, \newtcbox{\name}{options} an inline box command.
    if (kind == QLatin1String("newtcolorbox") || kind == QLatin1String("newtcbox")) {
        QString name = readArg(s, i).trimmed();
        if (name.startsWith(u'\\'))
            name.remove(0, 1);
        Macro macro;
        QString count;
        if (readOptional(s, i, &count))
            macro.arguments = qBound(0, count.trimmed().toInt(), 9);
        macro.hasDefault = readOptional(s, i, &macro.defaultArgument);
        const QString options = readArg(s, i);
        if (name.isEmpty())
            return;
        if (kind == QLatin1String("newtcbox")) {
            macro.body = QStringLiteral("\\tcbinline{#%1}").arg(macro.arguments + 1);
            macro.arguments += 1;
            m_macros.insert(name, macro);
        } else {
            static const QRegularExpression title(QStringLiteral("(?:^|,)\\s*title\\s*=\\s*(\\{[^}]*\\}|[^,]*)"));
            const QRegularExpressionMatch match = title.match(options);
            if (match.hasMatch())
                macro.body = QStringLiteral("\\textbf{") + match.captured(1).trimmed() + QStringLiteral("}\\ \\ ");
            macro.boxed = true;
            m_environments.insert(name, macro);
        }
        return;
    }
    if (kind == QLatin1String("def")) {
        skipSpace(s, i);
        if (i >= s.size() || s.at(i) != u'\\')
            return;
        const QString name = readCommandName(s, i);
        Macro macro;
        // Parameter text such as #1#2 before the body.
        while (i < s.size() && s.at(i) != u'{') {
            if (s.at(i) == u'#')
                ++macro.arguments;
            ++i;
        }
        if (i < s.size())
            macro.body = readBraced(s, i);
        m_macros.insert(name, macro);
        return;
    }

    const bool environment = kind == QLatin1String("newenvironment");
    QString name = readArg(s, i).trimmed();
    if (name.startsWith(u'\\'))
        name.remove(0, 1);
    Macro macro;
    QString count;
    if (readOptional(s, i, &count))
        macro.arguments = qBound(0, count.trimmed().toInt(), 9);
    macro.hasDefault = readOptional(s, i, &macro.defaultArgument);
    macro.body = readArg(s, i);
    if (environment)
        macro.endBody = readArg(s, i);
    if (name.isEmpty())
        return;
    if (environment)
        m_environments.insert(name, macro);
    else if (kind != QLatin1String("providecommand") || !m_macros.contains(name))
        m_macros.insert(name, macro);
}

QString Converter::colorFor(const QString &spec)
{
    const int bar = spec.indexOf(u'|');
    if (bar < 0) {
        const QString name = spec.trimmed();
        if (m_colors.contains(name))
            return m_colors.value(name);
        // xcolor mixes such as red!50 use the first colour.
        return namedColor(name.section(u'!', 0, 0));
    }
    const QString model = spec.left(bar).trimmed();
    const QString value = spec.mid(bar + 1).trimmed();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{6}$"));
    if (model == QLatin1String("HTML") && hex.match(value).hasMatch())
        return u'#' + value.toLower();
    const QStringList parts = value.split(u',');
    if ((model == QLatin1String("rgb") || model == QLatin1String("RGB")) && parts.size() == 3) {
        const double scale = model == QLatin1String("rgb") ? 255 : 1;
        QString result = QStringLiteral("#");
        for (const QString &part : parts) {
            bool ok = false;
            const int component = qBound(0, int(part.trimmed().toDouble(&ok) * scale + 0.5), 255);
            if (!ok)
                return QString();
            result += QStringLiteral("%1").arg(component, 2, 16, QLatin1Char('0'));
        }
        return result;
    }
    if (model == QLatin1String("gray") && parts.size() == 1) {
        bool ok = false;
        const int level = qBound(0, int(value.toDouble(&ok) * 255 + 0.5), 255);
        return ok ? QStringLiteral("#%1%1%1").arg(level, 2, 16, QLatin1Char('0')) : QString();
    }
    return namedColor(value);
}

QString Converter::heading(int level, bool numbered, const QString &title)
{
    QString number;
    if (level >= 0) {
        if (numbered) {
            ++m_sectionCounters[level];
            for (int k = level + 1; k < 6; ++k)
                m_sectionCounters[k] = 0;
            if (level == 0)
                m_equation = m_figure = m_table = 0;
            const int first = m_hasChapters ? 0 : 1;
            if (level <= 3 && level >= first) {
                QStringList parts;
                for (int k = first; k <= level; ++k)
                    parts << QString::number(m_sectionCounters[k]);
                number = parts.join(u'.');
                m_currentLabel = number;
            }
        }
    }
    const QString anchor = QStringLiteral("sec%1").arg(m_toc.size() + 1);
    if (level <= 3)
        m_toc.append({qMax(level, 0), number, title, anchor});

    // Paragraph headings run into the text that follows.
    if (level >= 4)
        return QStringLiteral("</p><p><a name=\"%1\"></a><b>%2</b>&nbsp;&nbsp;").arg(anchor, title);
    if (level == 0 && numbered)
        return QStringLiteral("<h1><a name=\"%1\"></a>Chapter %2<br>%3</h1>").arg(anchor, number, title);
    const int tag = level < 0 ? 1 : qMin(level + 1, 4);
    return QStringLiteral("<h%1><a name=\"%2\"></a>%3%4</h%1>")
        .arg(tag)
        .arg(anchor, number.isEmpty() ? QString() : number + QStringLiteral("&nbsp;&nbsp;"), title);
}

QString Converter::titleBlock()
{
    QString html;
    if (!m_title.isEmpty())
        html += QStringLiteral("<h1 class=\"title\" align=\"center\">") + text(m_title) + QStringLiteral("</h1>");
    if (!m_author.isEmpty())
        html += QStringLiteral("<p class=\"author\" align=\"center\">") + text(m_author) + QStringLiteral("</p>");
    const QString date = m_hasDate ? text(m_date)
                                   : QDate::currentDate().toString(QStringLiteral("MMMM d, yyyy"));
    if (!date.trimmed().isEmpty())
        html += QStringLiteral("<p class=\"date\" align=\"center\">") + date + QStringLiteral("</p>");
    return html;
}

QString Converter::footnoteMark(const QString &content)
{
    m_footnotes << content;
    return QStringLiteral("<sup><a href=\"#fn%1\">%1</a></sup>").arg(m_footnotes.size());
}

QString Converter::reference(const QString &kind, const QString &key)
{
    return RefOpen + kind + u':' + esc(key) + RefClose;
}

QString Converter::link(const QString &url, const QString &label)
{
    const QUrl parsed(url.trimmed());
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid()
        || !(scheme == QLatin1String("http") || scheme == QLatin1String("https")
             || scheme == QLatin1String("mailto"))) {
        return label;
    }
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(esc(parsed.toString(QUrl::FullyEncoded)), label);
}

QString Converter::image(const QString &options, const QString &path)
{
    int width = 0;
    int height = 0;
    double scale = 0;
    for (const QString &option : options.split(u',', Qt::SkipEmptyParts)) {
        const QString key = option.section(u'=', 0, 0).trimmed();
        const QString value = option.section(u'=', 1).trimmed();
        if (key == QLatin1String("width"))
            width = lengthToPixels(value);
        else if (key == QLatin1String("height"))
            height = lengthToPixels(value);
        else if (key == QLatin1String("scale"))
            scale = qBound(0.01, value.toDouble(), 10.0);
    }
    QString attributes;
    if (width > 0)
        attributes += QStringLiteral(" width=\"%1\"").arg(width);
    else if (height > 0)
        attributes += QStringLiteral(" height=\"%1\"").arg(height);
    else if (scale > 0)
        attributes += QStringLiteral(" width=\"%1\"").arg(int(scale * 400)); // a guess
    // The viewer resolves the path against the file's folder, tries the
    // usual extensions, and limits what it will load.
    return QStringLiteral("<img src=\"%1\"%2 alt=\"%1\">").arg(esc(path.trimmed()), attributes);
}

QString Converter::environment(const QString &name, const QString &s, int &i)
{
    // Verbatim text runs to the literal \end{name}, ignoring everything.
    static const QSet<QString> verbatim{"verbatim", "verbatim*", "Verbatim", "lstlisting",
                                        "minted", "alltt", "comment", "spverbatim", "BVerbatim"};
    if (verbatim.contains(name)) {
        if (name == QLatin1String("minted")) {
            readOptional(s, i);
            readArg(s, i);
        } else if (name != QLatin1String("verbatim") && name != QLatin1String("verbatim*")) {
            readOptional(s, i);
        }
        const QString end = QStringLiteral("\\end{") + name + u'}';
        const int close = s.indexOf(end, i);
        QString body = s.mid(i, close < 0 ? -1 : close - i);
        i = close < 0 ? int(s.size()) : close + int(end.size());
        if (body.startsWith(u'\n'))
            body.remove(0, 1);
        if (name == QLatin1String("comment"))
            return QString();
        return QStringLiteral("<pre>") + esc(body) + QStringLiteral("</pre>");
    }

    const int close = findEnd(s, i, name);
    const int bodyStart = i;
    const int bodyEnd = close < 0 ? int(s.size()) : close;
    i = close < 0 ? int(s.size()) : close + int(QStringLiteral("\\end{").size() + name.size() + 1);
    int k = 0;
    QString body = s.mid(bodyStart, bodyEnd - bodyStart);
    auto takeOptional = [&](QString *out = nullptr) {
        k = 0;
        const bool found = readOptional(body, k, out);
        if (found)
            body.remove(0, k);
        return found;
    };
    auto takeArg = [&] {
        k = 0;
        const QString arg = readArg(body, k);
        body.remove(0, k);
        return arg;
    };

    if (m_depth >= MaxDepth)
        return esc(body);
    ++m_depth;
    QString html;

    static const QSet<QString> lists{"itemize", "enumerate", "description", "compactitem",
                                     "compactenum", "compactdesc", "inparaenum", "asparaenum",
                                     "list", "dinglist"};
    static const QSet<QString> maths{"equation", "equation*", "align", "align*", "alignat",
                                     "alignat*", "flalign", "flalign*", "gather", "gather*",
                                     "multline", "multline*", "eqnarray", "eqnarray*",
                                     "displaymath", "dmath", "dmath*"};

    if (name == QLatin1String("document") || name == QLatin1String("titlepage")
        || name == QLatin1String("frame") || name == QLatin1String("adjustwidth")) {
        html = QStringLiteral("<p>") + text(body) + QStringLiteral("</p>");
    } else if (lists.contains(name)) {
        if (name == QLatin1String("list")) {
            takeArg();
            takeArg();
        } else if (name == QLatin1String("dinglist")) {
            takeArg();
        }
        takeOptional();
        html = list(name, body);
    } else if (name == QLatin1String("center") || name == QLatin1String("flushleft")
               || name == QLatin1String("flushright")) {
        const char *align = name == QLatin1String("center") ? "center"
                            : name == QLatin1String("flushright") ? "right" : "left";
        html = QStringLiteral("<div align=\"%1\"><p align=\"%1\">").arg(QLatin1String(align))
               + text(body).replace(QStringLiteral("</p><p>"),
                                    QStringLiteral("</p><p align=\"%1\">").arg(QLatin1String(align)))
               + QStringLiteral("</p></div>");
    } else if (name == QLatin1String("quote") || name == QLatin1String("quotation")
               || name == QLatin1String("verse")) {
        html = QStringLiteral("<blockquote><p>") + text(body) + QStringLiteral("</p></blockquote>");
    } else if (name == QLatin1String("abstract")) {
        html = QStringLiteral("<p align=\"center\"><b>Abstract</b></p><blockquote><p>") + text(body)
               + QStringLiteral("</p></blockquote>");
    } else if (name == QLatin1String("tabular") || name == QLatin1String("tabular*")
               || name == QLatin1String("tabularx") || name == QLatin1String("tabulary")
               || name == QLatin1String("longtable") || name == QLatin1String("supertabular")
               || name == QLatin1String("tblr")) {
        if (name == QLatin1String("tabular*") || name == QLatin1String("tabularx")
            || name == QLatin1String("tabulary"))
            takeArg();
        takeOptional();
        html = tabular(takeArg(), body);
    } else if (name == QLatin1String("figure") || name == QLatin1String("figure*")
               || name == QLatin1String("table") || name == QLatin1String("table*")
               || name == QLatin1String("wrapfigure") || name == QLatin1String("wraptable")
               || name == QLatin1String("subfigure") || name == QLatin1String("minipage")) {
        takeOptional();
        if (name.startsWith(QLatin1String("wrap"))) {
            takeArg();
            takeOptional();
            takeArg();
        } else if (name == QLatin1String("subfigure") || name == QLatin1String("minipage")) {
            takeOptional();
            takeOptional();
            takeArg();
        }
        const QString saved = m_floatKind;
        if (!name.startsWith(QLatin1String("sub")) && name != QLatin1String("minipage"))
            m_floatKind = name.contains(QLatin1String("table")) ? QStringLiteral("Table")
                                                                : QStringLiteral("Figure");
        html = QStringLiteral("<div align=\"center\"><p align=\"center\">") + text(body)
               + QStringLiteral("</p></div>");
        m_floatKind = saved;
    } else if (maths.contains(name)) {
        if (name.startsWith(QLatin1String("alignat")))
            takeArg();
        html = displayMath(name, body);
    } else if (name == QLatin1String("math")) {
        html = QStringLiteral("<p>") + inlineMath(body) + QStringLiteral("</p>");
    } else if (name == QLatin1String("proof")) {
        QString title;
        takeOptional(&title);
        html = QStringLiteral("<p><i>%1.</i> ").arg(title.isEmpty() ? QStringLiteral("Proof") : text(title))
               + text(body) + QStringLiteral(" ∎</p>");
    } else if (m_theorems.contains(name)) {
        const TheoremKind theorem = m_theorems.value(name);
        QString note;
        takeOptional(&note);
        QString heading = text(theorem.title);
        if (!theorem.counter.isEmpty()) {
            const int number = ++m_counters[theorem.counter];
            m_currentLabel = QString::number(number);
            heading += u' ' + m_currentLabel;
        }
        if (!note.isEmpty())
            heading += QStringLiteral(" (") + text(note) + u')';
        html = QStringLiteral("<p><b>%1.</b> <i>").arg(heading) + text(body) + QStringLiteral("</i></p>");
    } else if (name == QLatin1String("tcolorbox") || name == QLatin1String("mdframed")
               || name == QLatin1String("framed") || name == QLatin1String("shaded")) {
        QString options;
        takeOptional(&options);
        static const QRegularExpression title(QStringLiteral("(?:^|,)\\s*title\\s*=\\s*(\\{[^}]*\\}|[^,]*)"));
        const QRegularExpressionMatch match = title.match(options);
        const QString heading = match.hasMatch() ? QStringLiteral("<b>") + text(match.captured(1)) + QStringLiteral("</b><br>")
                                                 : QString();
        html = box(QStringLiteral("<p>") + heading + text(body) + QStringLiteral("</p>"));
    } else if (name == QLatin1String("thebibliography")) {
        takeArg();
        html = QStringLiteral("<h2>References</h2><p>") + text(body) + QStringLiteral("</p>");
    } else if (name == QLatin1String("multicols") || name == QLatin1String("multicols*")) {
        takeArg();
        html = QStringLiteral("<p>") + text(body) + QStringLiteral("</p>");
    } else if (name == QLatin1String("tikzpicture") || name == QLatin1String("pgfpicture")
               || name == QLatin1String("picture") || name == QLatin1String("axis")
               || name == QLatin1String("forest") || name == QLatin1String("circuitikz")) {
        html = QStringLiteral("<p class=\"note\" align=\"center\">[%1: shown when typeset]</p>").arg(esc(name));
    } else if (m_environments.contains(name)) {
        const Macro macro = m_environments.value(name);
        if (!budgetExceeded()) {
            int j = 0;
            const QString begin = expand(macro, body, j);
            html = QStringLiteral("<p>") + text(begin + body.mid(j) + macro.endBody) + QStringLiteral("</p>");
            if (macro.boxed)
                html = box(html);
        }
    } else {
        // Font-size and style environments: \begin{small}...\end{small}.
        // Run the declaration as if it began the body's group.
        QString out;
        Styles styles;
        const QString declaration = u'\\' + name + u' ';
        int j = 0;
        command(declaration, j, out, styles);
        const QString content = text(body);
        html = QStringLiteral("<p>");
        for (const QString &open : std::as_const(styles.open))
            html += open;
        html += content.split(QStringLiteral("</p><p>")).join(styles.paragraphBreak());
        html += styles.closeAll() + QStringLiteral("</p>");
    }
    --m_depth;
    return html;
}

QString Converter::list(const QString &name, const QString &body)
{
    bool anyLabel = false;
    const QList<QPair<QString, QString>> items = splitItems(body, &anyLabel);
    const bool numbered = name.contains(QLatin1String("enum"));
    const bool description = name.contains(QLatin1String("desc"));
    ++m_listDepth;
    QString html;
    if (description) {
        html = QStringLiteral("<dl>");
        for (const auto &[label, content] : items)
            html += QStringLiteral("<dt><b>") + text(label) + QStringLiteral("</b></dt><dd>")
                    + text(content) + QStringLiteral("</dd>");
        html += QStringLiteral("</dl>");
    } else if (anyLabel) {
        // Custom labels: shown in place of the bullets.
        html = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"2\">");
        int number = 0;
        for (const auto &[label, content] : items) {
            ++number;
            const QString shown = !label.isEmpty() ? text(label)
                                  : numbered       ? QString::number(number) + u'.'
                                                   : QStringLiteral("•");
            html += QStringLiteral("<tr><td valign=\"top\" align=\"right\">") + shown
                    + QStringLiteral("&nbsp;</td><td>") + text(content) + QStringLiteral("</td></tr>");
        }
        html += QStringLiteral("</table>");
    } else {
        static const char *const enumerateTypes[] = {"1", "a", "i", "A"};
        static const char *const bulletTypes[] = {"disc", "circle", "square", "disc"};
        const int depth = qBound(0, m_listDepth - 1, 3);
        html = numbered ? QStringLiteral("<ol type=\"%1\">").arg(QLatin1String(enumerateTypes[depth]))
                        : QStringLiteral("<ul type=\"%1\">").arg(QLatin1String(bulletTypes[depth]));
        int number = 0;
        for (const auto &item : items) {
            ++number;
            if (numbered) {
                m_currentLabel = depth == 1 ? QString(QChar(u'a' + (number - 1) % 26))
                                 : depth == 2 ? romanNumeral(number)
                                 : depth == 3 ? QString(QChar(u'A' + (number - 1) % 26))
                                              : QString::number(number);
            }
            html += QStringLiteral("<li>") + text(item.second) + QStringLiteral("</li>");
        }
        html += numbered ? QStringLiteral("</ol>") : QStringLiteral("</ul>");
    }
    --m_listDepth;
    return html;
}

QString Converter::tabular(const QString &spec, const QString &body)
{
    // Column alignments from the spec: l, c, r, p{..}, X, with | rules and
    // @{..}, >{..}, <{..} and *{n}{..} handled or skipped.
    QStringList aligns;
    bool rules = spec.contains(u'|');
    for (int k = 0; k < spec.size() && aligns.size() < 200; ++k) {
        const QChar ch = spec.at(k);
        if (ch == u'l' || ch == u'X' || ch == u'L') {
            aligns << QStringLiteral("left");
        } else if (ch == u'c' || ch == u'C') {
            aligns << QStringLiteral("center");
        } else if (ch == u'r' || ch == u'R') {
            aligns << QStringLiteral("right");
        } else if (ch == u'p' || ch == u'm' || ch == u'b') {
            aligns << QStringLiteral("left");
            ++k;
            readArg(spec, k);
            --k;
        } else if (ch == u'@' || ch == u'>' || ch == u'<' || ch == u'!') {
            ++k;
            readArg(spec, k);
            --k;
        } else if (ch == u'*') {
            ++k;
            const int count = qBound(0, readArg(spec, k).trimmed().toInt(), 50);
            const QString repeated = readArg(spec, k);
            --k;
            for (int c = 0; c < count && aligns.size() < 200; ++c) {
                for (const QChar r : repeated) {
                    if (r == u'l' || r == u'p' || r == u'X')
                        aligns << QStringLiteral("left");
                    else if (r == u'c')
                        aligns << QStringLiteral("center");
                    else if (r == u'r')
                        aligns << QStringLiteral("right");
                }
            }
        }
    }

    QString rows;
    for (QString row : splitTopLevel(body, u'\\')) {
        // Horizontal rules at the start of the row.
        static const QRegularExpression ruleCommands(QStringLiteral(
            "\\\\(hline|toprule|midrule|bottomrule|endhead|endfirsthead|endfoot|endlastfoot)\\b"
            "|\\\\c(mid)?rule(\\([^)]*\\))?\\{[^}]*\\}"));
        if (row.contains(ruleCommands)) {
            rules = true;
            row.replace(ruleCommands, QString());
        }
        if (row.trimmed().isEmpty())
            continue;
        rows += QStringLiteral("<tr>");
        int column = 0;
        for (const QString &cell : splitTopLevel(row, u'&')) {
            // Trimmed, so a line left blank by a removed rule isn't read as
            // a paragraph break.
            QString content = cell.trimmed();
            int span = 1;
            QString align = aligns.value(column, QStringLiteral("left"));
            int k = 0;
            skipSpace(content, k);
            if (QStringView(content).mid(k).startsWith(u"\\multicolumn")) {
                k += 12;
                span = qBound(1, readArg(content, k).trimmed().toInt(), 100);
                const QString cellSpec = readArg(content, k);
                align = cellSpec.contains(u'c') ? QStringLiteral("center")
                        : cellSpec.contains(u'r') ? QStringLiteral("right")
                                                  : QStringLiteral("left");
                content = readArg(content, k);
            }
            rows += QStringLiteral("<td align=\"%1\"%2>").arg(align, span > 1 ? QStringLiteral(" colspan=\"%1\"").arg(span) : QString())
                    + text(content) + QStringLiteral("</td>");
            column += span;
        }
        rows += QStringLiteral("</tr>");
    }
    return QStringLiteral("<table align=\"center\" border=\"%1\" cellspacing=\"0\" cellpadding=\"4\" "
                          "style=\"border-collapse:collapse\">")
               .arg(rules ? 1 : 0)
           + rows + QStringLiteral("</table>");
}

QString Converter::nextEquationNumber()
{
    ++m_equation;
    m_currentLabel = m_hasChapters && m_sectionCounters[0] > 0
        ? QStringLiteral("%1.%2").arg(m_sectionCounters[0]).arg(m_equation)
        : QString::number(m_equation);
    return m_currentLabel;
}

QString Converter::displayMath(const QString &name, const QString &body)
{
    const bool numbered = !name.endsWith(u'*') && name != QLatin1String("displaymath");
    const bool aligned = name.startsWith(QLatin1String("align")) || name.startsWith(QLatin1String("flalign"))
                         || name.startsWith(QLatin1String("eqnarray"));
    const bool multiRow = aligned || name.startsWith(QLatin1String("gather"))
                          || name.startsWith(QLatin1String("multline"));
    if (multiRow)
        return mathRows(body, aligned, numbered);

    m_noNumber = false;
    m_tag.clear();
    const bool saved = m_display;
    m_display = true;
    const QString content = math(body);
    m_display = saved;
    QString number;
    if (!m_tag.isEmpty())
        number = u'(' + m_tag + u')';
    else if (numbered && !m_noNumber)
        number = u'(' + nextEquationNumber() + u')';
    // Labels inside the equation refer to its number.
    if (!number.isEmpty() && m_tag.isEmpty()) {
        static const QRegularExpression label(QStringLiteral("\\\\label\\s*\\{([^}]*)\\}"));
        auto it = label.globalMatch(body);
        while (it.hasNext())
            setLabel(QStringLiteral("r"), it.next().captured(1), m_currentLabel);
    }
    return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\"><tr>"
                          "<td width=\"15%\"></td><td align=\"center\">%1</td>"
                          "<td width=\"15%\" align=\"right\" class=\"eqno\">%2</td></tr></table>")
        .arg(content, number);
}

// align, gather and multline: one table row per line, numbered unless
// starred or marked \nonumber / \notag.
QString Converter::mathRows(const QString &body, bool aligned, bool numbered)
{
    const bool saved = m_display;
    m_display = true;
    QString rows;
    for (const QString &row : splitTopLevel(body, u'\\')) {
        if (row.trimmed().isEmpty())
            continue;
        m_noNumber = false;
        m_tag.clear();
        QString cells;
        const QStringList parts = aligned ? splitTopLevel(row, u'&') : QStringList{row};
        for (int c = 0; c < parts.size(); ++c) {
            const QString align = !aligned ? QStringLiteral("center")
                                  : c % 2 == 0 ? QStringLiteral("right") : QStringLiteral("left");
            cells += QStringLiteral("<td align=\"%1\">").arg(align) + math(parts.at(c))
                     + QStringLiteral("</td>");
        }
        QString number;
        if (!m_tag.isEmpty()) {
            number = u'(' + m_tag + u')';
        } else if (numbered && !m_noNumber) {
            number = u'(' + nextEquationNumber() + u')';
            static const QRegularExpression label(QStringLiteral("\\\\label\\s*\\{([^}]*)\\}"));
            const QRegularExpressionMatch match = label.match(row);
            if (match.hasMatch())
                setLabel(QStringLiteral("r"), match.captured(1), m_currentLabel);
        }
        rows += QStringLiteral("<tr><td width=\"15%\"></td>") + cells
                + QStringLiteral("<td width=\"15%\" align=\"right\" class=\"eqno\">") + number
                + QStringLiteral("</td></tr>");
    }
    m_display = saved;
    return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"3\">") + rows
           + QStringLiteral("</table>");
}

QString Converter::inlineMath(const QString &body)
{
    const bool saved = m_display;
    m_display = false;
    const QString html = math(body);
    m_display = saved;
    return html;
}

QString Converter::math(const QString &s)
{
    if (m_depth >= MaxDepth)
        return esc(s);
    ++m_depth;
    QString out;
    QString letters; // a run of italic letters
    auto flush = [&] {
        if (!letters.isEmpty()) {
            out += m_upright ? letters : QStringLiteral("<i>") + letters + QStringLiteral("</i>");
            letters.clear();
        }
    };
    auto symbol = [&](const QString &value) {
        flush();
        if (isRelation(value))
            out += u' ' + esc(value) + u' ';
        else if (isBinaryOperator(value))
            out += u' ' + esc(value) + u' ';
        else
            out += esc(value);
    };

    const int n = int(s.size());
    int i = 0;
    while (i < n && out.size() < MaxOutput) {
        const QChar ch = s.at(i);
        if (ch.isLetter() && ch.unicode() < 128) {
            letters += ch;
            ++i;
            continue;
        }
        switch (ch.unicode()) {
        case '\\': {
            flush();
            mathCommand(s, i, out);
            break;
        }
        case '^':
        case '_': {
            flush();
            ++i;
            const QString arg = readArg(s, i);
            const char *tag = ch == u'^' ? "sup" : "sub";
            out += QStringLiteral("<%1>").arg(QLatin1String(tag)) + math(arg)
                   + QStringLiteral("</%1>").arg(QLatin1String(tag));
            break;
        }
        case '{':
            flush();
            out += math(readBraced(s, i));
            break;
        case '}':
            ++i;
            break;
        case '%':
            flush();
            skipComment(s, i);
            break;
        case '\'': {
            flush();
            int primes = 0;
            while (i < n && s.at(i) == u'\'') {
                ++primes;
                ++i;
            }
            out += primes == 1 ? QStringLiteral("′") : primes == 2 ? QStringLiteral("″") : QStringLiteral("‴");
            break;
        }
        case '-':
            symbol(QStringLiteral("−"));
            ++i;
            break;
        case '*':
            symbol(QStringLiteral("∗"));
            ++i;
            break;
        case '=':
        case '<':
        case '>':
        case '+':
            symbol(QString(ch));
            ++i;
            break;
        case '~':
            flush();
            out += QStringLiteral("&nbsp;");
            ++i;
            break;
        case '&':
            flush();
            out += QStringLiteral(" ");
            ++i;
            break;
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            ++i; // TeX ignores spaces in maths
            break;
        default:
            flush();
            out += escChar(ch);
            ++i;
        }
    }
    flush();
    --m_depth;
    return out;
}

void Converter::mathCommand(const QString &s, int &i, QString &out)
{
    const QString name = readCommandName(s, i);
    const bool word = !name.isEmpty() && isLetter(name.at(0));
    if (word && i < s.size() && s.at(i) == u'*')
        ++i;

    auto spaced = [&](const QString &value) {
        if (isRelation(value))
            out += u' ' + esc(value) + u' ';
        else if (isBinaryOperator(value))
            out += u' ' + esc(value) + u' ';
        else
            out += esc(value);
    };

    if (!word) {
        if (name == QLatin1String(",") || name == QLatin1String(">"))
            out += QStringLiteral(" ");
        else if (name == QLatin1String(";") || name == QLatin1String(":"))
            out += QStringLiteral(" ");
        else if (name == QLatin1String(" "))
            out += u' ';
        else if (name == QLatin1String("!") || name == QLatin1String("/"))
            ; // negative and italic-correction spaces
        else if (name == QLatin1String("\\"))
            out += QStringLiteral("<br>");
        else if (const auto symbol = mathSymbols().constFind(name); symbol != mathSymbols().cend())
            out += esc(*symbol);
        else
            out += esc(name); // \% \& \$ \# \_
        return;
    }

    if (const auto macro = m_macros.constFind(name); macro != m_macros.cend()) {
        if (!budgetExceeded())
            out += math(expand(*macro, s, i));
        return;
    }
    if (const auto symbol = mathSymbols().constFind(name); symbol != mathSymbols().cend()) {
        spaced(*symbol);
        return;
    }

    static const QSet<QString> functions{
        "sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh",
        "tanh", "coth", "log", "ln", "lg", "exp", "lim", "liminf", "limsup", "max", "min", "sup",
        "inf", "det", "gcd", "arg", "deg", "dim", "ker", "hom", "Pr", "mod", "bmod"};
    if (functions.contains(name)) {
        out += (name == QLatin1String("bmod") || name == QLatin1String("mod"))
                   ? QStringLiteral(" mod ")
                   : name + u' ';
        return;
    }

    if (name == QLatin1String("frac") || name == QLatin1String("dfrac") || name == QLatin1String("tfrac")
        || name == QLatin1String("cfrac") || name == QLatin1String("over")) {
        const QString top = readArg(s, i);
        const QString bottom = readArg(s, i);
        const bool simple = top.trimmed().size() <= 1 && bottom.trimmed().size() <= 1;
        if (simple)
            out += math(top) + u'/' + math(bottom);
        else
            out += QStringLiteral("(") + math(top) + QStringLiteral(")/(") + math(bottom) + u')';
        return;
    }
    if (name == QLatin1String("binom") || name == QLatin1String("dbinom") || name == QLatin1String("tbinom")) {
        const QString top = math(readArg(s, i));
        const QString bottom = math(readArg(s, i));
        out += QStringLiteral("(<sup>") + top + QStringLiteral("</sup><sub>") + bottom + QStringLiteral("</sub>)");
        return;
    }
    if (name == QLatin1String("sqrt")) {
        QString root;
        readOptional(s, i, &root);
        const QString arg = readArg(s, i);
        if (!root.isEmpty())
            out += QStringLiteral("<sup>") + math(root) + QStringLiteral("</sup>");
        out += QStringLiteral("√");
        out += arg.trimmed().size() <= 1 ? math(arg) : u'(' + math(arg) + u')';
        return;
    }
    if (name == QLatin1String("text") || name == QLatin1String("textrm") || name == QLatin1String("mbox")
        || name == QLatin1String("textnormal") || name == QLatin1String("textup")
        || name == QLatin1String("hbox") || name == QLatin1String("intertext")) {
        out += text(readArg(s, i));
        return;
    }
    if (name == QLatin1String("textit") || name == QLatin1String("textbf") || name == QLatin1String("texttt")
        || name == QLatin1String("emph")) {
        const char *tag = name == QLatin1String("textbf") ? "b" : name == QLatin1String("texttt") ? "tt" : "i";
        out += QStringLiteral("<%1>").arg(QLatin1String(tag)) + text(readArg(s, i))
               + QStringLiteral("</%1>").arg(QLatin1String(tag));
        return;
    }
    if (name == QLatin1String("mathrm") || name == QLatin1String("operatorname")
        || name == QLatin1String("mathup") || name == QLatin1String("mathsf")
        || name == QLatin1String("mathtt") || name == QLatin1String("rm")) {
        const bool saved = m_upright;
        m_upright = true;
        const QString content = math(readArg(s, i));
        m_upright = saved;
        out += name == QLatin1String("mathtt") ? QStringLiteral("<tt>") + content + QStringLiteral("</tt>")
               : name == QLatin1String("operatorname") ? content + u' '
                                                       : content;
        return;
    }
    if (name == QLatin1String("mathbf") || name == QLatin1String("boldsymbol") || name == QLatin1String("bm")
        || name == QLatin1String("mathbfit")) {
        const bool saved = m_upright;
        m_upright = name == QLatin1String("mathbf");
        out += QStringLiteral("<b>") + math(readArg(s, i)) + QStringLiteral("</b>");
        m_upright = saved;
        return;
    }
    if (name == QLatin1String("mathit")) {
        out += QStringLiteral("<i>") + math(readArg(s, i)) + QStringLiteral("</i>");
        return;
    }
    if (name == QLatin1String("mathbb") || name == QLatin1String("mathcal") || name == QLatin1String("mathfrak")
        || name == QLatin1String("mathscr") || name == QLatin1String("mathbbm")) {
        const QString style = name == QLatin1String("mathbbm") ? QStringLiteral("mathbb") : name;
        for (const QChar c : readArg(s, i)) {
            if (!c.isSpace())
                out += styledLetter(style, c);
        }
        return;
    }
    if (const QChar mark = accentMark(name); !mark.isNull()) {
        const QString arg = readArg(s, i).trimmed();
        if (arg.size() == 1 && arg.at(0).isLetterOrNumber()) {
            const bool italic = arg.at(0).isLetter() && arg.at(0).unicode() < 128 && !m_upright;
            out += (italic ? QStringLiteral("<i>") : QString()) + esc(arg) + mark
                   + (italic ? QStringLiteral("</i>") : QString());
        } else if (name == QLatin1String("bar") || name == QLatin1String("overline")) {
            out += QStringLiteral("<span style=\"text-decoration:overline\">") + math(arg) + QStringLiteral("</span>");
        } else {
            out += math(arg) + mark;
        }
        return;
    }
    if (name == QLatin1String("overline")) {
        out += QStringLiteral("<span style=\"text-decoration:overline\">") + math(readArg(s, i)) + QStringLiteral("</span>");
        return;
    }
    if (name == QLatin1String("underline")) {
        out += QStringLiteral("<u>") + math(readArg(s, i)) + QStringLiteral("</u>");
        return;
    }
    if (name == QLatin1String("overbrace") || name == QLatin1String("underbrace")
        || name == QLatin1String("boxed") || name == QLatin1String("displaystyle")
        || name == QLatin1String("textstyle") || name == QLatin1String("scriptstyle")
        || name == QLatin1String("phantom") || name == QLatin1String("smash")) {
        if (name == QLatin1String("phantom")) {
            readArg(s, i);
            return;
        }
        if (name.endsWith(QLatin1String("style")))
            return;
        out += math(readArg(s, i));
        return;
    }
    if (name == QLatin1String("stackrel") || name == QLatin1String("overset")
        || name == QLatin1String("underset")) {
        const QString above = math(readArg(s, i));
        const QString base = math(readArg(s, i));
        out += base + (name == QLatin1String("underset") ? QStringLiteral("<sub>") + above + QStringLiteral("</sub>")
                                                         : QStringLiteral("<sup>") + above + QStringLiteral("</sup>"));
        return;
    }
    if (name == QLatin1String("pmod")) {
        out += QStringLiteral(" (mod ") + math(readArg(s, i)) + u')';
        return;
    }
    static const QSet<QString> sizes{"left", "right", "middle", "big", "Big", "bigg", "Bigg",
                                     "bigl", "bigr", "Bigl", "Bigr", "biggl", "biggr", "Biggl",
                                     "Biggr", "bigm", "Bigm"};
    if (sizes.contains(name)) {
        skipSpace(s, i);
        if (i < s.size() && s.at(i) == u'.')
            ++i; // \left. is an invisible delimiter
        return;
    }
    if (name == QLatin1String("quad")) {
        out += QStringLiteral(" ");
        return;
    }
    if (name == QLatin1String("qquad")) {
        out += QStringLiteral("  ");
        return;
    }
    if (name == QLatin1String("label")) {
        readArg(s, i);
        return;
    }
    if (name == QLatin1String("nonumber") || name == QLatin1String("notag")) {
        m_noNumber = true;
        return;
    }
    if (name == QLatin1String("tag")) {
        m_tag = text(readArg(s, i));
        return;
    }
    if (name == QLatin1String("begin")) {
        const QString env = readArg(s, i).trimmed();
        const int close = findEnd(s, i, env);
        const QString body = s.mid(i, close < 0 ? -1 : close - i);
        i = close < 0 ? int(s.size()) : close + 6 + int(env.size());
        out += matrix(env, body);
        return;
    }
    if (name == QLatin1String("end")) {
        readArg(s, i);
        return;
    }
    if (name == QLatin1String("ref") || name == QLatin1String("eqref")) {
        const QString ref = reference(QStringLiteral("r"), readArg(s, i).trimmed());
        out += name == QLatin1String("eqref") ? u'(' + ref + u')' : ref;
        return;
    }
    // Unknown: show its name, which is usually the clearest guess.
    out += esc(name);
}

// Matrices and friends inside maths: a small table in display maths, or a
// bracketed list of rows inline.
QString Converter::matrix(const QString &name, const QString &rawBody)
{
    QString body = rawBody;
    if (name == QLatin1String("array") || name == QLatin1String("subarray")) {
        int k = 0;
        readOptional(body, k);
        readArg(body, k);
        body.remove(0, k);
    }
    QString open, close;
    if (name == QLatin1String("pmatrix")) {
        open = QStringLiteral("(");
        close = QStringLiteral(")");
    } else if (name == QLatin1String("bmatrix")) {
        open = QStringLiteral("[");
        close = QStringLiteral("]");
    } else if (name == QLatin1String("Bmatrix") || name == QLatin1String("cases")) {
        open = QStringLiteral("{");
        close = name == QLatin1String("cases") ? QString() : QStringLiteral("}");
    } else if (name == QLatin1String("vmatrix")) {
        open = close = QStringLiteral("|");
    } else if (name == QLatin1String("Vmatrix")) {
        open = close = QStringLiteral("‖");
    }

    const QStringList rows = splitTopLevel(body, u'\\');
    const bool aligned = name == QLatin1String("aligned") || name == QLatin1String("split")
                         || name == QLatin1String("alignedat");
    if (!m_display) {
        QStringList parts;
        for (const QString &row : rows) {
            if (row.trimmed().isEmpty())
                continue;
            QStringList cells;
            for (const QString &cell : splitTopLevel(row, u'&'))
                cells << math(cell);
            parts << cells.join(QStringLiteral(" "));
        }
        return esc(open) + parts.join(QStringLiteral("; ")) + esc(close);
    }
    QString html = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"2\"><tr>");
    if (!open.isEmpty())
        html += QStringLiteral("<td valign=\"middle\"><span style=\"font-size:xx-large\">%1</span></td>").arg(esc(open));
    html += QStringLiteral("<td><table cellspacing=\"0\" cellpadding=\"3\">");
    for (const QString &row : rows) {
        if (row.trimmed().isEmpty())
            continue;
        html += QStringLiteral("<tr>");
        const QStringList cells = splitTopLevel(row, u'&');
        for (int c = 0; c < cells.size(); ++c) {
            const QString align = aligned ? (c % 2 == 0 ? QStringLiteral("right") : QStringLiteral("left"))
                                  : name == QLatin1String("cases") ? QStringLiteral("left")
                                                                   : QStringLiteral("center");
            html += QStringLiteral("<td align=\"%1\">").arg(align) + math(cells.at(c)) + QStringLiteral("</td>");
        }
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table></td>");
    if (!close.isEmpty())
        html += QStringLiteral("<td valign=\"middle\"><span style=\"font-size:xx-large\">%1</span></td>").arg(esc(close));
    html += QStringLiteral("</tr></table>");
    return html;
}

} // namespace

namespace LatexHtml {

QString toHtml(const QString &latex)
{
    Converter converter;
    return converter.run(latex);
}

} // namespace LatexHtml
