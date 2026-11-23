#!/usr/bin/env python3
"""Summarises a LeakSanitizer report by allocation site (tools/mem/asan_run.sh).

Groups the "Direct/Indirect leak of N byte(s) in M object(s) allocated from:" records by the
first stack frame outside the allocator (operator new, the jrt allocation helpers, calloc) and
by that frame plus its caller. Direct leaks are objects nothing points to; indirect leaks are
only reachable from leaked objects (they disappear once their direct owner is freed).
usage: lsan_summary.py <report> [top]
"""
import collections
import re
import sys

ALLOCATOR = re.compile(
    r"operator new|operator delete|__interceptor_|calloc|malloc|realloc|aligned_alloc|"
    r"raw_alloc|jrt::geom_alloc|jrt::search_alloc|jrt::board_data_alloc|jrt::alloc_uncollectable|"
    r"jrt::Arena::|std::__new_allocator|std::allocator|__gnu_cxx::new_allocator|"
    r"std::_Vector_base|std::vector<.*>::_M_|std::_Rb_tree<.*>::_M_(create|get)_node|"
    r"std::__cxx11::basic_string<.*>::_M_|jrt::new_root")
LEAK = re.compile(r"^(Direct|Indirect) leak of (\d+) byte\(s\) in (\d+) object\(s\) allocated from:")
FRAME = re.compile(r"^\s+#\d+ 0x[0-9a-f]+ in (.+?) (/\S+|\(\S+\))")


def short(fn):
    fn = re.sub(r"\(.*", "", fn)  # drop parameter lists
    return fn.replace("fr::", "")


def where(path):
    m = re.search(r"/(src|runtime)/(.+)", path)
    return m.group(2) if m else path


def main():
    path = sys.argv[1]
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    sites = {k: collections.Counter() for k in ("Direct", "Indirect")}
    objs = {k: collections.Counter() for k in ("Direct", "Indirect")}
    pairs = collections.Counter()
    totals = collections.Counter()
    kind = None
    frames = []

    def flush():
        if kind is None:
            return
        user = [f for f in frames if not ALLOCATOR.search(f[0])]
        site = f"{short(user[0][0])}  [{where(user[0][1])}]" if user else "?"
        caller = short(user[1][0]) if len(user) > 1 else "?"
        sites[kind][site] += nbytes
        objs[kind][site] += nobj
        if kind == "Direct":
            pairs[f"{site}  <-  {caller}"] += nbytes

    with open(path, errors="replace") as f:
        for line in f:
            m = LEAK.match(line)
            if m:
                flush()
                kind, nbytes, nobj = m.group(1), int(m.group(2)), int(m.group(3))
                totals[kind] += nbytes
                totals[kind + " objects"] += nobj
                frames = []
                continue
            if kind is not None:
                fm = FRAME.match(line)
                if fm:
                    frames.append((fm.group(1), fm.group(2)))
                elif not line.strip():
                    flush()
                    kind = None
    flush()

    mb = 1 << 20
    print(f"direct: {totals['Direct'] / mb:.1f} MB in {totals['Direct objects']} objects; "
          f"indirect: {totals['Indirect'] / mb:.1f} MB in {totals['Indirect objects']} objects")
    for k in ("Direct", "Indirect"):
        print(f"\n== {k.lower()} leaks by site ==")
        for site, b in sites[k].most_common(top):
            print(f"{b / mb:10.1f} MB {objs[k][site]:10d}  {site}")
    print("\n== direct leaks by site <- caller ==")
    for p, b in pairs.most_common(top):
        print(f"{b / mb:10.1f} MB  {p}")


if __name__ == "__main__":
    main()
