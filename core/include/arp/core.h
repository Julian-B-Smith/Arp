// arp/core.h - Arp's note processor (SPEC 1): input events + transport + settings in, output
// events out, each at a sample offset. Pure: it reads no clock, makes no system call, and after
// prepare() allocates nothing. Framework-free; the adapter (CLAP/VST3/AU, horde) owns the rest.
//
// M1a scope (ROADMAP T-003): order `up`, the plain one-step pattern, restart `chord`, sync time,
// the held set, the host and free clocks, the single release path and the event caps. Every
// setting outside that scope is accepted and ignored (see core.cpp, "SPEC-GAP: M1a").
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "arp/arith.h"
#include "arp/int128.h"
#include "arp/types.h"

namespace arp {

#ifdef ARP_PLANTS
// TEST-ONLY fault switches (SPEC 11 "planted control"): each one makes the core wrong in exactly the
// way a named detector must catch. Compiled in only by the test targets; the shipped library
// target does not define ARP_PLANTS, so none of this exists there. All off by default.
struct Plants {
  bool dropRelease = false;      // N1: one release is silently dropped
  bool doubleRelease = false;    // N1: one release is emitted twice
  bool sameSampleOff = false;    // N1: a 1-tick note's off lands on its own on sample
  bool clampOffset = false;      // G1: an offset past the block is clamped into it
  bool floatPosition = false;    // G1: the position accumulates in float32
  bool roundNotCeil = false;     // G2: offsets round instead of taking the ceiling
  bool noOnceGuard = false;      // G5: the evaluated-step cursor is re-derived from the position each block
  bool timeInDraw = false;       // D1: a time-varying value enters the draw
  bool noOffReserve = false;     // C1: an on is admitted into the off-reserve; a full buffer loses the off
  bool allocInProcess = false;   // A1: process() allocates
  bool allocInReset = false;     // A1: reset() allocates
  bool allocInSettings = false;  // A1: adopting a changed discrete setting allocates
  bool srTickMath = false;       // G4: a sample-rate-dependent value enters tick math
  bool latchAtNextStep = false;  // SPEC 13b.12: latch off applies at the next arp step instead of offset 0
  bool noLatchToggle = false;    // SPEC 3: under latch append a press on a latched key adds instead of toggling out
  bool pedalIgnored = false;     // SPEC 3: a NoteOff releases the key although the pedal is down
  bool latchReplaceKeeps = false;// SPEC 3: under latch replace a gesture start keeps the latched refs
  bool t11EvictsFirst = false;   // T11: the victim is table slot 0, even a note released inside its own on sample (two notes sound at once)
  bool prepareForgetsNotes = false; // T10: prepare() drops the notes still owed a NoteOff
  bool sameRefDropsPress = false;// intake 1: a NoteOn whose ref is already known (any state) is dropped, and an ignored ref never expires
};
#endif

// Whether the library this translation unit LINKS was built with the test-only plants. A function in core.cpp, not a
// header constant: a constant would only report the macro of whoever includes the header.
bool plantsCompiledIntoLibrary();

using EventSpan = std::span<const Event>;

class Core {
 public:
  explicit Core(const Caps& caps = {});

  // sampleRate rounded to an integer in [8000, 768000] (IF-11), maxBlock in [1, 4096].
  // Allocates every buffer and resets all state. T10: any note still owed a NoteOff gets exactly one, delivered at offset 0
  // of the next process() (or reset()), so no adapter call order can strand a note. Returns false (and stays unprepared) on bad arguments.
  bool prepare(int sampleRate, int maxBlock);

  // Real time: no allocation, no locks, no syscalls. The returned span is valid until the next
  // process() / reset() / prepare() and is sorted per SPEC 1.1.
  EventSpan process(const Block& block);

  // T10: release-all. Returns the NoteOffs (at offset 0) the adapter must deliver to the engine
  // directly; every note's ref is released exactly once. Ids restart at 0 afterwards.
  EventSpan reset();

  // The steps evaluated by the last process() call (SPEC 4.1), in order.
  std::span<const StepRecord> lastSteps() const { return {steps_.data(), static_cast<std::size_t>(stepsN_)}; }

  const Counters& counters() const { return counters_; }
  int eventCapacity() const { return capEv_; }
  // A diagnostic for tests and the UI: distinct held keys. It reads the audio thread's tables without synchronisation,
  // so call it from the audio thread (between process() calls) only; it is NOT safe from another thread.
  std::size_t heldKeyCount() const;
  std::size_t soundingCount() const { return static_cast<std::size_t>(soundN_); }
  std::uint32_t epoch() const { return epoch_; }   // SPEC 2.4: free-clock epoch, mixed into every draw

  // SPEC 10, as the core uses it: draw() with this instance's current epoch.
  std::uint32_t draw(std::uint32_t seed, std::int64_t x, std::uint32_t lane) const;

#ifdef ARP_PLANTS
  Plants plants;
#endif

