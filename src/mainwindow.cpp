#include "mainwindow.h"

#include "codeeditor.h"
#include "findbar.h"

#include <QActionGroup>
#include <algorithm>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileOpenEvent>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringDecoder>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

#ifdef Q_OS_MACOS
constexpr int DefaultFontSize = 14;
#else
constexpr int DefaultFontSize = 11;
#endif
constexpr int MinFontSize = 6;
constexpr int MaxFontSize = 72;

// Files above the first limit need confirmation; above the second they are
// refused outright, since the whole file is held in memory.
constexpr qint64 LargeFileSize = 50LL * 1024 * 1024;
constexpr qint64 MaxFileSize = 512LL * 1024 * 1024;

const char *const FileFilters =
    "All Files (*);;"
    "Text Files (*.txt *.md);;"
    "Web Files (*.html *.htm *.css *.js *.json);;"
    "C/C++ Files (*.c *.h *.cpp *.hpp *.cc *.cxx)";

// Message text often contains file names or paths, so it is always shown as
// plain text: QMessageBox would otherwise render a name such as
// "<img src=...>.txt" as HTML.
QMessageBox::StandardButton showMessage(
    QWidget *parent, QMessageBox::Icon icon, const QString &title, const QString &text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    QMessageBox box(icon, title, text, buttons, parent);
    box.setTextFormat(Qt::PlainText);
    if (defaultButton != QMessageBox::NoButton)
        box.setDefaultButton(defaultButton);
    return QMessageBox::StandardButton(box.exec());
}

QString megabytes(qint64 bytes)
{
    return QString::number(double(bytes) / (1024 * 1024), 'f', 0) + QStringLiteral(" MB");
}

// Opens path for reading and returns an error message, or an empty string on
// success. On Unix the regular-file check is made on the opened file itself,
// so the path can't be swapped for a FIFO or device after loadFile() looked
// at it, and O_NONBLOCK stops open() from waiting on a FIFO.
QString openForReading(QFile &file, const QString &path)
{
#ifdef Q_OS_UNIX
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return qt_error_string(errno);
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return QCoreApplication::translate("MainWindow", "Not a regular file.");
    }
    // Reads from a regular file never block, so O_NONBLOCK can stay set.
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return file.errorString();
    }
    return {};
#else
    file.setFileName(path);
    return file.open(QIODevice::ReadOnly) ? QString() : file.errorString();
#endif
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_editor(new CodeEditor(this))
    , m_highlighter(new Highlighter(m_editor->document()))
    , m_findBar(new FindBar(m_editor, this))
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_findBar);
    layout->addWidget(m_editor);
    setCentralWidget(central);
    m_findBar->hide();

    createUi();

    connect(m_editor->document(), &QTextDocument::modificationChanged,
            this, &QWidget::setWindowModified);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged,
            this, &MainWindow::updateCursorPosition);
    connect(m_editor, &CodeEditor::zoomRequested, this, &MainWindow::changeFontSize);
    connect(m_editor, &CodeEditor::fileDropped, this, &MainWindow::openDroppedFile);

    // macOS sends files dropped on the Dock icon (or opened from Finder)
    // to the application object rather than to a window.
    qApp->installEventFilter(this);

#ifdef Q_OS_MACOS
    // The app keeps running after its window is closed, as Mac apps do.
    // Clicking the Dock icon then brings the window back.
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive)
                    reveal();
            });
#endif

    readSettings();
    setCurrentFile({});
    updateCursorPosition();
}

