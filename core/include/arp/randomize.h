// arp/randomize.h - SPEC 15 (D-044, D-048): Arp's own randomize, for horde's Random panel.
//
// One pure function. It takes the current settings, a home (the bell's centre) and horde's draw key (seed, click, corner), chaos,
// amount, locks and "must change", and returns new settings. Integers only: no float, no libm, no allocation, no locks, no state,
// bounded work. The same inputs give the same result on every platform (RZ1). The core never calls it: horde calls it on its
// message thread and hands the result to the core as an ordinary settings snapshot, so SPEC 9's timing and SPEC 7's terminals
// apply unchanged (T22). Every table it reads is data from spec/randomize.json (randomize_data.h, generated).
// The design and its reviews: docs/design/randomize.md.
#pragma once

#include <cstdint>

#include "arp/types.h"

namespace arp::rnd {

enum class Verb : std::uint8_t { Structure, Values, Both };   // horde's ARCH, PARAMS, randomize patch
enum class Class : std::uint8_t { Structure, Value, Never };

// One id per spec/params.json key, in params.json order, append-only. Locks and the changed mask index it.
enum class ParamId : std::uint8_t {
  Enabled, ArpEnabled, Order, Pattern, Rate, RateMode, StepTimeUs, Steps, StepSize, StepUnit, Gate, Swing, Latch, VelMode, VelFixed,
  VelRandom, GateRandom, RandomCurve, Restart, Repeats, Quantize, ScaleSource, ScaleRoot,
  ScaleDeg0, ScaleDeg1, ScaleDeg2, ScaleDeg3, ScaleDeg4, ScaleDeg5, ScaleDeg6, ScaleDeg7, ScaleDeg8, ScaleDeg9, ScaleDeg10, ScaleDeg11,
  Seed, ChordEnabled, ChordUnit,
  ChordOn1, ChordShift1, ChordOn2, ChordShift2, ChordOn3, ChordShift3, ChordOn4, ChordShift4, ChordOn5, ChordShift5, ChordOn6, ChordShift6,
  Count
};
inline constexpr int kParamCount = static_cast<int>(ParamId::Count);
static_assert(kParamCount == 50, "spec/params.json holds 50 settings");

struct LockSet {
  std::uint64_t bits = 0;
  constexpr void set(ParamId p, bool on = true) { const std::uint64_t b = std::uint64_t{1} << static_cast<int>(p); bits = on ? (bits | b) : (bits & ~b); }
  constexpr bool test(ParamId p) const { return (bits >> static_cast<int>(p)) & 1u; }
};

#ifdef ARP_PLANTS
// TEST-ONLY fault switches (SPEC 11 "planted control"), one per RZ detector; compiled in only by the test targets, as core.h's Plants.
struct Plants {
  bool carriedStream = false;        // RZ1/RZ2: one mulberry32 stream advanced per draw, ignoring the key (Sluice-style)
  bool pointerInSeed = false;        // RZ1: an address enters the draw
  bool shiftOutOfRange = false;      // RZ3: the chord table's +24 is drawn as +25
  bool noOwnShapeRule = false;       // RZ3: a steps above 3 is span-filtered too, emptying the size table
  bool ownShapeRedraw = false;       // RZ12: a steps above 3 draws a new size from the whole table (an unbounded span)
  bool seedAlwaysRead = false;       // RZ9: the seed is drawn even when nothing reads it
  bool curveAlwaysLive = false;      // RZ9: random_curve is drawn with both randomness amounts at 0
  bool centreNotCandidate = false;   // RZ3: the bell's centre is not added when it lies outside the band
  bool locksValuesOnly = false;      // RZ4: Structure ignores locks
  bool drawLatch = false;            // RZ5: latch is drawn by Structure
  bool mustChangeAtChaos0 = false;   // RZ6: must change fires at chaos 0
  bool forcedAtChaos0 = false;       // RZ6: a group's forced redraws run at chaos 0
  bool livenessFromOut = false;      // RZ7: liveness reads the keys just drawn, not cur (revision 1's rule)
  bool pickDependsOnAmount = false;  // RZ7: the value pick mixes in the amount
  bool wrongClass = false;           // RZ8: swing is drawn by Structure instead of Values
  bool stepTimeInSync = false;       // RZ9: step_time_us is drawn in sync mode
  bool ignoreArpEnabled = false;     // RZ9: the arp's keys are drawn with the arp off
  bool ignoreChaos = false;          // RZ10: the bell is flat at every chaos above 0
  bool reverseSigma = false;         // RZ10: the sigma table is read backwards
  bool linearRoot = false;           // RZ10: the circle of fifths is not circular
  bool mustChangeOff = false;        // RZ11: must change never fires
  bool mustChangeBell = false;       // RZ11: an ordered key's must-change move uses the bell around cur (zero weights at low chaos)
  bool mustChangeFirstMoveOnly = false;  // RZ11: each group tries only its first move
  bool noSpanFilter = false;         // RZ12: the transposition span bound is not applied
  bool repeatsDown = false;          // RZ12: repeats may draw below cur
  bool lightTriggerPulse = false;    // RZ12: trigger_pulse is lit (taste 1)
  bool allocScratch = false;         // RZ13: randomize allocates
  bool keepUnlitChordHome = false;   // RZ16: with the arp off, chord_enabled's keep test returns home's off
};
#endif

struct Request {
  Verb verb = Verb::Both;
  std::uint32_t seed = 0, click = 0, corner = 0;   // horde's draw key (corner 0..3)
  std::uint32_t chaosPm = 0;                       // 0..1000; larger values are read as 1000
  std::uint32_t amountPm = 1000;                   // 0..1000; larger values are read as 1000
  bool mustChange = true;                          // horde's "ARCH must change" (Structure only)
  LockSet locks{};                                 // horde's effective locks: start from defaultLocks()
#ifdef ARP_PLANTS
  Plants plants{};
#endif
};

struct Result {
  Settings settings{};            // valid whenever `cur` is (validSettings)
  std::uint64_t changed = 0;      // bit per ParamId: the keys that differ from `cur`
  bool nothingToDraw = false;     // must change was due and found no movable unit (horde can say why)
  std::int32_t emptyPicks = 0;    // a pick that met no candidate or a zero total weight: always 0 by construction (RZ3 asserts it)
};

// SPEC 15. `home` is the bell's centre (horde: the corner's saved snapshot); keys not drawn keep `cur`.
Result randomize(const Settings& cur, const Settings& home, const Request& rq) noexcept;
inline Result randomize(const Settings& cur, const Request& rq) noexcept { return randomize(cur, cur, rq); }

LockSet defaultLocks() noexcept;                 // the 15 keys exempt by default (rate, quantize, Arp's own scale)
Class classOf(ParamId) noexcept;
const char* paramName(ParamId) noexcept;          // the params.json key
bool validSettings(const Settings&) noexcept;     // every key inside params.json's legal range

// Generic access by id, enums and bools as their index (the form locks, the changed mask and horde's one table use).
std::int64_t getParam(const Settings&, ParamId) noexcept;
void setParam(Settings&, ParamId, std::int64_t) noexcept;   // the value is clamped into the key's legal range

}  // namespace arp::rnd
