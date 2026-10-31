#include "preview.h"

#include "codeeditor.h"
#include "document.h"
#include "latexhtml.h"
#include "location.h"
#include "messagebox.h"

#include <QAbstractScrollArea>
#include <QBuffer>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

namespace {

constexpr int QuickDelayMs = 150;
constexpr int TypesetDelayMs = 900;
constexpr qint64 MaxImageBytes = 20 * 1024 * 1024;
constexpr qint64 MaxStyleSheetBytes = 1024 * 1024;
constexpr int MaxImageSide = 2400;
constexpr int MaxCachedImages = 64;
// The browser previews are made on the window's thread; beyond this size
// they would make typing sluggish.
constexpr int MaxQuickChars = 2 * 1024 * 1024;
constexpr int PageGap = 12;

QString cssColor(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

// Qt loads a <link rel="stylesheet"> only when it says type="text/css",
// which HTML5 pages leave out; this adds it.
QString withStyleSheetTypes(const QString &html)
{
    static const QRegularExpression link(QStringLiteral("<link\\b[^>]*>"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression stylesheet(QStringLiteral("\\brel\\s*=\\s*[\"']?\\s*stylesheet\\b"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression type(QStringLiteral("\\btype\\s*="),
                                         QRegularExpression::CaseInsensitiveOption);
    QString result;
    qsizetype done = 0;
    for (auto it = link.globalMatch(html); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString tag = match.captured();
        if (!tag.contains(stylesheet) || tag.contains(type))
            continue;
        result += QStringView(html).mid(done, match.capturedStart() + 5 - done);
        result += QLatin1String(" type=\"text/css\"");
        done = match.capturedStart() + 5; // after "<link"
    }
    if (done == 0)
        return html;
    result += QStringView(html).mid(done);
    return result;
}

} // namespace

// Shows the Quick, Markdown and HTML previews. Images (\includegraphics,
// <img>, ![]()) and HTML style sheets are loaded from the file's folder
// (local files only), trying the usual image extensions, with size limits;
// images can also be data: URLs. Nothing is ever fetched from the network,
// and no scripts run. Only PNG, JPEG, GIF and BMP are decoded, chosen by
// the file's extension (or the data: URL's type), so a file can't steer
// its bytes into another (less used) image decoder.
class PreviewBrowser : public QTextBrowser
{
public:
    using QTextBrowser::QTextBrowser;

    void setFolder(const QString &folder) { m_folder = folder; }

    QVariant loadResource(int type, const QUrl &name) override
    {
        const QVariant resource = load(type, name);
        if (resource.isValid())
            return resource;
        // Something empty rather than nothing: given nothing, QTextDocument
        // reads files and data: URLs itself, in any image format.
        if (type == QTextDocument::ImageResource)
            return QImage();
        if (type == QTextDocument::StyleSheetResource)
            return QString();
        return QByteArray();
    }

private:
    QVariant load(int type, const QUrl &name)
    {
        if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("data"))
            return dataImage(name);
        // Web addresses and the like are never fetched.
        if (m_folder.isEmpty() || (!name.scheme().isEmpty() && !name.isLocalFile()))
            return {};
        const QString requested = name.isLocalFile() ? name.toLocalFile() : name.path();
        if (requested.isEmpty())
            return {};
        const QString base = QFileInfo(requested).isAbsolute() ? requested : m_folder + u'/' + requested;
        if (type == QTextDocument::StyleSheetResource)
            return styleSheet(base);
        if (type != QTextDocument::ImageResource)
            return {};
        static const QHash<QString, QByteArray> formats{
            {"png", "png"}, {"jpg", "jpeg"}, {"jpeg", "jpeg"}, {"gif", "gif"}, {"bmp", "bmp"}};
        for (const char *extension : {"", ".png", ".jpg", ".jpeg", ".gif", ".bmp"}) {
            const QFileInfo info(base + QLatin1String(extension));
            const QByteArray format = formats.value(info.suffix().toLower());
            if (format.isEmpty() || !info.isFile() || info.size() > MaxImageBytes)
                continue;
            const QString key = info.absoluteFilePath();
            const auto cached = m_cache.constFind(key);
            if (cached != m_cache.cend() && cached->first == info.lastModified())
                return cached->second;
            QImageReader reader(key, format);
            const QImage image = read(reader);
            if (image.isNull())
                continue;
            if (m_cache.size() >= MaxCachedImages)
                m_cache.clear();
            m_cache.insert(key, {info.lastModified(), image});
            return image;
        }
        return {};
    }

    static QImage read(QImageReader &reader)
    {
        reader.setAutoDetectImageFormat(false);
        reader.setDecideFormatFromContent(false);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isValid() && qMax(size.width(), size.height()) > MaxImageSide)
            reader.setScaledSize(size.scaled(MaxImageSide, MaxImageSide, Qt::KeepAspectRatio));
        return reader.read();
    }

    // data:image/png;base64,... and the like.
    static QVariant dataImage(const QUrl &url)
    {
        const QString data = url.path(QUrl::FullyEncoded);
        const qsizetype comma = data.indexOf(u',');
        if (comma < 0 || data.size() > MaxImageBytes * 2)
            return {};
        const QString header = data.left(comma).toLower();
        static const QHash<QString, QByteArray> formats{{"image/png", "png"}, {"image/jpeg", "jpeg"},
                                                        {"image/jpg", "jpeg"}, {"image/gif", "gif"},
                                                        {"image/bmp", "bmp"}};
        const QByteArray format = formats.value(header.section(u';', 0, 0));
        if (format.isEmpty())
            return {};
        const QByteArray encoded = QByteArray::fromPercentEncoding(data.mid(comma + 1).toLatin1());
        QByteArray bytes = header.endsWith(QLatin1String(";base64")) ? QByteArray::fromBase64(encoded) : encoded;
        if (bytes.size() > MaxImageBytes)
            return {};
        QBuffer buffer(&bytes);
        QImageReader reader(&buffer, format);
        const QImage image = read(reader);
        return image.isNull() ? QVariant() : QVariant(image);
    }

    // <link rel="stylesheet" href="...">: a local .css file.
    static QVariant styleSheet(const QString &path)
    {
        const QFileInfo info(path);
        if (info.suffix().toLower() != QLatin1String("css") || !info.isFile() || info.size() > MaxStyleSheetBytes)
            return {};
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(file.read(MaxStyleSheetBytes));
    }

    QString m_folder;
    QHash<QString, QPair<QDateTime, QImage>> m_cache;
};

// The typeset pages, one under another, scaled to the width of the view.
class PagesView : public QAbstractScrollArea
{
public:
    explicit PagesView(QWidget *parent = nullptr)
        : QAbstractScrollArea(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        verticalScrollBar()->setSingleStep(30);
    }

    void setPages(const QStringList &files)
    {
        // Keep the reading position, as a fraction of the document.
        const QScrollBar *bar = verticalScrollBar();
        const double position = bar->maximum() > 0 ? double(bar->value()) / bar->maximum() : 0.0;
        m_pages.clear();
        for (const QString &file : files) {
            QImage image(file, "PNG");
            if (!image.isNull())
                m_pages << image;
        }
        m_scaled.clear();
        updateLayout();
        verticalScrollBar()->setValue(int(position * verticalScrollBar()->maximum()));
        viewport()->update();
    }

    void setBackground(const QColor &color)
    {
        m_background = color;
        viewport()->update();
    }

    bool isEmpty() const { return m_pages.isEmpty(); }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QAbstractScrollArea::resizeEvent(event);
        m_scaled.clear();
        updateLayout();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), m_background);
        ensureScaled();
        int y = PageGap - verticalScrollBar()->value();
        for (const QPixmap &page : std::as_const(m_scaled)) {
            const QSize size = page.deviceIndependentSize().toSize();
            const QRect rect(PageGap, y, size.width(), size.height());
            if (rect.bottom() >= 0 && rect.top() <= viewport()->height()) {
                painter.fillRect(rect.translated(2, 2), QColor(0, 0, 0, 60));
                painter.drawPixmap(rect.topLeft(), page);
            }
            y += size.height() + PageGap;
        }
    }

private:
    int pageWidth() const { return qMax(50, viewport()->width() - 2 * PageGap); }

