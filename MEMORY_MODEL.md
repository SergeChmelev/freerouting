# Memory model: from Boehm GC to explicit ownership

Status: stages **A and B done**, **C and D in progress** (C1, D1–D3, 2026-11-23), E open; plan agreed 2026-11-23. Baseline: tag `gc-port-baseline` (backup in WSL
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

**2. Optimizer-candidate arena** (`kBoardData`, 41 sites: item constructors and copies, undo-list
nodes, search-tree nodes, leaves and leaf arrays; temporary items such as the probe trace of
`BasicBoard::checkPolylineTrace` and the substitute pieces of `ShapeTraceEntries` stay `kOp` —
in the arena they and their cached geometry lived until the candidate ended, and bm01 grew to
1.8 GB in a few minutes). `BatchOptimizer_OptimizeCandidateTask::call`
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

Arena chunks are allocated with `GC_MALLOC_IGNORE_OFF_PAGE` (the arena keeps a pointer to each
chunk start). Diagnostics: `FR_NO_SEARCH_ARENA=1` / `FR_NO_BOARD_ARENA=1` switch an arena off,
`FR_MEM_STATS=1` prints arena / GC heap figures (per 20 optimizer candidates and at exit).
Lesson for later arenas: an arena must only hold objects that live about as long as the arena
itself — anything with high churn inside the operation belongs on the general heap.

Escape checks: checked builds (`JRT_CHECKED=1`, the default RelWithDebInfo build) poison
released memory; the corpus parity run passes on both builds (see the results below).

Sites per package after stage B (`classify_allocs.py report --top`):

| package | board | board data | op | search | geom | total |
|---|---:|---:|---:|---:|---:|---:|
| autoroute | 24 | 0 | 596 | 52 | 169 | 841 |
| board | 120 | 35 | 311 | 1 | 211 | 678 |
| geometry | 1 | 0 | 51 | 0 | 598 | 650 |
| io (DSN/SES) | 154 | 0 | 285 | 0 | 34 | 473 |
| core | 70 | 0 | 82 | 0 | 2 | 154 |
| rules | 79 | 0 | 36 | 0 | 3 | 118 |
| drc | 1 | 0 | 85 | 0 | 13 | 99 |
| settings | 59 | 0 | 29 | 0 | 0 | 88 |
| datastructures | 7 | 6 | 67 | 0 | 3 | 83 |
| interactive | 0 | 0 | 25 | 0 | 8 | 33 |
| logger, util, bindings/lua | 12 | 0 | 7 | 0 | 0 | 19 |
| **total** | **527** | **41** | **1574** | **53** | **1041** | **3236** |

Parity after stage B: corpus run (`tools/parity_corpus.sh`, release build, stopped after 71 of
180 fixtures): 60 SAME, 0 DIFF; 9 time out (900 s) in both Java and C++; 2 fail in C++ already
at `gc-port-baseline` (Issue006: DSN header with a non-ANSI character is rejected; Issue179:
segfault after `TriangleGraph.position_locate`). Checked build (poisoned arenas),
`tools/parity_sample.sh` on 40 fixtures: 38 SAME, 0 DIFF, the same 2 old failures.
Successful Java results are kept as a reference in WSL `~/frtest/java-reference/upstream-fixtures`
(63 boards); `tools/parity_sample.sh <outdir> [max_java_s] [count]` compares the C++ build against
it without running Java (`FR_SKIP_JAVA=1` with a copy of it as `<outdir>/java` does the same for
`parity_corpus.sh`).

### Results (release build, WSL, single-threaded C++)

| | bm02 wall | bm02 peak RSS | bm01 wall | bm01 peak RSS |
|---|---:|---:|---:|---:|
| `gc-port-baseline` | 4.97 s | 292 MB | 2219 s | 389 MB |
| stage B (arenas only, commit 384492a) | 4.83 s | 291 MB | — | — |
| stage B + perf fixes (commit b9bd456) | **4.03 s** | 289 MB | **1781 s** | 394 MB |
| Java, default threads | 4.24 s | 1320 MB | 185 s | 4639 MB |
| Java, 1 thread | 6.97 s | 767 MB | 727 s | 1547 MB |

bm02: mean of 3 runs on an idle machine; bm01: baseline and final run concurrently (so equally
loaded); all SES byte-identical to Java. The arenas alone gain little while the collector still
marks the whole heap (bm02 −3%); most of the gain comes from the hot-path fixes found while
profiling (O(1) item-list iteration, allocation-free integer geometry fast paths, natural-order
comparators without `std::function`, see commit 113b4cd/f299e09). Profiles after these fixes:
GC (allocation + marking) is 40–50% of the time (bm01: `GC_mark_from` alone 17%), so stages C–E
remain the main lever; bm01 C++ is still ~2.4x slower than Java with one thread.

## Stage D — geometry and temporaries (in progress)

Measured first, then cut: the allocation-site profiler (`-DJRT_ALLOC_PROFILE=1`,
`tools/mem/alloc_profile.sh <binary> <out> <dsn>`, `FR_ALLOC_PROFILE_FP=N` attributes each site
to the caller N levels up; needs `-fno-omit-frame-pointer`) showed 114M `operator new` calls
(4.6 GB) on bm02, 60% of them immutable geometry that dies right after it is created. Steps so
far (all keep the routing output identical):

* **D1 (439c5c8)** — iterators of a range-for over an abstract `jrt::Collection` are built in a
  stack buffer (`Collection::iteratorIn`, `ScopedIter`: same Java iterator class, no heap);
  allocation-free integer paths in `Line` (`direction`, `translate`, `getInstance`,
  `sideOfIntersection`), `Direction::sideOf/projection`, `FloatPoint::projectionApprox`,
  `IntOctagon::cornerApprox`; `IntOctagon::normalized()` (one octagon instead of two);
  `Line::ofInts` (a line and its two points in one block — the collector honours interior
  pointers); `TempBorderLine` (border lines of octagons/boxes as stack values);
  `MinAreaTree` keeps an inner bounding octagon that already contains the inserted leaf;
  `ShapeSearchTree::overlappingObjects` without the temporary tree-entry list.
* **D2 (5b2ae4e)** — `jrt::BigInteger` stores values that fit in int64 inline (no `Rep`, no GMP
  limbs); every result is normalised; GMP only beyond int64 and for pow / sqrt / modInverse.
  `tests/bigint_fuzz.cpp` (ctest) checks every operation against plain GMP around the int64
  boundaries.
* **D3 (627f930)** — `Line::directionInts`, `FloatPoint::projectionApproxInts`,
  `restrainShapeInto` (no list per recursive call), union of room shapes accumulated as
  coordinates, local difference vectors in `Line::intersection`, `IntBox::ofInts`.

Escape rule for these paths: a stack value (`IntPoint`, `IntOctagon`, `Line` in
`TempBorderLine`, `IntVector`) is only passed to functions that read it; anything stored
(results, cached shapes, list entries) is still heap-allocated.

| bm02 | `operator new` calls | bytes |
|---|---:|---:|
| after stage B | 114.2M | 4.65 GB |
| D1 | 68.6M | 2.98 GB |
| D2 | 64.9M (+ GMP limbs no longer allocated) | 2.93 GB |
| D3 | 56.8M | 2.77 GB |

Parity after each step: bm02 SES identical; `tools/parity_sample.sh` 53 SAME (release and
checked build), only the two pre-existing failures (Issue006, Issue179).

Timing (release, WSL; old/new run interleaved or concurrently, so equally loaded):

| | bm02 wall | bm01 wall | bm01 peak RSS |
|---|---:|---:|---:|
| stage B + perf fixes (b9bd456) | 3.85 s | 1597 s | 393 MB |
| D1 + D2 (5b2ae4e) | 3.18 s (−17%) | 1397 s (−12.6%) | 504 MB |
| D3 (627f930) | ≈ D2 (within noise) | — | — |

All SES byte-identical. bm01 peak RSS grew although live data did not: with fewer, larger
allocations Boehm runs 35–57% fewer collections (at optimizer candidate 321: 3861 → 2525 (D2)
→ 1664 (D3)) and sizes the heap larger (400–430 MB vs 332 MB; heap minus free space is about the
same). That is collector policy, not retention; it disappears with stage E, or can be traded
back with `GC_free_space_divisor` / a heap limit if it matters before that.

Remaining heavy streams (bm02, D3): border lines that escape into simplices
(`IntOctagon::toSimplex`), `Simplex::getInstance/removeRedundantLines` arrays, octagon results
of `intersection` / `union_` / `offset`, result sets of `overlappingObjects` (`TreeSet` nodes,
`getNormalContacts`), op-lifetime collections. bm02 has only ~18 collections (the heap starts at
256 MB), so its GC cost is allocation (`GC_malloc_kind`, free-list building); bm01 runs ~9100
collections on a ~340 MB heap, so there marking dominates — fewer allocated bytes or a larger
`GC_free_space_divisor` (tuning, not yet measured on bm01) reduce the number of collections.

## Stage C — explicit ownership (in progress)

Board data of optimizer candidates already lives in arenas (stage B), so ownership on the main
board matters for stage E, not for speed. Stage C therefore introduces owners one structure at a
time, each verified on its own, while the collector stays as the safety net for everything not
yet owned.

**Tool: `jrt::dispose(p)`** (`runtime/jrt/alloc.h`): the owner ends the object's life — the
destructor runs and the block goes back to the heap (`GC_FREE`) at once. Pointers that are not a
separate heap block (arena objects, the points inside a `Line::ofInts` block, statics, stack
values) are left alone, so a call site need not know where the object came from. Checked builds
poison the block before freeing it. **Verification mode** `FR_DISPOSE_CHECK=1`: disposed blocks
are poisoned and *not* freed (a read through a dangling pointer sees `0xA5` bytes, a virtual call
crashes), and a disappearing link per block tells whether it is still reachable after a full
collection (every 2^20 disposals and at exit). The exit report lists those survivors by type:
each one is a dangling reference (or a stale pointer the conservative collector sees, e.g. in
the unused capacity of a cleared `std::vector`). `tests/dispose_check.cpp` (ctest) checks that a
dangling reference is reported and garbage is not. `FR_MEM_STATS=1` prints the disposed
objects/bytes.

* **C1 — shape-tree nodes.** `MinAreaTree` owns its inner nodes, their bounding shapes (always a
  fresh union, never shared: board copies rebuild their trees by insertion) and its leaves (the
  leaf's bounding shape belongs to the item's tree shape and is not freed). Replaced bounding
  shapes, temporary unions of the area comparison, the removed parent node and the removed
  leaf are disposed. `CompleteFreeSpaceExpansionRoom::removeFromTree` drops its leaf array; the
  shared query leaf buffer nulls its slots before `clear()` (the only survivors the check
  found). bm02: 1.13M objects / 86 MB disposed, 0 survivors, SES identical, time unchanged
  (3.3 s, within noise). `parity_sample.sh <out> 120 60`: 58 SAME on the release build with
  `FR_DISPOSE_CHECK=1` and on the checked build, only the two old failures; 286M disposals, 9
  survivors in 7 boards (1–3 each, no crash or diff). The referrer search (below) showed that
  every one was referred to only by the main thread's stack, never by heap or static data, and
  rerunning the 7 boards gave survivors in 3 of them only: stale slots in live stack frames
  (conservative noise), no dangling reference.

* **C2 — item search-tree info.** `ItemSearchTreesInfo` owns its tree list, the per-tree
  records and the leaf arrays (not the leaves: the trees own them; not the precalculated tree
  shapes: some `calculateTreeShapes` results may be shared). `Item::clearSearchTreeEntries`
  disposes it — every caller has removed the leaves from the trees or handed them to another
  trace first (merge / cut-out of traces), and a replaced leaf array is disposed in
  `setTreeEntries`. bm02: +29k disposals, 0 survivors. Sample (120 s, 60 boards): 58 SAME on
  the checked build; release + `FR_DISPOSE_CHECK=1` 57 SAME + Issue178 (over the 900 s limit in
  check mode, SAME when rerun alone), 289M disposals, 0 survivors.

* **C3 — undo-list nodes (deferred).** A node cannot be freed when it leaves the map:
  `UndoableObjects_NodeIterator` prefetches the *next* node like Java's weakly consistent
  skip-list iterator and returns it even if it was deleted meanwhile, and removed nodes stay
  reachable through the undo/redo links of nodes kept for undo. So the list registers every
  heap node (board-arena nodes are left to the arena) and, when no iteration is live and the
  registry has doubled since the last sweep (minimum 4096, env `FR_UNDO_SWEEP_MIN` for stress
  runs), `sweepNodes()` marks what the map and the delete lists reach through undo/redo links
  and disposes the rest. Iterations are counted exactly: `startReadObject()` became
  `readScope()`, which returns the iterator as a scoped value (`auto itScope =
  list->readScope(); auto* it = &itScope;`, `itScope.restart()` where Java reassigned a new
  iterator); 47 call sites converted. bm02 frees only ~450 nodes (most undo nodes belong to
  optimizer worker boards and live in their arenas).

Not yet owned, with the reason found:

* Items: callers keep using an item after `removeItem` (observers, building replacement
  traces), and many caches refer to items; needs the same kind of deferred scheme.
* Dropped search trees (`clearanceValueChanged`, `clearanceClassRemoved`): rare; items keep
  stale `ItemSearchTreesInfo` records for them.

**Referrer search** (verification mode): for each survivor (up to 200 per run) the check walks
every reachable heap block (`GC_enumerate_reachable_objects_inner` right after the collection),
the writable static segments and the main thread's stack for words pointing into it. Survivors
referred to only by a stack are counted apart as noise; the others are printed with up to three
levels of referrers (`heap block … (size, word0=<module>+<offset>) at +<field offset>`);
`tools/mem/dispose_referrers.sh <log>` turns `word0` (the vtable pointer) into the class name.

## Verification

* `tools/parity_corpus.sh` (180 fixtures, C++ vs Java SES byte-for-byte) after every stage;
  keep Java results with `FR_SKIP_JAVA=1` to rerun only the C++ side.
* Benchmarks `tools/bench.sh` on bm01 / bm02 (wall time, peak RSS).
* Sanitizer builds: `-fsanitize=address,undefined`, then LeakSanitizer once GC is gone.
* Work one subsystem at a time, no parallel agents (plan-usage limits).

## Stage E — build without the collector (started)

`cmake -DFR_NO_GC=ON` (defines `JRT_NO_GC=1`): `runtime/jrt/nogc.cpp` replaces the Boehm backend
— `operator new` is `calloc` (zero-filled like `GC_MALLOC`), `delete` / `jrt::dispose` is
`free`, arena chunks are plain heap blocks whose address ranges `dispose` checks, thread
registration is a no-op. Nothing is collected: every object without an owner leaks, and
`FR_MEM_STATS=1` reports the live (= leaked) bytes at exit as "GC heap". The verification mode
`FR_DISPOSE_CHECK` needs the collector and is off in this build.

First run (2026-11-23, `~/build/fr-nogc`, `parity_sample.sh 120 60`, 3.5 GB address-space limit
per run): **32 SAME, 0 DIFF**; 26 boards abort with `bad_alloc` after 6–10 s (they leak more
than 3.5 GB), plus the two old failures. bm02: SES identical, 2.85 GB leaked (peak RSS 3.4 GB).
So explicit freeing is functionally sound; what is left is ownership of everything else,
ordered by leaked volume (geometry first: `Line::ofInts`, octagon results, simplices; then
the result collections of search-tree queries).

### E1 — geometry regions ("region + copy")

Chosen 2026-11-23 over intrusive ref-counting (which would change ~1100 geometry-returning
functions, ~130 fields and ~900 array uses in 333 files). Immutable geometry (`kGeom`) of an
operation lives in a `jrt::GeomRegion` (an arena) and is dropped with it; whatever must outlive
the operation is copied out into the holder's domain.

* `GeomRegion` + `GeomRegionScope` (activate, nesting) / `LocalGeomRegion` (active for its own
  lifetime, released at the end). `kGeom` allocates in the current region, else on the heap.
  `jrt::Array` storage follows the array object (region or heap; the destructor frees heap
  storage, so disposed arrays no longer leak their elements).
* Domain rule: an object fills lazy caches and stores computed geometry in its own domain —
  `jrt::DomainScope(owner)` selects the active region that owns `owner` (a region also owns the
  objects of its *companion* arena, e.g. the search arena), else the heap. Applied to every
  `precalculated*` / `dir` cache of geometry and items (30 sites, by pattern), the exact-copper
  cache of drill items, and `MinAreaTree` insert/remove (inner-node bounding unions belong to
  the tree, not to the search that inserts a room). Function-local geometry singletons
  (`EMPTY()`, directions, `Side` constants: 21) are created with `jrt::onHeap`.
* Copy-out: `geometry/planar/GeomCopy.h` (`geomcopy::tileShape/octagon/line/point/intPoints`;
  copies only objects inside the given region, aborts on an unhandled shape type).
* Regions so far: `ShapeSearchTree45Degree::completeShape` (local; result room shapes copied
  out) and the **search region** of each `AutorouteEngine` (companion of its search arena,
  active exactly when the arena is; released with it, arena first). The found connection's
  corners are copied out before the inserter puts them into board items.
* Diagnostics: `FR_REGION_CHECK=N` (GC build) scans the live heap and statics for pointers into
  every N-th local region and every search region at release and prints the holders (vtable as
  `exe+offset`, resolved with `nm -C`; for buffers the chain of holders). Known noise: removed
  tree nodes and leaves left in a live board arena (stale memory, never read), dead objects kept
  by stale stack slots (marked `[dead holder]`). `FR_NO_GEOM_REGION=1` disables all regions,
  `=2` only the non-local ones. Checked builds poison released region memory, so a missed
  escape shows up as a crash or SES difference.

bm02 (no-GC build): leaked 2.85 GB → 2.08 GB, SES identical in the release, checked and no-GC
builds.

### E2 — candidate and insertion regions, copy-at-store by owner

* Region ids: every live region (and its companion arena) is registered by chunk range with its
  id (complemented addresses in an uncollectable map, so neither dangling region objects nor
  collector retention). `geomcopy::keep(x, owner)` copies x (point, vector, polyline, tile shape,
  array of tile shapes) only when a part lies in a live region that is not the owner's own
  domain, into the owner's domain (its region while active, else the heap).
* Optimizer candidates: `CandidateArena` owns a `GeomRegion` whose companion is the candidate
  arena; worker-board items keep region geometry without copies; only the item position leaves
  the candidate (computed on the heap). Released after the arena.
* `AutorouteEngine::autorouteConnection`: a `LocalGeomRegion` around ripping and inserting the
  found connection (pull-tight, shove, normalization on the main board).
* Stores fixed by the escape check / crashes in checked builds: precalculated tree shapes
  (`Item::setPrecalculatedTreeShapes` keeps), drill-item centre in `translateBy` /
  `turn90Degree` / `changePlacementSide`, leaf bounding shapes (`ShapeTree::insert` in the
  object's domain).

bm02 (no-GC build): leaked 2.08 → 1.25 GB; SES identical in release, checked and no-GC builds.
Sample (120 s, 60 boards): checked 56 SAME + 2 over the 900 s limit (Issue110-RelayModule,
Issue178; the domain lookups slow these boards down — performance is measured after the
migration), no-GC 37 SAME (was 32 before E1), 0 DIFF, only the two old failures besides.

### AddressSanitizer / LeakSanitizer (no-GC build)

`cmake -DFR_NO_GC=ON -DFR_SANITIZE=ON` (RelWithDebInfo, `~/build/fr-asan`); `FR_SANITIZE` without
`FR_NO_GC` is rejected (Boehm and ASan both replace the allocator). Arena memory that is not
handed out is poisoned (`JRT_ASAN_POISON/UNPOISON` in `jrt::Arena`: fresh and pooled chunks,
`allocate` unpoisons, `release` poisons), so reading a released region or arena object is
reported like a heap use-after-free. `tools/mem/asan_run.sh <bin> <out> <dsn>` runs a board and
`tools/mem/lsan_summary.py` groups the leak report by the first non-allocator frame (and its
caller); direct leaks = unreachable objects, indirect = reachable only from leaked ones.

First runs (2026-11-23, after E2): bm02, Issue143, Issue313 — **no memory errors**, SES identical.
LeakSanitizer only counts what is unreachable at exit: bm02 25 MB (direct 5.7 MB), while the
no-GC build's live counter is 1.25 GB — most of what is not yet freed is still reachable at exit
(board, undo history, caches, kept board copies), which LSan does not report; a live-bytes
profiler by allocation site is the tool for that part. Direct leaks it found: element storage of
`jrt::Array`s that live in the search / board arenas (arena objects are never destroyed) — now
taken from the same arena — and `std::vector` buffers of `jrt` collections inside arena objects
(door lists of rooms: `FreeSpaceExpansionRoom::addDoor`), still open.
