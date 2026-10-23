// arp/types.h - the seam: events, transport, settings (mirrors spec/params.json) and capacities.
// Plain data, no behaviour. SPEC section numbers in comments point at the rule being carried.
#pragma once

#include <cstdint>
#include <span>

#include "arp/int128.h"

namespace arp {

inline constexpr int kTpq = 15360;   // SPEC 2.1: ticks per quarter note

// SPEC 1.3 capacities. Fixed at compile time; nothing grows after prepare().
inline constexpr int kMaxHeldKeys = 32;
inline constexpr int kMaxRefsPerKey = 4;
inline constexpr int kMaxRefs = kMaxHeldKeys * kMaxRefsPerKey;
inline constexpr int kMaxSounding = 128;
inline constexpr int kMaxPending = 512;
inline constexpr int kOffReserve = 128;            // event-buffer slots only NoteOffs may use
inline constexpr int kMinEventsPerBlock = 1024;    // cap = max(1024, 4 * maxBlock)
inline constexpr int kMaxBlock = 4096;
inline constexpr int kMinSampleRate = 8000;
inline constexpr int kMaxSampleRate = 768000;

// TEST HOOK, lower-only: shrinks a capacity so a test can reach an overflow row (T11, T13, ...)
// in a short session. Each value is clamped to the spec's capacity above; 0 / default = the spec's.
struct Caps {
  int held = kMaxHeldKeys;
  int refsPerKey = kMaxRefsPerKey;
  int sounding = kMaxSounding;
  int pending = kMaxPending;
  int events = 0;   // 0 = max(1024, 4 * maxBlock)
};

// intake/note-vocabulary.md: a local placeholder for the NoteRef-compatible identity (survey Q6),
// kept behind this one type so horde's FOUNDATIONS type can replace it in one place.
struct NoteRef {
  std::int32_t port = 0;
  std::int32_t channel = 0;
  std::int32_t key = 0;
  // input: the host note id, or 0 when the host supplies none (identity is then (port, channel, key): every press of a
  // key carries the SAME ref, intake/note-vocabulary.md 1); output: Arp's own per-instance counter (SPEC 1.2). 64-bit so
  // the counter cannot wrap in practice and ascending order (SPEC 1.1) is preserved.
  std::int64_t id = 0;
  friend constexpr bool operator==(const NoteRef&, const NoteRef&) = default;
};

enum class EventType : std::uint8_t {
  NoteOn,
  NoteOff,
  Pedal,     // input only: CC 64, `down` when the value is >= 64 (consumed, never forwarded)
  Panic,     // input: all-notes-off / all-sound-off; output: forwarded after the offs (T7)
  Trigger,   // output only; not emitted before M1b (the plain step has trig 0)
  Other,     // input: anything else, copied to the output unchanged (SPEC 1, pass-through)
};
enum class Stream : std::uint8_t { Arp, Chord };   // SPEC 1 / D-009; `Chord` arrives with the chord stage (M2)

inline constexpr std::int64_t kNoTick = INT64_MIN;

// One struct for input and output events (SPEC 1). Offsets are samples in [0, n).
struct Event {
  EventType type = EventType::Other;
  std::int32_t offset = 0;
  NoteRef ref{};              // NoteOn / NoteOff
  std::int32_t key = 0;       // NoteOn (an input key may be out of 0..127; the core drops it at output, T16)
  std::int32_t vel = 0;       // NoteOn velocity; NoteOff release velocity (always 64 out)
  Stream stream = Stream::Arp;   // output NoteOn
  bool down = false;          // Pedal
  std::int32_t lane = 0;      // Trigger
  std::int32_t data[3] = {0, 0, 0};   // Other: an opaque payload the adapter defines, copied through verbatim
  // Output diagnostics, never read on input:
  std::int64_t tick = kNoTick;   // NoteOn: its tick; NoteOff: the tick its off was SCHEDULED at (kNoTick if unscheduled). Sample-rate independent (G4)
  std::int64_t stepSeq = 0;      // NoteOn: which evaluated step produced it (also the same-offset sort key, SPEC 1.1)
};

// SPEC 2.2: the transport snapshot the adapter fills from CLAP / VST3 / AU, once per block.
struct Transport {
  bool playing = false;
  bool hasBeats = false;
  bool hasRep = false;   // `rep` below is exact (SPEC 13.13); else `beats` is used and rounded
  I128 rep{};            // round(beats * TPQ * D), the exact block-start position in posNum units
  double beats = 0;      // the block-start position in quarter notes (only when !hasRep)
  bool hasTempo = false;
  double tempo = 120;    // bpm; <= 1 means 120, then clamped to [20, 999] (SPEC 1.3)
  std::int32_t timeSigNum = 4;       // restart = bar (M2); carried now so the seam does not change later
  std::int32_t timeSigDen = 4;
  bool hasBarStart = false;
  double barStartBeats = 0;
  // The host's loop. Informational for now: the adapter splits the block at a wrap, and the core sees the wrap as a
  // position jump (band 3, T6). Carried now so the public struct does not change when a later milestone needs them.
  bool loopActive = false;
  double loopStartBeats = 0;
  double loopEndBeats = 0;
};

// horde's global scale, read-only (SPEC 6). Unused until quantize lands (M2).
struct Scale {
  std::int32_t root = 0;    // 0..11
  std::uint16_t mask = 0;   // 12 bits, root-relative
};

// ---- settings: integers and enums only (SPEC 9), defaults equal spec/params.json ----
enum class Order : std::uint8_t { Up, Down, Bounce, Bounce2, Played, Chord, Random, Shuffle, Converge, Diverge, PedalLow, PedalHigh, ChordWalk, Stride, ChordBetween, Pairs };
enum class Pattern : std::uint8_t { None, Offbeat, Gallop, ThreeThreeTwo, AccentOne, TiedLegato, OctaveLeap, RollEnd, SparseChance, BarTrigger, TriggerPulse, User };
enum class Rate : std::uint8_t { R2_1, R3_2, R2_1T, R1_1, R3_4, R1_1T, R1_2, R3_8, R1_2T, R1_4, R3_16, R1_4T, R1_8, R3_32, R1_8T, R1_16, R3_64, R1_16T, R1_32, R3_128, R1_32T, R1_64 };
enum class RateMode : std::uint8_t { Sync, Time };
enum class StepUnit : std::uint8_t { Semitones, Degrees };
enum class Latch : std::uint8_t { Off, Replace, Append };
enum class VelMode : std::uint8_t { Played, Fixed };
enum class Restart : std::uint8_t { Chord, Bar, Free };
enum class Quantize : std::uint8_t { Off, Scale, ChordRoot };
enum class ScaleSource : std::uint8_t { Global, Own };

// The names params.json stores each enum by, in enum order. Index with static_cast<size_t>(value).
inline constexpr const char* kOrderNames[] = {"up", "down", "bounce", "bounce2", "played", "chord", "random", "shuffle", "converge", "diverge", "pedal_low", "pedal_high", "chord_walk", "stride", "chord_between", "pairs"};
inline constexpr const char* kPatternNames[] = {"none", "offbeat", "gallop", "three_three_two", "accent_one", "tied_legato", "octave_leap", "roll_end", "sparse_chance", "bar_trigger", "trigger_pulse", "user"};
inline constexpr const char* kRateNames[] = {"2/1", "3/2", "2/1T", "1/1", "3/4", "1/1T", "1/2", "3/8", "1/2T", "1/4", "3/16", "1/4T", "1/8", "3/32", "1/8T", "1/16", "3/64", "1/16T", "1/32", "3/128", "1/32T", "1/64"};
inline constexpr std::int32_t kRateTicks[] = {122880, 92160, 81920, 61440, 46080, 40960, 30720, 23040, 20480, 15360, 11520, 10240, 7680, 5760, 5120, 3840, 2880, 2560, 1920, 1440, 1280, 960};
inline constexpr const char* kRateModeNames[] = {"sync", "time"};
inline constexpr const char* kStepUnitNames[] = {"semitones", "degrees"};
inline constexpr const char* kLatchNames[] = {"off", "replace", "append"};
inline constexpr const char* kVelModeNames[] = {"played", "fixed"};
inline constexpr const char* kRestartNames[] = {"chord", "bar", "free"};
inline constexpr const char* kQuantizeNames[] = {"off", "scale", "chord_root"};
inline constexpr const char* kScaleSourceNames[] = {"global", "own"};

// Mirrors spec/params.json, key for key (47 settings); tools/core_params_check.py guards the drift.
// `continuous` ones (gate, swing, vel_fixed, step_time_us) are read at step evaluation; every other
// one is discrete and applies from the next evaluated step, except `enabled` and `latch`, which
// apply at offset 0 of their block (SPEC 9, 13b.12).
struct Settings {
  bool enabled = false;          // the WHOLE module (T4/T5)
  bool arpEnabled = true;        // the arp alone (SPEC 14)
  Order order = Order::Up;
  Pattern pattern = Pattern::None;
  Rate rate = Rate::R1_16;
  RateMode rateMode = RateMode::Sync;
  std::int32_t stepTimeUs = 125000;   // continuous
  std::int32_t steps = 0;
  std::int32_t stepSize = 12;
  StepUnit stepUnit = StepUnit::Semitones;
  std::int32_t gate = 80;             // continuous, percent 1..200
  std::int32_t swing = 500;           // continuous, permille 500..750
  Latch latch = Latch::Off;
  VelMode velMode = VelMode::Played;
  std::int32_t velFixed = 100;        // continuous 1..127
  Restart restart = Restart::Chord;
  std::int32_t repeats = 0;
  Quantize quantize = Quantize::Off;
  ScaleSource scaleSource = ScaleSource::Global;
  std::int32_t scaleRoot = 0;
  bool scaleDeg[12] = {true, false, true, false, true, true, false, true, false, true, false, true};   // C major, root-relative
  std::uint32_t seed = 0;
  bool chordEnabled = false;
  StepUnit chordUnit = StepUnit::Semitones;
  bool chordOn[6] = {true, true, false, false, false, false};
  std::int32_t chordShift[6] = {4, 7, 12, 0, 0, 0};
  friend constexpr bool operator==(const Settings&, const Settings&) = default;
};

// One input block (SPEC 1). `in` must outlive the call; offsets are sorted, in [0, n).
struct Block {
  std::int32_t n = 0;               // 1 <= n <= maxBlock
  Transport transport{};
  Settings settings{};
  Scale scale{};
  std::span<const Event> in{};
};

// What the core did, as counts (SPEC 1.3 "counted"; the `rows` are T-row coverage evidence for the tests).
struct Counters {
  std::int64_t heldOverflow = 0;      // a new key beyond 32 held keys
  std::int64_t refOverflow = 0;       // a 5th ref on one key
  std::int64_t soundingOverflow = 0;  // T11
  std::int64_t pendingOverflow = 0;   // an on refused: the 512-entry queue was full
  std::int64_t eventOverflow = 0;     // a non-note event dropped: no room in the block's event buffer
  std::int64_t refused = 0;           // T13: an on refused at the event cap / sounding-table slack
  std::int64_t keysDropped = 0;       // T16
  std::int64_t badInput = 0;          // n out of range, or an input event outside [0, n): ignored
  std::int64_t stepLogDropped = 0;    // a step record that did not fit lastSteps()
  std::int64_t offsLost = 0;          // a NoteOff that found no room in the event buffer: must stay 0 (N1)
  std::int64_t rows[17] = {};         // rows[i] counts how often SPEC 7 row Ti really acted (index 0 unused)
};

// One evaluated grid step, for the UI's step highlight and the G5 once-guard check.
struct StepRecord {
  std::int32_t offset = 0;
  std::int64_t kAbs = 0;
  std::int64_t k = 0;          // kAbs - anchorK
  std::int64_t mBefore = 0;
  std::int64_t mAfter = 0;
  bool played = false;
};

}  // namespace arp
