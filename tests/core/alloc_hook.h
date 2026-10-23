// alloc_hook.h - A1: counts calls to the global operator new while armed.
// The test binary replaces the global allocation functions (alloc_hook.cpp); a plain count of
// calls is enough because the core allocates only through the standard containers / new.
// SPEC-GAP: this sees the global operator new forms (plain, array, nothrow, over-aligned), not a direct malloc() or a system call;
// which is also enforced by it including nothing but <algorithm>, <cmath>, <array>, <vector>, <span>.
#pragma once

#include <cstdint>

namespace arp_test {

std::int64_t allocCount();   // total armed calls so far
void armAllocCount(bool on);

// Counts the allocations made while it lives.
struct AllocScope {
  AllocScope() : before_(allocCount()) { armAllocCount(true); }
  ~AllocScope() { armAllocCount(false); }
  std::int64_t count() const { return allocCount() - before_; }
 private:
  std::int64_t before_;
};

}  // namespace arp_test
