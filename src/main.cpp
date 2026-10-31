#include "classicstyle.h"
#include "location.h"
#include "mainwindow.h"
#include "messagebox.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Classic scroll bars on the platform's own style.
    QApplication::setStyle(new ClassicStyle);
    QApplication::setApplicationName(QStringLiteral("TextEditor+"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setOrganizationName(QStringLiteral("sjwrc"));
    QApplication::setOrganizationDomain(QStringLiteral("sjwrc.com"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));
#ifdef Q_OS_MACOS
    // Closing the window leaves the app running in the Dock.
    QApplication::setQuitOnLastWindowClosed(false);
#endif

    // Portable mode: with a "settings" folder next to the program, settings
    // are kept there in an INI file instead of the registry or user profile,
    // so the app can run from a USB stick without leaving anything behind.
    const QString portableDir = QCoreApplication::applicationDirPath() + QStringLiteral("/settings");
    if (QFileInfo(portableDir).isDir()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, portableDir);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A simple code and text editor."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("files"),
        QStringLiteral("Files or folders to open: local paths, or sftp://user@host:port/path addresses."),
        QStringLiteral("[files...]"));
    parser.process(app);

    MainWindow window;
    window.show();
    for (const QString &argument : parser.positionalArguments()) {
        QString error;
        const QUrl url = Location::fromUserInput(argument, &error);
        if (url.isValid())
            window.openLocation(url);
        else
            showMessage(&window, QMessageBox::Warning, QStringLiteral("TextEditor+"), error);
    }

    return app.exec();
}
