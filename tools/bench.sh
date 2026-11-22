#!/usr/bin/env bash
# Performance + result comparison: C++ port vs reference Java, both through the same entry point
# (core.facade.FreeroutingHeadlessEngine). Variants: java (default threads), java-1t, cpp.
# Reports wall time (whole process incl. JVM start), peak RSS, unrouted, violations and whether
# the SES equals Java's.
# usage: tools/bench.sh <outdir> <fixture.dsn>...
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
CPP="${FR_CPP_BIN:-$HOME/build/fr-release/freerouting-cpp}"
JAR="${FR_JAVA_JAR:-$HOME/freerouting-java/build/libs/freerouting-current-executable.jar}"
out="$1"; shift
mkdir -p "$out"
printf '%-30s %-8s %8s %8s %8s %5s %s\n' board variant wall_s peak_MB unrouted viol ses | tee "$out/bench.txt"
run() {  # name variant ses cmd...
  local name="$1" variant="$2" ses="$3"; shift 3
  local log="$out/$name.$variant.log"
  /usr/bin/time -f '%e %M' -o "$out/$name.$variant.time" timeout "${FR_TIMEOUT:-3600}" "$@" > "$log" 2>&1
  local wall rss; read -r wall rss < "$out/$name.$variant.time" || { wall=NA; rss=0; }
  local line; line=$(grep -oE '[0-9]+ unrouted, [0-9]+ clearance violations' "$log" | tail -1)
  local unrouted viol; unrouted=$(echo "$line" | grep -oE '^[0-9]+'); viol=$(echo "$line" | grep -oE '[0-9]+ clearance' | grep -oE '[0-9]+')
  local same=-; [ "$variant" != java ] && { cmp -s "$ses" "$out/$name.java.ses" && same=same || same=DIFF; }
  printf '%-30s %-8s %8s %8s %8s %5s %s\n' "$name" "$variant" "$wall" "$((rss / 1024))" "${unrouted:-NA}" "${viol:-NA}" "$same" | tee -a "$out/bench.txt"
}
for dsn in "$@"; do
  name="$(basename "$dsn" .dsn)"
  run "$name" java "$out/$name.java.ses" java -cp "$JAR" "$here/parity/HeadlessRoute.java" "$dsn" "$out/$name.java.ses"
  run "$name" java-1t "$out/$name.java-1t.ses" java -cp "$JAR" "$here/parity/HeadlessRoute.java" "$dsn" "$out/$name.java-1t.ses" 1
  run "$name" cpp "$out/$name.cpp.ses" "$CPP" -q -de "$dsn" -do "$out/$name.cpp.ses"
done
