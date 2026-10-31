#!/usr/bin/env python3
"""Writes the third-party licence notices for a macOS build of TextEditor+.

macdeployqt copies Homebrew's Qt and the libraries it uses into the app
bundle. Their licences ask for their notices to go with every copy, so this
script lists everything in the bundle's Frameworks folder and writes one
file with, for each library: the Homebrew package it came from, its version,
licence, home page and source code address, and the licence text from the
package itself. It also adds the third-party code built into the Qt modules
the app uses, from Qt's own SBOM (software bill of materials).

Usage:
    tools/macos-third-party-notices.py "build/TextEditor+.app" licenses/THIRD-PARTY-NOTICES-macOS.txt

Run it again whenever Homebrew's packages are updated before a release. It
stops with an error if a bundled library can't be traced to a package.

If the app was built with a Qt other than Homebrew's (such as Qt's own macOS
package), set QT_DIR to it: Qt's version and SBOM are then read from there.
"""

import glob
import json
import os
import re
import subprocess
import sys
import textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
LICENSES = os.path.join(HERE, "..", "licenses")
SPDX = os.path.join(LICENSES, "spdx")

# Licence files in a Homebrew package, by preference. Packages whose own
# file doesn't cover the bundled library are listed in OVERRIDES.
LICENCE_FILES = ["LICENSE", "LICENSE.txt", "LICENSE.TXT", "LICENSE.md", "LICENCE.md",
                 "COPYING", "COPYING.0BSD"]

# Package: (licence files from the package, or from licenses/, and a note).
OVERRIDES = {
    # gettext's COPYING is the GPL for its tools; the bundled libintl is LGPL.
    "gettext": (["@lgpl-2.1.txt"], "libintl is licensed under the GNU LGPL "
                "version 2.1 or later (the gettext tools, not included, are GPL)."),
    # GLib's package has no licence file; GLib is LGPL 2.1 or later.
    "glib": (["@lgpl-2.1.txt"], "GLib is licensed under the GNU LGPL version 2.1 "
             "or later. Copyright (C) The GLib authors."),
    # liblzma is 0BSD; COPYING explains which parts of XZ use which licence.
    "xz": (["COPYING", "COPYING.0BSD"], None),
    # The FreeType licence asks for this credit; its text is in docs/FTL.TXT,
    # which Homebrew doesn't install.
    "freetype": (["LICENSE.TXT", "@spdx/FTL.txt"], "Portions of this software are "
                 "copyright (C) The FreeType Project (https://freetype.org). All rights "
                 "reserved. Used under the FreeType License (FTL)."),
    "jpeg-turbo": (["LICENSE.md", "@spdx/IJG.txt"], "This software is based in part on "
                   "the work of the Independent JPEG Group."),
    # Choose the AFL 2.1 side of D-Bus's AFL-2.1 OR GPL-2.0-or-later.
    "dbus": (["COPYING"], "Used under the Academic Free License version 2.1."),
    "graphite2": (["LICENSE", "COPYING"], "Used under the GNU LGPL version 2.1 or later "
                  "(one of graphite2's licence choices)."),
}

# Third-party code in the Qt modules and plugins shipped in the bundle (from
# Qt's SBOM): Core, Gui, Widgets, PrintSupport, Concurrent, DBus and the
# cocoa platform plugin.
QT_MODULES = ("Core", "Gui", "Widgets", "PrintSupport", "Concurrent", "DBus",
              "QCocoaIntegrationPlugin", "QMacStylePlugin")
QT_SBOM_PACKAGES = ("qtbase", "qtsvg", "qtimageformats")


def cellar():
    return subprocess.check_output(["brew", "--cellar"], text=True).strip()


def owner(dylib):
    """The Homebrew package directory holding this library."""
    matches = glob.glob(os.path.join(cellar(), "*", "*", "lib", dylib))
    if not matches:
        sys.exit(f"error: no Homebrew package contains {dylib}")
    # Newest version if several are installed.
    keg = sorted(os.path.dirname(os.path.dirname(m)) for m in matches)[-1]
    return keg


