#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR=${1:-"$ROOT_DIR/build"}
DIST_DIR=${2:-"$ROOT_DIR/dist"}

mkdir -p "$DIST_DIR"
rm -f "$DIST_DIR"/*.deb "$DIST_DIR"/*.rpm

cpack --config "$BUILD_DIR/CPackConfig.cmake" -G DEB -B "$DIST_DIR"
cpack --config "$BUILD_DIR/CPackConfig.cmake" -G RPM -B "$DIST_DIR"
