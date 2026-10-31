#include "mainwindow.h"

#include "codeeditor.h"
#include "document.h"
#include "findbar.h"
#include "foldersidebar.h"
#include "localfilesystem.h"
#include "location.h"
#include "messagebox.h"
#include "remotefiledialog.h"
#include "preview.h"
#include "settingsdialog.h"
#include "spellchecker.h"
#include "textcodec.h"

#ifdef TEXTEDITOR_HAVE_SFTP
#include "sftp/authdialog.h"
#include "sftp/connectdialog.h"
#include "sftp/connectionmanager.h"
#include "sftp/sftpfilesystem.h"
#endif

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <optional>

namespace {

#ifdef Q_OS_MACOS
constexpr int DefaultFontSize = 14;
#else
constexpr int DefaultFontSize = 11;
#endif
constexpr int MinFontSize = 6;
constexpr int MaxFontSize = 72;
constexpr int MaxRecentItems = 15;
// Operations finishing sooner than this show no progress dialog.
constexpr int ProgressDelayMs = 400;

const char *const FileFilters =
    "All Files (*);;"
    "Text Files (*.txt *.md);;"
    "Web Files (*.html *.htm *.css *.js *.json);;"
    "C/C++ Files (*.c *.h *.cpp *.hpp *.cc *.cxx);;"
    "Python Files (*.py *.pyw *.pyi);;"
    "Rust Files (*.rs);;"
    "Markdown Files (*.md *.markdown);;"
    "TeX Files (*.tex *.latex *.ltx *.sty *.cls)";

QString megabytes(qint64 bytes)
{
    return QString::number(double(bytes) / (1024 * 1024), 'f', 0) + QStringLiteral(" MB");
}

// What the preview shows for a tab, if anything. LaTeX package and class
// files, and XML, SVG and Vue files (highlighted as LaTeX or HTML), have
// nothing to show.
std::optional<Preview::Kind> previewKind(const Document *document)
{
    const QString suffix = QFileInfo(Location::fileName(document->location())).suffix().toLower();
    switch (document->highlighter()->language()) {
    case Language::Latex:
        if (suffix == QLatin1String("sty") || suffix == QLatin1String("cls"))
            return std::nullopt;
        return Preview::Kind::Latex;
    case Language::Markdown:
        return Preview::Kind::Markdown;
    case Language::Html:
        if (QStringList{"xml", "svg", "vue"}.contains(suffix))
            return std::nullopt;
        return Preview::Kind::Html;
    default:
        return std::nullopt;
    }
}

// A toolbar icon from resources/toolbar, at 1x, 2x and 3x.
QIcon toolBarIcon(const QString &name)
{
    QIcon icon;
    for (const char *scale : {"", "@2x", "@3x"})
        icon.addFile(QStringLiteral(":/toolbar/%1%2.png").arg(name, QLatin1String(scale)));
    return icon;
}

QString buttonStyleName(Qt::ToolButtonStyle style)
{
    switch (style) {
    case Qt::ToolButtonTextUnderIcon: return QStringLiteral("under");
    case Qt::ToolButtonIconOnly: return QStringLiteral("icon");
    default: return QStringLiteral("beside");
    }
}

Qt::ToolButtonStyle buttonStyleFromName(const QString &name)
{
    if (name == QLatin1String("under"))
        return Qt::ToolButtonTextUnderIcon;
    if (name == QLatin1String("icon"))
        return Qt::ToolButtonIconOnly;
    return Qt::ToolButtonTextBesideIcon;
}

// "&" in a menu or tab label would otherwise mark a shortcut letter.
QString escapeMnemonic(QString text)
{
    return text.replace(QLatin1Char('&'), QLatin1String("&&"));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabs(new QTabWidget(this))
    , m_findBar(new FindBar(nullptr, this))
    , m_localFs(std::make_shared<LocalFileSystem>())
{
    // The toolbar icons are in the texteditor_ui library, which is static.
    Q_INIT_RESOURCE(toolbar);
#ifdef TEXTEDITOR_HAVE_SFTP
    m_prompter = std::make_unique<DialogPrompter>(this);
    m_connections = new ConnectionManager(m_prompter.get(), SftpOptions(), this);
#endif

    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideMiddle);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_findBar);
    layout->addWidget(m_tabs);
    setCentralWidget(central);
    m_findBar->hide();

    createSidebar();
    createPreview();
    createUi();

    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::currentTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    // A tab restored for a remote file connects when clicked.
    connect(m_tabs->tabBar(), &QTabBar::tabBarClicked, this, [this](int index) {
        QPointer<Document> document = documentAt(index);
        if (document && !document->isLoaded()) {
            QTimer::singleShot(0, this, [this, document] {
                if (document && !document->isLoaded())
                    loadInto(document, document->location());
            });
        }
    });

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
    // Without this, the window's first focusable widget (the font box in
    // the toolbar) has the focus when it first appears.
    newTab()->setFocus();
}

MainWindow::~MainWindow()
{
#ifdef TEXTEDITOR_HAVE_SFTP
    // Stop the connection threads before the prompter they use goes away.
    m_connections->shutdownAll();
#endif
}

void MainWindow::createSidebar()
{
    m_sidebar = new FolderSidebar(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebar);
    m_sidebar->hide();
    connect(m_sidebar, &FolderSidebar::fileActivated, this, &MainWindow::openLocation);
    connect(m_sidebar, &FolderSidebar::connectRequested, this, &MainWindow::openFolderUrl);
    connect(m_sidebar, &FolderSidebar::itemRenamed, this, &MainWindow::followRename);
    connect(m_sidebar, &FolderSidebar::itemRemoved, this, [this](const QUrl &url) {
        statusBar()->showMessage(tr("Deleted %1").arg(Location::fileName(url)), 3000);
    });
}

void MainWindow::createPreview()
{
    m_preview = new Preview(this);
    addDockWidget(Qt::RightDockWidgetArea, m_preview);
    m_preview->hide();
    connect(m_preview, &Preview::closedByUser, this, [this] {
        m_previewWanted = false;
        m_previewAction->setChecked(false);
        m_preview->setDocument(nullptr);
    });
}

