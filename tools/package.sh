#!/usr/bin/env bash
# Linux release build + dist assembly, mirror of package.ps1:
#   driver_asvr/
#     README.txt
#     asvr_monitor       <- the ASVR window: the user runs this (Install and all
#     libopenvr_api.so      settings live in it); the driver also finds it here
#     asvr/              <- the SteamVR driver, registered by the window's Install
#       driver.vrdrivermanifest
#       bin/linux64/     driver_asvr.so, asvr_ctl
#       resources/
# Run from anywhere; needs cmake, a C++17 toolchain and X11 dev headers.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/linux"
DIST="$ROOT/dist/driver_asvr"

# One build covers driver_asvr.so, asvr_monitor and asvr_ctl (all in bin/linux64)
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"

rm -rf "$DIST"
mkdir -p "$DIST/asvr/bin/linux64"

cp "$ROOT/packaging/README.txt" "$DIST/README.txt"
cp "$ROOT/bin/linux64/asvr_monitor" \
   "$ROOT/bin/linux64/libopenvr_api.so" \
   "$DIST/"
# The same manifest serves both platforms
cp "$ROOT/bin/driver.vrdrivermanifest" "$DIST/asvr/driver.vrdrivermanifest"
# Neither the driver nor asvr_ctl links libopenvr_api; only the monitor does
cp "$ROOT/bin/linux64/driver_asvr.so" \
   "$ROOT/bin/linux64/asvr_ctl" \
   "$DIST/asvr/bin/linux64/"
cp -r "$ROOT/resources" "$DIST/asvr/resources"

tar -czf "$ROOT/dist/driver_asvr_linux.tar.gz" -C "$ROOT/dist" driver_asvr
echo "dist/driver_asvr_linux.tar.gz ready"
