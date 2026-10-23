// arp/draw.h - SPEC 10: the counter-based draw. No RNG state is carried between steps, so a
// draw is a pure function of (seed, x, lane, epoch): same inputs, same number, at any block
// size or sample rate. Lanes: 0 random, 1 shuffle, 2 prob. `x` is taken mod 2^32.
#pragma once

#include <cstdint>

namespace arp {

constexpr std::uint32_t mulberry32Once(std::uint32_t a) {
  a += 0x6D2B79F5u;
  std::uint32_t t = (a ^ (a >> 15)) * (1u | a);
  t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
  return t ^ (t >> 14);
}

constexpr std::uint32_t draw(std::uint32_t seed, std::int64_t x, std::uint32_t lane, std::uint32_t epoch) {
  return mulberry32Once(seed ^ (static_cast<std::uint32_t>(x) * 0x9E3779B1u) ^ (lane * 0x85EBCA77u) ^ (epoch * 0xC2B2AE3Du));
}

}  // namespace arp
