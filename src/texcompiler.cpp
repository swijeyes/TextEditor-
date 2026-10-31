#include "texcompiler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif

namespace {

constexpr int EngineTimeoutMs = 90 * 1000;
// Tectonic downloads the packages a document needs on first use.
constexpr int TectonicTimeoutMs = 300 * 1000;
constexpr int RenderTimeoutMs = 60 * 1000;
constexpr int MaxPasses = 2;

// Where TeX distributions and PDF tools are installed when they aren't on
// the PATH, which is short for apps started from the Finder or Dock.
QStringList extraSearchFolders()
{
    QStringList folders;
#if defined(Q_OS_MACOS)
    folders << QStringLiteral("/Library/TeX/texbin") << QStringLiteral("/opt/homebrew/bin")
            << QStringLiteral("/usr/local/bin") << QStringLiteral("/opt/local/bin")
            << QStringLiteral("/usr/texbin");
#elif defined(Q_OS_WIN)
    const QString localPrograms = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"))
                                  + QStringLiteral("/Programs");
    folders << QStringLiteral("C:/Program Files/MiKTeX/miktex/bin/x64")
            << localPrograms + QStringLiteral("/MiKTeX/miktex/bin/x64");
    const QDir texLive(QStringLiteral("C:/texlive"));
    for (const QString &year : texLive.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed))
        folders << texLive.filePath(year + QStringLiteral("/bin/windows"))
                << texLive.filePath(year + QStringLiteral("/bin/win64"));
#else
    folders << QStringLiteral("/usr/local/bin") << QStringLiteral("/usr/bin");
    const QDir texLive(QStringLiteral("/usr/local/texlive"));
    for (const QString &year : texLive.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
        const QDir bin(texLive.filePath(year + QStringLiteral("/bin")));
        for (const QString &arch : bin.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            folders << bin.filePath(arch);
    }
#endif
    return folders;
}

// TEXINPUTS is expanded by kpathsea: '$' (variables), '!!', braces and the
// list separators all mean something, and can't be escaped. A folder whose
// path contains them isn't added (its \input files then won't be found).
bool safeForTexInputs(const QString &folder)
{
    static const QRegularExpression special(QStringLiteral("[$!{}:;,\\n\\r]"));
    return !folder.contains(special) && !folder.contains(QLatin1String("//"));
}

qint64 folderSize(const QString &path, int depth = 0)
{
    qint64 total = 0;
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir())
            total += depth < 8 ? folderSize(entry.filePath(), depth + 1) : 0;
        else
            total += entry.size();
    }
    return total;
}

