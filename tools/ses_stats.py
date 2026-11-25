#!/usr/bin/env python3
"""Routing statistics of a Specctra session file (.ses), independent of the router.

usage: tools/ses_stats.py [--tsv] file.ses...

Per file: wires (path pieces), segments, vias, total wire length in mm and the length per layer.
--tsv prints one line per file: file, wires, segments, vias, length_mm, layer=length_mm,...
"""
import math
import sys


def tokens(text):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "()":
            yield c
            i += 1
        elif c.isspace():
            i += 1
        elif c == '"':
            j = text.index('"', i + 1)
            yield text[i + 1:j]
            i = j + 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '()"':
                j += 1
            yield text[i:j]
            i = j


def parse(text):
    stack = [[]]
    for t in tokens(text):
        if t == "(":
            stack.append([])
        elif t == ")":
            node = stack.pop()
            stack[-1].append(node)
        else:
            stack[-1].append(t)
    return stack[0]


def walk(node, name):
    if isinstance(node, list):
        if node and node[0] == name:
            yield node
        for child in node:
            yield from walk(child, name)


def stats(path):
    tree = parse(open(path, encoding="utf-8", errors="replace").read())
    # (resolution um 10): coordinates are in 1/10 um
    units_per_mm = 1000.0
    for res in walk(tree, "resolution"):
        unit, value = res[1], float(res[2])
        scale = {"um": 1000.0, "mm": 1.0, "mil": 1 / 0.0254, "inch": 1 / 25.4}.get(unit, 1000.0)
        units_per_mm = scale * value
        break
    result = {"wires": 0, "segments": 0, "vias": 0, "length_mm": 0.0, "layers": {}}
    for net_out in walk(tree, "network_out"):
        for wire in walk(net_out, "wire"):
            for p in walk(wire, "path"):
                layer = p[1]
                coords = [float(v) for v in p[3:] if not isinstance(v, list)]
                pts = list(zip(coords[0::2], coords[1::2]))
                length = sum(math.dist(a, b) for a, b in zip(pts, pts[1:])) / units_per_mm
                result["wires"] += 1
                result["segments"] += max(0, len(pts) - 1)
                result["length_mm"] += length
                result["layers"][layer] = result["layers"].get(layer, 0.0) + length
        result["vias"] += sum(1 for _ in walk(net_out, "via"))
    return result


def main():
    args = sys.argv[1:]
    tsv = "--tsv" in args
    files = [a for a in args if a != "--tsv"]
    for f in files:
        s = stats(f)
        layers = ",".join(f"{k}={v:.3f}" for k, v in sorted(s["layers"].items()))
        if tsv:
            print(f"{f}\t{s['wires']}\t{s['segments']}\t{s['vias']}\t{s['length_mm']:.3f}\t{layers}")
        else:
            print(f"{f}: {s['wires']} wires, {s['segments']} segments, {s['vias']} vias, "
                  f"{s['length_mm']:.3f} mm ({layers})")


if __name__ == "__main__":
    main()
