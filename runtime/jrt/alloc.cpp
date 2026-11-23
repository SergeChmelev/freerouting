// jrt::Arena (see alloc.h). Chunks live in the GC heap (scanned conservatively); the per-thread
// pool of free chunks is anchored in uncollectable memory so that pooled chunks are not
// reclaimed by the collector.

#ifndef JRT_NO_GC
#define JRT_NO_GC 0
#endif
#if !JRT_NO_GC
#define GC_THREADS 1
#include <gc.h>
#include <gc/gc_mark.h>
#include <dlfcn.h>
#include <link.h>
#else
#include <malloc.h>
#endif

#include "jrt/alloc.h"
#include "jrt/threads.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#ifndef JRT_CHECKED
#define JRT_CHECKED 1
#endif

namespace jrt {

namespace {

constexpr std::size_t kHeader = 16;
constexpr std::size_t kChunkSize = std::size_t{1} << 20;   // usable bytes of a standard chunk
constexpr std::size_t kPoolLimit = std::size_t{128} << 20;  // free bytes kept per thread
// Checked builds poison released memory so that a use after release (an escape from the arena)
// crashes or shows up instead of silently reading the next operation's objects.
constexpr unsigned char kPoison = 0xA5;

struct Pool {
  void* head;  // free standard chunks, linked through their first word
  std::size_t bytes;
};

#if JRT_NO_GC
// Without a collector, dispose() must know arena memory by address: the live chunk ranges.
std::mutex g_chunkMutex;
std::map<std::uintptr_t, std::size_t>* g_chunks = nullptr;  // start -> size

void* chunk_alloc(std::size_t bytes) {
  void* c = alloc_uncollectable(bytes);
  std::lock_guard<std::mutex> lock(g_chunkMutex);
  if (g_chunks == nullptr) g_chunks = new std::map<std::uintptr_t, std::size_t>();
  (*g_chunks)[reinterpret_cast<std::uintptr_t>(c)] = bytes;
  return c;
}

void chunk_free(void* c) {
  {
    std::lock_guard<std::mutex> lock(g_chunkMutex);
    g_chunks->erase(reinterpret_cast<std::uintptr_t>(c));
  }
  free_uncollectable(c);
}

bool in_arena_chunk(const void* p) {
  auto a = reinterpret_cast<std::uintptr_t>(p);
  std::lock_guard<std::mutex> lock(g_chunkMutex);
  if (g_chunks == nullptr) return false;
  auto it = g_chunks->upper_bound(a);
  if (it == g_chunks->begin()) return false;
  --it;
  return a < it->first + it->second;
}
#else
// Large pointer-containing block: IGNORE_OFF_PAGE (the arena always keeps a pointer to the
// chunk start) so that the collector does not expand the heap looking for a 1 MB range free of
// blacklisted pages (that grew bm01 from 0.4 to 3 GB). Zero-filled, like Java objects.
void* chunk_alloc(std::size_t bytes) { return GC_MALLOC_IGNORE_OFF_PAGE(bytes); }
void chunk_free(void* c) { GC_FREE(c); }
#endif

Pool* thread_pool() {
  thread_local Pool* pool = nullptr;
  if (pool == nullptr) {
    pool = static_cast<Pool*>(alloc_uncollectable(sizeof(Pool)));
    pool->head = nullptr;
    pool->bytes = 0;
  }
  return pool;
}

std::atomic<std::size_t> g_live{0};
std::atomic<std::size_t> g_peak{0};

}  // namespace

Arena*& current_board_arena() {
  thread_local Arena* current = nullptr;
  return current;
}

Arena*& current_search_arena() {
  thread_local Arena* current = nullptr;
  return current;
}

void Arena::refill(std::size_t n) {
  if (head_ != nullptr) fullBytes_ += static_cast<std::size_t>(cur_ - data(head_));
  Chunk* c = nullptr;
  std::size_t size = std::max(n, kChunkSize);
  Pool* pool = thread_pool();
  if (size == kChunkSize && pool->head != nullptr) {
    c = static_cast<Chunk*>(pool->head);
    pool->head = *static_cast<void**>(pool->head);
    pool->bytes -= kChunkSize;
#if JRT_CHECKED
    std::memset(c, 0, kHeader + kChunkSize);
#endif
  } else {
    c = static_cast<Chunk*>(chunk_alloc(kHeader + size));
    if (c == nullptr) throw std::bad_alloc();
  }
  c->size = size;
  c->next = head_;
  head_ = c;
  cur_ = data(c);
  end_ = cur_ + size;
  std::size_t live = g_live.fetch_add(size, std::memory_order_relaxed) + size;
  std::size_t peak = g_peak.load(std::memory_order_relaxed);
  while (live > peak && !g_peak.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
  }
}

std::size_t Arena::bytesAllocated() const {
  return head_ == nullptr ? 0 : fullBytes_ + static_cast<std::size_t>(cur_ - data(head_));
}

void Arena::release() {
  Pool* pool = thread_pool();
  bool first = true;
  for (Chunk* c = head_; c != nullptr;) {
    Chunk* next = c->next;
    std::size_t size = c->size;
    // Only the head chunk is partially used; the others were filled up to (almost) their end.
    std::size_t used = first ? static_cast<std::size_t>(cur_ - data(c)) : size;
    first = false;
    g_live.fetch_sub(size, std::memory_order_relaxed);
    if (size == kChunkSize && pool->bytes + kChunkSize <= kPoolLimit) {
#if JRT_CHECKED
      std::memset(data(c), kPoison, used);
#else
      std::memset(data(c), 0, used);
#endif
      *reinterpret_cast<void**>(c) = pool->head;
      pool->head = c;
      pool->bytes += kChunkSize;
    } else {
#if JRT_CHECKED
      std::memset(data(c), kPoison, used);
#endif
      chunk_free(c);
    }
    c = next;
  }
  head_ = nullptr;
  cur_ = end_ = nullptr;
  fullBytes_ = 0;
}

void Arena::abandon() {
  for (Chunk* c = head_; c != nullptr; c = c->next) g_live.fetch_sub(c->size, std::memory_order_relaxed);
  head_ = nullptr;
  cur_ = end_ = nullptr;
  fullBytes_ = 0;
}

// ---------------------------------------------------------------------------------------------
// dispose (stage C)
// ---------------------------------------------------------------------------------------------

namespace {

std::atomic<std::size_t> g_disposedCount{0};
std::atomic<std::size_t> g_disposedBytes{0};

#if !JRT_NO_GC
// Verification mode: disposed blocks are poisoned but not freed (a read through a dangling
// pointer sees 0xA5 bytes, a virtual call crashes); a disappearing link per block tells whether
// it was still reachable after a full collection (a dangling reference, or a conservative false
// positive from a stale stack slot). Links live in non-scanned, never-moved blocks.
struct CheckEntry {
  void* link;  // cleared by the collector once the block is unreachable
  const char* type;
  std::size_t scan;  // number of periodic scans before this block was disposed
};
constexpr std::size_t kCheckBlock = 1 << 16;
struct CheckBlock {
  CheckBlock* next;
  std::size_t used;
  CheckEntry entries[kCheckBlock];
};
std::mutex g_checkMutex;
CheckBlock* g_checkBlocks = nullptr;
std::size_t g_checkTotal = 0;
std::size_t g_checkScans = 0;
std::map<std::string, std::size_t> g_checkSurvivors;
constexpr std::size_t kCheckScanEvery = std::size_t{1} << 20;

// --- "who points here": explains a survivor by the words that point into it ----------------
// Runs right after a full collection, with the allocation lock held (no allocation here).

struct Referrer {
  const void* block;   // referring heap block (nullptr: root)
  std::size_t size;    // its size
  std::size_t offset;  // offset of the pointer in it
  const char* where;   // "heap", "static", "stack"
};
constexpr int kMaxReferrers = 8;
struct ReferrerSearch {
  std::uintptr_t lo, hi;  // target range [lo, hi)
  Referrer found[kMaxReferrers];
  int count;
};

void add_referrer(ReferrerSearch* s, const void* block, std::size_t size, std::size_t off, const char* where) {
  if (block == nullptr && std::strcmp(where, "stack") == 0) {  // one stack hit is enough to say so
    for (int i = 0; i < s->count; ++i) {
      if (std::strcmp(s->found[i].where, "stack") == 0) return;
    }
  }
  if (s->count < kMaxReferrers) s->found[s->count++] = Referrer{block, size, off, where};
}

void scan_words(ReferrerSearch* s, const void* from, std::size_t bytes, const void* block, const char* where) {
  auto* w = static_cast<const std::uintptr_t*>(from);
  for (std::size_t i = 0; i < bytes / sizeof(std::uintptr_t); ++i) {
    if (w[i] >= s->lo && w[i] < s->hi) add_referrer(s, block, bytes, i * sizeof(std::uintptr_t), where);
  }
}

void GC_CALLBACK scan_heap_object(void* obj, std::size_t bytes, void* client) {
  auto* s = static_cast<ReferrerSearch*>(client);
  auto a = reinterpret_cast<std::uintptr_t>(obj);
  if (a >= s->lo && a < s->hi) return;  // the target itself
  scan_words(s, obj, bytes, obj, "heap");
}

int scan_static_segment(dl_phdr_info* info, std::size_t, void* client) {
  auto* s = static_cast<ReferrerSearch*>(client);
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    const ElfW(Phdr)& ph = info->dlpi_phdr[i];
    if (ph.p_type == PT_LOAD && (ph.p_flags & PF_W) != 0) {
      scan_words(s, reinterpret_cast<const void*>(info->dlpi_addr + ph.p_vaddr), ph.p_memsz, nullptr, "static");
    }
  }
  return 0;
}

void* GC_CALLBACK find_referrers_locked(void* client) {
  auto* s = static_cast<ReferrerSearch*>(client);
  GC_enumerate_reachable_objects_inner(scan_heap_object, s);
  dl_iterate_phdr(scan_static_segment, s);
  GC_stack_base sb;
  if (GC_get_stack_base(&sb) == GC_SUCCESS) {
    volatile std::uintptr_t here = 0;
    auto lo = reinterpret_cast<std::uintptr_t>(&here) & ~std::uintptr_t{7};
    auto hi = reinterpret_cast<std::uintptr_t>(sb.mem_base);
    if (hi > lo) scan_words(s, reinterpret_cast<const void*>(lo), hi - lo, nullptr, "stack");
  }
  return nullptr;
}

// Name of the dynamic type of a heap block (its first word as a vtable), or nullptr.
const char* block_type_name(const void* block) {
  if (block == nullptr) return nullptr;
  Dl_info info;
  void* vptr = *static_cast<void* const*>(block);
  if (dladdr(vptr, &info) != 0 && info.dli_sname != nullptr && std::strncmp(info.dli_sname, "_ZTV", 4) == 0) {
    return info.dli_sname + 4;
  }
  return nullptr;
}

std::string describe_referrer(const Referrer& r) {
  char buf[256];
  if (r.block == nullptr) {
    std::snprintf(buf, sizeof buf, "%s", r.where);
    return buf;
  }
  const char* t = block_type_name(r.block);
  if (t != nullptr) {
    std::snprintf(buf, sizeof buf, "heap block %p (%zu B, %s) at +%zu", r.block, r.size, t, r.offset);
    return buf;
  }
  // Symbols of the executable are not exported: print the first word as module offset, so
  // that `nm -C <binary>` can name a vtable (tools/mem/dispose_referrers.sh).
  Dl_info info;
  void* first = *static_cast<void* const*>(r.block);
  if (dladdr(first, &info) != 0 && info.dli_fbase != nullptr) {
    std::snprintf(buf, sizeof buf, "heap block %p (%zu B, word0=%s+%#lx) at +%zu", r.block, r.size,
                  info.dli_fname, static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(first) -
                                                             reinterpret_cast<std::uintptr_t>(info.dli_fbase)),
                  r.offset);
  } else {
    std::snprintf(buf, sizeof buf, "heap block %p (%zu B, no vtable) at +%zu", r.block, r.size, r.offset);
  }
  return buf;
}

