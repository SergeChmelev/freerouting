#!/usr/bin/env bash
# Performance + result comparison: C++ port vs reference Java on the same boards.
# Runs each board with: java (default threads), java -mt 1, and the C++ port.
# Reports wall time, routing-pipeline time (Java: from its log; excludes JVM start), peak RSS,
# unrouted connections and clearance violations.
# usage: tools/bench.sh <outdir> <fixture.dsn>...
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
CPP="${FR_CPP_BIN:-$HOME/build/freerouting-cpp/freerouting-cpp}"
out="$1"; shift
mkdir -p "$out"
printf '%-34s %-9s %9s %9s %9s %8s %5s\n' board variant wall_s route_s peak_MB unrouted viol | tee "$out/bench.txt"
run() {  # name variant cmd...
  local name="$1" variant="$2"; shift 2
  local log="$out/$name.$variant.log"
  /usr/bin/time -f '%e %M' -o "$out/$name.$variant.time" timeout "${FR_TIMEOUT:-1800}" "$@" > "$log" 2>&1
  read -r wall rss < "$out/$name.$variant.time" || { wall=NA; rss=0; }
  local unrouted viol route
  if [ "$variant" = cpp ]; then
    unrouted=$(grep -oE '[0-9]+ unrouted' "$log" | tail -1 | grep -oE '^[0-9]+')
    viol=$(grep -oE '[0-9]+ clearance violations' "$log" | tail -1 | grep -oE '^[0-9]+')
    route=$wall
  else
    local s=$(grep -oE '\([0-9]+ unrouted and [0-9]+ violations\)' "$log" | tail -1)
    unrouted=$(echo "$s" | grep -oE '[0-9]+ unrouted' | grep -oE '^[0-9]+')
    viol=$(echo "$s" | grep -oE '[0-9]+ violations' | grep -oE '^[0-9]+')
    route=$(grep -oE 'finished with state: [A-Z_]+ \(elapsed: [0-9.]+ seconds' "$log" | grep -oE '[0-9.]+ seconds' | grep -oE '^[0-9.]+')
  fi
  printf '%-34s %-9s %9s %9s %9s %8s %5s\n' "$name" "$variant" "$wall" "${route:-NA}" "$((rss / 1024))" "${unrouted:-NA}" "${viol:-NA}" | tee -a "$out/bench.txt"
}
for dsn in "$@"; do
  name="$(basename "$dsn" .dsn)"
  run "$name" java "$here/java-route.sh" "$dsn" "$out/$name.java.ses"
  run "$name" java-mt1 "$here/java-route.sh" "$dsn" "$out/$name.java-mt1.ses" --router.max_threads=1 --router.optimizer.max_threads=1
  run "$name" cpp "$CPP" -de "$dsn" -do "$out/$name.cpp.ses"
done
