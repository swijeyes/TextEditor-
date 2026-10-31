#include "hostkeydialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

QLabel *plainLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    // Everything here comes from the server or the user's files.
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

} // namespace

HostKeyDialog::HostKeyDialog(const HostKeyInfo &info, Mode mode, QWidget *parent)
    : QDialog(parent)
{
    const QString server = info.server.displayName();
    setWindowTitle(mode == UnknownHost ? tr("Unknown Server") : tr("Server Key Changed"));

    auto *icon = new QLabel(this);
    const int size = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, this);
    icon->setPixmap(style()
                        ->standardIcon(mode == UnknownHost ? QStyle::SP_MessageBoxQuestion
                                                           : QStyle::SP_MessageBoxCritical,
                                       nullptr, this)
                        .pixmap(size, size));
    icon->setAlignment(Qt::AlignTop);

    QLabel *heading = plainLabel(
        mode == UnknownHost
            ? tr("TextEditor+ hasn't connected to %1 before.").arg(server)
            : tr("The host key for %1 has changed. TextEditor+ will not connect.").arg(server),
        this);
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);

    QLabel *explanation = plainLabel(
        mode == UnknownHost
            ? tr("Check that the fingerprint below matches the server's key (ask the server's "
                 "administrator, or run \"ssh-keygen -lf\" on the server's public host key). "
                 "If you trust it, it is added to %1 and you won't be asked again.")
                  .arg(info.knownHostsFile)
            : tr("This can mean that someone is intercepting the connection (a "
                 "man-in-the-middle attack), or that the server was reinstalled or given a new "
                 "key.\n\nIf you know why the key changed, remove the old key from %1 (for "
                 "example with: ssh-keygen -R \"%2\") and connect again.")
                  .arg(info.knownHostsFile,
                       info.server.port == Location::DefaultSftpPort
                           ? info.server.host
                           : QStringLiteral("[%1]:%2").arg(info.server.host).arg(info.server.port)),
        this);

    QLabel *fingerprint = plainLabel(info.fingerprint, this);
    fingerprint->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    fingerprint->setWordWrap(false); // easier to compare in one piece
    fingerprint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *details = new QFormLayout;
    details->addRow(tr("Server:"), plainLabel(server, this));
    details->addRow(tr("Key type:"), plainLabel(info.keyType, this));
    details->addRow(mode == UnknownHost ? tr("Fingerprint:") : tr("New fingerprint:"), fingerprint);

    auto *buttons = new QDialogButtonBox(this);
    if (mode == UnknownHost) {
        QPushButton *trust = buttons->addButton(tr("Trust and Connect"), QDialogButtonBox::AcceptRole);
        QPushButton *cancel = buttons->addButton(QDialogButtonBox::Cancel);
        trust->setAutoDefault(false);
        cancel->setDefault(true);
    } else {
        buttons->addButton(QDialogButtonBox::Close)->setDefault(true);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *text = new QVBoxLayout;
    text->addWidget(heading);
    text->addWidget(explanation);
    text->addLayout(details);
    auto *top = new QHBoxLayout;
    top->addWidget(icon);
    top->addLayout(text, 1);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(buttons);
    setMinimumWidth(520);
}

bool HostKeyDialog::askTrust(QWidget *parent, const HostKeyInfo &info)
{
    HostKeyDialog dialog(info, UnknownHost, parent);
    return dialog.exec() == QDialog::Accepted;
}

void HostKeyDialog::showChanged(QWidget *parent, const HostKeyInfo &info)
{
    HostKeyDialog dialog(info, ChangedHost, parent);
    dialog.exec();
}
