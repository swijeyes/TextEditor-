// CSV/TSV parsing and delimiter detection.

#include "csvdata.h"

#include <QTest>

using namespace CsvData;

class TestCsvData : public QObject
{
    Q_OBJECT

private slots:
    void detect_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("expected");

        QTest::newRow("csv by name") << "a\tb\tc" << "data.csv" << int(Delimiter::Comma);
        QTest::newRow("tsv by name") << "a,b,c" << "data.tsv" << int(Delimiter::Tab);
        QTest::newRow("tab by name") << "a,b,c" << "data.tab" << int(Delimiter::Tab);
        QTest::newRow("sniff comma") << "a,b,c\n1,2,3" << "data.txt" << int(Delimiter::Comma);
        QTest::newRow("sniff tab") << "a\tb\tc\n1\t2\t3" << "data.txt" << int(Delimiter::Tab);
        QTest::newRow("sniff semicolon") << "a;b;c\n1;2;3" << "data.txt" << int(Delimiter::Semicolon);
        QTest::newRow("quoted commas ignored")
            << "name\tnote\nJo\t\"a, b, c\"" << "x.txt" << int(Delimiter::Tab);
    }

    void detect()
    {
        QFETCH(QString, text);
        QFETCH(QString, name);
        QFETCH(int, expected);
        QCOMPARE(int(detectDelimiter(text, name)), expected);
    }

    void parseSimple()
    {
        const auto rows = parse(QStringLiteral("a,b,c\n1,2,3\n"), QLatin1Char(','));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0], (QStringList{"a", "b", "c"}));
        QCOMPARE(rows[1], (QStringList{"1", "2", "3"}));
    }

    void parseQuoting()
    {
        const auto rows = parse(
            QStringLiteral("\"hello, world\",\"she said \"\"hi\"\"\",plain\n"), QLatin1Char(','));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].size(), 3);
        QCOMPARE(rows[0][0], QStringLiteral("hello, world"));
        QCOMPARE(rows[0][1], QStringLiteral("she said \"hi\""));
        QCOMPARE(rows[0][2], QStringLiteral("plain"));
    }

    void parseEmbeddedNewline()
    {
        const auto rows = parse(QStringLiteral("\"line1\nline2\",b\nx,y\n"), QLatin1Char(','));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0][0], QStringLiteral("line1\nline2"));
        QCOMPARE(rows[0][1], QStringLiteral("b"));
        QCOMPARE(rows[1], (QStringList{"x", "y"}));
    }

    void parseCrlfAndTrailing()
    {
        const auto rows = parse(QStringLiteral("a,b\r\n1,2"), QLatin1Char(','));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[1], (QStringList{"1", "2"}));
    }

    void parseEmptyFields()
    {
        const auto rows = parse(QStringLiteral("a,,c\n,,\n"), QLatin1Char(','));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0], (QStringList{"a", "", "c"}));
        QCOMPARE(rows[1], (QStringList{"", "", ""}));
    }

    void parseTabs()
    {
        const auto rows = parse(QStringLiteral("a\tb\tc\n1\t2\t3\n"), QLatin1Char('\t'));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0], (QStringList{"a", "b", "c"}));
    }

    void tabularNames()
    {
        QVERIFY(isTabularFileName(QStringLiteral("x.csv")));
        QVERIFY(isTabularFileName(QStringLiteral("/a/b/data.TSV")));
        QVERIFY(isTabularFileName(QStringLiteral("x.tab")));
        QVERIFY(!isTabularFileName(QStringLiteral("x.txt")));
        QVERIFY(!isTabularFileName(QStringLiteral("x.md")));
    }
};

QTEST_APPLESS_MAIN(TestCsvData)
#include "tst_csvdata.moc"