    void ensureScaled()
    {
        if (m_scaled.size() == m_pages.size())
            return;
        const qreal ratio = devicePixelRatioF();
        m_scaled.clear();
        for (const QImage &image : std::as_const(m_pages)) {
            QPixmap pixmap = QPixmap::fromImage(
                image.scaledToWidth(int(pageWidth() * ratio), Qt::SmoothTransformation));
            pixmap.setDevicePixelRatio(ratio);
            m_scaled << pixmap;
        }
    }

    void updateLayout()
    {
        int height = PageGap;
        for (const QImage &image : std::as_const(m_pages))
            height += image.height() * pageWidth() / qMax(1, image.width()) + PageGap;
        verticalScrollBar()->setRange(0, qMax(0, height - viewport()->height()));
        verticalScrollBar()->setPageStep(viewport()->height());
    }

    QList<QImage> m_pages;
    QList<QPixmap> m_scaled;
    QColor m_background = Qt::gray;
};

Preview::Preview(QWidget *parent)
    : QDockWidget(tr("Preview"), parent)
    , m_modeCombo(new QComboBox(this))
    , m_openPdfButton(new QToolButton(this))
    , m_status(new QLabel(this))
    , m_stack(new QStackedWidget(this))
    , m_browser(new PreviewBrowser(this))
    , m_pages(new PagesView(this))
    , m_timer(new QTimer(this))
    , m_compiler(new TexCompiler(this))
    , m_theme(Theme::dracula())
{
    setObjectName(QStringLiteral("preview"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_modeCombo->addItem(tr("Quick Preview"), QStringLiteral("quick"));
    m_modeCombo->setItemData(0, tr("Shows the document straight away, without TeX. Maths, "
                                   "tables and layout are approximate."),
                             Qt::ToolTipRole);
    const QList<TexCompiler::Tool> engines = TexCompiler::availableEngines();
    for (const TexCompiler::Tool &tool : engines) {
        m_modeCombo->addItem(tr("Typeset with %1").arg(TexCompiler::engineName(tool.engine)),
                             QFileInfo(tool.path).baseName().toLower());
        m_modeCombo->setItemData(m_modeCombo->count() - 1, QDir::toNativeSeparators(tool.path),
                                 Qt::ToolTipRole);
    }
    if (engines.isEmpty()) {
        m_modeCombo->addItem(tr("Typeset (no TeX found)"), QString());
        m_modeCombo->setItemData(1, tr("Install a TeX distribution (MacTeX, TeX Live or MiKTeX) or "
                                       "Tectonic to see typeset pages."),
                                 Qt::ToolTipRole);
        if (auto *model = qobject_cast<QStandardItemModel *>(m_modeCombo->model()))
            model->item(1)->setEnabled(false);
    }
    m_modeCombo->setToolTip(tr("How the preview is made"));
    m_modeCombo->setFocusPolicy(Qt::ClickFocus);
    connect(m_modeCombo, &QComboBox::activated, this, [this](int index) {
        setMode(m_modeCombo->itemData(index).toString());
    });

    m_openPdfButton->setText(tr("Open PDF"));
    m_openPdfButton->setToolTip(tr("Open the typeset PDF in your PDF viewer"));
    m_openPdfButton->setAutoRaise(true);
    m_openPdfButton->hide();
    connect(m_openPdfButton, &QToolButton::clicked, this, [this] {
        if (QFileInfo::exists(m_pdfPath))
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_pdfPath));
    });

    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_browser->setOpenLinks(false);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->setFocusPolicy(Qt::ClickFocus);
    connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme().isEmpty() && url.path().isEmpty())
            m_browser->scrollToAnchor(url.fragment());
        else if (QStringList{"http", "https", "mailto"}.contains(url.scheme().toLower()))
            QDesktopServices::openUrl(url);
    });
    // A link's text can say anything, so show where it really goes.
    connect(m_browser, &QTextBrowser::highlighted, this, [this](const QUrl &url) {
        if (url.isEmpty() || url.scheme().isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(QCursor::pos(), plainToolTip(url.toDisplayString()), m_browser);
    });
    m_stack->addWidget(m_browser);
    m_stack->addWidget(m_pages);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(6, 4, 6, 0);
    header->addWidget(m_modeCombo, 1);
    header->addWidget(m_openPdfButton);
    auto *contents = new QWidget(this);
    auto *layout = new QVBoxLayout(contents);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(header);
    auto *statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(8, 0, 8, 0);
    statusRow->addWidget(m_status);
    layout->addLayout(statusRow);
    layout->addWidget(m_stack, 1);
    setWidget(contents);

    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Preview::updateNow);
    connect(m_compiler, &TexCompiler::finished, this, &Preview::typesetFinished);
    connect(m_compiler, &TexCompiler::failed, this, [this](const QString &message) {
        showStatus(message, true);
        if (m_pending) {
            m_pending = false;
            scheduleUpdate();
        }
    });

    // Typeset by default when TeX and a PDF renderer are both installed.
    QString mode = QSettings().value("texPreview/mode").toString();
    if (mode.isEmpty() || m_modeCombo->findData(mode) < 0)
        mode = !engines.isEmpty() && !TexCompiler::rasterizer().isEmpty()
                   ? QFileInfo(engines.first().path).baseName().toLower()
                   : QStringLiteral("quick");
    m_mode = mode;
    m_modeCombo->setCurrentIndex(m_modeCombo->findData(mode));
    updateControls();
    setTheme(m_theme);
}

