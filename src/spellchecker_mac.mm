// Spelling on macOS: the system's NSSpellChecker, in the language(s) chosen
// in System Settings > Keyboard > Text Input (automatic by default).

#include "spellchecker.h"

#import <AppKit/AppKit.h>

namespace SpellBackend {

bool available()
{
    return true;
}

bool check(const QString &word)
{
    @autoreleasepool {
        NSString *string = word.toNSString();
        const NSRange range = [[NSSpellChecker sharedSpellChecker] checkSpellingOfString:string
                                                                             startingAt:0];
        return range.location == NSNotFound;
    }
}

QStringList suggest(const QString &word)
{
    QStringList result;
    @autoreleasepool {
        NSString *string = word.toNSString();
        NSSpellChecker *checker = [NSSpellChecker sharedSpellChecker];
        NSArray<NSString *> *guesses = [checker guessesForWordRange:NSMakeRange(0, string.length)
                                                           inString:string
                                                           language:nil
                                             inSpellDocumentWithTag:0];
        for (NSString *guess in guesses)
            result << QString::fromNSString(guess);
    }
    return result;
}

void learn(const QString &word)
{
    @autoreleasepool {
        [[NSSpellChecker sharedSpellChecker] learnWord:word.toNSString()];
    }
}

} // namespace SpellBackend
