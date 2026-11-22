#!/usr/bin/env bash
# Configures (if needed) and builds the C++ port inside WSL. Build tree lives on the Linux
# filesystem for speed; sources stay in the Windows checkout.
# usage: tools/build.sh [target...]
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${FR_BUILD_DIR:-$HOME/build/freerouting-cpp}"
mkdir -p "$BUILD"
if [ ! -f "$BUILD/build.ninja" ]; then
  cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="${FR_BUILD_TYPE:-RelWithDebInfo}" >/dev/null
fi
cmake --build "$BUILD" -j"${FR_JOBS:-16}" ${1:+--target "$@"}
