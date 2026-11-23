# Memory model: from Boehm GC to explicit ownership

Status: stages **A and B done** (2026-11-23), C–E open; plan agreed 2026-11-23. Baseline: tag `gc-port-baseline` (backup in WSL
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
| connection search | expansion rooms, doors, drill pages, `MazeListElement`, backtrack elements | engine discarded (after `AutorouteEngine.clear()`; see stage B) |
| optimizer candidate | structural data of the worker board copy (items, undo nodes, tree nodes; see stage B) | candidate finished (Java drops the worker board in `finish()`) |
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

## Stage B — arenas (done)

`jrt::Arena` (`runtime/jrt/alloc.h/.cpp`): bump allocator over 1 MB chunks taken from the GC
heap (scanned, so arena objects keep their GC members alive and GC objects may point in);
`release()` wipes the used bytes (poison `0xA5` in checked builds, zero in release builds) and
returns the chunks to a per-thread pool anchored in uncollectable memory; `abandon()` forgets the
chunks and leaves them to the collector. Arena memory is zero-filled on allocation like
`GC_MALLOC`/Java objects. Destructors of arena objects never run (as for collected objects).

**1. Connection-search arena** (`kSearch`, 53 sites): the long-lived search graph — complete
and obstacle rooms, the engine's incomplete rooms, doors, target doors, door sections
(`MazeSearchElement`), drills and drill pages, backtrack elements, the `MazeSearchEngine` and its
helpers, and the collections owned by those objects. High-churn temporaries stay `kOp` (GC):
`MazeListElement`, candidate room pieces and result lists of `ShapeSearchTree*::completeShape`,
door-list copies, page lists. A first version put those into the arena too: since the arena
lives as long as the engine, bm01's huge searches then kept every temporary until the engine
was dropped and the peak RSS went from 391 MB to 3.3 GB (the collector frees them at once). Each `AutorouteEngine` that does not maintain its database
(the default; `freerouting.benchmark.retain_autoroute_database` keeps the GC path) owns an arena.
It is current (`jrt::ArenaScope`) while the engine builds its drill pages and during
`autorouteConnection` up to `clear()`. Contrary to the original plan, the arena can **not** be
reset at the end of `autorouteConnection`: the drill pages keep cached drills (with their rooms)
for later searches of the same net on the same engine (`RoutingBoard::autoroute` routes a pin to
its closest target and then to the whole set on one engine). So the arena is released when the
engine is discarded — `RoutingBoard::initAutoroute` replacing it, `finishAutoroute`, or its
whole board being dropped (`discardAutorouteEngineWithBoard`). At that point `clear()` has
removed the rooms from the search tree and the autoroute data (`ItemAutorouteInfo`,
`Via::autorouteDrillInfo`) from all items, so the board holds no arena pointer. If an exception
leaves a search before `clear()`, that engine's arena is abandoned and search arenas are switched
off for the rest of the process (stale rooms may stay in a tree and get doors later).

**2. Optimizer-candidate arena** (`kBoardData`, 43 sites: item constructors and copies, undo-list
nodes, search-tree nodes, leaves and leaf arrays). `BatchOptimizer_OptimizeCandidateTask::call`
makes it current (`jrt::BoardArenaScope`) for candidates whose board is not kept (`keepBoard ==
false`; the winner replay keeps its board and stays on the GC heap): the deep copy of the
baseline board, the items the candidate routes on it and their tree entries live in it, and it
is released together with the worker board's search arena when the candidate ends (also on
exceptions). Only structural board data goes there, on purpose: the worker copy **shares**
geometry, the library and the layer structure with the baseline board, and immutable geometry
fills lazy caches, so redirecting `kGeom`/`kOp`/`kBoard` would let shared objects (and
function-local statics first touched by a candidate, the job log, the global
`ShoveCheckCache`) point into a released arena. The `RoutingBoard` object itself also stays on
the GC heap, so caches keyed by board identity (`ShoveCheckCache::owner`) can never see a reused
address.

Escape checks: checked builds (`JRT_CHECKED=1`, the default RelWithDebInfo build) poison
released memory; the corpus parity run passes on both builds (see the results below).

Sites per package after stage B (`classify_allocs.py report --top`):

| package | board | board data | op | search | geom | total |
|---|---:|---:|---:|---:|---:|---:|
| autoroute | 24 | 0 | 596 | 52 | 169 | 841 |
| board | 120 | 37 | 309 | 1 | 211 | 678 |
| geometry | 1 | 0 | 51 | 0 | 598 | 650 |
| io (DSN/SES) | 154 | 0 | 285 | 0 | 34 | 473 |
| core | 70 | 0 | 82 | 0 | 2 | 154 |
| rules | 79 | 0 | 36 | 0 | 3 | 118 |
| drc | 1 | 0 | 85 | 0 | 13 | 99 |
| settings | 59 | 0 | 29 | 0 | 0 | 88 |
| datastructures | 7 | 6 | 67 | 0 | 3 | 83 |
| interactive | 0 | 0 | 25 | 0 | 8 | 33 |
| logger, util, bindings/lua | 12 | 0 | 7 | 0 | 0 | 19 |
| **total** | **527** | **43** | **1572** | **53** | **1041** | **3236** |

Parity after stage B: corpus run (`tools/parity_corpus.sh`, release build, stopped after 71 of
180 fixtures): 60 SAME, 0 DIFF; 9 time out (900 s) in both Java and C++; 2 fail in C++ already
at `gc-port-baseline` (Issue006: DSN header with a non-ANSI character is rejected; Issue179:
segfault after `TriangleGraph.position_locate`). Checked build (poisoned arenas),
`tools/parity_sample.sh` on 40 fixtures: 38 SAME, 0 DIFF, the same 2 old failures.
Successful Java results are kept as a reference in WSL `~/frtest/java-reference/upstream-fixtures`
(63 boards); `tools/parity_sample.sh <outdir> [max_java_s] [count]` compares the C++ build against
it without running Java (`FR_SKIP_JAVA=1` with a copy of it as `<outdir>/java` does the same for
`parity_corpus.sh`).

## Verification

* `tools/parity_corpus.sh` (180 fixtures, C++ vs Java SES byte-for-byte) after every stage;
  keep Java results with `FR_SKIP_JAVA=1` to rerun only the C++ side.
* Benchmarks `tools/bench.sh` on bm01 / bm02 (wall time, peak RSS).
* Sanitizer builds: `-fsanitize=address,undefined`, then LeakSanitizer once GC is gone.
* Work one subsystem at a time, no parallel agents (plan-usage limits).
