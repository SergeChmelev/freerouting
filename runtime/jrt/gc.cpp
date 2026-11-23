// Routes all C++ heap allocation through the Boehm conservative garbage collector.
//
// The port keeps Java reference semantics: objects are created with `new` and referenced
// through raw pointers, and nothing is ever deleted explicitly. Every allocation made with
// operator new (ported objects, std::vector / std::string buffers, std::function state, ...)
// lives in the collected heap and is reclaimed once it is unreachable from the stacks,
// static data or other collected memory. Explicit `delete` (e.g. by std containers that
// reallocate) frees immediately.
//
// Caveat: memory obtained from plain malloc() is NOT scanned. Never store the only pointer
// to a collected object in malloc'ed memory. In particular C++ exception objects are
// malloc'ed by the C++ runtime, so jrt exceptions keep their message in uncollectable
// storage (see jrt::Throwable).

#define GC_THREADS 1
#include <gc.h>
#include <gmp.h>

#include "jrt/threads.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace {

void* gc_alloc(std::size_t n) {
  void* p = GC_MALLOC(n == 0 ? 1 : n);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}

void* gc_alloc_aligned(std::size_t n, std::size_t align) {
  if (align <= 16) return gc_alloc(n);
  void* p = GC_memalign(align, n == 0 ? 1 : n);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}

void* gmp_gc_alloc(std::size_t n) { return GC_MALLOC_ATOMIC(n); }
void* gmp_gc_realloc(void* p, std::size_t, std::size_t n) { return GC_REALLOC(p, n); }
void gmp_gc_free(void* p, std::size_t) { GC_FREE(p); }

struct GcInit {
  GcInit() {
    GC_INIT();
    // Worker threads (e.g. the Lua binding's engine thread) register themselves explicitly.
    GC_allow_register_threads();
    // Start with a reasonably large heap to avoid frequent early collections.
    GC_expand_hp(256u * 1024u * 1024u);
    mp_set_memory_functions(gmp_gc_alloc, gmp_gc_realloc, gmp_gc_free);
  }
};

// Initialised before main(); operator new also works before this runs because
// GC_MALLOC initialises the collector lazily.
[[maybe_unused]] GcInit gc_init_instance;

}  // namespace

