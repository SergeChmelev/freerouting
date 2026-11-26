#!/usr/bin/env python3
"""Summary of tools/optimizer_compare.sh: variant B against variant A per board and in total.

usage: tools/optimizer_compare_summary.py results.tsv
"""
import math
import sys


def num(v):
    try:
        return float(v)
    except ValueError:
        return None


rows = {}
for line in open(sys.argv[1], encoding="utf-8"):
    f = line.rstrip("\n").split("\t")
    if f[0] == "board":
        continue
    rows.setdefault(f[0], {})[f[1]] = {
        "rc": f[2], "wall": num(f[3]), "unrouted": num(f[4]), "viol": num(f[5]),
        "vias": num(f[8]), "length": num(f[9]),
    }

print(f"{'board':42} {'time A':>8} {'time B':>8} {'B/A':>5}  {'unr A>B':>8} {'viol A>B':>9} "
      f"{'vias A>B':>11} {'len B/A':>8}")
ratios, len_ratios = [], []
worse_unrouted, worse_viol, better_unrouted, better_viol, failed = [], [], [], [], []
vias_a = vias_b = 0
for board in sorted(rows):
    r = rows[board]
    if "A" not in r or "B" not in r:
        continue
    a, b = r["A"], r["B"]
    if a["rc"] != "0" or b["rc"] != "0" or a["unrouted"] is None or b["unrouted"] is None:
        failed.append(f"{board} (rc A {a['rc']}, B {b['rc']})")
        continue
    ratio = b["wall"] / a["wall"] if a["wall"] else float("nan")
    ratios.append(ratio)
    lr = b["length"] / a["length"] if a["length"] else 1.0
    len_ratios.append(lr)
    vias_a += a["vias"] or 0
    vias_b += b["vias"] or 0
    if b["unrouted"] > a["unrouted"]:
        worse_unrouted.append(board)
    if b["unrouted"] < a["unrouted"]:
        better_unrouted.append(board)
    if b["viol"] > a["viol"]:
        worse_viol.append(board)
    if b["viol"] < a["viol"]:
        better_viol.append(board)
    print(f"{board[:42]:42} {a['wall']:8.1f} {b['wall']:8.1f} {ratio:5.2f}  "
          f"{int(a['unrouted']):>3}>{int(b['unrouted']):<4} {int(a['viol']):>4}>{int(b['viol']):<4} "
          f"{int(a['vias'] or 0):>5}>{int(b['vias'] or 0):<5} {lr:8.4f}")


def geo(xs):
    xs = [x for x in xs if x and x > 0 and not math.isnan(x)]
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float("nan")


print()
print(f"boards compared: {len(ratios)}; failed: {len(failed)} {failed if failed else ''}")
print(f"time B/A: geometric mean {geo(ratios):.3f}, min {min(ratios):.3f}, max {max(ratios):.3f}")
print(f"length B/A: geometric mean {geo(len_ratios):.4f}, min {min(len_ratios):.4f}, "
      f"max {max(len_ratios):.4f}")
print(f"vias total: A {int(vias_a)}, B {int(vias_b)} ({(vias_b - vias_a) / vias_a * 100 if vias_a else 0:+.2f}%)")
print(f"unrouted: worse on {len(worse_unrouted)} {worse_unrouted}, better on {len(better_unrouted)}")
print(f"clearance violations: worse on {len(worse_viol)} {worse_viol}, better on {len(better_viol)}")