void Preview::setDocument(Document *document, Kind kind)
{
    if (document == m_document && kind == m_kind)
        return;
    disconnect(m_textConnection);
    m_compiler->cancel();
    m_pending = false;
    m_document = document;
    m_kind = kind;
    m_lastText.clear();
    m_pdfPath.clear();
    m_pages->setPages({});
    m_browser->clear();
    showStatus(QString());
    updateControls();
    if (!document)
        return;
    m_textConnection = connect(document->editor(), &QPlainTextEdit::textChanged, this,
                               &Preview::scheduleUpdate);
    // Show something straight away.
    updateNow();
}

void Preview::setMode(const QString &mode)
{
    if (mode.isEmpty() || mode == m_mode)
        return;
    m_mode = mode;
    QSettings().setValue("texPreview/mode", mode);
    m_modeCombo->setCurrentIndex(m_modeCombo->findData(mode));
    m_compiler->cancel();
    m_pending = false;
    m_lastText.clear();
    showStatus(QString());
    updateControls();
    updateNow();
}

void Preview::setTheme(const Theme &theme)
{
    m_theme = theme;
    // The browser's scroll bars, like the editor's, follow the theme.
    m_browser->setProperty("classicDark", theme.dark);
    m_pages->setProperty("classicDark", theme.dark);
    m_browser->verticalScrollBar()->update();
    m_pages->verticalScrollBar()->update();
    m_pages->setBackground(theme.dark ? theme.gutterBackground.darker(130) : QColor(0xe0, 0xe0, 0xe4));
    m_lastText.clear(); // the style sheet's colours changed
    if (m_document && usesBrowser())
        updateNow();
}

