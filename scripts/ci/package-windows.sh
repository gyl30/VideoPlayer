#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR=${1:-"$ROOT_DIR/build"}
DIST_DIR=${2:-"$ROOT_DIR/dist"}
APP_NAME=VideoPlayer
ARCH=${ARCH:-x86_64}
PACKAGE_NAME="${APP_NAME}-windows-${ARCH}"
PACKAGE_DIR="${DIST_DIR}/${PACKAGE_NAME}"
MINGW_BIN=$(dirname "$(command -v g++)")

rm -rf "$PACKAGE_DIR"
rm -f "${DIST_DIR}/${PACKAGE_NAME}.zip"
mkdir -p "$PACKAGE_DIR"

cp "${BUILD_DIR}/${APP_NAME}.exe" "${PACKAGE_DIR}/${APP_NAME}.exe"

find_windeployqt() {
    local candidate
    for candidate in \
        "${MINGW_BIN}/windeployqt6.exe" \
        "${MINGW_BIN}/windeployqt.exe" \
        "/mingw64/bin/windeployqt6.exe" \
        "/mingw64/bin/windeployqt.exe"; do
        [[ -x "$candidate" ]] && echo "$candidate" && return
    done
    return 1
}

should_skip_windows_dep() {
    local dep_path=$1
    local normalized
    normalized=$(printf '%s' "$dep_path" | tr '\\' '/' | tr '[:upper:]' '[:lower:]')

    case "$normalized" in
        /c/windows/*|/c/windows|c:/windows/*|c:/windows)
            return 0
            ;;
    esac

    case "$(basename "$dep_path" | tr '[:lower:]' '[:upper:]')" in
        KERNEL32.DLL|USER32.DLL|GDI32.DLL|WINSPOOL.DRV|COMDLG32.DLL|ADVAPI32.DLL|SHELL32.DLL|OLE32.DLL|OLEAUT32.DLL|UUID.DLL|MPR.DLL|USERENV.DLL|COMBASE.DLL|WS2_32.DLL|MSVCRT.DLL|RPCRT4.DLL|SECHOST.DLL|SHLWAPI.DLL|UCRTBASE.DLL|SETUPAPI.DLL|IMM32.DLL|VERSION.DLL|NTDLL.DLL)
            return 0
            ;;
    esac
    return 1
}

copy_windows_deps() {
    local target=$1
    local dep
    while read -r dep; do
        [[ -n "$dep" && -f "$dep" ]] || continue
        should_skip_windows_dep "$dep" && continue

        local base
        base=$(basename "$dep")
        if [[ ! -f "${PACKAGE_DIR}/${base}" ]]; then
            cp -L "$dep" "${PACKAGE_DIR}/${base}"
            copy_windows_deps "${PACKAGE_DIR}/${base}"
        fi
    done < <(ldd "$target" | awk '/=> \// { print $3 }')
}

WINDEPLOYQT_BIN=$(find_windeployqt)
"$WINDEPLOYQT_BIN" --no-translations --no-system-d3d-compiler "${PACKAGE_DIR}/${APP_NAME}.exe"

copy_windows_deps "${PACKAGE_DIR}/${APP_NAME}.exe"
shopt -s nullglob
for dll in "${PACKAGE_DIR}"/*.dll; do
    copy_windows_deps "$dll"
done
shopt -u nullglob

7z a "${DIST_DIR}/${PACKAGE_NAME}.zip" "${PACKAGE_DIR}/"