// Folders TeX may write to besides the temporary folder: font and format
// caches, and Tectonic's package cache.
QStringList cacheFolders()
{
    const QString home = QDir(QDir::homePath()).canonicalPath();
    if (home.isEmpty())
        return {};
    QStringList folders;
#if defined(Q_OS_MACOS)
    folders << home + QStringLiteral("/Library/Caches/TectonicProject.Tectonic")
            << home + QStringLiteral("/Library/texlive");
#else
    const QString xdgCache = qEnvironmentVariable("XDG_CACHE_HOME");
    folders << (xdgCache.isEmpty() ? home + QStringLiteral("/.cache") : xdgCache) + QStringLiteral("/Tectonic");
#endif
    const QString tectonicCache = qEnvironmentVariable("TECTONIC_CACHE_DIR");
    if (QDir::isAbsolutePath(tectonicCache))
        folders << QDir::cleanPath(tectonicCache);
    const QDir homeDir(home);
    for (const QString &name : homeDir.entryList({QStringLiteral(".texlive*"), QStringLiteral(".texmf-var")},
                                                 QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
        folders << homeDir.filePath(name);
    return folders;
}

// The macOS sandbox profile: anything may be read, but files may only be
// written in the work folder and the caches, and (unless allowed) there is
// no network.
QString macSandboxProfile(int cacheCount, bool network)
{
    QString profile = QStringLiteral("(version 1)\n(allow default)\n(deny file-write*)\n"
                                     "(allow file-write* (subpath (param \"WORK\"))");
    for (int i = 0; i < cacheCount; ++i)
        profile += QStringLiteral(" (subpath (param \"CACHE%1\"))").arg(i);
    profile += QStringLiteral(" (literal \"/dev/null\") (literal \"/dev/zero\")"
                              " (literal \"/dev/dtracehelper\") (regex #\"^/dev/tty\"))\n");
    if (!network)
        profile += QStringLiteral("(deny network*)\n");
    return profile;
}

QString findTool(const QString &name)
{
    const QString onPath = QStandardPaths::findExecutable(name);
    return onPath.isEmpty() ? QStandardPaths::findExecutable(name, extraSearchFolders()) : onPath;
}

int pageNumber(const QString &fileName)
{
    static const QRegularExpression number(QStringLiteral("(\\d+)\\.png$"));
    return number.match(fileName).captured(1).toInt();
}

} // namespace

QList<TexCompiler::Tool> TexCompiler::availableEngines()
{
    static const QList<Tool> engines = [] {
        QList<Tool> found;
        const QList<QPair<Engine, QString>> candidates{{Engine::PdfLatex, QStringLiteral("pdflatex")},
                                                       {Engine::XeLatex, QStringLiteral("xelatex")},
                                                       {Engine::LuaLatex, QStringLiteral("lualatex")},
                                                       {Engine::Tectonic, QStringLiteral("tectonic")}};
        for (const auto &[engine, name] : candidates) {
            if (needsSandbox(engine) && sandbox() == Sandbox::None)
                continue;
            const QString path = findTool(name);
            if (!path.isEmpty())
                found.append({engine, path});
        }
        return found;
    }();
    return engines;
}

TexCompiler::Sandbox TexCompiler::sandbox()
{
#if defined(Q_OS_MACOS)
    return sandboxProgram().isEmpty() ? Sandbox::None : Sandbox::MacOS;
#elif defined(Q_OS_LINUX)
    return sandboxProgram().isEmpty() ? Sandbox::None : Sandbox::Bubblewrap;
#else
    return Sandbox::None;
#endif
}

QString TexCompiler::sandboxProgram()
{
#if defined(Q_OS_MACOS)
    static const QString path = QFileInfo::exists(QStringLiteral("/usr/bin/sandbox-exec"))
        ? QStringLiteral("/usr/bin/sandbox-exec")
        : QString();
#elif defined(Q_OS_LINUX)
    static const QString path = findTool(QStringLiteral("bwrap"));
#else
    static const QString path;
#endif
    return path;
}

bool TexCompiler::needsSandbox(Engine engine)
{
    return engine == Engine::Tectonic || engine == Engine::LuaLatex;
}

QString TexCompiler::rasterizer()
{
    static const QString path = [] {
        for (const QString &name : {QStringLiteral("pdftoppm"), QStringLiteral("pdftocairo"),
                                    QStringLiteral("mutool")}) {
            const QString found = findTool(name);
            if (!found.isEmpty())
                return found;
        }
        return QString();
    }();
    return path;
}

QString TexCompiler::engineName(Engine engine)
{
    switch (engine) {
    case Engine::PdfLatex: return tr("pdfLaTeX");
    case Engine::XeLatex: return tr("XeLaTeX");
    case Engine::LuaLatex: return tr("LuaLaTeX");
    case Engine::Tectonic: return tr("Tectonic");
    }
    return {};
}

TexCompiler::TexCompiler(QObject *parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
    , m_sizeCheck(new QTimer(this))
{
    m_sizeCheck->setInterval(500);
    connect(m_sizeCheck, &QTimer::timeout, this, &TexCompiler::checkFolderSize);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        cancel();
        fail(tr("Typesetting took too long and was stopped."));
    });
}

TexCompiler::~TexCompiler()
{
    cancel();
}

bool TexCompiler::isRunning() const
{
    return m_process != nullptr;
}

