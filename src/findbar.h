#pragma once

#include <QRegularExpression>
#include <QTextCursor>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

// Inline find/replace bar shown above the editor.
class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(QPlainTextEdit *editor, QWidget *parent = nullptr);

    void showFind(bool focusReplace);
    // Points the bar at another editor, such as the one in a newly selected tab.
    void setEditor(QPlainTextEdit *editor);

public slots:
    bool findNext();
    bool findPrevious();
    void replace();
    void replaceAll();

private:
    bool find(bool backward, bool fromSelectionStart = false);
    QTextCursor findFrom(const QTextCursor &from, bool backward) const;
    QRegularExpression searchRegex(bool anchored = false) const;
    bool validatePattern();
    bool selectionMatches() const;
    QString replacementFor(const QString &matched) const;
    void setStatus(const QString &text, bool error = false);

    QPlainTextEdit *m_editor;
    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QCheckBox *m_caseBox;
    QCheckBox *m_wordBox;
    QCheckBox *m_regexBox;
    QLabel *m_status;
};
