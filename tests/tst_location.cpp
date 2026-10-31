#include "location.h"

#include <QDir>
#include <QTest>

class TestLocation : public QObject
{
    Q_OBJECT

private slots:
    void parseSftp_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("host");
        QTest::addColumn<int>("port");
        QTest::addColumn<QString>("user");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("display");

        QTest::newRow("full") << "sftp://alice@example.com:2222/var/www/index.html"
                              << "example.com" << 2222 << "alice" << "/var/www/index.html"
                              << "sftp://alice@example.com:2222/var/www/index.html";
        QTest::newRow("default port") << "sftp://bob@Example.COM:22/etc/hosts"
                                      << "example.com" << 22 << "bob" << "/etc/hosts"
                                      << "sftp://bob@example.com/etc/hosts";
        QTest::newRow("no user") << "sftp://example.com/x" << "example.com" << 22 << ""
                                 << "/x" << "sftp://example.com/x";
        QTest::newRow("no path") << "sftp://example.com" << "example.com" << 22 << "" << ""
                                 << "sftp://example.com";
        QTest::newRow("home") << "sftp://example.com/~/notes.txt" << "example.com" << 22 << ""
                              << "~/notes.txt" << "sftp://example.com/~/notes.txt";
        QTest::newRow("dots cleaned") << "sftp://h/a/./b/../c" << "h" << 22 << "" << "/a/c"
                                      << "sftp://h/a/c";
        QTest::newRow("ipv6") << "sftp://u@[::1]:2200/tmp" << "::1" << 2200 << "u" << "/tmp"
                              << "sftp://u@[::1]:2200/tmp";
        QTest::newRow("upper scheme") << "SFTP://h/x" << "h" << 22 << "" << "/x"
                                      << "sftp://h/x";
    }

    void parseSftp()
    {
        QFETCH(QString, input);
        QFETCH(QString, host);
        QFETCH(int, port);
        QFETCH(QString, user);
        QFETCH(QString, path);
        QFETCH(QString, display);

        QString error;
        const QUrl url = Location::fromUserInput(input, &error);
        QVERIFY2(url.isValid(), qPrintable(error));
        QVERIFY(Location::isRemote(url));
        const ServerAddress server = ServerAddress::fromUrl(url);
        QCOMPARE(server.host, host);
        QCOMPARE(server.port, port);
        QCOMPARE(server.user, user);
        QCOMPARE(Location::path(url), path);
        QCOMPARE(Location::displayString(url), display);
    }

    void invalidSftp_data()
    {
        QTest::addColumn<QString>("input");
        QTest::newRow("no host") << "sftp:///path";
        QTest::newRow("bad port") << "sftp://host:99999/x";
        QTest::newRow("space in host") << "sftp://ho st/x";
        QTest::newRow("empty") << "   ";
        // Shell syntax in a host or user name, which a ProxyCommand's %h or %r
        // would pass to the shell.
        QTest::newRow("semicolon host") << "sftp://a;reboot/x";
        QTest::newRow("dollar host") << "sftp://a$(id)/x";
        QTest::newRow("backtick user") << "sftp://`id`@host/x";
        QTest::newRow("option-like host") << "sftp://-oProxyCommand=x/y";
        QTest::newRow("quote user") << "sftp://a'b@host/x";
        QTest::newRow("nul in path") << QStringLiteral("/tmp/a") + QChar(0) + QStringLiteral("b");
        QTest::newRow("nul in address") << QStringLiteral("sftp://h/a") + QChar(0);
    }

    void invalidSftp()
    {
        QFETCH(QString, input);
        QString error;
        QVERIFY(!Location::fromUserInput(input, &error).isValid());
        QVERIFY(!error.isEmpty());
    }

    void passwordsAreStripped()
    {
        const QString input = QStringLiteral("sftp://alice:s3cret@example.com/home/alice/a.txt");
        const QUrl url = Location::fromUserInput(input);
        QVERIFY(url.isValid());
        QVERIFY(url.password().isEmpty());
        QVERIFY(!url.toString().contains(QLatin1String("s3cret")));
        QVERIFY(!Location::displayString(url).contains(QLatin1String("s3cret")));
        QCOMPARE(ServerAddress::fromUrl(url).user, QStringLiteral("alice"));

        QUrl withPassword(input);
        QVERIFY(!Location::stripPassword(withPassword).toString().contains(QLatin1String("s3cret")));
        QVERIFY(!Location::normalized(withPassword).toString().contains(QLatin1String("s3cret")));
        QVERIFY(!Location::displayString(withPassword).contains(QLatin1String("s3cret")));
        QVERIFY(!ServerAddress::fromUrl(withPassword).url().toString().contains(
            QLatin1String("s3cret")));

        // An invalid address in an error message mustn't leak it either.
        QString error;
        Location::fromUserInput(QStringLiteral("sftp://alice:s3cret@ho st/x"), &error);
        QVERIFY(!error.contains(QLatin1String("s3cret")));
    }

    void localPaths()
    {
        const QUrl url = Location::fromUserInput(QDir::tempPath() + QStringLiteral("/a/../b.txt"));
        QVERIFY(Location::isLocal(url));
        QCOMPARE(Location::fileName(url), QStringLiteral("b.txt"));
        QCOMPARE(Location::path(url), QDir::cleanPath(QDir::tempPath() + QStringLiteral("/b.txt")));
        QVERIFY(Location::isLocal(Location::fromUserInput(QStringLiteral("relative.txt"))));
        QVERIFY(Location::isLocal(Location::fromUserInput(QStringLiteral("file:///tmp/x"))));
    }

    void serverKey()
    {
        ServerAddress a{QStringLiteral("Example.com"), 22, QStringLiteral("alice")};
        ServerAddress b = ServerAddress::fromUrl(QUrl(QStringLiteral("sftp://alice@example.com/x")));
        QCOMPARE(a.key(), b.key());
        QCOMPARE(a.displayName(), QStringLiteral("alice@Example.com"));
        b.port = 2222;
        QVERIFY(!(a == b));
        QCOMPARE(b.displayName(), QStringLiteral("alice@example.com:2222"));
        QVERIFY(!(ServerAddress{QStringLiteral("h"), 22, QStringLiteral("a@b")}.isValid()));
        QVERIFY(!(ServerAddress{QString(), 22, QString()}.isValid()));
        QVERIFY(!(ServerAddress{QStringLiteral("h"), 0, QString()}.isValid()));
        QVERIFY(!(ServerAddress{QStringLiteral("h;id"), 22, QString()}.isValid()));
        QVERIFY(!(ServerAddress{QStringLiteral("h"), 22, QStringLiteral("a b")}.isValid()));
        QVERIFY((ServerAddress{QStringLiteral("files-1.example.org"), 22, QStringLiteral("first.last+x")}.isValid()));
        QVERIFY((ServerAddress{QStringLiteral("fe80::1"), 22, QStringLiteral("u")}.isValid()));
    }

    void remotePaths()
    {
        QCOMPARE(Location::joinRemotePath(QStringLiteral("/"), QStringLiteral("a")), QStringLiteral("/a"));
        QCOMPARE(Location::joinRemotePath(QStringLiteral("/x"), QStringLiteral("a")), QStringLiteral("/x/a"));
        QCOMPARE(Location::joinRemotePath(QStringLiteral("~"), QStringLiteral("a")), QStringLiteral("~/a"));
        QCOMPARE(Location::remoteParent(QStringLiteral("/x/a")), QStringLiteral("/x"));
        QCOMPARE(Location::remoteParent(QStringLiteral("/a")), QStringLiteral("/"));
        QCOMPARE(Location::remoteParent(QStringLiteral("~/a")), QStringLiteral("~"));
        QCOMPARE(Location::remoteFileName(QStringLiteral("/x/a.txt")), QStringLiteral("a.txt"));

        const QUrl folder(QStringLiteral("sftp://h/srv"));
        const QUrl file = Location::child(folder, QStringLiteral("f.txt"));
        QCOMPARE(file.toString(), QStringLiteral("sftp://h/srv/f.txt"));
        QVERIFY(Location::isInside(file, folder));
        QVERIFY(!Location::isInside(QUrl(QStringLiteral("sftp://h/srv2/f.txt")), folder));
        QVERIFY(!Location::isInside(QUrl(QStringLiteral("sftp://other/srv/f.txt")), folder));
        QCOMPARE(Location::parentFolder(file).toString(), folder.toString());
        QCOMPARE(Location::fileName(QUrl(QStringLiteral("sftp://h"))), QStringLiteral("h"));
    }

    void listingNames_data()
    {
        QTest::addColumn<QByteArray>("name");
        QTest::addColumn<bool>("valid");
        QTest::newRow("plain") << QByteArray("file.txt") << true;
        QTest::newRow("dotfile") << QByteArray(".bashrc") << true;
        QTest::newRow("spaces") << QByteArray("a b") << true;
        QTest::newRow("unicode") << QByteArray("caf\xc3\xa9") << true;
        QTest::newRow("dots in name") << QByteArray("...") << true;
        QTest::newRow("empty") << QByteArray() << false;
        QTest::newRow("dot") << QByteArray(".") << false;
        QTest::newRow("dotdot") << QByteArray("..") << false;
        QTest::newRow("slash") << QByteArray("a/b") << false;
        QTest::newRow("escape") << QByteArray("../../etc/passwd") << false;
        QTest::newRow("absolute") << QByteArray("/etc/passwd") << false;
        QTest::newRow("nul") << QByteArray("a\0b", 3) << false;
        QTest::newRow("bad utf8") << QByteArray("a\xff") << false;
        QTest::newRow("truncated utf8") << QByteArray("a\xc3") << false;
    }

    void listingNames()
    {
        QFETCH(QByteArray, name);
        QFETCH(bool, valid);
        QCOMPARE(Location::isValidListingName(name), valid);
    }

    void serverText()
    {
        const QString text = QStringLiteral("line1\nline2\x1b[31m\x07") + QChar(0x202e) + QStringLiteral("txt");
        const QString clean = Location::sanitizedServerText(text);
        QVERIFY(!clean.contains(QChar(0x1b)));
        QVERIFY(!clean.contains(QChar(0x07)));
        QVERIFY(!clean.contains(QChar(0x202e)));
        QVERIFY(clean.contains(QLatin1Char('\n')));
        QVERIFY(Location::sanitizedServerText(QString(10000, QLatin1Char('x')), 100).size() <= 101);
    }
};

QTEST_GUILESS_MAIN(TestLocation)
#include "tst_location.moc"
