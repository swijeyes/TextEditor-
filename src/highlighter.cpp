#include "highlighter.h"

#include "spellchecker.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace {

struct WordLists
{
    QSet<QString> keywords;
    QSet<QString> types;
    QSet<QString> constants;
};

const WordLists &wordsFor(Language language)
{
    static const WordLists none;
    static const WordLists javaScript{
        {"async", "await", "break", "case", "catch", "class", "const", "continue", "debugger",
         "default", "delete", "do", "else", "export", "extends", "finally", "for", "from",
         "function", "get", "if", "import", "in", "instanceof", "let", "new", "of", "return",
         "set", "static", "super", "switch", "this", "throw", "try", "typeof", "var", "void",
         "while", "with", "yield"},
        {"Array", "Boolean", "Date", "Error", "JSON", "Map", "Math", "Number", "Object",
         "Promise", "RegExp", "Set", "String", "Symbol", "console", "document", "window"},
        {"true", "false", "null", "undefined", "NaN", "Infinity"}};
    static const WordLists c{
        {"auto", "break", "case", "const", "continue", "default", "do", "else", "enum",
         "extern", "for", "goto", "if", "inline", "register", "restrict", "return", "sizeof",
         "static", "struct", "switch", "typedef", "union", "volatile", "while"},
        {"void", "char", "short", "int", "long", "float", "double", "signed", "unsigned",
         "bool", "_Bool", "size_t", "ssize_t", "ptrdiff_t", "int8_t", "int16_t", "int32_t",
         "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "FILE"},
        {"NULL", "true", "false"}};
    static const WordLists cpp = [] {
        WordLists words = c;
        words.keywords.unite({"alignas", "alignof", "catch", "class", "concept", "consteval",
                              "constexpr", "constinit", "const_cast", "co_await", "co_return",
                              "co_yield", "decltype", "delete", "dynamic_cast", "explicit",
                              "export", "final", "friend", "mutable", "namespace", "new",
                              "noexcept", "operator", "override", "private", "protected",
                              "public", "reinterpret_cast", "requires", "static_assert",
                              "static_cast", "template", "this", "thread_local", "throw", "try",
                              "typeid", "typename", "using", "virtual"});
        words.types.unite({"wchar_t", "char8_t", "char16_t", "char32_t", "std", "string",
                           "vector", "map", "unique_ptr", "shared_ptr"});
        words.constants.unite({"nullptr"});
        return words;
    }();

    static const WordLists python{
        {"and", "as", "assert", "async", "await", "break", "case", "class", "continue", "def",
         "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import",
         "in", "is", "lambda", "match", "nonlocal", "not", "or", "pass", "raise", "return",
         "try", "while", "with", "yield"},
        {"bool", "bytearray", "bytes", "complex", "dict", "float", "frozenset", "int", "list",
         "object", "range", "set", "str", "tuple", "type", "abs", "all", "any", "callable",
         "dir", "enumerate", "filter", "format", "getattr", "hasattr", "hash", "id", "input",
         "isinstance", "issubclass", "iter", "len", "map", "max", "min", "next", "open",
         "print", "repr", "reversed", "round", "setattr", "sorted", "sum", "super", "vars",
         "zip", "Exception", "BaseException", "ValueError", "TypeError", "KeyError",
         "IndexError", "AttributeError", "RuntimeError", "OSError", "StopIteration",
         "NotImplementedError"},
        {"True", "False", "None", "self", "cls", "NotImplemented", "Ellipsis", "__name__"}};
    static const WordLists rust{
        {"as", "async", "await", "break", "const", "continue", "crate", "dyn", "else", "enum",
         "extern", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move",
         "mut", "pub", "ref", "return", "static", "struct", "super", "trait", "type", "union",
         "unsafe", "use", "where", "while", "yield", "macro_rules"},
        {"i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16", "u32", "u64", "u128",
         "usize", "f32", "f64", "bool", "char", "str"},
        {"true", "false", "self", "Self"}};

    switch (language) {
    case Language::JavaScript: return javaScript;
    case Language::C: return c;
    case Language::Cpp: return cpp;
    case Language::Python: return python;
    case Language::Rust: return rust;
    default: return none;
    }
}

bool isIdentStart(QChar ch) { return ch.isLetter() || ch == u'_' || ch == u'$'; }
bool isIdentChar(QChar ch) { return ch.isLetterOrNumber() || ch == u'_' || ch == u'$'; }

bool isPlainIdentStart(QChar ch) { return ch.isLetter() || ch == u'_'; }
bool isPlainIdentChar(QChar ch) { return ch.isLetterOrNumber() || ch == u'_'; }

int skipIdentifier(const QString &text, int i)
{
    while (i < text.size() && isPlainIdentChar(text.at(i)))
        ++i;
    return i;
}

// Numbers in Python and Rust: 42, 0xff, 1_000, 3.14e-2, 2j, 10u32, 1.5f64.
int skipNumber(const QString &text, int i)
{
    const int length = text.size();
    while (i < length) {
        const QChar ch = text.at(i);
        if (ch.isLetterOrNumber() || ch == u'_') {
            ++i;
        } else if (ch == u'.' && i + 1 < length && text.at(i + 1).isDigit()) {
            i += 2;
        } else if ((ch == u'+' || ch == u'-') && i > 0
                   && (text.at(i - 1) == u'e' || text.at(i - 1) == u'E')
                   && i + 1 < length && text.at(i + 1).isDigit()
                   && !(i > 1 && text.at(i - 2) == u'x')) {
            ++i;
        } else {
            break;
        }
    }
    return i;
}

int firstNonBlank(const QString &text)
{
    int i = 0;
    while (i < text.size() && text.at(i).isSpace())
        ++i;
    return i;
}

bool isHexColor(QStringView word)
{
    if (word.size() != 3 && word.size() != 4 && word.size() != 6 && word.size() != 8)
        return false;
    for (QChar ch : word) {
        if (!ch.isDigit() && !(ch.toLower() >= u'a' && ch.toLower() <= u'f'))
            return false;
    }
    return true;
}

} // namespace

