// Typesetting with a real TeX engine, when one is installed (skipped
// otherwise): a normal document gives pages, and a hostile one can't run
// commands or write files outside the temporary folder.

#include "texcompiler.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestTexCompiler : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (TexCompiler::availableEngines().isEmpty())
            QSKIP("No TeX engine is installed.");
    }

    void typesetsPages()
    {
        if (TexCompiler::rasterizer().isEmpty())
            QSKIP("No PDF renderer is installed.");
        TexCompiler compiler;
        QSignalSpy finished(&compiler, &TexCompiler::finished);
        QSignalSpy failed(&compiler, &TexCompiler::failed);
        compiler.compile(QStringLiteral("\\documentclass{article}\\begin{document}Hello\\newpage World\\end{document}"),
                         QString(), TexCompiler::availableEngines().first(), 60);
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || !failed.isEmpty(), 300000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QCOMPARE(finished.first().first().toStringList().size(), 2);
    }

    // Each engine found is tried with a document that attempts to run a
    // command and to write outside its folder.
    void hostileDocumentIsConfined()
    {
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const QString target = QDir(outside.path()).canonicalPath();
        for (const TexCompiler::Tool &tool : TexCompiler::availableEngines()) {
            const QString absolute = target + QStringLiteral("/absolute.tex");
            const QString shell = target + QStringLiteral("/shell");
            const QString source = QStringLiteral(
                "\\documentclass{article}\\begin{document}"
                "\\immediate\\write18{touch %1}"
                "\\newwrite\\f\\immediate\\openout\\f=%2\\immediate\\write\\f{x}\\immediate\\closeout\\f "
                "\\immediate\\openout\\f=../../parent.tex\\immediate\\write\\f{x}\\immediate\\closeout\\f "
                "Text.\\end{document}").arg(shell, absolute);
            TexCompiler compiler;
            QSignalSpy finished(&compiler, &TexCompiler::finished);
            QSignalSpy failed(&compiler, &TexCompiler::failed);
            compiler.compile(source, QString(), tool, 60);
            QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || !failed.isEmpty(), 300000);
            const QString name = TexCompiler::engineName(tool.engine);
            QVERIFY2(!QFile::exists(shell), qPrintable(name));
            QVERIFY2(!QFile::exists(absolute), qPrintable(name));
            QVERIFY2(!QFile::exists(QDir::tempPath() + QStringLiteral("/parent.tex")), qPrintable(name));
        }
    }
};

QTEST_GUILESS_MAIN(TestTexCompiler)
#include "tst_texcompiler.moc"
