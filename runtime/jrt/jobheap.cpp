// The job heap (MEMORY_MODEL.md stage E, "job region"): the memory of jrt::JobRegion.
//
// Geometry and per-operation temporaries that no operation region takes and no owner frees
// belong to the routing job. They are allocated here instead of with operator new: a
// size-class allocator on 64 KB pages, so jrt::dispose / operator delete still free single
// objects at once (and the memory is reused), and the end of the job frees everything that is
// left. A page table over the address space tells in O(1) whether a pointer is a job-heap block,
// so operator delete, jrt::dispose and the domain lookups recognise them.
//
// One job heap per process at a time, used by one thread (the routing core is single-threaded,
// see threads.h); other threads only ask job_heap_owns() for their own blocks (false).

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <unordered_map>
#include <vector>

#include "jrt/alloc.h"

namespace jrt {

namespace {

constexpr std::size_t kPageBits = 16;
constexpr std::size_t kPage = std::size_t{1} << kPageBits;  // 64 KB
constexpr std::uint8_t kLargePage = 255;

// Size classes: 16..1024 in steps of 16 (64 classes), then 4 per octave up to 32 KB (20).
constexpr int kSmallClasses = 64;
constexpr std::size_t kMaxSmall = 32768;
std::size_t g_classSize[96];
int g_classCount = 0;
std::uint8_t g_classOfQuarterKb[kMaxSmall / 256 + 1];  // sizes above 1024 by ceil(n / 256)

void init_classes() {
  if (g_classCount != 0) return;
  for (int i = 0; i < kSmallClasses; ++i) g_classSize[g_classCount++] = static_cast<std::size_t>(i + 1) * 16;
  for (std::size_t octave = 1024; octave < kMaxSmall; octave *= 2) {
    for (int q = 1; q <= 4; ++q) g_classSize[g_classCount++] = octave + octave / 4 * static_cast<std::size_t>(q);
  }
  int c = kSmallClasses;
  for (std::size_t k = 0; k <= kMaxSmall / 256; ++k) {
    std::size_t n = k * 256;
    while (c < g_classCount - 1 && g_classSize[c] < n) ++c;
    g_classOfQuarterKb[k] = static_cast<std::uint8_t>(c);
  }
}

inline int class_of(std::size_t n) {
  if (n <= 1024) return n == 0 ? 0 : static_cast<int>((n + 15) / 16) - 1;
  return g_classOfQuarterKb[(n + 255) / 256];
}

// Page table: level 1 by address bits 47..32, level 2 (one byte per 64 KB page) by bits 31..16.
// Entry 0: not a job page; 1 + class: a page of that class; kLargePage: part of a large block.
constexpr std::size_t kL1 = std::size_t{1} << 16;
constexpr std::size_t kL2 = std::size_t{1} << 16;
std::atomic<std::uint8_t*> g_table[kL1];

inline std::uint8_t page_entry(const void* p) {
  auto a = reinterpret_cast<std::uintptr_t>(p);
  std::size_t i1 = (a >> 32) & (kL1 - 1);
  std::uint8_t* l2 = g_table[i1].load(std::memory_order_acquire);
  return l2 == nullptr ? 0 : l2[(a >> kPageBits) & (kL2 - 1)];
}

void set_page_entry(const void* page, std::uint8_t v) {
  auto a = reinterpret_cast<std::uintptr_t>(page);
  std::size_t i1 = (a >> 32) & (kL1 - 1);
  std::uint8_t* l2 = g_table[i1].load(std::memory_order_acquire);
  if (l2 == nullptr) {
    l2 = static_cast<std::uint8_t*>(std::calloc(kL2, 1));  // the table is never freed
    if (l2 == nullptr) throw std::bad_alloc();
    g_table[i1].store(l2, std::memory_order_release);
  }
  l2[(a >> kPageBits) & (kL2 - 1)] = v;
}

struct JobHeap {
  void* freeList[96] = {};
  char* bumpCur[96] = {};
  char* bumpEnd[96] = {};
  std::vector<void*> pages;                      // every small page
  std::unordered_map<void*, std::size_t> large;  // large block -> rounded size
  std::size_t live = 0, peak = 0, pageBytes = 0, largeBytes = 0;
  bool active = false;
};

JobHeap& heap() {
  static JobHeap* h = new JobHeap();  // never destroyed: blocks may be freed after exit starts
  return *h;
}

inline void note(JobHeap& h, std::ptrdiff_t n) {
  h.live = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(h.live) + n);
  if (h.live > h.peak) h.peak = h.live;
}

void* alloc_page() {
  void* p = std::aligned_alloc(kPage, kPage);
  if (p == nullptr) throw std::bad_alloc();
  std::memset(p, 0, kPage);
  return p;
}

}  // namespace

