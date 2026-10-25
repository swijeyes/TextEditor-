#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TextEditor+"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setOrganizationName(QStringLiteral("sjwrc"));
    QApplication::setOrganizationDomain(QStringLiteral("sjwrc.com"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A simple code and text editor."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("File to open."));
    parser.process(app);

    MainWindow window;
    if (!parser.positionalArguments().isEmpty())
        window.loadFile(parser.positionalArguments().constFirst());
    window.show();

    return app.exec();
}
