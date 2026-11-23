// jrt::Arena (see alloc.h). Chunks live in the GC heap (scanned conservatively); the per-thread
// pool of free chunks is anchored in uncollectable memory so that pooled chunks are not
// reclaimed by the collector.

#define GC_THREADS 1
#include <gc.h>

#include "jrt/alloc.h"

#include <algorithm>
#include <atomic>
#include <cstring>

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
    c = static_cast<Chunk*>(GC_MALLOC(kHeader + size));  // zero-filled, like Java objects
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

std::size_t Arena::peakBytes() { return g_peak.load(std::memory_order_relaxed); }

}  // namespace jrt
