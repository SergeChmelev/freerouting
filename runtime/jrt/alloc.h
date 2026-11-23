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
#include <new>
#include <type_traits>
#include <typeinfo>

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
    return p;
  }

  /** Drops every object of the arena and returns its memory to the thread's chunk pool. */
  void release();

  /** Forgets the chunks without wiping them; the collector reclaims them once unreachable.
   * Used when objects may still be referenced (e.g. an exception left the operation midway). */
  void abandon();

  bool empty() const { return head_ == nullptr; }
  std::size_t bytesAllocated() const;

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

}  // namespace jrt

inline void* operator new(std::size_t n, jrt::BoardLifetime) { return ::operator new(n); }
inline void* operator new(std::size_t n, jrt::OpLifetime) { return ::operator new(n); }
inline void* operator new(std::size_t n, jrt::GeomLifetime) { return ::operator new(n); }
inline void* operator new(std::size_t n, jrt::SearchLifetime) { return jrt::search_alloc(n); }
inline void* operator new(std::size_t n, jrt::BoardDataLifetime) { return jrt::board_data_alloc(n); }
// Called only if a constructor throws. GC memory is reclaimed by the collector, arena memory by
// the arena, so nothing to do here.
inline void operator delete(void*, jrt::BoardLifetime) noexcept {}
inline void operator delete(void*, jrt::OpLifetime) noexcept {}
inline void operator delete(void*, jrt::GeomLifetime) noexcept {}
inline void operator delete(void*, jrt::SearchLifetime) noexcept {}
inline void operator delete(void*, jrt::BoardDataLifetime) noexcept {}