namespace {

// Each token's format carries its token number, so the spelling pass can
// tell comments and strings from code.
constexpr int TokenProperty = QTextFormat::UserProperty + 1;

} // namespace

Highlighter::Highlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    setTheme(Theme::dracula());
    connect(SpellChecker::instance(), &SpellChecker::dictionaryChanged, this, [this] {
        if (m_spellCheck)
            rehighlight();
    });
}

void Highlighter::setSpellCheck(bool on)
{
    if (on == m_spellCheck)
        return;
    m_spellCheck = on;
    rehighlight();
}

void Highlighter::setLanguage(Language language)
{
    if (language == m_language)
        return;
    m_language = language;
    rehighlight();
}

void Highlighter::setTheme(const Theme &theme)
{
    const QColor colors[TokenCount] = {theme.keyword, theme.type, theme.function, theme.string,
                                       theme.number, theme.comment, theme.preprocessor,
                                       theme.tag, theme.attribute};
    for (int i = 0; i < TokenCount; ++i) {
        m_formats[i] = QTextCharFormat();
        m_formats[i].setForeground(colors[i]);
        m_formats[i].setProperty(TokenProperty, i);
    }
    m_misspelledColor = theme.dark ? QColor(0xff, 0x55, 0x55) : QColor(0xe0, 0x00, 0x00);
    if (!theme.dark)
        m_formats[Keyword].setFontWeight(QFont::Bold);
    m_formats[Comment].setFontItalic(theme.dark);
    rehighlight();
}

QList<Language> Highlighter::languages()
{
    return {Language::PlainText, Language::Html, Language::Css, Language::JavaScript,
            Language::C, Language::Cpp, Language::Markdown, Language::Python, Language::Rust,
            Language::Latex};
}

QString Highlighter::languageName(Language language)
{
    switch (language) {
    case Language::PlainText: return tr("Plain Text");
    case Language::Html: return tr("HTML");
    case Language::Css: return tr("CSS");
    case Language::JavaScript: return tr("JavaScript");
    case Language::C: return tr("C");
    case Language::Cpp: return tr("C++");
    case Language::Markdown: return tr("Markdown");
    case Language::Python: return tr("Python");
    case Language::Rust: return tr("Rust");
    case Language::Latex: return tr("LaTeX");
    }
    return {};
}

Language Highlighter::languageForFile(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (QStringList{"html", "htm", "xhtml", "xml", "svg", "vue"}.contains(suffix))
        return Language::Html;
    if (QStringList{"css", "scss", "less"}.contains(suffix))
        return Language::Css;
    if (QStringList{"js", "mjs", "cjs", "jsx", "ts", "tsx", "json"}.contains(suffix))
        return Language::JavaScript;
    if (QStringList{"c", "h"}.contains(suffix))
        return Language::C;
    if (QStringList{"cpp", "cc", "cxx", "hpp", "hh", "hxx", "ino"}.contains(suffix))
        return Language::Cpp;
    if (QStringList{"md", "markdown", "mdown", "mkd", "mdx"}.contains(suffix))
        return Language::Markdown;
    if (QStringList{"py", "pyw", "pyi"}.contains(suffix))
        return Language::Python;
    if (suffix == QLatin1String("rs"))
        return Language::Rust;
    if (QStringList{"tex", "latex", "ltx", "sty", "cls"}.contains(suffix))
        return Language::Latex;
    return Language::PlainText;
}

void Highlighter::highlightBlock(const QString &text)
{
    switch (m_language) {
    case Language::PlainText:
        break;
    case Language::Html:
        highlightHtml(text);
        break;
    case Language::Markdown:
        highlightMarkdown(text);
        break;
    case Language::Python:
        highlightPython(text);
        break;
    case Language::Rust:
        highlightRust(text);
        break;
    case Language::Latex:
        highlightLatex(text);
        break;
    default: {
        bool inComment = previousBlockState() == InBlockComment;
        highlightCode(text, 0, text.size(), m_language, inComment);
        setCurrentBlockState(inComment ? InBlockComment : Normal);
    }
    }
    if (m_spellCheck)
        markMisspellings(text);
}

bool Highlighter::isProse(int position) const
{
    const QVariant token = format(position).property(TokenProperty);
    const int t = token.isValid() ? token.toInt() : -1;
    switch (m_language) {
    case Language::PlainText:
        return true;
    case Language::Markdown:
        // Text, headings and link text; not code, addresses or HTML.
        return t == -1 || t == Keyword || t == Function;
    case Language::Html:
    case Language::Latex:
        return t == -1 || t == Comment;
    default:
        return t == Comment || t == String;
    }
}

