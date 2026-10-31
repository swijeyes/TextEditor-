#!/bin/sh
# Makes the app icons from the generated image (../icon/texteditor_icon.png):
#   resources/icon.icns      macOS, on the 1024 px grid (824 px tile)
#   resources/icon.ico       Windows, 16 to 256 px
#   resources/icon.png       window icon, 256 px
#   ../user-manual/figures/icon.png   manual title page, 420 px
#
# The generated image has a thin lighter bevel (about 3 px) around its
# rounded square, which shows as a grey border at icon sizes. It is cut off
# by shrinking the rounded square's mask by 5 px (worked at 4x for a smooth
# edge), which leaves a 396x410 image.
#
# Usage: tools/make-icons.sh [SOURCE_PNG]
# Needs: ImageMagick 7 (magick) and iconutil (macOS).
set -eu

native=$(cd "$(dirname "$0")/.." && pwd)
src=${1:-$native/../../icon/texteditor_icon.png}
res=$native/resources
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cp "$src" "$res/texteditor_icon-source.png"

# Remove the bevel. The border before eroding stops the edge of the canvas
# (which the tile touches) from counting as inside.
size=$(magick "$src" -format '%wx%h' info:)
magick "$src" -alpha extract -filter point -resize 400% -threshold 50% \
    -bordercolor black -border 40 -morphology Erode Disk:20 -shave 40x40 \
    -filter box -resize "$size!" "$tmp/mask.png"
magick "$src" "$tmp/mask.png" -alpha off -compose CopyOpacity -composite \
    -background none -alpha background -trim +repage "$tmp/tile.png"

# square SIZE TILE OUT: the tile scaled to fit TILE px, centred on a
# transparent SIZE px square.
square() {
    magick "$tmp/tile.png" -filter Lanczos -resize "$2x$2" -background none \
        -gravity center -extent "$1x$1" -define png:color-type=6 "$3"
}

# macOS: the small sizes use the whole square, as before; from 64 px up the
# tile sits on Apple's grid (824 of 1024 px).
mkdir "$tmp/icon.iconset"
for s in 16 32 64 128 256 512 1024; do
    if [ "$s" -le 32 ]; then t=$s; else t=$((s * 824 / 1024)); fi
    square "$s" "$t" "$tmp/mac-$s.png"
done
set -- 16:icon_16x16 32:icon_16x16@2x 32:icon_32x32 64:icon_32x32@2x \
       128:icon_128x128 256:icon_128x128@2x 256:icon_256x256 \
       512:icon_256x256@2x 512:icon_512x512 1024:icon_512x512@2x
for entry; do
    cp "$tmp/mac-${entry%%:*}.png" "$tmp/icon.iconset/${entry#*:}.png"
done
iconutil -c icns "$tmp/icon.iconset" -o "$res/icon.icns"

# Windows and the window icon: the whole square at every size. The .ico
# holds the PNGs as they are (ImageMagick would store them as BMPs).
files=
for s in 16 24 32 48 64 128 256; do
    square "$s" "$s" "$tmp/win-$s.png"
    files="$files $tmp/win-$s.png"
done
# shellcheck disable=SC2086
python3 - "$res/icon.ico" $files <<'EOF'
import struct, sys
pngs = [open(p, 'rb').read() for p in sys.argv[2:]]
offset = 6 + 16 * len(pngs)
header = struct.pack('<HHH', 0, 1, len(pngs))
entries = b''
for png in pngs:
    w, h = struct.unpack('>II', png[16:24])
    entries += struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, 32, len(png), offset)
    offset += len(png)
open(sys.argv[1], 'wb').write(header + entries + b''.join(pngs))
EOF
cp "$tmp/win-256.png" "$res/icon.png"

square 420 420 "$native/../user-manual/figures/icon.png"
echo "Made icon.icns, icon.ico and icon.png in $res, and the manual's icon."
