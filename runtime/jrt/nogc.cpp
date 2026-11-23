// Allocation backend without a garbage collector (MEMORY_MODEL.md stage E; build with
// -DFR_NO_GC=ON, which defines JRT_NO_GC=1).
//
// operator new is calloc (zero-filled, like GC_MALLOC and Java objects), delete / jrt::dispose
// is free. Nothing is collected: an object without an owner that frees it is leaked. The live
// byte count (FR_MEM_STATS) is therefore the size of what still has no owner.
#ifndef JRT_NO_GC
#define JRT_NO_GC 0
#endif
#if JRT_NO_GC

#include <malloc.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

#include "jrt/threads.h"

namespace {

std::atomic<std::size_t> g_liveBytes{0};
std::atomic<std::size_t> g_peakBytes{0};
std::atomic<std::size_t> g_allocCount{0};

inline void note_alloc(void* p) {
  std::size_t n = malloc_usable_size(p);
  std::size_t live = g_liveBytes.fetch_add(n, std::memory_order_relaxed) + n;
  std::size_t peak = g_peakBytes.load(std::memory_order_relaxed);
  while (live > peak && !g_peakBytes.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
  }
  g_allocCount.fetch_add(1, std::memory_order_relaxed);
}

inline void note_free(void* p) { g_liveBytes.fetch_sub(malloc_usable_size(p), std::memory_order_relaxed); }

void* raw_alloc(std::size_t n) {
  void* p = std::calloc(1, n == 0 ? 1 : n);
  if (p == nullptr) throw std::bad_alloc();
  note_alloc(p);
  return p;
}

void* raw_alloc_aligned(std::size_t n, std::size_t align) {
  if (align <= alignof(std::max_align_t)) return raw_alloc(n);
  std::size_t size = (n == 0 ? align : (n + align - 1) / align * align);
  void* p = std::aligned_alloc(align, size);
  if (p == nullptr) throw std::bad_alloc();
  std::memset(p, 0, size);
  note_alloc(p);
  return p;
}

void raw_free(void* p) noexcept {
  if (p == nullptr) return;
  note_free(p);
  std::free(p);
}

}  // namespace

void* operator new(std::size_t n) { return raw_alloc(n); }
void* operator new[](std::size_t n) { return raw_alloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  try {
    return raw_alloc(n);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  try {
    return raw_alloc(n);
  } catch (...) {
    return nullptr;
  }
}
void* operator new(std::size_t n, std::align_val_t a) { return raw_alloc_aligned(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return raw_alloc_aligned(n, static_cast<std::size_t>(a)); }

void operator delete(void* p) noexcept { raw_free(p); }
void operator delete[](void* p) noexcept { raw_free(p); }
void operator delete(void* p, std::size_t) noexcept { raw_free(p); }
void operator delete[](void* p, std::size_t) noexcept { raw_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { raw_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { raw_free(p); }
void operator delete(void* p, std::align_val_t) noexcept { raw_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { raw_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { raw_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { raw_free(p); }

namespace jrt {

void* alloc_uncollectable(std::size_t n) { return raw_alloc(n); }
void free_uncollectable(void* p) { raw_free(p); }
// Without a collector the "heap" is everything allocated and not yet freed.
std::size_t gc_heap_size() { return g_liveBytes.load(std::memory_order_relaxed); }
std::size_t gc_free_bytes() { return 0; }
std::size_t gc_count() { return 0; }
void gc_collect() {}

std::size_t nogc_peak_bytes() { return g_peakBytes.load(std::memory_order_relaxed); }
std::size_t nogc_alloc_count() { return g_allocCount.load(std::memory_order_relaxed); }

GcThreadScope::GcThreadScope() {}
GcThreadScope::~GcThreadScope() {}

}  // namespace jrt

#endif  // JRT_NO_GC