void Highlighter::markMisspellings(const QString &text)
{
    SpellChecker *checker = SpellChecker::instance();
    if (!checker->isAvailable())
        return;
    int i = 0;
    while (i < text.size()) {
        int start, end;
        if (!SpellChecker::isWordChar(text.at(i)) || !SpellChecker::wordAt(text, i, &start, &end)) {
            ++i;
            continue;
        }
        i = end;
        bool prose = true;
        for (int k = start; k < end && prose; ++k)
            prose = isProse(k);
        if (!prose || !SpellChecker::shouldCheck(text, start, end)
            || checker->isCorrect(text.mid(start, end - start)))
            continue;
        // Character by character, keeping each one's colour and style.
        for (int k = start; k < end; ++k) {
            QTextCharFormat f = format(k);
            f.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            f.setUnderlineColor(m_misspelledColor);
            setFormat(k, 1, f);
        }
    }
}

void Highlighter::highlightCode(const QString &text, int from, int to, Language language,
                                bool &inComment)
{
    const bool cLike = language == Language::C || language == Language::Cpp;
    const bool css = language == Language::Css;
    const WordLists &words = wordsFor(language);

    int firstNonBlank = from;
    while (firstNonBlank < to && text.at(firstNonBlank).isSpace())
        ++firstNonBlank;

    int i = from;
    while (i < to) {
        if (inComment) {
            // Search only up to 'to': in HTML this runs once per <script> or
            // <style> section, and scanning the rest of a long line each time
            // would be quadratic.
            const qsizetype found = QStringView(text).mid(i, to - i).indexOf(u"*/");
            const int end = found < 0 ? -1 : i + int(found);
            if (end < 0) {
                setFormat(i, to - i, m_formats[Comment]);
                return;
            }
            setFormat(i, end + 2 - i, m_formats[Comment]);
            i = end + 2;
            inComment = false;
            continue;
        }

        const QChar ch = text.at(i);
        const QChar next = i + 1 < to ? text.at(i + 1) : QChar();

        if (ch == u'/' && next == u'*') {
            setFormat(i, 2, m_formats[Comment]);
            i += 2;
            inComment = true;
            continue;
        }
        if (ch == u'/' && next == u'/' && !css) {
            setFormat(i, to - i, m_formats[Comment]);
            return;
        }

        // Preprocessor directive: '#' as the first non-blank character.
        if (cLike && ch == u'#' && i == firstNonBlank) {
            int end = to;
            for (const auto marker : {QLatin1String("//"), QLatin1String("/*")}) {
                const int pos = text.indexOf(marker, i);
                if (pos >= 0 && pos < end)
                    end = pos;
            }
            setFormat(i, end - i, m_formats[Preprocessor]);
            i = end;
            continue;
        }

        if (ch == u'"' || ch == u'\'' || (ch == u'`' && language == Language::JavaScript)) {
            int j = i + 1;
            while (j < to && text.at(j) != ch) {
                if (text.at(j) == u'\\')
                    ++j;
                ++j;
            }
            j = qMin(j + 1, to);
            setFormat(i, j - i, m_formats[String]);
            i = j;
            continue;
        }

        if ((ch.isDigit() || (ch == u'.' && next.isDigit()))
            && (i == from || !isIdentChar(text.at(i - 1)))) {
            int j = i + 1;
            while (j < to && (text.at(j).isLetterOrNumber() || text.at(j) == u'.'
                              || (cLike && text.at(j) == u'\'')))
                ++j;
            if (css && j < to && text.at(j) == u'%')
                ++j;
            setFormat(i, j - i, m_formats[Number]);
            i = j;
            continue;
        }

        // CSS selectors (.class, #id), hex colours and at-rules.
        if (css && (ch == u'#' || ch == u'.' || ch == u'@')
            && (isIdentChar(next) || next == u'-')) {
            int j = i + 1;
            while (j < to && (isIdentChar(text.at(j)) || text.at(j) == u'-'))
                ++j;
            const QStringView word = QStringView(text).mid(i + 1, j - i - 1);
            const Token token = ch == u'@' ? Keyword
                                : (ch == u'#' && isHexColor(word)) ? Number
                                                                   : Tag;
            setFormat(i, j - i, m_formats[token]);
            i = j;
            continue;
        }

        if (isIdentStart(ch) || (css && ch == u'-' && isIdentStart(next))) {
            int j = i + 1;
            while (j < to && (isIdentChar(text.at(j)) || (css && text.at(j) == u'-')))
                ++j;
            const QString word = text.mid(i, j - i);
            int k = j;
            while (k < to && text.at(k).isSpace())
                ++k;
            const QChar after = k < to ? text.at(k) : QChar();

            if (css) {
                if (after == u':')
                    setFormat(i, j - i, m_formats[Attribute]);
                else if (word == QLatin1String("important") && i > 0 && text.at(i - 1) == u'!')
                    setFormat(i - 1, j - i + 1, m_formats[Keyword]);
            } else if (words.keywords.contains(word)) {
                setFormat(i, j - i, m_formats[Keyword]);
            } else if (words.types.contains(word)) {
                setFormat(i, j - i, m_formats[Type]);
            } else if (words.constants.contains(word)) {
                setFormat(i, j - i, m_formats[Number]);
            } else if (after == u'(') {
                setFormat(i, j - i, m_formats[Function]);
            }
            i = j;
            continue;
        }

        ++i;
    }
}

