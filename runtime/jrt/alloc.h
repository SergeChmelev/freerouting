// Allocation lifetime tags (MEMORY_MODEL.md, stages A/B).
//
// Every heap allocation of the port names the lifetime of the object it creates:
//
//   new (jrt::kBoard) Trace(...)     board lifetime: owned by the board or by another long-lived
//                                    owner (rules, library, settings, job, statics)
//   new (jrt::kOp) jrt::ArrayList<>  per-operation temporary: dead when the call / action ends
//   new (jrt::kGeom) IntPoint(x, y)  immutable geometry (stage D: value types / ref-counting)
//   new (jrt::kSearch) ExpansionDoor connection-search arena (see jrt::Arena below)
//   new (jrt::kBoardData) Via(...)    structural board data (items, undo nodes, search-tree nodes):
//                                    the current board arena (optimizer worker board) or GC heap
//
// While the Boehm collector is in place kBoard / kOp / kGeom all forward to the global operator
// new (GC heap), so the tags change nothing at run time; they document ownership for stage C and
// let tools/mem/classify_allocs.py count the sites. kSearch allocates from the arena that is
// current on this thread (jrt::ArenaScope), kBoardData from the current board arena
// (jrt::BoardArenaScope); both fall back to the GC heap when no arena is current.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <type_traits>
#include <typeinfo>
#include <vector>

// AddressSanitizer builds (FR_SANITIZE): arena memory that is not handed out is poisoned, so a
// read of a released region / arena object is reported like a heap use-after-free.
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define JRT_ASAN_POISON(p, n) ASAN_POISON_MEMORY_REGION((p), (n))
#define JRT_ASAN_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#else
#define JRT_ASAN_POISON(p, n) ((void)(p), (void)(n))
#define JRT_ASAN_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

namespace jrt {

struct BoardLifetime {};
struct OpLifetime {};
struct GeomLifetime {};
struct SearchLifetime {};
struct BoardDataLifetime {};

inline constexpr BoardLifetime kBoard{};
inline constexpr OpLifetime kOp{};
inline constexpr GeomLifetime kGeom{};
inline constexpr SearchLifetime kSearch{};
inline constexpr BoardDataLifetime kBoardData{};

/**
 * Bump-pointer arena for per-operation object graphs (MEMORY_MODEL.md section 3).
 *
 * Objects allocated here are never destroyed individually: release() drops all of them at once
 * (destructors do not run, exactly like for collected objects). Chunks come from the GC heap and
 * are scanned conservatively, so arena objects keep their GC-allocated members (shapes,
 * collection buffers) alive and GC objects may point into the arena. release() wipes the used
 * memory (poison pattern in checked builds, zero otherwise, so stale data neither keeps garbage
 * alive nor hides a use-after-release) and returns the chunks to a per-thread pool.
 *
 * Escape rule: nothing allocated in an arena may still be referenced by the board (or anything
 * else that is used later) after release().
 */
class Arena {
 public:
  Arena() = default;
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  void* allocate(std::size_t n) {
    n = (n + 15) & ~std::size_t{15};
    if (static_cast<std::size_t>(end_ - cur_) < n) refill(n);
    void* p = cur_;
    cur_ += n;
    JRT_ASAN_UNPOISON(p, n);
    return p;
  }

  /** Drops every object of the arena and returns its memory to the thread's chunk pool. */
  void release();

  /** Forgets the chunks without wiping them; the collector reclaims them once unreachable.
   * Used when objects may still be referenced (e.g. an exception left the operation midway). */
  void abandon();

  /** allocate() for container storage that may be given back (recycle): small blocks are
   * reused from per-size free lists, so churning containers in a long-lived arena do not grow
   * it without bound. The memory is zero-filled like allocate()'s. */
  void* allocateReusable(std::size_t n) {
    n = (n + 15) & ~std::size_t{15};
    std::size_t cls = n / 16 - 1;
    if (cls < kFreeClasses && free_[cls] != nullptr) {
      void* p = free_[cls];
      JRT_ASAN_UNPOISON(p, n);
      free_[cls] = *static_cast<void**>(p);
      std::memset(p, 0, n);
      return p;
    }
    return allocate(n);
  }
  /** Gives a block of allocateReusable(n) back (blocks above the size classes are abandoned
   * until release()). */
  void recycle(void* p, std::size_t n) {
    n = (n + 15) & ~std::size_t{15};
    std::size_t cls = n / 16 - 1;
    if (cls >= kFreeClasses) return;
    *static_cast<void**>(p) = free_[cls];
    free_[cls] = p;
    JRT_ASAN_POISON(static_cast<char*>(p) + sizeof(void*), n - sizeof(void*));
  }