void MainWindow::updatePreview()
{
    Document *document = currentDocument();
    const std::optional<Preview::Kind> kind =
        document && document->isLoaded() ? previewKind(document) : std::nullopt;
    m_previewAction->setEnabled(kind.has_value());
    m_previewAction->setChecked(m_previewWanted);
    if (kind && m_previewWanted) {
        m_preview->setDocument(document, *kind);
        if (!m_preview->isVisible()) {
            m_preview->show();
            // Give it a good share of the window, unless it was made wider.
            if (m_preview->width() < width() / 3)
                resizeDocks({m_preview}, {width() * 2 / 5}, Qt::Horizontal);
        }
    } else {
        m_preview->setDocument(nullptr);
        m_preview->hide();
    }
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
    auto editor = [this] { return currentDocument()->editor(); };

    // File
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *newAction = addAction(fileMenu, tr("&New Tab"), QKeySequence::New, [this] {
        newTab()->setFocus();
    });
    newAction->setShortcuts({QKeySequence(QKeySequence::New), QKeySequence(Qt::CTRL | Qt::Key_T)});
    QAction *openAction = addAction(fileMenu, tr("&Open…"), QKeySequence::Open, &MainWindow::open);
    addAction(fileMenu, tr("Open &Folder…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
              &MainWindow::openFolder);
#ifdef TEXTEDITOR_HAVE_SFTP
    fileMenu->addSeparator();
    addAction(fileMenu, tr("&Connect to Server…"), QKeySequence(Qt::CTRL | Qt::Key_K),
              &MainWindow::connectToServer);
    addAction(fileMenu, tr("Open &Remote File…"), QKeySequence(), &MainWindow::openRemoteFile);
    addAction(fileMenu, tr("Open Remote Fol&der…"), QKeySequence(), &MainWindow::openRemoteFolder);
#endif
    fileMenu->addSeparator();
    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentMenu);
    fileMenu->addSeparator();
    QAction *saveAction = addAction(fileMenu, tr("&Save"), QKeySequence::Save,
                                    [this] { save(currentDocument()); });
    addAction(fileMenu, tr("Save &As…"), QKeySequence::SaveAs, [this] { saveAs(currentDocument()); });
#ifdef TEXTEDITOR_HAVE_SFTP
    addAction(fileMenu, tr("Save to Ser&ver…"), QKeySequence(),
              [this] { saveToServer(currentDocument()); });
#endif
    fileMenu->addSeparator();
    addAction(fileMenu, tr("&Print…"), QKeySequence::Print, &MainWindow::print);
    fileMenu->addSeparator();
    addAction(fileMenu, tr("&Close Tab"), QKeySequence::Close,
              [this] { closeTab(m_tabs->currentIndex()); });
    addAction(fileMenu, tr("Close Fol&der"), QKeySequence(), &MainWindow::closeFolder);
    QAction *closeAction = fileMenu->addAction(tr("Close &Window"));
    closeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
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
    m_undoAction = addAction(editMenu, tr("&Undo"), QKeySequence::Undo, [editor] { editor()->undo(); });
    m_redoAction = addAction(editMenu, tr("&Redo"), QKeySequence::Redo, [editor] { editor()->redo(); });
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    editMenu->addSeparator();
    addAction(editMenu, tr("Cu&t"), QKeySequence::Cut, [editor] { editor()->cut(); });
    addAction(editMenu, tr("&Copy"), QKeySequence::Copy, [editor] { editor()->copy(); });
    addAction(editMenu, tr("&Paste"), QKeySequence::Paste, [editor] { editor()->paste(); });
    addAction(editMenu, tr("Select &All"), QKeySequence::SelectAll,
              [editor] { editor()->selectAll(); });
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
    editMenu->addSeparator();
    // On macOS this moves to the application menu, as TextEditor+ > Settings… (⌘,).
    QKeySequence settingsKey(QKeySequence::Preferences);
    if (settingsKey.isEmpty()) // No platform default on Windows.
        settingsKey = QKeySequence(Qt::CTRL | Qt::Key_Comma);
    QAction *settingsAction = addAction(editMenu, tr("Se&ttings…"), settingsKey, &MainWindow::showSettings);
    settingsAction->setMenuRole(QAction::PreferencesRole);

    // View
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    m_wrapAction = addAction(viewMenu, tr("&Word Wrap"), QKeySequence(Qt::ALT | Qt::Key_Z),
                             &MainWindow::setWordWrap);
    m_wrapAction->setCheckable(true);
    m_wrapAction->setIconText(tr("Wrap"));
    m_wrapAction->setIcon(toolBarIcon(QStringLiteral("wrap")));
    m_darkAction = addAction(viewMenu, tr("&Dark Theme"), QKeySequence(),
                             &MainWindow::setDarkTheme);
    m_darkAction->setCheckable(true);
    m_darkAction->setIconText(tr("Theme"));
    m_darkAction->setIcon(toolBarIcon(QStringLiteral("theme-sun")));
    viewMenu->addSeparator();
    QAction *zoomInAction = addAction(viewMenu, tr("Zoom &In"), QKeySequence(),
                                      [this] { changeFontSize(1); });
    QList<QKeySequence> zoomInKeys = QKeySequence::keyBindings(QKeySequence::ZoomIn);
    zoomInKeys << QKeySequence(Qt::CTRL | Qt::Key_Equal);
    zoomInAction->setShortcuts(zoomInKeys);
    zoomInAction->setIconText(tr("A+"));
    zoomInAction->setIcon(toolBarIcon(QStringLiteral("zoom-in")));
    QAction *zoomOutAction = addAction(viewMenu, tr("Zoom &Out"), QKeySequence::ZoomOut,
                                       [this] { changeFontSize(-1); });
    zoomOutAction->setIconText(tr("A−"));
    zoomOutAction->setIcon(toolBarIcon(QStringLiteral("zoom-out")));
    addAction(viewMenu, tr("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_0),
              &MainWindow::resetFontSize);
    viewMenu->addSeparator();
    QAction *sidebarAction = m_sidebar->toggleViewAction();
    sidebarAction->setText(tr("Show &Sidebar"));
    sidebarAction->setIconText(tr("Sidebar"));
    sidebarAction->setIcon(toolBarIcon(QStringLiteral("sidebar")));
    sidebarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backslash));
    viewMenu->addAction(sidebarAction);
    m_previewAction = addAction(viewMenu, tr("Show &Preview"),
                                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), [this](bool on) {
                                       m_previewWanted = on;
                                       updatePreview();
                                   });
    m_previewAction->setCheckable(true);
    m_previewAction->setChecked(true);
    m_previewAction->setEnabled(false);
    viewMenu->addSeparator();
    // Control+Tab on every platform (Cmd+Tab switches apps on macOS).