void Highlighter::highlightHtml(const QString &text)
{
    int state = qMax(previousBlockState(), 0);
    const int length = text.size();
    int i = 0;

    while (i < length) {
        switch (state) {
        case InScript:
        case InScriptComment:
        case InStyle:
        case InStyleComment: {
            const bool script = state == InScript || state == InScriptComment;
            const int close = text.indexOf(script ? QLatin1String("</script") : QLatin1String("</style"),
                                           i, Qt::CaseInsensitive);
            const int end = close < 0 ? length : close;
            bool inComment = state == InScriptComment || state == InStyleComment;
            highlightCode(text, i, end, script ? Language::JavaScript : Language::Css, inComment);
            if (close < 0) {
                state = script ? (inComment ? InScriptComment : InScript)
                               : (inComment ? InStyleComment : InStyle);
                i = length;
            } else {
                state = Normal;
                i = close;
            }
            break;
        }
        case InHtmlComment: {
            const int end = text.indexOf(QLatin1String("-->"), i);
            if (end < 0) {
                setFormat(i, length - i, m_formats[Comment]);
                i = length;
            } else {
                setFormat(i, end + 3 - i, m_formats[Comment]);
                i = end + 3;
                state = Normal;
            }
            break;
        }
        case InTag:
        case InScriptTag:
        case InStyleTag:
            i = highlightTagBody(text, i, state);
            break;
        default: {
            const QChar ch = text.at(i);
            const QChar next = i + 1 < length ? text.at(i + 1) : QChar();

            if (QStringView(text).mid(i).startsWith(u"<!--")) {
                setFormat(i, 4, m_formats[Comment]);
                i += 4;
                state = InHtmlComment;
                break;
            }
            if (ch == u'<' && (next.isLetter() || next == u'/' || next == u'!')) {
                const bool closing = next == u'/';
                int j = i + 1;
                if (next == u'/' || next == u'!')
                    ++j;
                const int nameStart = j;
                while (j < length && (text.at(j).isLetterOrNumber() || text.at(j) == u'-'
                                      || text.at(j) == u':'))
                    ++j;
                const QString name = text.mid(nameStart, j - nameStart).toLower();
                setFormat(i, j - i, m_formats[Tag]);
                if (!closing && name == QLatin1String("script"))
                    state = InScriptTag;
                else if (!closing && name == QLatin1String("style"))
                    state = InStyleTag;
                else
                    state = InTag;
                i = highlightTagBody(text, j, state);
                break;
            }
            if (ch == u'&') {
                // Entities are short, so only look a few characters ahead;
                // searching the whole line for each '&' would be quadratic.
                const QStringView entity = QStringView(text).mid(i, 11);
                const int semi = int(entity.indexOf(u';'));
                if (semi > 1 && !entity.left(semi).contains(u' ')) {
                    setFormat(i, semi + 1, m_formats[Number]);
                    i += semi + 1;
                    break;
                }
            }
            ++i;
        }
        }
    }

    setCurrentBlockState(state);
}

// Highlights attributes up to and including the closing '>'. Returns the
// position after the tag, or the end of the line if the tag continues.
int Highlighter::highlightTagBody(const QString &text, int pos, int &state)
{
    const int length = text.size();
    int i = pos;
    while (i < length) {
        const QChar ch = text.at(i);
        if (ch == u'>' || (ch == u'/' && i + 1 < length && text.at(i + 1) == u'>')) {
            const int n = ch == u'>' ? 1 : 2;
            setFormat(i, n, m_formats[Tag]);
            state = state == InScriptTag ? InScript : state == InStyleTag ? InStyle : Normal;
            return i + n;
        }
        if (ch == u'"' || ch == u'\'') {
            const int close = text.indexOf(ch, i + 1);
            const int end = close < 0 ? length : close + 1;
            setFormat(i, end - i, m_formats[String]);
            i = end;
            continue;
        }
        if (ch.isLetter() || ch == u'_' || ch == u':' || ch == u'@') {
            int j = i + 1;
            while (j < length && (text.at(j).isLetterOrNumber() || text.at(j) == u'-'
                                  || text.at(j) == u'_' || text.at(j) == u':' || text.at(j) == u'.'))
                ++j;
            setFormat(i, j - i, m_formats[Attribute]);
            i = j;
            continue;
        }
        ++i;
    }
    return length;
}

void Highlighter::addStyle(int start, int count, bool bold, bool italic, bool strikeOut)
{
    for (int k = start; k < start + count; ++k) {
        QTextCharFormat f = format(k);
        if (bold)
            f.setFontWeight(QFont::Bold);
        if (italic)
            f.setFontItalic(true);
        if (strikeOut)
            f.setFontStrikeOut(true);
        setFormat(k, 1, f);
    }
}

// --- Python ----------------------------------------------------------------------

namespace {

// The position of the closing ''' or """ at or after 'from', skipping
// backslash escapes, or -1.
int findTripleQuote(const QString &text, int from, QChar quote)
{
    const int length = text.size();
    for (int j = from; j < length; ++j) {
        const QChar ch = text.at(j);
        if (ch == u'\\') {
            ++j;
        } else if (ch == quote && j + 2 < length && text.at(j + 1) == quote
                   && text.at(j + 2) == quote) {
            return j;
        }
    }
    return -1;
}

} // namespace

