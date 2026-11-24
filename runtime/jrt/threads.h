// Thread rules of the port.
//
// The routing core is single-threaded (static caches, fast_cast tables, identity-hash counter,
// the job heap): at most ONE thread may run core code at a time. Other threads may only exchange
// plain data with it (see bindings/lua/engine.h). The thread that runs core code owns a
// jrt::JobRegion for the duration of its work (alloc.h).
#pragma once

#include <cstddef>

namespace jrt {

/** Plain heap blocks outside every lifetime tag (runtime internals: arena chunks, exception
 * messages). */
void* heap_alloc(std::size_t n);
void heap_free(void* p);

}  // namespace jrt
