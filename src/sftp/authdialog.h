#pragma once

#include "sftpconnection.h"

#include <QDialog>
#include <QPointer>

class QLineEdit;

// Asks for a password, a key's passphrase, or the answers to a server's
// keyboard-interactive questions. Nothing typed here is ever stored.
class AuthDialog : public QDialog
{
    Q_OBJECT

public:
    AuthDialog(const ServerAddress &server, const AuthPrompt &prompt, QWidget *parent = nullptr);
    ~AuthDialog() override;

    // Returns the answers and clears the fields.
    QStringList takeAnswers();

    static bool ask(QWidget *parent, const ServerAddress &server, const AuthPrompt &prompt,
                    QStringList *answers);

private:
    QList<QLineEdit *> m_fields;
};

// The SftpPrompter used by the app: asks with HostKeyDialog and AuthDialog.
class DialogPrompter : public SftpPrompter
{
public:
    explicit DialogPrompter(QWidget *parent) : m_parent(parent) {}

    bool trustHostKey(const HostKeyInfo &info) override;
    void reportChangedHostKey(const HostKeyInfo &info) override;
    bool askSecrets(const ServerAddress &server, const AuthPrompt &prompt,
                    QStringList *answers) override;

private:
    QPointer<QWidget> m_parent;
};