def brew_info(names):
    out = subprocess.check_output(["brew", "info", "--json=v2", *names], text=True)
    return {f["name"]: f for f in json.loads(out)["formulae"]}


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().rstrip() + "\n"


def licence_text(name, keg):
    files, note = OVERRIDES.get(name, (None, None))
    if files is None:
        files = [next((f for f in LICENCE_FILES if os.path.isfile(os.path.join(keg, f))), None)]
        if files[0] is None:
            sys.exit(f"error: no licence file in {keg}")
    parts = []
    for f in files:
        path = os.path.join(LICENSES, f[1:]) if f.startswith("@") else os.path.join(keg, f)
        if not os.path.isfile(path):
            sys.exit(f"error: missing {path}")
        parts.append(read(path))
    return note, "\n".join(parts)


QT_DIR = os.environ.get("QT_DIR")


def qt_version():
    if QT_DIR:
        return subprocess.check_output([os.path.join(QT_DIR, "bin", "qmake"), "-query", "QT_VERSION"],
                                       text=True).strip()
    return brew_info(["qtbase"])["qtbase"]["versions"]["stable"]


def qt_sboms(package):
    if QT_DIR:
        return glob.glob(os.path.join(QT_DIR, "sbom", f"{package}-*[0-9].spdx"))
    return glob.glob(os.path.join(cellar(), package, "*", "share", "qt", "sbom", "*.spdx"))


def qt_attributions():
    """(name, licence, copyright, comment) for code built into shipped Qt modules."""
    found = []
    for package in QT_SBOM_PACKAGES:
        for spdx in qt_sboms(package):
            text = read(spdx)
            for block in re.split(r"\n(?=PackageName: )", text):
                m = re.match(r"PackageName: (\w+)_Attribution_(.+)", block)
                if not m or m.group(1) not in QT_MODULES:
                    continue
                lic = re.search(r"PackageLicenseConcluded: (.+)", block).group(1).strip()
                cr = re.search(r"PackageCopyrightText: <text>(.*?)</text>", block, re.S)
                title = re.search(r"\n\s*Name: (.+)", block)
                usage = re.search(r"Qt usage: (.+)", block)
                found.append((title.group(1).strip() if title else m.group(2), lic,
                              cr.group(1).strip() if cr else "", usage.group(1).strip() if usage else "",
                              text))
    return found


# Long licence texts are printed once, in an appendix, and referred to.
LONG_TEXT_LINES = 40


