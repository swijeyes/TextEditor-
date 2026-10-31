#!/bin/sh
# Makes the macOS release: bundles Qt and the other libraries into the app,
# removes the plug-ins and libraries it doesn't use, writes their licence
# notices, signs the app (ad hoc unless SIGN_IDENTITY
# is set) and builds TextEditor+-<version>.dmg with the app, a link to
# Applications, LICENSE and a Licenses folder.
#
#   tools/make-macos-dmg.sh build-release/TextEditor+.app ../release
#
# Set QT_DIR to the Qt the app was built with if it isn't Homebrew's (for
# example Qt's own macOS package); its macdeployqt, version and SBOM are
# then used.
set -eu

app=$1
out=${2:-.}
native=$(cd "$(dirname "$0")/.." && pwd)
version=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$app/Contents/Info.plist")
identity=${SIGN_IDENTITY:--}

"${QT_DIR:+$QT_DIR/bin/}macdeployqt" "$app"
python3 "$native/tools/macos-prune-bundle.py" "$app"

# Notices for exactly what was bundled, kept in licenses/ for the source too.
python3 "$native/tools/macos-third-party-notices.py" "$app" "$native/licenses/THIRD-PARTY-NOTICES-macOS.txt"
mkdir -p "$app/Contents/Resources/licenses"
cp "$native/LICENSE" "$native"/licenses/*.txt "$app/Contents/Resources/licenses/"

if [ "$identity" = "-" ]; then
    codesign --force --deep -s - "$app"
else
    codesign --force --deep --options runtime --timestamp -s "$identity" "$app"
fi
codesign --verify --deep --strict "$app"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
ditto "$app" "$stage/TextEditor+.app"
ln -s /Applications "$stage/Applications"
cp "$native/LICENSE" "$stage/LICENSE.txt"
mkdir "$stage/Licenses"
cp "$native"/licenses/*.txt "$stage/Licenses/"
dmg="$out/TextEditor+-$version.dmg"
hdiutil create -quiet -volname "TextEditor+" -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$dmg"
echo "Made $dmg"