QString TexCompiler::path(const QString &name) const
{
    return m_dir->filePath(name);
}

void TexCompiler::compile(const QString &source, const QString &sourceFolder, const Tool &engine, int dpi)
{
    cancel();
    // The folder is kept between runs, so the .aux file from the last run
    // resolves references straight away.
    if (!m_dir || !m_dir->isValid()) {
        m_dir = std::make_unique<QTemporaryDir>();
        if (!m_dir->isValid()) {
            fail(tr("Couldn't create a temporary folder for typesetting."));
            return;
        }
    }
    QFile file(path(QStringLiteral("preview.tex")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(source.toUtf8()) < 0) {
        fail(tr("Couldn't write the temporary file for typesetting."));
        return;
    }
    file.close();
    // Never show an old PDF as if it were the new one.
    QFile::remove(path(QStringLiteral("preview.pdf")));

    m_engine = engine;
    m_sourceFolder = sourceFolder;
    m_dpi = qBound(30, dpi, 600);
    m_passes = 0;
    m_warning.clear();
    startEngine();
}

// Starts 'program' in the work folder, inside the sandbox if there is one.
void TexCompiler::start(const QString &program, const QStringList &arguments, bool network)
{
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_dir->path());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setStandardInputFile(QProcess::nullDevice());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Temporary files go in the work folder too (the sandbox allows nothing
    // else).
    const QString work = QFileInfo(m_dir->path()).canonicalFilePath();
    QDir(work).mkpath(QStringLiteral("tmp"));
    for (const char *name : {"TMPDIR", "TMP", "TEMP"})
        env.insert(QLatin1String(name), QDir::toNativeSeparators(work + QStringLiteral("/tmp")));
    // The engine's own settings (PATH, TEXINPUTS...) override the rest.
    const QStringList keys = m_extraEnvironment.keys();
    for (const QString &key : keys)
        env.insert(key, m_extraEnvironment.value(key));
    m_process->setProcessEnvironment(env);
#ifdef Q_OS_UNIX
    // Kernel limits as well: no file over the folder limit, and no more CPU
    // time than the run is allowed anyway.
    m_process->setChildProcessModifier([cpuSeconds = network ? TectonicTimeoutMs / 1000 : EngineTimeoutMs / 1000] {
        const struct rlimit fileSize{rlim_t(MaxFolderBytes), rlim_t(MaxFolderBytes)};
        setrlimit(RLIMIT_FSIZE, &fileSize);
        const struct rlimit cpu{rlim_t(cpuSeconds), rlim_t(cpuSeconds)};
        setrlimit(RLIMIT_CPU, &cpu);
    });
#endif

    const QStringList caches = cacheFolders();
    switch (sandbox()) {
    case Sandbox::MacOS: {
        QStringList wrapped{QStringLiteral("-p"), macSandboxProfile(int(caches.size()), network),
                            QStringLiteral("-D"), QStringLiteral("WORK=") + work};
        for (int i = 0; i < caches.size(); ++i)
            wrapped << QStringLiteral("-D") << QStringLiteral("CACHE%1=").arg(i) + caches.at(i);
        wrapped << program << arguments;
        m_process->start(sandboxProgram(), wrapped);
        break;
    }
    case Sandbox::Bubblewrap: {
        QStringList wrapped{QStringLiteral("--ro-bind"), QStringLiteral("/"), QStringLiteral("/"),
                            QStringLiteral("--dev"), QStringLiteral("/dev"),
                            QStringLiteral("--proc"), QStringLiteral("/proc"),
                            QStringLiteral("--bind"), work, work};
        for (const QString &cache : caches)
            wrapped << QStringLiteral("--bind-try") << cache << cache;
        wrapped << QStringLiteral("--unshare-all");
        if (network)
            wrapped << QStringLiteral("--share-net");
        wrapped << QStringLiteral("--die-with-parent") << QStringLiteral("--new-session")
                << QStringLiteral("--chdir") << work << QStringLiteral("--") << program << arguments;
        m_process->start(sandboxProgram(), wrapped);
        break;
    }
    case Sandbox::None:
        m_process->start(program, arguments);
        break;
    }
    m_sizeCheck->start();
}

void TexCompiler::checkFolderSize()
{
    if (m_process && m_dir && folderSize(m_dir->path()) > MaxFolderBytes) {
        cancel();
        fail(tr("Typesetting was stopped because the document wrote too much data."));
    }
}

void TexCompiler::startEngine()
{
    ++m_passes;
    if (needsSandbox(m_engine.engine) && sandbox() == Sandbox::None) {
        fail(tr("%1 can only be used where typesetting can be sandboxed.").arg(engineName(m_engine.engine)));
        return;
    }

    QProcessEnvironment env;
    const QChar listSeparator = QDir::listSeparator();
    // Engines run helper programs from their own folder.
    env.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(QFileInfo(m_engine.path).absolutePath())
                                           + listSeparator + qEnvironmentVariable("PATH"));
    if (!m_sourceFolder.isEmpty() && safeForTexInputs(m_sourceFolder)) {
        // A trailing separator keeps TeX's own folders in the search.
        const QString existing = qEnvironmentVariable("TEXINPUTS");
        env.insert(QStringLiteral("TEXINPUTS"), QDir::toNativeSeparators(m_sourceFolder) + listSeparator
                                                    + (existing.isEmpty() ? QString() : existing + listSeparator));
    }
    // No shell escape, and output files only in the working folder.
    env.insert(QStringLiteral("shell_escape"), QStringLiteral("f"));
    env.insert(QStringLiteral("openout_any"), QStringLiteral("p"));
    m_extraEnvironment = env;

    QStringList arguments;
    if (m_engine.engine == Engine::Tectonic) {
        // Tectonic never runs shell commands unless asked with
        // -Z shell-escape. (--untrusted would also turn off search-path.)
        arguments << QStringLiteral("--keep-logs") << QStringLiteral("--keep-intermediates")
                  << QStringLiteral("--chatter") << QStringLiteral("minimal")
                  << QStringLiteral("--outdir") << m_dir->path();
        if (!m_sourceFolder.isEmpty())
            arguments << QStringLiteral("-Z") << QStringLiteral("search-path=") + m_sourceFolder;
    } else {
        arguments << QStringLiteral("-interaction=nonstopmode") << QStringLiteral("-file-line-error")
                  << QStringLiteral("-no-shell-escape");
    }
    arguments << QStringLiteral("preview.tex");

    m_timeout->start(m_engine.engine == Engine::Tectonic ? TectonicTimeoutMs : EngineTimeoutMs);
    // Only Tectonic needs the network, to download packages.
    start(m_engine.path, arguments, m_engine.engine == Engine::Tectonic);
    connect(m_process, &QProcess::finished, this, &TexCompiler::engineFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            const QString name = engineName(m_engine.engine);
            cancel();
            fail(tr("%1 couldn't be started.").arg(name));
        }
    });
}

