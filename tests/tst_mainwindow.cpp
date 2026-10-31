// Drives the main window without a screen (QT_QPA_PLATFORM=offscreen): tabs,
// opening and saving, the safeguards, and starting blank.

#include "codeeditor.h"
#include "document.h"
#include "foldersidebar.h"
#include "location.h"
#include "mainwindow.h"
#include "messagebox.h"
#include "preview.h"
#include "settingsdialog.h"
#include "spellchecker.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextLayout>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QMenu>
#include <QWidgetAction>

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Answers the next message box with the button that has the given role or
// standard button, once it appears.
void answerNextMessageBox(QMessageBox::StandardButton button, QString *text = nullptr)
{
    QTimer::singleShot(50, [button, text] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) {
            answerNextMessageBox(button, text);
            return;
        }
        if (text)
            *text = box->text();
        if (QAbstractButton *b = box->button(button))
            b->click();
        else
            box->reject();
    });
}

void answerNextMessageBoxWith(const QString &buttonText)
{
    QTimer::singleShot(50, [buttonText] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) {
            answerNextMessageBoxWith(buttonText);
            return;
        }
        for (QAbstractButton *b : box->buttons()) {
            if (b->text() == buttonText) {
                b->click();
                return;
            }
        }
        box->reject();
    });
}

} // namespace

