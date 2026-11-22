# Memory model: from Boehm GC to explicit ownership

Status: **plan** (agreed 2026-11-23). Baseline: tag `gc-port-baseline` (backup in WSL
`~/_backup_before_kill_gc`). The GC port is the reference implementation: every step below must
keep the routing output byte-identical to Java (and to `gc-port-baseline`).

## Goals

* No garbage collector: ordinary `operator new/delete`, RAII, no leaks (LeakSanitizer clean).
* Faster than the GC build (profiles: GC ~20% on bm02, ~40% on bm01) and ready for a
  multi-threaded optimizer (worker boards on threads).
* Behavioural parity with Java preserved at every step.

## Ownership map

### 1. Board lifetime (single owner + non-owning raw pointers)

| Objects | Owner | Referenced by (non-owning) |
|---|---|---|
| Items and their undo-history versions | `UndoableObjects` nodes (`unique_ptr`) | search-tree leaves, contact caches, autoroute info, interactive session |
| Search-tree nodes / leaves | the tree (`unique_ptr` children) | parent links, item ↔ leaf back links |
| Rules: nets, net classes, via infos/rules, clearance matrix | `BoardRules` | back links (`Net::nets`, …) |
| Library (padstacks, packages), layer structure | shared by a board and its copies: `shared_ptr<BoardLibrary>` | everything |
| Components | `Components` | items refer by component id |
| Board | `unique_ptr` held by the job / engine / BoardHistory / optimizer worker | items (`Item::board`), trees, rules |

Invariant that makes raw pointers safe: an item is removed from every search tree before its
owner releases it, and it stays alive while it is in the undo history. Debug builds verify
non-owning links (poisoned memory + ASan).

### 2. `weak_ptr` / ids — only where a reference can outlive its target

* `RoutingFailureLog`: store item **ids**, not pointers (history trimming can free items).
* `ShoveCheckCache` owner board, `BoardHistory` snapshots, UI/Lua handles: `weak_ptr` or ids.
* Not in hot paths (`lock()` per access); hot paths use raw pointers + debug checks.

### 3. Per-operation arenas ("throw the whole collection away when the action ends")

| Arena | Contents | Reset when |
|---|---|---|
| connection search | expansion rooms, doors, drill pages, `MazeListElement`, backtrack elements | end of `AutorouteEngine::autorouteConnection` (Java: `AutorouteEngine.clear()`) |
| optimizer candidate | the worker board copy and all its temporaries | candidate finished (Java drops the worker board in `finish()`) |
| shove / pull-tight call | temporary polylines, shapes, obstacle lists (`TraceShover`, `TraceTightener`, `DrillItemMover`) | end of the call |
| DRC / statistics | everything | end of the computation |

Cycles inside an arena (rooms ↔ doors, backtrack chains) need no smart pointers at all.

**Escape rule:** anything stored into the board (items, item geometry, rules) is allocated with
the board's allocator or copied out of the arena at insertion. Enforcement: debug arena reset
fills memory with a poison pattern; ASan builds; corpus parity runs.

### 4. Geometry

* **Small immutable types become values:** `IntPoint`, `IntVector`, `IntBox`, `IntOctagon`,
  `FloatPoint`, `IntDirection` (8–40 bytes). Polymorphic `Point`/`Vector`/`Direction` keep a
  small tagged representation (int vs rational) instead of a heap object.
* **Large immutable types** (`Polyline`, `Simplex`, `PolylineShape`, `PolylineArea`, `Line`
  arrays): intrusive reference counting (immutable ⇒ no cycles), or arena allocation when they
  never escape the operation.
* `BigInteger`: inline `int64` fast path (`__builtin_*_overflow`), GMP only on overflow.

## Migration stages (each ends with: build, corpus parity vs Java, bm01/bm02 timing)

* **A — classify (still on GC).** Mark every `new` site by lifetime (board / operation /
  geometry) behind allocation helpers; no behaviour change.
* **B — arenas.** Connection-search arena and optimizer-candidate arena (largest garbage streams).
* **C — board ownership.** `unique_ptr` owners per the map above; non-owning raw pointers
  elsewhere; ids/`weak_ptr` where references can outlive targets.
* **D — geometry.** Small types as values, large types ref-counted; `BigInteger` fast path.
* **E — remove GC.** Plain `operator new`, drop `jrt::new_root`/`GcThreadScope`; LeakSanitizer +
  ASan clean; Lua module no longer needs its own static libstdc++ trick for the allocator.

## Stage A — allocation sites classified (done)

Every `new` in `src/` and `bindings/lua/` names its lifetime with a placement tag from
`runtime/jrt/alloc.h`: `new (jrt::kBoard) T(...)`, `new (jrt::kOp) T(...)`,
`new (jrt::kGeom) T(...)` (stage B adds `jrt::kSearch` and `jrt::kBoardData`). On the GC build
the tags forward to the global `operator new`, so behaviour is unchanged.

* `kBoard` — owned by the board or another long-lived owner (rules, library, settings, job,
  logger, function-local statics).
* `kOp` — per-operation temporary (locals, results, iterators, per-call engines).
* `kGeom` — immutable geometry and arrays of geometry (stage D: values / ref-counting).

The tags were assigned by `tools/mem/classify_allocs.py rewrite` (heuristics: allocated type,
`static` locals, stores into members of board / geometry classes; see the script header) and
spot-corrected by hand (undo-list nodes, search-tree leaf arrays). They are a first
approximation for stage C, which turns `kBoard` into real owners and will refine misfits.
`classify_allocs.py check` lists untagged sites (must be empty); `report [--top]` prints the
table. Sites per package (end of stage A):

| package | board | op | geom | total |
|---|---:|---:|---:|---:|
| autoroute | 24 | 648 | 169 | 841 |
| board | 158 | 309 | 211 | 678 |
| geometry | 1 | 51 | 597 | 649 |
| io (DSN/SES) | 154 | 285 | 34 | 473 |
| core | 70 | 82 | 2 | 154 |
| rules | 79 | 36 | 3 | 118 |
| drc | 1 | 85 | 13 | 99 |
| settings | 59 | 29 | 0 | 88 |
| datastructures | 12 | 68 | 3 | 83 |
| interactive | 0 | 25 | 8 | 33 |
| logger, util, bindings/lua | 12 | 7 | 0 | 19 |
| **total** | **570** | **1625** | **1040** | **3235** |

## Verification

* `tools/parity_corpus.sh` (180 fixtures, C++ vs Java SES byte-for-byte) after every stage;
  keep Java results with `FR_SKIP_JAVA=1` to rerun only the C++ side.
* Benchmarks `tools/bench.sh` on bm01 / bm02 (wall time, peak RSS).
* Sanitizer builds: `-fsanitize=address,undefined`, then LeakSanitizer once GC is gone.
* Work one subsystem at a time, no parallel agents (plan-usage limits).
