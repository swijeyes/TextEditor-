TextEditor+ 2.5.0 for Windows (portable)
========================================

A lightweight code and text editor. This is the portable version: it needs
no installation and leaves nothing behind on the computer it runs on.

Requirements: Windows 10 or 11, 64-bit (x64).


Running it
----------
1. Extract the whole folder from the zip file, to anywhere you like: your
   Documents folder, the desktop, or a USB stick. Don't run it from inside
   the zip file.
2. Double-click TextEditor+.exe.

The first time, Windows SmartScreen may say "Windows protected your PC",
because the program isn't signed. Click "More info", then "Run anyway".

To open a file, use File > Open, drag the file onto the window, or drag it
onto TextEditor+.exe in File Explorer. Each file opens in its own tab.
File > Open Folder shows a whole folder in the sidebar.


Files on a server (SFTP)
------------------------
File > Connect to Server, Open Remote File and Open Remote Folder work
with any SFTP server. TextEditor+ uses the keys in your .ssh folder
(C:\Users\<you>\.ssh) and .ssh\config, like the ssh command built into
Windows. Keys with a passphrase are asked for when needed. (The Windows
OpenSSH agent isn't used; keys are read from the .ssh folder.)

- The first time you connect to a server, check its key fingerprint
  before choosing "Trust and Connect". The key is then added to
  .ssh\known_hosts.
- If a server's key has changed, TextEditor+ refuses to connect. See the
  user manual before removing the old key.
- Passwords and passphrases are never saved, not even in the "settings"
  folder.


Your settings
-------------
Window size, font, theme, word wrap, your open tabs and folder, recent
files and saved servers are kept in the "settings" folder next to
TextEditor+.exe, not in the Windows registry. They travel with the folder
when you copy it. Passwords are never stored there.

- To reset everything, delete the contents of the "settings" folder.
- If you'd rather keep settings in the registry like an installed program,
  delete the "settings" folder itself.

The folder you run it from must be writable for settings to be saved.


Removing it
-----------
Delete the folder. Nothing else was installed. (A server's key that you
trusted stays in your .ssh\known_hosts file, as it would for ssh.)


More help
---------
"TextEditor+ User Manual.pdf" in this folder describes every feature and
keyboard shortcut.


Licenses
--------
TextEditor+ is free software, copyright (C) 2026 Sanjeeva Wijeyesakere,
released under the GNU Affero General Public License v3.0 or later
(licenses\agpl-3.0.txt). Source code: https://github.com/swijeyes

It includes these third-party components:

- Qt 6.12.0 (Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6PrintSupport.dll,
  Qt6Concurrent.dll and the files in the "platforms" and "styles"
  folders), used under the GNU Lesser General Public License v3
  (licenses\lgpl-3.0.txt, which adds to licenses\gpl-3.0.txt). Qt's source
  code is available from
  https://download.qt.io/official_releases/qt/6.12/6.12.0/ . You may
  replace these DLLs with your own builds of Qt 6.12.

- libssh 0.12.2 (libssh.dll), used under the GNU Lesser General Public
  License v2.1 (licenses\libssh-lgpl-2.1.txt). Source code:
  https://www.libssh.org/files/0.12/ . You may replace libssh.dll with
  your own build.

- OpenSSL 4.0.3 (libcrypto-4-x64.dll), used by libssh, under the Apache
  License v2.0 (licenses\openssl-LICENSE.txt). Source code:
  https://github.com/openssl/openssl/releases/tag/openssl-4.0.3

- The LLVM libc++ and libunwind runtime libraries (libc++.dll and
  libunwind.dll) from llvm-mingw, under the Apache License v2.0 with LLVM
  Exceptions (licenses\llvm-mingw-LICENSE.txt).

- The mingw-w64 winpthreads library (libwinpthread-1.dll), used by
  libssh, under the MIT license (licenses\winpthreads-COPYING.txt).
