// The syntax highlighters added in 2.5 (Markdown, Python, Rust, LaTeX):
// colours of typical code, state carried across lines, and speed on
// hostile input.

#include "highlighter.h"
#include "spellchecker.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace {

// The format the highlighter gave the character at (line, column).
QTextCharFormat formatAt(const QTextDocument &document, int line, int column)
{
    const QTextBlock block = document.findBlockByNumber(line);
    for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
        if (column >= range.start && column < range.start + range.length)
            return range.format;
    }
    return {};
}

QColor colorAt(const QTextDocument &document, int line, int column)
{
    const QTextCharFormat format = formatAt(document, line, column);
    return format.hasProperty(QTextFormat::ForegroundBrush) ? format.foreground().color() : QColor();
}

struct Highlighted
{
    explicit Highlighted(const QString &text, Language language)
        : highlighter(&document)
    {
        document.setPlainText(text);
        highlighter.setTheme(Theme::dracula());
        highlighter.setLanguage(language);
    }
    QColor at(int line, int column) const { return colorAt(document, line, column); }

    QTextDocument document;
    Highlighter highlighter;
};

const Theme theme = Theme::dracula();

} // namespace

class TestHighlighter : public QObject
{
    Q_OBJECT

private slots:
    void languageForFile_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<Language>("language");
        QTest::newRow("md") << "README.md" << Language::Markdown;
        QTest::newRow("markdown") << "notes.MARKDOWN" << Language::Markdown;
        QTest::newRow("py") << "script.py" << Language::Python;
        QTest::newRow("pyi") << "stubs.pyi" << Language::Python;
        QTest::newRow("rs") << "main.rs" << Language::Rust;
        QTest::newRow("tex") << "paper.tex" << Language::Latex;
        QTest::newRow("sty") << "style.sty" << Language::Latex;
        QTest::newRow("txt") << "notes.txt" << Language::PlainText;
        QTest::newRow("cpp still") << "a.cpp" << Language::Cpp;
    }

    void languageForFile()
    {
        QFETCH(QString, name);
        QFETCH(Language, language);
        QCOMPARE(Highlighter::languageForFile(name), language);
    }

    void python()
    {
        const Highlighted h(QStringLiteral("@dataclass\n"
                                           "def area(self, r=2.5):  # comment\n"
                                           "    s = \"\"\"multi\n"
                                           "line with def\"\"\" + f'{x}'\n"
                                           "    return None\n"
                                           "class Shape(object): pass\n"),
                            Language::Python);
        QCOMPARE(h.at(0, 0), theme.preprocessor);  // @dataclass
        QCOMPARE(h.at(1, 0), theme.keyword);       // def
        QCOMPARE(h.at(1, 4), theme.function);      // area
        QCOMPARE(h.at(1, 9), theme.number);        // self
        QCOMPARE(h.at(1, 17), theme.number);       // 2.5
        QCOMPARE(h.at(1, 24), theme.comment);      // # comment
        QCOMPARE(h.at(2, 8), theme.string);        // """multi
        QCOMPARE(h.at(3, 10), theme.string);       // def inside the string
        QCOMPARE(h.at(3, 21), theme.string);       // f'{x}'
        QCOMPARE(h.at(4, 4), theme.keyword);       // return
        QCOMPARE(h.at(4, 11), theme.number);       // None
        QCOMPARE(h.at(5, 6), theme.type);          // Shape
        QCOMPARE(h.at(5, 12), theme.type);         // object
    }

    void rust()
    {
        const Highlighted h(QStringLiteral("#[derive(Debug)]\n"
                                           "/* outer /* inner */ still comment */ fn main<'a>() {\n"
                                           "    let s: &'a str = r#\"raw \"quoted\"\n"
                                           "text\"#; let c = 'x';\n"
                                           "    println!(\"{}\", 42u32); // done\n"
                                           "}\n"),
                            Language::Rust);
        QCOMPARE(h.at(0, 0), theme.preprocessor);  // #[derive(Debug)]
        QCOMPARE(h.at(1, 21), theme.comment);      // \"still\" after the inner comment closed
        QCOMPARE(h.at(1, 38), theme.keyword);      // fn
        QCOMPARE(h.at(1, 41), theme.function);     // main
        QCOMPARE(h.at(1, 46), theme.attribute);    // 'a
        QCOMPARE(h.at(2, 4), theme.keyword);       // let
        QCOMPARE(h.at(2, 12), theme.attribute);         // 'a
        QCOMPARE(h.at(2, 15), theme.type);         // str
        QCOMPARE(h.at(2, 29), theme.string);       // "quoted" inside the raw string
        QCOMPARE(h.at(3, 0), theme.string);        // the raw string's second line
        QCOMPARE(h.at(3, 16), theme.string);       // 'x'
        QCOMPARE(h.at(4, 4), theme.function);      // println!
        QCOMPARE(h.at(4, 19), theme.number);       // 42u32
        QCOMPARE(h.at(4, 27), theme.comment);      // // done
    }

    void markdown()
    {
        const Highlighted h(QStringLiteral("# Title\n"
                                           "Some **bold** and *em* and `code` and [link](http://x).\n"
                                           "```python\n"
                                           "def f(): pass\n"
                                           "```\n"
                                           "- item\n"
                                           "> quote\n"
                                           "2*3*4 snake_case_name\n"),
                            Language::Markdown);
        QCOMPARE(h.at(0, 2), theme.keyword);
        QCOMPARE(formatAt(h.document, 0, 2).fontWeight(), int(QFont::Bold));
        QCOMPARE(formatAt(h.document, 1, 7).fontWeight(), int(QFont::Bold));   // bold
        QVERIFY(formatAt(h.document, 1, 19).fontItalic());                     // em
        QCOMPARE(h.at(1, 28), theme.string);                                    // `code`
        QCOMPARE(h.at(1, 39), theme.function);                                  // [link]
        QCOMPARE(h.at(1, 44), theme.attribute);                                 // (http://x)
        QCOMPARE(h.at(2, 0), theme.preprocessor);                               // ``` fence
        QCOMPARE(h.at(2, 3), theme.attribute);                                  // python
        QCOMPARE(h.at(3, 0), theme.string);                                     // code inside
        QCOMPARE(h.at(4, 0), theme.preprocessor);                               // closing fence
        QCOMPARE(h.at(5, 0), theme.tag);                                        // - marker
        QCOMPARE(h.at(6, 0), theme.tag);                                        // > marker
        // Underscores inside a word never start emphasis.
        QVERIFY(!formatAt(h.document, 7, 12).fontItalic());
    }

    void latex()
    {
        const Highlighted h(QStringLiteral("\\section{Intro} % note\n"
                                           "Inline $x^2$ and \\verb|a%b| text.\n"
                                           "\\begin{equation}\n"
                                           "E = mc^2\n"
                                           "\\end{equation}\n"
                                           "after\n"),
                            Language::Latex);
        QCOMPARE(h.at(0, 0), theme.keyword);      // \section
        QCOMPARE(h.at(0, 16), theme.comment);     // % note
        QCOMPARE(h.at(1, 8), theme.number);       // x in $x^2$
        QCOMPARE(h.at(1, 22), theme.string);      // \verb|a%b| (not a comment)
        QCOMPARE(h.at(1, 23), theme.string);
        QCOMPARE(h.at(1, 28), QColor());          // "text" after it
        QCOMPARE(h.at(2, 7), theme.type);         // equation
        QCOMPARE(h.at(3, 0), theme.number);       // maths on its own line
        QCOMPARE(h.at(5, 0), QColor());           // back to text
    }

    // Long lines of unmatched delimiters must not take quadratic time (the
    // fuzzers run with a timeout for the same reason).
    void hostileLinesAreFast()
    {
        const QString line = QString(100000, u'*') + QString(50000, u'[') + QString(50000, u'`')
                             + QString(50000, u'_') + QStringLiteral(" a") + QString(50000, u']');
        QElapsedTimer timer;
        timer.start();
        for (Language language : {Language::Markdown, Language::Python, Language::Rust, Language::Latex}) {
            Highlighted h(line, language);
            Q_UNUSED(h);
        }
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
    }

    // Misspellings are underlined in prose only: comments and strings in
    // code, text in Markdown, HTML and LaTeX, all of plain text.
    void spellCheck_data()
    {
        QTest::addColumn<Language>("language");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QList<int>>("underlined"); // first columns of underlined words
        QTest::addColumn<QList<int>>("plain");      // first columns of words left alone

        QTest::newRow("plain text") << Language::PlainText << QStringLiteral("teh cat")
                                    << QList<int>{0} << QList<int>{4};
        QTest::newRow("c++") << Language::Cpp << QStringLiteral("int teh = f(\"teh\"); // teh")
                             << QList<int>{14, 24} << QList<int>{4};
        QTest::newRow("python") << Language::Python << QStringLiteral("teh = 'teh'  # teh")
                                << QList<int>{7, 15} << QList<int>{0};
        QTest::newRow("markdown") << Language::Markdown << QStringLiteral("Teh `teh` [teh](teh.md)")
                                  << QList<int>{0, 11} << QList<int>{5, 16};
        QTest::newRow("markdown heading") << Language::Markdown << QStringLiteral("## Teh")
                                          << QList<int>{3} << QList<int>{};
        QTest::newRow("html") << Language::Html << QStringLiteral("<teh class=\"teh\">teh</teh>")
                              << QList<int>{17} << QList<int>{1, 12, 22};
        QTest::newRow("latex") << Language::Latex << QStringLiteral("\\teh{teh} $teh$ % teh")
                               << QList<int>{5, 18} << QList<int>{1, 11};
        QTest::newRow("code-like words") << Language::PlainText
                                         << QStringLiteral("tehTeh teh_1 TEH teh2 x.teh http://teh.org teh/teh don't")
                                         << QList<int>{} << QList<int>{0, 7, 13, 17, 24, 28, 43, 51};
        QTest::newRow("apostrophe") << Language::PlainText << QStringLiteral("can't teh's")
                                    << QList<int>{6} << QList<int>{0};
    }

    void spellCheck()
    {
        QFETCH(Language, language);
        QFETCH(QString, text);
        QFETCH(QList<int>, underlined);
        QFETCH(QList<int>, plain);
        SpellChecker::instance()->setCheckerForTesting([](const QString &word) {
            return !word.startsWith(QLatin1String("teh"), Qt::CaseInsensitive)
                   && word != QLatin1String("TEH");
        });
        Highlighted h(text, language);
        h.highlighter.setSpellCheck(true);
        auto isUnderlined = [&h](int column) {
            return formatAt(h.document, 0, column).underlineStyle() == QTextCharFormat::SpellCheckUnderline;
        };
        for (int column : underlined)
            QVERIFY2(isUnderlined(column), qPrintable(QString::number(column)));
        for (int column : plain)
            QVERIFY2(!isUnderlined(column), qPrintable(QString::number(column)));

        // Syntax colours stay under the underline.
        if (language == Language::Cpp)
            QCOMPARE(h.at(0, 24), theme.comment);

        h.highlighter.setSpellCheck(false);
        for (int column : underlined)
            QVERIFY(!isUnderlined(column));
        SpellChecker::instance()->setCheckerForTesting(nullptr);
    }

    void spellCheckLearnAndIgnore()
    {
        SpellChecker *checker = SpellChecker::instance();
        checker->setCheckerForTesting([](const QString &word) { return word != QLatin1String("teh")
                                                                     && word != QLatin1String("hte"); });
        Highlighted h(QStringLiteral("teh hte"), Language::PlainText);
        h.highlighter.setSpellCheck(true);
        auto isUnderlined = [&h](int column) {
            return formatAt(h.document, 0, column).underlineStyle() == QTextCharFormat::SpellCheckUnderline;
        };
        QVERIFY(isUnderlined(0));
        QVERIFY(isUnderlined(4));
        checker->ignore(QStringLiteral("teh"));
        QVERIFY(!isUnderlined(0));
        checker->learn(QStringLiteral("hte"));
        QVERIFY(!isUnderlined(4));
        checker->setCheckerForTesting(nullptr);
    }

    void wordAt()
    {
        const QString text = QStringLiteral("don't 'quoted' x");
        int start = -1, end = -1;
        QVERIFY(SpellChecker::wordAt(text, 2, &start, &end));
        QCOMPARE(text.mid(start, end - start), QStringLiteral("don't"));
        QVERIFY(SpellChecker::wordAt(text, 5, &start, &end)); // just after the word
        QCOMPARE(text.mid(start, end - start), QStringLiteral("don't"));
        QVERIFY(SpellChecker::wordAt(text, 7, &start, &end));
        QCOMPARE(text.mid(start, end - start), QStringLiteral("quoted"));
        QVERIFY(!SpellChecker::wordAt(text, 6, &start, &end));
    }
};

QTEST_MAIN(TestHighlighter)
#include "tst_highlighter.moc"