void MainWindow::createUi()
{
    auto addAction = [this](QMenu *menu, const QString &text, const QKeySequence &shortcut,
                            auto slot) {
        QAction *action = menu->addAction(text);
        action->setShortcut(shortcut);
        // On macOS the menus stay usable after the window is closed.
        connect(action, &QAction::triggered, this, &MainWindow::reveal);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    // File
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    addAction(fileMenu, tr("&New"), QKeySequence::New, &MainWindow::newFile);
    QAction *openAction = addAction(fileMenu, tr("&Open…"), QKeySequence::Open, &MainWindow::open);
    fileMenu->addSeparator();
    QAction *saveAction = addAction(fileMenu, tr("&Save"), QKeySequence::Save, &MainWindow::save);
    addAction(fileMenu, tr("Save &As…"), QKeySequence::SaveAs, &MainWindow::saveAs);
    fileMenu->addSeparator();
    addAction(fileMenu, tr("&Print…"), QKeySequence::Print, &MainWindow::print);
    fileMenu->addSeparator();
    QAction *closeAction = fileMenu->addAction(tr("&Close Window"));
    closeAction->setShortcut(QKeySequence::Close);
    connect(closeAction, &QAction::triggered, this, &QWidget::close);
    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, &QAction::triggered, this, [this] {
        if (close())
            QApplication::quit();
    });

    // Edit
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undoAction = addAction(editMenu, tr("&Undo"), QKeySequence::Undo,
                                    [this] { m_editor->undo(); });
    QAction *redoAction = addAction(editMenu, tr("&Redo"), QKeySequence::Redo,
                                    [this] { m_editor->redo(); });
    undoAction->setEnabled(false);
    redoAction->setEnabled(false);
    connect(m_editor, &QPlainTextEdit::undoAvailable, undoAction, &QAction::setEnabled);
    connect(m_editor, &QPlainTextEdit::redoAvailable, redoAction, &QAction::setEnabled);
    editMenu->addSeparator();
    addAction(editMenu, tr("Cu&t"), QKeySequence::Cut, [this] { m_editor->cut(); });
    addAction(editMenu, tr("&Copy"), QKeySequence::Copy, [this] { m_editor->copy(); });
    addAction(editMenu, tr("&Paste"), QKeySequence::Paste, [this] { m_editor->paste(); });
    addAction(editMenu, tr("Select &All"), QKeySequence::SelectAll,
              [this] { m_editor->selectAll(); });
    editMenu->addSeparator();
    QAction *findAction = addAction(editMenu, tr("&Find…"), QKeySequence::Find,
                                    [this] { m_findBar->showFind(false); });
    QKeySequence replaceKey(QKeySequence::Replace);
    if (replaceKey.isEmpty()) // No platform default on macOS.
        replaceKey = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F);
    addAction(editMenu, tr("&Replace…"), replaceKey, [this] { m_findBar->showFind(true); });
    addAction(editMenu, tr("Find &Next"), QKeySequence::FindNext,
              [this] { m_findBar->findNext(); });
    addAction(editMenu, tr("Find Pre&vious"), QKeySequence::FindPrevious,
              [this] { m_findBar->findPrevious(); });
    editMenu->addSeparator();
    addAction(editMenu, tr("&Go to Line…"), QKeySequence(Qt::CTRL | Qt::Key_L),
              &MainWindow::goToLine);

    // View
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    m_wrapAction = addAction(viewMenu, tr("&Word Wrap"), QKeySequence(Qt::ALT | Qt::Key_Z),
                             &MainWindow::setWordWrap);
    m_wrapAction->setCheckable(true);
    m_wrapAction->setIconText(tr("Wrap"));
    m_darkAction = addAction(viewMenu, tr("&Dark Theme"), QKeySequence(),
                             &MainWindow::setDarkTheme);
    m_darkAction->setCheckable(true);
    m_darkAction->setIconText(tr("Theme"));
    viewMenu->addSeparator();
    QAction *zoomInAction = addAction(viewMenu, tr("Zoom &In"), QKeySequence(),
                                      [this] { changeFontSize(1); });
    QList<QKeySequence> zoomInKeys = QKeySequence::keyBindings(QKeySequence::ZoomIn);
    zoomInKeys << QKeySequence(Qt::CTRL | Qt::Key_Equal);
    zoomInAction->setShortcuts(zoomInKeys);
    zoomInAction->setIconText(tr("A+"));
    QAction *zoomOutAction = addAction(viewMenu, tr("Zoom &Out"), QKeySequence::ZoomOut,
                                       [this] { changeFontSize(-1); });
    zoomOutAction->setIconText(tr("A−"));
    addAction(viewMenu, tr("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_0),
              &MainWindow::resetFontSize);

    // Language
    QMenu *languageMenu = menuBar()->addMenu(tr("&Language"));
    m_languageGroup = new QActionGroup(this);
    m_languageCombo = new QComboBox(this);
    for (Language language : Highlighter::languages()) {
        const QString name = Highlighter::languageName(language);
        QAction *action = languageMenu->addAction(name);
        action->setCheckable(true);
        action->setData(int(language));
        m_languageGroup->addAction(action);
        m_languageCombo->addItem(name, int(language));
    }
    connect(m_languageGroup, &QActionGroup::triggered, this,
            [this](QAction *action) { setLanguage(Language(action->data().toInt())); });
    connect(m_languageCombo, &QComboBox::activated, this, [this](int index) {
        setLanguage(Language(m_languageCombo->itemData(index).toInt()));
    });

    // Help
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAction = addAction(helpMenu, tr("&About TextEditor+"), QKeySequence(), [this] {
        QMessageBox::about(this, tr("About TextEditor+"),
                           tr("<b>TextEditor+ %1</b>"
                              "<p>An OS native version of WebText editor "
                              "(<a href=\"https://sjwrc.com/tools/editor\">https://sjwrc.com/tools/editor</a>).<br>"
                              "Developed by Sanjeeva Wijeyesakere.<br>"
                              "Github: <a href=\"https://github.com/swijeyes\">https://github.com/swijeyes</a>")
                               .arg(QApplication::applicationVersion()));
    });
    aboutAction->setMenuRole(QAction::AboutRole);

    // Toolbar, laid out like the web version's.
    m_fontCombo = new QFontComboBox(this);
    m_fontCombo->setFontFilters(QFontComboBox::MonospacedFonts);
    m_fontCombo->setToolTip(tr("Editor font"));
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        m_fontFamily = font.family();
        applyFont();
    });
    m_languageCombo->setToolTip(tr("Syntax highlighting"));

    QToolBar *toolBar = addToolBar(tr("Toolbar"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolBar->addAction(openAction);
    toolBar->addAction(saveAction);
    toolBar->addSeparator();
    toolBar->addAction(m_wrapAction);
    toolBar->addAction(m_darkAction);
    toolBar->addSeparator();
    toolBar->addAction(zoomInAction);
    toolBar->addAction(zoomOutAction);
    toolBar->addWidget(m_fontCombo);
    toolBar->addSeparator();
    toolBar->addAction(findAction);
    toolBar->addWidget(m_languageCombo);
    setUnifiedTitleAndToolBarOnMac(true);

    // Status bar
    m_positionLabel = new QLabel(this);
    m_lineEndingLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_positionLabel);
    statusBar()->addPermanentWidget(m_lineEndingLabel);
}