// Prints up to three levels of referrers of `target` (a block without a vtable, e.g. a
// std::vector buffer, is explained by its own referrers). Returns true if some heap block or
// static data refers to it, false if only thread stacks / registers do.
// True if `block` is held by static data or, through at most four heap blocks, by a block that
// is itself held that way. Blocks reached only from a stack (a dead object seen through a stale
// stack slot, e.g. a dropped list that still contains the survivor) are not rooted. At the
// depth limit the answer is "rooted" (conservative).
bool is_rooted(void* block, int depth = 0) {
  if (depth >= 4) return true;
  ReferrerSearch s{};
  s.lo = reinterpret_cast<std::uintptr_t>(block);
  s.hi = s.lo + GC_size(block);
  GC_call_with_alloc_lock(find_referrers_locked, &s);
  if (s.count == kMaxReferrers) return true;  // too many to follow
  for (int i = 0; i < s.count; ++i) {
    if (std::strcmp(s.found[i].where, "static") == 0) return true;
    if (s.found[i].block != nullptr && is_rooted(const_cast<void*>(s.found[i].block), depth + 1)) return true;
  }
  return false;
}

bool explain_survivor(void* target, const char* type, bool print, int depth = 0) {
  if (depth == 0 && !is_rooted(target)) return false;
  ReferrerSearch s{};
  s.lo = reinterpret_cast<std::uintptr_t>(target);
  s.hi = s.lo + GC_size(target);
  GC_call_with_alloc_lock(find_referrers_locked, &s);
  if (print && depth == 0) std::fprintf(stderr, "dispose check: survivor %p (%s), referred by:\n", target, type);
  if (print && s.count == 0) {
    std::fprintf(stderr, "%*s(nothing found: registers or another thread's stack)\n", 2 * depth + 4, "");
  }
  for (int i = 0; i < s.count; ++i) {
    if (print) std::fprintf(stderr, "%*s%s\n", 2 * depth + 4, "", describe_referrer(s.found[i]).c_str());
    // expand every heap referrer (the same block once per level: it may hold several pointers)
    bool seen = false;
    for (int j = 0; j < i; ++j) seen = seen || s.found[j].block == s.found[i].block;
    if (print && s.found[i].block != nullptr && depth < 3 && !seen) {
      explain_survivor(const_cast<void*>(s.found[i].block), nullptr, true, depth + 1);
    }
  }
  return true;
}

