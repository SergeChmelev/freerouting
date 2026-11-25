# Memory model: from Boehm GC to explicit ownership

Status (2026-11-24): **the collector is removed** (E10); explicit owners, operation regions and the job region (E9) hold all memory; remaining work in the TODO list. Plan agreed 2026-11-23. Baseline: tag `gc-port-baseline` (backup in WSL
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

### E4 — collection storage in the collection's domain

`jrt::DomainAlloc<T>` (alloc.h): the std containers inside `ArrayList`, `LinkedList`,
`ArrayDeque`, `PriorityQueue`, `HashMap` (table; nodes via `new (jrt::Like{this})`), `HashSet`
(its map), `TreeSet` / `TreeMap` (nodes, entries, comparator) allocate from the arena that holds
the collection object (region, search or board arena — `domain_arena_of(this)` in the
constructor), else from the heap. Arena-backed storage that a container gives back goes to
per-size free lists (`Arena::allocateReusable` / `recycle`, 16–1024 bytes), so churning sets in
long-lived regions reuse memory. bm02 no-GC leak 0.80 → 0.57 GB; smoke clean.

### E5 — runtime results are operation temporaries; statics on the heap

The jrt runtime's own results (`Array::of`, `clone`, `Arrays::copyOf`, `newArray2`, iterators,
`toArray`, `subList`, `Collections` helpers, …) are allocated with `kOp` instead of a plain
`new`, so they follow operation regions; owned sub-objects (`LinkedHashSet`'s map) use
`jrt::Like{this}`. Every function-local static pointer in `src/` whose initialiser allocates is
created with `jrt::onHeap` (a static first touched inside a region would otherwise point into
it: `BigIntAux::trailingZeroTable` was the first crash). `ShoveCheckCache_Key::heapCopy` runs in
a `HeapScope`. bm02 no-GC leak 0.57 → 0.50 GB; smoke clean.

### Live-memory profile (no-GC build)

`-DFR_NO_GC=ON` with `-DJRT_ALLOC_PROFILE=1 -fno-omit-frame-pointer` (`~/build/fr-nogc-prof`):
every block has a 32-byte header with its allocation site; frees subtract; the dump at exit
lists what is still allocated. `tools/mem/live_profile.sh <bin> <out> <dsn>` resolves it by
site and caller, largest first (`FR_ALLOC_PROFILE_FP=N`: caller N levels up). bm02 after E5:
476 MB live at exit — ~130 MB are pooled free arena chunks (a cache), 32 MB
`PolygonWireScan::netsByLayer` at the start of routing, shove / forced-trace geometry and
collections on the main board (`TraceShover::springOver*`, `insertForcedTracePolyline`,
`StepSlider`), board copies outside the candidate arena (`copyMapped`, `copyAsDeserialized`),
item caches and query results.

bm02 at bee104d: **113 MB** still allocated at exit (2.54 M blocks, 177 board copies made / 176
freed, 42k tree shapes freed by the graveyard). No site above 5 MB any more: pin shapes of
board copies (`PlacementTransform::shape/padPoint/direction`, item caches on copies), trace
offset shapes (`Line::translate`, `IntOctagon::borderLine`, `offsetShapes`), and kOp/kGeom of
operations that run outside any region (`BoardStatistics` fanout maps 3 MB,
`NetRoutingLedger::buildItemLists` 3 MB, DRC, `ShoveCheckCache::put`). The final board and the
DSN board are still alive at exit too (the result keeps the board). A LeakSanitizer-clean exit
needs an owner for item geometry first (copies share it with their source; see C4 and E8).

### E6 — main-board operations in local regions; DSN scanners owned

* Geometry-only local regions around `RoutingBoard::optChangedArea` (pull-tight), `fanout` and
  the optimizer's winner replay (kept board). New escapes found by the smoke test: the board's
  change journal and update boxes (`noteChange`, `journalChange`, `joinGraphicsUpdateBox`: board
  domain + `keepBox`), `ConductionArea` caches (smoothed outline, detailed fill area, fracture
  memo/result: item domain).
* `SpecctraDsnStreamReader` owns its input buffer; the three DSN reads dispose their scanner.
* bm02 no-GC leak 0.50 → 0.39 GB; smoke clean.

### E7 — step slider in a local region

`StepSlider::run` (an optimization stage that shoves traces on the main board through
`insertForcedTracePolyline` / `TraceShover`) runs each trace's move trials in a geometry-only
local region. bm02 no-GC leak 317 → 246 MB; smoke clean. Letting this region also take `kOp`
broke 11 of 40 smoke boards (main-board structures keep per-operation objects), so main-board
regions stay geometry-only until those stores are known. **Done since:** ASan (arena poisoning) named
the stores: `ItemSearchTreesInfo`'s tree list and records were kOp (now in the info's domain,
`jrt::Like{this}`), and `NetRoutingLedger` stored per-item net arrays / lists built in the
operation (its storing methods now run in its own domain; the counting ones stay in the
operation's domain). The step-slider region takes kOp now: Issue187 (no-GC) 5.3 → 2.66 GB still
allocated at exit, peak RSS 6.5 → 3.4 GB; bm02 137.6 → 132.4 MB; smoke clean.

### Status 2026-11-24 (main 82d9aaa)

No-GC build, full sample (`parity_sample.sh 120 60`, 3.5 GB limit): **50 SAME, 0 DIFF**, 8 boards
still run out of memory (Issue022, Issue110-Pajalnaja_stancija / -Паяльная станция / -RelayModule,
Issue178, Issue187, Issue214, Issue269), plus the two old failures (was 37 SAME / 21 out of
memory after E2). bm02: 184 MB still allocated at exit (was 2.85 GB with E1's start).

Live profile of an out-of-memory board (Issue187, no limit: 5.2 GB still allocated at exit):
> 1.4 GB are tree-shape caches of removed / replaced items (`getPrecalculatedTreeShapes`,
`offsetShapes` from `treeShapeCount`, `Line::translate`): nothing frees an item and its
caches yet — deferred item disposal (TODO) is the main lever for the large boards. About 1 GB
more is kOp temporaries of main-board shoving (`TraceShover::springOver`, `entrancePoints`,
`cutout` via `StepSlider`), which main-board regions cannot take yet (TODO).

### E8 — board copies are freed by their owner (merged branch) (`BasicBoard::discard`)

`BasicBoard::discard(board)` ends the life of a board copy whose only owner drops it. It frees
what a copy owns and only that: the search trees (`ShapeTree::disposeNodes`: inner nodes with
their bounding unions, leaves; then the tree objects and the `SearchTreeManager`), every item of
the item list — current, deleted on an undo level, or only reachable through undo/redo links —
with its `ItemSearchTreesInfo` and its net-number array (`UndoableObjects::disposeAll`, which also
frees the nodes, the map with its entries and the delete lists), the components (all versions),
the id map of `BoardItemRepository`, the normal-contact cache and, for a `RoutingBoard`, the
autoroute engine (search arena and region released first) and finally the board object.
Shared with the source and never freed here: library, padstacks, layer structure, bounding box,
clearance baseline, item geometry (polylines, shapes, centres; copies share them), fracture
memo geometry. Not owned yet (still leak with a freed copy): `BoardRules` (deep copy, ~1 MB per
170 candidates on bm02), `Communication` / id generator, `RoutingFailureLog`, item geometry
caches computed on the copy (precalculated tree shapes, `ComponentOutline` absolute area: deep
geometry, possibly shared — needs a geometry owner first).

Caches keyed by board identity must not see a reused address once boards are freed:
`ShoveCheckCache::forget` (Java: the weak owner reference is cleared) and
`BoardStatisticsBoundsCalculator::forget` (Java: WeakHashMap key) are called by `discard`.

Owners and drop sites (the job's input board — the DSN board or a preset board of the Lua
engine — is the caller's and is never freed):

* Optimizer (`BatchOptimizer`): the incumbent snapshot `bestBoard` (replaced per accepted pass,
  dropped at the end); a board `this->board` / `job->board` stops referring to (restore of the
  incumbent, the winner replay replacing the pass baseline: `dropBoard`, which skips the input
  board, the current, the incumbent and the job's board); a replayed winner that does not
  match; a heap worker board the candidate task does not return (`DropUnreturnedBoard`); every
  arena worker board (`~CandidateArena`: before the arena is released, so its heap parts — board
  object, `UndoableObjects` with map, trees and tree objects, search-tree info, net-number
  arrays — go too).
* Auto-router: `BoardHistory` owns its snapshots (evicted, removed and cleared entries are
  freed); `AutorouteBatchLoop` frees a restored board when the next restore / the final best
  board replaces it (never the board the loop started on or `job->board`).
* Routing fallback: the input snapshot of `RoutingPipeline::run` after `RoutingFallbackRunner::run`
  (it only deserializes copies of it), the exploration snapshot right after its copy is made.
* Not yet: the strict-DRC snapshots of `AutorouteConnectionRouter` (only with `strict_drc`), the
  attempt boards of the fallback runner that lose (their router keeps them), the candidate
  `RoutingJob` with its cloned settings.

`Item::Item` allocates the item's own net-number array in the item's domain (`jrt::Like{this}`:
the candidate arena for worker items; was always the heap) and `ComponentOutline` no longer
makes a throw-away empty array per copy; `BoardItemRepository` frees its stale id map when the
item list changes. The temporaries of a copy are freed when it is made (the item memo of
`copyItemListFrom`, the node map of `UndoableObjects::copyMapped`, the component map, the item
map and plane lists of `RoutingBoardUndoFacade::deepCopy`; the id maps of `BoardRules::deepCopy`
and `ClearanceMatrix::deepCopy` are locals now): the dispose check found freed items "reachable"
through such dead maps (stale stack words). A freed copy's rules, which are not freed yet, lose
their back link (`Nets::board`). Before `AutorouteBatchLoop` frees a replaced board, the
router's per-pass item collections (`forgetPassItems`: refilled at every pass start) are
emptied — they still held the old board's items (the dispose check found them on Issue214, and
on bm02 when the machine was loaded). Diagnostics: `FR_MEM_STATS=1` also prints `board copies
made N, freed M`; `FR_NO_BOARD_DISCARD=all` (or a list of drop sites: `optimizer`, `candidate`,
`replay`, `history`, `autoroute`, `fallback`) keeps those boards as before.

bm02 (no-GC build): leaked 315.9 MB → 269.7 MB (−46.2 MB, −15%); board copies made 177, freed
176 (the one left is the final board of the job); `FR_NO_BOARD_DISCARD=all` gives 306.0 MB (the
other ~10 MB are the net-number arrays, id maps, outline arrays and copy temporaries above).
Live-memory profile (profiling build, before the copy temporaries were freed): 296.8 → 255.4 MB;
what is still attributed to board copies is ~12 MB, mostly geometry caches computed on freed
copies (tree shapes, outline areas) and their rules. `FR_DISPOSE_CHECK=1` on bm02 (checked
build): 2.47M disposed, 0 reachable after a full collection. SES identical in the checked,
no-GC and profiling builds.

Smoke (`FR_SMOKE_BUILDS="bc-checked bc-nogc"`): checked 38 SAME, no-GC 37 SAME + Issue190
out of memory, only the two old failures besides.

`parity_sample.sh <out> 120 60` (FR_JOBS=4): checked build with `FR_DISPOSE_CHECK=1` **56 SAME,
0 DIFF** + Issue006, Issue179 and the Issue110-Pajalnaja_stancija pair (pre-existing, below);
674M disposals, 224 reachable in 7 boards, all explained: `ShapeTree_Leaf` referred by
`ShapeSearchTree_EntrySortedByClearance` objects in live search-arena chunks (Issue022 has the
same 15 on plain 73fd11f, also with `FR_NO_BOARD_DISCARD=all`), items referred by a `std::regex`
NFA block (known noise) or by a stale map slot of the `std::deque` inside the profiler's
`ArrayDeque` pointing at a reused address (a dead item array; Issue209, Issue214), one
`RegularTileShape` (the E2b class). No-GC build (3.5 GB limit): **48 SAME, 0 DIFF**, 10 boards
`bad_alloc` (RC 134, they still leak more than 3.5 GB), the two old failures; E2b had 35 SAME /
23 out of memory.

Found while testing, **not caused by E7**: Issue110-Pajalnaja_stancija (and its Cyrillic-named
twin) crash on the checked build of main 73fd11f as well (RC 134: `bad_alloc` of ~90 TB). In
the routing-fallback continuation (`RoutingFallbackRunner::runAttempt` → `AutorouteBatchLoop` →
`AutoroutePassRunner::runSingleThread`, pass 2) the `AutorouteAttemptResult` of a connection is
already zero-filled memory when its `details` are recorded in the failure log: it lived in an
arena or region that was released and reused. `FR_NO_SEARCH_ARENA=1` or `FR_NO_GEOM_REGION=1`
make the board SAME again. The E2b sample (checked) still had it SAME, so it came with E3–E6.

### C4 — deferred disposal of removed items and of traces' tree shapes

* `UndoableObjects::sweepNodes` reports every object that only swept nodes held (no live node,
  no map key) to a listener: removed for good. `BasicBoard` keeps those items in a graveyard.
* Traces' precalculated tree shapes are counted (`TreeShapeKeeper`, implemented by
  `BasicBoard`): `ItemSearchTreesInfo` with a keeper (traces only — other items' tree shapes
  may be shared, e.g. with pin shape caches) retains a new array before it releases the old one;
  tree leaves whose bounding shape *is* a tracked tree shape count too (`ShapeTree::
  leafInserted/leafRemoved`, `ShapeSearchTree` forwards to the board) — a cut-out trace's pieces
  reuse its leaves, and a cleared cache leaves the old shapes in the leaves. A shape no array and
  no leaf refers to goes to the graveyard.
* `BasicBoard::releaseGraveyard()` frees the graveyard (items: tree info, net-number array,
  object; then shapes no longer counted; then dropped arrays) only between top-level operations
  — after the routing and the optimization stage, after each optimizer pass and each step-slider
  pass — and only without a live autoroute engine (its drill pages / obstacle rooms refer to
  tree shapes). Callers do keep removed items during a pass (pass item lists, ripped lists).
  `discard` frees it unconditionally. `RoutingFailureLog` keeps the class name as a string
  (its item pointer may be freed). `FR_MEM_STATS` prints the graveyard counters.
* Effect: bm02 449 items (with forced sweeps) / 42k shapes; Issue187 21.5k items / 1.2M shapes —
  but only the shape *objects*: a `Simplex` does not own its line array and lines (offset shapes
  share lines), so most of a tree shape's memory is not freed yet. Next: tree shapes of a trace
  in a region of their own (released with the last array / leaf referring to it).
* (b) dropped search trees: nothing to do in the headless port. `clearanceValueChanged`,
  `clearanceClassRemoved`, `resetCompensatedTrees` and `setClearanceCompensationUsed` have no
  callers (interactive rule editing only); the trees live until `disposeTrees`.

### E9 — job region: unowned memory belongs to the job

Geometry and per-operation temporaries (kGeom, kOp) that no operation region takes and no owner
frees (operations outside regions: statistics, DRC, ledger counting, DSN parsing, SES writing;
caches of heap items; item geometry shared by board copies) now belong to the routing job:
`jrt::JobRegion` (alloc.h) is the outermost region of the routing thread for the whole run
(`main`; the Lua engine thread next). Its memory is the **job heap** (`runtime/jrt/jobheap.cpp`,
no-GC build only): a size-class allocator on 64 KB pages (16-byte classes up to 1 KB, four per
octave up to 32 KB, larger blocks on their own pages) with a page table over the address space,
so `operator delete`, `jrt::dispose` and the domain lookups recognise job-heap blocks in O(1) and
free single objects at once (memory reused, zeroed); the end of the job frees everything left.
A bump arena instead (first try) kept 151 MB on bm02 instead of 102 MB: much of what lands here
is freed explicitly during the run (query results, container growth).

Rules: kGeom / kOp with no operation region → job heap; `DomainScope` with a heap owner → job
heap (the owner's caches and stored geometry are the job's); `geomcopy::keep` to a heap owner
copies into the job heap; `HeapScope` / `onHeap` (process-lifetime statics, the global shove
cache) and kBoard / plain `new` (explicitly owned objects) stay on the heap. Job-heap objects
count as heap objects for copy-out (`geom_domain_of` = 0: no chunk registration).
`FR_NO_JOB_REGION=1` turns it off (e.g. for the live-memory profiler, which only sees heap
blocks). `FR_MEM_STATS` prints what is left in the job heap at the end of the run and the heap
bytes still allocated after it.

Owners found with LeakSanitizer once the job heap took the diffuse part: operation temporaries
tagged kBoard (`ExactCopper::of/ofOval`, `ShapeEntrySide`, `ShapeAndEntrySide`,
`Pin_TraceExitRestriction`, `PadOval`, `ViaMoveHistory`, DSN tokens) are kOp; item caches
(pin / via shape arrays, exact-copper cache, `ItemAutorouteInfo`, tree leaf arrays of an object)
are allocated in the item's domain; `HashMap::setFreesNodes` frees the entries of removed /
cleared mappings (only for maps that never hand out entries: the ledger's item→nets map, which
also owns its net arrays); `ShoveCheckCache` frees its keys, entries and map nodes when it starts
over; a replaced or finished `AutorouteEngine` is freed; a `ComponentOutline` copy frees the empty
net array `copy(id)` made; the board read from the DSN bytes is freed by `route()` when the job
finished on another board, and the CLI frees the routed board and the result at the end.

bm02 (no-GC): job heap 102 MB at the end (peak 102 MB: it is the former leak), heap after the
run 122 → 6.6 MB, LeakSanitizer 24 MB → 6.1 MB unreachable; SES identical; smoke clean (checked
GC build and no-GC build 38 SAME each, only the two old failures).

More owners (second round): a board frees its `NetRoutingLedger` (now in the board's domain)
with its collections; `BoardRules::disposeCopy` / `ClearanceMatrix::disposeCopy` free a board's
own rules (deep copies, or the DSN board's) without what they share (layer structure, padstacks,
constraint records, `ViaRule::EMPTY()`, foreign via infos); `deepCopy` frees the `NetClass`
constructor's arrays it replaces; `DefaultItemClearanceClasses` owns its array; an optimizer
candidate frees its private `RoutingJob` and settings copy (`RoutingJob::disposePrivate`,
`RouterSettings::disposeTree`; `clone()` no longer leaks the constructor's defaults);
`TreeMap::setRetiresEntries` keeps removed entries until the owner says no iterator can hold
them (`UndoableObjects` frees them when no node iterator is live); `disposeAll` also frees the
objects that only unswept, unreachable undo nodes hold (the board dies); GMP memory and
`BigInteger` reps are job-heap blocks while a job runs (reps are shared by value copies, so never
in an operation region). bm02: heap after the run 6.6 → 2.3 MB, LeakSanitizer 6.1 → 1.9 MB.

### E10 — the collector is gone

The Boehm backend is removed: `runtime/jrt/gc.cpp`, the bdw-gc dependency and the `FR_NO_GC` /
`FR_STATIC_GC` options (CMake links GMP only: `FR_STATIC_GMP`), `JRT_NO_GC` (every
`#if JRT_NO_GC` resolved to the collector-free side), `GcThreadScope`, `new_root` /
`destroy_root`, `alloc_uncollectable` (now `jrt::heap_alloc` / `heap_free`), and the
collector-based diagnostics (`FR_DISPOSE_CHECK` with `tests/dispose_check.cpp` and
`tools/mem/dispose_referrers.sh`, `FR_REGION_CHECK`): AddressSanitizer builds (`FR_SANITIZE`,
poisoned arena / region / job-heap memory) and LeakSanitizer replace them. `nogc.cpp` is
`heap.cpp` (operator new = calloc, delete = free, job-heap blocks handed back to the job heap,
GMP memory in the job heap during a job); the statistics are `jrt::heap_live_bytes` /
`heap_peak_bytes` / `heap_alloc_count` (`FR_MEM_STATS` prints "heap" and "still allocated after
the run"). Every build is collector-free: `~/build/freerouting-cpp` is the checked build,
`~/build/fr-nogc` the release build (names kept for the tools).

Lua module: the engine object is a plain `new` (no GC root needed), the engine thread owns a
`jrt::JobRegion` for its lifetime (instead of registering with the collector) and frees a board
the engine replaces (a new load, the routed board) and its board when it stops
(`Engine::dropBoard`). `ui/test_engine.lua` (load, route, save, close) on Issue026: SES identical
to Java.

Owners added after the removal: the last board of a family frees what the family shared
(`BasicBoard::discardLast`: library with padstacks, packages and logical parts, layer structure,
the DSN parser info and coordinate transform; the CLI and the Lua engine call it for the last
board, `discard` stays for copies); every board frees its `Communication` copy (id generator,
observers) and its `RoutingFailureLog`; the DSN reader keeps its text copy on the stack;
`jrt::Array` storage of trivially destructible elements (also `BigInteger`) lives in the array's
own region / job heap, and an array object made while another target was current finds its
domain by address. Tried and reverted: moving `ItemSearchTreesInfo` into the item's domain (even
only for region items) - its destructor (shape release through the keeper) then never runs and
the graveyard frees far fewer tree shapes (bm02 heap after the run 1.8 → 6 MB).

bm02 after this: heap still allocated after the run 1.5 MB (was 122 MB before E9), job heap
100 MB at the end, SES identical; ASan: no memory errors; LeakSanitizer 1.1 MB unreachable at
exit (direct 0.5 MB; was 24 MB before E9). What is left: the `ItemSearchTreesInfo` of the
temporary trace pieces a shove check measures (~0.5 MB with their lists and shape arrays; see
the reverted attempt above), job-level objects of a run (the optimizer's per-candidate
`RouterSettings` defaults and phase metrics, pass counters, a few lazily made board facades /
snapshot managers of copies), and small collections of board copies (`UndoableObjects` maps,
`HashSet` iterators' snapshots).

### E11 — peak memory of large boards

The job heap only frees at the end of the run, so whatever an operation outside a region leaves
behind is the peak. Found with the live profile (`FR_NO_JOB_REGION=1`, `FR_ALLOC_PROFILE_AT_MB=N`
dumps the live blocks by site once N MB are live; `FR_ALLOC_PROFILE_FP=N` attributes them to the
caller N levels up) and `FR_ARENA_WARN_MB=N` (stack of an arena each time it grows by N MB):

* **Item caches of board copies** (pin shapes, outline areas, drill shapes, tree shapes of
  non-trace items) go to a per-board cache region (`BasicBoard::cacheRegion`, kGeom only;
  `ItemCacheScope`), released with the board. A region that also took kOp grew without bound
  (disposed temporaries are not reused in an arena): kOp under an explicit target outside the
  active chain goes to the job heap (`op_alloc`).
* **Tree shapes of a trace** are computed in a local operation region and copied out
  (`geomcopy::keep`): only the shapes stay, not the simplex arithmetic.
* **Plane fracture** (`ConductionArea::getFractureResult`): the evaluation (rasters, enlarged
  obstacle shapes, the overlap query) runs in a local operation region; the result and what the
  memo stored are copied out (`FastPlaneConnectivityGrid::keepOutside`; maps keep their table
  size and order through `HashMap::copyLayout`, so they iterate exactly as before). The island
  set of every connected-set visit of a fractured plane is freed on exit, and the island maps
  are iterated without snapshot collections (`Map::valueVector`, `entryVector`).
* **Overlap queries outside operation regions** (`ShapeSearchTree::overlappingObjects`): the
  shape tests run in a local operation region when kOp would otherwise go to the job region
  (`jrt::ops_go_to_job`); inside candidate / insertion regions nothing changes.
* Operation regions around board statistics' fanout count and the ledger's net marking; the
  connectable index is rebuilt in the board's domain and frees the stale one.
* Losing fallback attempts free their boards (`RoutingFallbackRunner::dropAttempt`).
* `ChangedArea` of a board is made in the board's domain (it outlived the operation region it
  was started in: the crash of Issue214 / Issue269).
* A `Polygon` owns its corner list; the temporary polygons of `PolygonShape` / `Polyline` /
  `TileShape` are freed.

Peak RSS (release build, `/usr/bin/time`, one board at a time; SES identical to Java on all):

| board | E10 | E11 |
|---|---|---|
| bm02 | 192 MB (job heap 100 MB) | 122 MB (job heap 31 MB) |
| Issue187-processor.Z80 | 4442 MB | 1425 MB |
| Issue022-AutoRouter_interrupted | 6402 MB | 1492 MB |
| Issue214-freerouting | crash (5.5 GB) | 3981 MB |
| Issue269-z10_module | crash; out of memory at 6-7 GB | 2387 MB |

### Speed after the migration (2026-11-24, `tools/bench_speed.sh`)

Release builds with the same flags (Release, `-DJRT_CHECKED=0`, LTO): `gc-port-baseline` (Boehm,
single-threaded optimizer) and main bbb4952 (no collector; `-mt 1` and `-mt 4` optimizer
threads), Java through `FreeroutingHeadlessEngine` with its default threads (cores - 1 = 19) and
with one optimizer thread. One process at a time on an otherwise idle machine (WSL, 20 cores);
wall seconds / peak RSS MB; the SES of every C++ run and of Java 1T equals Java's. bm02 is the
mean of three runs.

| board | Java (19 thr.) | Java 1T | baseline | main 1T | main mt4 |
|---|---|---|---|---|---|
| bm02 | 7.2 / 1125 | 11.2 / 781 | 9.1 / 291 | **3.9** / 116 | **2.0** / 200 |
| Issue187-processor.Z80 | 59 / 2943 | 56 / 796 | 254 / 295 | 56 / 1364 | 54 / 1542 |
| Issue022-AutoRouter_interrupted | 49 / 2493 | 62 / 912 | 70 / 371 | 48 / 1470 | 45 / 2201 |
| Issue214-freerouting | 54 / 2066 | 56 / 870 | 152 / 743 | 100 / 3976 | 97 / 4149 |
| Issue110-RelayModule | 47 / 2536 | 116 / 1046 | 334 / 304 | 215 / 1278 | 113 / 2303 |
| Issue178-KeebMaker_Sofle_Choc | 62 / 3862 | 196 / 1534 | 683 / 819 | 685 / 1986 | 275 / 3060 |
| DAC2020 bm01 | 269 / 4678 | 652 / 1531 | 1837 / 370 | 1128 / 6266 | 529 / **14163** |

* main is faster than the baseline on every board but Issue178 (equal). There the baseline spends
  about 55% of its time in the collector (`GC_mark_from`, `GC_header_cache_miss`, ...; perf), and
  main has no collector cost, yet takes as long: main's own code is slower on that board.
  Its profile is geometry and tree search (`MinAreaTree::collectOverlapsUnlocked` 7%,
  `BigIntAux::binaryGcd` 7%, `completeShape`, `Simplex`, `Line`); the memory model's own share
  is small (`is_heap_block` 1.4%, `dynamic_cast` about 6%, malloc / rb-tree in libc and
  libstdc++ about 18%).
* Against Java with one optimizer thread, main is faster on the small and middle boards and
  1.7-3.5x slower on the large ones (Issue214, Issue110, Issue178, bm01): single-threaded code,
  not the thread count.
* The peak memory of main is above the baseline's (the job heap, see E11) and was far above it
  with optimizer threads: 14 GB on bm01 with four threads (see E12).

### E12 — shove checks in their own regions; temporary items' tree info

Found on bm01 (`-mt 4`: 14 GB) with an RSS timeline plus `FR_MEM_STATS` (arenas live, heap,
job heap every 20 optimizer candidates) and `FR_ARENA_WARN_MB` stacks (a RelWithDebInfo build;
`addr2line`), then the live profile at exit:

* **One connection search grew past 1-2 GB** (the engine's search region takes kOp, and the
  search runs a via check for every drill and layer and a shove check for every shoved door),
  in the autorouter and inside optimizer candidates: arenas live jumped from 0.2 to 6.2 GB within
  20 candidates, and the freed memory stayed in malloc (RSS 10-14 GB). Java's collector frees
  these temporaries during the search. `ForcedViaInserter::checkLayer` and both
  `TraceShover::check` overloads return a value only (the shove cache stores heap copies, the
  board records ids, the failing obstacle and layer), so each now runs in a local region taking
  kOp: their geometry and substitute trace pieces die with the check.
* **Temporary items leaked their tree info**: a substitute trace piece of a shove check lives in
  an operation region and is never disposed, but `Item::newSearchTreesInfo` put its
  `ItemSearchTreesInfo` (and the keeper's count of its tree shapes) on the heap: 130 MB still
  allocated at exit on Issue110 (`-mt 4`), 2.1 GB on bm01. An item that lies in a live region now
  gets its info in that region, without a keeper (its tree shapes are already computed in its
  domain).

bm01 `-mt 4` (release, SES identical to Java):

| | before | after |
|---|---|---|
| RSS during the autorouter (at 330 s) | 3.4 GB | 1.1 GB |
| arenas live, optimizer (max of the samples) | 6.2 GB | 1.1 GB |
| peak RSS | 14.2 GB | 2.7 GB |
| heap still allocated after the run | 2.2 GB | 46 MB |

Peak RSS of every board of the speed comparison, MB (release, same flags; SES identical to Java
on all; the times of this run are not comparable, another job shared the machine; bm01 `-mt 4`
from the separate run above):

| board | main 1T before | after | main mt4 before | after |
|---|---|---|---|---|
| bm02 | 116 | 115 | 200 | 171 |
| Issue187-processor.Z80 | 1364 | 1358 | 1542 | 1609 |
| Issue022-AutoRouter_interrupted | 1470 | 1184 | 2201 | 1615 |
| Issue214-freerouting | 3976 | 4016 | 4149 | 4235 |
| Issue110-RelayModule | 1278 | 954 | 2303 | 1354 |
| Issue178-KeebMaker_Sofle_Choc | 1986 | 1523 | 3060 | 2388 |
| DAC2020 bm01 | 6266 | 1250 | 14163 | 2699 |

Issue214 is unchanged: its peak is the job heap (E11 TODO), not search regions.
Issue110 `-mt 4` (profiling build): heap after the run 204 -> 73 MB. ASan clean on bm02,
Issue187 (19 threads), Issue690, Issue1161, Fanout-blind-via-plane-garland; LeakSanitizer
bm02 1.1 -> 0.6 MB, Issue187 5.8 -> 3.4 MB.

### E13 — the job heap's diffuse temporaries; failing fast_casts cached

**Job-heap profile.** The heap's live profile cannot see job-heap blocks, and
`FR_NO_JOB_REGION=1` distorts the picture (the local regions keyed on `ops_go_to_job` and the
caches of `ItemCacheScope` then go elsewhere). A profiling build (`-DJRT_ALLOC_PROFILE=1`) now
records the job heap itself: `FR_JOB_PROFILE=<file>` samples one block in 16 by address with its
site (the return address of `job_heap_alloc`, into which `Arena::allocate` and operator new are
inlined) and its caller `FR_ALLOC_PROFILE_FP` levels up; frees subtract, the dump at exit is the
live job heap. `tools/mem/job_profile.sh` resolves it (FP=3-5 find the top-level operation).

On Issue214 (job heap 2.7 GB at exit, peak RSS 4.0 GB) it showed temporaries of operations that
run outside any operation region, mostly queries whose search shapes (and the lazily cached
simplex of those octagons, in their domain) were made in the job heap:

* **Contact queries** (`Trace` / `DrillItem` / `Pin` / `ConductionArea::getNormalContacts`, the
  connected-set walk `Item::getConnectedSet`): the result set is made first, in the caller's
  domain (its nodes follow it), then `jrt::OpRegionIfTopLevel` opens a local region taking kOp
  where kOp would otherwise reach the job heap or the heap (inside an operation region nothing
  changes).
* **Incomplete counts** (`NetRoutingLedger::countNet`, `DanglingCopper::open`,
  `ExitStubs::open`): the triangulation and its walks die in a local region (a count escapes).
* **Clearance checks** (`Item::clearanceViolations`, from the DRC, the batch loop and the
  fallback runner): the shape tests run in a local region; each violation is made in the
  caller's domain with a copy of its shape (`geomcopy::tileShape`).
* `ops_go_to_job` is also true without any current region (HeapScope, or no job region): nothing
  frees temporaries on the heap either.

**fast_cast.** A failing cast was never cached (`kFail` did not pass `packable`), so every
`fast_cast` that returns nullptr (an inner tree node tested for a leaf, an item tested for a trace)
ran libstdc++'s `dynamic_cast` on the virtual-inheritance hierarchy: about 8-10% of the time in
the maze search. Failing casts are cached like the others now; the two direct `dynamic_cast`s in
hot paths (`ShapeSearchTree::overlappingObjects`, `SortedRoomNeighbours`) became `fast_cast`s.

**Arena lookup of dispose.** `is_heap_block` (every `jrt::dispose`) took a global mutex and
searched a map of the live arena chunks. A table of 64 KB pages now answers lock-free for pages
that lie wholly inside a chunk or touch none; only a chunk's two edge pages still need the map.
bm02 `-mt 1` 2.84 -> 2.75 s, `-mt 4` 1.55 -> 1.40 s; Issue187 `-mt 4` 37.8 -> 36.0 s.

Results (release, SES identical to Java on all):

| | before (E12) | after (E13) |
|---|---|---|
| Issue214 `-mt 1`: peak RSS | 3957 MB | 1911 MB |
| Issue214 `-mt 1`: job heap at exit | 2714 MB | 939 MB |
| bm01 `-mt 4`: peak RSS / job heap at exit | 2699 / 424 MB | 2753 / 319 MB |
| bm02 `-mt 1` (LTO build, idle machine, mean of 3) | 3.67 s | 2.87 s |
| Issue187 `-mt 1` | 51.8 s | 37.4 s |
| Issue110 `-mt 1` | 196 s | 152 s |

bm01's peak is the optimizer's search-region spikes (E12 TODO), not the job heap.

### Java vs C++ at 4 and 19 optimizer threads (2026-11-25, after E13)

`FR_VARIANTS="java-4t java-19t main-mt4 main-mt19" tools/bench_speed.sh`: Java through
`FreeroutingHeadlessEngine` with 4 / 19 optimizer threads, C++ ef26aa2 (release, LTO) with
`-mt 4` / `-mt 19`; one process at a time on an idle machine (WSL, 20 cores). Wall seconds /
peak RSS MB; the SES of every run equals Java's (4 threads). bm02 is the mean of three runs.

| board | Java 4 thr. | Java 19 thr. | C++ 4 thr. | C++ 19 thr. |
|---|---|---|---|---|
| bm02 | 4.3 / 988 | 3.7 / 1202 | **1.5** / 177 | **1.4** / 329 |
| Issue187-processor.Z80 | 26.9 / 1417 | 31.1 / 2531 | 35.3 / 1529 | 43.9 / 1874 |
| Issue022-AutoRouter_interrupted | 26.1 / 1400 | 29.0 / 2585 | 31.5 / 1348 | 43.2 / 3041 |
| Issue214-freerouting | 50.5 / 1336 | 49.6 / 2190 | 69.9 / 2050 | 69.5 / 2802 |
| Issue110-RelayModule | 54.0 / 1691 | 46.1 / 2604 | 77.0 / 1267 | 69.8 / 2366 |
| Issue178-KeebMaker_Sofle_Choc | 76.2 / 2855 | 60.0 / 3933 | 129.0 / 2081 | 123.2 / 3688 |
| DAC2020 bm01 | 220.6 / 2394 | 169.6 / 4632 | 334.4 / 2545 | 349.2 / 5697 |

* C++ is 2.7-2.9x faster than Java on bm02 and 1.2-1.4x slower on the middle boards at four
  threads; on the large boards Java is 1.4-1.7x faster at four threads and up to 2x at 19.
* Java gains from 4 to 19 threads on the large boards (bm01 -23%, Issue178 -21%); C++ does not,
  and is slower with 19 than with 4 on Issue187, Issue022 and bm01: the optimizer threads
  contend. Suspects: the job heap's global lock while workers run (`JobHeapSharedScope`: every
  job-heap allocation and free), two malloc arenas (`M_ARENA_MAX`), `BasicBoard::copyLock`.
* Memory: with four threads C++ needs less than Java on bm02, Issue110 and Issue178, about the
  same on Issue187, Issue022 and bm01, more on Issue214 (job heap); with 19 threads more than
  Java on Issue022, Issue214 and bm01 (search-region spikes of more concurrent candidates).

### E14 — optimizer threads: malloc locks, the arena lookup, cancelled candidates

Where the time goes (`-v` logs; C++ e13 against Java, 4 threads, seconds): before the optimizer
(fanout, autorouter, single-threaded in both) C++ takes 1.2-1.55x Java's time (Issue110 28.5 /
20.2, Issue214 70 / 47, bm01 136 / 88); the optimizer passes 1.6-2x (Issue110 52 / 32, Issue178
106 / 53, bm01 233 / 131); Issue187 is mostly the step slider after the optimizer (32 / 21).
From 4 to 19 threads Java's passes gain 30-40 % (bm01 131 -> 81 s), C++'s did not (233 -> 243).

At 19 threads the workers mostly waited (Issue178: about 9 busy of 19, 283 s system time and
13.9 million voluntary context switches). The waits (perf, lock-wait samples by caller):

* **malloc's arena locks.** The candidates allocate and free many small heap objects (tree nodes,
  `ItemSearchTreesInfo`, list and map nodes of their board copies); glibc serves 19 threads from
  two arenas (`M_ARENA_MAX`). `MALLOC_ARENA_MAX=20` alone gave Issue178 139.7 -> 94 s but 6.2 GB
  instead of 3.8; jemalloc (LD_PRELOAD) 91 s at 3.9 GB. Fix: `raw_alloc` / `raw_free` keep a
  per-thread cache of blocks up to 1 KB (by 16-byte class, at most 4 MB a thread, flushed when the
  thread ends), and the live-byte counters are added per thread in 256 KB batches. Not in
  sanitizer builds.
* **`g_chunkMutex` in `in_arena_chunk`** (every `dispose` of a pointer on the edge page of an
  arena chunk took it). A chunk is at least 1 MB, so a 64 KB page holds the end of at most one
  chunk and the start of at most one: the page table keeps both offsets and the lookup takes no
  lock any more.
* **The tail of an early-stopped pass.** C++ waited for every running candidate (up to 2 x
  threads) before the pass could end; Java leaves them running. The candidates now see a per-pass
  stop (`PassStop`) that the pass sets when it stops reading results; their results were never
  read (Issue022 at 19 threads: 8 s of tail, Issue187 5 s).

Not the cause: the worker's chunk pool (16 MB; 512 MB changed nothing), calloc vs malloc, glibc's
trim / mmap thresholds, `copyLock` (the board copies are 3-4 % of the candidates' time).
Issue178 at 19 threads: 139.7 -> 95.4 s, system time 283 -> 33 s, context switches 13.9 -> 1.0
million, peak 3.8 -> 4.3 GB (more candidates at once), SES the same; with the cache the workers
are 16-17 busy of 19 (10 physical cores, SMT): the rest is single-threaded speed.

`FR_VARIANTS="java-4t java-19t main-mt4 main-mt19 old-mt19" tools/bench_speed.sh` (2026-11-25;
main = E14, old = ef26aa2, interleaved per board: the laptop's speed drifts with its heat).
Wall seconds / peak RSS MB; all 45 SES equal Java's (4 threads); bm02 the mean of three runs.

| board | Java 4 thr. | Java 19 thr. | C++ 4 thr. | C++ 19 thr. | C++ 19 thr. before |
|---|---|---|---|---|---|
| bm02 | 4.3 / 966 | 3.95 / 1308 | **1.4** / 176 | **1.36** / 340 | 1.48 / 327 |
| Issue187-processor.Z80 | 27.4 / 1365 | 29.2 / 2626 | 35.8 / 1531 | 44.9 / 1924 | 46.2 / 1792 |
| Issue022-AutoRouter_interrupted | 26.1 / 1371 | 29.3 / 2599 | 31.9 / 1274 | 36.3 / 3268 | 45.8 / 3191 |
| Issue214-freerouting | 50.0 / 1392 | 49.9 / 2152 | 72.0 / 2046 | 71.7 / 2815 | 70.5 / 2796 |
| Issue110-RelayModule | 56.0 / 1581 | 45.7 / 2532 | 74.4 / 1505 | 64.4 / 1942 | 71.8 / 1949 |
| Issue178-KeebMaker_Sofle_Choc | 77.1 / 3171 | 61.8 / 3889 | 126.7 / 2072 | 88.0 / 4144 | 125.0 / 3681 |
| DAC2020 bm01 | 225.0 / 2366 | 172.5 / 4667 | 325.0 / 2465 | 260.2 / 5932 | 356.7 / 6345 |

* C++ now gains from 4 to 19 threads like Java: bm01 -20 % (Java -23 %), Issue178 -31 % (Java
  -20 %), Issue110 -13 % (Java -18 %). Against the e13 build at 19 threads: bm01 -27 %, Issue178
  -30 %, Issue022 -21 %, Issue110 -10 %.
* Still slower than Java: 1.4-1.5x at 19 threads on the large boards. The rest is
  single-threaded speed: the autorouter before the optimizer (Issue214 is almost all of it), the
  candidates themselves, the step slider (Issue187).
* Issue187 and Issue022 stay slower with 19 threads than with 4, as in Java: short passes whose
  candidates beyond the early stop are wasted work.

Exact arithmetic (asked for a filtered-predicate decision; perf, frame pointers, no LTO):
`BigInteger` with GMP, `RationalPoint`, `RationalVector`, `BigIntDirection` and the exact parts
of `Line::intersection` together take 0.2-0.7 % (Issue110 0.7 %, Issue178 0.25 %, Issue187
0.2 %); 0.01-0.09 % of the BigInteger operations leave int64 for GMP (Issue110 17 thousand of
176 million). The machine-precision side tests take 3-6.5 %. A floating-point filter in front of
the exact predicates would save well under 1 %.

### E15 — single-threaded speed: step slider, tree search, connected sets

Profiles (perf, frame pointers, no LTO) of the phases that are single-threaded in both ports.
Issue187 spends three quarters of its CPU time in the step slider, Issue214 more than half in the
connected sets of a fractured plane (`BoardStatistics::isConnectedToWholeNet` and the batch loop
call `Item::getConnectedSet` after every autorouter pass). What changed (all exact: the SES stays
the same):

* **`clearAllItemTemporaryAutorouteData`** walked the whole item list after every tried move of
  the step slider (17 % of it), clearing nothing: the slider creates no autoroute data. The board
  keeps `autorouteInfoMayExist`, set where such data is created (`Item::getAutorouteInfo`,
  `Via::getAutorouteDrillInfo`) or comes with an inserted item, cleared by the walk; undo / redo
  already clear the data of the items they restore. The walk is skipped while the flag is clear.
* **Incomplete counts after an undo.** An undo puts the board back as it was at its snapshot, so
  `NetRoutingLedger` keeps the counts of the last four snapshots taken with all counts up to date
  and takes them back instead of counting the touched nets again (9 % of the slider). The count of
  a net is its number of airlines, which does not depend on the order of its items; an in-place
  net change (`noteNetAssignmentChanged`), which no undo reverts, voids the kept counts.
* **`Polyline::lengthApprox()`** is computed once per polyline (`StepSlider::cost` sums the
  length of every trace per tried move).
* **Tree search.** `MinAreaTree::collectOverlapsUnlocked` was the largest self time of every board
  (14-17 %), with `IntOctagon::intersects` and the leaf cast behind it: each visited node loaded
  its bounding shape through a pointer and dispatched twice. A tree node now keeps the eight
  coordinates of an octagon bounding shape (`ShapeTree_TreeNode::oct`, written by
  `setBoundingShape`) and whether it is a leaf; the searches (`collectOverlapsUnlocked`, the room
  completion of `ShapeSearchTree` / `ShapeSearchTree45Degree`) and the insertion
  (`positionLocate`) compare the coordinates as `IntOctagon::intersects` / `unionArea` /
  `unionEquals` do. A node is 36 bytes larger.
* **Item comparisons in collections.** `jrt::equalsOf` called `element->equals(probe)` (Java calls
  `probe.equals(element)`): a virtual call and a load of every element. Java's `Item` does not
  override `equals`; the port declares `Item::equals` final (identity) with the marker
  `jrt_identity_equals`, and `equalsOf` compares item pointers directly. `Item::compareTo` is final
  too, and `jrt::compareOf` on item pointers compares the ids without the dynamic cast.
* **The islands of a fractured plane** (`ConductionArea::getConnectedSetRecu`): for every item of
  the connected set Java searches every island list (`List.contains`). The port looks the items up
  in an index of the result's island lists (item -> island keys, in entry order, so the same keys
  are added in the same order), cached per thread for the last four results by a serial number of
  the result (`FastPlaneConnectivityGrid_PlaneFractureResult::serial`; the lists of a result never
  change).

* **More tree search** (after Issue022 at one thread lost to Java, 34.4 s vs 30.4): an inner
  node keeps copies of its children's octagon coordinates (`childOct`, refreshed by
  `setBoundingShape` and where children are linked), so a search tests a child before pushing it
  and never loads one that does not intersect (the others are pushed in the same order); the
  pushed children are prefetched; a leaf keeps its object's `shapeLayer` (`ShapeTree_Leaf::layer`,
  set by `ShapeSearchTree::leafInserted` and where a leaf is handed to another trace; an item's
  layer never changes in a tree), and the room completion and the overlap filters pass over a
  leaf of another layer without loading its object; `ArrayStack::pop` no longer clears the slot.
* **`Line::directionInts`** keeps the normalized direction of a line without `dir` (the gcd was
  computed on every call, 3 % of Issue022).
* **`ItemSearchTreesInfo`** remembers the records of the last two trees looked up (the walk of its
  `LinkedList` through the virtual iterator was 4-5 % of Issue110).
* **`Simplex` sorts its few lines** by a stable insertion sort (no `std::function` comparator, no
  merge buffer; every stable sort gives the same order).

A Java profile (JFR, `-XX:FlightRecorderOptions=stackdepth=2048`, `jfr print --stack-depth 2048`)
has nearly the same shares per phase as the C++ profile; the tree search takes Java twice the
time it takes C++ now (Issue022 26 % vs 13 %), the rest of the geometry is 15-25 % slower in C++,
spread over many functions (Issue110: the maze search 99 s vs 80, `expandToOtherLayers` 36 s vs
26).

Not done: the clearance-violation count of the step slider (a full DRC of the board per accepted
move, 8 % of the slider) could check only the changed items, but that holds only on boards
without conduction areas; the connected sets of fractured planes are still revisited for every
contact (Java's algorithm).

`FR_VARIANTS="java-1t main-mt1 java-4t main-mt4 java-10t main-mt10" tools/bench_speed.sh`
(2026-11-25, E15 release build with LTO, interleaved per board; the machine was running another
job at the same time: compare the ratios, not the seconds). Wall seconds Java / C++ and C++/Java;
all 54 SES equal Java's; bm02 the mean of three runs. Peak RSS: C++ below Java on bm02 (116-261 MB
vs 0.8-1.2 GB), Issue110 and Issue178 at 4 and 10 threads; above it on Issue214 (2.0-2.4 GB vs
0.8-2.1) and Issue187 at one thread.

| board | 1 thread | 4 threads | 10 threads |
|---|---|---|---|
| bm02 | 6.67 / 2.53 = **0.38** | 5.02 / 1.24 = **0.25** | 4.76 / 1.13 = **0.24** |
| Issue187-processor.Z80 | 31.9 / 26.3 = **0.82** | 27.8 / 26.4 = **0.95** | 30.6 / 24.6 = **0.80** |
| Issue022-AutoRouter_interrupted | 31.5 / 31.0 = **0.98** | 29.5 / 27.8 = **0.94** | 32.3 / 31.1 = **0.96** |
| Issue214-freerouting | 56.0 / 43.2 = **0.77** | 51.5 / 40.2 = **0.78** | 51.3 / 41.2 = **0.80** |
| Issue110-RelayModule | 132.3 / 164.2 = **1.24** | 105.2 / 129.4 = **1.23** | 103.2 / 70.9 = **0.69** |
| Issue178-KeebMaker_Sofle_Choc | 219.3 / 328.7 = **1.50** | 79.4 / 107.6 = **1.36** | 68.8 / 80.2 = **1.17** |
| DAC2020 bm01 | 509.9 / 640.6 = **1.26** | 240.6 / 291.2 = **1.21** | 193.3 / 295.5 = **1.53** |

C++ is faster on the boards where the step slider, the connected sets or the autorouter
dominate (bm02, Issue187, Issue022, Issue214); the large boards, whose time is the optimizer's
candidates (the maze search on board copies), are still 1.2-1.5x slower.

## TODO (after E10)

* **Peak memory of large boards** (after E13): Issue214 still keeps 0.9 GB in the job heap at
  exit (`tools/mem/job_profile.sh`): tree-shape geometry of inserted items (`ShapeTree::insert`,
  `calculateTreeShapes`, `Simplex::cornerApprox` caches), the board outline's slices, the
  incompletes of the DRC (`DesignRulesChecker::calculateAllIncompletes` keeps them), airlines of
  the batch loop.
* **LeakSanitizer residue** (bm02, after E12: 0.6 MB unreachable): small job-level objects;
  `tools/mem/asan_run.sh` lists them by site and caller (`FR_ASAN_CTX=20` for deeper stacks).
  With optimizer threads (Issue110 `-mt 4`, 73 MB still allocated at exit): the worker threads'
  shove caches (`ShoveCheckCache::put`, about 14 MB), the connectable index
  (`BoardConnectivityQueries::getConnectableIndex`, 9 MB), two board copies of the worker
  state / undo facade (13 MB).
* **Search-region spikes after E12**: the arenas still reach about 1 GB live at times in the
  optimizer on bm01 (`-mt 4`), less than before but more than Java's whole heap there; a profile
  of what the search region keeps (`FR_ARENA_WARN_MB=64`) is the next step.
* **Performance** (see E15): C++ is faster than Java at 1, 4 and 10 threads on bm02, Issue187,
  Issue022 and Issue214; the large boards (Issue110 at 1 and 4 threads, Issue178, bm01) are
  1.2-1.5x slower, in the optimizer's candidates: the maze search and its geometry
  (`expandToOtherLayers` / `ForcedPadRouter::checkForcedPad`, `completeShape`, `Simplex`
  intersections), 15-25 % slower than Java's JIT code, spread over many functions.
* Not freed yet: the strict-DRC snapshots of `AutorouteConnectionRouter` (only with
  `strict_drc`), the `InteractiveSession`
  of the Lua engine, job-level objects of `route()` (job, pipeline, settings merger: freed with
  the process).
* The arena/region escape behind the Issue110-Pajalnaja_stancija crash (see E7).

### E3 — operation regions take kOp too

A region can take per-operation temporaries (`kOp`), not only geometry (`setTakesOps`); `kOp`
goes to the nearest region from the current kGeom target outwards that takes it, so the
connection searches and insertions inside a candidate put their temporaries into the
candidate's region; `HeapScope` and `DomainScope` with a heap owner send kOp to the heap.
`jrt::Array` storage follows an array in any enclosing region. Enabled for optimizer candidates
(everything they allocate dies with them; the `ItemRouteResult` is copied to the heap).
Escapes fixed: the global `ShoveCheckCache` stores heap copies of key and entry; the two
function-local kOp statics are created with `onHeap`. bm02 no-GC leak 1.24 → 0.84 GB.

Verification is now `tools/smoke.sh [tag]` (~1–2 minutes): bm02 plus the fixtures that Java
routes within 12 s (40 boards) on the checked and the no-GC build; it prints only verdict
counts and unexpected results (Issue006 / Issue179 and no-GC out-of-memory are expected).

### E2b — owners for per-operation collections (query results; parallel branch, merged)

Collections (not geometry) that a query creates and its caller reads and drops are now freed at
the end of the caller's scope; iteration order and semantics are unchanged.

* Tools: `jrt::DisposeOnExit` (`alloc.h`; scoped owner, `reset()` where Java reassigns the
  variable, usable inline: `jrt::DisposeOnExit(x->getNormalContacts())->isEmpty()`),
  `jrt::DisposeAllOnExit<E>` / `jrt::disposeWithElements` (`collections.h`) for a list that also
  owns its elements. `TreeSet` owns (and disposes) its heap comparator.
* Owned now: the sets of `ShapeSearchTree::overlappingObjects` / `overlappingObjectsWithPointIn`
  / `overlappingItemsWithClearance` at every caller that only iterates them; tree-entry lists of
  `overlappingTreeEntries*` together with their `ShapeTree_TreeEntry` objects (callers that do
  not keep an entry; not `SortedRoomNeighbours`, which keeps some in `ownNetObjects`), the
  `EntrySortedByClearance` list and the query-copper holder arrays inside the clearance query,
  the temporary empty `ignoreNetNos` arrays of query calls; results of
  `get{Normal,Start,End,All}Contacts`, `getConnectedSet` / `getConnectionItems` (where only
  read or copied by `addAll`/`removeAll`), `getItems` / `getPins` / `getSmdPins` / `getVias` /
  `getTraces` / `getConductionAreas`; `Sorted45Degree-` / `SortedOrthogonalRoomNeighbours` (the
  neighbour objects, their set and flag array, owned by `calculate()`); the stale contact sets
  of `BasicBoard::cachedNormalContacts` when the item list changes (the walks that read them do
  not change the list). A few heap iterators became `ScopedIter`.
* Left alone: results that are stored or passed on (`autorouteConnection` start sets,
  `NetIncompletes`, `removeItems(...)` arguments), file I/O and parser code (runs once),
  everything in `AutorouteEngine` and the room completion of `ShapeSearchTree45Degree`
  (region work of E1), `HashMap` nodes (entries can be handed out, so neither `clear()` nor the
  destructor frees them).
* Resolved (main, after the merge): the `RegularTileShape` survivors below were real stale
  pointers, not a check artefact — `MinAreaTree::removeLeafUnlocked` disposed the removed
  parent's bounding shape and the parent, but on an optimizer worker board the parent lives in
  the board arena, so `dispose` leaves it in place and live arena memory kept a pointer to the
  freed shape (more disposals = more such nodes). The node is now cleared before the shape is
  freed: Issue022 1253 → 15, Issue102 ~11.3k → 43 survivors. The rest (`Set<Item*>`,
  `Array<int>`, `std::function`, referred from fields of unrelated live objects) are artefacts
  of the check mode in the GC build (the collector reclaims disposed, unreachable blocks and
  reuses the addresses); ASan on the no-GC build reports no double free or use after free on
  Issue022. Also: `BasicBoard::cachedNormalContacts` allocates the cached sets in the item's
  domain (they could live in an operation region and outlive it in the board cache).
* Former note: on 4 large sample boards `FR_DISPOSE_CHECK` reports `RegularTileShape` survivors
  (Issue015 24777, Issue022 1253, Issue027 62–86k, Issue102 ~11.3k; stable between runs and
  under other Boehm heap settings), none on bm02 and the other 50 boards. Every explained one is
  a `MinAreaTree` bounding union that a word inside a 1 MB arena chunk refers to, at the place
  of a `ShapeTree_InnerNode` / `ShapeTree_Leaf` next to search-arena objects (stale tree nodes;
  pooled chunks are reachable through the uncollectable pool and are only cleared in checked
  builds, so their stale tails are scanned). E2 disposes no geometry and no tree node; the base
  (72a3e70) gives 0 on these boards, also with a check every 2^17 disposals. Bisection on
  Issue102: E2 without `src/autoroute` 12581, without `src/board` (and the other non-autoroute
  files) 1, with only the search-tree files 1, without them 1 — no single part, only the
  volume of disposals (55M vs 33–35M per run). Likely a false positive of the check (a stale
  chunk word pointing to an address that the collector reused for a union that was later
  disposed); not proven. Routing is unaffected: a disposed shape has a poisoned vtable in this
  mode and any use would crash, and all of these boards are SAME in the check run, the checked
  build and the no-GC build. Note also that only the first 200 survivors of a run get the
  referrer search; the rest are counted as rooted without it.

* Diagnostics: the branch's own leak profiler (`JRT_LEAK_PROFILE`) was dropped in the merge in
  favour of the equivalent live-memory profiler on main (`JRT_ALLOC_PROFILE` with `FR_NO_GC`,
  `tools/mem/live_profile.sh`, see "Live-memory profile"). Its measurement:
  On bm02: ~1.65 GB still live, ~850 MB of it `kGeom` geometry, then `Array` storage of geometry,
  the room lists of `completeShape`, undo-list map copies of board copies.
* The referrer report of `FR_DISPOSE_CHECK` now names the object around a referring word
  inside a large block (arena / region chunk) by the nearest preceding word into the executable
  (`[object word exe+offset at -N]`; `nm -C` gives the vtable).
* Known noise: a survivor referred to from a 2048-byte block of a `std::regex` NFA
  (`_Sp_counted_ptr_inplace<_NFA>` control block) is a stale stack word copied into the padding
  of a regex state: the static patterns of `ExitStubs` are compiled lazily in the middle of
  routing (seen once each on Issue102, Issue107, Issue214).

bm02 (no-GC build): leaked 2087.8 MB → 1671.1 MB (−417 MB, −20%); disposed objects 1.16M →
6.16M. SES identical in the release, checked and no-GC builds; `FR_DISPOSE_CHECK=1`: 6.16M
disposed, 0 survivors.

`parity_sample.sh <out> 120 60` (FR_JOBS=4): release with `FR_DISPOSE_CHECK=1` 54 SAME + the two
old failures + 4 over the 900 s limit in check mode (Issue110-RelayModule, Issue178, Issue214,
Issue269; machine shared with another sample run); checked build 57 SAME + the two old failures
+ Issue178 over 900 s; no-GC build (3.5 GB limit) 35 SAME, 0 DIFF, 23 boards leak-limited
(RC 134), the two old failures. Issue178 alone (release): SAME in 482 s. No DIFF anywhere.


