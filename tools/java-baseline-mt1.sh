#!/usr/bin/env bash
# Single-threaded Java baseline on the same boards; compares SES with the default-thread run.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$1"; ref="$2"; shift 2
mkdir -p "$out"
for dsn in "$@"; do
  name="$(basename "$dsn" .dsn)"
  /usr/bin/time -f '%e %M' -o "$out/$name.time" timeout 1800 "$here/java-route.sh" "$dsn" "$out/$name.ses" \
    --router.max_threads=1 --router.optimizer.max_threads=1 > "$out/$name.log" 2>&1
  read -r wall rss < "$out/$name.time"
  s=$(grep -oE '\([0-9]+ unrouted and [0-9]+ violations\)' "$out/$name.log" | tail -1)
  route=$(grep -oE 'elapsed: [0-9.]+ seconds' "$out/$name.log" | tail -1 | grep -oE '[0-9.]+')
  thr=$(grep -oE 'optimizer.max_threads=[0-9]+' "$out/$name.log" | head -1)
  same=$(cmp -s "$out/$name.ses" "$ref/$name.ses" && echo "SES==default" || echo "SES differs")
  printf '%-32s wall %6.1fs route %7ss %5d MB %s %s %s\n' "$name" "$wall" "$route" $((rss/1024)) "$s" "$thr" "$same" | tee -a "$out/summary.txt"
done
