// Thread rules of the port.
//
// One thread runs a routing job (the CLI's main thread, the Lua engine thread) and owns its
// jrt::JobRegion (alloc.h). Other threads may only exchange plain data with it (see
// bindings/lua/engine.h), with one exception: the optimizer's worker threads
// (BatchOptimizer::optRoutePass with optimizer.max_threads > 1). They run candidates on private
// board copies in their own arenas and regions, and the runtime pieces they share are
// thread-safe: fast_cast caches and identity hashes (lock-free), the job heap (locked while a
// jrt::JobHeapSharedScope exists), Java ThreadLocals (jrt::perThread), Java `synchronized`
// (mutexes: BasicBoard::copyLock, FRLogger::mutex, ...). Everything else of the core is used by
// one thread at a time.
#pragma once

#include <cstddef>

namespace jrt {

/** Plain heap blocks outside every lifetime tag (runtime internals: arena chunks, exception
 * messages). */
void* heap_alloc(std::size_t n);
void heap_free(void* p);

}  // namespace jrt
