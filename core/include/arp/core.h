// arp/core.h - Arp's note processor (SPEC 1): input events + transport + settings in, output
// events out, each at a sample offset. Pure: it reads no clock, makes no system call, and after
// prepare() allocates nothing. Framework-free; the adapter (CLAP/VST3/AU, horde) owns the rest.
//
// M1a scope (ROADMAP T-003): order `up`, the plain one-step pattern, restart `chord`, sync time,
// the held set, the host and free clocks, the single release path and the event caps. Every
// setting outside that scope is accepted and ignored (see core.cpp, "SPEC-GAP: M1a").
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "arp/arith.h"
#include "arp/int128.h"
#include "arp/pattern.h"
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
  // ---- M1b ----
  bool owedIdCollision = false;  // LOW-A: after prepare() the id counter restarts at 0 although owed offs still hold ids
  bool orderTables = false;      // SPEC 4.2: bounce uses bounce2's period, converge's parity is swapped, pedal_high plays pedal_low, chord_between starts with a single
  bool shuffleBoundaryRepeat = false; // N2: shuffle skips the swap that forbids a back-to-back repeat
  bool advanceOnRest = false;    // R1: m advances on a rest
  bool pickAdvancesM = false;    // R1: an explicit pick advances m
  bool ignorePick = false;       // U1: every pick is read as `order`
  bool swapMidStep = false;      // U1: setUserPattern writes the READER's slot in place (an edit can land mid-block)
  bool userFirstWins = false;    // U1: a write is dropped while an earlier one is still unconsumed
  bool prepareResetsUser = false;// U1: prepare() wipes the user pattern
  bool transposeMerged = false;  // X1: r2.6's merged octave range instead of repeated shifted passes
  bool stepsZeroShifts = false;  // X1: steps = 0 still transposes
  bool stepSizeIgnored = false;  // ST1: every step offset uses 12 semitones
  bool stepOffsetReadsGlobal = false; // ST1: the step offset reads the GLOBAL step_size instead of the pattern's
  bool tieAtTieStep = false;     // K1: ties are evaluated at the tie step instead of by look-ahead
  bool trigEveryRatchet = false; // T1: the trigger fires on every ratchet
  bool probLE = false;           // P1: prob uses <= instead of <
  bool untaggedOn = false;       // S1: some ons carry the wrong stream tag
  bool timeUsesTempo = false;    // TM1: the time step scales with the host tempo
  bool timeAnchorsTransport = false; // TM1: the time grid runs from the start of the run, not from the key press
  bool regridAbsolute = false;   // TM2: a step-time change re-grids on absolute multiples of the new time
  bool regridKAbs = false;       // TM2: a step-time change re-grids at kAbs * L_new
  bool timeBarsCount = false;    // TM3: restart bar / free count bars of 61,440 us in time mode
  bool inactiveReanchors = false;// TM3: a change of the inactive timing setting (rate / step_time_us) restarts the order
  bool timeEntryNoStart = false; // TM3: entering time mode with keys held does not start the time clock
  std::function<void()> midBlockHook; // U1: called before every step is evaluated, standing for another thread landing a setUserPattern mid-block
  bool playedTieByKeyDesc = false;  // SPEC 4.2: `played` breaks a tie of one press sample by the HIGHER key instead of the lower
  bool drawIgnoresEpoch = false; // SPEC 2.4 H7c: the free-clock epoch is left out of the draw (random / shuffle / prob replay on every chord)
  bool timeStepsWhileRinging = false; // SPEC 2.5/2.6: in time mode steps (trigger-only ones too) keep firing with H empty while a note rings (the old 2.4 idle)
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

  // SPEC 4.6. The user pattern is preset DATA: its storage belongs to the instance (three slots, sized at construction), so
  // prepare() and reset() never touch it. setUserPattern() / getUserPattern() are called OFF the audio thread, by ONE
  // writer at a time (the caller serialises them): setUserPattern validates and clamps, then publishes through a triple
  // buffer (one atomic index exchange: no lock, no tearing, the latest write wins). process() takes a published pattern
  // ONCE per call, so it applies from the first step evaluated in that block and never mid-block (T17).
  void setUserPattern(const UserPattern& p);
  UserPattern getUserPattern() const { return latest_; }   // the writer side's latest validated copy (what a preset saves)
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
    std::int32_t lane = 0;   // > 0: this entry is a TRIGGER on that lane (SPEC 8), not a note
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
  // SPEC 2.6: sync mode: the tick of step k is k * L. Time mode: a PIECEWISE grid, tick(k) = gO + (k - gK1) * L (a step-time
  // change moves the origin), which is k * L again while gO = gK1 = 0.
  std::int64_t gtick(std::int64_t k) const { return timeMode_ ? gO_ + (k - gK1_) * L_ : k * L_; }
  std::int64_t kAtOrAfter(I128 pos) const {
    return timeMode_ ? gK1_ + (pos - I128::mul(gO_, D_)).ceilDiv(L_ * D_) : pos.ceilDiv(L_ * D_);
  }
  std::int64_t curL() const;
  void timeRegrid();
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
  struct Note { std::int32_t key = 0, vel = 0, ch = 0; };
  int notesFor(const HeldKey* E, int e, std::int64_t kAbs, std::int32_t pick, Note* out) const;
  int shuffleAt(int e, std::int64_t c, std::int64_t pos) const;
  void regridAbsolute(std::int64_t kAbs);
  void queueOn(const Pending& p);
  void queueTrigger(I128 pos, std::int32_t lane, std::int64_t ord);
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
  std::int64_t D_ = 0;   // posNum per tick: 60 * sr * 1000 in sync mode, sr in time mode (a tick is a microsecond, SPEC 2.6)
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

  // time mode (SPEC 2.6)
  bool timeMode_ = false;
  std::int64_t gO_ = 0, gK1_ = 0;   // the piecewise grid: step gK1_ sits at tick gO_

  // the user pattern (SPEC 4.6): three slots, a writer slot w_ (writer-private), a reader slot r_ (audio-private) and the
  // published slot in mid_ (bit 2 = "fresh"). Sized at construction; prepare() and reset() never touch them.
  static constexpr int kFresh = 4;
  std::array<UserPattern, 3> userSlots_{};
  static_assert(std::atomic<int>::is_always_lock_free, "the user-pattern hand-over must be lock-free: process() may not block");
  std::atomic<int> mid_{1};
  int w_ = 0, r_ = 2;
  UserPattern latest_{};

  // pattern
  bool hasLastEvaluated_ = false, hasAnchor_ = false;
  std::int64_t lastEvaluated_ = 0, anchorK_ = 0, m_ = 0;
  std::int64_t stepSeq_ = 0;

  // enable state: moduleOn_ is `enabled`, enabled_ is "the arp is running" (module on and arp_enabled)
  bool moduleOn_ = false, enabled_ = false;
  std::array<std::int64_t, 4> prevCont_{};
  bool havePrevCont_ = false;
#ifdef ARP_PLANTS
  std::int64_t plantLastBar_ = INT64_MIN;   // timeBarsCount
  std::int64_t inactSeen_ = -1;   // inactiveReanchors: the last value of the inactive timing setting
#endif

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
