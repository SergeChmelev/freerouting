// Linux (glibc) implementation of the platform interface (platform/platform.h).
// The same functions are declared in platform/windows.h; keep both in step.
#pragma once

#include <cstddef>
#include <cstdint>

namespace jrt::platform {

// ---- heap blocks of the port's allocator (runtime/jrt/heap.cpp) ----------------------------
/** A zero-filled block of at least n bytes (n > 0), aligned for any type; nullptr if out of
 * memory. Freed with heap_free. */
void* heap_zalloc(std::size_t n);
/** A zero-filled block of at least n bytes aligned to `align` (a power of two larger than
 * alignof(std::max_align_t)); nullptr if out of memory. Freed with heap_free. */
void* heap_aligned_zalloc(std::size_t align, std::size_t n);
/** Frees a block of heap_zalloc / heap_aligned_zalloc (nullptr: nothing). */
void heap_free(void* p);
/** The usable size of a heap_zalloc / heap_aligned_zalloc block (>= the requested size). */
std::size_t heap_usable_size(void* p);

// ---- pages of the job heap (runtime/jrt/jobheap.cpp) ----------------------------------------
/** `size` bytes (a multiple of `align`) aligned to `align`, not zeroed; nullptr if out of
 * memory. Freed with page_free. */
void* page_alloc(std::size_t align, std::size_t size);
void page_free(void* p);

// ---- process setup ---------------------------------------------------------------------------
/** Limits the number of malloc arenas (glibc gives every thread one, which keeps freed memory:
 * many optimizer threads doubled the peak). An explicit MALLOC_ARENA_MAX in the environment
 * wins. */
void limit_malloc_arenas(int arenas);

// ---- diagnostics -----------------------------------------------------------------------------
/** Prints the current call stack to stderr (addresses with module names; for addr2line). */
void print_stack_trace();
/** The load address of the module (executable or shared library) containing `address`, or 0. */
std::uintptr_t module_base(const void* address);

}  // namespace jrt::platform
