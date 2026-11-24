#!/usr/bin/env bash
# Smoke test for the memory-model migration (~5 minutes): builds the checked and the no-GC
# binaries, routes bm02 with both and the fast fixtures (Java <= 12 s, about 40 boards) against
# the stored Java results. Prints only the verdict counts and the boards that are not SAME.
# usage: tools/smoke.sh [tag]
#   FR_SMOKE_BUILDS  build dirs under ~/build (default "freerouting-cpp fr-nogc")
#   FR_JOBS          parallel runs (default 8)
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
tag="${1:-smoke}"
builds="${FR_SMOKE_BUILDS:-freerouting-cpp fr-nogc}"
bm02="$HOME/freerouting-java/fixtures/Issue508-DAC2020/DAC2020_bm02/DAC2020_bm02.unrouted.dsn"
bm02ref="$HOME/frtest/allocprof/bm02i.ses"
status=0
for b in $builds; do
  (cd "$HOME/build/$b" && ninja freerouting-cpp 2>&1 | grep -E ' error|error:' -A3 | head -20)
  bin="$HOME/build/$b/freerouting-cpp"
  out="$HOME/frtest/$tag-$b"
  mkdir -p "$out"
  FR_MEM_STATS=1 "$bin" -q -de "$bm02" -do "$out/bm02.ses" > "$out/bm02.log" 2>&1
  if cmp -s <(tail -n +3 "$out/bm02.ses") <(tail -n +3 "$bm02ref"); then v=SAME; else v=DIFF; status=1; fi
  leak=$(grep -o 'GC heap [0-9.]* MB' "$out/bm02.log" | tail -1)
  # the no-GC build leaks: keep each run below 3.5 GB of address space
  case "$b" in *nogc*) lim=3500000;; *) lim=unlimited;; esac
  (ulimit -v "$lim"; FR_CPP_BIN="$bin" FR_JOBS="${FR_JOBS:-8}" "$root/tools/parity_sample.sh" "$out/sample" 12 60 > /dev/null 2>&1)
  counts=$(tail -n +2 "$out/sample/results.tsv" | cut -f2 | sort | uniq -c | awk '{printf "%s %s, ", $1, $2}')
  echo "$b: bm02 $v ($leak); sample: ${counts%, }"
  # expected failures: Issue006 (RC 1), Issue179 (RC 139); no-GC: RC(134) = out of memory (leaks)
  tail -n +2 "$out/sample/results.tsv" | awk -F'\t' -v b="$b" '$2 != "SAME" && $1 !~ /^Issue006-|^Issue179-/ && !(b ~ /nogc/ && $2 == "RC(134)") {print "  " $1 "\t" $2; bad=1} END {exit bad}' || status=1
done
exit $status
