// Geometry regions (see jrt::GeomRegion in alloc.h, MEMORY_MODEL.md stage E "region + copy").

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


// Chunks of the regions of this thread that have not been released yet (by start address).
// Ranges, not region pointers: a region whose holder is dropped without release() must not
// leave a dangling entry; its chunks then merely cause needless copies. Keys and values are
// stored complemented (ordered by ~start, i.e. descending start address).
// Value: complemented end, region id (also for the chunks of a region's companion arena).
using ChunkMap = std::map<std::uintptr_t, std::pair<std::uintptr_t, std::uint32_t>>;
ChunkMap& live_chunks() {
  // Heap-allocated and never destroyed for the first thread (regions may be released during
  // exit); other threads (optimizer workers) free theirs when they end.
  static std::atomic<bool> firstTaken{false};
  struct Holder {
    ChunkMap* m = new ChunkMap();
    bool keep = !firstTaken.exchange(true);
    ~Holder() {
      if (!keep) delete m;
    }
  };
  thread_local Holder holder;
  return *holder.m;
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

void GeomRegion::registerChunks() { arena_.setChunkHook(region_chunk_hook, id_); }

void GeomRegion::release() {
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

}  // namespace jrt
