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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <new>

#include "jrt/alloc.h"
#include "jrt/threads.h"

namespace {

std::atomic<std::size_t> g_liveBytes{0};
std::atomic<std::size_t> g_peakBytes{0};
std::atomic<std::size_t> g_allocCount{0};

inline void note_alloc(std::size_t n) {
  std::size_t live = g_liveBytes.fetch_add(n, std::memory_order_relaxed) + n;
  std::size_t peak = g_peakBytes.load(std::memory_order_relaxed);
  while (live > peak && !g_peakBytes.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
  }
  g_allocCount.fetch_add(1, std::memory_order_relaxed);
}

inline void note_free(std::size_t n) { g_liveBytes.fetch_sub(n, std::memory_order_relaxed); }

#if JRT_ALLOC_PROFILE
// Live-memory profile (-DJRT_ALLOC_PROFILE=1 with FR_NO_GC; tools/mem/live_profile.sh): every
// block carries a 32-byte header with its allocation site; frees subtract, so the dump at exit
// (env FR_ALLOC_PROFILE=<file>) lists what is still allocated, by site and live bytes.
struct Header {
  void* base;          // start of the underlying malloc block
  std::uint64_t size;  // requested size
  std::uint32_t slot;  // site slot + 1 (0: none)
  std::uint32_t magic;
};
constexpr std::size_t kHeader = 32;
constexpr std::uint32_t kMagic = 0x4A525431;  // "JRT1"
static_assert(sizeof(Header) <= kHeader);

struct SiteSlot {
  std::atomic<std::uintptr_t> addr{0};
  std::atomic<std::uintptr_t> caller{0};
  std::atomic<std::int64_t> count{0};
  std::atomic<std::int64_t> bytes{0};
};
constexpr std::size_t kSlots = 1u << 18;
SiteSlot* site_table() {
  static SiteSlot* t = static_cast<SiteSlot*>(std::calloc(kSlots, sizeof(SiteSlot)));
  return t;
}

// The caller N levels above the allocating function (frame pointers; FR_ALLOC_PROFILE_FP=N).
__attribute__((noinline)) std::uintptr_t caller_of_site() {
  static const int levels =
      std::getenv("FR_ALLOC_PROFILE_FP") != nullptr ? std::atoi(std::getenv("FR_ALLOC_PROFILE_FP")) : 0;
  if (levels <= 0) return 0;
  // frames: caller_of_site <- site_slot <- raw_alloc_site <- raw_alloc <- operator new
  //         <- allocating function <- its caller
  void** fp = static_cast<void**>(__builtin_frame_address(0));
  for (int i = 0; i < 4 + levels && fp != nullptr; ++i) fp = static_cast<void**>(fp[0]);
  return fp != nullptr ? reinterpret_cast<std::uintptr_t>(fp[1]) : 0;
}

__attribute__((noinline)) std::uint32_t site_slot(void* ra) {
  std::uintptr_t c = caller_of_site();
  auto a = reinterpret_cast<std::uintptr_t>(ra) ^ (c << 1);
  SiteSlot* t = site_table();
  std::size_t h = (a * 0x9E3779B97F4A7C15ull) >> 46;
  for (std::size_t i = 0; i < kSlots; ++i) {
    std::size_t k = (h + i) & (kSlots - 1);
    SiteSlot& s = t[k];
    std::uintptr_t cur = s.addr.load(std::memory_order_relaxed);
    if (cur == 0) {
      std::uintptr_t expected = 0;
      if (!s.addr.compare_exchange_strong(expected, a) && expected != a) continue;
      cur = a;
      s.caller.store(c, std::memory_order_relaxed);
    }
    if (cur == a) return static_cast<std::uint32_t>(k + 1);
  }
  return 0;
}

inline Header* header_of(void* p) { return reinterpret_cast<Header*>(static_cast<char*>(p) - kHeader); }

__attribute__((noinline)) void* raw_alloc_site(std::size_t n, std::size_t align, void* ra) {
  if (n == 0) n = 1;
  std::size_t off = align <= 16 ? kHeader : align;  // keeps the user pointer aligned
  void* base = align <= 16 ? std::calloc(1, n + off)
                           : std::aligned_alloc(align, (n + off + align - 1) / align * align);
  if (base == nullptr) throw std::bad_alloc();
  if (align > 16) std::memset(base, 0, n + off);
  void* p = static_cast<char*>(base) + off;
  Header* h = header_of(p);
  h->base = base;
  h->size = n;
  h->magic = kMagic;
  h->slot = ra != nullptr ? site_slot(ra) : 0;
  if (h->slot != 0) {
    SiteSlot& s = site_table()[h->slot - 1];
    s.count.fetch_add(1, std::memory_order_relaxed);
    s.bytes.fetch_add(static_cast<std::int64_t>(n), std::memory_order_relaxed);
  }
  note_alloc(n);
  return p;
}

// noinline: the return address seen here is in operator new; its caller is the site, so the
// site is taken one frame further up (operator new's return address).
__attribute__((noinline)) void* raw_alloc(std::size_t n) {
  void** fp = static_cast<void**>(__builtin_frame_address(1));  // operator new's frame
  return raw_alloc_site(n, 16, fp != nullptr ? fp[1] : nullptr);
}
__attribute__((noinline)) void* raw_alloc_aligned(std::size_t n, std::size_t align) {
  void** fp = static_cast<void**>(__builtin_frame_address(1));
  return raw_alloc_site(n, align, fp != nullptr ? fp[1] : nullptr);
}

std::size_t raw_size(void* p) {
  if (jrt::job_heap_owns(p)) return jrt::job_heap_block_size(p);
  return header_of(p)->size;
}

void raw_free(void* p) noexcept {
  if (p == nullptr) return;
  if (jrt::job_heap_owns(p)) {
    jrt::job_heap_free(p);
    return;
  }
  Header* h = header_of(p);
  if (h->magic != kMagic) std::abort();  // every block comes from raw_alloc_site
  if (h->slot != 0) {
    SiteSlot& s = site_table()[h->slot - 1];
    s.count.fetch_sub(1, std::memory_order_relaxed);
    s.bytes.fetch_sub(static_cast<std::int64_t>(h->size), std::memory_order_relaxed);
  }
  note_free(h->size);
  h->magic = 0;
  std::free(h->base);
}

struct LiveDump {
  ~LiveDump() {
    const char* path = std::getenv("FR_ALLOC_PROFILE");
    if (path == nullptr) return;
    FILE* f = std::fopen(path, "w");
    if (f == nullptr) return;
    SiteSlot* t = site_table();
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < kSlots; ++i)
      if (t[i].addr.load() != 0 && t[i].bytes.load() > 0) idx.push_back(i);
    std::sort(idx.begin(), idx.end(),
              [t](std::size_t x, std::size_t y) { return t[x].bytes.load() > t[y].bytes.load(); });
    for (std::size_t i : idx) {
      Dl_info info{};
      auto c = t[i].caller.load();
      auto a = t[i].addr.load() ^ (c << 1);
      std::uintptr_t base = dladdr(reinterpret_cast<void*>(a), &info) ? reinterpret_cast<std::uintptr_t>(info.dli_fbase) : 0;
      std::fprintf(f, "%#lx %ld %ld %#lx\n", static_cast<unsigned long>(a - base - 1),
                   static_cast<long>(t[i].count.load()), static_cast<long>(t[i].bytes.load()),
                   static_cast<unsigned long>(c != 0 ? c - base - 1 : 0));
    }
    std::fclose(f);
  }
} live_dump_instance;
#else
void* raw_alloc(std::size_t n) {
  void* p = std::calloc(1, n == 0 ? 1 : n);
  if (p == nullptr) throw std::bad_alloc();
  note_alloc(malloc_usable_size(p));
  return p;
}

