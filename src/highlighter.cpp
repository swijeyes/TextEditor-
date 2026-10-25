#include "highlighter.h"

#include <QFileInfo>
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

    switch (language) {
    case Language::JavaScript: return javaScript;
    case Language::C: return c;
    case Language::Cpp: return cpp;
    default: return none;
    }
}

bool isIdentStart(QChar ch) { return ch.isLetter() || ch == u'_' || ch == u'$'; }
bool isIdentChar(QChar ch) { return ch.isLetterOrNumber() || ch == u'_' || ch == u'$'; }

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

Highlighter::Highlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    setTheme(Theme::dracula());
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
    }
    if (!theme.dark)
        m_formats[Keyword].setFontWeight(QFont::Bold);
    m_formats[Comment].setFontItalic(theme.dark);
    rehighlight();
}

QList<Language> Highlighter::languages()
{
    return {Language::PlainText, Language::Html, Language::Css, Language::JavaScript,
            Language::C, Language::Cpp};
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
    return Language::PlainText;
}

void Highlighter::highlightBlock(const QString &text)
{
    switch (m_language) {
    case Language::PlainText:
        return;
    case Language::Html:
        highlightHtml(text);
        return;
    default: {
        bool inComment = previousBlockState() == InBlockComment;
        highlightCode(text, 0, text.size(), m_language, inComment);
        setCurrentBlockState(inComment ? InBlockComment : Normal);
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
            const int end = text.indexOf(QLatin1String("*/"), i);
            if (end < 0 || end + 2 > to) {
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
                const int semi = text.indexOf(u';', i);
                if (semi > i + 1 && semi - i <= 10
                    && !QStringView(text).mid(i, semi - i).contains(u' ')) {
                    setFormat(i, semi + 1 - i, m_formats[Number]);
                    i = semi + 1;
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
