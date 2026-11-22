// Thread support for the garbage-collected port.
//
// Every thread that touches ported objects must be registered with the Boehm collector (it has
// to scan that thread's stack and stop it during collections). The thread that loads the
// runtime is registered by GC_INIT; any other thread wraps its body in a GcThreadScope.
//
// The routing core itself is single-threaded (static caches, fast_cast tables, identity-hash
// counter): at most ONE thread may run core code at a time. Other threads may only exchange
// plain data with it (see bindings/lua/engine.h).
#pragma once

#include <cstddef>
#include <new>
#include <utility>

namespace jrt {

class GcThreadScope {
 public:
  GcThreadScope();
  ~GcThreadScope();
  GcThreadScope(const GcThreadScope&) = delete;
  GcThreadScope& operator=(const GcThreadScope&) = delete;

 private:
  bool registered_ = false;
};

void* alloc_uncollectable(std::size_t n);
void free_uncollectable(void* p);

// Creates T in uncollectable (but scanned) memory: a GC root that stays alive until
// destroy_root(). Use for objects owned by foreign code (e.g. Lua userdata), whose memory the
// collector does not scan.
template <class T, class... Args>
T* new_root(Args&&... args) {
  void* mem = alloc_uncollectable(sizeof(T));
  return new (mem) T(std::forward<Args>(args)...);
}
template <class T>
void destroy_root(T* p) {
  if (!p) return;
  p->~T();
  free_uncollectable(p);
}

}  // namespace jrt