#ifdef Q_OS_MACOS
    const Qt::KeyboardModifier control = Qt::MetaModifier;
#else
    const Qt::KeyboardModifier control = Qt::ControlModifier;
#endif
    QAction *nextTab = addAction(viewMenu, tr("Next Tab"), QKeySequence(), [this] {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    nextTab->setShortcuts({QKeySequence(control | Qt::Key_Tab), QKeySequence(QKeySequence::NextChild)});
    QAction *previousTab = addAction(viewMenu, tr("Previous Tab"), QKeySequence(), [this] {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });
    previousTab->setShortcuts({QKeySequence(control | Qt::ShiftModifier | Qt::Key_Tab),
                               QKeySequence(control | Qt::ShiftModifier | Qt::Key_Backtab),
                               QKeySequence(QKeySequence::PreviousChild)});

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

    openAction->setIcon(toolBarIcon(QStringLiteral("open")));
    saveAction->setIcon(toolBarIcon(QStringLiteral("save")));
    findAction->setIcon(toolBarIcon(QStringLiteral("find")));
    // Short names under or beside the icons.
    openAction->setIconText(tr("Open"));
    saveAction->setIconText(tr("Save"));
    findAction->setIconText(tr("Find"));

    m_toolBar = addToolBar(tr("Toolbar"));
    m_toolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->setToolButtonStyle(m_preferences.toolButtonStyle);
    // The boxes stay in their own actions, so the toolbar can be refilled.
    auto *fontBox = new QWidgetAction(this);
    fontBox->setDefaultWidget(m_fontCombo);
    auto *languageBox = new QWidgetAction(this);
    languageBox->setDefaultWidget(m_languageCombo);
    m_toolBarGroups = {{sidebarAction},
                       {openAction, saveAction},
                       {m_wrapAction, m_darkAction},
                       {zoomInAction, zoomOutAction, fontBox},
                       {findAction, languageBox}};
    m_hideableButtons = {{QStringLiteral("sidebar"), sidebarAction},
                         {QStringLiteral("open"), openAction},
                         {QStringLiteral("save"), saveAction},
                         {QStringLiteral("wrap"), m_wrapAction},
                         {QStringLiteral("theme"), m_darkAction},
                         {QStringLiteral("zoomIn"), zoomInAction},
                         {QStringLiteral("zoomOut"), zoomOutAction},
                         {QStringLiteral("find"), findAction}};
    rebuildToolBar();
    setUnifiedTitleAndToolBarOnMac(true);

    // Status bar
    m_positionLabel = new QLabel(this);
    m_lineEndingLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_positionLabel);
    statusBar()->addPermanentWidget(m_lineEndingLabel);
}

// --- Tabs ---------------------------------------------------------------------

Document *MainWindow::documentAt(int index) const
{
    return qobject_cast<Document *>(m_tabs->widget(index));
}

Document *MainWindow::currentDocument() const
{
    return documentAt(m_tabs->currentIndex());
}

QList<Document *> MainWindow::documents() const
{
    QList<Document *> result;
    for (int i = 0; i < m_tabs->count(); ++i)
        result << documentAt(i);
    return result;
}

Document *MainWindow::newTab()
{
    auto *document = new Document(m_tabs);
    applyEditorSettings(document);
    connect(document, &Document::stateChanged, this, [this, document] { updateTab(document); });
    connect(document, &Document::loadRequested, this,
            [this, document] { loadInto(document, document->location()); });
    connect(document->editor(), &CodeEditor::zoomRequested, this, &MainWindow::changeFontSize);
    // Queued: opening can show dialogs, which shouldn't run inside the drop.
    connect(document->editor(), &CodeEditor::urlsDropped, this, &MainWindow::openUrls,
            Qt::QueuedConnection);
    const int index = m_tabs->addTab(document, document->displayName());
    updateTab(document);
    m_tabs->setCurrentIndex(index);
    return document;
}

int MainWindow::findTab(const QUrl &url) const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        const Document *document = documentAt(i);
        if (!document->isUntitled() && document->location() == url)
            return i;
    }
    return -1;
}

bool MainWindow::closeTab(int index)
{
    QPointer<Document> document = documentAt(index);
    if (!document || !maybeSave(document))
        return false;
    if (!document)
        return true;
    // Tabs may have moved while the question was showing.
    m_tabs->removeTab(m_tabs->indexOf(document));
    // Deleted later, so nothing still running for it touches a dead tab.
    document->deleteLater();
    if (m_tabs->count() == 0)
        newTab()->setFocus();
    return true;
}

bool MainWindow::closeAllTabs()
{
    for (Document *document : documents()) {
        m_tabs->removeTab(m_tabs->indexOf(document));
        document->deleteLater();
    }
    newTab()->setFocus();
    return true;
}

void MainWindow::currentTabChanged()
{
    Document *document = currentDocument();
    if (!document)
        return;
    for (const QMetaObject::Connection &connection : std::as_const(m_editorConnections))
        disconnect(connection);
    m_editorConnections.clear();

    // Menu commands and the find bar act on the current tab.
    CodeEditor *editor = document->editor();
    m_findBar->setEditor(editor);
    m_undoAction->setEnabled(editor->document()->isUndoAvailable());
    m_redoAction->setEnabled(editor->document()->isRedoAvailable());
    m_editorConnections << connect(editor, &QPlainTextEdit::undoAvailable, m_undoAction, &QAction::setEnabled)
                        << connect(editor, &QPlainTextEdit::redoAvailable, m_redoAction, &QAction::setEnabled)
                        << connect(editor, &QPlainTextEdit::cursorPositionChanged, this,
                                   &MainWindow::updateCursorPosition);
    updateLanguageUi(document->highlighter()->language());
    updateCursorPosition();
    updateWindowState();
    updatePreview();
}

void MainWindow::updateTab(Document *document)
{
    const int index = m_tabs->indexOf(document);
    if (index < 0)
        return;
    QString text = escapeMnemonic(document->displayName());
    if (document->isModified())
        text += QStringLiteral(" •");
    m_tabs->setTabText(index, text);
    m_tabs->setTabToolTip(index, plainToolTip(document->displayLocation()));
    if (document == currentDocument())
        updateWindowState();
}

