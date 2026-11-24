#!/usr/bin/env bash
# Quick C++ parity check against stored Java reference results (no Java run).
#
# usage: tools/parity_sample.sh <outdir> [max_java_seconds] [count]
#   Picks up to <count> boards (default 20) of the reference whose Java run took at most
#   <max_java_seconds> (default 30), routes them with the C++ CLI and compares the SES.
#   FR_JOBS=14  parallel runs;  FR_CPP_BIN  binary (default: release build)
#   FR_JAVA_REF reference dir (default ~/frtest/java-reference/upstream-fixtures; made from a
#               tools/parity_corpus.sh run: <name>.ses/.log/.rc of every successful Java run)
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$1"; max="${2:-30}"; count="${3:-20}"
ref="${FR_JAVA_REF:-$HOME/frtest/java-reference/upstream-fixtures}"
export CPP="${FR_CPP_BIN:-$HOME/build/fr-release/freerouting-cpp}" REF="$ref" OUT="$out" ROOT="$root"
mkdir -p "$out"
pick() {
  for rc in "$ref"/*.rc; do
    read -r code secs < "$rc"
    awk -v s="$secs" -v m="$max" 'BEGIN { exit !(s <= m) }' && basename "$rc" .rc
  done | head -n "$count"
}
one() {
  name="$1"
  s=$(date +%s.%N)
  timeout 900 "$CPP" -q -de "$ROOT/upstream/fixtures/$name.dsn" -do "$OUT/$name.ses" > "$OUT/$name.log" 2>&1
  rc=$?
  t=$(echo "$(date +%s.%N) - $s" | bc)
  read -r jc jt < "$REF/$name.rc"
  # A failed Java run ("-1 unrouted", empty SES) is matched by a failed C++ run (rc 1, empty SES).
  if [ $rc = 1 ] && grep -aq -- '-1 unrouted' "$REF/$name.log"; then rc=0; fi
  if [ $rc != 0 ]; then v="RC($rc)"; elif cmp -s "$OUT/$name.ses" "$REF/$name.ses"; then v=SAME; else v=DIFF; fi
  printf '%s\t%s\t%.1f\t%.1f\n' "$name" "$v" "$jt" "$t"
}
export -f one
printf 'board\tverdict\tjava_s\tcpp_s\n' > "$out/results.tsv"
pick | xargs -P "${FR_JOBS:-14}" -I{} bash -c 'one "$@"' _ {} >> "$out/results.tsv"
cut -f2 "$out/results.tsv" | tail -n +2 | sort | uniq -c
grep -vE $'\tSAME\t|^board\t' "$out/results.tsv"
exit 0