int g_searched = 0;
int g_printed = 0;
constexpr int kMaxSearched = 200;  // referrer searches walk the whole heap
constexpr int kMaxPrinted = 20;
std::size_t g_checkStackOnly = 0;  // survivors referred to only by a thread stack

// Full collection, then every watched block disposed before the previous scan (so it has lived
// through at least one full collection since) that is still reachable counts as a survivor and
// is no longer watched. `all` also counts the blocks disposed since the previous scan (exit).
// A survivor that only a thread stack refers to (a stale slot in a live frame: the conservative
// collector cannot tell) is counted apart; one that heap or static data refers to is a dangling
// reference and is printed with its referrers.
void dispose_check_scan(bool all) {
  GC_gcollect();
  for (CheckBlock* b = g_checkBlocks; b != nullptr; b = b->next) {
    for (std::size_t i = 0; i < b->used; ++i) {
      CheckEntry& e = b->entries[i];
      if (e.link != nullptr && (all || e.scan < g_checkScans)) {
        void* target = reinterpret_cast<void*>(~reinterpret_cast<std::uintptr_t>(e.link));
        GC_unregister_disappearing_link(&e.link);
        e.link = nullptr;
        bool rooted = true;
        if (g_searched < kMaxSearched) {
          ++g_searched;
          rooted = explain_survivor(target, e.type, g_printed < kMaxPrinted);
          if (rooted) ++g_printed;
        }
        if (rooted) {
          ++g_checkSurvivors[e.type];
        } else {
          ++g_checkStackOnly;
        }
      }
    }
  }
  ++g_checkScans;
}

