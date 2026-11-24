#!/usr/bin/env bash
# Speed comparison after the GC removal (MEMORY_MODEL.md): gc-port-baseline (Boehm) vs main, both
# release builds with identical flags (Release, -DJRT_CHECKED=0, LTO; ~/build/bench-base and
# ~/build/bench-main), and Java through the same entry point as tools/bench.sh
# (core.facade.FreeroutingHeadlessEngine via tools/parity/HeadlessRoute.java), with default
# threads and with one thread; main runs with -mt 1 and with -mt 4 (optimizer threads). Runs one
# process at a time (needs an otherwise idle machine) and records wall time, peak RSS, the
# router's result line and whether the SES equals Java's.
#
# usage: tools/bench_speed.sh [outdir] [board...]
#   board: bm01, bm02 or an upstream fixture name (default list below; bm02 is run 3 times)
#   FR_TIMEOUT  per run, seconds (default 5400)
# writes <outdir>/bench.txt (one line per run) and the logs / SES files of every run. Rerunning
# into the same outdir skips the runs bench.txt already has (an interrupted run continues).
set -u
repo="$(cd "$(dirname "$0")/.." && pwd)"
JAR="${FR_JAVA_JAR:-$HOME/freerouting-java/build/libs/freerouting-current-executable.jar}"
BASE="${FR_BASE_BIN:-$HOME/build/bench-base/freerouting-cpp}"
MAIN="${FR_MAIN_BIN:-$HOME/build/bench-main/freerouting-cpp}"
out="${1:-$HOME/frtest/bench-speed}"
shift || true
boards=("$@")
[ ${#boards[@]} -gt 0 ] || boards=(bm02 bm02 bm02 Issue187-processor.Z80 Issue022-AutoRouter_interrupted
  Issue214-freerouting Issue110-RelayModule Issue178-KeebMaker_Sofle_Choc bm01)
mkdir -p "$out"
cd "$repo"
for f in "$JAR" "$BASE" "$MAIN"; do [ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }; done

dsn_of() {
  case "$1" in
    bm01|bm02) echo "$HOME/freerouting-java/fixtures/Issue508-DAC2020/DAC2020_$1/DAC2020_$1.unrouted.dsn" ;;
    *) echo "$repo/upstream/fixtures/$1.dsn" ;;
  esac
}
[ -s "$out/bench.txt" ] ||
  printf '%-34s %-8s %3s %9s %8s %4s %-44s %s\n' board variant rep wall_s peak_MB rc result ses_vs_java | tee "$out/bench.txt"
run() {  # name variant rep cmd...
  local name="$1" variant="$2" rep="$3"; shift 3
  local tag="$name.$variant.$rep"
  # a rerun into the same outdir continues: runs already in bench.txt are kept
  if grep -qE "^$name +$variant +$rep " "$out/bench.txt" 2>/dev/null; then return; fi
  /usr/bin/time -f '%e %M' -o "$out/$tag.time" timeout "${FR_TIMEOUT:-5400}" "$@" > "$out/$tag.log" 2>&1
  local rc=$?
  local wall rss
  read -r wall rss < "$out/$tag.time" 2>/dev/null || { wall=NA; rss=0; }
  local line
  line=$(grep -oE '[0-9]+ unrouted, [0-9]+ clearance violations' "$out/$tag.log" | tail -1)
  local same=-
  if [ "$variant" != java ] && [ -f "$out/$name.java.$rep.ses" ]; then
    cmp -s <(tail -n +3 "$out/$tag.ses") <(tail -n +3 "$out/$name.java.$rep.ses") && same=SAME || same=DIFF
  fi
  printf '%-34s %-8s %3s %9s %8s %4s %-44s %s\n' "$name" "$variant" "$rep" "$wall" "$((rss / 1024))" "$rc" \
    "${line:-}" "$same" | tee -a "$out/bench.txt"
}
declare -A seen
for name in "${boards[@]}"; do
  rep=$(( ${seen[$name]:-0} + 1 )); seen[$name]=$rep
  dsn=$(dsn_of "$name")
  # Java first: the other variants are compared with its SES. Then the two C++ builds back to back.
  run "$name" java "$rep" java -cp "$JAR" "$repo/tools/parity/HeadlessRoute.java" "$dsn" "$out/$name.java.$rep.ses"
  run "$name" java-1t "$rep" java -cp "$JAR" "$repo/tools/parity/HeadlessRoute.java" "$dsn" "$out/$name.java-1t.$rep.ses" 1
  run "$name" base "$rep" "$BASE" -q -de "$dsn" -do "$out/$name.base.$rep.ses"
  # main: one optimizer thread (comparable with the baseline, which has no -mt); main-mt4: four
  run "$name" main "$rep" "$MAIN" -q -mt 1 -de "$dsn" -do "$out/$name.main.$rep.ses"
  run "$name" main-mt4 "$rep" "$MAIN" -q -mt 4 -de "$dsn" -do "$out/$name.main-mt4.$rep.ses"
done
echo "BENCH DONE: $out/bench.txt"