void TexCompiler::engineFinished()
{
    m_timeout->stop();
    m_sizeCheck->stop();
    const QString output = QString::fromUtf8(m_process->readAll());
    const bool ok = m_process->exitStatus() == QProcess::NormalExit && m_process->exitCode() == 0;
    m_process->deleteLater();
    m_process = nullptr;

    QFile logFile(path(QStringLiteral("preview.log")));
    const QString log = logFile.open(QIODevice::ReadOnly) ? QString::fromUtf8(logFile.read(4 * 1024 * 1024))
                                                          : QString();
    if (!ok)
        m_warning = firstError(output + u'\n' + log);

    // One more pass when LaTeX says references changed (Tectonic reruns by
    // itself).
    if (ok && m_engine.engine != Engine::Tectonic && m_passes < MaxPasses
        && (log.contains(QLatin1String("Rerun to get")) || log.contains(QLatin1String("Label(s) may have changed")))) {
        startEngine();
        return;
    }

    if (!QFileInfo::exists(path(QStringLiteral("preview.pdf")))) {
        fail(m_warning.isEmpty() ? tr("%1 didn't produce a PDF.").arg(engineName(m_engine.engine)) : m_warning);
        return;
    }
    startRendering();
}

void TexCompiler::startRendering()
{
    const QString tool = rasterizer();
    if (tool.isEmpty()) {
        // The PDF is still there to open in another app.
        emit finished({}, path(QStringLiteral("preview.pdf")), m_warning);
        return;
    }
    QDir dir(m_dir->path());
    for (const QString &old : dir.entryList({QStringLiteral("page-*.png")}, QDir::Files))
        dir.remove(old);

    // The PDF can carry content from the document (raw PDF code, included
    // PDFs and images), so it's rendered in the sandbox too, with a limit
    // on the page size in pixels: a page can claim to be enormous.
    const QString dpi = QString::number(m_dpi);
    const QString lastPage = QString::number(MaxPages);
    const QString maxPixels = QString::number(qMin(MaxPagePixels, m_dpi * 12));
    QStringList arguments;
    if (QFileInfo(tool).baseName() == QLatin1String("mutool")) {
        arguments << QStringLiteral("draw") << QStringLiteral("-q") << QStringLiteral("-r") << dpi
                  << QStringLiteral("-w") << maxPixels << QStringLiteral("-h") << maxPixels
                  << QStringLiteral("-o") << QStringLiteral("page-%d.png") << QStringLiteral("preview.pdf")
                  << QStringLiteral("1-") + lastPage;
    } else {
        arguments << QStringLiteral("-png") << QStringLiteral("-scale-to") << maxPixels
                  << QStringLiteral("-f") << QStringLiteral("1") << QStringLiteral("-l") << lastPage
                  << QStringLiteral("preview.pdf") << QStringLiteral("page");
    }
    m_extraEnvironment = QProcessEnvironment();
    start(tool, arguments, false);
    connect(m_process, &QProcess::finished, this, &TexCompiler::renderingFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            cancel();
            emit finished({}, path(QStringLiteral("preview.pdf")), m_warning);
        }
    });
    m_timeout->start(RenderTimeoutMs);
}

