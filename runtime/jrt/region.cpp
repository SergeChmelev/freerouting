// Geometry regions (see jrt::GeomRegion in alloc.h, MEMORY_MODEL.md stage E "region + copy").
#ifndef JRT_NO_GC
#define JRT_NO_GC 0
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "jrt/alloc.h"
#include "jrt/threads.h"

#if !JRT_NO_GC
#define GC_THREADS 1
#include <gc.h>
#include <gc/gc_mark.h>
#include <dlfcn.h>
#include <link.h>
#endif

namespace jrt {

GeomRegion*& current_geom_region() {
  thread_local GeomRegion* current = nullptr;
  return current;
}

namespace {

// Innermost region of this thread in lifetime order (the chain of outer_ links); differs from
// current_geom_region() inside a DomainScope / HeapScope.
GeomRegion*& innermost_region() {
  thread_local GeomRegion* innermost = nullptr;
  return innermost;
}

void region_escape_check(GeomRegion* region);

// Chunks of the regions of this thread that have not been released yet (by start address).
// Ranges, not region pointers: a region whose holder is dropped without release() (left to the
// collector) must not leave a dangling entry; its chunks then merely cause needless copies.
// Keys and values are stored complemented so that the map does not keep the chunks alive for the
// collector. Ordered by ~start, i.e. descending start address.
// The map object lives in uncollectable (scanned) memory: thread-local storage is not a root of
// the collector, so its nodes would otherwise be reclaimed.
// Value: complemented end, region id (also for the chunks of a region's companion arena).
using ChunkMap = std::map<std::uintptr_t, std::pair<std::uintptr_t, std::uint32_t>>;
ChunkMap& live_chunks() {
  thread_local ChunkMap* m = new_root<ChunkMap>();
  return *m;
}

void region_chunk_hook(std::uintptr_t tag, const char* begin, const char* end, bool added) {
  auto key = ~reinterpret_cast<std::uintptr_t>(begin);
  if (added) {
    live_chunks()[key] = {~reinterpret_cast<std::uintptr_t>(end), static_cast<std::uint32_t>(tag)};
  } else {
    live_chunks().erase(key);
  }
}

const ChunkMap::value_type* chunk_of(const void* p) {
  auto& m = live_chunks();
  auto q = reinterpret_cast<std::uintptr_t>(p);
  auto it = m.lower_bound(~q);  // the chunk with the greatest start <= p
  if (it == m.end() || q >= ~it->second.first) return nullptr;
  return &*it;
}

}  // namespace

GeomRegionScope::GeomRegionScope(GeomRegion* region, bool local) : region_(region) {
  static const int disabled = [] {
    const char* v = std::getenv("FR_NO_GEOM_REGION");
    return v != nullptr ? std::atoi(v) : 0;
  }();
  if (disabled == 1 || (disabled == 2 && !local)) region_ = nullptr;
  if (region_ == nullptr) return;
  if (!region_->job_) region_->arena_.setChunkHook(region_chunk_hook, region_->id_);
  region_->outer_ = innermost_region();
  region_->restore_ = current_geom_region();
  region_->active_ = true;
  innermost_region() = region_;
  current_geom_region() = region_;
}

GeomRegionScope::~GeomRegionScope() {
  if (region_ == nullptr) return;
  innermost_region() = region_->outer_;
  current_geom_region() = region_->restore_;
  region_->active_ = false;
  region_->outer_ = nullptr;
}

void GeomRegion::release() {
  region_escape_check(this);
  arena_.release();
}

void GeomRegion::abandon() { arena_.abandon(); }

std::uint32_t GeomRegion::next_id() {
  static std::atomic<std::uint32_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

void GeomRegion::setCompanion(Arena* arena) {
  companion_ = arena;
  arena->setChunkHook(region_chunk_hook, id_);
}

std::uint32_t geom_domain_of(const void* p) {
  const auto* c = chunk_of(p);
  return c != nullptr ? c->second.second : 0;
}

GeomRegion* active_geom_region(std::uint32_t id) {
  for (GeomRegion* r = innermost_region(); r != nullptr; r = r->outer()) {
    if (r->id() == id) return r;
  }
  return nullptr;
}

bool in_live_geom_region(const void* p) {
  // region chunks only: a companion arena holds no geometry, but be exact anyway
  const auto* c = chunk_of(p);
  if (c == nullptr) return false;
  for (GeomRegion* r = innermost_region(); r != nullptr; r = r->outer()) {
    if (r->id() == c->second.second) return r->contains(p);
  }
  return true;  // an inactive live region (or its companion): conservative
}

DomainScope::DomainScope(const void* owner) : saved_(current_geom_region()) {
  GeomRegion* domain = nullptr;
  for (GeomRegion* r = innermost_region(); r != nullptr; r = r->outer()) {
    if (r->owns(owner)) {
      domain = r;
      break;
    }
  }
  // A heap owner: its caches and stored geometry are the job's (freed by dispose, or with
  // the job), never a process-lifetime allocation (HeapScope / onHeap say so explicitly).
  current_geom_region() = domain != nullptr ? domain : job_region();
}

namespace {
GeomRegion*& job_region_slot() {
  thread_local GeomRegion* job = nullptr;
  return job;
}
bool mem_stats_enabled() {
  static const bool on = std::getenv("FR_MEM_STATS") != nullptr;
  return on;
}
std::atomic<std::size_t> g_jobDisposes{0}, g_jobDisposeBytes{0};
}  // namespace

GeomRegion* job_region() { return job_region_slot(); }

JobRegion::JobRegion() {
  if (innermost_region() != nullptr || job_region_slot() != nullptr) {
    std::fprintf(stderr, "jrt: a JobRegion must be the outermost region of its thread\n");
    std::abort();
  }
#if JRT_NO_GC
  // FR_NO_JOB_REGION=1: allocate as before (heap), e.g. for the live-memory profiler.
  static const bool disabled = std::getenv("FR_NO_JOB_REGION") != nullptr;
  if (disabled) return;
  setTakesOps(true);
  job_ = true;
  local_ = true;  // FR_NO_GEOM_REGION=2 keeps it; =1 disables it like every region
  arenaRef().setJobHeap();
  scope_.emplace(this, true);
  if (active()) {
    job_heap_begin();
    job_region_slot() = this;
  }
#endif
}

JobRegion::~JobRegion() {
  bool wasActive = active();
  scope_.reset();
  if (!wasActive) return;
  job_region_slot() = nullptr;
  arenaRef().release();  // the whole job heap; no escape check: nothing outlives the job
}

void note_job_region_dispose(const void* mem, std::size_t size) noexcept {
  if (!mem_stats_enabled()) return;
  GeomRegion* job = job_region_slot();
  if (job == nullptr || !job->contains(mem)) return;
  g_jobDisposes.fetch_add(1, std::memory_order_relaxed);
  g_jobDisposeBytes.fetch_add(size, std::memory_order_relaxed);
}
std::size_t job_region_disposes() noexcept { return g_jobDisposes.load(std::memory_order_relaxed); }
std::size_t job_region_dispose_bytes() noexcept { return g_jobDisposeBytes.load(std::memory_order_relaxed); }

bool in_geom_region(const void* p) {
  for (GeomRegion* r = innermost_region(); r != nullptr; r = r->outer()) {
    if (r->contains(p)) return true;
  }
  return false;
}

namespace {

#if !JRT_NO_GC
// FR_REGION_CHECK=N: every N-th released region, scan everything reachable outside it for words
// pointing into it (after a full collection, so only live holders count). Holders are reported
// at exit by the first word of their block (vtable: tools/mem/dispose_referrers.sh names it)
// and field offset.

int region_check_every() {
  static const int n = [] {
    const char* v = std::getenv("FR_REGION_CHECK");
    return v != nullptr ? std::atoi(v) : 0;
  }();
  return n;
}

constexpr int kMaxRanges = 256;
constexpr int kMaxHits = 512;
struct Hit {
  const void* block;  // holder block (nullptr: root)
  std::size_t offset;
  const char* where;
  void* word0;
};

}  // namespace
bool debug_is_rooted(void* block);  // alloc.cpp
namespace {
struct EscapeScan {
  std::uintptr_t lo[kMaxRanges], hi[kMaxRanges];
  int ranges;
  Hit hits[kMaxHits];
  int count;
};

bool in_ranges(const EscapeScan* s, std::uintptr_t w) {
  for (int i = 0; i < s->ranges; ++i) {
    if (w >= s->lo[i] && w < s->hi[i]) return true;
  }
  return false;
}

void scan_block(EscapeScan* s, const void* from, std::size_t bytes, const void* block, const char* where) {
  auto* w = static_cast<const std::uintptr_t*>(from);
  for (std::size_t i = 0; i < bytes / sizeof(std::uintptr_t); ++i) {
    if (in_ranges(s, w[i]) && s->count < kMaxHits) {
      void* word0 = block != nullptr ? *static_cast<void* const*>(block) : nullptr;
      // for roots the offset is the absolute address of the word
      std::size_t off = block != nullptr ? i * sizeof(std::uintptr_t)
                                         : reinterpret_cast<std::uintptr_t>(&w[i]);
      s->hits[s->count++] = Hit{block, off, where, word0};
    }
  }
}

void GC_CALLBACK scan_reachable(void* obj, std::size_t bytes, void* client) {
  auto* s = static_cast<EscapeScan*>(client);
  // a chunk of the region itself (its data starts a header after the block)
  auto lo = reinterpret_cast<std::uintptr_t>(obj), hi = lo + bytes;
  for (int i = 0; i < s->ranges; ++i) {
    if (s->lo[i] >= lo && s->lo[i] < hi) return;
  }
  scan_block(s, obj, bytes, obj, "heap");
}

int scan_statics(dl_phdr_info* info, std::size_t, void* client) {
  auto* s = static_cast<EscapeScan*>(client);
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    const ElfW(Phdr)& ph = info->dlpi_phdr[i];
    if (ph.p_type == PT_LOAD && (ph.p_flags & PF_W) != 0) {
      scan_block(s, reinterpret_cast<const void*>(info->dlpi_addr + ph.p_vaddr), ph.p_memsz, nullptr, "static");
    }
  }
  return 0;
}

void* GC_CALLBACK escape_scan_locked(void* client) {
  auto* s = static_cast<EscapeScan*>(client);
  GC_enumerate_reachable_objects_inner(scan_reachable, s);
  dl_iterate_phdr(scan_statics, s);
  return nullptr;
}

std::mutex g_escMutex;
std::map<std::string, std::size_t>* g_escapes = nullptr;  // holder description -> count
std::size_t g_regions = 0, g_checked = 0, g_dirty = 0;

// "exe+offset" of a word that points into a loaded module (a vtable), else nullptr.
bool module_offset(void* w, char* out, std::size_t n) {
  Dl_info info;
  if (dladdr(w, &info) == 0 || info.dli_fbase == nullptr) return false;
  std::snprintf(out, n, "%s+%#lx", info.dli_fname,
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(w) -
                                           reinterpret_cast<std::uintptr_t>(info.dli_fbase)));
  return true;
}

std::string describe_hit(const Hit& h) {
  char buf[512];
  if (h.block == nullptr) {
    char mod[256];
    if (module_offset(reinterpret_cast<void*>(h.offset), mod, sizeof mod)) {
      std::snprintf(buf, sizeof buf, "%s at %s", h.where, mod);  // nm: the static's symbol
    } else {
      std::snprintf(buf, sizeof buf, "%s", h.where);
    }
    return buf;
  }
  Dl_info info;
  if (dladdr(h.word0, &info) == 0) {
    // No vtable at the block start: an arena chunk (or a buffer). Guess the object around the
    // pointer by the nearest preceding word that points into a module (its vptr).
    auto* w = static_cast<void* const*>(h.block);
    char mod[256];
    for (std::size_t back = 0; back <= 64 && back * 8 <= h.offset; ++back) {
      void* cand = w[h.offset / 8 - back];
      if (module_offset(cand, mod, sizeof mod)) {
        std::snprintf(buf, sizeof buf, "arena/buffer object (word0=%s) at +%zu", mod, back * 8);
        return buf;
      }
    }
    // a buffer: name its holders instead (up to three levels: buffer <- container <- owner)
    std::string chain;
    const void* cur = h.block;
    for (int level = 0; level < 3 && cur != nullptr; ++level) {
      EscapeScan* up = static_cast<EscapeScan*>(std::calloc(1, sizeof(EscapeScan)));
      up->lo[0] = reinterpret_cast<std::uintptr_t>(cur);
      up->hi[0] = up->lo[0] + GC_size(const_cast<void*>(cur));
      up->ranges = 1;
      GC_call_with_alloc_lock(escape_scan_locked, up);
      const void* next = nullptr;
      std::string step = "?";
      for (int i = 0; i < up->count; ++i) {
        if (up->hits[i].block == nullptr) {
          step = up->hits[i].where;
          continue;
        }
        char t[300];
        if (module_offset(up->hits[i].word0, mod, sizeof mod)) {
          std::snprintf(t, sizeof t, "word0=%s at +%zu", mod, up->hits[i].offset);
        } else {
          std::snprintf(t, sizeof t, "buffer at +%zu", up->hits[i].offset);
        }
        step = t;
        next = up->hits[i].block;
        break;
      }
      std::free(up);
      chain += " <- " + step;
      cur = next;
    }
    std::snprintf(buf, sizeof buf, "buffer (%zu B)%s", GC_size(const_cast<void*>(h.block)), chain.c_str());
    return buf;
  }
  if (dladdr(h.word0, &info) != 0 && info.dli_fbase != nullptr) {
    std::snprintf(buf, sizeof buf, "heap block (word0=%s+%#lx) at +%zu", info.dli_fname,
                  static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(h.word0) -
                                             reinterpret_cast<std::uintptr_t>(info.dli_fbase)),
                  h.offset);
  } else {
    std::snprintf(buf, sizeof buf, "heap block without vtable (e.g. an arena chunk or a buffer) at +%zu", h.offset);
  }
  return buf;
}