void MainWindow::loadFile(const QString &path)
{
    const QString nativePath = QDir::toNativeSeparators(path);

    // Only open regular files: reading a device such as /dev/zero or a FIFO
    // would never finish.
    const QFileInfo info(path);
    if (!info.isFile()) {
        showMessage(this, QMessageBox::Warning, tr("Open File"),
                    info.exists() ? tr("%1 is not a regular file.").arg(nativePath)
                                  : tr("%1 does not exist.").arg(nativePath));
        return;
    }
    if (info.size() > MaxFileSize) {
        showMessage(this, QMessageBox::Warning, tr("Open File"),
                    tr("%1 is too large to open (%2; the limit is %3).")
                        .arg(nativePath, megabytes(info.size()), megabytes(MaxFileSize)));
        return;
    }
    if (info.size() > LargeFileSize
        && showMessage(this, QMessageBox::Question, tr("Open File"),
                       tr("%1 is %2. Large files can make the editor slow. Open it anyway?")
                           .arg(nativePath, megabytes(info.size())),
                       QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel)
               != QMessageBox::Open) {
        return;
    }

    QFile file;
    const QString openError = openForReading(file, path);
    if (!openError.isEmpty()) {
        showMessage(this, QMessageBox::Warning, tr("Open File"),
                    tr("Cannot read %1:\n%2").arg(nativePath, openError));
        return;
    }
    // Bounded read, in case the file changed after the checks above.
    const QByteArray data = file.read(MaxFileSize + 1);
    if (data.size() > MaxFileSize) {
        showMessage(this, QMessageBox::Warning, tr("Open File"),
                    tr("%1 is too large to open.").arg(nativePath));
        return;
    }

    // NUL bytes mean a binary file, which saving as text would corrupt.
    if (data.contains('\0')
        && showMessage(this, QMessageBox::Warning, tr("Open File"),
                       tr("%1 appears to be a binary file. Saving it from a text editor "
                          "may damage it. Open it anyway?").arg(nativePath),
                       QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel)
               != QMessageBox::Open) {
        return;
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(data);
    m_latin1 = decoder.hasError();
    if (m_latin1)
        text = QString::fromLatin1(data);

    // Edit with '\n' internally; remember the file's style for saving.
    m_crlf = text.contains(QLatin1String("\r\n"));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));

    m_editor->clear();
    setLanguage(Highlighter::languageForFile(path));
    m_editor->setPlainText(text);
    setCurrentFile(path);

    if (m_latin1)
        statusBar()->showMessage(tr("Not valid UTF-8: opened as Latin-1"), 8000);
}