// Shows the parts that suit what's being previewed: the LaTeX mode menu and
// Open PDF only for LaTeX, and the pages only when typesetting.
void Preview::updateControls()
{
    const bool latex = m_kind == Kind::Latex;
    setWindowTitle(latex ? tr("TeX Preview") : m_kind == Kind::Markdown ? tr("Markdown Preview")
                                                                         : tr("HTML Preview"));
    m_modeCombo->setVisible(latex);
    m_openPdfButton->setVisible(latex && !usesBrowser() && QFileInfo::exists(m_pdfPath));
    m_stack->setCurrentWidget(usesBrowser() ? static_cast<QWidget *>(m_browser) : m_pages);
}

bool Preview::usesBrowser() const
{
    return m_kind != Kind::Latex || m_mode == QLatin1String("quick");
}

void Preview::closeEvent(QCloseEvent *event)
{
    QDockWidget::closeEvent(event);
    emit closedByUser();
}

void Preview::showEvent(QShowEvent *event)
{
    QDockWidget::showEvent(event);
    if (m_document)
        scheduleUpdate();
}

void Preview::scheduleUpdate()
{
    if (!isVisible())
        return;
    m_timer->start(usesBrowser() ? QuickDelayMs : TypesetDelayMs);
}

void Preview::updateNow()
{
    m_timer->stop();
    if (!m_document)
        return;
    const QString text = m_document->editor()->toPlainText();
    if (text == m_lastText && m_mode == m_lastMode)
        return;
    if (m_kind == Kind::Markdown)
        updateMarkdown(text);
    else if (m_kind == Kind::Html)
        updateHtml(text);
    else if (m_mode == QLatin1String("quick"))
        updateQuick(text);
    else
        updateTypeset(text);
}

