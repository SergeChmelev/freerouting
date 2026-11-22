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

void* operator new(std::size_t n) { return gc_alloc(n); }
void* operator new[](std::size_t n) { return gc_alloc(n); }
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
