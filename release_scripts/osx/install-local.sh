#!/bin/bash
# Install a local OpenBoard-ML build into /Applications, so it shows up between the other apps
# (Launchpad, Spotlight, Finder).
#
# usage: release_scripts/osx/install-local.sh [OpenBoard-ML.app | OpenBoard-ML-*.dmg]
#
# Without an argument the script installs the standalone bundle that deploy-homebrew.sh makes in
# ../OpenBoard-ML-release/v<version>/bundle.noindex; it runs deploy-homebrew.sh first when that bundle is
# missing or older than the build in build/macx/release/product. The standalone bundle carries
# its own Qt, so the installed app keeps working when Homebrew updates Qt.
#
# DEST_DIR=<folder> installs somewhere else than /Applications (e.g. ~/Applications).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DEST_DIR="${DEST_DIR:-/Applications}"
NAME="OpenBoard-ML.app"
DEST="$DEST_DIR/$NAME"

version_field() { sed -n "s/^$1 *= *\([0-9]*\).*/\1/p" "$REPO/version.txt"; }
VERSION="$(version_field VERSION_MAJ).$(version_field VERSION_MIN).$(version_field VERSION_PATCH)"

MOUNT=""
cleanup() {
    if [ -n "$MOUNT" ]; then
        hdiutil detach "$MOUNT" -quiet || true
        rmdir "$MOUNT" 2>/dev/null || true
    fi
    rm -rf "$DEST.new"
}
trap cleanup EXIT

SRC="${1:-}"
if [ -z "$SRC" ]; then
    BUILD_EXE="$REPO/build/macx/release/product/$NAME/Contents/MacOS/OpenBoard-ML"
    OUT="$REPO/../OpenBoard-ML-release/v$VERSION"
    SRC="$OUT/bundle.noindex/$NAME"
    if [ ! -x "$SRC/Contents/MacOS/OpenBoard-ML" ] || [ "$BUILD_EXE" -nt "$SRC/Contents/MacOS/OpenBoard-ML" ]; then
        [ -x "$BUILD_EXE" ] || { echo "no build at $BUILD_EXE (run qmake + make first)"; exit 1; }
        echo "== making a standalone bundle (deploy-homebrew.sh, takes a minute or two)"
        "$REPO/release_scripts/osx/deploy-homebrew.sh" "$VERSION" "$OUT"
    fi
elif [ "${SRC##*.}" = "dmg" ]; then
    [ -f "$SRC" ] || { echo "no such disk image: $SRC"; exit 1; }
    MOUNT="$(mktemp -d /tmp/openboard-ml-dmg.XXXXXX)"
    hdiutil attach "$SRC" -nobrowse -readonly -mountpoint "$MOUNT" -quiet
    SRC="$MOUNT/$NAME"
fi

[ -x "$SRC/Contents/MacOS/OpenBoard-ML" ] || { echo "not an OpenBoard-ML bundle: $SRC"; exit 1; }
[ -d "$DEST_DIR" ] || mkdir -p "$DEST_DIR"
[ -w "$DEST_DIR" ] || { echo "cannot write to $DEST_DIR; use DEST_DIR=\"\$HOME/Applications\" or run with sudo"; exit 1; }

if pgrep -f "$DEST/Contents/MacOS/OpenBoard-ML" > /dev/null; then
    echo "$DEST is running; quit it first and run this script again"
    exit 1
fi

echo "== copying $SRC"
rm -rf "$DEST.new"
ditto "$SRC" "$DEST.new"
rm -rf "$DEST"
mv "$DEST.new" "$DEST"

# a bundle taken from a downloaded disk image is quarantined; this one is built here
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true

if ! codesign --verify --deep --strict "$DEST" 2>/dev/null; then
    echo "== signature does not verify, signing ad hoc"
    codesign --force --deep --sign - "$DEST"
fi

INSTALLED="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$DEST/Contents/Info.plist" 2>/dev/null || echo "$VERSION")"
echo "installed OpenBoard-ML $INSTALLED in $DEST_DIR"