  bool empty() const { return head_ == nullptr; }
  std::size_t bytesAllocated() const;
  /** True if `p` points into memory handed out by this arena. */
  bool contains(const void* p) const;
  /** True if `p` lies in the chunk the next allocation comes from (where the object allocated
   * last is). O(1), unlike contains(). */
  bool inHeadChunk(const void* p) const {
    auto* q = static_cast<const char*>(p);
    return head_ != nullptr && q >= data(head_) && q < cur_;
  }
  /** Calls f(begin, end) for the used part of every chunk. */
  template <class F>
  void forEachUsedRange(F f) const {
    bool first = true;
    for (Chunk* c = head_; c != nullptr; c = c->next) {
      char* b = data(c);
      f(b, first ? cur_ : b + c->size);  // only the head chunk is partly used
      first = false;
    }
  }

  /** Statistics over all arenas of the process (for diagnostics / benchmarks). */
  static std::size_t peakBytes();
  static std::size_t liveBytes();
  static std::size_t poolBytes();  // free pooled chunk bytes of this thread

 private:
  struct Chunk {
    Chunk* next;
    std::size_t size;  // usable bytes after the header
  };
  void refill(std::size_t n);
  static char* data(Chunk* c) { return reinterpret_cast<char*>(c) + 16; }

  Chunk* head_ = nullptr;  // current chunk first
  char* cur_ = nullptr;
  char* end_ = nullptr;
  std::size_t fullBytes_ = 0;  // used bytes in chunks other than head_
  static constexpr std::size_t kFreeClasses = 64;  // 16 .. 1024 bytes
  void* free_[kFreeClasses] = {};
  // Chunk data ranges sorted by address, for contains() in O(log n).
  std::vector<std::pair<const char*, const char*>> ranges_;

 public:
  /** Called for every chunk the arena takes (added) or gives back (removed). */
  using ChunkHook = void (*)(std::uintptr_t tag, const char* begin, const char* end, bool added);
  /** Installs `hook` (with `tag`) and reports the chunks the arena already has. */
  void setChunkHook(ChunkHook hook, std::uintptr_t tag) {
    chunkHook_ = hook;
    chunkTag_ = tag;
    for (auto& r : ranges_) hook(tag, r.first, r.second, true);
  }

 private:
  ChunkHook chunkHook_ = nullptr;
  std::uintptr_t chunkTag_ = 0;
};

/** The arena kSearch allocations go to on this thread (nullptr: GC heap). */
Arena*& current_search_arena();

/** Makes `arena` the current search arena for the lifetime of the scope (nullptr allowed). */
class ArenaScope {
 public:
  explicit ArenaScope(Arena* arena) : prev_(current_search_arena()) { current_search_arena() = arena; }
  ~ArenaScope() { current_search_arena() = prev_; }
  ArenaScope(const ArenaScope&) = delete;
  ArenaScope& operator=(const ArenaScope&) = delete;

 private:
  Arena* prev_;
};

inline void* search_alloc(std::size_t n) {
  Arena* a = current_search_arena();
  return a != nullptr ? a->allocate(n) : ::operator new(n);
}

/** The arena kBoardData allocations go to on this thread (nullptr: GC heap). Set while an
 * optimizer candidate works on its private worker board. */
Arena*& current_board_arena();

/** Makes `arena` the current board arena for the lifetime of the scope (nullptr allowed). */
class BoardArenaScope {
 public:
  explicit BoardArenaScope(Arena* arena) : prev_(current_board_arena()) { current_board_arena() = arena; }
  ~BoardArenaScope() { current_board_arena() = prev_; }
  BoardArenaScope(const BoardArenaScope&) = delete;
  BoardArenaScope& operator=(const BoardArenaScope&) = delete;

