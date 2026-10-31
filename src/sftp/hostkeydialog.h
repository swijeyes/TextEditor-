#pragma once

#include "sftpconnection.h"

#include <QDialog>

// Shows a server's host key. For an unknown server it asks whether to trust
// the key (trust on first use); for a server whose key has changed it only
// explains why TextEditor+ refused to connect. There is deliberately no
// "connect anyway" button.
class HostKeyDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode { UnknownHost, ChangedHost };

    HostKeyDialog(const HostKeyInfo &info, Mode mode, QWidget *parent = nullptr);

    static bool askTrust(QWidget *parent, const HostKeyInfo &info);
    static void showChanged(QWidget *parent, const HostKeyInfo &info);
};
