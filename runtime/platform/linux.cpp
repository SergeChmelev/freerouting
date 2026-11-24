// Linux (glibc) implementation of the platform interface (platform/platform.h).

#include "platform/linux.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <malloc.h>

#include <cstdlib>
#include <cstring>

namespace jrt::platform {

void* heap_zalloc(std::size_t n) { return std::calloc(1, n); }

void* heap_aligned_zalloc(std::size_t align, std::size_t n) {
  std::size_t size = (n + align - 1) / align * align;  // aligned_alloc needs a multiple
  void* p = std::aligned_alloc(align, size);
  if (p != nullptr) std::memset(p, 0, size);
  return p;
}

void heap_free(void* p) { std::free(p); }

std::size_t heap_usable_size(void* p) { return malloc_usable_size(p); }

void* page_alloc(std::size_t align, std::size_t size) { return std::aligned_alloc(align, size); }

void page_free(void* p) { std::free(p); }

void limit_malloc_arenas(int arenas) {
  if (std::getenv("MALLOC_ARENA_MAX") == nullptr) mallopt(M_ARENA_MAX, arenas);
}

void print_stack_trace() {
  void* frames[40];
  int count = backtrace(frames, 40);
  backtrace_symbols_fd(frames, count, 2);
}

std::uintptr_t module_base(const void* address) {
  Dl_info info{};
  return dladdr(address, &info) != 0 ? reinterpret_cast<std::uintptr_t>(info.dli_fbase) : 0;
}

}  // namespace jrt::platform
