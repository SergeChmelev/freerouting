#!/usr/bin/env bash
# Corpus parity check: routes every fixture with the reference Java build and with the C++ port
# (both through core.facade.FreeroutingHeadlessEngine) and compares the SES byte-for-byte.
#
# usage: tools/parity_corpus.sh <outdir> [fixture.dsn ...]     (default: upstream/fixtures/*.dsn)
#   FR_JOBS=6        boards processed in parallel (each board runs Java then C++)
#   FR_TIMEOUT=900   per-run timeout in seconds
#   FR_CPP_BIN / FR_JAVA_JAR  override the binaries
#   FR_SKIP_JAVA=1   reuse existing Java results in <outdir>/java (after a C++ change)
#
# Output: <outdir>/results.tsv (one line per board) and a summary on stdout.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
out="$1"; shift
[ $# -eq 0 ] && set -- "$root"/upstream/fixtures/*.dsn
mkdir -p "$out/java" "$out/cpp"
export OUT="$out" HERE="$here"
export CPP="${FR_CPP_BIN:-$HOME/build/fr-release/freerouting-cpp}"
export JAR="${FR_JAVA_JAR:-$HOME/freerouting-java/build/libs/freerouting-current-executable.jar}"
export TMO="${FR_TIMEOUT:-900}" SKIP_JAVA="${FR_SKIP_JAVA:-0}"

one() {
  dsn="$1"; name="$(basename "$dsn" .dsn)"
  j="$OUT/java/$name"; c="$OUT/cpp/$name"
  if [ "$SKIP_JAVA" != 1 ] || [ ! -f "$j.ses" ]; then
    s=$(date +%s.%N)
    timeout "$TMO" java -cp "$JAR" "$HERE/parity/HeadlessRoute.java" "$dsn" "$j.ses" > "$j.log" 2>&1
    echo "$? $(echo "$(date +%s.%N) - $s" | bc)" > "$j.rc"
  fi
  s=$(date +%s.%N)
  timeout "$TMO" "$CPP" -q -de "$dsn" -do "$c.ses" > "$c.log" 2>&1
  echo "$? $(echo "$(date +%s.%N) - $s" | bc)" > "$c.rc"
  read -r jrc jt < "$j.rc"; read -r crc ct < "$c.rc"
  ju=$(grep -oE '[0-9]+ unrouted, [0-9]+ clearance' "$j.log" | tail -1)
  cu=$(grep -oE '[0-9]+ unrouted, [0-9]+ clearance' "$c.log" | tail -1)
  if [ "$jrc" != 0 ] || [ "$crc" != 0 ]; then verdict="RC(java=$jrc,cpp=$crc)"
  elif cmp -s "$j.ses" "$c.ses"; then verdict=SAME
  else verdict=DIFF; fi
  printf '%s\t%s\t%.1f\t%.1f\t%s\t%s\n' "$name" "$verdict" "$jt" "$ct" "${ju:-?}" "${cu:-?}"
}
export -f one

printf 'board\tverdict\tjava_s\tcpp_s\tjava_result\tcpp_result\n' > "$out/results.tsv"
printf '%s\n' "$@" | xargs -P "${FR_JOBS:-6}" -I{} bash -c 'one "$@"' _ {} >> "$out/results.tsv"

total=$(($(wc -l < "$out/results.tsv") - 1))
same=$(grep -c $'\tSAME\t' "$out/results.tsv")
diff=$(grep -c $'\tDIFF\t' "$out/results.tsv")
rc=$(grep -c $'\tRC(' "$out/results.tsv")
echo "boards: $total  SAME: $same  DIFF: $diff  failed/timeout: $rc"
awk -F'\t' 'NR>1 && $2=="SAME" {j+=$3; c+=$4} END {printf "time on SAME boards: java %.0f s, cpp %.0f s\n", j, c}' "$out/results.tsv"
grep -vE $'\tSAME\t|^board\t' "$out/results.tsv" | sort | head -50