void TexCompiler::renderingFinished()
{
    m_timeout->stop();
    m_sizeCheck->stop();
    m_process->deleteLater();
    m_process = nullptr;

    QStringList pages = QDir(m_dir->path()).entryList({QStringLiteral("page-*.png")}, QDir::Files);
    std::sort(pages.begin(), pages.end(),
              [](const QString &a, const QString &b) { return pageNumber(a) < pageNumber(b); });
    for (QString &page : pages)
        page = path(page);
    emit finished(pages, path(QStringLiteral("preview.pdf")), m_warning);
}

void TexCompiler::cancel()
{
    m_timeout->stop();
    m_sizeCheck->stop();
    if (!m_process)
        return;
    disconnect(m_process, nullptr, this, nullptr);
    m_process->kill();
    m_process->waitForFinished(1000);
    m_process->deleteLater();
    m_process = nullptr;
}

void TexCompiler::fail(const QString &message)
{
    emit failed(message);
}

// The first error in TeX's output, as "Line 12: Undefined control sequence."
QString TexCompiler::firstError(const QString &output) const
{
    static const QRegularExpression fileLine(QStringLiteral("preview\\.tex:(\\d+):\\s*(.+)"));
    const QRegularExpressionMatch match = fileLine.match(output);
    if (match.hasMatch())
        return tr("Line %1: %2").arg(match.captured(1), match.captured(2).trimmed());
    static const QRegularExpression bang(QStringLiteral("^! (.+)$"), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch error = bang.match(output);
    if (error.hasMatch())
        return error.captured(1).trimmed();
    static const QRegularExpression tectonic(QStringLiteral("^error: (.+)$"), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch other = tectonic.match(output);
    if (other.hasMatch())
        return other.captured(1).trimmed();
    return tr("%1 reported errors.").arg(engineName(m_engine.engine));
}
