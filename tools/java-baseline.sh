#!/usr/bin/env bash
# Routes a set of fixture boards with the reference Java build and stores SES + log + summary.
# usage: tools/java-baseline.sh <outdir> <fixture.dsn>...
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$1"; shift
mkdir -p "$out"
for dsn in "$@"; do
  name="$(basename "$dsn" .dsn)"
  start=$(date +%s.%N)
  timeout "${FR_TIMEOUT:-900}" "$here/java-route.sh" "$dsn" "$out/$name.ses" > "$out/$name.log" 2>&1
  rc=$?
  end=$(date +%s.%N)
  summary=$(grep -oE 'Auto-routing stage completed.*final score: [0-9.]+ \([0-9]+ unrouted and [0-9]+ violations\)' "$out/$name.log" | grep -oE '\([0-9]+ unrouted and [0-9]+ violations\)' | tail -1)
  printf '%-45s rc=%-3s %7.1fs %s\n' "$name" "$rc" "$(echo "$end - $start" | bc)" "$summary" | tee -a "$out/summary.txt"
done
