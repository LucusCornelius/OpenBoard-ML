#!/bin/bash
# Make a distributable OpenBoard-ML.app and a .dmg from a qmake release build made with Homebrew Qt.
#
# usage: release_scripts/osx/deploy-homebrew.sh <version> [output-dir]
#
# macdeployqt from Homebrew Qt leaves two things broken in the bundle:
#  * the Qt WebEngine helper process still links Qt frameworks by their absolute
#    /opt/homebrew paths, so web mode fails on a machine without Homebrew Qt;
#  * the deployed Qt frameworks reference their dependencies (ICU, glib, ...) as
#    @executable_path/../Frameworks/..., which is only right for the main executable,
#    not for the helper process (its own Contents/Frameworks does not exist).
# This script rewrites the helper's links to @rpath, gives the helper a Frameworks link to
# the bundle's Frameworks folder, drops the Homebrew rpath, signs the bundle ad hoc and
# builds a compressed .dmg with an Applications shortcut.
set -euo pipefail

VERSION="${1:?version, e.g. 1.0.0}"
OUT="${2:-$(pwd)/release-$VERSION}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$REPO/build/macx/release/product/OpenBoard-ML.app"
PREFIX="${MAC_LIB_PREFIX:-/opt/homebrew}"
MACDEPLOYQT="${MACDEPLOYQT:-$PREFIX/bin/macdeployqt}"

[ -d "$SRC" ] || { echo "no build at $SRC (run qmake + make first)"; exit 1; }
mkdir -p "$OUT"
APP="$OUT/OpenBoard-ML.app"
rm -rf "$APP"
cp -R "$SRC" "$APP"

echo "== macdeployqt"
"$MACDEPLOYQT" "$APP" > "$OUT/macdeployqt.log" 2>&1

FW="$APP/Contents/Frameworks"
HELPER_APP="$FW/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app"
HELPER="$HELPER_APP/Contents/MacOS/QtWebEngineProcess"

echo "== helper: absolute $PREFIX links -> @rpath"
if [ -f "$HELPER" ]; then
    for old in $(otool -L "$HELPER" | grep -o "$PREFIX/opt/[^ ]*" || true); do
        install_name_tool -change "$old" "@rpath/${old#*/lib/}" "$HELPER" 2>/dev/null
    done
    # @executable_path/../Frameworks of the helper: link it to the bundle's Frameworks
    ln -sfn ../../../../../.. "$HELPER_APP/Contents/Frameworks"
fi

echo "== frameworks and plugins: @executable_path/../Frameworks -> @rpath"
find "$FW" "$APP/Contents/PlugIns" -type f | while IFS= read -r f; do
    file "$f" | grep -q "Mach-O" || continue
    refs=$(otool -L "$f" 2>/dev/null | grep -o "@executable_path/\.\./Frameworks/[^ ]*" || true)
    for old in $refs; do
        install_name_tool -change "$old" "@rpath/${old#@executable_path/../Frameworks/}" "$f" 2>/dev/null || true
    done
done

# the main executable finds the frameworks next to it, never in Homebrew
install_name_tool -delete_rpath "$PREFIX/lib" "$APP/Contents/MacOS/OpenBoard-ML" 2>/dev/null || true

echo "== install names: $PREFIX -> @rpath"
find "$FW" -type f | while IFS= read -r f; do
    file "$f" | grep -q "Mach-O" || continue
    id=$(otool -D "$f" 2>/dev/null | sed -n 2p)
    case "$id" in
        "$PREFIX"/*) install_name_tool -id "@rpath/${id#*/lib/}" "$f" 2>/dev/null || true ;;
    esac
done

echo "== leftover absolute links (should be empty)"
find "$APP" -type f -exec sh -c 'file "$1" | grep -q Mach-O && otool -L "$1" | grep -q "'"$PREFIX"'" && echo "$1"' _ {} \; || true

echo "== ad hoc signature"
codesign --force --deep --sign - "$APP" > "$OUT/codesign.log" 2>&1
codesign --verify --deep --strict "$APP"

echo "== dmg"
DMG="$OUT/OpenBoard-ML-$VERSION-macOS-$(uname -m).dmg"
STAGE="$OUT/dmgroot"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -sfn /Applications "$STAGE/Applications"
hdiutil create -volname "OpenBoard-ML $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG" > "$OUT/hdiutil.log" 2>&1
rm -rf "$STAGE"
shasum -a 256 "$DMG"
echo "done: $DMG"
