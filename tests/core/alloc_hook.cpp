#include "alloc_hook.h"

#include <cstdlib>
#include <new>

// A1's hook. Every global allocation form the C++ runtime can reach is replaced and counted while armed: plain,
// array, nothrow and over-aligned. NOT hookable portably: a direct malloc() / calloc() / mmap() or any system call.
// The core makes none (it includes only <algorithm>, <array>, <cmath>, <iterator>, <span>, <vector>), but that is by
// inspection, not by this hook.
namespace {
// Plain (non-atomic) globals: the suite is single-threaded.
bool g_armed = false;
std::int64_t g_count = 0;

void* counted(std::size_t n) noexcept {
  if (g_armed) ++g_count;
  return std::malloc(n ? n : 1);
}
void* countedAligned(std::size_t n, std::size_t align) noexcept {
  if (g_armed) ++g_count;
  void* p = nullptr;
  if (align < sizeof(void*)) align = sizeof(void*);
  if (::posix_memalign(&p, align, n ? n : 1) != 0) return nullptr;
  return p;
}
}  // namespace

namespace arp_test {
std::int64_t allocCount() { return g_count; }
void armAllocCount(bool on) { g_armed = on; }
}  // namespace arp_test

void* operator new(std::size_t n) { if (void* p = counted(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (void* p = counted(n)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return counted(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return counted(n); }
void* operator new(std::size_t n, std::align_val_t a) { if (void* p = countedAligned(n, static_cast<std::size_t>(a))) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n, std::align_val_t a) { if (void* p = countedAligned(n, static_cast<std::size_t>(a))) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return countedAligned(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return countedAligned(n, static_cast<std::size_t>(a)); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
