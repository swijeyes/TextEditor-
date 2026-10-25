# qmake alternative to CMakeLists.txt, for machines without CMake.
QT += widgets printsupport
CONFIG += c++17
TARGET = "TextEditor+"
VERSION = 1.0.0

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/codeeditor.cpp \
    src/findbar.cpp \
    src/highlighter.cpp

HEADERS += \
    src/mainwindow.h \
    src/codeeditor.h \
    src/findbar.h \
    src/highlighter.h \
    src/theme.h

RESOURCES += resources.qrc

macx {
    ICON = resources/icon.icns
    # qmake derives the bundle identifier from TARGET, and "+" isn't allowed
    # in identifiers, so set it explicitly (matching CMakeLists.txt).
    QMAKE_POST_LINK += /usr/libexec/PlistBuddy -c \"Set :CFBundleIdentifier com.sjwrc.texteditor\" \"$${OUT_PWD}/$${TARGET}.app/Contents/Info.plist\"
}
