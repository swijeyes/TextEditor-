#pragma once

#include <QColor>
#include <QString>

// Editor colour schemes, matching the web version of the editor: the dark
// theme is Dracula's palette (draculatheme.com, MIT licence), and the Light
// theme's colours are based on the "eclipse" theme for CodeMirror (MIT
// licence).
struct Theme
{
    bool dark;

    QColor background;
    QColor foreground;
    QColor selection;
    QColor currentLine;
    QColor gutterBackground;
    QColor gutterForeground;
    QColor gutterCurrent;

    QColor keyword;
    QColor type;
    QColor function;
    QColor string;
    QColor number;
    QColor comment;
    QColor preprocessor;
    QColor tag;
    QColor attribute;

    static Theme dracula()
    {
        return {true,
                c("#282a36"), c("#f8f8f2"), c("#44475a"), c("#313341"),
                c("#282a36"), c("#6d8a88"), c("#f8f8f2"),
                c("#ff79c6"), c("#8be9fd"), c("#50fa7b"), c("#f1fa8c"), c("#bd93f9"),
                c("#6272a4"), c("#ff79c6"), c("#ff79c6"), c("#50fa7b")};
    }

    static Theme light()
    {
        return {false,
                c("#ffffff"), c("#000000"), c("#d7d4f0"), c("#e8f2ff"),
                c("#f7f7f7"), c("#999999"), c("#333333"),
                c("#7f0055"), c("#0000c0"), c("#0000ff"), c("#2a00ff"), c("#116644"),
                c("#3f7f5f"), c("#7f0055"), c("#117700"), c("#0000cc")};
    }

private:
    static QColor c(const char *name) { return QColor(QString::fromLatin1(name)); }
};