 private:
  Arena* prev_;
};

inline void* board_data_alloc(std::size_t n) {
  Arena* a = current_board_arena();
  return a != nullptr ? a->allocate(n) : ::operator new(n);
}

// ---------------------------------------------------------------------------------------------
// Geometry regions (MEMORY_MODEL.md stage E, "region + copy")
// ---------------------------------------------------------------------------------------------

class GeomRegion;

/** The innermost active geometry region of this thread whose arena kGeom allocations go to
 * (nullptr: the heap). */
GeomRegion*& current_geom_region();

/**
 * A scope in which immutable geometry (kGeom) is allocated in an arena that is dropped when the
 * scope ends. Whatever must outlive the scope is copied out first (deep copy into the domain of
 * its holder) - the escape rule. Regions nest; an inner region's objects must not be stored into
 * objects of an outer region or the heap either.
 *
 * Lazily filled caches of objects outside the region are filled in the domain of their holder
 * (jrt::DomainScope). Checked builds poison the memory on release; FR_REGION_CHECK=N (GC build)
 * scans the heap for pointers into every N-th released region and reports their holders.
 */
class GeomRegion {
 public:
  GeomRegion() = default;
  GeomRegion(const GeomRegion&) = delete;
  GeomRegion& operator=(const GeomRegion&) = delete;

  void* allocate(std::size_t n) { return arena_.allocate(n); }
  bool contains(const void* p) const { return arena_.contains(p); }
  /** Objects in `arena` belong to this region's domain too (their lazy caches and the geometry
   * they store are allocated here): e.g. the search arena of an autoroute engine. */
  void setCompanion(Arena* arena);
  /** Unique id of this region (never 0). */
  std::uint32_t id() const { return id_; }
  bool owns(const void* p) const { return contains(p) || (companion_ != nullptr && companion_->contains(p)); }
  bool inHeadChunk(const void* p) const { return arena_.inHeadChunk(p); }
  bool active() const { return active_; }
  bool local() const { return local_; }
  /** Operation regions also take kOp (per-operation temporaries), not only kGeom: everything
   * the operation allocates dies with it except what is copied out. Inner regions that do not
   * take kOp pass it to the nearest enclosing one that does. */
  void setTakesOps(bool v) { takesOps_ = v; }
  bool takesOps() const { return takesOps_; }
  GeomRegion* outer() const { return outer_; }
  /** The kGeom domain that was current when the region was activated (nullptr: the heap): where
   * results are copied out to. */
  GeomRegion* enclosing() const { return restore_; }
  const Arena& arena() const { return arena_; }
  Arena& arenaRef() { return arena_; }

  /** Drops every object of the region (it must not be active). FR_REGION_CHECK scans for
   * escapes first. */
  void release();
  /** Forgets the memory without reusing it (an exception left the region's operation midway). */
  void abandon();

 private:
  friend class GeomRegionScope;
  Arena arena_;
  const Arena* companion_ = nullptr;
  std::uint32_t id_ = next_id();
  static std::uint32_t next_id();
  GeomRegion* outer_ = nullptr;    // enclosing active region while active
  GeomRegion* restore_ = nullptr;  // current region to restore when deactivated
  bool active_ = false;
  bool takesOps_ = false;

 protected:
  bool local_ = false;  // a LocalGeomRegion
};

/** Makes `region` the innermost active region and the kGeom target for the lifetime of the
 * scope (nullptr: no effect). Activations nest. */
class GeomRegionScope {
 public:
  /** `local`: the region of a LocalGeomRegion. Diagnostics: env FR_NO_GEOM_REGION=1 disables
   * all regions, =2 only the non-local ones. */
  explicit GeomRegionScope(GeomRegion* region, bool local = false);
  ~GeomRegionScope();
  GeomRegionScope(const GeomRegionScope&) = delete;
  GeomRegionScope& operator=(const GeomRegionScope&) = delete;

 private:
  GeomRegion* region_;
};

/** A region that is active for exactly the lifetime of this local object. */
class LocalGeomRegion : public GeomRegion {
 public:
  LocalGeomRegion() {
    local_ = true;
    scope_.emplace(this, true);
  }
  ~LocalGeomRegion() {
    scope_.reset();
    release();
  }

