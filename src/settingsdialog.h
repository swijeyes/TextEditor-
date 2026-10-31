#pragma once

#include <QDialog>
#include <QIcon>
#include <QStringList>

class QCheckBox;
class QComboBox;

// The choices in the Settings dialog, with their defaults.
struct Preferences
{
    Qt::ToolButtonStyle toolButtonStyle = Qt::ToolButtonTextBesideIcon;
    QStringList hiddenButtons; // ids of toolbar buttons not shown
    bool darkTheme = true;
    bool lineNumbers = true;
    bool spellCheck = true;
};

// A toolbar button that can be shown or hidden.
struct ToolBarButton
{
    QString id;
    QString name;
    QIcon icon;
};

// Settings for the toolbar, the theme and the editor. Each change applies
// straight away (changed() is emitted); there is no OK or Cancel.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(const Preferences &preferences, const QList<ToolBarButton> &buttons,
                   bool spellCheckAvailable, QWidget *parent = nullptr);

    Preferences preferences() const;
    void setPreferences(const Preferences &preferences);

signals:
    void changed(const Preferences &preferences);

private:
    void emitChanged();

    QComboBox *m_styleCombo;
    QComboBox *m_themeCombo;
    QList<QCheckBox *> m_buttonBoxes;
    QStringList m_buttonIds;
    QCheckBox *m_lineNumbersBox;
    QCheckBox *m_spellCheckBox;
    bool m_spellCheckAvailable;
    bool m_updating = false;
};
