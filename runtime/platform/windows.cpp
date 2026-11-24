// Windows (MSVCRT / UCRT, MinGW-w64) implementation of the platform interface
// (platform/platform.h).

#include "platform/windows.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <malloc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace jrt::platform {

namespace {

// Windows has no free() for _aligned_malloc blocks and no usable size for them without the
// alignment, so heap blocks carry a small header right before the user pointer: the start of the
// underlying malloc block and the requested size. Every heap block (aligned or not) has it, so
// heap_free and heap_usable_size need not know how a block was made.
struct Header {
  void* base;
  std::size_t size;
};
constexpr std::size_t kMinAlign = 16;  // alignof(std::max_align_t) with MinGW-w64 / MSVC x64
static_assert(sizeof(Header) <= kMinAlign);

void* alloc_with_header(std::size_t align, std::size_t n) {
  if (align < kMinAlign) align = kMinAlign;
  // room for the header before an aligned user pointer, wherever malloc puts the block
  std::size_t total = n + align + sizeof(Header);
  void* base = std::calloc(1, total);
  if (base == nullptr) return nullptr;
  auto first = reinterpret_cast<std::uintptr_t>(base) + sizeof(Header);
  auto user = (first + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
  auto* h = reinterpret_cast<Header*>(user - sizeof(Header));
  h->base = base;
  h->size = n;
  return reinterpret_cast<void*>(user);
}

inline Header* header_of(void* p) { return reinterpret_cast<Header*>(static_cast<char*>(p) - sizeof(Header)); }

}  // namespace

void* heap_zalloc(std::size_t n) { return alloc_with_header(kMinAlign, n); }

void* heap_aligned_zalloc(std::size_t align, std::size_t n) { return alloc_with_header(align, n); }

void heap_free(void* p) {
  if (p != nullptr) std::free(header_of(p)->base);
}

std::size_t heap_usable_size(void* p) { return header_of(p)->size; }

void* page_alloc(std::size_t align, std::size_t size) { return _aligned_malloc(size, align); }

void page_free(void* p) { _aligned_free(p); }

bool utc_time(std::time_t t, std::tm* out) { return gmtime_s(out, &t) == 0; }
bool local_time(std::time_t t, std::tm* out) { return localtime_s(out, &t) == 0; }

void limit_malloc_arenas(int) {}

void print_stack_trace() {
  void* frames[40];
  USHORT count = CaptureStackBackTrace(0, 40, frames, nullptr);
  for (USHORT i = 0; i < count; ++i) {
    std::uintptr_t base = module_base(frames[i]);
    char name[MAX_PATH] = "?";
    if (base != 0) GetModuleFileNameA(reinterpret_cast<HMODULE>(base), name, MAX_PATH);
    std::fprintf(stderr, "%s(+%#llx) [%p]\n", name,
                 static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(frames[i]) - base), frames[i]);
  }
}

std::uintptr_t module_base(const void* address) {
  HMODULE module = nullptr;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCSTR>(address), &module)) {
    return 0;
  }
  return reinterpret_cast<std::uintptr_t>(module);
}

}  // namespace jrt::platform