void Highlighter::highlightPython(const QString &text)
{
    const WordLists &words = wordsFor(Language::Python);
    const int length = text.size();
    int state = qMax(previousBlockState(), 0);
    int i = 0;

    // A triple-quoted string carried over from the line before.
    if (state == InPythonTripleSingle || state == InPythonTripleDouble) {
        const QChar quote = state == InPythonTripleSingle ? u'\'' : u'"';
        const int end = findTripleQuote(text, 0, quote);
        if (end < 0) {
            setFormat(0, length, m_formats[String]);
            setCurrentBlockState(state);
            return;
        }
        setFormat(0, end + 3, m_formats[String]);
        i = end + 3;
    }
    state = Normal;

    const int indent = firstNonBlank(text);
    QString previousWord; // to colour the names after def and class
    while (i < length) {
        const QChar ch = text.at(i);
        const QChar next = i + 1 < length ? text.at(i + 1) : QChar();
        const bool wordStart = i == 0 || !isPlainIdentChar(text.at(i - 1));

        if (ch == u'#') {
            setFormat(i, length - i, m_formats[Comment]);
            break;
        }
        if (ch == u'@' && i == indent) {
            int j = i + 1;
            while (j < length && (isPlainIdentChar(text.at(j)) || text.at(j) == u'.'))
                ++j;
            setFormat(i, j - i, m_formats[Preprocessor]);
            i = j;
            continue;
        }

        // Strings, with an optional prefix such as r, b, f or rb.
        int quotePos = -1;
        if (ch == u'"' || ch == u'\'') {
            quotePos = i;
        } else if (wordStart && isPlainIdentStart(ch)) {
            int k = i;
            while (k < length && k - i < 2 && QStringLiteral("rRbBfFuU").contains(text.at(k)))
                ++k;
            if (k > i && k < length && (text.at(k) == u'"' || text.at(k) == u'\''))
                quotePos = k;
        }
        if (quotePos >= 0) {
            const QChar quote = text.at(quotePos);
            if (quotePos + 2 < length && text.at(quotePos + 1) == quote && text.at(quotePos + 2) == quote) {
                const int end = findTripleQuote(text, quotePos + 3, quote);
                if (end < 0) {
                    setFormat(i, length - i, m_formats[String]);
                    state = quote == u'\'' ? InPythonTripleSingle : InPythonTripleDouble;
                    break;
                }
                setFormat(i, end + 3 - i, m_formats[String]);
                i = end + 3;
            } else {
                int j = quotePos + 1;
                while (j < length && text.at(j) != quote) {
                    if (text.at(j) == u'\\')
                        ++j;
                    ++j;
                }
                j = qMin(j + 1, length);
                setFormat(i, j - i, m_formats[String]);
                i = j;
            }
            previousWord.clear();
            continue;
        }

        if ((ch.isDigit() || (ch == u'.' && next.isDigit())) && wordStart) {
            const int j = skipNumber(text, i + 1);
            setFormat(i, j - i, m_formats[Number]);
            i = j;
            previousWord.clear();
            continue;
        }

        if (isPlainIdentStart(ch)) {
            const int j = skipIdentifier(text, i);
            const QString word = text.mid(i, j - i);
            int k = j;
            while (k < length && text.at(k).isSpace())
                ++k;
            const bool attribute = i > 0 && text.at(i - 1) == u'.';
            if (previousWord == QLatin1String("def"))
                setFormat(i, j - i, m_formats[Function]);
            else if (previousWord == QLatin1String("class"))
                setFormat(i, j - i, m_formats[Type]);
            else if (words.keywords.contains(word) && !attribute)
                setFormat(i, j - i, m_formats[Keyword]);
            else if (words.constants.contains(word) && !attribute)
                setFormat(i, j - i, m_formats[Number]);
            else if (words.types.contains(word) && !attribute)
                setFormat(i, j - i, m_formats[Type]);
            else if (k < length && text.at(k) == u'(')
                setFormat(i, j - i, m_formats[Function]);
            previousWord = word;
            i = j;
            continue;
        }

        if (!ch.isSpace())
            previousWord.clear();
        ++i;
    }
    setCurrentBlockState(state);
}

// --- Rust --------------------------------------------------------------------------

