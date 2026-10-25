// arp/core.h - Arp's note processor (SPEC 1): input events + transport + settings in, output
// events out, each at a sample offset. Pure: it reads no clock, makes no system call, and after
// prepare() allocates nothing. Framework-free; the adapter (CLAP/VST3/AU, horde) owns the rest.
//
// Scope (ROADMAP T-003, T-004, T-005 M2a + M2b): the seam, all order styles and step fields, time mode, scale quantize,
// the degree unit, swing and velocity, restart (chord / bar / free), repeats, the morph timing of settings and the chord stage.
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
  // ---- M2a ----
  bool skipQuantizeChord = false;   // Q1: a step that plays several keys (a chord step) is not quantized
  bool tieAlwaysDown = false;       // Q2 / Q5: a tie always resolves to the lower key, whatever lastQ says
  bool ownIgnored = false;          // Q3: scale_source = own still reads the global scale
  bool chordRootAsRoot = false;     // Q4: chord_root anchors the scale shape on the scale_root setting, not on the lowest held key
  bool lastQAtEmission = false;     // Q5: lastQ updates when an on is EMITTED, not when its step is evaluated (a refused on then never counts)
  bool lastQNoReset = false;        // Q5: release-all (and reset / prepare) leaves lastQ alone
  bool collisionKeepsLast = false;  // SPEC 6: two keys landing on one key keep the LAST source's velocity and channel, not the earliest
  bool degreesAsSemitones = false;  // X1: the transposition steps' degrees count as semitones while a scale is on
  bool stepDegreesAsSemitones = false;  // ST1: the step offset's degrees count as semitones while a scale is on
  bool semitoneAfterQuantize = false;   // SPEC 4.7 stage 1: the semitone part is added AFTER quantize instead of before it
  bool degreeBeforeQuantize = false;    // SPEC 4.7 stage 4: the degree move runs BEFORE quantize instead of after it
  bool lastQPreMove = false;        // SPEC 4.7 stage 6: lastQ is the quantized key before the degree move, not the final key
  bool droppedKeySetsLastQ = false; // SPEC 4.7 stage 6: a key dropped at stage 5 still sets lastQ (to its quantized value)
  bool noStage2Drop = false;        // SPEC 4.7 stage 2: only the final range check drops a key (r2.7's single drop)
  bool swingParityK = false;        // G3: swing parity follows k = kAbs - anchor instead of kAbs
  bool timeIgnoresSwing = false;    // TM1: time mode plays every step straight
  bool releaseKeepsPending = false; // SPEC 5.1 / 8: release-all leaves the pending queue alone (a swung on or trigger survives a Panic)
  // ---- M2b: restart, repeats, morph timing ----
  bool restartBarAsChord = false;   // SPEC 4.5: restart `bar` anchors like `chord` (at the gesture start, never at a bar start)
  bool restartFreeAsChord = false;  // SPEC 4.5: restart `free` anchors like `chord`
  bool repeatsCountPatternPasses = false; // RP1: one repeat is one pass of the STEP pattern, not a full cycle of the whole pattern
  bool repeatsNeverStop = false;    // RP1: repeats is ignored, the arp never runs out
  bool exhaustedStopsGuard = false; // RP1: a spent arp stops consuming the once-guard (a burst of overdue steps at the next gesture)
  bool onlyGestureResets = false;   // RP1: only a gesture start restarts the repeat count; a key-adding press (latch append, pedal) does not
  bool morphRevives = false;        // RP1: the exhausted flag is re-derived every step, so raising `repeats` revives a spent arp
  bool restartMorphKeepsM = false;  // SPEC 9 Q-A2: a morph into restart bar / free leaves the played-note counter alone
  bool restartMorphReanchors = false; // SPEC 9 Q-A2: a morph into restart `chord` re-anchors at the next step instead of keeping the anchor
  bool morphSwitchCuts = false;     // SPEC 7 T8: a discrete setting that changes releases the arp's sounding notes
  bool morphSwitchDropsPending = false; // SPEC 7 T8: a discrete setting that changes clears the pending queue (queued ratchets and triggers vanish)
  // ---- M2b: the chord stage (CH1) ----
  bool chordNoDedupe = false;       // CH1: a key shared by two sources is emitted twice (no dedupe)
  bool chordEndsOnFirstSource = false; // CH1: a chord note ends when its first source leaves, not its last
  bool arpIgnoresChord = false;     // CH1: the arp plays the raw held keys although the chord stage is on
  bool chordRestartAll = false;     // CH1: a re-map ends and restarts every chord note, not only the leavers and joiners
  bool chordRecomputeAtRelease = false; // CH1: the leaver's chord keys are recomputed at release instead of the stored mapping (strands a note)
  bool t2AcrossStreams = false;     // CH1 / SPEC 5.2: a retriggered arp note ends the chord note sounding on the same key
  bool arpToggleEndsChord = false;  // CH1: arp_enabled off ends the chord notes too
  bool chordEndsAtEdge = false;     // CH1: a transport edge ends and restarts the chord notes
  bool sharedSoundingTable = false; // CH1: T11 counts chord notes in the arp's 128-entry table and may evict one
  bool chordSyncPerEvent = false;   // CH1: the stage syncs after every input event, so a count going 1-0-1 inside one sample ends and restarts its note
  bool chordDedupeByChannelKey = false; // CH1: dedupe by (channel, key) instead of key alone
  bool chordPlusZeroUnquantized = false; // CH1 / D-020: the +0 slot (the played note) is left unquantized while quantize is on
  bool chordSkipQuantize = false;   // CH1 / D-020: the chord stage never snaps to the scale
  bool chordReadsLastQ = false;     // CH1 / D-020: the chord stage resolves a tie toward the arp's lastQ instead of always lower
  bool chordWritesLastQ = false;    // CH1 / D-020: the chord stage writes its snapped keys into the arp's lastQ
  bool chordTaggedArp = false;      // S1: some chord-stage ons carry the `arp` stream tag
  bool swingPerStep = false;        // SPEC 5.1 E6: swing is read at EVERY step (M2a), not latched per step pair, so a ratcheted step can sound past the next on
  bool chordRootFromChordSet = false; // SPEC 14 / 6: the arp's chord_root anchors on the lowest key of H' (the chord set), not on the lowest PLAYED key
  // ---- M2b critic follow-up (D-027) ----
  bool chordSyncBlockStartFirst = false; // CH1 / SPEC 14: the block-start sync runs before an input at offset 0 (a 1-0-1 count at that sample ends and restarts its note; a shift change plus a release starts a 1-sample note)
  bool adoptKeepsLastBar = false;   // SPEC 4.5 / 9 Q-A2: a morph into restart bar / free keeps the old bar bookkeeping, so a return to bar inside the same bar does not re-anchor
  bool reanchorKeepsLastBar = false; // SPEC 4.5 / 2.4: a loop wrap or jump (T6) keeps the old bar bookkeeping, so a landing in the same bar index does not re-anchor at the bar start
  bool repeatsPauseWhileEmpty = false; // SPEC 4.8 E9: with H' empty the repeat count is not checked (the pre-D-027 behaviour: an arp let go early never runs out)
  bool keepSpilledRatchets = false; // SPEC 5.1 E10: time mode keeps an earlier step's pending sub-notes that fall at or after the next step's on
  bool swingLatchUnanchored = false;  // SPEC 5.1 / 2.5: a step with no anchor still latches the swing pair (the pre-D-027 order)
  bool chordStalePressTime = false; // SPEC 3 / 14: a stored chord record keeps the press time it had when its key entered H, not the earliest LIVE ref's
  bool chordOnLate = false;         // G4 / CH1: a chord-stream NoteOn lands one sample after the input that caused it (96 kHz and up)
  bool chordOnTickSkew = false;     // G4 / CH1: a chord-stream NoteOn reports the tick one past the host's position at its sample
  // ---- M2 re-check (D-028) ----
  bool cOnlyAtStep = false;         // SPEC 4.8 E11: C is set only at an evaluated step with keys held (a chord tapped and released between two steps never sets it)
  bool edgeRevivesWhileEmpty = false; // SPEC 4.8 E12: T6 and T7 reset the count and clear `exhausted` even with no key held (the pre-D-028 behaviour: a loop wrap, jump or Panic revives a used-up arp)
  bool edgeResetsCountWhileEmpty = false; // SPEC 4.8 E12: T6 and T7 reset the repeat count even with no key held (a round in progress restarts after a loop wrap, jump or Panic)
  bool swingSpentNoLatch = false;   // SPEC 5.1 E6 / L1: a spent step (4.8) does not latch the swing pair, so the odd step after it reads the current swing
  // ---- M3a (T-007, horde's note-processor 1.0 bar) ----
  bool meterAs44 = false;           // SPEC 4.5: restart `bar` reads every time signature as 4/4 (the bar length ignores timeSigNum / timeSigDen)
  bool emitOnEmptySet = false;      // bar row "no input, no output": a step that finds no key held plays a note anyway
  // ---- M3b rework (D-030) ----
  bool evictBornPrevSample = false; // T11: a note born in the PREVIOUS sample cannot be evicted either (the victim must be two samples old): the off-by-one a mutant of the eviction test made
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
  // `slot` orders a chord-stage key among keys of one source press (SPEC 14 `Pp`); it is 0 for every key of the plain held set.
  struct HeldKey { std::int32_t key = 0, vel = 0, ch = 0; std::int64_t abs = 0; std::int32_t slot = 0; };
  // One slot of the sounding table. Slots do not move: a note keeps its slot from its on to its off, and the ways to find it (the per-key
  // chain, the off schedule) hold the slot's index. See "sounding notes" below for the indexes and the invariants they keep.
  enum : std::uint8_t { kOffNone, kOffSched, kOffQuick };   // where an entry's due off is looked up: nowhere / sched_ / quick_
  struct Sounding {
    I128 offPos{};
    NoteRef ref{};
    std::int64_t onAbs = 0;
    std::int32_t quickT = 0;   // kOffQuick only: when the off is due, as an offset in the block it was filed in (at most the block length)
    std::int16_t keyNext = -1; // the next slot sounding on the same key (the per-key chain); in a FREE slot, the next free slot
    std::int16_t liveNext = -1, livePrev = -1;   // the live-arp list (liveHead_): ascending output id, only notes that are arp, used and not dying
    std::uint8_t off = kOffNone;
    bool used = false;
    bool hasOff = false;       // a scheduled off exists (else: dying, or never)
    bool dying = false;        // released inside its own on sample: its off waits for on + 1 (SPEC 1.1)
    bool chord = false;        // a chord-stage note (SPEC 14): no scheduled off, it ends with its last source
  };
  struct Pending {
    I128 pos{}, offPos{};
    std::int32_t key = 0, vel = 0, ch = 0;
    std::int32_t lane = 0;   // > 0: this entry is a TRIGGER on that lane (SPEC 8), not a note
    std::int64_t ord = 0;
#ifdef ARP_PLANTS
    bool setsLastQ = false;  // lastQAtEmission: lastQ follows this on when it is emitted
#endif
  };
  enum class Clock : std::uint8_t { None, Host, Free };
  static constexpr int kSoundingSlack = 16;   // physical room for notes dying inside their own on sample
  static constexpr int kMaxChordSounding = 224;   // SPEC 14 capacity: its OWN table, 32 held keys x 7 slots (distinct keys never exceed 128)
  static constexpr int kChordSlots = 7;           // the +0 slot and six shifts
  static constexpr int kMaxArpKeys = 128;         // the arp's held set H' is the chord set when the stage is on: distinct keys 0..127
  static constexpr int kIgnoredRing = 256;
  static constexpr int kSoundingSlots = kMaxSounding + kSoundingSlack + kMaxChordSounding;   // the sounding table: 368 slots

  // time
  std::int64_t ofs(I128 x) const;
  // The timeline asks ofs() about the same few positions at every pick (the front of the pending ring, the soonest off, the next grid step), and a
  // run of ons or offs shares one position. Inside a block ofs() is a pure function of the position (start_ and inc_ do not move; freeStart shifts
  // positions and start_ together), so asking again about the SAME position is answered from the last answer. dropMemos(): start_ / inc_ changed.
  struct OfsMemo { I128 pos{}; std::int64_t s = 0; bool ok = false; };
  std::int64_t ofsMemo(OfsMemo& m, const I128& pos) const {
    if (!m.ok || !(m.pos == pos)) { m.pos = pos; m.s = ofs(pos); m.ok = true; }
    return m.s;
  }
  void dropMemos() const { memoPend_.ok = memoOff_.ok = memoStep_.ok = false; }
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
  Restart restartMode() const;
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
  void switchRate(std::int64_t kAbs, std::int32_t so);
  void switchTouch(std::int32_t so);
  void adopt(std::int32_t so);
  void evalStep(std::int64_t kAbs, std::int32_t so);
  void dropSpilledSubnotes(std::int64_t on0);
  void activePattern(const PatternStep*& steps, std::int64_t& plen, std::int64_t& pSize, StepUnit& pUnit) const;
  std::int64_t cycleLength(const PatternStep* steps, std::int64_t plen, int e) const;
  void updateCycle();
  int heldForArp(HeldKey* out) const;
  std::int32_t lowestHeldKey() const;
  struct Note { std::int32_t key = 0, vel = 0, ch = 0; };
  int notesFor(const HeldKey* E, int e, std::int64_t kAbs, std::int32_t pick, Note* out) const;
  int shuffleAt(int e, std::int64_t c, std::int64_t pos) const;
  void regridAbsolute(std::int64_t kAbs);
  void queueOn(const Pending& p);
  void queueTrigger(I128 pos, std::int32_t lane, std::int64_t ord);
  // emission and the ONE release path
  bool room(int extra) const { return outN_ + soundN_ + extra <= capEv_; }
  // the sounding table, its indexes and the pending queue (see "sounding notes and the pending queue" below)
  static int keyChain(std::int32_t key) { return static_cast<unsigned>(key) < 128u ? key : 0; }
  void resetSounding();
  int allocSlot(const NoteRef& ref, bool chord, std::int64_t onAbs);
  void freeSlot(int i);
  int findSounding(const NoteRef& ref) const;
  int collectSounding(int* slots, bool includeChord) const;
  int oldestSounding() const;
  void liveLink(int i);
  void liveUnlink(int i);
  void unlinkOff(int i);
  void schedInsert(int i);
  void schedErase(int i);
  void quickAdd(int i, std::int64_t t);
  void quickErase(int i);
  void startBlockOffs();
  std::int64_t nextOffTime() const;
  bool offDueBy(const I128& pos, const I128& bound, std::int64_t t) const;
  int takeDueOffs(std::int64_t t, int* out);
  bool releaseSlot(int i, std::int32_t s);
  int& schedAt(int logical) { return sched_[static_cast<std::size_t>((schedHead_ + logical) & (kSchedRing - 1))]; }
  int schedAt(int logical) const { return sched_[static_cast<std::size_t>((schedHead_ + logical) & (kSchedRing - 1))]; }
  Pending& pendAt(int logical) { return pending_[static_cast<std::size_t>((pendHead_ + logical) & (kMaxPending - 1))]; }
  const Pending& pendAt(int logical) const { return pending_[static_cast<std::size_t>((pendHead_ + logical) & (kMaxPending - 1))]; }
  Pending popPending();
  bool pushEvent(const Event& e);
  void pushNonNote(const Event& e);
  void emitOn(const Pending& p, std::int32_t s);
  bool release(const NoteRef& ref, std::int32_t s);
  void releaseAll(std::int32_t s, bool includeChord = true);
  void releaseArp(std::int32_t s) { releaseAll(s, false); }   // T6 and the arp's own switch-off: chord notes behave like held keys (SPEC 14)
  void sortEvents();
  void insertionSort(int from, int to);
  void sortSegment(int from, int to);
  static constexpr int kSortScratch = 512;    // the largest same-offset run sortSegment sorts by key
  void flushOwed();
  void resetState();
  void plantAlloc();
  // the chord stage (SPEC 14)
  void chordSync(std::int32_t s);
  void chordEndAll(std::int32_t s);
  bool chordStart(std::int32_t key, std::int32_t vel, std::int32_t ch, std::int32_t s, NoteRef& refOut);   // emits the chord-stream NoteOn
  void chordStop(NoteRef& ref, bool& sounding, std::int32_t s);                                              // the ONE end of a chord note: release()

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
  std::int64_t inc_ = 0, L_ = 0;
  std::int64_t lastTempoMilli_ = 120000;

  // clock
  Clock clock_ = Clock::None;
  I128 start_{}, next_{};
  bool freeRunning_ = false;
  std::uint32_t epoch_ = 0;

  mutable OfsMemo memoPend_, memoOff_, memoStep_;   // see ofsMemo()

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

  // restart (SPEC 4.5) and repeats (SPEC 4.8)
  std::int64_t barTicks_ = 4 * kTpq, barPhase_ = 0;   // the bar grid of this block (time mode: 61,440 us, only a plant reads it)
  // swing is latched per step pair (SPEC 5.1, E6): the even step reads it, its odd partner reuses it when the even step was the last one visited
  std::int64_t swingUsed_ = 500, swingPair_ = 500, swingPairK_ = INT64_MIN;
  std::int64_t lastBar_ = 0;
  bool hasLastBar_ = false;
  std::int64_t repeatSteps_ = 0;
  bool exhausted_ = false;
  std::int64_t lastC_ = 0;   // SPEC 4.8 E9: the last non-empty cycle length; kept while H' is empty so the count keeps running (hasLastC_: not before the first gesture)
  bool hasLastC_ = false;

  // scale quantize (SPEC 6): the block's global scale, read at step evaluation, and lastQ, the tie chain (reset at every release-all)
  Scale scale_{};
  std::int64_t lastQ_ = 0;
  bool hasLastQ_ = false;

  // enable state: moduleOn_ is `enabled`, enabled_ is "the arp is running" (module on and arp_enabled)
  bool moduleOn_ = false, enabled_ = false;
  std::array<std::int64_t, 4> prevCont_{};
  bool havePrevCont_ = false;
