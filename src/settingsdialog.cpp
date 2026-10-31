#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const Preferences &preferences, const QList<ToolBarButton> &buttons,
                               bool spellCheckAvailable, QWidget *parent)
    : QDialog(parent)
    , m_styleCombo(new QComboBox(this))
    , m_themeCombo(new QComboBox(this))
    , m_lineNumbersBox(new QCheckBox(tr("Show line numbers"), this))
    , m_spellCheckBox(new QCheckBox(tr("Check spelling as you type"), this))
    , m_spellCheckAvailable(spellCheckAvailable)
{
    setWindowTitle(tr("Settings"));

    // Toolbar
    auto *toolBarGroup = new QGroupBox(tr("Toolbar"), this);
    auto *toolBarLayout = new QVBoxLayout(toolBarGroup);
    auto *styleForm = new QFormLayout;
    m_styleCombo->addItem(tr("Icon and text beside it"), int(Qt::ToolButtonTextBesideIcon));
    m_styleCombo->addItem(tr("Text under icon"), int(Qt::ToolButtonTextUnderIcon));
    m_styleCombo->addItem(tr("Icon only"), int(Qt::ToolButtonIconOnly));
    styleForm->addRow(tr("Buttons:"), m_styleCombo);
    toolBarLayout->addLayout(styleForm);
    toolBarLayout->addWidget(new QLabel(tr("Show these buttons:"), toolBarGroup));
    auto *grid = new QGridLayout;
    const int rows = (int(buttons.size()) + 1) / 2;
    for (int i = 0; i < buttons.size(); ++i) {
        auto *box = new QCheckBox(buttons[i].name, toolBarGroup);
        box->setIcon(buttons[i].icon);
        grid->addWidget(box, i % rows, i / rows);
        m_buttonBoxes << box;
        m_buttonIds << buttons[i].id;
        connect(box, &QCheckBox::toggled, this, &SettingsDialog::emitChanged);
    }
    toolBarLayout->addLayout(grid);

    // Appearance
    auto *appearanceGroup = new QGroupBox(tr("Appearance"), this);
    auto *appearanceForm = new QFormLayout(appearanceGroup);
    m_themeCombo->addItem(tr("Dark"), true);
    m_themeCombo->addItem(tr("Light"), false);
    appearanceForm->addRow(tr("Theme:"), m_themeCombo);

    // Editor
    auto *editorGroup = new QGroupBox(tr("Editor"), this);
    auto *editorLayout = new QVBoxLayout(editorGroup);
    editorLayout->addWidget(m_lineNumbersBox);
    editorLayout->addWidget(m_spellCheckBox);
    if (!spellCheckAvailable) {
        m_spellCheckBox->setEnabled(false);
        auto *note = new QLabel(tr("Spell checking isn't available on this system."), editorGroup);
        note->setEnabled(false);
        editorLayout->addWidget(note);
    }

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::RestoreDefaults, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Return closes the dialog; it never resets the settings.
    buttonBox->button(QDialogButtonBox::Close)->setDefault(true);
    buttonBox->button(QDialogButtonBox::RestoreDefaults)->setAutoDefault(false);
    connect(buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        setPreferences(Preferences());
        emitChanged();
    });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(toolBarGroup);
    layout->addWidget(appearanceGroup);
    layout->addWidget(editorGroup);
    layout->addWidget(buttonBox);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    setPreferences(preferences);
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::emitChanged);
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::emitChanged);
    connect(m_lineNumbersBox, &QCheckBox::toggled, this, &SettingsDialog::emitChanged);
    connect(m_spellCheckBox, &QCheckBox::toggled, this, &SettingsDialog::emitChanged);
}

Preferences SettingsDialog::preferences() const
{
    Preferences p;
    p.toolButtonStyle = Qt::ToolButtonStyle(m_styleCombo->currentData().toInt());
    for (int i = 0; i < m_buttonBoxes.size(); ++i) {
        if (!m_buttonBoxes[i]->isChecked())
            p.hiddenButtons << m_buttonIds[i];
    }
    p.darkTheme = m_themeCombo->currentData().toBool();
    p.lineNumbers = m_lineNumbersBox->isChecked();
    p.spellCheck = m_spellCheckBox->isChecked();
    return p;
}

void SettingsDialog::setPreferences(const Preferences &preferences)
{
    m_updating = true;
    m_styleCombo->setCurrentIndex(qMax(0, m_styleCombo->findData(int(preferences.toolButtonStyle))));
    for (int i = 0; i < m_buttonBoxes.size(); ++i)
        m_buttonBoxes[i]->setChecked(!preferences.hiddenButtons.contains(m_buttonIds[i]));
    m_themeCombo->setCurrentIndex(preferences.darkTheme ? 0 : 1);
    m_lineNumbersBox->setChecked(preferences.lineNumbers);
    m_spellCheckBox->setChecked(preferences.spellCheck);
    m_updating = false;
}

void SettingsDialog::emitChanged()
{
    if (!m_updating)
        emit changed(preferences());
}
