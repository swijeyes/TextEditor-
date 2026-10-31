#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <functional>

// Spelling, from the operating system's own spell checker: NSSpellChecker
// on macOS and the Windows Spell Checking API on Windows. Elsewhere it isn't
// available. Used from the GUI thread only.
class SpellChecker : public QObject
{
    Q_OBJECT

public:
    static SpellChecker *instance();

    bool isAvailable() const;
    // Words are checked once and remembered.
    bool isCorrect(const QString &word);
    QStringList suggestions(const QString &word);
    // Adds the word to the user's dictionary (shared with other apps).
    void learn(const QString &word);
    // Accepts the word until TextEditor+ quits.
    void ignore(const QString &word);

    // Whether a word in running text is worth checking: not code-like
    // (camelCase, snake_case, digits), an acronym, a single letter, or part
    // of an address or path.
    static bool shouldCheck(const QString &text, int start, int end);
    // The word (letters, with apostrophes inside) around position, as
    // [*start, *end). False if there's no letter there.
    static bool wordAt(const QString &text, int position, int *start, int *end);
    static bool isWordChar(QChar ch) { return ch.isLetter() || ch.isMark(); }

    // Replaces the system's checker, for tests. Pass nullptr to restore it.
    void setCheckerForTesting(std::function<bool(const QString &)> checker);

signals:
    // A word was learned or ignored, so marks may need redrawing.
    void dictionaryChanged();

private:
    SpellChecker();

    QHash<QString, bool> m_cache;
    QSet<QString> m_ignored;
    std::function<bool(const QString &)> m_testChecker;
};

// The platform part (spellchecker_mac.mm, spellchecker_win.cpp or
// spellchecker_none.cpp).
namespace SpellBackend {
bool available();
bool check(const QString &word);
QStringList suggest(const QString &word);
void learn(const QString &word);
}
