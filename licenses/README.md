# Licences

TextEditor+ is licensed under the GNU Affero General Public License version 3
or later: see [`../LICENSE`](../LICENSE).

Packaged builds include third-party libraries, each under its own licence:

| File | What it covers |
|---|---|
| `THIRD-PARTY-NOTICES-macOS.txt` | Every library in the macOS app (Qt, libssh, OpenSSL, ICU, FreeType, HarfBuzz, GLib, libpng, libjpeg-turbo and the rest), with versions, licences, source code addresses and licence texts, plus the third-party code built into Qt. Made from the app bundle by `tools/macos-third-party-notices.py`. |
| `lgpl-3.0.txt`, `gpl-3.0.txt` | Qt (GNU LGPL 3, which adds to the GNU GPL 3) |
| `lgpl-2.1.txt` | GLib, libintl and graphite2 (GNU LGPL 2.1 or later) |
| `libssh-COPYING.txt` | libssh (GNU LGPL 2.1) |
| `OpenSSL-LICENSE.txt` | OpenSSL (Apache 2.0) |
| `spdx/` | Standard licence texts from the [SPDX licence list](https://github.com/spdx/license-list-data), used by the notices script |

The Windows package's licences are in `../packaging/windows/licenses/`.

The app icon was generated with Google's Gemini (the Nano Banana image model); see
[`../resources/ICON.md`](../resources/ICON.md).

All the libraries are separate shared libraries (in `TextEditor+.app/Contents/Frameworks`
on macOS, next to the program on Windows), so you can replace any of them with
your own build. On macOS, re-sign the app afterwards:
`codesign --force --deep -s - TextEditor+.app`.

The macOS release (`tools/make-macos-dmg.sh`) puts these files in
`TextEditor+.app/Contents/Resources/licenses`, and `LICENSE.txt` and a
`Licenses` folder at the top of the disk image.