void escape_report() {
  std::lock_guard<std::mutex> lock(g_escMutex);
  std::fprintf(stderr, "region check: %zu regions, %zu checked, %zu with escapes\n", g_regions, g_checked, g_dirty);
  if (g_escapes == nullptr) return;
  for (auto& [what, n] : *g_escapes) std::fprintf(stderr, "  %8zu  %s\n", n, what.c_str());
}

void region_escape_check(GeomRegion* region) {
  int every = region_check_every();
  if (every <= 0) return;
  std::lock_guard<std::mutex> lock(g_escMutex);
  if (g_regions++ == 0) std::atexit(escape_report);
  // every N-th local region, every long-lived one (search regions are few)
  if (region->arena().empty() || (region->local() && (g_regions % static_cast<std::size_t>(every)) != 0)) return;
  ++g_checked;
  // The region is still current: allocate nothing in it from here (the scan allocates nothing).
  auto* s = static_cast<EscapeScan*>(std::calloc(1, sizeof(EscapeScan)));
  region->arena().forEachUsedRange([&](const char* b, const char* e) {
    if (s->ranges < kMaxRanges) {
      s->lo[s->ranges] = reinterpret_cast<std::uintptr_t>(b);
      s->hi[s->ranges] = reinterpret_cast<std::uintptr_t>(e);
      ++s->ranges;
    }
  });
  GC_gcollect();
  GC_call_with_alloc_lock(escape_scan_locked, s);
  if (s->count > 0) {
    ++g_dirty;
    HeapScope heap;  // the map lives on the heap
    if (g_escapes == nullptr) g_escapes = new std::map<std::string, std::size_t>();
    int rootChecks = 0;
    for (int i = 0; i < s->count; ++i) {
      std::string what = describe_hit(s->hits[i]);
      // A holder that only a stack keeps is a dead object (conservative noise); checked for a
      // few hits per region (each check walks the heap).
      if (s->hits[i].block != nullptr && rootChecks < 8 && what.rfind("heap block (", 0) == 0) {
        ++rootChecks;
        if (!debug_is_rooted(const_cast<void*>(s->hits[i].block))) what = "[dead holder] " + what;
      }
      ++(*g_escapes)[what];
      static int printed = 0;
      if (printed < 300 && i < 30) {
        ++printed;
        std::fprintf(stderr, "region check (%s region %u): %s\n", region->local() ? "local" : "long-lived",
                     region->id(), what.c_str());
      }
    }
  }
  std::free(s);
}
#else
void region_escape_check(GeomRegion*) {}
#endif

}  // namespace

}  // namespace jrt
