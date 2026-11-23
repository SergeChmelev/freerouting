#!/usr/bin/env bash
# Runs a board with an AddressSanitizer build (cmake -DFR_NO_GC=ON -DFR_SANITIZE=ON) and
# summarises the LeakSanitizer report by allocation site (MEMORY_MODEL.md stage E).
# usage: tools/mem/asan_run.sh <binary> <out-prefix> <fixture.dsn> [top]
# writes <out-prefix>.ses, <out-prefix>.asan (raw report: memory errors first, then leaks) and
# <out-prefix>.txt (leaks by site). Exit status: the router's (an ASan error aborts with 1).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
bin="$1"; out="$2"; dsn="$3"; top="${4:-40}"
# halt_on_error: the first memory error is the interesting one; leaks are reported at exit.
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:malloc_context_size=10:fast_unwind_on_malloc=1:log_path=$out.asanlog" \
LSAN_OPTIONS="report_objects=0:max_leaks=0" \
  "$bin" -q -de "$dsn" -do "$out.ses" > "$out.log" 2>&1
rc=$?
cat "$out".asanlog.* > "$out.asan" 2>/dev/null
rm -f "$out".asanlog.*
python3 "$here/lsan_summary.py" "$out.asan" "$top" > "$out.txt"
grep -m1 -E "ERROR: AddressSanitizer" "$out.asan" || true
head -n 3 "$out.txt"
exit $rc
