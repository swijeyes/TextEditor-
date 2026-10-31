#include "authdialog.h"

#include "hostkeydialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace {

QLabel *plainLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    // Prompts and instructions come from the server.
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

} // namespace

AuthDialog::AuthDialog(const ServerAddress &server, const AuthPrompt &prompt, QWidget *parent)
    : QDialog(parent)
{
    auto *layout = new QVBoxLayout(this);
    switch (prompt.kind) {
    case AuthPrompt::Password:
        setWindowTitle(tr("Password"));
        layout->addWidget(plainLabel(tr("Enter the password for %1.").arg(server.displayName()), this));
        break;
    case AuthPrompt::Passphrase:
        setWindowTitle(tr("Key Passphrase"));
        layout->addWidget(plainLabel(tr("Enter the passphrase for the key %1, to log in to %2.")
                                         .arg(prompt.keyFile, server.displayName()), this));
        break;
    case AuthPrompt::KeyboardInteractive:
        setWindowTitle(tr("Log In"));
        layout->addWidget(plainLabel(tr("%1 asks:").arg(server.displayName()), this));
        if (!prompt.name.isEmpty()) {
            QLabel *name = plainLabel(prompt.name, this);
            QFont bold = name->font();
            bold.setBold(true);
            name->setFont(bold);
            layout->addWidget(name);
        }
        if (!prompt.instruction.isEmpty())
            layout->addWidget(plainLabel(prompt.instruction, this));
        break;
    }
    if (!prompt.retryMessage.isEmpty())
        layout->addWidget(plainLabel(prompt.retryMessage, this));

    auto *form = new QFormLayout;
    for (int i = 0; i < prompt.prompts.size(); ++i) {
        auto *field = new QLineEdit(this);
        const bool echo = i < prompt.echo.size() && prompt.echo.at(i);
        field->setEchoMode(echo ? QLineEdit::Normal : QLineEdit::Password);
        // Keep secrets out of input-method history and predictive text.
        if (!echo)
            field->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText);
        QLabel *label = plainLabel(prompt.prompts.at(i), this);
        label->setBuddy(field);
        form->addRow(label, field);
        m_fields << field;
    }
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    setMinimumWidth(420);
}

AuthDialog::~AuthDialog()
{
    takeAnswers();
}

QStringList AuthDialog::takeAnswers()
{
    QStringList answers;
    for (QLineEdit *field : std::as_const(m_fields)) {
        answers << field->text();
        field->clear();
    }
    return answers;
}

bool AuthDialog::ask(QWidget *parent, const ServerAddress &server, const AuthPrompt &prompt,
                     QStringList *answers)
{
    AuthDialog dialog(server, prompt, parent);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    const QStringList typed = dialog.takeAnswers();
    if (accepted)
        *answers = typed;
    return accepted;
}

bool DialogPrompter::trustHostKey(const HostKeyInfo &info)
{
    return HostKeyDialog::askTrust(m_parent, info);
}

void DialogPrompter::reportChangedHostKey(const HostKeyInfo &info)
{
    HostKeyDialog::showChanged(m_parent, info);
}

bool DialogPrompter::askSecrets(const ServerAddress &server, const AuthPrompt &prompt,
                                QStringList *answers)
{
    return AuthDialog::ask(m_parent, server, prompt, answers);
}