void MainWindow::updateWindowState()
{
    const Document *document = currentDocument();
    if (!document)
        return;
    if (document->isRemote()) {
        setWindowFilePath(QString());
        setWindowTitle(QStringLiteral("%1 — %2[*]")
                           .arg(document->displayName(),
                                ServerAddress::fromUrl(document->location()).displayName()));
    } else {
        setWindowTitle(QString());
        setWindowFilePath(document->isUntitled() ? tr("Untitled") : Location::path(document->location()));
    }
    setWindowModified(document->isModified());
    m_lineEndingLabel->setText(
        document->isLoaded() ? (document->latin1() ? QStringLiteral("Latin-1  ") : QStringLiteral("UTF-8  "))
                                   + (document->crlf() ? "CRLF" : "LF")
                             : QString());
}

// --- Waiting for file operations ---------------------------------------------

template<typename T>
T MainWindow::waitFor(QFuture<T> future, const QString &label, FileSystem *fileSystem)
{
    auto cancelled = [] {
        T result;
        result.error = FsError::Cancelled;
        result.message = tr("Cancelled.");
        return result;
    };

    if (!future.isFinished()) {
        ++m_busy;
        QFutureWatcher<T> watcher;
        QEventLoop loop;
        connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(future);
        // Most operations finish quickly. Until the progress dialog appears,
        // ignore input so nothing else starts meanwhile; the window still
        // repaints, and login dialogs still work.
        QTimer::singleShot(ProgressDelayMs, &loop, &QEventLoop::quit);
        if (!future.isFinished())
            loop.exec(QEventLoop::ExcludeUserInputEvents);

        if (!future.isFinished()) {
            QProgressDialog progress(this);
            // The label shows a file name, so it must not be read as HTML.
            auto *labelWidget = new QLabel(label, &progress);
            labelWidget->setTextFormat(Qt::PlainText);
            progress.setLabel(labelWidget);
            progress.setCancelButtonText(tr("Cancel"));
            progress.setRange(0, 0);
            progress.setWindowModality(Qt::WindowModal);
            progress.setMinimumDuration(0);
            progress.setAutoClose(false);
            progress.setAutoReset(false);
            connect(&watcher, &QFutureWatcherBase::progressRangeChanged, &progress,
                    &QProgressDialog::setRange);
            connect(&watcher, &QFutureWatcherBase::progressValueChanged, &progress,
                    &QProgressDialog::setValue);
            connect(&watcher, &QFutureWatcherBase::finished, &progress, &QProgressDialog::accept);
            if (!future.isFinished())
                progress.exec();
            if (!future.isFinished()) {
                // Cancelled: stop the operation, but don't wait for it.
                if (fileSystem)
                    fileSystem->cancel();
                --m_busy;
                return cancelled();
            }
        }
        --m_busy;
    }
    if (future.isCanceled() || future.resultCount() == 0)
        return cancelled();
    return future.result();
}

// --- Opening -----------------------------------------------------------------

std::shared_ptr<FileSystem> MainWindow::fileSystemFor(const QUrl &url)
{
    if (Location::isLocal(url))
        return m_localFs;
#ifdef TEXTEDITOR_HAVE_SFTP
    if (Location::isRemote(url))
        return m_connections->fileSystem(ServerAddress::fromUrl(url));
#endif
    showMessage(this, QMessageBox::Warning, tr("Open"),
                Location::isRemote(url)
                    ? tr("This copy of TextEditor+ was built without SFTP support, so it can't "
                         "open %1.").arg(Location::displayString(url))
                    : tr("TextEditor+ can't open %1.").arg(Location::displayString(url)));
    return nullptr;
}

QUrl MainWindow::resolveHome(const QUrl &url, FileSystem *fileSystem)
{
#ifdef TEXTEDITOR_HAVE_SFTP
    const QString path = Location::path(url);
    auto *sftp = qobject_cast<SftpFileSystem *>(fileSystem);
    if (!sftp || !(path.isEmpty() || path == QLatin1String("~") || path.startsWith(QLatin1String("~/"))))
        return url;
    const ServerAddress server = ServerAddress::fromUrl(url);
    const FsStatus status = waitFor(sftp->connection()->ensureConnected(),
                                    tr("Connecting to %1…").arg(server.displayName()), sftp);
    if (!status.ok()) {
        if (status.error != FsError::Cancelled)
            showMessage(this, QMessageBox::Warning, tr("Connect to Server"), status.message);
        return {};
    }
    const QString home = sftp->connection()->homeFolder();
    const QString resolved = path.size() <= 1 ? home : Location::joinRemotePath(home, path.mid(2));
    return server.url(resolved);
#else
    Q_UNUSED(fileSystem);
    return url;
#endif
}

void MainWindow::open()
{
    const Document *document = currentDocument();
    const QString dir = document && Location::isLocal(document->location())
        ? QFileInfo(Location::path(document->location())).absolutePath()
        : (m_sidebar->hasFolder() && Location::isLocal(m_sidebar->folderUrl())
               ? Location::path(m_sidebar->folderUrl())
               : QString());
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open File"), dir, tr(FileFilters));
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls << Location::fromLocalPath(path);
    openUrls(urls);
}

void MainWindow::openUrls(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls)
        openLocation(url);
}

bool MainWindow::openLocation(const QUrl &input)
{
    const QUrl url = Location::normalized(input);
    if (!url.isValid()) {
        showMessage(this, QMessageBox::Warning, tr("Open File"),
                    tr("TextEditor+ can't open %1.").arg(Location::stripPassword(input).toString()));
        return false;
    }
    const int existing = findTab(url);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        Document *document = documentAt(existing);
        return document->isLoaded() || loadInto(document, url);
    }
    if (Location::isLocal(url) && QFileInfo(Location::path(url)).isDir())
        return openFolderUrl(url);
    Document *current = currentDocument();
    return loadInto(current && current->isPristine() ? current : nullptr, url);
}

