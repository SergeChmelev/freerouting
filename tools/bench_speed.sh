#!/usr/bin/env bash
# Speed comparison: Java (core.facade.FreeroutingHeadlessEngine via tools/parity/HeadlessRoute.java,
# the same entry point as tools/bench.sh) against release builds of the C++ port. Runs one
# process at a time (needs an otherwise idle machine) and records wall time, peak RSS, the
# router's result line and whether the SES equals the reference (the first Java variant run).
#
# usage: tools/bench_speed.sh [outdir] [board...]
#   board: bm01, bm02 or an upstream fixture name (default list below; bm02 is run 3 times)
#   FR_VARIANTS  variants in run order (default "java java-1t base main main-mt4"):
#                java          Java with its default threads (cores - 1)
#                java-<N>t     Java with N optimizer threads
#                base          FR_BASE_BIN (gc-port-baseline build, no -mt option)
#                main          FR_MAIN_BIN with -mt 1
#                main-mt<N>    FR_MAIN_BIN with -mt N
#                old-mt<N>     FR_OLD_BIN with -mt N (a previous build, for before / after runs
#                              interleaved with the others: a laptop's speed drifts with its heat)
#   FR_TIMEOUT   per run, seconds (default 5400)
# writes <outdir>/bench.txt (one line per run) and the logs / SES files of every run. Rerunning
# into the same outdir skips the runs bench.txt already has (an interrupted run continues).
set -u
repo="$(cd "$(dirname "$0")/.." && pwd)"
JAR="${FR_JAVA_JAR:-$HOME/freerouting-java/build/libs/freerouting-current-executable.jar}"
BASE="${FR_BASE_BIN:-$HOME/build/bench-base/freerouting-cpp}"
MAIN="${FR_MAIN_BIN:-$HOME/build/bench-main/freerouting-cpp}"
OLD="${FR_OLD_BIN:-}"
read -r -a variants <<< "${FR_VARIANTS:-java java-1t base main main-mt4}"
out="${1:-$HOME/frtest/bench-speed}"
shift || true
boards=("$@")
[ ${#boards[@]} -gt 0 ] || boards=(bm02 bm02 bm02 Issue187-processor.Z80 Issue022-AutoRouter_interrupted
  Issue214-freerouting Issue110-RelayModule Issue178-KeebMaker_Sofle_Choc bm01)
mkdir -p "$out"
cd "$repo"
need=("$JAR")
for v in "${variants[@]}"; do
  case "$v" in base) need+=("$BASE") ;; main*) need+=("$MAIN") ;; old*) need+=("${OLD:-FR_OLD_BIN}") ;; esac
done
for f in "${need[@]}"; do [ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }; done
ref=""  # the reference variant: the first Java one
for v in "${variants[@]}"; do case "$v" in java*) ref="$v"; break ;; esac; done
ref="${ref:-java}"

dsn_of() {
  case "$1" in
    bm01|bm02) echo "$HOME/freerouting-java/fixtures/Issue508-DAC2020/DAC2020_$1/DAC2020_$1.unrouted.dsn" ;;
    *) echo "$repo/upstream/fixtures/$1.dsn" ;;
  esac
}
[ -s "$out/bench.txt" ] ||
  printf '%-34s %-9s %3s %9s %8s %4s %-44s %s\n' board variant rep wall_s peak_MB rc result ses_vs_ref | tee "$out/bench.txt"
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
  if [ "$variant" != "$ref" ] && [ -f "$out/$name.$ref.$rep.ses" ]; then
    cmp -s <(tail -n +3 "$out/$tag.ses") <(tail -n +3 "$out/$name.$ref.$rep.ses") && same=SAME || same=DIFF
  fi
  printf '%-34s %-9s %3s %9s %8s %4s %-44s %s\n' "$name" "$variant" "$rep" "$wall" "$((rss / 1024))" "$rc" \
    "${line:-}" "$same" | tee -a "$out/bench.txt"
}
declare -A seen
for name in "${boards[@]}"; do
  rep=$(( ${seen[$name]:-0} + 1 )); seen[$name]=$rep
  dsn=$(dsn_of "$name")
  for v in "${variants[@]}"; do
    ses="$out/$name.$v.$rep.ses"
    case "$v" in
      java) run "$name" "$v" "$rep" java -cp "$JAR" "$repo/tools/parity/HeadlessRoute.java" "$dsn" "$ses" ;;
      java-*t) n="${v#java-}"; n="${n%t}"
        run "$name" "$v" "$rep" java -cp "$JAR" "$repo/tools/parity/HeadlessRoute.java" "$dsn" "$ses" "$n" ;;
      base) run "$name" "$v" "$rep" "$BASE" -q -de "$dsn" -do "$ses" ;;
      main) run "$name" "$v" "$rep" "$MAIN" -q -mt 1 -de "$dsn" -do "$ses" ;;
      main-mt*) n="${v#main-mt}"
        run "$name" "$v" "$rep" "$MAIN" -q -mt "$n" -de "$dsn" -do "$ses" ;;
      old-mt*) n="${v#old-mt}"
        run "$name" "$v" "$rep" "$OLD" -q -mt "$n" -de "$dsn" -do "$ses" ;;
      *) echo "unknown variant: $v" >&2; exit 2 ;;
    esac
  done
done
echo "BENCH DONE: $out/bench.txt"