class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        // Keep the tests' settings away from the real ones.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.filePath(QStringLiteral("settings")));
        QCoreApplication::setOrganizationName(QStringLiteral("sjwrc-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("TextEditor+ tests"));
    }

    void init()
    {
        QSettings().clear();
        m_window = std::make_unique<MainWindow>();
        m_window->show();
        m_tabs = m_window->findChild<QTabWidget *>();
        QVERIFY(m_tabs);
    }

    void cleanup()
    {
        // Discard anything left unsaved.
        for (int i = 0; i < m_tabs->count(); ++i)
            document(i)->setModified(false);
        m_window.reset();
    }

    void startsWithOneUntitledTab()
    {
        QCOMPARE(m_tabs->count(), 1);
        QVERIFY(document(0)->isPristine());
        QVERIFY(m_window->windowFilePath().contains(QLatin1String("Untitled")));
    }

    // The text area, not the font box in the toolbar, has the keyboard
    // focus when the window first appears.
    void startsWithFocusInEditor()
    {
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
        QCOMPARE(QApplication::focusWidget(), document(0)->editor());
    }

    void openReusesUntitledTabAndSwitches()
    {
        const QString a = path(QStringLiteral("a.cpp"));
        const QString b = path(QStringLiteral("b.txt"));
        writeFile(a, "int main() {}\n");
        writeFile(b, "notes\n");

        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(a)));
        QCOMPARE(m_tabs->count(), 1); // the untitled tab was reused
        QCOMPARE(document(0)->editor()->toPlainText(), QStringLiteral("int main() {}\n"));
        QCOMPARE(document(0)->highlighter()->language(), Language::Cpp);
        QCOMPARE(m_tabs->tabToolTip(0), plainToolTip(QDir::toNativeSeparators(a)));

        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(b)));
        QCOMPARE(m_tabs->count(), 2);
        QCOMPARE(m_tabs->currentIndex(), 1);
        QCOMPARE(m_window->windowFilePath(), b);

        // Opening it again switches to its tab instead of opening another.
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(a)));
        QCOMPARE(m_tabs->count(), 2);
        QCOMPARE(m_tabs->currentIndex(), 0);
    }

    // A file name that looks like HTML is shown as text everywhere.
    void htmlLookingNamesStayPlain()
    {
        const QString name = QStringLiteral("<b>bold<img src=x>.txt");
        const QString a = path(name);
        writeFile(a, "x");
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(a)));
        QVERIFY(m_tabs->tabText(0).contains(QLatin1String("<b>")));
        QVERIFY(m_tabs->tabToolTip(0).contains(QLatin1String("&lt;b&gt;")));
        QVERIFY(!m_tabs->tabToolTip(0).contains(QLatin1String("<b>")));
        QCOMPARE(plainToolTip(QStringLiteral("a&b")), QStringLiteral("<p style='white-space:pre'>a&amp;b</p>"));
    }

    void modifiedMarker()
    {
        const QString a = path(QStringLiteral("m.txt"));
        writeFile(a, "x");
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(a)));
        document(0)->editor()->insertPlainText(QStringLiteral("y"));
        QVERIFY(m_tabs->tabText(0).endsWith(QStringLiteral("•")));
        QVERIFY(m_window->isWindowModified());
    }

    void saveKeepsLineEndingsAndEncoding()
    {
        const QString crlf = path(QStringLiteral("crlf.txt"));
        writeFile(crlf, "one\r\ntwo\r\n");
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(crlf)));
        CodeEditor *editor = document(0)->editor();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("three\n"));
        triggerAction(QStringLiteral("&Save"));
        QCOMPARE(readFile(crlf), QByteArray("one\r\ntwo\r\nthree\r\n"));
        QVERIFY(!document(0)->isModified());

        const QString latin1 = path(QStringLiteral("latin1.txt"));
        writeFile(latin1, "caf\xe9\n");
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(latin1)));
        Document *doc = document(m_tabs->currentIndex());
        QVERIFY(doc->latin1());
        doc->editor()->moveCursor(QTextCursor::End);
        doc->editor()->insertPlainText(QStringLiteral("ok"));
        triggerAction(QStringLiteral("&Save"));
        QCOMPARE(readFile(latin1), QByteArray("caf\xe9\nok"));

        // A character Latin-1 can't hold asks before switching to UTF-8.
        doc->editor()->insertPlainText(QStringLiteral("€"));
        answerNextMessageBox(QMessageBox::Save);
        triggerAction(QStringLiteral("&Save"));
        QCOMPARE(readFile(latin1), QByteArray("caf\xc3\xa9\nok\xe2\x82\xac"));
        QVERIFY(!doc->latin1());
    }

    void conflictIsDetected()
    {
        const QString a = path(QStringLiteral("conflict.txt"));
        writeFile(a, "original");
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(a)));
        QTest::qWait(1100);
        writeFile(a, "changed by another program");
        document(0)->editor()->insertPlainText(QStringLiteral("mine "));

        QString message;
        answerNextMessageBox(QMessageBox::Cancel, &message);
        triggerAction(QStringLiteral("&Save"));
        QVERIFY2(message.contains(QLatin1String("changed")), qPrintable(message));
        QCOMPARE(readFile(a), QByteArray("changed by another program"));
        QVERIFY(document(0)->isModified());

        answerNextMessageBoxWith(QStringLiteral("Replace"));
        triggerAction(QStringLiteral("&Save"));
        QCOMPARE(readFile(a), QByteArray("mine original"));
    }

    void binaryAndMissingFiles()
    {
        const QString binary = path(QStringLiteral("bin.dat"));
        writeFile(binary, QByteArray("a\0b", 3));
        answerNextMessageBox(QMessageBox::Cancel);
        QVERIFY(!m_window->openLocation(QUrl::fromLocalFile(binary)));
        QCOMPARE(m_tabs->count(), 1);
        QVERIFY(document(0)->isPristine());

        answerNextMessageBox(QMessageBox::Ok);
        QVERIFY(!m_window->openLocation(QUrl::fromLocalFile(path(QStringLiteral("missing.txt")))));
        QCOMPARE(m_tabs->count(), 1);
    }

    void closeTabs()
    {
        const QString a = path(QStringLiteral("c1.txt"));
        writeFile(a, "1");
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(a)));
        document(0)->editor()->insertPlainText(QStringLiteral("edit"));
        // Cancel keeps the tab.
        answerNextMessageBox(QMessageBox::Cancel);
        triggerAction(QStringLiteral("&Close Tab"));
        QCOMPARE(m_tabs->count(), 1);
        QVERIFY(document(0)->isModified());
        // Discard closes it, leaving a fresh untitled tab.
        answerNextMessageBox(QMessageBox::Discard);
        triggerAction(QStringLiteral("&Close Tab"));
        QTest::qWait(10);
        QCOMPARE(m_tabs->count(), 1);
        QVERIFY(document(0)->isPristine());
        QCOMPARE(readFile(a), QByteArray("1"));
    }

    void folderOpensInSidebar()
    {
        const QString folder = path(QStringLiteral("project"));
        QVERIFY(QDir().mkpath(folder));
        auto *sidebar = m_window->findChild<FolderSidebar *>();
        QVERIFY(sidebar);
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(folder)));
        QVERIFY(sidebar->hasFolder());
        QCOMPARE(Location::path(sidebar->folderUrl()), QDir::cleanPath(folder));
        QVERIFY(sidebar->isVisible());
    }

    void freshStartIsBlank()
    {
        const QString a = path(QStringLiteral("s1.txt"));
        const QString folder = path(QStringLiteral("sessionfolder"));
        writeFile(a, "first");
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(a)));
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(folder)));
        QVERIFY(m_window->close());

        // The next start opens one blank document and no folder.
        m_window = std::make_unique<MainWindow>();
        m_window->show();
        m_tabs = m_window->findChild<QTabWidget *>();
        QCOMPARE(m_tabs->count(), 1);
        QVERIFY(document(0)->isUntitled());
        QVERIFY(document(0)->editor()->toPlainText().isEmpty());
        QVERIFY(!m_window->findChild<FolderSidebar *>()->hasFolder());
        QVERIFY(!QSettings().contains("session/tabs"));
    }

    // The TeX preview appears beside a LaTeX tab, follows the text, and
    // goes away for other tabs; closing it keeps it closed until asked for.
    void texPreviewFollowsTabs()
    {
        auto *preview = m_window->findChild<Preview *>();
        QVERIFY(preview);
        preview->setMode(QStringLiteral("quick")); // no TeX runs in the tests
        QVERIFY(!preview->isVisible());

        const QString tex = path(QStringLiteral("paper.tex"));
        const QString txt = path(QStringLiteral("notes.txt"));
        writeFile(tex, "\\section{Hello}\nWorld $x^2$.\n");
        writeFile(txt, "plain\n");

        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(tex)));
        QVERIFY(preview->isVisible());
        auto *browser = preview->findChild<QTextBrowser *>();
        QVERIFY(browser->toPlainText().contains(QLatin1String("Hello")));
        // Typing updates it shortly afterwards.
        document(0)->editor()->moveCursor(QTextCursor::End);
        document(0)->editor()->insertPlainText(QStringLiteral("Typed words."));
        QTRY_VERIFY(browser->toPlainText().contains(QLatin1String("Typed words.")));
        // Focus stays in the text, not in the preview.
        QCOMPARE(QApplication::focusWidget(), document(0)->editor());

        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(txt)));
        QVERIFY(!preview->isVisible());
        m_tabs->setCurrentIndex(0);
        QVERIFY(preview->isVisible());

        // Closed with its close button: stays closed, even for LaTeX tabs.
        preview->close();
        QVERIFY(!preview->isVisible());
        m_tabs->setCurrentIndex(1);
        m_tabs->setCurrentIndex(0);
        QVERIFY(!preview->isVisible());
        triggerAction(QStringLiteral("Show &Preview"));
        QVERIFY(preview->isVisible());
    }

    // Choosing LaTeX for an untitled tab previews it too.
    void texPreviewForChosenLanguage()
    {
        auto *preview = m_window->findChild<Preview *>();
        preview->setMode(QStringLiteral("quick"));
        triggerAction(QStringLiteral("LaTeX"));
        QVERIFY(preview->isVisible());
        triggerAction(QStringLiteral("Plain Text"));
        QVERIFY(!preview->isVisible());
    }

    // Markdown and HTML files get a live preview too: Markdown in the
    // theme's colours, HTML as a web page. Nothing is fetched from the
    // network.
    void markdownAndHtmlPreview()
    {
        auto *preview = m_window->findChild<Preview *>();
        triggerAction(QStringLiteral("Plain Text"));
        const QString md = path(QStringLiteral("readme.md"));
        const QString html = path(QStringLiteral("page.html"));
        const QString css = path(QStringLiteral("page.css"));
        const QString svg = path(QStringLiteral("drawing.svg"));
        writeFile(md, "# Title\n\nSome *emphasis* and `code`.\n\n```\nblock\n```\n");
        writeFile(css, "p.note { color: #ff0000; }\n");
        // A 1x1 PNG, as a data: URL that's allowed, and labelled as TIFF
        // (a decoder that isn't).
        const QByteArray png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==";
        writeFile(html, "<html><head><title>T</title><link rel=\"stylesheet\" href=\"page.css\">"
                        "<script>document.write('scripted')</script></head><body>"
                        "<h1>Page heading</h1><p class=\"note\">Styled</p>"
                        "<img src=\"https://example.invalid/x.png\">"
                        "<img src=\"data:image/png;base64," + png + "\">"
                        "<img src=\"data:image/tiff;base64," + png + "\"></body></html>\n");
        writeFile(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>\n");
        auto *browser = preview->findChild<QTextBrowser *>();

        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(md)));
        const int mdTab = m_tabs->currentIndex();
        QVERIFY(preview->isVisible());
        QCOMPARE(preview->kind(), Preview::Kind::Markdown);
        QCOMPARE(preview->windowTitle(), QStringLiteral("Markdown Preview"));
        QVERIFY(browser->isVisible());
        QVERIFY(browser->toPlainText().contains(QLatin1String("Title")));
        QVERIFY(!browser->toPlainText().contains(QLatin1String("# Title")));
        QTextCursor cursor(browser->document());
        cursor.movePosition(QTextCursor::NextWord, QTextCursor::MoveAnchor, 3); // "emphasis"
        cursor.movePosition(QTextCursor::NextCharacter);
        QVERIFY(cursor.charFormat().fontItalic());
        // It follows the typing.
        Document *doc = document(mdTab);
        doc->editor()->moveCursor(QTextCursor::End);
        doc->editor()->insertPlainText(QStringLiteral("\n## Added heading\n"));
        QTRY_VERIFY(browser->toPlainText().contains(QLatin1String("Added heading")));

        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(html)));
        QVERIFY(preview->isVisible());
        QCOMPARE(preview->kind(), Preview::Kind::Html);
        QCOMPARE(preview->windowTitle(), QStringLiteral("HTML Preview"));
        const QString shown = browser->toPlainText();
        QVERIFY(shown.contains(QLatin1String("Page heading")));
        QVERIFY(!shown.contains(QLatin1String("<h1>")));
        QVERIFY(!shown.contains(QLatin1String("scripted")));
        // The local style sheet applies.
        const QTextBlock styled = browser->document()->findBlock(shown.indexOf(QLatin1String("Styled")));
        QTextCursor styledCursor(styled);
        styledCursor.movePosition(QTextCursor::NextCharacter);
        QCOMPARE(styledCursor.charFormat().foreground().color(), QColor(0xff, 0, 0));
        // Images: data: URLs in the allowed formats only.
        const auto image = [browser](const QByteArray &url) {
            return qvariant_cast<QImage>(browser->loadResource(QTextDocument::ImageResource, QUrl(QString::fromLatin1(url))));
        };
        QCOMPARE(image("data:image/png;base64," + png).size(), QSize(1, 1));
        QVERIFY(image("data:image/tiff;base64," + png).isNull());
        QVERIFY(image("https://example.invalid/x.png").isNull());
        QVERIFY(browser->document()->resource(QTextDocument::ImageResource,
                                              QUrl(QStringLiteral("data:image/tiff;base64,") + QLatin1String(png)))
                    .value<QImage>().isNull());

        // SVG is highlighted as HTML, but isn't a web page.
        QVERIFY(m_window->openLocation(QUrl::fromLocalFile(svg)));
        QVERIFY(!preview->isVisible());

        // Back to the Markdown tab.
        m_tabs->setCurrentIndex(mdTab);
        QVERIFY(preview->isVisible());
        QCOMPARE(preview->kind(), Preview::Kind::Markdown);
    }

    // Every toolbar button has an icon and a label beside it, in the
    // Settings defaults: dark theme, line numbers and spell checking on.
    void toolbarDefaults()
    {
        auto *toolBar = m_window->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
        QVERIFY(toolBar);
        QCOMPARE(toolBar->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        QCOMPARE(toolBarLabels(), (QStringList{"Sidebar", "Open", "Save", "Wrap", "Theme", "A+", "A−", "Find"}));
        for (QAction *action : toolBar->actions()) {
            if (!action->isSeparator() && !qobject_cast<QWidgetAction *>(action))
                QVERIFY2(!action->icon().isNull(), qPrintable(action->iconText()));
        }
        QVERIFY(document(0)->editor()->lineNumbersVisible());
        QVERIFY(document(0)->highlighter()->spellCheck());
        QCOMPARE(document(0)->editor()->palette().color(QPalette::Base), Theme::dracula().background);
    }

    // The Settings dialog applies each change straight away, and the
    // choices are kept for next time.
    void settingsDialog()
    {
        QTimer::singleShot(0, this, [this] {
            auto *dialog = qobject_cast<SettingsDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto combos = dialog->findChildren<QComboBox *>();
            QCOMPARE(combos.size(), 2);
            combos[0]->setCurrentIndex(combos[0]->findData(int(Qt::ToolButtonIconOnly)));
            combos[1]->setCurrentIndex(combos[1]->findData(false)); // Light
            for (QCheckBox *box : dialog->findChildren<QCheckBox *>()) {
                if (QStringList{"Save", "Find", "Show line numbers", "Check spelling as you type"}.contains(box->text()))
                    box->setChecked(false);
            }
            // Applied while the dialog is still open.
            auto *toolBar = m_window->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
            QCOMPARE(toolBar->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QCOMPARE(toolBarLabels(), (QStringList{"Sidebar", "Open", "Wrap", "Theme", "A+", "A−"}));
            QVERIFY(!document(0)->editor()->lineNumbersVisible());
            QVERIFY(!document(0)->highlighter()->spellCheck());
            QCOMPARE(document(0)->editor()->palette().color(QPalette::Base), Theme::light().background);
            dialog->reject();
        });
        triggerAction(QStringLiteral("Se&ttings…"));

        // Hidden buttons stay in the menus.
        bool saveInMenu = false;
        for (QAction *action : m_window->findChildren<QAction *>())
            saveInMenu |= action->text() == QLatin1String("&Save") && action->isVisible();
        QVERIFY(saveInMenu);

        // New tabs follow the settings, and so does the next window.
        triggerAction(QStringLiteral("&New Tab"));
        QVERIFY(!document(m_tabs->count() - 1)->editor()->lineNumbersVisible());
        QVERIFY(m_window->close());
        m_window = std::make_unique<MainWindow>();
        m_window->show();
        m_tabs = m_window->findChild<QTabWidget *>();
        auto *toolBar = m_window->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
        QCOMPARE(toolBar->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(toolBarLabels(), (QStringList{"Sidebar", "Open", "Wrap", "Theme", "A+", "A−"}));
        QVERIFY(!document(0)->editor()->lineNumbersVisible());
        QVERIFY(!document(0)->highlighter()->spellCheck());
        QCOMPARE(document(0)->editor()->palette().color(QPalette::Base), Theme::light().background);

        // Restore Defaults brings everything back.
        QTimer::singleShot(0, this, [] {
            auto *dialog = qobject_cast<SettingsDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            for (QAbstractButton *button : dialog->findChildren<QAbstractButton *>()) {
                if (button->text() == QLatin1String("Restore Defaults"))
                    button->click();
            }
            dialog->reject();
        });
        triggerAction(QStringLiteral("Se&ttings…"));
        QCOMPARE(toolBarLabels().size(), 8);
        QVERIFY(document(0)->editor()->lineNumbersVisible());
        QVERIFY(document(0)->highlighter()->spellCheck());
        QCOMPARE(document(0)->editor()->palette().color(QPalette::Base), Theme::dracula().background);
    }

    // The theme button shows a sun in the dark theme and a moon in the light one.
    void themeButtonIcon()
    {
        QAction *themeAction = nullptr;
        for (QAction *action : m_window->findChildren<QAction *>())
            if (action->text() == QLatin1String("&Dark Theme"))
                themeAction = action;
        QVERIFY(themeAction);
        const QImage sun = themeAction->icon().pixmap(20).toImage();
        themeAction->trigger();
        QCOMPARE(document(0)->editor()->palette().color(QPalette::Base), Theme::light().background);
        QVERIFY(themeAction->icon().pixmap(20).toImage() != sun);
    }

    // The right-click menu offers to ignore or learn an underlined word.
    void spellingMenu()
    {
        SpellChecker::instance()->setCheckerForTesting([](const QString &word) { return word != QLatin1String("teh"); });
        CodeEditor *editor = document(0)->editor();
        editor->setPlainText(QStringLiteral("the teh"));
        document(0)->highlighter()->rehighlight();
        const QTextBlock block = editor->document()->firstBlock();
        bool underlined = false;
        for (const QTextLayout::FormatRange &range : block.layout()->formats())
            underlined |= range.start == 4 && range.format.underlineStyle() == QTextCharFormat::SpellCheckUnderline;
        QVERIFY(underlined);

        QTextCursor cursor(block);
        cursor.setPosition(5);
        const QPoint pos = editor->cursorRect(cursor).center();
        QTimer::singleShot(0, this, [] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QStringList texts;
            for (QAction *action : menu->actions())
                texts << action->text();
            QVERIFY2(texts.contains(QLatin1String("Ignore Spelling")), qPrintable(texts.join(u'|')));
            QVERIFY(texts.contains(QLatin1String("Add to Dictionary")));
            for (QAction *action : menu->actions())
                if (action->text() == QLatin1String("Ignore Spelling"))
                    action->trigger();
            menu->close();
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, pos, editor->viewport()->mapToGlobal(pos));
        QApplication::sendEvent(editor->viewport(), &event);

        bool stillUnderlined = false;
        for (const QTextLayout::FormatRange &range : block.layout()->formats())
            stillUnderlined |= range.format.underlineStyle() == QTextCharFormat::SpellCheckUnderline;
        QVERIFY(!stillUnderlined);
        SpellChecker::instance()->setCheckerForTesting(nullptr);
    }

private:
    QStringList toolBarLabels() const
    {
        QStringList labels;
        auto *toolBar = m_window->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
        for (QAction *action : toolBar->actions()) {
            if (!action->isSeparator() && !qobject_cast<QWidgetAction *>(action))
                labels << action->iconText();
        }
        return labels;
    }
    QString path(const QString &name) const { return m_dir.filePath(name); }
    Document *document(int index) const { return qobject_cast<Document *>(m_tabs->widget(index)); }
    void triggerAction(const QString &text)
    {
        for (QAction *action : m_window->findChildren<QAction *>()) {
            if (action->text() == text) {
                action->trigger();
                return;
            }
        }
        QFAIL(qPrintable(QStringLiteral("no action ") + text));
    }

    QTemporaryDir m_dir;
    std::unique_ptr<MainWindow> m_window;
    QTabWidget *m_tabs = nullptr;
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