bool dispose_check_enabled() {
  static const bool on = [] {
    const char* v = std::getenv("FR_DISPOSE_CHECK");
    return v != nullptr && *v != '\0' && *v != '0';
  }();
  return on;
}

void dispose_check_report() {
  std::lock_guard<std::mutex> lock(g_checkMutex);
  dispose_check_scan(true);
  std::size_t survivors = 0;
  for (auto& [type, n] : g_checkSurvivors) survivors += n;
  std::fprintf(stderr,
               "dispose check: %zu disposed, %zu reachable after a full collection "
               "(+ %zu held only through a thread stack: conservative noise)\n",
               g_checkTotal, survivors, g_checkStackOnly);
  for (auto& [type, n] : g_checkSurvivors) std::fprintf(stderr, "  %8zu  %s\n", n, type.c_str());
}

void dispose_check_register(void* mem, const char* type) {
  std::lock_guard<std::mutex> lock(g_checkMutex);
  if (g_checkBlocks == nullptr || g_checkBlocks->used == kCheckBlock) {
    auto* b = static_cast<CheckBlock*>(GC_MALLOC_ATOMIC_UNCOLLECTABLE(sizeof(CheckBlock)));
    if (b == nullptr) return;
    if (g_checkBlocks == nullptr) std::atexit(dispose_check_report);
    b->next = g_checkBlocks;
    b->used = 0;
    g_checkBlocks = b;
  }
  CheckEntry& e = g_checkBlocks->entries[g_checkBlocks->used++];
  // Hidden (complemented) so that the referrer search does not find the link itself; the
  // collector only clears the slot, it does not read it.
  e.link = reinterpret_cast<void*>(~reinterpret_cast<std::uintptr_t>(mem));
  e.type = type;
  e.scan = g_checkScans;
  GC_general_register_disappearing_link(&e.link, mem);
  if (++g_checkTotal % kCheckScanEvery == 0) dispose_check_scan(false);
}

