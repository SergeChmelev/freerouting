// jrt::Arena and jrt::dispose (see alloc.h). Arena chunks are plain heap blocks registered by
// address range (dispose leaves arena objects alone); a per-thread pool keeps free chunks.

#include "jrt/alloc.h"
#include "jrt/threads.h"
#include "platform/platform.h"

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
constexpr std::size_t kPoolLimit = std::size_t{128} << 20;  // free bytes kept by the job's thread
// Optimizer worker threads keep less: their pools add up (one per thread).
constexpr std::size_t kWorkerPoolLimit = std::size_t{16} << 20;
// Checked builds poison released memory so that a use after release (an escape from the arena)
// crashes or shows up instead of silently reading the next operation's objects.
constexpr unsigned char kPoison = 0xA5;

struct Pool {
  void* head;  // free standard chunks, linked through their first word
  std::size_t bytes;
  std::size_t limit;  // the most free bytes kept
};

// dispose() must know arena memory by address: the live chunk ranges.
std::mutex g_chunkMutex;
std::map<std::uintptr_t, std::size_t>* g_chunks = nullptr;  // start -> size

// Lock-free lookup (E13, E14): a table of 64 KB pages, written under g_chunkMutex, read with
// relaxed atomics. A page lying wholly inside a live chunk is arena memory; a page no chunk
// touches is not. A chunk is at least 1 MB, so on the page where a chunk ends no other chunk ends
// and on the page where one starts no other starts: an edge page keeps the end offset of the
// chunk that ends on it (it covers [page, page + lowEnd)) and the start offset of the one that
// starts on it ([page + highStart, page end)). Level 1 by address bits 47..32, level 2 by 31..16.
constexpr std::size_t kTablePageBits = 16;
constexpr std::uint32_t kTablePage = std::uint32_t{1} << kTablePageBits;
struct PageCounts {
  std::atomic<std::uint8_t> inner[std::size_t{1} << 16];      // chunks covering the page wholly (0/1)
  std::atomic<std::uint32_t> lowEnd[std::size_t{1} << 16];     // 0: no chunk ends on the page
  std::atomic<std::uint32_t> highStart[std::size_t{1} << 16];  // kTablePage: no chunk starts on it
};
std::atomic<PageCounts*> g_pageTable[std::size_t{1} << 16];

PageCounts* page_counts(std::uintptr_t a, bool create) {
  std::size_t hi = (a >> 32) & 0xffff;
  PageCounts* t = g_pageTable[hi].load(std::memory_order_acquire);
  if (t == nullptr && create) {
    t = static_cast<PageCounts*>(std::calloc(1, sizeof(PageCounts)));
    if (t == nullptr) throw std::bad_alloc();
    for (auto& s : t->highStart) s.store(kTablePage, std::memory_order_relaxed);
    g_pageTable[hi].store(t, std::memory_order_release);
  }
  return t;
}

// Marks (add) or unmarks a chunk [start, start + bytes); bytes >= 1 MB. Under g_chunkMutex.
void mark_chunk_pages(std::uintptr_t start, std::size_t bytes, bool add) {
  std::uintptr_t end = start + bytes;
  for (std::uintptr_t page = start >> kTablePageBits; page <= (end - 1) >> kTablePageBits; ++page) {
    std::uintptr_t lo = page << kTablePageBits, hi = lo + kTablePage;
    PageCounts* t = page_counts(lo, true);
    std::size_t i = (lo >> kTablePageBits) & 0xffff;
    if (start <= lo && hi <= end) {
      t->inner[i].store(add ? 1 : 0, std::memory_order_relaxed);
    } else if (start > lo) {  // the chunk starts on this page
      t->highStart[i].store(add ? static_cast<std::uint32_t>(start - lo) : kTablePage, std::memory_order_relaxed);
    } else {  // it ends on this page
      t->lowEnd[i].store(add ? static_cast<std::uint32_t>(end - lo) : 0, std::memory_order_relaxed);
    }
  }
}

void* chunk_alloc(std::size_t bytes) {
  void* c = heap_alloc(bytes);
  std::lock_guard<std::mutex> lock(g_chunkMutex);
  if (g_chunks == nullptr) g_chunks = new std::map<std::uintptr_t, std::size_t>();
  (*g_chunks)[reinterpret_cast<std::uintptr_t>(c)] = bytes;
  mark_chunk_pages(reinterpret_cast<std::uintptr_t>(c), bytes, true);
  return c;
}

