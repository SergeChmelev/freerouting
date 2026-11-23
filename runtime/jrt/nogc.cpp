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

#if JRT_LEAK_PROFILE
// Diagnostic build only (-DJRT_LEAK_PROFILE=1 with FR_NO_GC): remembers the allocation site
// (return address of operator new, and with FR_ALLOC_PROFILE_FP=N the caller N levels up; needs
// -fno-omit-frame-pointer) of every live block and writes, at exit, the blocks still live (=
// leaked) per site to $FR_ALLOC_PROFILE as "offset count bytes caller" — the format of
// tools/mem/alloc_profile.sh, which resolves it.
}  // namespace
#include <dlfcn.h>
#include <sys/mman.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>
namespace {
struct LeakSite {
  std::uintptr_t addr, caller;
  std::uint64_t count, bytes;
};
struct LeakEntry {
  std::uintptr_t ptr;
  std::uint32_t site;
};
constexpr std::size_t kLeakSites = 1u << 18;
constexpr std::size_t kLeakTable = 1u << 27;  // live blocks
std::mutex g_leakMutex;
LeakSite* g_leakSites = nullptr;
LeakEntry* g_leakTable = nullptr;
bool g_leakInit = false;
int g_leakLevels = 0;
void* leak_map(std::size_t n) {
  void* p = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
}
inline std::size_t leak_hash(std::uintptr_t x) { return (x * 0x9E3779B97F4A7C15ull) >> (64 - 27); }
std::uint32_t leak_site(std::uintptr_t a, std::uintptr_t c) {
  std::uintptr_t key = a ^ (c << 1);
  std::size_t h = (key * 0x9E3779B97F4A7C15ull) >> 46;
  for (std::size_t i = 0; i < kLeakSites; ++i) {
    std::size_t s = (h + i) & (kLeakSites - 1);
    LeakSite& e = g_leakSites[s];
    if (e.addr == 0) {
      e.addr = a == 0 ? 1 : a;
      e.caller = c;
      return static_cast<std::uint32_t>(s);
    }
    if (e.addr == a && e.caller == c) return static_cast<std::uint32_t>(s);
  }
  return 0;
}
__attribute__((noinline)) void leak_note_alloc(void* p, std::size_t n, void* ra) {
  std::uintptr_t c = 0;
  if (g_leakLevels > 0) {
    // frames: leak_note_alloc <- operator new <- site function <- its caller
    void** fp = static_cast<void**>(__builtin_frame_address(0));
    for (int i = 0; i < 1 + g_leakLevels && fp != nullptr; ++i) fp = static_cast<void**>(fp[0]);
    c = fp != nullptr ? reinterpret_cast<std::uintptr_t>(fp[1]) : 0;
  }
  std::lock_guard<std::mutex> lock(g_leakMutex);
  if (!g_leakInit) {
    g_leakInit = true;
    g_leakSites = static_cast<LeakSite*>(leak_map(kLeakSites * sizeof(LeakSite)));
    g_leakTable = static_cast<LeakEntry*>(leak_map(kLeakTable * sizeof(LeakEntry)));
    const char* lv = std::getenv("FR_ALLOC_PROFILE_FP");
    g_leakLevels = lv != nullptr ? std::atoi(lv) : 0;
  }
  if (g_leakTable == nullptr) return;
  std::uint32_t site = leak_site(reinterpret_cast<std::uintptr_t>(ra), c);
  g_leakSites[site].count++;
  g_leakSites[site].bytes += n;
  auto key = reinterpret_cast<std::uintptr_t>(p);
  for (std::size_t i = leak_hash(key);; i = (i + 1) & (kLeakTable - 1)) {
    if (g_leakTable[i].ptr == 0) {
      g_leakTable[i] = {key, site};
      return;
    }
  }
}
void leak_note_free(void* p, std::size_t n) {
  std::lock_guard<std::mutex> lock(g_leakMutex);
  if (g_leakTable == nullptr) return;
  auto key = reinterpret_cast<std::uintptr_t>(p);
  std::size_t i = leak_hash(key);
  for (;; i = (i + 1) & (kLeakTable - 1)) {
    if (g_leakTable[i].ptr == 0) return;  // not recorded
    if (g_leakTable[i].ptr == key) break;
  }
  LeakSite& s = g_leakSites[g_leakTable[i].site];
  s.count--;
  s.bytes -= n;
  // backward-shift deletion (linear probing)
  std::size_t j = i;
  for (;;) {
    j = (j + 1) & (kLeakTable - 1);
    if (g_leakTable[j].ptr == 0) break;
    std::size_t k = leak_hash(g_leakTable[j].ptr);
    bool between = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
    if (between) continue;
    g_leakTable[i] = g_leakTable[j];
    i = j;
  }
  g_leakTable[i].ptr = 0;
}
struct LeakDump {
  ~LeakDump() {
    const char* path = std::getenv("FR_ALLOC_PROFILE");
    if (path == nullptr || g_leakSites == nullptr) return;
    FILE* f = std::fopen(path, "w");
    if (f == nullptr) return;
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < kLeakSites; ++i)
      if (g_leakSites[i].addr != 0 && g_leakSites[i].count != 0) idx.push_back(i);
    std::sort(idx.begin(), idx.end(), [](std::size_t x, std::size_t y) { return g_leakSites[x].bytes > g_leakSites[y].bytes; });
    for (std::size_t i : idx) {
      Dl_info info{};
      const LeakSite& s = g_leakSites[i];
      std::uintptr_t base = dladdr(reinterpret_cast<void*>(s.addr), &info) ? reinterpret_cast<std::uintptr_t>(info.dli_fbase) : 0;
      std::fprintf(f, "%#lx %lu %lu %#lx\n", static_cast<unsigned long>(s.addr - base - 1),
                   static_cast<unsigned long>(s.count), static_cast<unsigned long>(s.bytes),
                   static_cast<unsigned long>(s.caller != 0 ? s.caller - base - 1 : 0));
    }
    std::fclose(f);
  }
} leak_dump_instance;
#endif

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
#if JRT_LEAK_PROFILE
  leak_note_free(p, malloc_usable_size(p));
#endif
  note_free(p);
  std::free(p);
}

}  // namespace

#if JRT_LEAK_PROFILE
__attribute__((noinline)) void* operator new(std::size_t n) {
  void* p = raw_alloc(n);
  leak_note_alloc(p, malloc_usable_size(p), __builtin_return_address(0));
  return p;
}
__attribute__((noinline)) void* operator new[](std::size_t n) {
  void* p = raw_alloc(n);
  leak_note_alloc(p, malloc_usable_size(p), __builtin_return_address(0));
  return p;
}
#else
void* operator new(std::size_t n) { return raw_alloc(n); }
void* operator new[](std::size_t n) { return raw_alloc(n); }
#endif
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