// `load` sets up the document and fills it through the browser (not the
// document directly), so images and style sheets come from loadResource().
void Preview::showInBrowser(const std::function<void(QTextDocument *)> &load)
{
    m_browser->setFolder(sourceFolder());
    const int scroll = m_browser->verticalScrollBar()->value();
    load(m_browser->document());
    m_browser->verticalScrollBar()->setValue(scroll);
    showStatus(QString());
}

void Preview::updateMarkdown(const QString &text)
{
    m_lastText = text;
    m_lastMode = m_mode;
    if (text.size() > MaxQuickChars) {
        m_browser->clear();
        showStatus(tr("This document is too large to preview."), true);
        return;
    }
    QPalette p = m_browser->palette();
    p.setColor(QPalette::Base, m_theme.background);
    p.setColor(QPalette::Text, m_theme.foreground);
    p.setColor(QPalette::Link, m_theme.type);
    m_browser->setPalette(p);

    showInBrowser([this, &text](QTextDocument *doc) {
        doc->setDefaultStyleSheet(QString());
        QFont font = QApplication::font();
        font.setPointSizeF(font.pointSizeF() + 2);
        doc->setDefaultFont(font);
        doc->setDocumentMargin(16);
        m_browser->setMarkdown(text);

        // Shade code and dim quotes, in the theme's colours.
        QTextCharFormat code;
        code.setFontFamilies({QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});
        code.setBackground(m_theme.currentLine);
        QTextCursor cursor(doc);
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (fragment.isValid() && fragment.charFormat().fontFixedPitch()) {
                    cursor.setPosition(fragment.position());
                    cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                    cursor.mergeCharFormat(code);
                }
            }
            const QTextBlockFormat format = block.blockFormat();
            if (format.hasProperty(QTextFormat::BlockCodeFence) || format.nonBreakableLines()) {
                QTextBlockFormat shaded = format;
                shaded.setBackground(m_theme.currentLine);
                cursor.setPosition(block.position());
                cursor.setBlockFormat(shaded);
            } else if (format.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
                QTextCharFormat dim;
                dim.setForeground(m_theme.comment);
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
                cursor.mergeCharFormat(dim);
            }
        }
    });
}

void Preview::updateHtml(const QString &text)
{
    m_lastText = text;
    m_lastMode = m_mode;
    if (text.size() > MaxQuickChars) {
        m_browser->clear();
        showStatus(tr("This document is too large to preview."), true);
        return;
    }
    // A web page, as a browser shows one: black on white unless it says
    // otherwise, whatever the editor's theme.
    QPalette p = m_browser->palette();
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::Text, Qt::black);
    p.setColor(QPalette::Link, QColor(0x00, 0x00, 0xee));
    m_browser->setPalette(p);

    showInBrowser([this, &text](QTextDocument *doc) {
        doc->setDefaultStyleSheet(QStringLiteral("a { color: #0000ee; }"));
        doc->setDefaultFont(QApplication::font());
        doc->setDocumentMargin(8);
        m_browser->setHtml(withStyleSheetTypes(text));
    });
}