void chunk_free(void* c) {
  {
    std::lock_guard<std::mutex> lock(g_chunkMutex);
    auto it = g_chunks->find(reinterpret_cast<std::uintptr_t>(c));
    if (it != g_chunks->end()) {
      mark_chunk_pages(it->first, it->second, false);
      g_chunks->erase(it);
    }
  }
  heap_free(c);
}

bool in_arena_chunk(const void* p) {
  auto a = reinterpret_cast<std::uintptr_t>(p);
  PageCounts* t = page_counts(a, false);
  if (t == nullptr) return false;
  std::size_t i = (a >> kTablePageBits) & 0xffff;
  if (t->inner[i].load(std::memory_order_relaxed) != 0) return true;
  auto off = static_cast<std::uint32_t>(a & (kTablePage - 1));
  return off < t->lowEnd[i].load(std::memory_order_relaxed) ||
         off >= t->highStart[i].load(std::memory_order_relaxed);
}

void chunk_free(void* c);

// Frees the pooled chunks of a thread's pool (they are a cache: at exit nothing needs them).
void drain_pool(Pool* pool) {
  for (void* c = pool->head; c != nullptr;) {
    void* next = *static_cast<void**>(c);
    JRT_ASAN_UNPOISON(c, sizeof(void*));
    chunk_free(c);
    c = next;
  }
  pool->head = nullptr;
  pool->bytes = 0;
}

Pool* g_mainPool = nullptr;

std::mutex g_mainPoolMutex;