bool MainWindow::saveFile(const QString &path)
{
    QString text = m_editor->toPlainText();
    if (m_crlf)
        text.replace(QLatin1Char('\n'), QLatin1String("\r\n"));

    // Save in the encoding the file was opened with. Latin-1 can't hold every
    // character, so ask before switching such a file to UTF-8.
    bool latin1 = m_latin1;
    if (latin1) {
        const bool fits = std::all_of(text.cbegin(), text.cend(),
                                      [](QChar ch) { return ch.unicode() <= 0xff; });
        if (!fits) {
            if (showMessage(this, QMessageBox::Question, tr("Save File"),
                            tr("This file uses the Latin-1 encoding, which can't store some of "
                               "the characters it now contains. Save it as UTF-8 instead?"),
                            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save)
                != QMessageBox::Save) {
                return false;
            }
            latin1 = false;
        }
    }

    // QSaveFile writes to a temporary file and swaps it in, so a failed save
    // never leaves a half-written file behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(latin1 ? text.toLatin1() : text.toUtf8()) < 0 || !file.commit()) {
        showMessage(this, QMessageBox::Warning, tr("Save File"),
                    tr("Cannot write %1:\n%2").arg(QDir::toNativeSeparators(path),
                                                   file.errorString()));
        return false;
    }

    m_latin1 = latin1;
    setCurrentFile(path);
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 2000);
    return true;
}

void MainWindow::setCurrentFile(const QString &path)
{
    m_currentFile = path;
    m_editor->document()->setModified(false);
    setWindowModified(false);
    setWindowFilePath(path.isEmpty() ? tr("Untitled") : path);
    m_lineEndingLabel->setText((m_latin1 ? QStringLiteral("Latin-1  ") : QStringLiteral("UTF-8  "))
                               + (m_crlf ? "CRLF" : "LF"));
}

void MainWindow::newFile()
{
    if (maybeSave())
        resetDocument();
}

void MainWindow::resetDocument()
{
    m_editor->clear();
    m_crlf = false;
    m_latin1 = false;
    setLanguage(Language::PlainText);
    setCurrentFile({});
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    const QString dir = m_currentFile.isEmpty() ? QString() : QFileInfo(m_currentFile).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open File"), dir,
                                                      tr(FileFilters));
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::openDroppedFile(const QString &path)
{
    if (maybeSave())
        loadFile(path);
}

void MainWindow::openRequestedFile(const QString &path)
{
    // A file can arrive from the Dock or Finder while a dialog is open, such
    // as Save As. Opening it then would swap the document out from under
    // the dialog, so wait until the dialog has closed.
    if (QApplication::activeModalWidget()) {
        QTimer::singleShot(250, this, [this, path] { openRequestedFile(path); });
        return;
    }
    reveal();
    raise();
    activateWindow();
    openDroppedFile(path);
}

void MainWindow::reveal()
{
    if (isMinimized())
        showNormal();
    else if (!isVisible())
        show();
}

bool MainWindow::save()
{
    return m_currentFile.isEmpty() ? saveAs() : saveFile(m_currentFile);
}

bool MainWindow::saveAs()
{
    const QString suggested = m_currentFile.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/untitled.txt"
        : m_currentFile;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), suggested,
                                                      tr(FileFilters));
    if (path.isEmpty() || !saveFile(path))
        return false;

    // Pick up highlighting from the new name, but keep a manual choice when
    // the name says nothing (e.g. notes.txt).
    const Language detected = Highlighter::languageForFile(path);
    if (detected != Language::PlainText)
        setLanguage(detected);
    return true;
}