#if JRT_ALLOC_PROFILE
// Diagnostic build only (-DJRT_ALLOC_PROFILE=1): counts allocations per call site (return
// address of operator new) and prints the top sites as "offset count bytes" at exit; resolve
// with `addr2line -f -C -i -e <binary> <offset>` (tools/mem/alloc_profile.sh).
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>
namespace {
struct SiteSlot {
  std::atomic<std::uintptr_t> addr{0};
  std::atomic<std::uintptr_t> caller{0};
  std::atomic<std::uint64_t> count{0};
  std::atomic<std::uint64_t> bytes{0};
};
constexpr std::size_t kSlots = 1u << 18;
SiteSlot* site_table() {
  static SiteSlot* t = static_cast<SiteSlot*>(std::calloc(kSlots, sizeof(SiteSlot)));
  return t;
}
// Return address of the function that called the allocating function (needs frame pointers:
// build with -fno-omit-frame-pointer; 0 otherwise / when unknown).
__attribute__((noinline)) std::uintptr_t caller_of_site() {
  // FR_ALLOC_PROFILE_FP=N: record the caller N levels above the allocating function (1: its caller)
  static const int levels = std::getenv("FR_ALLOC_PROFILE_FP") != nullptr ? std::atoi(std::getenv("FR_ALLOC_PROFILE_FP")) : 0;
  static const bool enabled = levels > 0;
  if (!enabled) return 0;
  // frames: caller_of_site <- record_site <- operator new <- site function <- its caller
  void** fp = static_cast<void**>(__builtin_frame_address(0));
  for (int i = 0; i < 2 + levels && fp != nullptr; ++i) fp = static_cast<void**>(fp[0]);
  return fp != nullptr ? reinterpret_cast<std::uintptr_t>(fp[1]) : 0;
}
__attribute__((noinline)) void record_site(void* ra, std::size_t n) {
  std::uintptr_t c = caller_of_site();
  auto a = reinterpret_cast<std::uintptr_t>(ra) ^ (c << 1);
  SiteSlot* t = site_table();
  std::size_t h = (a * 0x9E3779B97F4A7C15ull) >> 46;
  for (std::size_t i = 0; i < kSlots; ++i) {
    SiteSlot& s = t[(h + i) & (kSlots - 1)];
    std::uintptr_t cur = s.addr.load(std::memory_order_relaxed);
    if (cur == 0) {
      std::uintptr_t expected = 0;
      if (!s.addr.compare_exchange_strong(expected, a) && expected != a) continue;
      cur = a;
      s.caller.store(c, std::memory_order_relaxed);
    }
    if (cur == a) {
      s.count.fetch_add(1, std::memory_order_relaxed);
      s.bytes.fetch_add(n, std::memory_order_relaxed);
      return;
    }
  }
}
struct SiteDump {
  ~SiteDump() {
    const char* path = std::getenv("FR_ALLOC_PROFILE");
    if (path == nullptr) return;
    FILE* f = std::fopen(path, "w");
    if (f == nullptr) return;
    SiteSlot* t = site_table();
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < kSlots; ++i)
      if (t[i].addr.load() != 0) idx.push_back(i);
    std::sort(idx.begin(), idx.end(), [t](std::size_t x, std::size_t y) { return t[x].count.load() > t[y].count.load(); });
    for (std::size_t i : idx) {
      Dl_info info{};
      auto c = t[i].caller.load();
      auto a = t[i].addr.load() ^ (c << 1);
      std::uintptr_t base = dladdr(reinterpret_cast<void*>(a), &info) ? reinterpret_cast<std::uintptr_t>(info.dli_fbase) : 0;
      std::fprintf(f, "%#lx %lu %lu %#lx\n", static_cast<unsigned long>(a - base - 1),
                   static_cast<unsigned long>(t[i].count.load()), static_cast<unsigned long>(t[i].bytes.load()),
                   static_cast<unsigned long>(c != 0 ? c - base - 1 : 0));
    }
    std::fclose(f);
  }
} site_dump_instance;
}  // namespace
__attribute__((noinline)) void* operator new(std::size_t n) {
  record_site(__builtin_return_address(0), n);
  return gc_alloc(n);
}
__attribute__((noinline)) void* operator new[](std::size_t n) {
  record_site(__builtin_return_address(0), n);
  return gc_alloc(n);
}
#else
void* operator new(std::size_t n) { return gc_alloc(n); }
void* operator new[](std::size_t n) { return gc_alloc(n); }
#endif
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return GC_MALLOC(n == 0 ? 1 : n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return GC_MALLOC(n == 0 ? 1 : n); }
void* operator new(std::size_t n, std::align_val_t a) { return gc_alloc_aligned(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return gc_alloc_aligned(n, static_cast<std::size_t>(a)); }

void operator delete(void* p) noexcept { GC_FREE(p); }
void operator delete[](void* p) noexcept { GC_FREE(p); }
void operator delete(void* p, std::size_t) noexcept { GC_FREE(p); }
void operator delete[](void* p, std::size_t) noexcept { GC_FREE(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { GC_FREE(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { GC_FREE(p); }
void operator delete(void* p, std::align_val_t) noexcept { GC_FREE(p); }
void operator delete[](void* p, std::align_val_t) noexcept { GC_FREE(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { GC_FREE(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { GC_FREE(p); }

namespace jrt {

void* alloc_uncollectable(std::size_t n) { return GC_MALLOC_UNCOLLECTABLE(n == 0 ? 1 : n); }
void free_uncollectable(void* p) { GC_FREE(p); }
std::size_t gc_heap_size() { return GC_get_heap_size(); }
std::size_t gc_free_bytes() { return GC_get_free_bytes(); }
std::size_t gc_count() { return GC_get_gc_no(); }

GcThreadScope::GcThreadScope() {
  GC_stack_base sb;
  if (GC_get_stack_base(&sb) == GC_SUCCESS) {
    int r = GC_register_my_thread(&sb);
    registered_ = (r == GC_SUCCESS);  // GC_DUPLICATE: already known (e.g. the main thread)
  }
}

GcThreadScope::~GcThreadScope() {
  if (registered_) GC_unregister_my_thread();
}
void gc_collect() { GC_gcollect(); }

}  // namespace jrt