void Preview::updateQuick(const QString &text)
{
    m_lastText = text;
    m_lastMode = m_mode;
    if (text.size() > MaxQuickChars) {
        m_browser->clear();
        showStatus(tr("This document is too large for Quick Preview. Choose a Typeset mode to "
                      "preview it."), true);
        return;
    }
    QPalette p = m_browser->palette();
    p.setColor(QPalette::Base, m_theme.background);
    p.setColor(QPalette::Text, m_theme.foreground);
    m_browser->setPalette(p);

    const QString css = QStringLiteral(
        "body { color: %1; background-color: %2; }"
        "a { color: %3; }"
        "h1, h2, h3, h4 { color: %1; }"
        ".title { font-size: xx-large; }"
        ".author, .date { font-size: large; }"
        ".caption { font-size: small; }"
        ".note, .eqno { color: %4; }"
        ".box { background-color: %5; }"
        ".key { background-color: %6; font-family: sans-serif; font-size: small; }"
        "pre, tt { font-family: monospace; }"
        ".footnotes { font-size: small; }")
        .arg(cssColor(m_theme.foreground), cssColor(m_theme.background), cssColor(m_theme.type),
             cssColor(m_theme.comment), cssColor(m_theme.currentLine), cssColor(m_theme.selection));
    showInBrowser([this, &css, &text](QTextDocument *doc) {
        doc->setDefaultStyleSheet(css);
        QFont font;
        font.setFamilies({QStringLiteral("Latin Modern Roman"), QStringLiteral("CMU Serif"),
                          QStringLiteral("Charter"), QStringLiteral("Georgia"),
                          QStringLiteral("Times New Roman")});
        font.setStyleHint(QFont::Serif);
        font.setPointSizeF(QApplication::font().pointSizeF() + 2);
        doc->setDefaultFont(font);
        doc->setDocumentMargin(16);
        m_browser->setHtml(LatexHtml::toHtml(text));
    });
}

void Preview::updateTypeset(const QString &text)
{
    const TexCompiler::Tool *tool = engine();
    if (!tool) {
        showStatus(tr("No TeX engine was found. Choose Quick Preview, or install MacTeX, "
                      "TeX Live, MiKTeX or Tectonic."), true);
        return;
    }
    if (m_compiler->isRunning()) {
        m_pending = true;
        return;
    }
    m_lastText = text;
    m_lastMode = m_mode;
    m_pending = false;
    // Render at about the view's width for a typical page (8.5 inches).
    const int dpi = qBound(72, int(m_pages->viewport()->width() * devicePixelRatioF() / 8.3), 300);
    showStatus(tr("Typesetting with %1…").arg(TexCompiler::engineName(tool->engine)));
    m_compiler->compile(text, sourceFolder(), *tool, dpi);
}

void Preview::typesetFinished(const QStringList &pages, const QString &pdfPath, const QString &warning)
{
    m_pdfPath = pdfPath;
    updateControls();
    if (pages.isEmpty() && TexCompiler::rasterizer().isEmpty()) {
        showStatus(tr("Typeset, but no PDF renderer was found to show the pages here. Install "
                      "Poppler (pdftoppm) or MuPDF, or use Open PDF."), true);
    } else if (!warning.isEmpty()) {
        showStatus(warning, true);
    } else if (pages.size() >= TexCompiler::MaxPages) {
        showStatus(tr("Showing the first %1 pages.").arg(TexCompiler::MaxPages));
    } else {
        showStatus(QString());
    }
    m_pages->setPages(pages);
    if (m_pending) {
        m_pending = false;
        scheduleUpdate();
    }
}

void Preview::showStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
    QPalette p = m_status->palette();
    p.setColor(QPalette::WindowText, error ? QColor(0xd0, 0x40, 0x40) : palette().color(QPalette::WindowText));
    m_status->setPalette(p);
}

const TexCompiler::Tool *Preview::engine() const
{
    static const QList<TexCompiler::Tool> engines = TexCompiler::availableEngines();
    for (const TexCompiler::Tool &tool : engines) {
        if (QFileInfo(tool.path).baseName().toLower() == m_mode)
            return &tool;
    }
    return nullptr;
}

QString Preview::sourceFolder() const
{
    if (!m_document || m_document->isUntitled() || !Location::isLocal(m_document->location()))
        return {};
    return QFileInfo(Location::path(m_document->location())).absolutePath();
}
