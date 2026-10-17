#!/usr/bin/env bash
# Linux release build + dist assembly, mirror of package.ps1.
# Run from anywhere; needs cmake, a C++17 toolchain and X11 dev headers.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/linux"
DIST="$ROOT/dist/driver_asvr"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"

rm -rf "$DIST"
mkdir -p "$DIST/asvr/bin/linux64"

cp "$ROOT/packaging/README.txt" "$DIST/README.txt"
cp "$BUILD/config_tool/Config" "$DIST/Config"
# The Linux manifest deliberately omits "redirectsDisplay": the Linux frame
# path is a direct-mode component, not an IVRVirtualDisplay device
cp "$ROOT/packaging/driver.vrdrivermanifest.linux" "$DIST/asvr/driver.vrdrivermanifest"
cp "$ROOT/bin/linux64/driver_asvr.so" \
   "$ROOT/bin/linux64/asvr_monitor" \
   "$ROOT/bin/linux64/asvr_ctl" \
   "$ROOT/bin/linux64/libopenvr_api.so" \
   "$DIST/asvr/bin/linux64/"
cp -r "$ROOT/resources" "$DIST/asvr/resources"

tar -czf "$ROOT/dist/driver_asvr_linux.tar.gz" -C "$ROOT/dist" driver_asvr
echo "dist/driver_asvr_linux.tar.gz ready"