void job_heap_begin() {
  init_classes();
  JobHeap& h = heap();
  if (h.active) {
    std::fprintf(stderr, "jrt: only one job heap at a time\n");
    std::abort();
  }
  h.active = true;
}

void* job_heap_alloc(std::size_t n) {
  JobHeap& h = heap();
  if (n > kMaxSmall) {
    std::size_t size = (n + kPage - 1) & ~(kPage - 1);
    void* p = std::aligned_alloc(kPage, size);
    if (p == nullptr) throw std::bad_alloc();
    std::memset(p, 0, size);
    for (std::size_t off = 0; off < size; off += kPage) set_page_entry(static_cast<char*>(p) + off, kLargePage);
    h.large.emplace(p, size);
    h.largeBytes += size;
    note(h, static_cast<std::ptrdiff_t>(size));
    return p;
  }
  int c = class_of(n);
  std::size_t size = g_classSize[c];
  if (void* p = h.freeList[c]) {
    JRT_ASAN_UNPOISON(p, size);
    h.freeList[c] = *static_cast<void**>(p);
    *static_cast<void**>(p) = nullptr;  // the rest was zeroed when it was freed
    note(h, static_cast<std::ptrdiff_t>(size));
    return p;
  }
  if (static_cast<std::size_t>(h.bumpEnd[c] - h.bumpCur[c]) < size) {
    char* page = static_cast<char*>(alloc_page());
    set_page_entry(page, static_cast<std::uint8_t>(1 + c));
    h.pages.push_back(page);
    h.pageBytes += kPage;
    h.bumpCur[c] = page;
    h.bumpEnd[c] = page + kPage / size * size;
  }
  void* p = h.bumpCur[c];
  h.bumpCur[c] += size;
  note(h, static_cast<std::ptrdiff_t>(size));
  return p;
}

bool job_heap_owns(const void* p) noexcept { return page_entry(p) != 0; }

std::size_t job_heap_block_size(const void* p) noexcept {
  std::uint8_t e = page_entry(p);
  if (e == 0) return 0;
  if (e != kLargePage) return g_classSize[e - 1];
  auto it = heap().large.find(const_cast<void*>(p));
  return it != heap().large.end() ? it->second : 0;
}

void job_heap_free(void* p) noexcept {
  std::uint8_t e = page_entry(p);
  if (e == 0) return;
  JobHeap& h = heap();
  if (e == kLargePage) {
    auto it = h.large.find(p);
    if (it == h.large.end()) std::abort();  // not the start of a large block
    std::size_t size = it->second;
    for (std::size_t off = 0; off < size; off += kPage) set_page_entry(static_cast<char*>(p) + off, 0);
    h.large.erase(it);
    h.largeBytes -= size;
    note(h, -static_cast<std::ptrdiff_t>(size));
    std::free(p);
    return;
  }
  int c = e - 1;
  std::size_t size = g_classSize[c];
  // Zeroed: allocations are zero-filled, and stale pointers must not keep anything reachable
  // for LeakSanitizer.
  std::memset(p, 0, size);
  *static_cast<void**>(p) = h.freeList[c];
  h.freeList[c] = p;
  JRT_ASAN_POISON(static_cast<char*>(p) + sizeof(void*), size - sizeof(void*));
  note(h, -static_cast<std::ptrdiff_t>(size));
}

void job_heap_release() noexcept {
  JobHeap& h = heap();
  for (void* page : h.pages) {
    set_page_entry(page, 0);
    JRT_ASAN_UNPOISON(page, kPage);
    std::free(page);
  }
  h.pages.clear();
  h.pages.shrink_to_fit();
  for (auto& [p, size] : h.large) {
    for (std::size_t off = 0; off < size; off += kPage) set_page_entry(static_cast<char*>(p) + off, 0);
    std::free(p);
  }
  h.large.clear();
  for (int c = 0; c < 96; ++c) h.freeList[c] = h.bumpCur[c] = h.bumpEnd[c] = nullptr;
  h.live = h.pageBytes = h.largeBytes = 0;
  h.active = false;
}

std::size_t job_heap_live_bytes() noexcept { return heap().live; }
std::size_t job_heap_peak_bytes() noexcept { return heap().peak; }
std::size_t job_heap_reserved_bytes() noexcept { return heap().pageBytes + heap().largeBytes; }

}  // namespace jrt