void Highlighter::highlightRust(const QString &text)
{
    const WordLists &words = wordsFor(Language::Rust);
    const int length = text.size();
    int state = qMax(previousBlockState(), 0);
    int i = 0;
    QString previousWord; // to colour the names after fn, struct, enum and so on

    while (i < length) {
        // Carried over from earlier lines, or started on this one.
        if (state > InRustComment && state < InRustRawString) {
            int depth = state - InRustComment;
            int j = i;
            while (j < length && depth > 0) {
                if (text.at(j) == u'/' && j + 1 < length && text.at(j + 1) == u'*') {
                    depth = qMin(depth + 1, 255);
                    j += 2;
                } else if (text.at(j) == u'*' && j + 1 < length && text.at(j + 1) == u'/') {
                    --depth;
                    j += 2;
                } else {
                    ++j;
                }
            }
            setFormat(i, j - i, m_formats[Comment]);
            i = j;
            state = depth > 0 ? InRustComment + depth : Normal;
            continue;
        }
        if (state >= InRustRawString && state < InRustRawString + 0x100) {
            const QString close = QLatin1Char('"') + QString(state - InRustRawString, u'#');
            const int end = text.indexOf(close, i);
            if (end < 0) {
                setFormat(i, length - i, m_formats[String]);
                break;
            }
            setFormat(i, end + close.size() - i, m_formats[String]);
            i = end + close.size();
            state = Normal;
            continue;
        }
        if (state == InRustString) {
            int j = i;
            while (j < length && text.at(j) != u'"') {
                if (text.at(j) == u'\\')
                    ++j;
                ++j;
            }
            if (j >= length) {
                setFormat(i, length - i, m_formats[String]);
                break;
            }
            setFormat(i, j + 1 - i, m_formats[String]);
            i = j + 1;
            state = Normal;
            continue;
        }

        const QChar ch = text.at(i);
        const QChar next = i + 1 < length ? text.at(i + 1) : QChar();
        const bool wordStart = i == 0 || !isPlainIdentChar(text.at(i - 1));

        if (ch == u'/' && next == u'/') {
            setFormat(i, length - i, m_formats[Comment]);
            break;
        }
        if (ch == u'/' && next == u'*') {
            setFormat(i, 2, m_formats[Comment]);
            i += 2;
            state = InRustComment + 1;
            continue;
        }

        // Raw strings: r"...", r#"..."#, br"...", cr"...".
        if (wordStart && (ch == u'r' || ((ch == u'b' || ch == u'c') && next == u'r'))) {
            int k = i + (ch == u'r' ? 1 : 2);
            const int hashStart = k;
            while (k < length && text.at(k) == u'#')
                ++k;
            if (k < length && text.at(k) == u'"' && k - hashStart <= 255) {
                setFormat(i, k + 1 - i, m_formats[String]);
                state = InRustRawString + (k - hashStart);
                i = k + 1;
                continue;
            }
        }
        // Strings ("...", b"...", c"..."), which may span lines.
        if (ch == u'"' || (wordStart && (ch == u'b' || ch == u'c') && next == u'"')) {
            const int start = i;
            i += ch == u'"' ? 1 : 2;
            setFormat(start, i - start, m_formats[String]);
            state = InRustString;
            continue;
        }
        // Character literals ('a', '\n', b'x') and lifetimes ('a, 'static).
        if (ch == u'\'' || (wordStart && ch == u'b' && next == u'\'')) {
            const int q = ch == u'\'' ? i : i + 1;
            int end = -1;
            if (q + 1 < length && text.at(q + 1) == u'\\') {
                const int close = int(QStringView(text).mid(q + 2, 12).indexOf(u'\''));
                if (close >= 0)
                    end = q + 2 + close + 1;
            } else if (q + 2 < length && text.at(q + 2) == u'\'') {
                end = q + 3;
            } else if (q + 3 < length && text.at(q + 1).isHighSurrogate() && text.at(q + 3) == u'\'') {
                end = q + 4;
            }
            if (end > 0) {
                setFormat(i, end - i, m_formats[String]);
                i = end;
                continue;
            }
            if (ch == u'\'' && q + 1 < length && isPlainIdentStart(text.at(q + 1))) {
                const int j = skipIdentifier(text, q + 1);
                setFormat(i, j - i, m_formats[Attribute]);
                i = j;
                continue;
            }
            ++i;
            continue;
        }
        // Attributes: #[derive(Debug)], #![allow(unused)].
        if (ch == u'#' && (next == u'[' || (next == u'!' && i + 2 < length && text.at(i + 2) == u'['))) {
            int depth = 0;
            int j = i + 1;
            for (; j < length; ++j) {
                if (text.at(j) == u'[') {
                    ++depth;
                } else if (text.at(j) == u']' && --depth == 0) {
                    ++j;
                    break;
                }
            }
            setFormat(i, j - i, m_formats[Preprocessor]);
            i = j;
            continue;
        }

        if (ch.isDigit() && wordStart) {
            const int j = skipNumber(text, i + 1);
            setFormat(i, j - i, m_formats[Number]);
            i = j;
            previousWord.clear();
            continue;
        }

        if (isPlainIdentStart(ch)) {
            const int j = skipIdentifier(text, i);
            const QString word = text.mid(i, j - i);
            // Macros: println!, vec!, but not a != b.
            if (j < length && text.at(j) == u'!' && (j + 1 >= length || text.at(j + 1) != u'=')) {
                setFormat(i, j + 1 - i, m_formats[Function]);
                previousWord.clear();
                i = j + 1;
                continue;
            }
            int k = j;
            while (k < length && text.at(k).isSpace())
                ++k;
            if (previousWord == QLatin1String("fn"))
                setFormat(i, j - i, m_formats[Function]);
            else if (words.keywords.contains(word))
                setFormat(i, j - i, m_formats[Keyword]);
            else if (words.constants.contains(word))
                setFormat(i, j - i, m_formats[Number]);
            else if (words.types.contains(word) || word.at(0).isUpper())
                setFormat(i, j - i, m_formats[Type]);
            else if (k < length && (text.at(k) == u'(' || text.mid(k, 3) == QLatin1String("::<")))
                setFormat(i, j - i, m_formats[Function]);
            previousWord = word;
            i = j;
            continue;
        }

        if (!ch.isSpace())
            previousWord.clear();
        ++i;
    }
    setCurrentBlockState(state);
}

// --- Markdown ----------------------------------------------------------------------

