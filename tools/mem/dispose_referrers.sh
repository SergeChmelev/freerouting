#!/usr/bin/env bash
# Names the referrers in an FR_DISPOSE_CHECK=1 log: "word0=<module>+<offset>" (the first word of
# a referring heap block, usually its vtable pointer) becomes the demangled vtable symbol.
# usage: tools/mem/dispose_referrers.sh <log>...
set -euo pipefail
grep -h -A40 'dispose check: survivor' "$@" | grep -E '^ |survivor' | python3 -c '
import re, subprocess, sys, functools
@functools.lru_cache(None)
def symbols(mod):
    out = subprocess.run(["nm", "-C", "--defined-only", mod], capture_output=True, text=True).stdout
    syms = []
    for l in out.splitlines():
        p = l.split(" ", 2)
        if len(p) == 3:
            syms.append((int(p[0], 16), p[2]))
    syms.sort()
    return syms
def name(mod, off):
    best = None
    for a, s in symbols(mod):
        if a > off: break
        best = (a, s)
    return f"{best[1]}+{off-best[0]:#x}" if best else hex(off)
for line in sys.stdin:
    m = re.search(r"word0=(\S+)\+(0x[0-9a-f]+)", line)
    if m:
        line = line.replace(m.group(0), name(m.group(1), int(m.group(2), 16)))
    sys.stdout.write(line)
'