 private:
  enum class RefState : std::uint8_t { Down, Pedal, Latched };
  struct Ref {
    NoteRef id{};
    std::int32_t key = 0, vel = 0, ch = 0;
    RefState state = RefState::Down;
    std::int64_t abs = 0;   // absolute press sample
  };
  struct HeldKey { std::int32_t key = 0, vel = 0, ch = 0; std::int64_t abs = 0; };
  struct Sounding {
    NoteRef ref{};
    I128 offPos{};
    bool hasOff = false;       // a scheduled off exists (else: dying, or never)
    bool dying = false;        // released inside its own on sample: its off waits for on + 1 (SPEC 1.1)
    std::int64_t onAbs = 0;
    std::int64_t ofsCache = 0; // sample offset of offPos, valid for block cacheBlk
    std::uint64_t cacheBlk = 0;
  };
  struct Pending {
    I128 pos{}, offPos{};
    std::int32_t key = 0, vel = 0, ch = 0;
    std::int64_t ord = 0;
  };
  enum class Clock : std::uint8_t { None, Host, Free };
  static constexpr int kSoundingSlack = 16;   // physical room for notes dying inside their own on sample
  static constexpr int kIgnoredRing = 256;

  // time
  std::int64_t ofs(I128 x) const;
  std::int64_t offTime(Sounding& s);
  I128 posAt(std::int64_t sample) const { return start_ + I128::mul(sample, inc_); }
  std::int64_t tickOf(I128 pos) const { return pos.floorDiv(D_); }
  std::int64_t kAtOrAfter(I128 pos) const { return pos.ceilDiv(L_ * D_); }
  std::int64_t tempoMilli(const Transport& tr);
  I128 repOf(const Transport& tr) const;
  void edge();
  void reanchor(std::int32_t s, bool resetEvaluated);
  void maybeFreeStart(std::int32_t s);
  void freeStart(std::int32_t s);
  // input
  void input(const Event& e, std::int32_t s);
  void noteOn(const Event& e, std::int32_t s);
  void noteOff(const Event& e);
  void pedal(bool down);
  void gesture(std::int32_t s);
  int collectHeld(HeldKey* out) const;
  bool refIgnored(const NoteRef& r) const;
  void ignoreRef(const NoteRef& r);
  bool unignoreRef(const NoteRef& r);   // true if it was in the ignored list (and removes it)
  template <class Pred> void eraseRefs(Pred pred);   // compacts refs_ in place, keeping order
  // the grid walk
  void visit(std::int32_t so);
  void switchRate(std::int64_t kAbs);
  void adopt();
  void evalStep(std::int64_t kAbs, std::int32_t so);
  void queueOn(const Pending& p);
  // emission and the ONE release path
  bool room(int extra) const { return outN_ + soundN_ + extra <= capEv_; }
  bool pushEvent(const Event& e);
  void pushNonNote(const Event& e);
  void emitOn(const Pending& p, std::int32_t s);
  bool release(const NoteRef& ref, std::int32_t s);
  void releaseAll(std::int32_t s);
  void sortEvents();
  void flushOwed();
  void resetState();
  void plantAlloc();

  // configuration, fixed at prepare
  Caps caps_;
  bool prepared_ = false;
  int sr_ = 0, maxBlock_ = 0, capEv_ = 0;
  std::int64_t D_ = 0;   // 60 * sr * 1000: posNum per tick
  std::vector<Event> out_;
  std::vector<StepRecord> steps_;
  int outN_ = 0, stepsN_ = 0;
  Counters counters_{};

  // per-block
  Settings S_{};
  Settings active_{};
  bool haveActive_ = false;
  std::int32_t n_ = 0;
  std::int64_t absBase_ = 0, absSample_ = 0;
  std::uint64_t blk_ = 0;
  std::int64_t inc_ = 0, L_ = 0;
  std::int64_t lastTempoMilli_ = 120000;

  // clock
  Clock clock_ = Clock::None;
  I128 start_{}, next_{};
  bool freeRunning_ = false;
  std::uint32_t epoch_ = 0;

  // pattern
  bool hasLastEvaluated_ = false, hasAnchor_ = false;
  std::int64_t lastEvaluated_ = 0, anchorK_ = 0, m_ = 0;
  std::int64_t stepSeq_ = 0;

  // enable state: moduleOn_ is `enabled`, enabled_ is "the arp is running" (module on and arp_enabled)
  bool moduleOn_ = false, enabled_ = false;
  std::array<std::int64_t, 4> prevCont_{};
  bool havePrevCont_ = false;

  // held set
  std::array<Ref, kMaxRefs> refs_{};
  int refN_ = 0;
  std::array<NoteRef, kIgnoredRing> ignored_{};
  int ignoredN_ = 0;
  bool pedalDown_ = false;

  // sounding notes and the pending queue
  std::array<Sounding, kMaxSounding + kSoundingSlack> sounding_{};
  int soundN_ = 0;
  std::array<Pending, kMaxPending> pending_{};
  int pendN_ = 0;
  std::int64_t idCounter_ = 0;
  // T10 across prepare(): notes still owed a NoteOff when prepare() is called. Their refs live here (sized at construction,
  // untouched by resetState) and are released at offset 0 of the next process() or reset().
  std::array<NoteRef, kMaxSounding + kSoundingSlack> owed_{};
  int owedN_ = 0;

#ifdef ARP_PLANTS
  bool dropped_ = false, doubled_ = false;
  double fstart_ = 0, tps_ = 0;   // floatPosition: the block-start position in ticks, and ticks per sample
  float fnext_ = 0;               // ... carried between blocks in float32
  bool haveFnext_ = false;
#endif
};

}  // namespace arp