#ifdef ARP_PLANTS
  std::int64_t inactSeen_ = -1;   // inactiveReanchors: the last value of the inactive timing setting
#endif

  // the chord stage (SPEC 14). Records hold the STORED mapping of each held key (computed once when the key enters H, never
  // recomputed at release); notes are keyed by chord KEY (dedupe by key alone) with a source count; the set is what the arp reads (H').
  struct ChordRec {
    HeldKey h{};
    int n = 0;
    std::int16_t key[kChordSlots]{};
    std::int8_t slot[kChordSlots]{};
  };
  struct ChordNote {
    bool used = false;          // count > 0: the note exists (it may be silent: refused at the event cap, SPEC 14 note 17)
    bool sounding = false;      // an on was emitted and no off yet
    std::int32_t vel = 0, ch = 0;
    NoteRef ref{};
    int count = 0;
#ifdef ARP_PLANTS
    int maxCount = 0;
    bool sounding2 = false;     // chordNoDedupe: the second on for a shared key
    NoteRef ref2{};
#endif
  };
  struct ChordSigData {
    StepUnit unit{};
    ScaleSource source{};
    Quantize quantize{};
    std::int32_t root = 0;
    std::uint16_t ownMask = 0, globalMask = 0;
    std::int32_t globalRoot = 0;
    bool on[6]{};
    std::int32_t shift[6]{};
    friend constexpr bool operator==(const ChordSigData&, const ChordSigData&) = default;
  };
  std::array<ChordRec, kMaxHeldKeys> chordRec_{};
  int chordRecN_ = 0;
  std::array<ChordNote, 128> chordNote_{};     // indexed by key 0..127
  std::array<HeldKey, kMaxArpKeys> chordSet_{};
  int chordSetN_ = 0;
  ChordSigData chordSig_{};
  bool chordSigValid_ = false;
  std::uint64_t chordSyncVer_ = 0;   // refsVer_ as of the last COMPLETE pass of chordSync (only trusted while chordSigValid_ and the signature are unchanged)