bool MainWindow::loadInto(Document *targetDocument, const QUrl &requested)
{
    const QPointer<Document> target(targetDocument);
    const QString title = tr("Open File");
    const std::shared_ptr<FileSystem> fs = fileSystemFor(requested);
    if (!fs)
        return false;
    const QUrl url = resolveHome(requested, fs.get());
    if (!url.isValid())
        return false;
    const QString display = Location::displayString(url);
    const QString path = Location::path(url);

    // Only open regular files: reading a device such as /dev/zero or a FIFO
    // would never finish.
    const StatResult st = waitFor(fs->stat(path), tr("Opening %1…").arg(display), fs.get());
    if (!st.ok()) {
        if (st.error != FsError::Cancelled)
            showMessage(this, QMessageBox::Warning, title, st.message);
        return false;
    }
    if (!st.info.exists()) {
        showMessage(this, QMessageBox::Warning, title, tr("%1 does not exist.").arg(display));
        return false;
    }
    if (st.info.isFolder() && !target)
        return openFolderUrl(url);
    if (!st.info.isFile()) {
        showMessage(this, QMessageBox::Warning, title, tr("%1 is not a regular file.").arg(display));
        return false;
    }
    if (st.info.size > FileLimits::MaxFileSize) {
        showMessage(this, QMessageBox::Warning, title,
                    tr("%1 is too large to open (%2; the limit is %3).")
                        .arg(display, megabytes(st.info.size), megabytes(FileLimits::MaxFileSize)));
        return false;
    }
    if (st.info.size > FileLimits::LargeFileSize
        && showMessage(this, QMessageBox::Question, title,
                       tr("%1 is %2. Large files can make the editor slow. Open it anyway?")
                           .arg(display, megabytes(st.info.size)),
                       QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel)
               != QMessageBox::Open) {
        return false;
    }

    // The read is capped too, in case the file grew after the checks above.
    const ReadResult read = waitFor(fs->read(path, FileLimits::MaxFileSize),
                                    tr("Opening %1…").arg(display), fs.get());
    if (!read.ok()) {
        if (read.error != FsError::Cancelled)
            showMessage(this, QMessageBox::Warning, title, read.message);
        return false;
    }

    const DecodedText decoded = decodeText(read.data);
    if (decoded.isBinary
        && showMessage(this, QMessageBox::Warning, title,
                       tr("%1 appears to be a binary file. Saving it from a text editor "
                          "may damage it. Open it anyway?").arg(display),
                       QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel)
               != QMessageBox::Open) {
        return false;
    }

    Document *document = target ? target.data() : newTab();
    document->setContent(url, decoded, read.info, read.resolvedPath);
    document->setFileSystem(fs);
    m_tabs->setCurrentWidget(document);
    currentTabChanged();
    document->setFocus();
    addRecent(url, RecentKind::File);

    if (!read.resolvedPath.isEmpty())
        statusBar()->showMessage(tr("%1 is a symbolic link: saving writes to %2")
                                     .arg(document->displayName(), read.resolvedPath), 10000);
    else if (decoded.latin1)
        statusBar()->showMessage(tr("Not valid UTF-8: opened as Latin-1"), 8000);
    return true;
}

void MainWindow::openRequested(const QUrl &url)
{
    // A file can arrive from the Dock or Finder while a dialog is open, such
    // as Save As, or while a file is loading. Opening it then would change
    // tabs under the dialog, so wait until it has closed.
    if (QApplication::activeModalWidget() || m_busy > 0) {
        QTimer::singleShot(250, this, [this, url] { openRequested(url); });
        return;
    }
    reveal();
    raise();
    activateWindow();
    openLocation(url);
}

void MainWindow::reveal()
{
    if (isMinimized())
        showNormal();
    else if (!isVisible())
        show();
}

// --- Saving ------------------------------------------------------------------

bool MainWindow::save(Document *document)
{
    if (!document || !document->isLoaded())
        return false;
    return document->isUntitled() ? saveAs(document)
                                  : writeDocument(document, document->location(), true);
}

bool MainWindow::saveAs(Document *document)
{
    if (!document || !document->isLoaded())
        return false;
#ifdef TEXTEDITOR_HAVE_SFTP
    if (document->isRemote())
        return saveToServer(document);
#endif
    return saveAsLocal(document);
}

bool MainWindow::saveAsLocal(Document *document)
{
    const QString suggested = document->isUntitled() || !Location::isLocal(document->location())
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/')
              + (document->isUntitled() ? QStringLiteral("untitled.txt") : document->displayName())
        : Location::path(document->location());
    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), suggested, tr(FileFilters));
    if (path.isEmpty())
        return false;
    const QUrl url = Location::fromLocalPath(path);
    if (!writeDocument(document, url, url == document->location()))
        return false;

    // Pick up highlighting from the new name, but keep a manual choice when
    // the name says nothing (e.g. notes.txt).
    const Language detected = Highlighter::languageForFile(path);
    if (detected != Language::PlainText) {
        document->highlighter()->setLanguage(detected);
        if (document == currentDocument()) {
            updateLanguageUi(detected);
            updatePreview();
        }
    }
    return true;
}

bool MainWindow::saveToServer(Document *document)
{
#ifdef TEXTEDITOR_HAVE_SFTP
    if (!document || !document->isLoaded())
        return false;
    ServerAddress server;
    QString folder;
    if (document->isRemote()) {
        server = ServerAddress::fromUrl(document->location());
        folder = Location::remoteParent(Location::path(document->location()));
    } else if (!askForServer(tr("Save to Server"), tr("Continue"), &server, &folder)) {
        return false;
    }
    const std::shared_ptr<FileSystem> fs = fileSystemFor(server.url());
    if (!fs)
        return false;
    const QUrl start = resolveHome(server.url(folder), fs.get());
    if (!start.isValid())
        return false;

    RemoteFileDialog dialog(fs, tr("Save to %1").arg(server.displayName()), Location::path(start),
                            RemoteFileDialog::SaveFile, this);
    dialog.setFileName(document->isUntitled() ? QStringLiteral("untitled.txt") : document->displayName());
    const int result = dialog.exec();
    if (result == RemoteFileDialog::SaveLocally)
        return saveAsLocal(document);
    if (result != QDialog::Accepted)
        return false;

    const QUrl url = server.url(dialog.selectedPath());
    if (!writeDocument(document, url, url == document->location()))
        return false;
    const Language detected = Highlighter::languageForFile(Location::fileName(url));
    if (detected != Language::PlainText) {
        document->highlighter()->setLanguage(detected);
        if (document == currentDocument()) {
            updateLanguageUi(detected);
            updatePreview();
        }
    }
    return true;
#else
    Q_UNUSED(document);
    return false;
#endif
}

