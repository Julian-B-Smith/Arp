// arp/arith.h - SPEC "Arithmetic conventions": mathematical floor, ceiling and modulo.
// C++ `/` and `%` truncate toward zero, so they are WRONG for a negative dividend (grid
// steps and positions go negative: pre-roll, Q-A5). Every division in the core goes
// through these; b > 0 throughout.
#pragma once

#include <cstdint>

namespace arp {

constexpr std::int64_t fdiv(std::int64_t a, std::int64_t b) {
  const std::int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
constexpr std::int64_t cdiv(std::int64_t a, std::int64_t b) {
  const std::int64_t q = a / b;
  return (a % b != 0 && ((a < 0) == (b < 0))) ? q + 1 : q;
}
// always in [0, b)
constexpr std::int64_t mod(std::int64_t a, std::int64_t b) { return a - b * fdiv(a, b); }

constexpr std::int64_t clampi(std::int64_t x, std::int64_t lo, std::int64_t hi) { return x < lo ? lo : x > hi ? hi : x; }

}  // namespace arp
