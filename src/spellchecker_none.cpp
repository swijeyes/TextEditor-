// No system spell checker on this platform (Linux and others).

#include "spellchecker.h"

namespace SpellBackend {

bool available()
{
    return false;
}

bool check(const QString &)
{
    return true;
}

QStringList suggest(const QString &)
{
    return {};
}

void learn(const QString &)
{
}

} // namespace SpellBackend
