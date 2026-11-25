#!/usr/bin/env bash
# Routing quality and time of two router variants on a set of boards (the result may differ, so
# the SES are compared by their statistics, not byte by byte).
#
# usage: tools/optimizer_compare.sh <outdir> "<args A>" "<args B>" [board...]
#   Each board is routed with A and then with B (same load for both). Per run: wall seconds,
#   unrouted connections and clearance violations (router summary), wires, vias and wire length
#   (tools/ses_stats.py). Summary: tools/optimizer_compare_summary.py <outdir>/results.tsv
#   Boards: upstream fixture names (upstream/fixtures/<name>.dsn) or bm01/bm02; default: every board
#   of the Java reference set (FR_JAVA_REF) plus bm01 and bm02.
#   FR_JOBS=4 boards in parallel;  FR_CPP_BIN binary;  FR_TIMEOUT=1800 seconds per run
#   FR_ENV_A / FR_ENV_B: environment of the variant (e.g. "FR_RETAIN_AUTOROUTE_DATABASE=true")
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$1"; argsA="$2"; argsB="$3"; shift 3
ref="${FR_JAVA_REF:-$HOME/frtest/java-reference/upstream-fixtures}"
export CPP="${FR_CPP_BIN:-$HOME/build/fr-release/freerouting-cpp}" OUT="$out" ROOT="$root"
export ARGS_A="$argsA" ARGS_B="$argsB" TMO="${FR_TIMEOUT:-1800}" ENV_A="${FR_ENV_A:-}" ENV_B="${FR_ENV_B:-}"
mkdir -p "$out"
boards() {
  if [ $# -gt 0 ]; then printf '%s\n' "$@"; return; fi
  for rc in "$ref"/*.rc; do basename "$rc" .rc; done
  echo bm01; echo bm02
}
one() {
  name="$1"
  case "$name" in
    bm01|bm02) dsn="$HOME/freerouting-java/fixtures/Issue508-DAC2020/DAC2020_$name/DAC2020_$name.unrouted.dsn" ;;
    *) dsn="$ROOT/upstream/fixtures/$name.dsn" ;;
  esac
  safe="${name// /_}"
  for v in A B; do
    if [ $v = A ]; then a="$ARGS_A"; e="$ENV_A"; else a="$ARGS_B"; e="$ENV_B"; fi
    ses="$OUT/$safe.$v.ses"; log="$OUT/$safe.$v.log"
    s=$(date +%s.%N)
    env $e timeout "$TMO" "$CPP" -q $a -de "$dsn" -do "$ses" > "$log" 2>&1
    rc=$?
    t=$(echo "$(date +%s.%N) - $s" | bc)
    sum=$(grep -ao -- "-\?[0-9]* unrouted, [0-9]* clearance violations" "$log" | tail -1)
    unr=$(echo "$sum" | awk '{print $1}'); viol=$(echo "$sum" | awk '{print $3}')
    if [ -s "$ses" ]; then st=$(python3 "$ROOT/tools/ses_stats.py" --tsv "$ses" | cut -f2-6); else st=$'-\t-\t-\t-\t-'; fi
    printf '%s\t%s\t%s\t%.2f\t%s\t%s\t%s\n' "$safe" "$v" "$rc" "$t" "${unr:--}" "${viol:--}" "$st"
  done
}
export -f one
printf 'board\tvariant\trc\twall_s\tunrouted\tviolations\twires\tsegments\tvias\tlength_mm\tlayers\n' > "$out/results.tsv"
boards "$@" | xargs -d '\n' -P "${FR_JOBS:-4}" -I{} bash -c 'one "$@"' _ {} >> "$out/results.tsv"
python3 "$root/tools/optimizer_compare_summary.py" "$out/results.tsv"
