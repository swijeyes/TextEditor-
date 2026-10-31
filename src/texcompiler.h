#pragma once

#include <QList>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <memory>

class QProcess;
class QTemporaryDir;
class QTimer;

// Typesets a LaTeX document with a TeX engine installed on the computer
// (pdfLaTeX, XeLaTeX, LuaLaTeX or Tectonic), then turns the PDF's pages into
// PNG images with a PDF renderer (pdftoppm or pdftocairo from Poppler, or
// MuPDF's mutool), for the TeX preview's Typeset mode.
//
// The text is typeset from a private temporary folder, so unsaved changes
// can be shown and nothing is written next to the file. The file's own
// folder is added to the search path so \input and \includegraphics find
// their files.
//
// A document being previewed may come from anyone, and typesetting starts
// as soon as it is opened, so TeX is treated as hostile:
//  - Shell escape is always off.
//  - The engine and the PDF renderer run in an operating-system sandbox
//    (sandbox-exec on macOS, bubblewrap on Linux) that lets them write only
//    to the temporary folder and TeX's own caches, with no network access
//    (except Tectonic, which downloads packages). Tectonic and LuaLaTeX can
//    write anywhere a document asks (TeX's openout_any setting doesn't apply
//    to them), so they're only offered where there is a sandbox.
//  - Without a sandbox (Windows), pdfLaTeX and XeLaTeX rely on
//    openout_any=p, which keeps their output in the temporary folder.
//  - Runs that take too long, or fill the folder with more than
//    MaxFolderBytes, are stopped, and rendered pages are size-limited.
class TexCompiler : public QObject
{
    Q_OBJECT

public:
    enum class Engine { PdfLatex, XeLatex, LuaLatex, Tectonic };

    struct Tool
    {
        Engine engine;
        QString path;
    };

    enum class Sandbox { None, MacOS, Bubblewrap };

    // Engines found on this computer that can be run safely here, in order
    // of preference.
    static QList<Tool> availableEngines();
    // The sandbox used for typesetting, and the path of its program.
    static Sandbox sandbox();
    static QString sandboxProgram();
    // Engines whose file writes TeX itself can't confine.
    static bool needsSandbox(Engine engine);
    // The PDF renderer found, or an empty string.
    static QString rasterizer();
    static QString engineName(Engine engine);

    explicit TexCompiler(QObject *parent = nullptr);
    ~TexCompiler() override;

    bool isRunning() const;
    // Starts typesetting 'source'. 'sourceFolder' is the file's folder, or
    // empty for files on servers and untitled files. Pages are rendered at
    // 'dpi' (up to MaxPages of them).
    void compile(const QString &source, const QString &sourceFolder, const Tool &engine, int dpi);
    void cancel();

    static constexpr int MaxPages = 40;
    static constexpr qint64 MaxFolderBytes = 256 * 1024 * 1024;
    // Rendered pages are at most this many pixels on their long side.
    static constexpr int MaxPagePixels = 4000;

signals:
    // The pages as PNG files in order. 'warning' describes the first TeX
    // error, if the document had errors but a PDF was still made.
    void finished(const QStringList &pages, const QString &pdfPath, const QString &warning);
    void failed(const QString &message);

private:
    void start(const QString &program, const QStringList &arguments, bool network);
    void checkFolderSize();
    void startEngine();
    void engineFinished();
    void startRendering();
    void renderingFinished();
    void fail(const QString &message);
    QString firstError(const QString &output) const;
    QString path(const QString &name) const;

    std::unique_ptr<QTemporaryDir> m_dir;
    QProcess *m_process = nullptr;
    QTimer *m_timeout;
    QTimer *m_sizeCheck;
    Tool m_engine{Engine::PdfLatex, {}};
    QString m_sourceFolder;
    QProcessEnvironment m_extraEnvironment; // for the next process started
    int m_dpi = 100;
    int m_passes = 0;
    QString m_warning;
};