bool MainWindow::writeDocument(Document *documentIn, const QUrl &url, bool sameFile)
{
    const QPointer<Document> document(documentIn);
    const QString title = tr("Save File");
    const QString text = document->editor()->toPlainText();

    // Save in the encoding the file was opened with. Latin-1 can't hold every
    // character, so ask before switching such a file to UTF-8.
    bool latin1 = document->latin1();
    if (latin1 && !fitsLatin1(text)) {
        if (showMessage(this, QMessageBox::Question, title,
                        tr("This file uses the Latin-1 encoding, which can't store some of "
                           "the characters it now contains. Save it as UTF-8 instead?"),
                        QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save)
            != QMessageBox::Save) {
            return false;
        }
        latin1 = false;
    }
    const QByteArray bytes = encodeText(text, document->crlf(), latin1);

    const std::shared_ptr<FileSystem> fs = fileSystemFor(url);
    if (!fs)
        return false;
    const QString path = sameFile ? document->writePath() : Location::path(url);
    const QString resolvedPath = sameFile ? document->resolvedPath() : QString();
    // Saving over the file it came from: check nobody else changed it. A
    // new location was already confirmed in the save dialog.
    WriteExpectation expected = sameFile && document->savedVersion().exists()
        ? WriteExpectation::mustMatch(document->savedVersion())
        : WriteExpectation::anything();

    for (;;) {
        const WriteResult written = waitFor(fs->writeAtomic(path, bytes, expected),
                                            tr("Saving %1…").arg(Location::displayString(url)), fs.get());
        if (!document)
            return false;
        if (written.error == FsError::Conflict) {
            QMessageBox box(QMessageBox::Warning, title,
                            written.message + QStringLiteral("\n\n")
                                + tr("Replace it with your version, or reload it and lose your changes?"),
                            QMessageBox::NoButton, this);
            box.setTextFormat(Qt::PlainText);
            QPushButton *replace = box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
            QPushButton *reload = box.addButton(tr("Reload"), QMessageBox::ActionRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() == replace) {
                expected = WriteExpectation::anything();
                continue;
            }
            if (box.clickedButton() == reload && document)
                loadInto(document, document->location());
            return false;
        }
        if (!written.ok()) {
            if (written.error != FsError::Cancelled)
                showMessage(this, QMessageBox::Warning, title, written.message);
            return false;
        }
        document->markSaved(url, latin1, written.info, resolvedPath);
        document->setFileSystem(fs);
        addRecent(url, RecentKind::File);
        updateWindowState();
        statusBar()->showMessage(tr("Saved %1").arg(document->displayName()), 2000);
        return true;
    }
}

