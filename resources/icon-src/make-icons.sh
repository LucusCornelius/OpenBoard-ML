#!/bin/sh
# Regenerates the OpenBoard-ML application icon files from the SVG masters.
#
# Needs:
#   - Qt 6 with the Qt SVG module, found through qmake6 or qmake on PATH
#     (or set QMAKE=/path/to/qmake), plus make and a C++17 compiler
#   - iconutil (macOS only) for the .icns; elsewhere the .icns is skipped
#
# Run from anywhere:  sh resources/icon-src/make-icons.sh
#
# Masters (this folder):
#   macos-1024.svg          macOS icon on Apple's 1024 grid (824 px body)
#   macos-16.svg, -32.svg   pixel-aligned macOS 16 and 32 px versions
#   full-16/20/24/32.svg    pixel-aligned versions with the body filling the
#                           canvas, for the Windows .ico
#   mark-512.svg            the mark alone (no body, dim grey board, gap cut
#                           out), for the blackout screen logo
#   ../images/org.openboard-ml.OpenBoard-ML.svg
#                           the same full-canvas artwork at 512 px: Linux icon
#                           and source of the larger .ico sizes
# Outputs:
#   ../macx/OpenBoard.icns  16, 32, 128, 256, 512 at 1x and 2x
#   ../win/OpenBoard.ico    16, 20, 24, 32, 40, 48, 64, 128, 256
#   ../images/OpenBoard.png      64 px window icon (full-canvas artwork)
#   ../images/bigOpenBoard.png   512 px blackout screen logo (mark-512.svg,
#                                transparent background, kept dim on black)
#
# icontool.cpp is a small helper (SVG -> PNG with Qt SVG, PNGs -> .ico);
# it is built in a temporary folder and removed afterwards.

set -e

SRC=$(cd "$(dirname "$0")" && pwd)
RES=$(cd "$SRC/.." && pwd)

if [ -z "$QMAKE" ]; then
    QMAKE=$(command -v qmake6 || command -v qmake || true)
fi
if [ -z "$QMAKE" ]; then
    echo "make-icons.sh: qmake6/qmake not found (set QMAKE)" >&2
    exit 1
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/obicon.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# build the helper
mkdir "$WORK/build"
cp "$SRC/icontool.cpp" "$WORK/build/"
cat > "$WORK/build/icontool.pro" <<'EOF'
QT = core gui svg
CONFIG += console c++17
CONFIG -= app_bundle
SOURCES = icontool.cpp
TARGET = icontool
EOF
(cd "$WORK/build" && "$QMAKE" icontool.pro > /dev/null && make > /dev/null 2>&1) || {
    echo "make-icons.sh: building icontool failed" >&2
    (cd "$WORK/build" && make)
    exit 1
}
TOOL="$WORK/build/icontool"

render() { "$TOOL" render "$1" "$2" "$3"; }

FULL="$RES/images/org.openboard-ml.OpenBoard-ML.svg"
MAC="$SRC/macos-1024.svg"

# macOS .icns
if command -v iconutil > /dev/null 2>&1; then
    SET="$WORK/OpenBoard.iconset"
    mkdir "$SET"
    render "$SRC/macos-16.svg" "$SET/icon_16x16.png"      16
    render "$SRC/macos-32.svg" "$SET/icon_16x16@2x.png"   32
    render "$SRC/macos-32.svg" "$SET/icon_32x32.png"      32
    render "$MAC"              "$SET/icon_32x32@2x.png"   64
    render "$MAC"              "$SET/icon_128x128.png"    128
    render "$MAC"              "$SET/icon_128x128@2x.png" 256
    render "$MAC"              "$SET/icon_256x256.png"    256
    render "$MAC"              "$SET/icon_256x256@2x.png" 512
    render "$MAC"              "$SET/icon_512x512.png"    512
    render "$MAC"              "$SET/icon_512x512@2x.png" 1024
    iconutil -c icns -o "$RES/macx/OpenBoard.icns" "$SET"
    echo "wrote resources/macx/OpenBoard.icns"
else
    echo "iconutil not found: resources/macx/OpenBoard.icns left unchanged"
fi

# Windows .ico
render "$SRC/full-16.svg" "$WORK/ico16.png" 16
render "$SRC/full-20.svg" "$WORK/ico20.png" 20
render "$SRC/full-24.svg" "$WORK/ico24.png" 24
render "$SRC/full-32.svg" "$WORK/ico32.png" 32
for s in 40 48 64 128 256; do
    render "$FULL" "$WORK/ico$s.png" $s
done
"$TOOL" ico "$RES/win/OpenBoard.ico" \
    "$WORK/ico16.png" "$WORK/ico20.png" "$WORK/ico24.png" "$WORK/ico32.png" \
    "$WORK/ico40.png" "$WORK/ico48.png" "$WORK/ico64.png" "$WORK/ico128.png" \
    "$WORK/ico256.png"
echo "wrote resources/win/OpenBoard.ico"

# images used inside the application (resources/OpenBoard.qrc)
render "$FULL" "$RES/images/OpenBoard.png" 64
render "$SRC/mark-512.svg" "$RES/images/bigOpenBoard.png" 512
echo "wrote resources/images/OpenBoard.png and bigOpenBoard.png"
