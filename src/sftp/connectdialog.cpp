#include "connectdialog.h"

#include "messagebox.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

const char *const ServersKey = "servers";
constexpr int MaxSavedServers = 50;

} // namespace

ConnectDialog::ConnectDialog(const QString &title, const QString &acceptText, QWidget *parent)
    : QDialog(parent)
    , m_address(new QLineEdit(this))
    , m_addressNote(new QLabel(this))
    , m_host(new QLineEdit(this))
    , m_port(new QSpinBox(this))
    , m_user(new QLineEdit(this))
    , m_folder(new QLineEdit(this))
    , m_remember(new QCheckBox(tr("Remember this server"), this))
    , m_saved(new QListWidget(this))
{
    setWindowTitle(title);

    m_address->setPlaceholderText(QStringLiteral("sftp://user@host:port/folder"));
    m_address->setToolTip(tr("Paste an sftp:// address to fill in the fields below."));
    m_addressNote->setTextFormat(Qt::PlainText);
    m_addressNote->setWordWrap(true);
    m_addressNote->hide();
    m_port->setRange(1, 65535);
    m_port->setValue(Location::DefaultSftpPort);
    m_user->setPlaceholderText(tr("Your user name on this computer"));
    m_folder->setPlaceholderText(tr("Home folder"));

    auto *form = new QFormLayout;
    form->addRow(tr("Address:"), m_address);
    form->addRow(QString(), m_addressNote);
    form->addRow(tr("Server:"), m_host);
    form->addRow(tr("Port:"), m_port);
    form->addRow(tr("User name:"), m_user);
    form->addRow(tr("Folder:"), m_folder);
    form->addRow(QString(), m_remember);

    auto *savedLabel = new QLabel(tr("Saved servers:"), this);
    auto *removeButton = new QPushButton(tr("Remove"), this);
    m_removeButton = removeButton;
    auto *savedLayout = new QVBoxLayout;
    savedLayout->addWidget(savedLabel);
    savedLayout->addWidget(m_saved, 1);
    savedLayout->addWidget(removeButton, 0, Qt::AlignLeft);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton *acceptButton = buttons->addButton(acceptText, QDialogButtonBox::AcceptRole);
    acceptButton->setDefault(true);
    m_acceptButton = acceptButton;
    connect(buttons, &QDialogButtonBox::accepted, this, &ConnectDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *columns = new QHBoxLayout;
    columns->addLayout(savedLayout, 2);
    columns->addLayout(form, 3);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(columns);
    layout->addWidget(buttons);

    connect(m_address, &QLineEdit::textEdited, this, &ConnectDialog::parseAddress);
    connect(m_host, &QLineEdit::textChanged, this, &ConnectDialog::updateButtons);
    connect(m_saved, &QListWidget::currentRowChanged, this, [this](int row) {
        const QList<SavedServer> servers = savedServers();
        if (row >= 0 && row < servers.size())
            setServer(servers.at(row).address, servers.at(row).folder);
        updateButtons();
    });
    connect(m_saved, &QListWidget::itemActivated, this, &ConnectDialog::accept);
    connect(removeButton, &QPushButton::clicked, this, [this] {
        const QList<SavedServer> servers = savedServers();
        const int row = m_saved->currentRow();
        if (row >= 0 && row < servers.size())
            removeServer(servers.at(row).address);
        loadSavedServers();
    });

    loadSavedServers();
    updateButtons();
    resize(640, 320);
}

void ConnectDialog::setServer(const ServerAddress &server, const QString &folder)
{
    m_host->setText(server.host);
    m_port->setValue(server.port);
    m_user->setText(server.user);
    m_folder->setText(folder == QLatin1String("~") ? QString() : folder);
    m_address->clear();
    m_addressNote->hide();
}

ServerAddress ConnectDialog::server() const
{
    return {m_host->text().trimmed(), m_port->value(), m_user->text().trimmed()};
}

QString ConnectDialog::folder() const
{
    const QString folder = m_folder->text().trimmed();
    return folder.isEmpty() ? QStringLiteral("~") : folder;
}

void ConnectDialog::parseAddress(const QString &text)
{
    m_addressNote->hide();
    QString input = text.trimmed();
    if (input.isEmpty())
        return;
    if (!input.contains(QLatin1String("://")))
        input.prepend(QLatin1String("sftp://"));
    const QUrl raw(input, QUrl::StrictMode);
    if (!raw.password().isEmpty()) {
        // Never keep a password from an address; it's asked for when needed.
        m_address->setText(Location::stripPassword(raw).toString());
        m_addressNote->setText(tr("The password was removed from the address. You'll be asked "
                                  "for it when connecting; it is never saved."));
        m_addressNote->show();
    }
    const QUrl url = Location::fromUserInput(input);
    if (!Location::isRemote(url))
        return;
    const ServerAddress server = ServerAddress::fromUrl(url);
    m_host->setText(server.host);
    m_port->setValue(server.port);
    m_user->setText(server.user);
    const QString path = Location::path(url);
    m_folder->setText(path == QLatin1String("~") ? QString() : path);
}

void ConnectDialog::updateButtons()
{
    m_acceptButton->setEnabled(!m_host->text().trimmed().isEmpty());
    m_removeButton->setEnabled(m_saved->currentRow() >= 0);
}

void ConnectDialog::loadSavedServers()
{
    const QSignalBlocker blocker(m_saved);
    m_saved->clear();
    for (const SavedServer &server : savedServers()) {
        auto *item = new QListWidgetItem(server.address.displayName(), m_saved);
        item->setToolTip(plainToolTip(Location::displayString(server.address.url(server.folder))));
    }
    updateButtons();
}

void ConnectDialog::accept()
{
    const ServerAddress address = server();
    if (!address.isValid()) {
        showMessage(this, QMessageBox::Warning, windowTitle(),
                    tr("Enter a valid server name, port and user name."));
        return;
    }
    if (folder().contains(QChar(0))) {
        showMessage(this, QMessageBox::Warning, windowTitle(), tr("Enter a valid folder."));
        return;
    }
    if (m_remember->isChecked())
        saveServer({address, folder()});
    QDialog::accept();
}

QList<SavedServer> ConnectDialog::savedServers()
{
    QSettings settings;
    QList<SavedServer> servers;
    const int count = qMin(settings.beginReadArray(ServersKey), MaxSavedServers);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        SavedServer server;
        server.address.host = settings.value("host").toString();
        server.address.port = settings.value("port", Location::DefaultSftpPort).toInt();
        server.address.user = settings.value("user").toString();
        server.folder = settings.value("folder", QStringLiteral("~")).toString();
        if (server.address.isValid())
            servers << server;
    }
    settings.endArray();
    return servers;
}

static void writeServers(const QList<SavedServer> &servers)
{
    QSettings settings;
    settings.remove(ServersKey);
    settings.beginWriteArray(ServersKey, int(servers.size()));
    for (int i = 0; i < servers.size(); ++i) {
        settings.setArrayIndex(i);
        // Host, port, user name and folder only: never a password.
        settings.setValue("host", servers.at(i).address.host);
        settings.setValue("port", servers.at(i).address.port);
        settings.setValue("user", servers.at(i).address.user);
        settings.setValue("folder", servers.at(i).folder);
    }
    settings.endArray();
}

void ConnectDialog::saveServer(const SavedServer &server)
{
    QList<SavedServer> servers = savedServers();
    servers.removeIf([&server](const SavedServer &s) { return s.address == server.address; });
    servers.prepend(server);
    writeServers(servers.mid(0, MaxSavedServers));
}

void ConnectDialog::removeServer(const ServerAddress &server)
{
    QList<SavedServer> servers = savedServers();
    servers.removeIf([&server](const SavedServer &s) { return s.address == server; });
    writeServers(servers);
}
