# TextEditor+

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)
![Qt 6](https://img.shields.io/badge/Qt-6-41CD52.svg)
![Platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey.svg)

A lightweight code and text editor for macOS, Windows and Linux, written in C++ with Qt 6.

TextEditor+ is fast native text editor that is an OS-native version of "Web TextEditor" (https://sjwrc.com/tools/editor/; https://github.com/swijeyes/Web-TextEditor), a browser-based editor developed by Sanjeeva Wijeyesakere and is hosted on sjwrc.com. It retains the same look and features while adding OS-native file handling, printing, find/replace and more.

![TextEditor+ with a folder in the sidebar and an HTML file open in the dark theme](docs/screenshot.png)

## Features

- **Tabs:** open as many files as you like, each in its own tab; reopen them automatically next time
- **Folder sidebar:** open a folder (on this computer or a server) and browse, create, rename and delete files in it
- **Files on servers (SFTP):** open, edit and save files on any SFTP server, using your SSH keys, `ssh-agent` and `~/.ssh/config`. Server keys are checked strictly, saves are atomic and keep permissions, and changes made by others are detected before you overwrite them. Passwords are never stored.
- **Syntax highlighting** for HTML (including embedded `<script>` and `<style>`), CSS, JavaScript, C, C++, Markdown, Python, Rust and LaTeX, picked automatically from the file extension
- **Live preview:** a sidebar beside LaTeX, Markdown and HTML files that follows your typing. Markdown is shown formatted and HTML as a web page (no scripts, nothing fetched from the internet); LaTeX either instantly (Quick Preview, no TeX needed) or as real typeset pages from pdfLaTeX, XeLaTeX, LuaLaTeX or Tectonic
- **Classic scroll bars:** a glossy blue thumb in a shaded track, with both arrow buttons together at the end
- **Two themes:** Dark (Dracula's colours) and Light
- **Toolbar icons and Settings:** colourful icons with labels beside or under them, or icons alone; hide the buttons you don't use, pick the theme, and turn line numbers and spell checking on or off (⌘, / Ctrl+,)
- **Spell checking** with the system's dictionary (macOS and Windows): misspelled words in text, comments and strings are underlined, with suggestions on right-click; code-like words are skipped
- **Editing:** line numbers, current-line highlight, auto-indent, word wrap
- **Find and replace** with match case, whole word, regular expressions and `\1` back-references
- **Native file handling:** Open, Save, Save As and New Tab with your system's file dialogs; unsaved-changes markers on tabs and in the title bar, and a prompt before closing
- **Printing** of the whole file or a selection, in colours suited to paper
- **Fonts:** pick any installed monospace font and zoom with A+/A− or Ctrl/Cmd + scroll wheel
- **Faithful saving:** keeps each file's line endings (LF/CRLF) and encoding (UTF-8, or Latin-1 for files that aren't valid UTF-8). Saves are atomic, so a failed save never leaves a half-written file.
- **Safeguards:** won't open devices or pipes, asks before opening binary files or files over 50 MB, refuses files over 512 MB, and warns before saving over a file someone else changed
- Open files and folders by dragging them onto the window or the app's icon, with "Open With" in your file manager, from Open Recent, or by passing paths or `sftp://` addresses on the command line
- Remembers window size, font, theme, word-wrap setting, Settings, open tabs and the sidebar folder
- On macOS, closing the window (⇧⌘W) keeps the app running; click its icon or drop a file on it to bring the window back

## Keyboard shortcuts

| Action | macOS | Windows / Linux |
|---|---|---|
| New Tab / Open / Save | ⌘N (or ⌘T) / ⌘O / ⌘S | Ctrl+N (or Ctrl+T) / Ctrl+O / Ctrl+S |
| Save As | ⇧⌘S | Ctrl+Shift+S |
| Open Folder | ⇧⌘O | Ctrl+Shift+O |
| Connect to Server | ⌘K | Ctrl+K |
| Close tab / Close window | ⌘W / ⇧⌘W | Ctrl+W / Ctrl+Shift+W |
| Next / previous tab | ⌃Tab / ⌃⇧Tab | Ctrl+Tab / Ctrl+Shift+Tab |
| Show sidebar | ⌘\ | Ctrl+\ |
| Show preview | ⇧⌘P | Ctrl+Shift+P |
| Print | ⌘P | Ctrl+P |
| Find / Replace | ⌘F / ⌥⌘F | Ctrl+F / Ctrl+H (Ctrl+R on KDE) |
| Find next / previous | ⌘G / ⇧⌘G | F3 / Shift+F3 |
| Go to line | ⌘L | Ctrl+L |
| Settings | ⌘, | Ctrl+, |
| Zoom in / out / reset | ⌘= / ⌘− / ⌘0 | Ctrl+= / Ctrl+− / Ctrl+0 |
| Toggle word wrap | ⌥Z | Alt+Z |

## Building from source

You need **Qt 6.4 or later**, **CMake 3.21 or later**, a C++17 compiler and, for SFTP, **libssh 0.11.5 or later** (0.12.2 recommended). To build without SFTP (and without libssh), add `-DTEXTEDITOR_SFTP=OFF` to the `cmake` configure command; the remote menus are then left out.

### macOS

```sh
brew install qt cmake libssh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libssh)"
cmake --build build
open "build/TextEditor+.app"
```

### Linux (Debian/Ubuntu)

```sh
sudo apt install qt6-base-dev cmake g++ libssh-dev
cmake -S . -B build
cmake --build build
./build/TextEditor+
```

### Windows

The official Windows build is made by GitHub Actions (`.github/workflows/windows.yml`) with Microsoft's compiler and libssh from vcpkg. To build by hand, install Qt 6 with the [Qt Online Installer](https://www.qt.io/download-qt-installer) (choose an MSVC kit), CMake and [vcpkg](https://vcpkg.io), then, from a *Developer Command Prompt for Visual Studio*:

```bat
vcpkg install libssh[openssl]:x64-windows
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64 ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

### Using qmake instead of CMake

```sh
mkdir build && cd build && qmake6 ../TextEditor.pro && make
```

qmake finds libssh with pkg-config; add `CONFIG+=no_sftp` to build without it.

## Tests

```sh
cmake --build build && ctest --test-dir build
```

- **Unit tests** (Qt Test, in `tests/`): text encoding and line endings, `sftp://` addresses and password stripping, folder-listing name checks, the local file system (size limits, devices and FIFOs, atomic saves, conflicts), and the main window run off-screen (tabs, saving, prompts, blank start, keyboard focus at startup, the TeX preview), the Markdown/Python/Rust/LaTeX highlighters, and the LaTeX-to-HTML converter (including hostile input).
- **SFTP integration tests** (`sftp_integration`, macOS and Linux): `tests/sftp/run-integration-tests.sh` starts a throwaway OpenSSH `sshd` as the current user, servers that offer only outdated algorithms, and a deliberately hostile server (`tests/sftp/hostile_server.py`, which needs Python with `paramiko`; set `-DTEXTEDITOR_TEST_PYTHON=/path/to/python`). It covers key, passphrase, password and keyboard-interactive logins, unknown and changed host keys, saving with permissions kept, conflicts, dropped connections, symbolic links, huge and hostile listings, files that never end, slow replies, cancelling and timeouts.
- **Sanitizers:** configure with `-DTEXTEDITOR_SANITIZE="address;undefined"` or `-DTEXTEDITOR_SANITIZE=thread` (for ThreadSanitizer, set `TSAN_OPTIONS=suppressions=tests/sftp/tsan.supp`; the file explains the two Qt-internal entries).
- **TeX sandbox test** (`texcompiler`, runs when a TeX engine is installed): typesets a document, then checks that a hostile one can't run commands or write outside its folder with each engine found.
- **Fuzzers:** configure with `-DTEXTEDITOR_FUZZERS=ON` and a Clang that has libFuzzer (for example Homebrew's `llvm`) to build `fuzz_textcodec`, `fuzz_location`, `fuzz_highlighter` and `fuzz_latexhtml` in `build/tests/fuzz/`. With other compilers they build against a simple standalone driver.

## Packaging

Qt's deployment tools copy the Qt libraries into the app so it runs on machines without Qt installed:

- **macOS:** `tools/make-macos-dmg.sh "build/TextEditor+.app"` runs `macdeployqt`, removes the Qt plug-ins TextEditor+ doesn't use and the libraries only they needed (`tools/macos-prune-bundle.py`; it keeps the macOS platform and style plug-ins and the JPEG and GIF image plug-ins), writes the licence notices for everything left (`tools/macos-third-party-notices.py`), signs the app (ad hoc, or set `SIGN_IDENTITY`) and builds the disk image with `LICENSE.txt` and a `Licenses` folder. `macdeployqt` bundles libssh and OpenSSL too. Keep `libssl` and `libcrypto`: Homebrew's QtCore links them as well as libssh.
- **Windows:** `windeployqt "build\Release\TextEditor+.exe"`, then copy `ssh.dll` and the OpenSSL DLLs from vcpkg's `installed\x64-windows\bin` next to the program (the GitHub Actions workflow does this)
- **Linux:** [`linuxdeploy`](https://github.com/linuxdeploy/linuxdeploy) with its Qt plugin, which builds an AppImage

With Homebrew's Qt, `macdeployqt` reports that it can't resolve `QtSvg` and `QtPdf`. These are only needed by optional SVG/PDF image plugins that TextEditor+ doesn't use; the script removes those plug-ins before signing.

### Signing on macOS

For use on your own computer, an ad-hoc signature is enough:

```sh
codesign --force --deep -s - "build/TextEditor+.app"
```

To give the app to others, sign it with a Developer ID, turn on the hardened runtime, and notarize it:

```sh
codesign --force --deep --options runtime --timestamp \
    -s "Developer ID Application: <Your Name> (<TEAMID>)" "build/TextEditor+.app"
ditto -c -k --keepParent "build/TextEditor+.app" TextEditor+.zip
xcrun notarytool submit TextEditor+.zip --keychain-profile <profile> --wait
xcrun stapler staple "build/TextEditor+.app"
```

Don't combine `--options runtime` with an ad-hoc (`-s -`) signature. The hardened runtime then refuses to load the bundled Qt frameworks, and the app won't start.

SFTP needs no extra entitlements: the hardened runtime doesn't restrict outgoing network connections (only the App Sandbox does, and TextEditor+ isn't sandboxed).

## Project layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | Application startup and command-line handling |
| `src/mainwindow.*` | Window, tabs, menus, toolbar, opening and saving, settings |
| `src/document.*` | One open file (a tab): its editor, highlighter, location, encoding and line endings |
| `src/textcodec.*` | Decoding and encoding file contents (UTF-8/Latin-1, LF/CRLF, binary detection) |
| `src/location.*` | `file://` and `sftp://` addresses, password stripping, listing-name checks |
| `src/filesystem.h` | The asynchronous file-system interface shared by local and remote files |
| `src/localfilesystem.*` | Local files: the opening safeguards and atomic saves |
| `src/sftp/` | SFTP: the connection and its worker thread, the file system, the connection manager, and the connect, login and host-key dialogs |
| `src/foldersidebar.*` | The folder sidebar |
| `src/remotefilemodel.*` | The lazily loaded tree of a server's files, and the sorting/hidden-file filter |
| `src/remotefiledialog.*` | File picker for a server |
| `src/messagebox.*` | Message boxes and tooltips that always show plain text |
| `src/codeeditor.*` | Editor widget: line numbers, current-line highlight, auto-indent, the spelling menu |
| `src/highlighter.*` | Syntax highlighting for each language |
| `src/findbar.*` | Find/replace bar |
| `src/settingsdialog.*` | The Settings dialog: toolbar buttons and their style, theme, line numbers, spell checking |
| `src/spellchecker*` | Spell checking with the system's checker (`_mac.mm`: NSSpellChecker; `_win.cpp`: Windows Spell Checking API; `_none.cpp`: elsewhere) |
| `src/preview.*` | The live preview sidebar: Markdown, HTML and LaTeX (Quick and Typeset modes) |
| `src/classicstyle.*` | The Classic scroll bars |
| `src/latexhtml.*` | LaTeX to HTML for the Quick preview |
| `src/texcompiler.*` | Finds and runs a TeX engine and a PDF renderer, sandboxed, for the Typeset preview |
| `src/theme.h` | The Dark (Dracula) and Light colour schemes |
| `resources/` | App icons (and `ICON.md`, where the icon came from), Info.plist and document types |
| `resources/toolbar/` | The toolbar icons: SVG sources and the PNGs made from them by `tools/make-toolbar-icons.sh` |
| `tests/` | Unit and integration tests, SFTP test servers, fuzzers |
| `LICENSE` | The GNU Affero General Public License v3 |
| `licenses/` | Licences and notices of bundled third-party libraries |
| `tools/` | Release scripts: the macOS disk image and its licence notices, the Windows portable build |
| `CMakeLists.txt`, `TextEditor.pro` | Build files for CMake and qmake |

## Contributing

Bug reports and pull requests are welcome. By submitting a contribution, you agree that it is licensed under the same AGPLv3 terms as the rest of the project.

## License

Copyright © 2026 Sanjeeva Wijeyesakere

TextEditor+ is free software: you can redistribute it and/or modify it under the terms of the **GNU Affero General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

TextEditor+ is distributed in the hope that it will be useful, but **without any warranty**; without even the implied warranty of merchantability or fitness for a particular purpose. See the [GNU Affero General Public License](LICENSE) for more details.

You should have received a copy of the GNU Affero General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

### Third-party components

TextEditor+ is built with the [Qt framework](https://www.qt.io/), used under the [GNU LGPL v3](https://www.gnu.org/licenses/lgpl-3.0.html). Qt's source code is available from [download.qt.io](https://download.qt.io/official_releases/qt/). Qt includes code from other open-source projects, such as HarfBuzz, FreeType, PCRE2, libpng and zlib, each under its own license; builds made with Homebrew's Qt bundle more of them as separate libraries (ICU and GLib, for example). SFTP support uses [libssh](https://www.libssh.org/) (GNU LGPL 2.1, linked dynamically) and [OpenSSL](https://www.openssl.org/) (Apache 2.0). Every bundled library's version, licence, source address and licence text is in `licenses/` (for macOS, `licenses/THIRD-PARTY-NOTICES-macOS.txt`) and in the packaged builds; see [`licenses/README.md`](licenses/README.md). All of these licenses are compatible with the AGPLv3.

The dark theme's colours come from the [Dracula](https://draculatheme.com/) theme (MIT licence, © Dracula Theme), and the Light theme's colours are based on the "eclipse" theme for [CodeMirror](https://codemirror.net/) (MIT licence).

The app icon was generated by Nano Banana, the image model in Google's Gemini; [`resources/ICON.md`](resources/ICON.md) records the prompt and how the icon files were made from it.

### Trademarks

TextEditor+ is an independent project, not affiliated with or endorsed by any of the companies or projects named here. macOS is a trademark of Apple Inc., registered in the U.S. and other countries and regions. Windows and Visual Studio are trademarks of the Microsoft group of companies. Linux® is the registered trademark of Linus Torvalds in the U.S. and other countries. Qt is a registered trademark of The Qt Company Ltd. and its subsidiaries. TeX is a trademark of the American Mathematical Society. Eclipse is a trademark of the Eclipse Foundation, Inc. GitHub is a trademark of GitHub, Inc. Gemini is a trademark of Google LLC. Other names are the property of their respective owners and are used only to identify the platforms, tools and file formats TextEditor+ works with.

## Author

**Sanjeeva Wijeyesakere**: [GitHub @swijeyes](https://github.com/swijeyes) · [sjwrc.com](https://sjwrc.com)