 private:
  std::optional<GeomRegionScope> scope_;
};

/** Allocates kGeom in the domain of `owner`: the active region that contains it, else the heap.
 * Used where a long-lived object fills a lazy cache or stores geometry it computed. */
class DomainScope {
 public:
  explicit DomainScope(const void* owner);
  ~DomainScope() { current_geom_region() = saved_; }
  DomainScope(const DomainScope&) = delete;
  DomainScope& operator=(const DomainScope&) = delete;

 private:
  GeomRegion* saved_;
};

/** Allocates kGeom on the heap for the lifetime of the scope (statics, board-owned geometry). */
class HeapScope {
 public:
  HeapScope() : saved_(current_geom_region()) { current_geom_region() = nullptr; }
  ~HeapScope() { current_geom_region() = saved_; }
  HeapScope(const HeapScope&) = delete;
  HeapScope& operator=(const HeapScope&) = delete;

 private:
  GeomRegion* saved_;
};

/** Allocates kGeom in `target` (nullptr: the heap) for the lifetime of the scope; used to copy
 * results out of a region into its enclosing domain. */
class GeomTarget {
 public:
  explicit GeomTarget(GeomRegion* target) : saved_(current_geom_region()) { current_geom_region() = target; }
  ~GeomTarget() { current_geom_region() = saved_; }
  GeomTarget(const GeomTarget&) = delete;
  GeomTarget& operator=(const GeomTarget&) = delete;

 private:
  GeomRegion* saved_;
};

/** Returns f() with kGeom allocated on the heap: for function-local statics (singletons) that
 * may be first touched inside a region. */
template <class F>
auto onHeap(F f) {
  HeapScope heap;
  return f();
}

/** True if `p` lies in an active geometry region (diagnostics, copy-out decisions). */
bool in_geom_region(const void* p);
/** True if `p` lies in a region of this thread that has not been released yet, active or not
 * (copy-at-store: an inactive search region is still released later). */
bool in_live_geom_region(const void* p);
/** Id of the live region whose arena or companion arena holds `p` (0: none, i.e. heap or
 * another arena). */
std::uint32_t geom_domain_of(const void* p);
/** The active region with this id, or nullptr. */
GeomRegion* active_geom_region(std::uint32_t id);

/** kOp: the nearest region (from the current kGeom target outwards) that takes kOp, else the
 * heap. HeapScope / DomainScope(heap owner) therefore send kOp to the heap as well. */
inline void* op_alloc(std::size_t n) {
  for (GeomRegion* r = current_geom_region(); r != nullptr; r = r->outer()) {
    if (r->takesOps()) return r->allocate(n);
  }
  return ::operator new(n);
}

/** The arena (region, search or board arena) a just-constructed object `p` was allocated in, or
 * nullptr (heap, stack, static). Only valid in constructors: it checks the chunk the next
 * allocation comes from. */
inline Arena* domain_arena_of(const void* p) {
  for (GeomRegion* r = current_geom_region(); r != nullptr; r = r->outer()) {
    if (r->inHeadChunk(p)) return &r->arenaRef();
  }
  Arena* a = current_search_arena();
  if (a != nullptr && a->inHeadChunk(p)) return a;
  a = current_board_arena();
  if (a != nullptr && a->inHeadChunk(p)) return a;
  return nullptr;
}

/** std allocator for the containers inside jrt collections: storage comes from the arena that
 * holds the collection (dropped with it), else from the heap. */
template <class T>
struct DomainAlloc {
  using value_type = T;
  using is_always_equal = std::false_type;
  using propagate_on_container_move_assignment = std::true_type;
  Arena* arena = nullptr;
  DomainAlloc() noexcept = default;
  explicit DomainAlloc(Arena* a) noexcept : arena(a) {}
  template <class U>
  DomainAlloc(const DomainAlloc<U>& o) noexcept : arena(o.arena) {}
  T* allocate(std::size_t n) {
    std::size_t bytes = n * sizeof(T);
    return static_cast<T*>(arena != nullptr ? arena->allocateReusable(bytes) : ::operator new(bytes));
  }
  void deallocate(T* p, std::size_t n) noexcept {
    if (arena != nullptr) {
      arena->recycle(p, n * sizeof(T));
    } else {
      ::operator delete(p);
    }
  }
  template <class U>
  bool operator==(const DomainAlloc<U>& o) const noexcept { return arena == o.arena; }
};

/** Placement tag: allocate in the domain of `owner` (see domain_arena_of; constructors only). */
struct Like {
  const void* owner;
};

inline void* geom_alloc(std::size_t n) {
  GeomRegion* r = current_geom_region();
  return r != nullptr ? r->allocate(n) : ::operator new(n);
}

// ---------------------------------------------------------------------------------------------
// Explicit end of life (MEMORY_MODEL.md stage C)
// ---------------------------------------------------------------------------------------------

/** True if `mem` is the start of a separate GC heap block, i.e. it can be freed on its own (not
 * an arena object, not an object inside another allocation such as the points of
 * Line::ofInts, not static or stack memory). */
bool is_heap_block(const void* mem) noexcept;

/** Frees the block `mem` (destructor already run). Verification mode (env FR_DISPOSE_CHECK=1)
 * instead poisons it and reports at exit how many disposed objects were still reachable. */
void dispose_block(void* mem, const char* type) noexcept;

/** Disposed objects / bytes so far (diagnostics). */
std::size_t disposed_count() noexcept;
std::size_t disposed_bytes() noexcept;

/**
 * Ends the life of `p` now: its owner drops the only reference. Runs the destructor and returns
 * the memory to the heap. Objects that cannot be freed on their own (arena objects, parts of a
 * combined block, statics) are left alone, so callers need not know where `p` came from.
 *
 * Contract (checked by FR_DISPOSE_CHECK=1 runs): nothing reads `p` afterwards.
 */
template <class T>
inline void dispose(T* p) noexcept {
  if (p == nullptr) return;
  void* mem;
  if constexpr (std::is_polymorphic_v<T>) {
    mem = dynamic_cast<void*>(p);
  } else {
    mem = static_cast<void*>(p);
  }
  if (!is_heap_block(mem)) return;
  p->~T();
  dispose_block(mem, typeid(T).name());
}

/**
 * Scoped owner of an operation temporary (e.g. the result collection of a search-tree query that
 * the caller reads and drops): disposes it when the scope ends. `release()` gives up ownership
 * (the object escapes). Same contract as dispose: nothing may keep a reference.
 */
template <class T>
class DisposeOnExit {
 public:
  explicit DisposeOnExit(T* p) noexcept : p_(p) {}
  ~DisposeOnExit() { dispose(p_); }
  DisposeOnExit(const DisposeOnExit&) = delete;
  DisposeOnExit& operator=(const DisposeOnExit&) = delete;
  T* get() const noexcept { return p_; }
  T& operator*() const noexcept { return *p_; }
  T* operator->() const noexcept { return p_; }
  T* release() noexcept {
    T* p = p_;
    p_ = nullptr;
    return p;
  }
  /** Disposes the current object and takes `p` (Java: the variable is reassigned). */
  void reset(T* p) noexcept {
    if (p != p_) dispose(p_);
    p_ = p;
  }