void Highlighter::highlightMarkdown(const QString &text)
{
    const int length = text.size();
    const int previous = qMax(previousBlockState(), 0);

    // Inside a fenced code block: look for the closing fence.
    if (previous >= InMarkdownFence) {
        const QChar fenceChar = (previous & MarkdownTildeFence) ? u'~' : u'`';
        const int fenceLength = previous & 0xff;
        const int indent = firstNonBlank(text);
        int j = indent;
        while (j < length && text.at(j) == fenceChar)
            ++j;
        if (indent <= 3 && j - indent >= fenceLength && QStringView(text).mid(j).trimmed().isEmpty()) {
            setFormat(0, length, m_formats[Preprocessor]);
            setCurrentBlockState(Normal);
        } else {
            setFormat(0, length, m_formats[String]);
            setCurrentBlockState(previous);
        }
        return;
    }

    setCurrentBlockState(Normal);
    int i = 0;
    if (previous == InMarkdownComment) {
        const int end = text.indexOf(QLatin1String("-->"));
        if (end < 0) {
            setFormat(0, length, m_formats[Comment]);
            setCurrentBlockState(InMarkdownComment);
            return;
        }
        setFormat(0, end + 3, m_formats[Comment]);
        highlightMarkdownInline(text, end + 3, length);
        return;
    }

    const int indent = firstNonBlank(text);
    if (indent >= length) {
        return;
    }
    const QChar ch = text.at(indent);

    if (indent <= 3) {
        // Code fences: ``` or ~~~, with an optional language name.
        if (ch == u'`' || ch == u'~') {
            int j = indent;
            while (j < length && text.at(j) == ch)
                ++j;
            const int n = j - indent;
            if (n >= 3 && !(ch == u'`' && text.indexOf(u'`', j) >= 0)) {
                setFormat(indent, n, m_formats[Preprocessor]);
                setFormat(j, length - j, m_formats[Attribute]);
                setCurrentBlockState(InMarkdownFence + (ch == u'~' ? MarkdownTildeFence : 0) + qMin(n, 255));
                return;
            }
        }
        // Headings: # to ######.
        if (ch == u'#') {
            int j = indent;
            while (j < length && text.at(j) == u'#')
                ++j;
            if (j - indent <= 6 && (j == length || text.at(j).isSpace())) {
                setFormat(indent, length - indent, m_formats[Keyword]);
                addStyle(indent, length - indent, true, false);
                return;
            }
        }
        // Rules (---, ***, ___) and the underlines of setext headings (===).
        if (ch == u'-' || ch == u'*' || ch == u'_' || ch == u'=') {
            int count = 0;
            bool only = true;
            for (int j = indent; j < length && only; ++j) {
                if (text.at(j) == ch)
                    ++count;
                else if (!text.at(j).isSpace())
                    only = false;
            }
            if (only && (count >= 3 || (ch == u'=' && count >= 1))) {
                setFormat(indent, length - indent, m_formats[ch == u'=' ? Keyword : Comment]);
                return;
            }
        }
    }

    i = indent;
    // Block quotes, possibly nested: > > text.
    while (i < length && (text.at(i) == u'>' || (text.at(i) == u' ' && i > indent))) {
        if (text.at(i) == u'>')
            setFormat(i, 1, m_formats[Tag]);
        ++i;
    }
    // List items: -, *, + or 1. / 1) followed by a space, then a task box.
    if (i < length) {
        const QChar c = text.at(i);
        int j = i;
        if ((c == u'-' || c == u'*' || c == u'+') && i + 1 < length && text.at(i + 1) == u' ') {
            j = i + 1;
        } else if (c.isDigit()) {
            int k = i;
            while (k < length && k - i < 9 && text.at(k).isDigit())
                ++k;
            if (k + 1 < length && (text.at(k) == u'.' || text.at(k) == u')') && text.at(k + 1) == u' ')
                j = k + 1;
        }
        if (j > i) {
            setFormat(i, j - i, m_formats[Tag]);
            i = j + 1;
            const QStringView box = QStringView(text).mid(i, 3);
            if (box == u"[ ]" || box == u"[x]" || box == u"[X]") {
                setFormat(i, 3, m_formats[Keyword]);
                i += 3;
            }
        }
    }
    highlightMarkdownInline(text, i, length);
}

void Highlighter::highlightMarkdownInline(const QString &text, int from, int to)
{
    // Each delimiter's last search result is remembered: a later search
    // starting before it would find the same place, and once a search fails
    // there is nothing further along. This keeps long lines full of '*' or
    // '[' from taking quadratic time.
    QHash<QString, int> found; // delimiter -> position, or -1 for none
    auto findCloser = [&](const QString &delimiter, int start, auto valid) {
        const auto cached = found.constFind(delimiter);
        if (cached != found.cend() && (*cached < 0 || *cached >= start))
            return *cached;
        int j = text.indexOf(delimiter, start);
        while (j >= 0 && j < to && !valid(j))
            j = text.indexOf(delimiter, j + 1);
        if (j >= to)
            j = -1;
        found.insert(delimiter, j);
        return j;
    };
    auto anywhere = [](int) { return true; };

    int i = from;
    while (i < to) {
        const QChar ch = text.at(i);
        const QChar next = i + 1 < to ? text.at(i + 1) : QChar();

        if (ch == u'\\' && next.isPunct()) {
            i += 2;
            continue;
        }
        if (ch == u'<' && QStringView(text).mid(i, 4) == u"<!--") {
            const int end = text.indexOf(QLatin1String("-->"), i + 4);
            if (end < 0) {
                setFormat(i, to - i, m_formats[Comment]);
                setCurrentBlockState(InMarkdownComment);
                return;
            }
            setFormat(i, end + 3 - i, m_formats[Comment]);
            i = end + 3;
            continue;
        }

        // Code spans: `code`, ``code with ` inside``.
        if (ch == u'`') {
            int j = i;
            while (j < to && text.at(j) == u'`')
                ++j;
            const QString run(j - i, u'`');
            const int close = findCloser(run, j, [&](int k) {
                return (k + run.size() >= to || text.at(k + run.size()) != u'`')
                       && text.at(k - 1) != u'`';
            });
            if (close < 0) {
                i = j;
                continue;
            }
            setFormat(i, close + run.size() - i, m_formats[String]);
            i = close + int(run.size());
            continue;
        }

        // Links and images: [text](url), ![alt](src), [text][ref].
        if (ch == u'[' || (ch == u'!' && next == u'[')) {
            const int open = ch == u'[' ? i : i + 1;
            const int close = findCloser(QStringLiteral("]"), open + 1, anywhere);
            if (close >= 0 && close + 1 < to && (text.at(close + 1) == u'(' || text.at(close + 1) == u'[')) {
                const QString end = text.at(close + 1) == u'(' ? QStringLiteral(")") : QStringLiteral("]");
                const int target = findCloser(end, close + 2, anywhere);
                if (target >= 0) {
                    setFormat(i, close + 1 - i, m_formats[Function]);
                    setFormat(close + 1, target + 1 - (close + 1), m_formats[Attribute]);
                    i = target + 1;
                    continue;
                }
            }
            ++i;
            continue;
        }

        // Autolinks (<https://...>) and inline HTML tags.
        if (ch == u'<' && (next.isLetter() || next == u'/')) {
            const int close = findCloser(QStringLiteral(">"), i + 1, anywhere);
            if (close >= 0) {
                const QStringView inside = QStringView(text).mid(i + 1, close - i - 1);
                if (!inside.contains(u' ') && (inside.contains(u"://") || inside.contains(u'@'))) {
                    setFormat(i, close + 1 - i, m_formats[Attribute]);
                    i = close + 1;
                    continue;
                }
                setFormat(i, close + 1 - i, m_formats[Tag]);
                i = close + 1;
                continue;
            }
            ++i;
            continue;
        }

        // Emphasis: *em*, _em_, **strong**, __strong__, ***both***, ~~struck~~.
        if (ch == u'*' || ch == u'_' || ch == u'~') {
            int j = i;
            while (j < to && text.at(j) == ch)
                ++j;
            const int run = j - i;
            const int n = ch == u'~' ? 2 : qMin(run, 3);
            const bool intraword = ch == u'_' && i > 0 && text.at(i - 1).isLetterOrNumber();
            if ((ch == u'~' && run != 2) || intraword || j >= to || text.at(j).isSpace()) {
                i = j;
                continue;
            }
            const QString delimiter(n, ch);
            const int close = findCloser(delimiter, j, [&](int k) {
                return !text.at(k - 1).isSpace()
                       && !(ch == u'_' && k + n < to && text.at(k + n).isLetterOrNumber());
            });
            if (close < 0) {
                i = j;
                continue;
            }
            const int end = close + n;
            if (ch == u'~')
                addStyle(i, end - i, false, false, true);
            else
                addStyle(i, end - i, n >= 2, n != 2);
            setFormat(i, n, m_formats[Comment]);
            setFormat(close, n, m_formats[Comment]);
            i = end;
            continue;
        }
        ++i;
    }
}

