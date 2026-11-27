#!/usr/bin/env bash
# Profile-guided release build (GCC, Linux): an instrumented build, training runs on a few boards
# (single- and multi-threaded, so both the autorouter and the optimizer are profiled), then the
# optimized build in the same directory (CMakeLists.txt, FR_PGO).
# usage: tools/pgo_build.sh            -> $FR_BUILD_DIR (default ~/build/fr-pgo)/freerouting-cpp
#   FR_PGO_TRAIN  "board:threads ..." (default below)
#   FR_FIXTURES   directory of the fixtures (default upstream/fixtures of this checkout; a git
#                 worktree has none: point it at the main checkout's)
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${FR_BUILD_DIR:-$HOME/build/fr-pgo}"
DATA="$BUILD/pgo-data"
JOBS="${FR_JOBS:-20}"
TRAIN="${FR_PGO_TRAIN:-Issue022-AutoRouter_interrupted:4 Issue187-processor.Z80:4 Issue214-freerouting:4 Issue110-RelayModule:4 Issue178-KeebMaker_Sofle_Choc:10}"
FIXTURES="${FR_FIXTURES:-$SRC/upstream/fixtures}"
[ -d "$FIXTURES" ] || { echo "no fixtures in $FIXTURES (set FR_FIXTURES)" >&2; exit 1; }
BM02="$HOME/freerouting-java/fixtures/Issue508-DAC2020/DAC2020_bm02/DAC2020_bm02.unrouted.dsn"

mkdir -p "$BUILD"
rm -rf "$DATA"
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DFR_STATIC_GMP=ON \
  -DCMAKE_CXX_FLAGS=-DJRT_CHECKED=0 -DFR_PGO=generate "-DFR_PGO_DIR=$DATA" > "$BUILD/pgo-cfg.log"
cmake --build "$BUILD" -j"$JOBS" --target freerouting-cpp > "$BUILD/pgo-gen.log"
echo "instrumented build done; training (the instrumented binary is several times slower)"
out=$(mktemp -d)
if [ -f "$BM02" ]; then
  "$BUILD/freerouting-cpp" -q -de "$BM02" -do "$out/bm02.ses" > /dev/null
fi
for item in $TRAIN; do
  board="${item%%:*}"; threads="${item##*:}"
  start=$(date +%s)
  "$BUILD/freerouting-cpp" -q -mt "$threads" -de "$FIXTURES/$board.dsn" -do "$out/$board.ses" > /dev/null
  echo "  trained on $board -mt $threads ($(( $(date +%s) - start )) s)"
done
rm -rf "$out"
cmake -S "$SRC" -B "$BUILD" -DFR_PGO=use > "$BUILD/pgo-cfg2.log"
cmake --build "$BUILD" -j"$JOBS" --target freerouting-cpp > "$BUILD/pgo-use.log"
echo "optimized build: $BUILD/freerouting-cpp"