void MainWindow::print()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(m_currentFile.isEmpty() ? tr("Untitled") : QFileInfo(m_currentFile).fileName());

    QPrintDialog dialog(&printer, this);
    if (m_editor->textCursor().hasSelection())
        dialog.setOption(QAbstractPrintDialog::PrintSelection);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Print a copy highlighted with the light theme, so dark-theme colours
    // never end up on white paper.
    const bool selectionOnly = printer.printRange() == QPrinter::Selection;
    QTextDocument document;
    document.setDefaultFont(m_editor->font());
    document.setPlainText(selectionOnly ? m_editor->textCursor().selection().toPlainText()
                                        : m_editor->toPlainText());
    Highlighter highlighter(&document);
    highlighter.setTheme(Theme::eclipse());
    highlighter.setLanguage(m_highlighter->language());
    document.print(&printer);
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified())
        return true;

    const QString name = m_currentFile.isEmpty() ? tr("Untitled")
                                                 : QFileInfo(m_currentFile).fileName();
    const auto answer = showMessage(
        this, QMessageBox::Warning, tr("Unsaved Changes"),
        tr("Do you want to save the changes you made to \"%1\"?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::FileOpen) {
        const QString path = static_cast<QFileOpenEvent *>(event)->file();
        if (!path.isEmpty())
            openRequestedFile(path);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    writeSettings();
    event->accept();
#ifdef Q_OS_MACOS
    // The app keeps running, so start afresh the next time the window shows.
    resetDocument();
#endif
}

void MainWindow::setLanguage(Language language)
{
    m_highlighter->setLanguage(language);

    const QSignalBlocker blocker(m_languageCombo);
    m_languageCombo->setCurrentIndex(m_languageCombo->findData(int(language)));
    for (QAction *action : m_languageGroup->actions())
        action->setChecked(action->data().toInt() == int(language));
}

void MainWindow::setWordWrap(bool on)
{
    m_wrapAction->setChecked(on);
    m_editor->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void MainWindow::setDarkTheme(bool dark)
{
    m_darkAction->setChecked(dark);
    const Theme theme = dark ? Theme::dracula() : Theme::eclipse();
    m_editor->setTheme(theme);
    m_highlighter->setTheme(theme);
}

void MainWindow::changeFontSize(int delta)
{
    m_fontSize = qBound(MinFontSize, m_fontSize + delta, MaxFontSize);
    applyFont();
}

void MainWindow::resetFontSize()
{
    m_fontSize = DefaultFontSize;
    applyFont();
}

void MainWindow::applyFont()
{
    QFont font(m_fontFamily);
    font.setPointSize(m_fontSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    m_editor->setEditorFont(font);

    const QSignalBlocker blocker(m_fontCombo);
    m_fontCombo->setCurrentFont(font);
}

void MainWindow::goToLine()
{
    const int lines = m_editor->document()->blockCount();
    bool ok = false;
    const int line = QInputDialog::getInt(this, tr("Go to Line"), tr("Line (1–%1):").arg(lines),
                                          m_editor->textCursor().blockNumber() + 1, 1, lines, 1,
                                          &ok);
    if (!ok)
        return;
    m_editor->setTextCursor(QTextCursor(m_editor->document()->findBlockByNumber(line - 1)));
    m_editor->centerCursor();
    m_editor->setFocus();
}

void MainWindow::updateCursorPosition()
{
    const QTextCursor cursor = m_editor->textCursor();
    m_positionLabel->setText(tr("Ln %1, Col %2")
                                 .arg(cursor.blockNumber() + 1)
                                 .arg(cursor.positionInBlock() + 1));
}

void MainWindow::readSettings()
{
    const QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
    if (settings.value("geometry").isNull())
        resize(1000, 700);

    m_fontFamily = settings.value("fontFamily",
                                  QFontDatabase::systemFont(QFontDatabase::FixedFont).family())
                       .toString();
    m_fontSize = qBound(MinFontSize, settings.value("fontSize", DefaultFontSize).toInt(),
                        MaxFontSize);
    applyFont();
    setWordWrap(settings.value("wordWrap", true).toBool());
    setDarkTheme(settings.value("darkTheme", true).toBool());
}

void MainWindow::writeSettings() const
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("fontFamily", m_fontFamily);
    settings.setValue("fontSize", m_fontSize);
    settings.setValue("wordWrap", m_wrapAction->isChecked());
    settings.setValue("darkTheme", m_darkAction->isChecked());
}
