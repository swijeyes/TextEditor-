#!/usr/bin/env python3
"""Removes the Qt plug-ins TextEditor+ doesn't use from a macdeployqt'd app,
then the libraries and frameworks that nothing links to any more.

macdeployqt copies every image-format and icon-engine plug-in it finds
(TIFF, WebP, HEIF, JPEG 2000, SVG...), and the libraries they need.
TextEditor+ only decodes PNG and BMP (built into Qt) and JPEG and GIF (plug-
ins), so the rest add size, licence notices and decoders for files nobody
asked to open.

Universal (x86_64 + arm64) libraries, as in Qt's own macOS package, are
thinned to the program's architecture.

Usage:
    tools/macos-prune-bundle.py "build-release/TextEditor+.app"
"""

import os
import re
import shutil
import subprocess
import sys

# Plug-ins to keep, relative to Contents/PlugIns.
KEEP_PLUGINS = {
    "platforms/libqcocoa.dylib",       # the macOS window system
    "styles/libqmacstyle.dylib",       # the native look
    "imageformats/libqjpeg.dylib",     # JPEG images in the previews
    "imageformats/libqgif.dylib",      # GIF images in the previews
}


def dependencies(binary):
    """Names of the bundled libraries and frameworks a binary links to."""
    out = subprocess.check_output(["otool", "-L", binary], text=True)
    names = set()
    for line in out.splitlines()[1:]:
        path = line.strip().split(" (")[0]
        m = re.search(r"(?:@executable_path|@loader_path|@rpath)(?:/\.\.)*(?:/Frameworks)?/([^/]+)", path)
        if m:
            names.add(m.group(1))
    return names


def framework_binary(frameworks, name):
    if name.endswith(".framework"):
        return os.path.join(frameworks, name, name[: -len(".framework")])
    return os.path.join(frameworks, name)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    contents = os.path.join(sys.argv[1], "Contents")
    plugins = os.path.join(contents, "PlugIns")
    frameworks = os.path.join(contents, "Frameworks")

    # Plug-ins.
    kept = []
    for folder in sorted(os.listdir(plugins)):
        for entry in sorted(os.listdir(os.path.join(plugins, folder))):
            rel = f"{folder}/{entry}"
            if rel in KEEP_PLUGINS:
                kept.append(os.path.join(plugins, rel))
            else:
                os.remove(os.path.join(plugins, rel))
                print(f"removed plug-in {rel}")
        if not os.listdir(os.path.join(plugins, folder)):
            os.rmdir(os.path.join(plugins, folder))
    missing = KEEP_PLUGINS - {os.path.relpath(p, plugins) for p in kept}
    if missing:
        sys.exit(f"error: expected plug-ins missing: {', '.join(sorted(missing))}")

    # Everything reachable from the program and the kept plug-ins.
    executable = os.path.join(contents, "MacOS", "TextEditor+")
    needed, queue = set(), [executable, *kept]
    while queue:
        for name in dependencies(queue.pop()):
            if name not in needed and os.path.exists(os.path.join(frameworks, name)):
                needed.add(name)
                queue.append(framework_binary(frameworks, name))

    for entry in sorted(os.listdir(frameworks)):
        if entry not in needed:
            path = os.path.join(frameworks, entry)
            shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)
            print(f"removed unused {entry}")
    print(f"kept {len(kept)} plug-ins and {len(needed)} libraries/frameworks")

    # A universal library carries code the program can't use.
    arch = subprocess.check_output(["lipo", "-archs", executable], text=True).split()
    if len(arch) == 1:
        for root, _, files in os.walk(contents):
            for f in files:
                path = os.path.join(root, f)
                if os.path.islink(path):
                    continue
                archs = subprocess.run(["lipo", "-archs", path], capture_output=True, text=True)
                if archs.returncode == 0 and len(archs.stdout.split()) > 1:
                    subprocess.check_call(["lipo", path, "-thin", arch[0], "-output", path])
                    print(f"thinned {os.path.relpath(path, contents)} to {arch[0]}")


if __name__ == "__main__":
    main()
