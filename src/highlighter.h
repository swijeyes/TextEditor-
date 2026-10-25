#pragma once

#include "theme.h"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

enum class Language { PlainText, Html, Css, JavaScript, C, Cpp };

// A small hand-written tokenizer covering the languages offered by the web
// version (HTML with embedded <script>/<style>, CSS, JavaScript, C and C++).
class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit Highlighter(QTextDocument *document);

    Language language() const { return m_language; }
    void setLanguage(Language language);
    void setTheme(const Theme &theme);

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
    };

    void highlightHtml(const QString &text);
    int highlightTagBody(const QString &text, int pos, int &state);
    void highlightCode(const QString &text, int from, int to, Language language, bool &inComment);

    Language m_language = Language::PlainText;
    QTextCharFormat m_formats[TokenCount];
};