void* raw_alloc_aligned(std::size_t n, std::size_t align) {
  if (align <= alignof(std::max_align_t)) return raw_alloc(n);
  std::size_t size = (n == 0 ? align : (n + align - 1) / align * align);
  void* p = std::aligned_alloc(align, size);
  if (p == nullptr) throw std::bad_alloc();
  std::memset(p, 0, size);
  note_alloc(malloc_usable_size(p));
  return p;
}

std::size_t raw_size(void* p) {
  if (jrt::job_heap_owns(p)) return jrt::job_heap_block_size(p);
  return malloc_usable_size(p);
}

void raw_free(void* p) noexcept {
  if (p == nullptr) return;
  if (jrt::job_heap_owns(p)) {
    jrt::job_heap_free(p);
    return;
  }
  note_free(malloc_usable_size(p));
  std::free(p);
}
#endif

}  // namespace

#if JRT_ALLOC_PROFILE
#define JRT_NOGC_NEW __attribute__((noinline))
#else
#define JRT_NOGC_NEW
#endif
JRT_NOGC_NEW void* operator new(std::size_t n) { return raw_alloc(n); }
JRT_NOGC_NEW void* operator new[](std::size_t n) { return raw_alloc(n); }
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

std::size_t nogc_block_size(void* p) { return raw_size(p); }
std::size_t nogc_peak_bytes() { return g_peakBytes.load(std::memory_order_relaxed); }
std::size_t nogc_alloc_count() { return g_allocCount.load(std::memory_order_relaxed); }

GcThreadScope::GcThreadScope() {}
GcThreadScope::~GcThreadScope() {}

}  // namespace jrt

#endif  // JRT_NO_GC
