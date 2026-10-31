#include "textcodec.h"

#include <QTest>

class TestTextCodec : public QObject
{
    Q_OBJECT

private slots:
    void utf8()
    {
        const DecodedText d = decodeText(QByteArray("caf\xc3\xa9\nna\xc3\xafve"));
        QCOMPARE(d.text, QString::fromUtf8("café\nnaïve"));
        QVERIFY(!d.latin1);
        QVERIFY(!d.crlf);
        QVERIFY(!d.isBinary);
        QCOMPARE(encodeText(d.text, d.crlf, d.latin1), QByteArray("caf\xc3\xa9\nna\xc3\xafve"));
    }

    void latin1()
    {
        const QByteArray bytes("caf\xe9\r\n");
        const DecodedText d = decodeText(bytes);
        QVERIFY(d.latin1);
        QVERIFY(d.crlf);
        QCOMPARE(d.text, QString::fromUtf8("café\n"));
        QCOMPARE(encodeText(d.text, d.crlf, d.latin1), bytes);
    }

    void crlfRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("crlf");
        QTest::newRow("lf") << QByteArray("a\nb\nc\n") << false;
        QTest::newRow("crlf") << QByteArray("a\r\nb\r\nc") << true;
        QTest::newRow("empty") << QByteArray() << false;
        QTest::newRow("lone cr") << QByteArray("a\rb") << false;
    }

    void crlfRoundTrip()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, crlf);
        const DecodedText d = decodeText(bytes);
        QCOMPARE(d.crlf, crlf);
        QVERIFY(!d.text.contains(QLatin1String("\r\n")));
        QCOMPARE(encodeText(d.text, d.crlf, d.latin1), bytes);
    }

    void binary()
    {
        QVERIFY(decodeText(QByteArray("abc\0def", 7)).isBinary);
        QVERIFY(!decodeText(QByteArray("abcdef")).isBinary);
    }

    // Saving a Latin-1 file needs a prompt when it no longer fits Latin-1.
    void latin1SavePrompt()
    {
        QVERIFY(fitsLatin1(QString::fromUtf8("café ÿ")));
        QVERIFY(!fitsLatin1(QString::fromUtf8("price: 5€")));
        QVERIFY(!fitsLatin1(QString::fromUtf8("😀")));
        QVERIFY(fitsLatin1(QString()));
    }

    void utf8WithBadByteFallsBackToLatin1()
    {
        // A truncated UTF-8 sequence at the end isn't valid UTF-8.
        const DecodedText d = decodeText(QByteArray("ok \xc3"));
        QVERIFY(d.latin1);
        QCOMPARE(d.text, QString::fromLatin1("ok \xc3"));
    }
};

QTEST_GUILESS_MAIN(TestTextCodec)
#include "tst_textcodec.moc"