bool MainWindow::maybeSave(Document *document)
{
    if (!document || !document->isModified())
        return true;

    m_tabs->setCurrentWidget(document);
    const auto answer = showMessage(
        this, QMessageBox::Warning, tr("Unsaved Changes"),
        tr("Do you want to save the changes you made to \"%1\"?").arg(document->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save(document);
    return answer == QMessageBox::Discard;
}

void MainWindow::print()
{
    const Document *document = currentDocument();
    const CodeEditor *editor = document->editor();
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(document->displayName());

    QPrintDialog dialog(&printer, this);
    if (editor->textCursor().hasSelection())
        dialog.setOption(QAbstractPrintDialog::PrintSelection);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Print a copy highlighted with the light theme, so dark-theme colours
    // never end up on white paper.
    const bool selectionOnly = printer.printRange() == QPrinter::Selection;
    QTextDocument copy;
    copy.setDefaultFont(editor->font());
    copy.setPlainText(selectionOnly ? editor->textCursor().selection().toPlainText()
                                    : editor->toPlainText());
    Highlighter highlighter(&copy);
    highlighter.setTheme(Theme::light());
    highlighter.setLanguage(document->highlighter()->language());
    copy.print(&printer);
}

// --- Folders and servers -------------------------------------------------------

void MainWindow::openFolder()
{
    const QString start = m_sidebar->hasFolder() && Location::isLocal(m_sidebar->folderUrl())
        ? Location::path(m_sidebar->folderUrl())
        : QString();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Folder"), start);
    if (!dir.isEmpty())
        openFolderUrl(Location::fromLocalPath(dir));
}

bool MainWindow::openFolderUrl(const QUrl &requested)
{
    const QString title = tr("Open Folder");
    const QUrl normalized = Location::normalized(requested);
    if (!normalized.isValid())
        return false;
    if (Location::isLocal(normalized)) {
        if (!QFileInfo(Location::path(normalized)).isDir()) {
            showMessage(this, QMessageBox::Warning, title,
                        tr("%1 is not a folder.").arg(Location::displayString(normalized)));
            return false;
        }
        m_sidebar->openLocalFolder(normalized, m_localFs);
    } else {
        const std::shared_ptr<FileSystem> fs = fileSystemFor(normalized);
        if (!fs)
            return false;
        const QUrl url = resolveHome(normalized, fs.get());
        if (!url.isValid())
            return false;
        const StatResult st = waitFor(fs->stat(Location::path(url)),
                                      tr("Opening %1…").arg(Location::displayString(url)), fs.get());
        if (!st.ok() || !st.info.isFolder()) {
            if (st.error != FsError::Cancelled)
                showMessage(this, QMessageBox::Warning, title,
                            st.ok() ? tr("%1 is not a folder.").arg(Location::displayString(url)) : st.message);
            return false;
        }
        m_sidebar->openRemoteFolder(url, fs);
        addRecent(url, RecentKind::Folder);
        m_sidebar->show();
        m_sidebar->raise();
        return true;
    }
    addRecent(normalized, RecentKind::Folder);
    m_sidebar->show();
    m_sidebar->raise();
    return true;
}

void MainWindow::closeFolder()
{
    m_sidebar->closeFolder();
}

bool MainWindow::askForServer(const QString &title, const QString &acceptText,
                              ServerAddress *server, QString *folder)
{
#ifdef TEXTEDITOR_HAVE_SFTP
    ConnectDialog dialog(title, acceptText, this);
    // Start from the server already in use, if any.
    if (m_sidebar->hasFolder() && Location::isRemote(m_sidebar->folderUrl())) {
        dialog.setServer(ServerAddress::fromUrl(m_sidebar->folderUrl()),
                         Location::path(m_sidebar->folderUrl()));
    } else if (const Document *document = currentDocument(); document && document->isRemote()) {
        dialog.setServer(ServerAddress::fromUrl(document->location()),
                         Location::remoteParent(Location::path(document->location())));
    }
    if (dialog.exec() != QDialog::Accepted)
        return false;
    *server = dialog.server();
    *folder = dialog.folder();
    return true;
#else
    Q_UNUSED(title);
    Q_UNUSED(acceptText);
    Q_UNUSED(server);
    Q_UNUSED(folder);
    return false;
#endif
}

void MainWindow::connectToServer()
{
    ServerAddress server;
    QString folder;
    if (askForServer(tr("Connect to Server"), tr("Connect"), &server, &folder))
        openFolderUrl(server.url(folder));
}

void MainWindow::openRemoteFile()
{
    ServerAddress server;
    QString folder;
    if (!askForServer(tr("Open Remote File"), tr("Continue"), &server, &folder))
        return;
    const std::shared_ptr<FileSystem> fs = fileSystemFor(server.url());
    if (!fs)
        return;
    const QUrl start = resolveHome(server.url(folder), fs.get());
    if (!start.isValid())
        return;
    RemoteFileDialog dialog(fs, tr("Open File on %1").arg(server.displayName()), Location::path(start),
                            RemoteFileDialog::OpenFile, this);
    if (dialog.exec() == QDialog::Accepted)
        openLocation(server.url(dialog.selectedPath()));
}

void MainWindow::openRemoteFolder()
{
    ServerAddress server;
    QString folder;
    if (!askForServer(tr("Open Remote Folder"), tr("Continue"), &server, &folder))
        return;
    const std::shared_ptr<FileSystem> fs = fileSystemFor(server.url());
    if (!fs)
        return;
    const QUrl start = resolveHome(server.url(folder), fs.get());
    if (!start.isValid())
        return;
    RemoteFileDialog dialog(fs, tr("Open Folder on %1").arg(server.displayName()), Location::path(start),
                            RemoteFileDialog::ChooseFolder, this);
    if (dialog.exec() == QDialog::Accepted)
        openFolderUrl(server.url(dialog.selectedPath()));
}

void MainWindow::followRename(const QUrl &from, const QUrl &to)
{
    // Tabs showing the renamed file, or files inside a renamed folder,
    // follow it.
    const QString fromPath = Location::path(from);
    for (Document *document : documents()) {
        if (document->isUntitled() || !Location::isInside(document->location(), from))
            continue;
        const QString suffix = Location::path(document->location()).mid(fromPath.size());
        QUrl moved = to;
        if (Location::isLocal(to))
            moved = Location::fromLocalPath(Location::path(to) + suffix);
        else
            moved.setPath(to.path() + suffix);
        document->relocate(moved);
    }
    updateWindowState();
}

// --- Recent files ----------------------------------------------------------------

void MainWindow::addRecent(const QUrl &url, RecentKind kind)
{
    QSettings settings;
    QStringList recent = settings.value("recent").toStringList();
    // Never a password, though normalized URLs don't carry one anyway.
    const QString entry = (kind == RecentKind::File ? QStringLiteral("file ") : QStringLiteral("folder "))
        + Location::stripPassword(url).toString(QUrl::RemovePassword);
    recent.removeAll(entry);
    recent.prepend(entry);
    while (recent.size() > MaxRecentItems)
        recent.removeLast();
    settings.setValue("recent", recent);
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList recent = QSettings().value("recent").toStringList();
    for (const QString &entry : recent) {
        const qsizetype space = entry.indexOf(QLatin1Char(' '));
        if (space < 0)
            continue;
        const bool folder = entry.left(space) == QLatin1String("folder");
        const QUrl url = Location::normalized(QUrl(entry.mid(space + 1)));
        if (!url.isValid())
            continue;
        QString text = Location::displayString(url);
        if (folder)
            text = tr("%1 (folder)").arg(text);
        QAction *action = m_recentMenu->addAction(escapeMnemonic(text));
        connect(action, &QAction::triggered, this, &MainWindow::reveal);
        connect(action, &QAction::triggered, this, [this, url, folder] {
            if (folder)
                openFolderUrl(url);
            else
                openLocation(url);
        });
    }
    if (m_recentMenu->isEmpty())
        m_recentMenu->addAction(tr("No Recent Items"))->setEnabled(false);
    m_recentMenu->addSeparator();
    QAction *clear = m_recentMenu->addAction(tr("Clear Menu"));
    clear->setEnabled(!recent.isEmpty());
    connect(clear, &QAction::triggered, this, [] { QSettings().remove("recent"); });
}

// --- Events --------------------------------------------------------------------

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::FileOpen) {
        const auto *openEvent = static_cast<QFileOpenEvent *>(event);
        const QUrl url = openEvent->url().isValid() ? openEvent->url()
                                                    : Location::fromLocalPath(openEvent->file());
        // A folder dropped on the Dock icon opens in the sidebar.
        if (url.isValid() && !url.isEmpty())
            openRequested(url);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (Document *document : documents()) {
        if (!maybeSave(document)) {
            event->ignore();
            return;
        }
    }
    writeSettings();
    event->accept();
#ifdef Q_OS_MACOS
    // The app keeps running, so start afresh the next time the window shows.
    // This also releases any server connections.
    closeAllTabs();
    m_sidebar->closeFolder();
#endif
}

// --- Editor settings -------------------------------------------------------------

void MainWindow::setLanguage(Language language)
{
    if (Document *document = currentDocument())
        document->highlighter()->setLanguage(language);
    updateLanguageUi(language);
    updatePreview();
}

void MainWindow::updateLanguageUi(Language language)
{
    const QSignalBlocker blocker(m_languageCombo);
    m_languageCombo->setCurrentIndex(m_languageCombo->findData(int(language)));
    for (QAction *action : m_languageGroup->actions())
        action->setChecked(action->data().toInt() == int(language));
}

void MainWindow::applyEditorSettings(Document *document)
{
    QFont font(m_fontFamily);
    font.setPointSize(m_fontSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    document->editor()->setEditorFont(font);
    document->editor()->setLineWrapMode(m_wordWrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    document->editor()->setLineNumbersVisible(m_preferences.lineNumbers);
    const Theme theme = m_preferences.darkTheme ? Theme::dracula() : Theme::light();
    document->editor()->setTheme(theme);
    document->highlighter()->setTheme(theme);
    document->highlighter()->setSpellCheck(m_preferences.spellCheck);
}

void MainWindow::setWordWrap(bool on)
{
    m_wordWrap = on;
    m_wrapAction->setChecked(on);
    for (Document *document : documents())
        document->editor()->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void MainWindow::setDarkTheme(bool dark)
{
    m_preferences.darkTheme = dark;
    m_darkAction->setChecked(dark);
    // The button shows what clicking it changes to.
    m_darkAction->setIcon(toolBarIcon(dark ? QStringLiteral("theme-sun") : QStringLiteral("theme-moon")));
    const Theme theme = dark ? Theme::dracula() : Theme::light();
    for (Document *document : documents()) {
        document->editor()->setTheme(theme);
        document->highlighter()->setTheme(theme);
    }
    m_preview->setTheme(theme);
}

QList<ToolBarButton> MainWindow::toolBarButtons() const
{
    QList<ToolBarButton> result;
    for (const auto &[id, action] : m_hideableButtons) {
        QString name = action->iconText();
        if (id == QLatin1String("zoomIn"))
            name = tr("Zoom In (A+)");
        else if (id == QLatin1String("zoomOut"))
            name = tr("Zoom Out (A−)");
        const QIcon icon = id == QLatin1String("theme") ? toolBarIcon(QStringLiteral("theme-moon")) : action->icon();
        result << ToolBarButton{id, name, icon};
    }
    return result;
}

void MainWindow::rebuildToolBar()
{
    for (QAction *action : m_toolBar->actions()) {
        m_toolBar->removeAction(action);
        // Separators belong to the toolbar; everything else is kept.
        if (action->isSeparator() && action->parent() == m_toolBar)
            delete action;
    }
    auto hidden = [this](QAction *action) {
        for (const auto &[id, button] : std::as_const(m_hideableButtons)) {
            if (button == action)
                return m_preferences.hiddenButtons.contains(id);
        }
        return false;
    };
    for (const QList<QAction *> &group : std::as_const(m_toolBarGroups)) {
        QList<QAction *> shown;
        for (QAction *action : group) {
            if (!hidden(action))
                shown << action;
        }
        if (shown.isEmpty())
            continue;
        if (!m_toolBar->actions().isEmpty())
            m_toolBar->addSeparator();
        m_toolBar->addActions(shown);
    }
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(m_preferences, toolBarButtons(), SpellChecker::instance()->isAvailable(), this);
    connect(&dialog, &SettingsDialog::changed, this, &MainWindow::applyPreferences);
    dialog.exec();
}

void MainWindow::applyPreferences(const Preferences &preferences)
{
    const bool toolBarChanged = preferences.hiddenButtons != m_preferences.hiddenButtons;
    m_preferences.toolButtonStyle = preferences.toolButtonStyle;
    m_preferences.hiddenButtons = preferences.hiddenButtons;
    m_preferences.lineNumbers = preferences.lineNumbers;
    m_preferences.spellCheck = preferences.spellCheck;
    m_toolBar->setToolButtonStyle(preferences.toolButtonStyle);
    if (toolBarChanged)
        rebuildToolBar();
    if (preferences.darkTheme != m_preferences.darkTheme)
        setDarkTheme(preferences.darkTheme);
    for (Document *document : documents()) {
        document->editor()->setLineNumbersVisible(preferences.lineNumbers);
        document->highlighter()->setSpellCheck(preferences.spellCheck);
    }
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
    for (Document *document : documents())
        document->editor()->setEditorFont(font);

    const QSignalBlocker blocker(m_fontCombo);
    m_fontCombo->setCurrentFont(font);
}

void MainWindow::goToLine()
{
    CodeEditor *editor = currentDocument()->editor();
    const int lines = editor->document()->blockCount();
    bool ok = false;
    const int line = QInputDialog::getInt(this, tr("Go to Line"), tr("Line (1–%1):").arg(lines),
                                          editor->textCursor().blockNumber() + 1, 1, lines, 1,
                                          &ok);
    if (!ok)
        return;
    editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(line - 1)));
    editor->centerCursor();
    editor->setFocus();
}

void MainWindow::updateCursorPosition()
{
    const Document *document = currentDocument();
    if (!document)
        return;
    const QTextCursor cursor = document->editor()->textCursor();
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
    restoreState(settings.value("windowState").toByteArray());
    m_sidebar->setShowHidden(settings.value("sidebar/showHidden", false).toBool());
    m_previewWanted = settings.value("preview/show", true).toBool();

    m_fontFamily = settings.value("fontFamily",
                                  QFontDatabase::systemFont(QFontDatabase::FixedFont).family())
                       .toString();
    m_fontSize = qBound(MinFontSize, settings.value("fontSize", DefaultFontSize).toInt(),
                        MaxFontSize);
    applyFont();
    setWordWrap(settings.value("wordWrap", true).toBool());

    Preferences preferences;
    preferences.toolButtonStyle = buttonStyleFromName(settings.value("toolbar/buttonStyle").toString());
    preferences.hiddenButtons = settings.value("toolbar/hidden").toStringList();
    preferences.darkTheme = settings.value("darkTheme", true).toBool();
    preferences.lineNumbers = settings.value("lineNumbers", true).toBool();
    preferences.spellCheck = settings.value("spellCheck", true).toBool();
    setDarkTheme(preferences.darkTheme);
    applyPreferences(preferences);
}

void MainWindow::writeSettings() const
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.setValue("sidebar/showHidden", m_sidebar->showHidden());
    settings.setValue("preview/show", m_previewWanted);
    settings.setValue("fontFamily", m_fontFamily);
    settings.setValue("fontSize", m_fontSize);
    settings.setValue("wordWrap", m_wrapAction->isChecked());
    settings.setValue("darkTheme", m_darkAction->isChecked());
    settings.setValue("toolbar/buttonStyle", buttonStyleName(m_preferences.toolButtonStyle));
    settings.setValue("toolbar/hidden", m_preferences.hiddenButtons);
    settings.setValue("lineNumbers", m_preferences.lineNumbers);
    settings.setValue("spellCheck", m_preferences.spellCheck);
    // Earlier versions reopened the last session's tabs; forget them.
    settings.remove("session");
}
