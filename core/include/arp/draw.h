// arp/draw.h - SPEC 10: the counter-based draw. No RNG state is carried between steps, so a
// draw is a pure function of (seed, x, lane, epoch): same inputs, same number, at any block
// size or sample rate. Lanes: 0 random, 1 shuffle, 2 prob, 3 velocity randomness, 4 length
// randomness (D-036). `x` is taken mod 2^32.
#pragma once

#include <cstdint>

namespace arp {

constexpr std::uint32_t mulberry32Once(std::uint32_t a) {
  a += 0x6D2B79F5u;
  std::uint32_t t = (a ^ (a >> 15)) * (1u | a);
  t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
  return t ^ (t >> 14);
}

// The three multipliers of SPEC 10, and the draw split in two: everything but `x` (constant for a step) and the `x` term. The note randomness (D-036) draws twice per
// note per sub-note, so the step computes the first part once and the second per note in a loop the compiler can vectorise; draw() below is the two put back together.
inline constexpr std::uint32_t kDrawX = 0x9E3779B1u, kDrawLane = 0x85EBCA77u, kDrawEpoch = 0xC2B2AE3Du;

constexpr std::uint32_t drawBase(std::uint32_t seed, std::uint32_t lane, std::uint32_t epoch) {
  return seed ^ (lane * kDrawLane) ^ (epoch * kDrawEpoch);
}
constexpr std::uint32_t drawWith(std::uint32_t base, std::uint32_t xTerm) { return mulberry32Once(base ^ xTerm); }   // xTerm = (uint32_t)x * kDrawX

constexpr std::uint32_t draw(std::uint32_t seed, std::int64_t x, std::uint32_t lane, std::uint32_t epoch) {
  return drawWith(drawBase(seed, lane, epoch), static_cast<std::uint32_t>(x) * kDrawX);
}

}  // namespace arp
