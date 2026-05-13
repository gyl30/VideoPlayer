#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR=${1:-"$ROOT_DIR/build"}
DIST_DIR=${2:-"$ROOT_DIR/dist"}
APP_NAME=VideoPlayer
ARCH=${ARCH:-$(uname -m)}
PACKAGE_NAME="${APP_NAME}-macos-${ARCH}"
APP_BUNDLE="${BUILD_DIR}/${APP_NAME}.app"
STAGE_DIR="${DIST_DIR}/${PACKAGE_NAME}"
PACKAGE_BUNDLE="${STAGE_DIR}/${APP_NAME}.app"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
cp -R "$APP_BUNDLE" "$PACKAGE_BUNDLE"

QT_PREFIX=${QT_PREFIX:-$(brew --prefix qt)}
MACDEPLOYQT_BIN=${MACDEPLOYQT_BIN:-"${QT_PREFIX}/bin/macdeployqt"}
DYLIBBUNDLER_BIN=${DYLIBBUNDLER_BIN:-$(command -v dylibbundler)}

"$MACDEPLOYQT_BIN" "$PACKAGE_BUNDLE" -always-overwrite
"$DYLIBBUNDLER_BIN" \
    -od \
    -b \
    -x "${PACKAGE_BUNDLE}/Contents/MacOS/${APP_NAME}" \
    -d "${PACKAGE_BUNDLE}/Contents/Frameworks" \
    -p "@executable_path/../Frameworks" \
    -s "$(brew --prefix)/lib" \
    -s /opt/homebrew/lib \
    -s /usr/local/lib

rm -f "${DIST_DIR}/${PACKAGE_NAME}.dmg"
hdiutil create -volname "${APP_NAME}" -srcfolder "$STAGE_DIR" -ov -format UDZO "${DIST_DIR}/${PACKAGE_NAME}.dmg"
