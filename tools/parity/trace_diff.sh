#!/usr/bin/env bash
# Runs Java (debug build) and C++ with full granular tracing on one board and diffs the
# normalised trace streams (timestamps/levels stripped, board hashes and ids masked).
# usage: tools/parity/trace_diff.sh <file.dsn> [outdir]
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
dsn="$1"; out="${2:-/tmp/trace_diff}"
mkdir -p "$out"
name="$(basename "$dsn" .dsn)"
JAR="${FR_JAVA_DBG_JAR:-$HOME/freerouting-java-dbg/build/libs/freerouting-current-executable.jar}"
java -Dfr.traceAll=true -cp "$JAR" "$here/TraceRoute.java" "$dsn" > "$out/$name.java.raw" 2>&1
FR_TRACE_ALL=1 "${FR_BUILD_DIR:-$HOME/build/freerouting-cpp}/trace_route" "$dsn" > "$out/$name.cpp.raw" 2>&1
norm() {
  # keep message text only; mask volatile values (board hashes, job ids, durations, memory)
  sed -E 's/^[0-9-]+ [0-9:.]+ +[A-Z]+ +//' "$1" |
    sed -E "s/\[[0-9A-F]{6}(\\\\[0-9A-F]{6})?\] //; s/board '[0-9a-f]{32}'/board '#'/g; s/in [0-9.]+ seconds/in N seconds/g; s/[0-9.]+ (MB|GB)/N \1/g; s/using [0-9.N\/A]+ (total )?CPU seconds/using N CPU seconds/g" |
    grep -vE '^(Freerouting v|Hardware:|New version|GUI is disabled)'
}
norm "$out/$name.java.raw" > "$out/$name.java.txt"
norm "$out/$name.cpp.raw" > "$out/$name.cpp.txt"
echo "java $(wc -l < "$out/$name.java.txt") lines, cpp $(wc -l < "$out/$name.cpp.txt") lines"
diff "$out/$name.java.txt" "$out/$name.cpp.txt" > "$out/$name.diff"
first=$(grep -m1 -nE '^[0-9]+' "$out/$name.diff")
echo "first difference: $first"
head -${FR_DIFF_LINES:-30} "$out/$name.diff"
