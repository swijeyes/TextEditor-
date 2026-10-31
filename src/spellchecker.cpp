#include "spellchecker.h"

namespace {

// Enough for a long document's vocabulary; past this the cache starts over.
constexpr int MaxCachedWords = 200000;
// Longer "words" are nonsense or data; they aren't checked.
constexpr int MaxWordLength = 64;

bool isApostrophe(QChar ch)
{
    return ch == u'\'' || ch == u'’';
}

} // namespace

SpellChecker *SpellChecker::instance()
{
    static SpellChecker *checker = new SpellChecker;
    return checker;
}

SpellChecker::SpellChecker() = default;

bool SpellChecker::isAvailable() const
{
    return m_testChecker || SpellBackend::available();
}

bool SpellChecker::isCorrect(const QString &word)
{
    if (!isAvailable() || m_ignored.contains(word))
        return true;
    const auto cached = m_cache.constFind(word);
    if (cached != m_cache.constEnd())
        return *cached;
    if (m_cache.size() >= MaxCachedWords)
        m_cache.clear();
    const bool correct = m_testChecker ? m_testChecker(word) : SpellBackend::check(word);
    m_cache.insert(word, correct);
    return correct;
}

QStringList SpellChecker::suggestions(const QString &word)
{
    if (m_testChecker || !SpellBackend::available())
        return {};
    return SpellBackend::suggest(word);
}

void SpellChecker::learn(const QString &word)
{
    if (!m_testChecker && SpellBackend::available())
        SpellBackend::learn(word);
    m_cache.insert(word, true);
    emit dictionaryChanged();
}

void SpellChecker::ignore(const QString &word)
{
    m_ignored.insert(word);
    emit dictionaryChanged();
}

void SpellChecker::setCheckerForTesting(std::function<bool(const QString &)> checker)
{
    m_testChecker = std::move(checker);
    m_cache.clear();
    m_ignored.clear();
}

bool SpellChecker::wordAt(const QString &text, int position, int *start, int *end)
{
    auto inWord = [&text](int i) {
        if (i < 0 || i >= text.size())
            return false;
        if (isWordChar(text.at(i)))
            return true;
        // An apostrophe joins letters (don't, it's) but doesn't start or end a word.
        return isApostrophe(text.at(i)) && i > 0 && i + 1 < text.size()
               && isWordChar(text.at(i - 1)) && isWordChar(text.at(i + 1));
    };
    if (!inWord(position)) {
        // Just after the word, as when clicking at its end.
        if (!inWord(position - 1))
            return false;
        --position;
    }
    int s = position, e = position + 1;
    while (inWord(s - 1))
        --s;
    while (inWord(e))
        ++e;
    *start = s;
    *end = e;
    return true;
}

bool SpellChecker::shouldCheck(const QString &text, int start, int end)
{
    const int length = end - start;
    if (length < 2 || length > MaxWordLength)
        return false;

    // Joined to digits or underscores, or a part of a.b names: code.
    auto codeNeighbour = [](QChar ch) { return ch.isDigit() || ch == u'_'; };
    if ((start > 0 && codeNeighbour(text.at(start - 1))) || (end < text.size() && codeNeighbour(text.at(end))))
        return false;
    if ((start > 1 && text.at(start - 1) == u'.' && text.at(start - 2).isLetterOrNumber())
        || (end + 1 < text.size() && text.at(end) == u'.' && text.at(end + 1).isLetterOrNumber()))
        return false;

    // camelCase, iPhone and ACRONYMS: only the first letter may be upper case.
    for (int i = start + 1; i < end; ++i) {
        if (text.at(i).isUpper())
            return false;
    }

    // Part of an address, e-mail address or path. Brackets and quotes end
    // one, so text between tags (<b>word</b>) or in braces still counts.
    auto ends = [](QChar ch) { return ch.isSpace() || QStringView(u"<>{}()[]\"'`").contains(ch); };
    int chunkStart = start, chunkEnd = end;
    while (chunkStart > 0 && !ends(text.at(chunkStart - 1)))
        --chunkStart;
    while (chunkEnd < text.size() && !ends(text.at(chunkEnd)))
        ++chunkEnd;
    const QStringView chunk = QStringView(text).mid(chunkStart, chunkEnd - chunkStart);
    if (chunk.contains(u"://") || chunk.contains(u'@') || chunk.contains(u'/') || chunk.contains(u'\\')
        || chunk.startsWith(u"www."))
        return false;
    return true;
}