 private:
  T* p_;
};

}  // namespace jrt

inline void* operator new(std::size_t n, jrt::BoardLifetime) { return ::operator new(n); }
inline void* operator new(std::size_t n, jrt::OpLifetime) { return jrt::op_alloc(n); }
inline void* operator new(std::size_t n, jrt::Like like) {
  jrt::Arena* a = jrt::domain_arena_of(like.owner);
  return a != nullptr ? a->allocate(n) : ::operator new(n);
}
inline void operator delete(void*, jrt::Like) noexcept {}
inline void* operator new(std::size_t n, jrt::GeomLifetime) { return jrt::geom_alloc(n); }
inline void* operator new(std::size_t n, jrt::SearchLifetime) { return jrt::search_alloc(n); }
inline void* operator new(std::size_t n, jrt::BoardDataLifetime) { return jrt::board_data_alloc(n); }
// Called only if a constructor throws. GC memory is reclaimed by the collector, arena memory by
// the arena, so nothing to do here.
inline void operator delete(void*, jrt::BoardLifetime) noexcept {}
inline void operator delete(void*, jrt::OpLifetime) noexcept {}
inline void operator delete(void*, jrt::GeomLifetime) noexcept {}
inline void operator delete(void*, jrt::SearchLifetime) noexcept {}
inline void operator delete(void*, jrt::BoardDataLifetime) noexcept {}
