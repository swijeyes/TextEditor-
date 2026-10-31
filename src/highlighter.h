#pragma once

#include "theme.h"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

enum class Language { PlainText, Html, Css, JavaScript, C, Cpp, Markdown, Python, Rust, Latex };

// A small hand-written tokenizer covering the languages offered by the web
// version (HTML with embedded <script>/<style>, CSS, JavaScript, C and C++),
// plus Markdown, Python, Rust and LaTeX.
class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit Highlighter(QTextDocument *document);

    Language language() const { return m_language; }
    void setLanguage(Language language);
    void setTheme(const Theme &theme);
    // Underlines misspelled words in prose: everywhere in plain text, the
    // text of Markdown, HTML and LaTeX, and comments and strings in code.
    void setSpellCheck(bool on);
    bool spellCheck() const { return m_spellCheck; }

    static QList<Language> languages();
    static QString languageName(Language language);
    static Language languageForFile(const QString &fileName);

protected:
    void highlightBlock(const QString &text) override;

private:
    enum Token { Keyword, Type, Function, String, Number, Comment, Preprocessor, Tag, Attribute, TokenCount };

    // Block states carried from one line to the next.
    enum State {
        Normal = 0,
        InBlockComment,   // /* ... */ in CSS, JavaScript, C, C++
        InHtmlComment,    // <!-- ... -->
        InTag,            // inside a tag that continues onto the next line
        InScriptTag,
        InStyleTag,
        InScript,         // between <script> and </script>
        InScriptComment,
        InStyle,          // between <style> and </style>
        InStyleComment,

        // Python: inside a ''' or """ string.
        InPythonTripleSingle = 20,
        InPythonTripleDouble,
        // Rust: inside a "..." string; nested block comments and raw strings
        // keep their depth or number of '#'s in the low bits.
        InRustString,
        InRustComment = 0x100,   // + depth (1-255)
        InRustRawString = 0x200, // + number of '#'s (0-255)
        // Markdown: inside <!-- -->, or a fenced code block (the fence's
        // length in the low bits, plus a flag for '~' fences).
        InMarkdownComment = 30,
        InMarkdownFence = 0x400,
        MarkdownTildeFence = 0x100,
        // LaTeX: inside $...$ or \(...\), $$...$$ or \[...\], or a math
        // environment such as equation or align.
        InLatexMath = 40,
        InLatexDisplayMath,
        InLatexMathEnv,
    };

    void highlightHtml(const QString &text);
    int highlightTagBody(const QString &text, int pos, int &state);
    void highlightCode(const QString &text, int from, int to, Language language, bool &inComment);
    void highlightPython(const QString &text);
    void highlightRust(const QString &text);
    void highlightMarkdown(const QString &text);
    void highlightMarkdownInline(const QString &text, int from, int to);
    void highlightLatex(const QString &text);
    // Adds bold, italic or strike-through to what's already there.
    void addStyle(int start, int count, bool bold, bool italic, bool strikeOut = false);
    void markMisspellings(const QString &text);
    bool isProse(int position) const;

    Language m_language = Language::PlainText;
    QTextCharFormat m_formats[TokenCount];
    QColor m_misspelledColor;
    bool m_spellCheck = false;
};