def spdx_text(expression, sbom_text, appendix):
    """Licence texts for an SPDX expression. For "A OR B" the first is used."""
    chosen = expression.split(" OR ")[0].strip("() ")
    texts = []
    for lic in re.split(r"\s+AND\s+", chosen):
        lic = lic.strip("() ")
        if lic.startswith("LicenseRef-"):
            m = re.search(r"LicenseID: " + re.escape(lic) + r"\nExtractedText: <text>(.*?)</text>", sbom_text, re.S)
            if not m:
                sys.exit(f"error: no text for {lic} in Qt's SBOM")
            texts.append(m.group(1).strip() + "\n")
        else:
            path = os.path.join(SPDX, lic + ".txt")
            if not os.path.isfile(path):
                sys.exit(f"error: add the SPDX text for {lic} to licenses/spdx/")
            text = read(path)
            if text.count("\n") > LONG_TEXT_LINES:
                appendix.setdefault(lic, text)
                text = f"The text of the {lic} licence is in the appendix at the end of this file.\n"
            texts.append(text)
    return chosen, "\n".join(texts)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    app, output = sys.argv[1:]
    frameworks = os.path.join(app, "Contents", "Frameworks")

    # Bundled libraries, grouped by the Homebrew package they came from.
    packages = {}
    for entry in sorted(os.listdir(frameworks)):
        if entry.endswith(".dylib"):
            keg = owner(entry)
            name = os.path.basename(os.path.dirname(keg))
            packages.setdefault(name, (keg, []))[1].append(entry)
        elif entry.endswith(".framework"):
            packages.setdefault("Qt", (None, []))[1].append(entry)
    qt = packages.pop("Qt", (None, []))[1]
    info = brew_info(sorted(packages)) if packages else {}

    rule = "=" * 78
    out = [
        "TEXTEDITOR+ FOR MACOS - THIRD-PARTY NOTICES",
        rule,
        "",
        "TextEditor+ is copyright (C) 2026 Sanjeeva Wijeyesakere and is licensed under the",
        "GNU Affero General Public License version 3 or later (LICENSE).",
        "",
        textwrap.fill("This copy of TextEditor+ includes the libraries listed below, each under "
                      "its own licence. They are separate shared libraries in "
                      "TextEditor+.app/Contents/Frameworks (and plug-ins in Contents/PlugIns), "
                      "so you can replace any of them with your own build; re-sign the app "
                      "afterwards (codesign --force --deep -s - TextEditor+.app). Their source "
                      "code is available from the addresses given, and the exact versions "
                      "included are those named.", 78),
        "",
        "Generated by tools/macos-third-party-notices.py.",
        "",
        "CONTENTS",
        "",
        "  1. Qt",
    ]
    for i, name in enumerate(sorted(packages), start=2):
        out.append(f"  {i}. {name}: " + ", ".join(packages[name][1]))
    out.append(f"  {len(packages) + 2}. Third-party code inside Qt")
    out.append("")

    # Qt itself.
    plugins = sorted(os.path.relpath(p, os.path.join(app, "Contents", "PlugIns"))
                     for p in glob.glob(os.path.join(app, "Contents", "PlugIns", "*", "*.dylib")))
    out += [rule, "1. Qt " + qt_version(), rule, "",
            "Files: " + ", ".join(qt), "Plug-ins: " + ", ".join(plugins), "",
            textwrap.fill("Qt is copyright (C) The Qt Company Ltd. and other contributors. "
                          "It is used under the GNU Lesser General Public License version 3 "
                          "(lgpl-3.0.txt), which adds permissions to the GNU General Public "
                          "License version 3 (gpl-3.0.txt). Source code: "
                          "https://download.qt.io/official_releases/qt/ and "
                          "https://code.qt.io/. Qt is a registered trademark of The Qt "
                          "Company Ltd.", 78), ""]

    for i, name in enumerate(sorted(packages), start=2):
        keg, files = packages[name]
        meta = info.get(name, {})
        version = os.path.basename(keg)
        note, text = licence_text(name, keg)
        source = (meta.get("urls", {}).get("stable") or {}).get("url", "")
        # The address of the installed version, when Homebrew has a newer one.
        installed = re.sub(r"_\d+$", "", version)
        stable = meta.get("versions", {}).get("stable", installed)
        if installed != stable:
            source = source.replace(stable, installed) if stable in source else \
                f"{source} (that is version {stable}; version {installed} is at the same site)"
        out += [rule, f"{i}. {name} {version}", rule, "",
                "Files: " + ", ".join(files),
                "Licence: " + str(meta.get("license", "see below")),
                "Home page: " + meta.get("homepage", ""),
                "Source code: " + source, ""]
        if note:
            out += [textwrap.fill(note, 78), ""]
        out += [text]

    out += [rule, f"{len(packages) + 2}. Third-party code inside Qt", rule, "",
            textwrap.fill("These components are compiled into the Qt modules and plug-ins "
                          "listed in section 1. Where a component offers a choice of "
                          "licences, the first one listed is used.", 78), ""]
    appendix = {}
    for title, lic, copyright, usage, sbom in qt_attributions():
        chosen, text = spdx_text(lic, sbom, appendix)
        out += ["-" * 78, title, "-" * 78, ""]
        if usage:
            out += [textwrap.fill(usage, 78)]
        out += ["Licence: " + lic + ("" if chosen == lic else f" (used under {chosen})")]
        if copyright and copyright != "NOASSERTION":
            out += ["", copyright]
        out += ["", text]

    if appendix:
        out += [rule, "Appendix: licence texts", rule, ""]
        for lic, text in sorted(appendix.items()):
            out += ["-" * 78, lic, "-" * 78, "", text]

    with open(output, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print(f"wrote {output}: {len(packages)} packages, Qt and its third-party code")


if __name__ == "__main__":
    main()
