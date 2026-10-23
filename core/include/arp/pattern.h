// arp/pattern.h - step patterns as DATA (SPEC 4.3, 4.6): one step, one pattern, the user pattern's public shape.
#pragma once

#include <cstdint>

#include "arp/types.h"

namespace arp {

inline constexpr int kMaxPatternSteps = 64;   // SPEC 4.3: a pattern is 1..64 steps
inline constexpr int kMaxUserSteps = 16;      // SPEC 4.6: the user pattern is 1..16 steps

// `pick` (SPEC 4.3): which held note(s) the step plays. 0 = the order style's next note(s); 1..8 = the k-th lowest key,
// cyclic; then low, high, chord. Any other value is read as `order` (SPEC 4.6).
inline constexpr std::int32_t kPickOrder = 0;
inline constexpr std::int32_t kPickLow = 9;
inline constexpr std::int32_t kPickHigh = 10;
inline constexpr std::int32_t kPickChord = 11;

// One step. int32 fields so that an out-of-range value can be REPRESENTED by an adapter and then clamped by validation.
struct PatternStep {
  bool on = true;
  bool tie = false;
  std::int32_t vel = 100;      // 0..200 %
  std::int32_t gate = 100;     // 1..200 %
  std::int32_t step = 0;       // -8..8: a pitch offset of step * the pattern's step_size
  std::int32_t prob = 100;     // 0..100 %
  std::int32_t ratchet = 1;    // 1..4
  std::int32_t trig = 0;       // 0 = none, 1..8 = the trigger lane
  std::int32_t pick = kPickOrder;
};

// A bank pattern: constant data embedded from spec/bank/starter.json (core/include/arp/bank_data.h, generated).
struct PatternDef {
  std::int32_t length = 1;
  std::int32_t stepSize = 12;
  StepUnit stepUnit = StepUnit::Semitones;
  PatternStep steps[kMaxPatternSteps];
};

// The user's own pattern (SPEC 4.6): preset DATA, set off the audio thread by Core::setUserPattern.
struct UserPattern {
  std::int32_t length = 8;     // 1..16
  std::int32_t stepSize = 12;  // -48..48
  StepUnit stepUnit = StepUnit::Semitones;
  PatternStep steps[kMaxUserSteps];   // default: plain steps
};

// The bank is indexed by the `pattern` setting's enum value (evalStep reads kBank[(size_t)pattern]), so the generated id order
// MUST be the enum's name order, and the enum must have exactly one more value (`user`) after the bank. A pure function over the
// arrays so that the suite can feed it a PERTURBED copy and watch it say no (the static_assert in core.cpp feeds it the real ones).
constexpr bool streq(const char* a, const char* b) {
  while (*a != '\0' && *a == *b) { ++a; ++b; }
  return *a == *b;
}
constexpr bool bankOrderMatchesPatternEnum(const char* const* ids, int count, const char* const* patternNames, int userIndex) {
  if (count != userIndex) return false;
  for (int i = 0; i < count; ++i)
    if (!streq(ids[i], patternNames[i])) return false;
  return true;
}

}  // namespace arp
