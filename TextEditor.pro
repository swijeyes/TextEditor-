# qmake alternative to CMakeLists.txt, for machines without CMake.
# SFTP support needs libssh (found with pkg-config); build without it with
#   qmake CONFIG+=no_sftp
QT += widgets printsupport concurrent
CONFIG += c++17
TARGET = "TextEditor+"
VERSION = 2.5.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"
INCLUDEPATH += src

SOURCES += \
    src/classicstyle.cpp \
    src/codeeditor.cpp \
    src/document.cpp \
    src/findbar.cpp \
    src/foldersidebar.cpp \
    src/highlighter.cpp \
    src/latexhtml.cpp \
    src/localfilesystem.cpp \
    src/location.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/messagebox.cpp \
    src/preview.cpp \
    src/remotefiledialog.cpp \
    src/remotefilemodel.cpp \
    src/settingsdialog.cpp \
    src/spellchecker.cpp \
    src/texcompiler.cpp \
    src/textcodec.cpp

HEADERS += \
    src/classicstyle.h \
    src/codeeditor.h \
    src/document.h \
    src/filesystem.h \
    src/findbar.h \
    src/foldersidebar.h \
    src/highlighter.h \
    src/latexhtml.h \
    src/localfilesystem.h \
    src/location.h \
    src/mainwindow.h \
    src/messagebox.h \
    src/preview.h \
    src/remotefiledialog.h \
    src/remotefilemodel.h \
    src/settingsdialog.h \
    src/spellchecker.h \
    src/texcompiler.h \
    src/textcodec.h \
    src/theme.h

!no_sftp {
    CONFIG += link_pkgconfig
    PKGCONFIG += libssh
    # 0.11.5 / 0.12.2 fix every libssh security issue known in October 2026.
    !packagesExist("libssh >= 0.11.5"): error("libssh 0.11.5 or later is needed (or build with CONFIG+=no_sftp)")
    DEFINES += TEXTEDITOR_HAVE_SFTP
    win32: LIBS += -lws2_32
    SOURCES += \
        src/sftp/authdialog.cpp \
        src/sftp/connectdialog.cpp \
        src/sftp/connectionmanager.cpp \
        src/sftp/hostkeydialog.cpp \
        src/sftp/sftpconnection.cpp \
        src/sftp/sftpfilesystem.cpp
    HEADERS += \
        src/sftp/authdialog.h \
        src/sftp/connectdialog.h \
        src/sftp/connectionmanager.h \
        src/sftp/hostkeydialog.h \
        src/sftp/sftpconnection.h \
        src/sftp/sftpfilesystem.h
}

# The system's spell checker.
macx {
    OBJECTIVE_SOURCES += src/spellchecker_mac.mm
    LIBS += -framework AppKit
} else: win32 {
    SOURCES += src/spellchecker_win.cpp
    LIBS += -lole32
} else {
    SOURCES += src/spellchecker_none.cpp
}

RESOURCES += resources.qrc resources/toolbar/toolbar.qrc

# Windows program icon; qmake adds version info from VERSION.
win32: RC_ICONS = resources/icon.ico

# Compiler and linker hardening, matching CMakeLists.txt.
win32-msvc {
    QMAKE_CXXFLAGS += /W4 /sdl /guard:cf
    QMAKE_LFLAGS += /guard:cf /DYNAMICBASE /NXCOMPAT
} else {
    QMAKE_CXXFLAGS += -fstack-protector-strong
}
linux {
    QMAKE_CXXFLAGS += -fstack-clash-protection
    QMAKE_CXXFLAGS_RELEASE += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
    QMAKE_LFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
}

macx {
    ICON = resources/icon.icns
    # qmake derives the bundle identifier from TARGET, and "+" isn't allowed
    # in identifiers, so set it explicitly (matching CMakeLists.txt).
    QMAKE_POST_LINK += /usr/libexec/PlistBuddy -c \"Set :CFBundleIdentifier com.sjwrc.texteditor\" \"$${OUT_PWD}/$${TARGET}.app/Contents/Info.plist\"
    # Register document types so files and folders can be dropped on the
    # Dock icon. Delete first so relinking doesn't append a second copy.
    QMAKE_POST_LINK += ; /usr/libexec/PlistBuddy -c \"Delete :CFBundleDocumentTypes\" \"$${OUT_PWD}/$${TARGET}.app/Contents/Info.plist\" 2>/dev/null
    QMAKE_POST_LINK += ; /usr/libexec/PlistBuddy -c \"Merge $${PWD}/resources/DocumentTypes.plist\" \"$${OUT_PWD}/$${TARGET}.app/Contents/Info.plist\"
}
