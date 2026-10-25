#include "findbar.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QToolButton>

FindBar::FindBar(QPlainTextEdit *editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
    , m_findEdit(new QLineEdit(this))
    , m_replaceEdit(new QLineEdit(this))
    , m_caseBox(new QCheckBox(tr("Match case"), this))
    , m_wordBox(new QCheckBox(tr("Whole word"), this))
    , m_regexBox(new QCheckBox(tr("Regex"), this))
    , m_status(new QLabel(this))
{
    m_status->setTextFormat(Qt::PlainText);
    m_findEdit->setPlaceholderText(tr("Find"));
    m_findEdit->setClearButtonEnabled(true);
    m_replaceEdit->setPlaceholderText(tr("Replace with"));
    m_replaceEdit->setClearButtonEnabled(true);

    auto button = [this](const QString &text, const QString &toolTip) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(toolTip);
        b->setAutoRaise(true);
        return b;
    };
    QToolButton *prevButton = button(tr("Previous"), tr("Find previous (Shift+Enter)"));
    QToolButton *nextButton = button(tr("Next"), tr("Find next (Enter)"));
    QToolButton *replaceButton = button(tr("Replace"), tr("Replace this match"));
    QToolButton *replaceAllButton = button(tr("Replace All"), tr("Replace every match"));
    QToolButton *closeButton = button(QStringLiteral("✕"), tr("Close (Esc)"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(m_findEdit, 2);
    layout->addWidget(prevButton);
    layout->addWidget(nextButton);
    layout->addWidget(m_caseBox);
    layout->addWidget(m_wordBox);
    layout->addWidget(m_regexBox);
    layout->addSpacing(12);
    layout->addWidget(m_replaceEdit, 2);
    layout->addWidget(replaceButton);
    layout->addWidget(replaceAllButton);
    layout->addWidget(m_status, 1);
    layout->addWidget(closeButton);

    connect(m_findEdit, &QLineEdit::textEdited, this, [this] { find(false, true); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
            findPrevious();
        else
            findNext();
    });
    connect(m_replaceEdit, &QLineEdit::returnPressed, this, &FindBar::replace);
    for (QCheckBox *box : {m_caseBox, m_wordBox, m_regexBox})
        connect(box, &QCheckBox::toggled, this, [this] { find(false, true); });

    connect(prevButton, &QToolButton::clicked, this, &FindBar::findPrevious);
    connect(nextButton, &QToolButton::clicked, this, &FindBar::findNext);
    connect(replaceButton, &QToolButton::clicked, this, &FindBar::replace);
    connect(replaceAllButton, &QToolButton::clicked, this, &FindBar::replaceAll);
    connect(closeButton, &QToolButton::clicked, this, [this] {
        hide();
        m_editor->setFocus();
    });

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, closeButton, &QToolButton::click);
}

void FindBar::showFind(bool focusReplace)
{
    const QString selected = m_editor->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
        m_findEdit->setText(selected);

    show();
    QLineEdit *target = focusReplace && !m_findEdit->text().isEmpty() ? m_replaceEdit : m_findEdit;
    target->setFocus();
    target->selectAll();
}

bool FindBar::findNext()
{
    if (m_findEdit->text().isEmpty()) {
        showFind(false);
        return false;
    }
    return find(false);
}

bool FindBar::findPrevious()
{
    if (m_findEdit->text().isEmpty()) {
        showFind(false);
        return false;
    }
    return find(true);
}

void FindBar::replace()
{
    if (m_findEdit->text().isEmpty() || !validatePattern())
        return;
    if (selectionMatches()) {
        QTextCursor cursor = m_editor->textCursor();
        cursor.insertText(replacementFor(cursor.selectedText()));
        m_editor->setTextCursor(cursor);
    }
    find(false);
}

void FindBar::replaceAll()
{
    if (m_findEdit->text().isEmpty() || !validatePattern())
        return;

    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock();
    int count = 0;
    QTextCursor from(m_editor->document());
    for (;;) {
        QTextCursor found = findFrom(from, false);
        // Stop on an empty match (e.g. the regex "a*") to avoid looping forever.
        if (found.isNull() || !found.hasSelection())
            break;
        found.insertText(replacementFor(found.selectedText()));
        from = found;
        ++count;
    }
    cursor.endEditBlock();

    if (count == 0)
        setStatus(tr("No matches"), true);
    else
        setStatus(count == 1 ? tr("Replaced 1 match") : tr("Replaced %1 matches").arg(count));
}

bool FindBar::find(bool backward, bool fromSelectionStart)
{
    if (m_findEdit->text().isEmpty()) {
        setStatus({});
        return false;
    }
    if (!validatePattern())
        return false;

    QTextCursor cursor = m_editor->textCursor();
    if (fromSelectionStart)
        cursor.setPosition(cursor.selectionStart());

    QTextCursor found = findFrom(cursor, backward);
    if (found.isNull()) {
        // Wrap around to the other end of the document.
        QTextCursor wrapped(m_editor->document());
        if (backward)
            wrapped.movePosition(QTextCursor::End);
        found = findFrom(wrapped, backward);
    }
    if (found.isNull()) {
        setStatus(tr("No matches"), true);
        return false;
    }

    m_editor->setTextCursor(found);
    setStatus({});
    return true;
}

QTextCursor FindBar::findFrom(const QTextCursor &from, bool backward) const
{
    QTextDocument::FindFlags flags;
    if (backward)
        flags |= QTextDocument::FindBackward;
    if (m_caseBox->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    if (m_wordBox->isChecked())
        flags |= QTextDocument::FindWholeWords;

    if (m_regexBox->isChecked())
        return m_editor->document()->find(searchRegex(), from, flags);
    return m_editor->document()->find(m_findEdit->text(), from, flags);
}

QRegularExpression FindBar::searchRegex(bool anchored) const
{
    const QString pattern = anchored ? QRegularExpression::anchoredPattern(m_findEdit->text())
                                     : m_findEdit->text();
    return QRegularExpression(pattern, m_caseBox->isChecked()
                                           ? QRegularExpression::NoPatternOption
                                           : QRegularExpression::CaseInsensitiveOption);
}

bool FindBar::validatePattern()
{
    if (m_regexBox->isChecked() && !searchRegex().isValid()) {
        setStatus(tr("Invalid pattern"), true);
        return false;
    }
    return true;
}

bool FindBar::selectionMatches() const
{
    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection())
        return false;
    const QString selected = cursor.selectedText();
    if (m_regexBox->isChecked())
        return searchRegex(true).match(selected).hasMatch();
    return selected.compare(m_findEdit->text(), m_caseBox->isChecked() ? Qt::CaseSensitive
                                                                       : Qt::CaseInsensitive) == 0;
}

QString FindBar::replacementFor(const QString &matched) const
{
    if (!m_regexBox->isChecked())
        return m_replaceEdit->text();
    // Supports back-references such as \1 in the replacement.
    QString result = matched;
    return result.replace(searchRegex(true), m_replaceEdit->text());
}

void FindBar::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color: #e5534b;") : QString());
}