#ifdef ARP_PLANTS
  std::array<int, 128> chordDrift_{};          // chordRecomputeAtRelease
#endif

  // held set
  std::array<Ref, kMaxRefs> refs_{};
  int refN_ = 0;
  std::uint64_t refsVer_ = 0;   // bumped when a ref joins or leaves refs_ (what H is made of; a state change Down / Pedal / Latched leaves H alone): the chord stage skips a pass while it holds still
  std::array<NoteRef, kIgnoredRing> ignored_{};
  int ignoredN_ = 0;
  bool pedalDown_ = false;

  // sounding notes and the pending queue
  // One table of slots (arp + chord, `Sounding::chord` tells them apart). The arp's own 128-entry limit (T11) counts only its
  // entries; the chord stage has 224 of its own (SPEC 14), so arp eviction never reaches a chord note.
  // M3b (D-030): the table used to be a dense list searched linearly by every event (and shifted on every release); now each note owns a
  // slot for its whole life and is found through indexes, so an event costs O(1) / O(log n) however many notes sound. The invariants:
  //   * a slot is `used` iff it holds a sounding note; free slots form a chain through keyNext from freeHead_ (soundN_ counts the used ones);
  //   * keyHead_[k] heads the chain (keyNext) of the used slots whose ref.key is k (any other key shares chain 0: keyChain()), newest first:
  //     a retrigger (T2) and release(ref) walk one short chain instead of the table;
  //   * liveN_ counts used slots that are not `dying`, liveChordN_ those of them in the chord stage: T11 asks "how many live arp notes" in O(1);
  //     liveHead_ / liveTail_ chain exactly the live ARP notes in ascending output id (liveNext / livePrev), so T11's victim, the oldest live arp note
  //     that was not born this very sample, is found from the head instead of by scanning the table (every on of an overfull table evicts);
  //   * the OFF SCHEDULE finds the next off without scanning. An entry with a due off (hasOff, or dying) is in exactly one of two places:
  //       sched_  - a ring of slot indices sorted by offPos (ties in any order), for an off that lies at or after the sample after its own on.
  //                 Sample offsets are monotone in offPos, so the head is the soonest, and `max(0, ofs(offPos))` is its due offset.
  //       quick_  - the few entries whose off is NOT later than their own on sample (a note shorter than a sample) or that are dying: the
  //                 SPEC 1.1 "never at the sample of its own on" clamp makes their due offset on + 1, a number that is fixed inside a block
  //                 (quickT) but not an order of offPos. At the next block start they move to sched_ (startBlockOffs).
  //     off == kOffSched / kOffQuick says which; an entry with neither (a chord note, until it dies) has no due off.
  //     Offs due at the same offset go out together, ascending output id (takeDueOffs), which is what scanning the dense table in order did.
  std::array<Sounding, kSoundingSlots> sounding_{};
  static constexpr int kSchedRing = 512;   // a power of two >= the slots
  static_assert(kSchedRing >= kSoundingSlots && (kSchedRing & (kSchedRing - 1)) == 0, "sched_ is a ring indexed with a mask");
  std::array<std::int16_t, 128> keyHead_{};
  std::int16_t freeHead_ = -1;
  int soundN_ = 0, chordSoundN_ = 0, liveN_ = 0, liveChordN_ = 0;
  std::int16_t liveHead_ = -1, liveTail_ = -1;
  std::array<int, kSchedRing> sched_{};
  int schedHead_ = 0, schedN_ = 0;
  std::array<int, kSoundingSlots> quick_{};
  int quickN_ = 0;
  // The pending queue is a ring sorted by `pos`, equal positions in the order they were queued: the soonest is always at the front
  // (popPending), and a new entry is filed from the back, where nearly every one belongs, so queueing and popping are O(1) in practice.
  static_assert((kMaxPending & (kMaxPending - 1)) == 0, "pending_ is a ring indexed with a mask");
  std::array<Pending, kMaxPending> pending_{};
  int pendHead_ = 0, pendN_ = 0;
  std::int64_t idCounter_ = 0;
  // T10 across prepare(): notes still owed a NoteOff when prepare() is called. Their refs live here (sized at construction,
  // untouched by resetState) and are released at offset 0 of the next process() or reset().
  std::array<NoteRef, kSoundingSlots> owed_{};
  int owedN_ = 0;

#ifdef ARP_PLANTS
  bool dropped_ = false, doubled_ = false;
  double fstart_ = 0, tps_ = 0;   // floatPosition: the block-start position in ticks, and ticks per sample
  float fnext_ = 0;               // ... carried between blocks in float32
  bool haveFnext_ = false;
#endif
};

}  // namespace arp
