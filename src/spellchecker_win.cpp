// Spelling on Windows: the Spell Checking API (Windows 8 and later), in the
// user's language, or US English if Windows has no dictionary for it. Words
// learned go into the user's Windows dictionary.

#include "spellchecker.h"

#include <windows.h>
#include <objbase.h>
#include <spellcheck.h>

namespace {

// Declared by spellcheck.h, but not every import library defines them.
const CLSID SpellCheckerFactoryClsid = {0x7ab36653, 0x1796, 0x484b, {0xbd, 0xfa, 0xe7, 0x4f, 0x1d, 0xb7, 0xc1, 0xdc}};
const IID SpellCheckerFactoryIid = {0x8e018a9d, 0x2415, 0x4677, {0xbf, 0x08, 0x79, 0x4e, 0xa6, 0x1f, 0x94, 0xbb}};

template<typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    ~ComPtr()
    {
        if (m_ptr)
            m_ptr->Release();
    }
    T *operator->() const { return m_ptr; }
    T **put() { return &m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

private:
    T *m_ptr = nullptr;
};

LPCWSTR wide(const QString &text)
{
    return reinterpret_cast<LPCWSTR>(text.utf16());
}

ISpellChecker *checker()
{
    static ISpellChecker *instance = [] () -> ISpellChecker * {
        // Qt has usually started COM on this thread already; this is harmless if so.
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ComPtr<ISpellCheckerFactory> factory;
        if (FAILED(CoCreateInstance(SpellCheckerFactoryClsid, nullptr, CLSCTX_INPROC_SERVER,
                                    SpellCheckerFactoryIid, reinterpret_cast<void **>(factory.put()))))
            return nullptr;
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        const wchar_t *languages[] = {locale, L"en-US"};
        if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH))
            locale[0] = 0;
        for (const wchar_t *language : languages) {
            BOOL supported = FALSE;
            if (!language[0] || FAILED(factory->IsSupported(language, &supported)) || !supported)
                continue;
            ISpellChecker *result = nullptr;
            if (SUCCEEDED(factory->CreateSpellChecker(language, &result)))
                return result;
        }
        return nullptr;
    }();
    return instance;
}

} // namespace

namespace SpellBackend {

bool available()
{
    return checker() != nullptr;
}

bool check(const QString &word)
{
    ComPtr<IEnumSpellingError> errors;
    if (FAILED(checker()->Check(wide(word), errors.put())) || !errors)
        return true;
    ComPtr<ISpellingError> error;
    return errors->Next(error.put()) != S_OK;
}

QStringList suggest(const QString &word)
{
    QStringList result;
    ComPtr<IEnumString> suggestions;
    if (FAILED(checker()->Suggest(wide(word), suggestions.put())) || !suggestions)
        return result;
    LPOLESTR suggestion = nullptr;
    ULONG fetched = 0;
    while (suggestions->Next(1, &suggestion, &fetched) == S_OK && fetched == 1) {
        result << QString::fromWCharArray(suggestion);
        CoTaskMemFree(suggestion);
    }
    return result;
}

void learn(const QString &word)
{
    checker()->Add(wide(word));
}

} // namespace SpellBackend
