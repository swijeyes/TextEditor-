#include "document.h"

#include "codeeditor.h"
#include "location.h"
#include "textcodec.h"

#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>

Document::Document(QWidget *parent)
    : QWidget(parent)
    , m_editor(new CodeEditor(this))
    , m_highlighter(new Highlighter(m_editor->document()))
    , m_stack(new QStackedLayout(this))
    , m_placeholder(new QWidget(this))
    , m_placeholderLabel(new QLabel(m_placeholder))
{
    m_placeholderLabel->setTextFormat(Qt::PlainText);
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    m_placeholderLabel->setWordWrap(true);
    auto *loadButton = new QPushButton(tr("Connect and Open"), m_placeholder);
    connect(loadButton, &QPushButton::clicked, this, &Document::loadRequested);
    auto *placeholderLayout = new QVBoxLayout(m_placeholder);
    placeholderLayout->addStretch();
    placeholderLayout->addWidget(m_placeholderLabel);
    placeholderLayout->addWidget(loadButton, 0, Qt::AlignCenter);
    placeholderLayout->addStretch();

    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(m_editor);
    m_stack->addWidget(m_placeholder);
    setFocusProxy(m_editor);

    connect(m_editor->document(), &QTextDocument::modificationChanged, this,
            &Document::stateChanged);
}

bool Document::isRemote() const
{
    return Location::isRemote(m_location);
}

QString Document::displayName() const
{
    return isUntitled() ? tr("Untitled") : Location::fileName(m_location);
}

QString Document::displayLocation() const
{
    return isUntitled() ? tr("Untitled") : Location::displayString(m_location);
}

bool Document::isModified() const
{
    return m_editor->document()->isModified();
}

void Document::setModified(bool modified)
{
    m_editor->document()->setModified(modified);
}

bool Document::isPristine() const
{
    return isUntitled() && m_loaded && !isModified() && m_editor->document()->isEmpty();
}

QString Document::writePath() const
{
    return m_resolvedPath.isEmpty() ? Location::path(m_location) : m_resolvedPath;
}

void Document::setUnloaded(const QUrl &location)
{
    m_location = location;
    m_loaded = false;
    m_savedVersion = {};
    m_resolvedPath.clear();
    m_fileSystem.reset();
    m_placeholderLabel->setText(tr("%1\n\nThis file is on %2. It opens when you connect.")
                                    .arg(Location::displayString(location),
                                         ServerAddress::fromUrl(location).displayName()));
    m_stack->setCurrentWidget(m_placeholder);
    setFocusProxy(nullptr);
    emit stateChanged();
}

void Document::setContent(const QUrl &location, const DecodedText &decoded,
                          const FileInfo &version, const QString &resolvedPath)
{
    m_location = location;
    m_crlf = decoded.crlf;
    m_latin1 = decoded.latin1;
    m_savedVersion = version;
    m_resolvedPath = resolvedPath;
    m_loaded = true;
    m_stack->setCurrentWidget(m_editor);
    setFocusProxy(m_editor);

    m_editor->clear();
    m_highlighter->setLanguage(Highlighter::languageForFile(Location::fileName(location)));
    m_editor->setPlainText(decoded.text);
    m_editor->document()->setModified(false);
    emit stateChanged();
}

void Document::reset()
{
    setContent(QUrl(), DecodedText(), FileInfo());
    m_highlighter->setLanguage(Language::PlainText);
    m_fileSystem.reset();
}

void Document::markSaved(const QUrl &location, bool latin1, const FileInfo &version,
                         const QString &resolvedPath)
{
    m_location = location;
    m_latin1 = latin1;
    m_savedVersion = version;
    m_resolvedPath = resolvedPath;
    m_editor->document()->setModified(false);
    emit stateChanged();
}

void Document::relocate(const QUrl &location)
{
    m_location = location;
    m_resolvedPath.clear();
    if (!m_loaded)
        setUnloaded(location);
    emit stateChanged();
}