// --- LaTeX -------------------------------------------------------------------------

void Highlighter::highlightLatex(const QString &text)
{
    static const QSet<QString> mathEnvironments{
        "equation", "equation*", "align", "align*", "alignat", "alignat*", "flalign",
        "flalign*", "gather", "gather*", "multline", "multline*", "eqnarray", "eqnarray*",
        "displaymath", "math"};
    const int length = text.size();
    int state = qMax(previousBlockState(), 0);
    if (state != InLatexMath && state != InLatexDisplayMath && state != InLatexMathEnv)
        state = Normal;
    auto inMath = [&state] { return state != Normal; };

    int i = 0;
    while (i < length) {
        const QChar ch = text.at(i);
        const QChar next = i + 1 < length ? text.at(i + 1) : QChar();

        if (ch == u'%') {
            setFormat(i, length - i, m_formats[Comment]);
            break;
        }
        if (ch == u'\\') {
            int j = i + 1;
            if (next.isLetter() || next == u'@') {
                while (j < length && (text.at(j).isLetter() || text.at(j) == u'@'))
                    ++j;
                if (j < length && text.at(j) == u'*')
                    ++j;
            } else {
                j = qMin(i + 2, length);
            }
            const QStringView command = QStringView(text).mid(i, j - i);

            if ((command == u"\\(" || command == u"\\[") && !inMath()) {
                state = command == u"\\(" ? InLatexMath : InLatexDisplayMath;
                setFormat(i, j - i, m_formats[Number]);
            } else if ((command == u"\\)" || command == u"\\]") && inMath()) {
                setFormat(i, j - i, m_formats[Number]);
                state = Normal;
            } else if (command == u"\\begin" || command == u"\\end") {
                setFormat(i, j - i, m_formats[Keyword]);
                if (j < length && text.at(j) == u'{') {
                    const int close = int(QStringView(text).mid(j, 40).indexOf(u'}'));
                    if (close > 0) {
                        const QString name = text.mid(j + 1, close - 1);
                        setFormat(j + 1, close - 1, m_formats[Type]);
                        if (mathEnvironments.contains(name)) {
                            if (command == u"\\begin" && !inMath())
                                state = InLatexMathEnv;
                            else if (command == u"\\end" && state == InLatexMathEnv)
                                state = Normal;
                        }
                        j += close + 1;
                    }
                }
            } else if ((command == u"\\verb" || command == u"\\verb*") && j < length) {
                const int close = text.indexOf(text.at(j), j + 1);
                const int end = close < 0 ? length : close + 1;
                setFormat(i, end - i, m_formats[String]);
                j = end;
            } else {
                setFormat(i, j - i, m_formats[inMath() ? Number : Keyword]);
            }
            i = j;
            continue;
        }
        if (ch == u'$') {
            const bool display = next == u'$';
            const int n = display ? 2 : 1;
            if (state == Normal)
                state = display ? InLatexDisplayMath : InLatexMath;
            else if (state != InLatexMathEnv)
                state = Normal;
            setFormat(i, n, m_formats[Number]);
            i += n;
            continue;
        }
        if (inMath()) {
            setFormat(i, 1, m_formats[Number]);
        } else if (ch == u'&' || ch == u'~') {
            setFormat(i, 1, m_formats[Tag]);
        }
        ++i;
    }
    setCurrentBlockState(state);
}
