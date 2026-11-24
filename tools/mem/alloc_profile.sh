#!/usr/bin/env bash
# Allocation-site profile of heap blocks: resolves the dump a -DJRT_ALLOC_PROFILE=1 build
# (runtime/jrt/heap.cpp, with -fno-omit-frame-pointer) writes at exit: the blocks still
# allocated, by site. Normally run through tools/mem/live_profile.sh (heap blocks) or
# tools/mem/job_profile.sh (job-heap blocks, FR_JOB=1). FR_CPP_ARGS: extra router options.
# usage: tools/mem/alloc_profile.sh <binary> <out-prefix> <fixture.dsn> [top]
# writes <out-prefix>.raw (offset count bytes caller) and <out-prefix>.txt (resolved).
set -euo pipefail
bin="$1"; out="$2"; dsn="$3"; top="${4:-60}"
# FR_JOB=1: the job heap's live blocks instead (sampled; runtime/jrt/jobheap.cpp, FR_JOB_PROFILE).
dumpvar=FR_ALLOC_PROFILE; [ "${FR_JOB:-0}" = 1 ] && dumpvar=FR_JOB_PROFILE
env "$dumpvar=$out.raw" FR_ALLOC_PROFILE_FP="${FR_ALLOC_PROFILE_FP:-1}" "$bin" -q ${FR_CPP_ARGS:-} -de "$dsn" -do "$out.ses" > "$out.log" 2>&1
total=$(awk '{c+=$2; b+=$3} END {printf "%d allocations, %.1f MB", c, b/1048576}' "$out.raw")
head -n 20000 "$out.raw" > "$out.top"
{ cut -d' ' -f1 "$out.top"; cut -d' ' -f4 "$out.top"; } | sort -u | grep -v '^0x0$' | addr2line -f -C -i -e "$bin" -a > "$out.sym"
python3 - "$out.top" "$out.sym" "$top" "$total" > "$out.txt" <<'PY'
import sys, collections, re, os
rows = [l.split() for l in open(sys.argv[1])]
sym = open(sys.argv[2]).read().split('\n')
top = int(sys.argv[3])
frames, cur, i = {}, None, 0
while i < len(sym):
    l = sym[i]
    if l.startswith('0x'):
        cur = int(l, 16); frames[cur] = []; i += 1; continue
    if cur is not None and i + 1 < len(sym):
        frames[cur].append((l, sym[i + 1])); i += 2; continue
    i += 1
def short(f): return re.sub(r'\(.*', '', f)
def loc(fr): return fr[1].split('/src/')[-1].split('/jrt/')[-1].split(' ')[0]
site, siteb = collections.Counter(), collections.Counter()
fn, fnb = collections.Counter(), collections.Counter()
pair, pairb = collections.Counter(), collections.Counter()
for r in rows:
    off, cnt, byt = int(r[0], 16), int(r[1]), int(r[2])
    caller = int(r[3], 16) if len(r) > 3 else 0
    fr = frames.get(off, [('?', '?')])
    key = f'{short(fr[0][0])}  [{loc(fr[0])}]'
    site[key] += cnt; siteb[key] += byt
    outer = short(fr[-1][0])
    fn[outer] += cnt; fnb[outer] += byt
    cf = frames.get(caller, [('?', '?')])
    ck = f'{outer}  <-  {short(cf[0][0])} [{loc(cf[0])}]'
    pair[ck] += cnt; pairb[ck] += byt
print('total:', sys.argv[4])
for title, c, b in (('top sites (innermost frame)', site, siteb), ('top enclosing functions', fn, fnb),
                    ('top enclosing function <- caller', pair, pairb)):
    print(f'\n== {title} ==')
    order = b if os.environ.get("FR_PROFILE_BY_BYTES") else c
    for k, _ in order.most_common(top):
        n = c[k]
        print(f'{n:>12} {b[k]/1048576:>9.1f} MB  {k}')
PY
head -n 1 "$out.txt"
