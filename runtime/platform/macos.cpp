// macOS implementation of the platform interface (platform/platform.h). It shares the POSIX
// declarations of platform/linux.h; only the allocator queries differ from Linux (no malloc.h, no
// malloc arenas to limit).

#include "platform/linux.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <malloc/malloc.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

namespace jrt::platform {

void* heap_zalloc(std::size_t n) { return std::calloc(1, n); }

void* heap_aligned_zalloc(std::size_t align, std::size_t n) {
  void* p = nullptr;
  if (posix_memalign(&p, align, n) != 0) return nullptr;
  std::memset(p, 0, n);
  return p;
}

void heap_free(void* p) { std::free(p); }

std::size_t heap_usable_size(void* p) { return p != nullptr ? malloc_size(p) : 0; }

void* page_alloc(std::size_t align, std::size_t size) {
  void* p = nullptr;
  return posix_memalign(&p, align, size) == 0 ? p : nullptr;
}

void page_free(void* p) { std::free(p); }

bool utc_time(std::time_t t, std::tm* out) { return gmtime_r(&t, out) != nullptr; }
bool local_time(std::time_t t, std::tm* out) { return localtime_r(&t, out) != nullptr; }

// The macOS allocator has no per-thread arenas of glibc's kind: nothing to limit.
void limit_malloc_arenas(int) {}

void set_binary_stdio() {}

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