#else
bool dispose_check_enabled() { return false; }  // needs the collector
void dispose_check_register(void*, const char*) {}
#endif

}  // namespace

#if JRT_NO_GC
// Every pointer outside the arenas is taken to be a separate heap block: dispose() is only
// called on whole objects allocated with new (never on statics, stack values or the parts of a
// combined block such as the points of Line::ofInts).
bool is_heap_block(const void* mem) noexcept { return mem != nullptr && !in_arena_chunk(mem); }
static std::size_t block_size(void* mem) { return malloc_usable_size(mem); }
static void block_free(void* mem) { free_uncollectable(mem); }
#else
bool is_heap_block(const void* mem) noexcept { return GC_base(const_cast<void*>(mem)) == mem; }
static std::size_t block_size(void* mem) { return GC_size(mem); }
static void block_free(void* mem) { GC_FREE(mem); }
#endif

void dispose_block(void* mem, const char* type) noexcept {
  std::size_t size = block_size(mem);  // the whole block (may exceed the object's size)
  g_disposedCount.fetch_add(1, std::memory_order_relaxed);
  g_disposedBytes.fetch_add(size, std::memory_order_relaxed);
  // Through a volatile copy: with LTO, _FORTIFY_SOURCE knows the size of the `new` expression
  // and would reject poisoning the rounded-up block.
  void* volatile block = mem;
  if (dispose_check_enabled()) {
    std::memset(block, kPoison, size);
    dispose_check_register(mem, type);
    return;
  }
#if JRT_CHECKED
  std::memset(block, kPoison, size);
#endif
  block_free(mem);
}

std::size_t disposed_count() noexcept { return g_disposedCount.load(std::memory_order_relaxed); }
std::size_t disposed_bytes() noexcept { return g_disposedBytes.load(std::memory_order_relaxed); }

std::size_t Arena::peakBytes() { return g_peak.load(std::memory_order_relaxed); }
std::size_t Arena::liveBytes() { return g_live.load(std::memory_order_relaxed); }
std::size_t Arena::poolBytes() { return thread_pool()->bytes; }

}  // namespace jrt
