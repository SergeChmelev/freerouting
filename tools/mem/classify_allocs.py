#!/usr/bin/env python3
"""Allocation-site classification for MEMORY_MODEL.md stage A.

  classify_allocs.py rewrite   rewrites every untagged `new T` in src/ and bindings/lua/ into
                               `new (jrt::kBoard|kOp|kGeom) T` using the heuristics below
  classify_allocs.py report    prints a markdown table: allocation sites per package/lifetime
  classify_allocs.py check     lists untagged `new` sites (exit 1 if there are any)

Heuristics (first match wins):
  1. allocated type is a geometry type (geometry/planar) or an array of geometry types -> kGeom
  2. allocated type is a long-lived ("board") class: items, rules, library, layer structure,
     board facade/state, search trees and their nodes, undo nodes, settings, job, logger -> kBoard
  3. a `static` local (singleton accessor) -> kBoard
  4. the value is stored into a member (constructor body / init list, `this->f =`, `f =` where
     `f` is a field of the enclosing class, default member initializer) -> lifetime of the
     enclosing class (board class -> kBoard, geometry class -> kGeom, otherwise kOp)
  5. otherwise (locals, return values, arguments, temporaries) -> kOp
Manual tags written in the sources are never changed by `rewrite`.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC_DIRS = ["src", "bindings/lua"]
TAGS = {"kBoard": "board", "kOp": "op", "kGeom": "geom", "kSearch": "search", "kBoardData": "boarddata"}

# ---------------------------------------------------------------- class tables
classmap = {}  # simple C++ name -> header (relative to src/)
with open(os.path.join(ROOT, "tools/classmap.tsv"), encoding="utf-8") as f:
    for line in f:
        if line.startswith("#"):
            continue
        cols = line.rstrip("\n").split("\t")
        if len(cols) >= 4:
            classmap[cols[1].split("::")[-1]] = cols[3]

BOARD_PKGS = (
    "board/model/", "board/trace/", "board/state/", "board/facade/", "rules/", "core/library/",
    "settings/", "logger/", "core/facade/", "core/results/", "core/events/",
)
BOARD_EXTRA = {
    # long-lived objects outside the packages above
    "ShapeSearchTree", "ShapeSearchTree45Degree", "ShapeSearchTree90Degree", "SearchTreeManager",
    "ShapeTree_Leaf", "ShapeTree_InnerNode", "ShapeTree_TreeNode", "MinAreaTree", "ShapeTree",
    "UndoableObjects", "UndoableObjects_UndoableObjectNode", "UndoableObjects_Undoable",
    "IdGenerator", "Observers", "ExactCopper", "RoutingJob", "RoutingJobState", "StoppableThread",
    "BoardFileDetails", "RouterCounters", "RouterJobResourceUsage", "ProgressThrottler",
    "BoardHistory", "BoardHistoryEntry", "RoutingFailureLog", "InteractiveSession",
    "ItemIdGenerator", "TextManager", "RuntimeTelemetryProvider", "DesignBlocks",
    "ShoveCheckCache", "ViaMoveHistory", "CoordinateTransform",
}
OP_EXTRA = {
    # temporaries inside "board" packages
    "ShapeTraceEntries", "ShapeTraceEntries_EntryPoint", "ShapeSearchTree_EntrySortedByClearance",
    "ShapeTree_TreeEntry", "ShapeTree_FoundOverlap", "RoutingBoardUndoFacade_AreaRecordingOutputStream",
    "RoutingBoardUndoFacade_AreaRestoringInputStream", "BasicBoard_DominantSide",
    "ChangedArea", "ChangedArea_MutableOctagon",
}


def lifetime_of_class(name):
    """Lifetime of objects of class `name` (None if unknown / not a ported class)."""
    if name in OP_EXTRA:
        return "op"
    if name in BOARD_EXTRA:
        return "board"
    hdr = classmap.get(name)
    if hdr is None:
        return None
    if hdr.startswith("geometry/planar/"):
        return "geom"
    if hdr.startswith(BOARD_PKGS):
        return "board"
    return "op"


# ---------------------------------------------------------------- lexer
def code_mask(text):
    """Returns a list of booleans: True where text[i] is code (not comment / string / char)."""
    n = len(text)
    mask = [True] * n
    i = 0
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            j = min(j, n)
            for k in range(i, j):
                mask[k] = False
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            j = min(j, n)
            for k in range(i, j):
                mask[k] = False
            i = j
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^(]*)\(', text[i:])
            end = text.find(")" + m.group(1) + '"', i)
            j = end + len(m.group(1)) + 2
            j = min(j, n)
            for k in range(i, j):
                mask[k] = False
            i = j
        elif c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum())):
            q = c
            j = i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            j += 1
            j = min(j, n)
            for k in range(i, j):
                mask[k] = False
            i = j
        else:
            i += 1
    return mask


NEW_RE = re.compile(r"\bnew\b")
TAGGED_RE = re.compile(r"new\s*\(\s*jrt::(k\w+)\s*\)")


def parse_type(text, i):
    """Parses the type expression starting at text[i]; returns (type_string, end_index)."""
    j = i
    m = re.match(r"(::)?[A-Za-z_]\w*(\s*::\s*[A-Za-z_]\w*)*", text[j:])
    if not m:
        return None, i
    j += m.end()
    if j < len(text) and text[j] == "<":
        depth = 0
        while j < len(text):
            if text[j] == "<":
                depth += 1
            elif text[j] == ">":
                depth -= 1
                if depth == 0:
                    j += 1
                    break
            j += 1
        # nested name after template args (e.g. Outer<T>::Inner) is not used in the port
    return re.sub(r"\s+", "", text[i:j]), j


def base_name(t):
    t = t.split("<", 1)[0]
    return t.split("::")[-1]


def template_args(t):
    if "<" not in t:
        return []
    inner = t[t.index("<") + 1 : t.rindex(">")]
    args, depth, cur = [], 0, ""
    for ch in inner:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(cur)
            cur = ""
        else:
            cur += ch
    args.append(cur)
    return [a.strip().rstrip("*").strip() for a in args]


def type_lifetime(t):
    """Lifetime implied by the allocated type alone (rules 1 and 2), or None."""
    b = base_name(t)
    if t.startswith("jrt::") or t.startswith("std::"):
        if b == "Array":
            args = template_args(t)
            if args:
                elem = args[0]
                # nested arrays jrt::Array<jrt::Array<X*>*>
                while elem.startswith("jrt::Array<"):
                    sub = template_args(elem)
                    elem = sub[0] if sub else elem
                    break
                if lifetime_of_class(base_name(elem)) == "geom":
                    return "geom"
        return None
    lt = lifetime_of_class(b)
    if lt in ("geom", "board"):
        return lt
    return None


# ---------------------------------------------------------------- context
DEF_RE = re.compile(r"^(?:[A-Za-z_][\w:<>,\*&\s]*?\s+[\*&]*)?((?:\w+::)*)(\w+)::(~?\w+)\s*\(", re.M)
CLASS_RE = re.compile(r"^\s*(?:class|struct)\s+(\w+)\b[^;]*$", re.M)
_header_cache = {}


def header_text_for_class(cls):
    hdr = classmap.get(cls)
    if hdr is None:
        return ""
    if hdr not in _header_cache:
        try:
            with open(os.path.join(ROOT, "src", hdr), encoding="utf-8-sig") as f:
                _header_cache[hdr] = f.read()
        except OSError:
            _header_cache[hdr] = ""
    return _header_cache[hdr]


def is_field(cls, name):
    txt = header_text_for_class(cls)
    return re.search(r"[\w\*>&]\s*\b" + re.escape(name) + r"\s*(=[^=]|;|\{)", txt) is not None


def enclosing(text, pos, is_header, path):
    """(class, function, is_ctor) enclosing pos."""
    if is_header:
        cls = None
        for m in CLASS_RE.finditer(text, 0, pos):
            cls = m.group(1)
        if cls is None:
            cls = os.path.splitext(os.path.basename(path))[0]
        return cls, None, False
    cls = func = None
    for m in DEF_RE.finditer(text, 0, pos):
        cls, func = m.group(2), m.group(3)
    if cls is None:
        return None, None, False
    return cls, func, func == cls


def statement_prefix(text, pos):
    """Text from the start of the statement (after ; { } or line start of init list) to pos."""
    k = pos - 1
    depth = 0
    while k >= 0:
        ch = text[k]
        if ch in ")]":
            depth += 1
        elif ch in "([":
            if depth == 0:
                break  # inside a call / init-list element
            depth -= 1
        elif ch in ";{}" and depth == 0:
            break
        k -= 1
    return text[k + 1 : pos]


def classify(text, pos, typ, path):
    lt = type_lifetime(typ)
    if lt:
        return lt
    is_header = path.endswith(".h")
    prefix = statement_prefix(text, pos)
    if re.match(r"\s*static\b", prefix):
        return "board"
    cls, func, is_ctor = enclosing(text, pos, is_header, path)
    cls_lt = lifetime_of_class(cls) if cls else None
    member = False
    if is_header:
        # default member initializers, inline constructors, accessors in headers
        member = func is None and re.search(r"(this->)?\w+\s*=\s*$|\w+\s*\($|\w+\s*\{$", prefix.strip() + "") is not None
        if re.search(r"\breturn\b", prefix):
            member = False
    else:
        m = re.match(r"\s*this->(\w+)\s*=\s*$", prefix)
        m2 = re.match(r"\s*(\w+)\s*=\s*$", prefix)
        if m:
            member = True
        elif m2 and cls and is_field(cls, m2.group(1)):
            member = True
        elif is_ctor:
            # init list element `field(new ...)`
            before = text[max(0, pos - 200) : pos]
            mi = re.search(r"[:,]\s*(\w+)\s*\(\s*$", before)
            if mi and is_field(cls, mi.group(1)):
                member = True
    if member and cls_lt:
        return cls_lt
    return "op"


TAG_OF = {"board": "kBoard", "op": "kOp", "geom": "kGeom"}


def files():
    for d in SRC_DIRS:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            for fn in sorted(fns):
                if fn.endswith((".h", ".cpp")):
                    yield os.path.join(dp, fn)


def scan(path, text):
    """Yields (pos, kind, tag_or_None, type) for every `new` in code."""
    mask = code_mask(text)
    for m in NEW_RE.finditer(text):
        p = m.start()
        if not mask[p]:
            continue
        before = text[max(0, p - 9) : p]
        if re.search(r"operator\s*$", before):
            continue
        rest = text[m.end() :]
        tm = re.match(r"\s*\(\s*jrt::(k\w+)\s*\)\s*", rest)
        if tm:
            typ, _ = parse_type(text, m.end() + tm.end())
            yield p, "tagged", tm.group(1), typ
            continue
        if re.match(r"\s*\(", rest):
            continue  # other placement new (jrt runtime helpers)
        ws = len(rest) - len(rest.lstrip())
        typ, _ = parse_type(text, m.end() + ws)
        if typ is None:
            continue
        yield p, "plain", None, typ


def package_of(path):
    rel = os.path.relpath(path, ROOT).replace("\\", "/")
    parts = rel.split("/")
    if parts[0] == "bindings":
        return "bindings/lua"
    if len(parts) > 3 or (len(parts) == 3 and parts[1] in ("autoroute", "board", "core", "io")):
        return parts[1] + "/" + parts[2] if parts[2].find(".") < 0 else parts[1]
    return parts[1]


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "report"
    untagged = []
    counts = {}
    for path in files():
        with open(path, encoding="utf-8-sig") as f:
            text = f.read()
        bom = open(path, "rb").read(3) == b"\xef\xbb\xbf"
        sites = list(scan(path, text))
        if mode == "rewrite":
            out = text
            for p, kind, tag, typ in reversed(sites):
                if kind != "plain":
                    continue
                lt = classify(text, p, typ, path)
                out = out[: p + 3] + " (jrt::" + TAG_OF[lt] + ")" + out[p + 3 :]
            if out != text:
                with open(path, "w", encoding="utf-8-sig" if bom else "utf-8", newline="") as f:
                    f.write(out)
        else:
            pkg = package_of(path)
            if "--top" in sys.argv and pkg != "bindings/lua":
                pkg = pkg.split("/")[0].replace("main.cpp", "core")
            for p, kind, tag, typ in sites:
                if kind == "plain":
                    line = text.count("\n", 0, p) + 1
                    untagged.append(f"{os.path.relpath(path, ROOT)}:{line}: new {typ}")
                    continue
                lt = TAGS.get(tag, tag)
                counts.setdefault(pkg, {}).setdefault(lt, 0)
                counts[pkg][lt] += 1
    if mode == "check":
        for u in untagged:
            print(u)
        sys.exit(1 if untagged else 0)
    if mode == "report":
        cols = ["board", "boarddata", "op", "search", "geom"]
        print("| package | " + " | ".join(cols) + " | total |")
        print("|---|" + "---:|" * (len(cols) + 1))
        tot = {c: 0 for c in cols}
        for pkg in sorted(counts):
            row = [counts[pkg].get(c, 0) for c in cols]
            for c, v in zip(cols, row):
                tot[c] += v
            print(f"| {pkg} | " + " | ".join(str(v) for v in row) + f" | {sum(row)} |")
        print("| **total** | " + " | ".join(f"**{tot[c]}**" for c in cols) + f" | **{sum(tot.values())}** |")
        if untagged:
            print(f"\n{len(untagged)} untagged sites")


if __name__ == "__main__":
    main()
