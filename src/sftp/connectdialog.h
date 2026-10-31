#pragma once

#include "location.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

// A server the user saved: only the address and starting folder, kept in the
// normal settings. Passwords are never saved.
struct SavedServer
{
    ServerAddress address;
    QString folder;
};

// Asks which server (and starting folder) to use: typed in, pasted as an
// sftp:// address, or picked from the saved servers.
class ConnectDialog : public QDialog
{
    Q_OBJECT

public:
    ConnectDialog(const QString &title, const QString &acceptText, QWidget *parent = nullptr);

    void setServer(const ServerAddress &server, const QString &folder);
    ServerAddress server() const;
    // The starting folder, or "~" for the home folder.
    QString folder() const;

    static QList<SavedServer> savedServers();
    static void saveServer(const SavedServer &server);
    static void removeServer(const ServerAddress &server);

    void accept() override;

private:
    void parseAddress(const QString &text);
    void loadSavedServers();
    void updateButtons();

    QLineEdit *m_address;
    QLabel *m_addressNote;
    QLineEdit *m_host;
    QSpinBox *m_port;
    QLineEdit *m_user;
    QLineEdit *m_folder;
    QCheckBox *m_remember;
    QListWidget *m_saved;
    QWidget *m_removeButton;
    QWidget *m_acceptButton;
};
