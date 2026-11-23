// jrt::Arena (see alloc.h). Chunks live in the GC heap (scanned conservatively); the per-thread
// pool of free chunks is anchored in uncollectable memory so that pooled chunks are not
// reclaimed by the collector.

#define GC_THREADS 1
#include <gc.h>

#include "jrt/alloc.h"

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

Pool* thread_pool() {
  thread_local Pool* pool = nullptr;
  if (pool == nullptr) {
    pool = static_cast<Pool*>(GC_MALLOC_UNCOLLECTABLE(sizeof(Pool)));
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
    // Large pointer-containing block: IGNORE_OFF_PAGE (the arena always keeps a pointer to the
    // chunk start) so that the collector does not expand the heap looking for a 1 MB range free
    // of blacklisted pages (that grew bm01 from 0.4 to 3 GB). Zero-filled, like Java objects.
    c = static_cast<Chunk*>(GC_MALLOC_IGNORE_OFF_PAGE(kHeader + size));
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
      GC_FREE(c);
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

// Full collection, then every watched block disposed before the previous scan (so it has lived
// through at least one full collection since) that is still reachable counts as a survivor and
// is no longer watched. `all` also counts the blocks disposed since the previous scan (exit).
void dispose_check_scan(bool all) {
  GC_gcollect();
  for (CheckBlock* b = g_checkBlocks; b != nullptr; b = b->next) {
    for (std::size_t i = 0; i < b->used; ++i) {
      CheckEntry& e = b->entries[i];
      if (e.link != nullptr && (all || e.scan < g_checkScans)) {
        ++g_checkSurvivors[e.type];
        GC_unregister_disappearing_link(&e.link);
        e.link = nullptr;
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
  std::fprintf(stderr, "dispose check: %zu disposed, %zu reachable after a full collection\n", g_checkTotal,
               survivors);
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
  e.link = mem;
  e.type = type;
  e.scan = g_checkScans;
  GC_general_register_disappearing_link(&e.link, mem);
  if (++g_checkTotal % kCheckScanEvery == 0) dispose_check_scan(false);
}

}  // namespace

bool is_heap_block(const void* mem) noexcept { return GC_base(const_cast<void*>(mem)) == mem; }

void dispose_block(void* mem, const char* type) noexcept {
  std::size_t size = GC_size(mem);  // the whole block (may exceed the object's size)
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
  GC_FREE(mem);
}

std::size_t disposed_count() noexcept { return g_disposedCount.load(std::memory_order_relaxed); }
std::size_t disposed_bytes() noexcept { return g_disposedBytes.load(std::memory_order_relaxed); }

std::size_t Arena::peakBytes() { return g_peak.load(std::memory_order_relaxed); }
std::size_t Arena::liveBytes() { return g_live.load(std::memory_order_relaxed); }
std::size_t Arena::poolBytes() { return thread_pool()->bytes; }

}  // namespace jrt
