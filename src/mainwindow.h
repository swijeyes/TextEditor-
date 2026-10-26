#pragma once

#include "highlighter.h"

#include <QMainWindow>

class CodeEditor;
class FindBar;
class QAction;
class QActionGroup;
class QComboBox;
class QFontComboBox;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void loadFile(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void createUi();
    void newFile();
    void open();
    bool save();
    bool saveAs();
    void print();
    void resetDocument();
    void openDroppedFile(const QString &path);
    void openRequestedFile(const QString &path);
    void reveal();
    bool maybeSave();
    bool saveFile(const QString &path);
    void setCurrentFile(const QString &path);

    void setLanguage(Language language);
    void setWordWrap(bool on);
    void setDarkTheme(bool dark);
    void changeFontSize(int delta);
    void resetFontSize();
    void applyFont();
    void goToLine();
    void updateCursorPosition();

    void readSettings();
    void writeSettings() const;

    CodeEditor *m_editor;
    Highlighter *m_highlighter;
    FindBar *m_findBar;

    QFontComboBox *m_fontCombo = nullptr;
    QComboBox *m_languageCombo = nullptr;
    QLabel *m_positionLabel = nullptr;
    QLabel *m_lineEndingLabel = nullptr;
    QAction *m_wrapAction = nullptr;
    QAction *m_darkAction = nullptr;
    QActionGroup *m_languageGroup = nullptr;

    QString m_currentFile;
    bool m_crlf = false;
    bool m_latin1 = false;
    QString m_fontFamily;
    int m_fontSize = 0;
};