Pool* thread_pool() {
  // The pool of another thread (an optimizer worker) is drained and freed when the thread ends.
  struct Holder {
    Pool* pool = nullptr;
    ~Holder() {
      if (pool == nullptr) return;
      {
        std::lock_guard<std::mutex> lock(g_mainPoolMutex);
        if (pool == g_mainPool) return;  // drained at exit (below)
      }
      drain_pool(pool);
      heap_free(pool);
    }
  };
  thread_local Holder holder;
  Pool*& pool = holder.pool;
  if (pool == nullptr) {
    pool = static_cast<Pool*>(heap_alloc(sizeof(Pool)));
    pool->head = nullptr;
    pool->bytes = 0;
    pool->limit = kWorkerPoolLimit;
    // PORT NOTE (stage E): the first pool (the main thread's) is drained at exit, so that the
    // leak accounting and LeakSanitizer do not see the cached chunks.
    std::lock_guard<std::mutex> lock(g_mainPoolMutex);
    if (g_mainPool == nullptr) {
      g_mainPool = pool;
      pool->limit = kPoolLimit;
      std::atexit([] { drain_pool(g_mainPool); });
    }
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
  // Diagnostics: FR_ARENA_WARN_MB=N prints the stack (addresses; addr2line) the first time one
  // arena grows past N MB, and every further N MB.
  static const std::size_t warnAt = [] {
    const char* v = std::getenv("FR_ARENA_WARN_MB");
    return v != nullptr ? static_cast<std::size_t>(std::atol(v)) << 20 : std::size_t{0};
  }();
  if (warnAt != 0 && fullBytes_ / warnAt != warnedSteps_) {
    warnedSteps_ = fullBytes_ / warnAt;
    std::fprintf(stderr, "jrt: arena %p holds %zu MB\n", static_cast<void*>(this), fullBytes_ >> 20);
    platform::print_stack_trace();
  }
  Chunk* c = nullptr;
  std::size_t size = std::max(n, kChunkSize);
  Pool* pool = thread_pool();
  if (size == kChunkSize && pool->head != nullptr) {
    c = static_cast<Chunk*>(pool->head);
    pool->head = *static_cast<void**>(pool->head);
    pool->bytes -= kChunkSize;
    JRT_ASAN_UNPOISON(c, kHeader + kChunkSize);  // pooled chunks are poisoned
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
  {
    std::pair<const char*, const char*> r{data(c), data(c) + size};
    ranges_.insert(std::upper_bound(ranges_.begin(), ranges_.end(), r), r);
  }
  if (chunkHook_ != nullptr) chunkHook_(chunkTag_, data(c), data(c) + size, true);
  cur_ = data(c);
  end_ = cur_ + size;
  JRT_ASAN_POISON(cur_, size);  // unpoisoned piece by piece in allocate()
  std::size_t live = g_live.fetch_add(size, std::memory_order_relaxed) + size;
  std::size_t peak = g_peak.load(std::memory_order_relaxed);
  while (live > peak && !g_peak.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
  }
}

std::size_t Arena::bytesAllocated() const {
  if (jobHeap_) return job_heap_live_bytes();
  return head_ == nullptr ? 0 : fullBytes_ + static_cast<std::size_t>(cur_ - data(head_));
}

bool Arena::contains(const void* p) const {
  if (jobHeap_) return job_heap_owns(p);
  // Whole chunk ranges (the unused tail of the head chunk counts too: nothing points there).
  auto* q = static_cast<const char*>(p);
  auto it = std::upper_bound(ranges_.begin(), ranges_.end(), std::pair<const char*, const char*>{q, nullptr},
                             [](const auto& a, const auto& b) { return a.first < b.first; });
  if (it == ranges_.begin()) return false;
  --it;
  return q >= it->first && q < it->second;
}

void Arena::release() {
  if (jobHeap_) {
    job_heap_release();
    return;
  }
  for (auto& f : free_) f = nullptr;
  Pool* pool = thread_pool();
  bool first = true;
  for (Chunk* c = head_; c != nullptr;) {
    Chunk* next = c->next;
    std::size_t size = c->size;
    // Only the head chunk is partially used; the others were filled up to (almost) their end.
    std::size_t used = first ? static_cast<std::size_t>(cur_ - data(c)) : size;
    first = false;
    g_live.fetch_sub(size, std::memory_order_relaxed);
    JRT_ASAN_UNPOISON(data(c), size);  // the unused tails are still poisoned
    if (size == kChunkSize && pool->bytes + kChunkSize <= pool->limit) {
#if JRT_CHECKED
      std::memset(data(c), kPoison, used);
#else
      std::memset(data(c), 0, used);
#endif
      JRT_ASAN_POISON(data(c), size);
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
  if (chunkHook_ != nullptr) {
    for (auto& r : ranges_) chunkHook_(chunkTag_, r.first, r.second, false);
  }
  ranges_.clear();
  cur_ = end_ = nullptr;
  fullBytes_ = 0;
}

void Arena::abandon() {
  for (auto& f : free_) f = nullptr;
  for (Chunk* c = head_; c != nullptr; c = c->next) g_live.fetch_sub(c->size, std::memory_order_relaxed);
  head_ = nullptr;
  if (chunkHook_ != nullptr) {
    for (auto& r : ranges_) chunkHook_(chunkTag_, r.first, r.second, false);
  }
  ranges_.clear();
  cur_ = end_ = nullptr;
  fullBytes_ = 0;
}

// ---------------------------------------------------------------------------------------------
// dispose (stage C)
// ---------------------------------------------------------------------------------------------

namespace {

std::atomic<std::size_t> g_disposedCount{0};
std::atomic<std::size_t> g_disposedBytes{0};

}  // namespace

// Every pointer outside the arenas is taken to be a separate heap block: dispose() is only
// called on whole objects allocated with new (never on statics, stack values or the parts of a
// combined block such as the points of Line::ofInts).
bool is_heap_block(const void* mem) noexcept { return mem != nullptr && !in_arena_chunk(mem); }
std::size_t heap_block_size(void* p);  // heap.cpp
static std::size_t block_size(void* mem) { return heap_block_size(mem); }
static void block_free(void* mem) { heap_free(mem); }

void dispose_block(void* mem, const char* type) noexcept {
  std::size_t size = block_size(mem);  // the whole block (may exceed the object's size)
  g_disposedCount.fetch_add(1, std::memory_order_relaxed);
  g_disposedBytes.fetch_add(size, std::memory_order_relaxed);
  // Through a volatile copy: with LTO, _FORTIFY_SOURCE knows the size of the `new` expression
  // and would reject poisoning the rounded-up block.
  void* volatile block = mem;
  (void)type;
#if JRT_CHECKED
  std::memset(block, kPoison, size);
#endif
  block_free(mem);
}

std::size_t disposed_count() noexcept { return g_disposedCount.load(std::memory_order_relaxed); }
std::size_t disposed_bytes() noexcept { return g_disposedBytes.load(std::memory_order_relaxed); }

void drain_arena_pool() { drain_pool(thread_pool()); }

std::size_t Arena::peakBytes() { return g_peak.load(std::memory_order_relaxed); }
std::size_t Arena::liveBytes() { return g_live.load(std::memory_order_relaxed); }
std::size_t Arena::poolBytes() { return thread_pool()->bytes; }

}  // namespace jrt
