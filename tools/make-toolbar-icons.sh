#!/bin/sh
# Makes the toolbar icons' PNGs from their SVG sources in resources/toolbar:
# <name>.png (20 px), <name>@2x.png and <name>@3x.png. The PNGs are what the
# app uses (it doesn't link Qt's SVG module); run this after changing an SVG.
#
# Usage: tools/make-toolbar-icons.sh
# Needs: Google Chrome (to draw the SVGs) and ImageMagick 7 (magick).
set -eu

native=$(cd "$(dirname "$0")/.." && pwd)
icons=$native/resources/toolbar
chrome=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
size=20
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# All the icons in one row on a transparent page, drawn once per scale.
names=$(cd "$icons" && ls *.svg | sed 's/\.svg$//')
count=$(echo "$names" | wc -l | tr -d ' ')
{
    echo '<!doctype html><style>html,body{margin:0;background:transparent}img{display:block;float:left}</style>'
    for name in $names; do
        echo "<img src=\"file://$icons/$name.svg\" width=\"$size\" height=\"$size\">"
    done
} > "$tmp/icons.html"

for scale in 1 2 3; do
    "$chrome" --headless=new --disable-gpu --hide-scrollbars --allow-file-access-from-files \
        --default-background-color=00000000 --force-device-scale-factor=$scale \
        --window-size=$((size * count)),$size --screenshot="$tmp/row-$scale.png" \
        "file://$tmp/icons.html" >/dev/null 2>&1
    px=$((size * scale))
    suffix=$([ "$scale" = 1 ] && echo "" || echo "@${scale}x")
    i=0
    for name in $names; do
        magick "$tmp/row-$scale.png" -crop "${px}x${px}+$((i * px))+0" +repage \
            -define png:color-type=6 "$icons/$name$suffix.png"
        i=$((i + 1))
    done
done
echo "Made $count icons at ${size} px, @2x and @3x in $icons"
