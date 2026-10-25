// core.cpp - Arp's note processor, M2b scope (ROADMAP T-003, T-004, T-005).
//
// Implemented: SPEC 1 (block I/O, 1.1 event order, 1.2 identity, 1.3 capacities), 2.1-2.6 (tick grid, exact three-band
// position, host + free clocks, epoch, TIME MODE with the smooth step-time sweep), 3 (held set), 4.1 (counters,
// once-guard), 4.2 all 16 order styles, 4.3 every step-pattern field (bank embedded from spec/bank/starter.json, plus the
// user pattern, 4.6), 4.4 velocity, 4.5 restart `chord`, 4.7 transposition steps and step offsets in semitones AND degrees
// (the six-stage order), 5.1-5.2 (on-times with swing, span, ratchets, ties by look-ahead, gate incl. > 100 %, T2), 6 scale
// quantize (global or own scale, `scale` / `chord_root`, the lastQ tie chain), 4.5 restart chord / bar / free, 4.8 repeats,
// 7 (terminals), 8 (triggers), 9 (the timing of a changed setting: horde resolves the morph, Arp applies the integer snapshot at the
// next evaluated step or, for the settings SPEC 9 / 14 name, at offset 0), 10, 14 (the chord stage).
//
// M3b (D-030) changed no behaviour, only the cost: the sounding table and the pending queue are indexed (core.h, "sounding notes and the pending
// queue") so an event no longer scans or shifts them, and a chord-stage pass that has nothing new to do is skipped. The output is byte-identical to
// M3a's: tests/core/stream_digest*.txt pin it, hashed from the M3a core.
//
// M3c (D-032) changed no behaviour either, and again for a host that keeps sending notes: the held-key counts are kept per key instead of scanned
// (core.h, keyRefCnt_), and the chord stage updates itself for the keys that changed instead of rebuilding (chordIncremental; chordRebuild is the
// definition it must agree with, plant chordFullPass runs it every time). Same digests, same bytes.
//
// Everything is fixed-capacity: the only allocations are in prepare().
// (__FINITE_MATH_ONLY__ is predefined as 0 normally and 1 under -ffinite-math-only, so test its VALUE.)
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
#error "arp core requires IEEE semantics (NaN/inf checks on the host's floats): build without -ffast-math / -ffinite-math-only"
#endif

#include "arp/core.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iterator>
#include <numeric>
#include <tuple>

#include "arp/bank_data.h"
#include "arp/draw.h"

namespace arp {

static_assert(bankOrderMatchesPatternEnum(kBankIds, kBankCount, kPatternNames, static_cast<int>(Pattern::User)),
              "arp::kBank (generated from spec/bank/starter.json) must list the patterns in the order of arp::Pattern, user last");

namespace {

constexpr std::int64_t kNever = INT64_MAX;
constexpr std::int32_t kReleaseVel = 64;

// The step length in ticks of a rate. An out-of-range enum value (a corrupt snapshot) falls back to the default 1/16
// rather than reading outside the table.
std::int64_t rateTicks(Rate r) {
  const auto i = static_cast<std::size_t>(r);
  return i < std::size(kRateTicks) ? kRateTicks[i] : kRateTicks[static_cast<std::size_t>(Rate::R1_16)];
}

// Finite = the exponent bits are not all ones. Done on the bits, not with std::isfinite / a comparison, so that the answer
// does not depend on the compiler's floating-point mode (the #error above is the first line of defence, this the second).
bool finiteBits(double v) {
  std::uint64_t b;
  std::memcpy(&b, &v, sizeof b);
  return ((b >> 52) & 0x7FF) != 0x7FF;
}

// SPEC 4.2: the length of one pass of an order style over e >= 1 held keys. `random` has no period of its own: e.
// `tables` is the orderTables plant (bounce borrows bounce2's period).
std::int64_t periodOf(Order o, int e, bool tables) {
  switch (o) {
    case Order::Bounce: return tables ? 2 * e : (e == 1 ? 1 : 2 * e - 2);
    case Order::Bounce2: return 2 * e;
    case Order::Chord: return 1;
    case Order::PedalLow: case Order::PedalHigh: return e == 1 ? 1 : 2 * (e - 1);
    case Order::ChordWalk: return e + 1;
    case Order::Stride: return 2;
    case Order::ChordBetween: return 2 * e;
    case Order::Pairs: return e == 1 ? 1 : e - 1;
    default: return e;   // up down played shuffle converge diverge random
  }
}

// converge: E[0], E[e-1], E[1], E[e-2], ...
std::int64_t convergeIndex(std::int64_t q, int e) { return q % 2 == 0 ? q / 2 : e - 1 - (q - 1) / 2; }

// SPEC 6: a key is in the scale iff the root-relative mask has bit mod(key - root, 12) set (horde's quantise()).
bool inScale(std::uint32_t mask, std::int32_t root, std::int64_t key) { return ((mask >> mod(key - root, 12)) & 1u) != 0; }

// SPEC 6: the nearest in-scale key within 0-127; a key already in the scale is unchanged. A tie between the key below and the
// key above goes toward the previous quantized output (lastQ); with none, or both candidates equally near it, to the LOWER key.
// A key past 0-127 (only a plant feeds one) with no candidate is returned unchanged for the caller's range check to drop.
std::int64_t quantizeKey(std::int64_t key, std::int32_t root, std::uint32_t mask, bool hasLast, std::int64_t last, bool tieDown) {
  if (inScale(mask, root, key)) return key;
  for (std::int64_t d = 1; d <= 127; ++d) {
    const std::int64_t lo = key - d, hi = key + d;
    const bool okLo = lo >= 0 && lo <= 127 && inScale(mask, root, lo), okHi = hi >= 0 && hi <= 127 && inScale(mask, root, hi);
    if (okLo && okHi) {
      const std::int64_t dl = lo > last ? lo - last : last - lo, dh = hi > last ? hi - last : last - hi;
      return (!tieDown && hasLast && dh < dl) ? hi : lo;
    }
    if (okLo) return lo;
    if (okHi) return hi;
  }
  return key;
}

// SPEC 4.7 stage 4: move n in-scale keys up (n > 0) or down, counting only keys the scale admits. A zero mask is the chromatic
// scale: a degree is one semitone. May return a key outside 0-127: stage 5 drops it.
std::int64_t degreeMove(std::int64_t key, std::int64_t n, std::int32_t root, std::uint32_t mask) {
  const std::int64_t dir = n > 0 ? 1 : -1;
  for (std::int64_t c = n < 0 ? -n : n; c > 0; --c) {
    do key += dir; while (key >= 0 && key <= 127 && mask != 0 && !inScale(mask, root, key));
    if (key < 0 || key > 127) return key;
  }
  return key;
}

std::int32_t normPick(std::int32_t v) { return v >= kPickOrder && v <= kPickChord ? v : kPickOrder; }

// SPEC 4.6: numeric fields clamp to their ranges, an invalid pick or unit becomes its default.
UserPattern validated(const UserPattern& in) {
  UserPattern v;
  v.length = static_cast<std::int32_t>(clampi(in.length, 1, kMaxUserSteps));
  v.stepSize = static_cast<std::int32_t>(clampi(in.stepSize, -48, 48));
  v.stepUnit = static_cast<std::uint8_t>(in.stepUnit) <= 1 ? in.stepUnit : StepUnit::Semitones;
  for (int i = 0; i < v.length; ++i) {   // steps past `length` stay default, so equal patterns compare equal
    const PatternStep& a = in.steps[i];
    PatternStep& d = v.steps[i];
    d.on = a.on;
    d.tie = a.tie;
    d.vel = static_cast<std::int32_t>(clampi(a.vel, 0, 200));
    d.gate = static_cast<std::int32_t>(clampi(a.gate, 1, 200));
    d.step = static_cast<std::int32_t>(clampi(a.step, -8, 8));
    d.prob = static_cast<std::int32_t>(clampi(a.prob, 0, 100));
    d.ratchet = static_cast<std::int32_t>(clampi(a.ratchet, 1, 4));
    d.trig = static_cast<std::int32_t>(clampi(a.trig, 0, 8));
    d.pick = normPick(a.pick);
  }
  return v;
}

// SPEC 1.1 order at one offset: offs, Panic, ons, triggers, pass-through.
int rankOf(EventType t) {
  switch (t) {
    case EventType::NoteOff: return 0;
    case EventType::Panic: return 1;
    case EventType::NoteOn: return 2;
    case EventType::Trigger: return 3;
    default: return 4;
  }
}

bool sortsBefore(const Event& a, const Event& b) {
  if (a.offset != b.offset) return a.offset < b.offset;
  const int ra = rankOf(a.type), rb = rankOf(b.type);
  if (ra != rb) return ra < rb;
  switch (a.type) {
    case EventType::NoteOff: return a.ref.id < b.ref.id;                       // ascending output id
    case EventType::NoteOn:  return a.stepSeq != b.stepSeq ? a.stepSeq < b.stepSeq : a.key < b.key;   // the step's order, then ascending key
    case EventType::Trigger: return a.lane < b.lane;
    default: return false;   // Panic / pass-through: input order (the sort below is stable)
  }
}

#ifdef ARP_PLANTS
#define ARP_PLANT(name) (plants.name)
#else
#define ARP_PLANT(name) (false)
#endif

}  // namespace

Core::Core(const Caps& caps) : caps_(caps) {
  caps_.held = static_cast<int>(clampi(caps_.held, 1, kMaxHeldKeys));
  caps_.refsPerKey = static_cast<int>(clampi(caps_.refsPerKey, 1, kMaxRefsPerKey));
  caps_.sounding = static_cast<int>(clampi(caps_.sounding, 1, kMaxSounding));
  caps_.pending = static_cast<int>(clampi(caps_.pending, 1, kMaxPending));
  if (caps_.events < 0) caps_.events = 0;
  resetSounding();
}

bool Core::prepare(int sampleRate, int maxBlock) {
  prepared_ = false;
  if (sampleRate < kMinSampleRate || sampleRate > kMaxSampleRate || maxBlock < 1 || maxBlock > kMaxBlock) return false;
  // T10: a note still owed a NoteOff is not forgotten: its ref moves to storage that survives prepare()
  // and is released at offset 0 of the next process() / reset().
  if (!ARP_PLANT(prepareForgetsNotes)) {
    int slots[kSoundingSlots];
    const int n = collectSounding(slots, true);   // in output-id order, as the dense table listed them
    for (int i = 0; i < n && owedN_ < static_cast<int>(owed_.size()); ++i) owed_[static_cast<std::size_t>(owedN_++)] = sounding_[static_cast<std::size_t>(slots[i])].ref;
  }
  sr_ = sampleRate;
  maxBlock_ = maxBlock;
  D_ = static_cast<std::int64_t>(60) * sr_ * 1000;
  capEv_ = caps_.events > 0 ? caps_.events : std::max(kMinEventsPerBlock, 4 * maxBlock_);
  out_.assign(static_cast<std::size_t>(capEv_), Event{});
  // SPEC 1.3: steps per block <= cdiv(maxBlock * 999 * TPQ, 60 * sr * 960) + 1; a forward re-base can add a late step or two.
  const std::int64_t bound = cdiv(static_cast<std::int64_t>(maxBlock_) * 999 * kTpq, static_cast<std::int64_t>(60) * sr_ * 960) + 1;
  steps_.assign(static_cast<std::size_t>(bound + 8), StepRecord{});
  counters_ = Counters{};
  resetState();
  // LOW-A: an owed off still holds its ref id; a note emitted after prepare() must never share one with it
  // (intake/note-vocabulary.md 1). The counter starts above the largest owed id (else at 0, SPEC 1.2).
  if (!ARP_PLANT(owedIdCollision))
    for (int i = 0; i < owedN_; ++i) idCounter_ = std::max(idCounter_, owed_[static_cast<std::size_t>(i)].id + 1);
#ifdef ARP_PLANTS
  if (plants.prepareResetsUser) {   // plant: the user pattern is "runtime state" and prepare() wipes it
    latest_ = UserPattern{};
    userSlots_[static_cast<std::size_t>(r_)] = UserPattern{};
    mid_.store(mid_.load() & 3);
  }
#endif
  prepared_ = true;
  return true;
}

void Core::resetState() {
  clearRefs();   // (chordSigValid_ is cleared below, so the chord stage cannot skip its next pass: no refsVer_ bump needed)
  ignoredN_ = 0;
  pedalDown_ = false;
  moduleOn_ = false;
  enabled_ = false;
  idCounter_ = 0;
  resetSounding();
  pendN_ = 0;
  pendHead_ = 0;
  absSample_ = 0;
  clock_ = Clock::None;
  start_ = I128{};
  next_ = I128{};
  inc_ = 0;
  L_ = 0;
  freeRunning_ = false;
  epoch_ = 0;
  lastTempoMilli_ = 120000;
  hasLastEvaluated_ = false;
  hasAnchor_ = false;
  lastEvaluated_ = 0;
  anchorK_ = 0;
  m_ = 0;
  stepSeq_ = 0;
  if (!ARP_PLANT(lastQNoReset)) hasLastQ_ = false;   // SPEC 6: lastQ resets at reset / prepare too (T10)
  hasLastBar_ = false;
  swingPairK_ = INT64_MIN;
  lastBar_ = 0;
  barTicks_ = 4 * kTpq;
  barPhase_ = 0;
  repeatSteps_ = 0;
  exhausted_ = false;
  lastC_ = 0;
  hasLastC_ = false;
  chordRecN_ = 0;
  for (ChordNote& n : chordNote_) n = ChordNote{};
  chordSetN_ = 0;
  chordSigValid_ = false;
#ifdef ARP_PLANTS
  chordDrift_.fill(0);
#endif
  timeMode_ = false;
  D_ = static_cast<std::int64_t>(60) * sr_ * 1000;
  gO_ = 0;
  gK1_ = 0;
  haveActive_ = false;
  havePrevCont_ = false;
#ifdef ARP_PLANTS
  haveFnext_ = false;
#endif
}

bool plantsCompiledIntoLibrary() {
#ifdef ARP_PLANTS
  return true;
#else
  return false;
#endif
}

void Core::plantAlloc() {
#ifdef ARP_PLANTS
  // Calls the allocation FUNCTION directly and parks the pointer in a volatile: a new-expression
  // whose result is unused may be elided by the compiler, which would make the A1 plant vacuous.
  static void* volatile sink;
  void* p = ::operator new(16);
  sink = p;
  (void)sink;
  ::operator delete(p);
#endif
}

std::uint32_t Core::draw(std::uint32_t seed, std::int64_t x, std::uint32_t lane) const {
#ifdef ARP_PLANTS
  static std::uint32_t plantedTicks = 0;   // shared across instances ON PURPOSE: two same-seed runs must differ
  if (plants.timeInDraw) seed ^= plantedTicks++;
#endif
  return arp::draw(seed, x, lane, ARP_PLANT(drawIgnoresEpoch) ? 0 : epoch_);
}

std::size_t Core::heldKeyCount() const {
  HeldKey tmp[kMaxHeldKeys];
  return static_cast<std::size_t>(collectHeld(tmp));
}

// SPEC 4.6, writer side. Validate, remember as the latest copy, and publish through the triple buffer: fill the writer's
// private slot, then ONE atomic exchange hands it over as the fresh slot and returns the slot to write next. No lock,
// nothing tears, a later call simply replaces an unconsumed earlier one. The audio thread never sees a slot being written.
void Core::setUserPattern(const UserPattern& p) {
  const UserPattern v = validated(p);
#ifdef ARP_PLANTS
  if (plants.userFirstWins && (mid_.load(std::memory_order_acquire) & kFresh)) return;   // plant: a write is dropped while an earlier one is unconsumed
#endif
  latest_ = v;
#ifdef ARP_PLANTS
  if (plants.swapMidStep) {   // plant: write the READER's slot in place, so an edit can land in the middle of a block
    userSlots_[static_cast<std::size_t>(r_)] = v;
    return;
  }
#endif
  userSlots_[static_cast<std::size_t>(w_)] = v;
  w_ = mid_.exchange(w_ | kFresh, std::memory_order_acq_rel) & 3;
}

// ------------------------------------------------------------------------------------------ time

std::int64_t Core::curL() const {
  return timeMode_ ? clampi(S_.stepTimeUs, 10000, 2000000) : rateTicks(S_.rate);
}

// SPEC 2.6: step_time_us changed (read at the block start). The next unevaluated step k1 = lastEvaluated + 1 lands at
// max(lastTick + L_new, changeTick), later steps are spaced L_new: integer-exact, no burst, kAbs / m / swing parity carry
// on, sounding notes keep their offs (T18). changeTick is the block-start position rounded up to a whole microsecond.
void Core::timeRegrid() {
  const std::int64_t Lnew = curL();
  if (Lnew == L_) return;
  if (ARP_PLANT(regridAbsolute)) return;   // plant: handled in visit() on absolute multiples
  if (soundN_ > 0) ++counters_.rows[18];
  if (hasLastEvaluated_ && lastEvaluated_ >= 0 && !ARP_PLANT(regridKAbs)) {   // a change before any step of the gesture just sets L (13b.10)
    const std::int64_t lastTick = gtick(lastEvaluated_);
    const std::int64_t changeTick = start_.ceilDiv(D_);
    gO_ = std::max(lastTick + Lnew, changeTick);
    gK1_ = lastEvaluated_ + 1;
  }
  L_ = Lnew;
}

// Plant regridAbsolute: r2.8's behaviour, a re-grid on absolute multiples of the new step time at the old step's tick.
void Core::regridAbsolute(std::int64_t kAbs) {
  const std::int64_t oldTick = kAbs * L_;
  L_ = curL();
  const std::int64_t kNew = cdiv(oldTick, L_);
  lastEvaluated_ = kNew - 1;
  if (hasAnchor_) anchorK_ = kNew - (kAbs - anchorK_);
}

// SPEC 2.3: s = cdiv(x - start, inc). Negative for a LATE event (after a forward re-base); callers clamp to 0.
std::int64_t Core::ofs(I128 x) const {
#ifdef ARP_PLANTS
  if (plants.floatPosition) {
    const double v = std::ceil((x.toDouble() / static_cast<double>(D_) - fstart_) / tps_);
    return v > 4e18 ? INT64_MAX : v < -4e18 ? INT64_MIN : static_cast<std::int64_t>(v);
  }
  if (plants.roundNotCeil) {
    const I128 d = x - start_;
    return (d + d + I128::fromInt(inc_)).floorDiv(2 * inc_);
  }
#endif
  return (x - start_).ceilDiv(inc_);
}

std::int64_t Core::tempoMilli(const Transport& tr) {
  if (tr.hasTempo && finiteBits(tr.tempo)) {   // NaN / inf: as if no tempo were reported (the last valid one stays)
    double bpm = tr.tempo;
    if (!(bpm > 1)) bpm = 120;   // horde's rule: a tempo <= 1 means 120
    bpm = std::min(999.0, std::max(20.0, bpm));
    lastTempoMilli_ = static_cast<std::int64_t>(std::llround(bpm * 1000));
  }
  return lastTempoMilli_;   // no tempo: the last valid one, else 120
}

// The floating `beats` fallback is usable only if it is finite and small enough that every cast below is defined.
// 1e10 quarter notes is about 1.9 years at 120 bpm, far beyond any session.
static bool beatsUsable(const Transport& tr) {
  return tr.hasRep || (finiteBits(tr.beats) && std::fabs(tr.beats) <= 1e10);
}

I128 Core::repOf(const Transport& tr) const {
  if (tr.hasRep) return tr.rep;
  // Only the floating fallback rounds (SPEC 13.13): half up, once.
  const double ticks = tr.beats * kTpq, whole = std::floor(ticks);
  const auto frac = static_cast<std::int64_t>(std::floor((ticks - whole) * static_cast<double>(D_) + 0.5));
  return I128::mul(static_cast<std::int64_t>(whole), D_) + I128::fromInt(frac);
}

// T6: release everything, restart the grid from the new position, re-anchor.
void Core::edge() {
  if (soundN_ - chordSoundN_ > 0) ++counters_.rows[6];
  releaseArp(0);   // T6 ends the ARP's notes; chord notes behave like held keys and ignore transport edges (SPEC 14, T21)
#ifdef ARP_PLANTS
  if (plants.chordEndsAtEdge) { chordEndAll(0); chordSync(0); }   // plant: the r2.10 behaviour, chord notes end and restart at an edge
#endif
  swingPairK_ = INT64_MIN;  // the step numbering restarts: no pair partner survives an edge (SPEC 5.1)
  // SPEC 4.8 (E12, D-028): T6 restarts the repeat count and clears `exhausted` only while keys are held. With none, a loop or jump must neither revive a used-up arp
  // nor start a fresh round of trigger-only steps: a round in progress finishes and stops. H' is empty iff H is (every held key keeps its +0 slot).
  // Plants: edgeResetsCountWhileEmpty (resets the count anyway); edgeRevivesWhileEmpty (the M2 behaviour: resets the count AND clears `exhausted`; clearing the flag alone
  // would be re-latched at once, the count still being spent).
  if (refN_ > 0 || ARP_PLANT(edgeResetsCountWhileEmpty) || ARP_PLANT(edgeRevivesWhileEmpty)) repeatSteps_ = 0;
  if (refN_ > 0 || ARP_PLANT(edgeRevivesWhileEmpty)) exhausted_ = false;
  active_.rate = S_.rate;   // SPEC-GAP: the rate is adopted immediately at an edge (13.8)
  L_ = curL();
  gO_ = 0;
  gK1_ = 0;
  freeRunning_ = false;
  epoch_ = 0;               // under the host clock epoch = 0 (SPEC 2.4)
  hasLastEvaluated_ = false;
  reanchor(0, false);
}

// m := 0 at every anchor (SPEC 4.5, H8). Restart `chord` anchors only if an anchor already
// exists or keys are held (SPEC 13.5): steps run only once there is a reason to play.
void Core::reanchor(std::int32_t s, bool resetEvaluated) {
  if (resetEvaluated || !hasLastEvaluated_) {
    lastEvaluated_ = kAtOrAfter(posAt(s)) - 1;
    hasLastEvaluated_ = true;
  }
  m_ = 0;
  // A new anchor forgets the bar bookkeeping: the next evaluated step re-derives the bar anchor even when it lands in the SAME bar
  // index as before (a loop wrap or a jump back into the bar). Plant reanchorKeepsLastBar keeps it, so that landing keeps `reanchor`'s step.
  if (!ARP_PLANT(reanchorKeepsLastBar)) hasLastBar_ = false;
  switch (restartMode()) {
    case Restart::Free: anchorK_ = 0; hasAnchor_ = true; break;   // tick 0 of the active clock (SPEC 4.5)
    case Restart::Bar: anchorK_ = lastEvaluated_ + 1; hasAnchor_ = true; break;   // evalStep re-derives it from the bar grid at the next evaluated step
    default:
      if (hasAnchor_ || refN_ > 0) {
        anchorK_ = lastEvaluated_ + 1;
        hasAnchor_ = true;
      } else {
        hasAnchor_ = false;
      }
  }
}

// SPEC 4.5 / 2.6: the restart mode in force. Restart is INERT in time mode (there are no bars in time): bar and free behave as
// chord. Plants: timeBarsCount applies restart in time mode (TM3); restartBarAsChord / restartFreeAsChord ignore a mode (RP1/Q-A2).
Restart Core::restartMode() const {
  const Restart r = active_.restart;
  if (timeMode_ && !ARP_PLANT(timeBarsCount)) return Restart::Chord;
  if (r == Restart::Bar && ARP_PLANT(restartBarAsChord)) return Restart::Chord;
  if (r == Restart::Free && ARP_PLANT(restartFreeAsChord)) return Restart::Chord;
  return r;
}

// H7b: the free clock starts at a switch / enable sample when keys are held.
void Core::maybeFreeStart(std::int32_t s) {
  if (clock_ == Clock::Free && !freeRunning_ && enabled_ && refN_ > 0) freeStart(s);
}

// H7a / H7c: posNum := 0 at sample s; every pending event and sounding off shifts by the same exact
// delta, so their remaining durations are exact and no terminal fires (T14).
void Core::freeStart(std::int32_t s) {
  const I128 shift = posAt(s);
  if (freeRunning_ && (soundN_ > 0 || pendN_ > 0)) ++counters_.rows[14];
  start_ -= shift;
  dropMemos();
  for (Sounding& x : sounding_)   // every key moves by the same amount, so the order of sched_ and of the pending ring is kept
    if (x.used && x.hasOff) x.offPos -= shift;
  for (int i = 0; i < pendN_; ++i) {
    pendAt(i).pos -= shift;
    pendAt(i).offPos -= shift;
  }
  epoch_ += 1;   // uint32: wraps mod 2^32
  freeRunning_ = true;
  lastEvaluated_ = -1;
  hasLastEvaluated_ = true;
  gO_ = 0;           // time mode: step 0 lands on the press
  gK1_ = 0;
  m_ = 0;
  swingPairK_ = INT64_MIN;
  hasLastBar_ = false;   // restart bar: the bars are re-derived from the new origin at the next evaluated step
  anchorK_ = 0;   // the gesture anchors at tick 0 of the new origin
  hasAnchor_ = true;
}

// ------------------------------------------------------------------------------------------ process

EventSpan Core::process(const Block& b) {
  outN_ = 0;
  stepsN_ = 0;
  if (!prepared_ || b.n < 1 || b.n > maxBlock_) {
    ++counters_.badInput;
    return {};
  }
  if (ARP_PLANT(allocInProcess)) plantAlloc();
  flushOwed();
  n_ = b.n;
  S_ = b.settings;
  scale_ = b.scale;   // SPEC 6: horde's scale is read, never owned; steps evaluated in this block quantize to it
  absBase_ = absSample_;
  startBlockOffs();
  const Transport& tr = b.transport;
  // SPEC 4.6: a published user pattern is taken ONCE per process(): it applies from the first step evaluated in this block
  // and never mid-block (T17). The audio thread never touches the writer's slot.
  if (mid_.load(std::memory_order_acquire) & kFresh) {
    r_ = mid_.exchange(r_, std::memory_order_acq_rel) & 3;
    ++counters_.rows[17];
  }
  // SPEC 2.6: in time mode a tick is one microsecond (D = sr, inc = 10^6 per sample) and the host's tempo, position and
  // transport are ignored. Switching mode changes the unit of every stored position, so it is a clock switch (T6 / T19).
  const bool tm = S_.rateMode == RateMode::Time;
  bool modeSwitch = false;
  if (tm != timeMode_) {
    modeSwitch = clock_ != Clock::None;
    timeMode_ = tm;
    D_ = tm ? sr_ : static_cast<std::int64_t>(60) * sr_ * 1000;
  }
  inc_ = tm ? (ARP_PLANT(timeUsesTempo) ? tempoMilli(tr) * 1000000 / 120000 : 1000000) : tempoMilli(tr) * kTpq;
  if (!haveActive_) {   // the first block after prepare / reset adopts every setting at once
    active_ = S_;
    haveActive_ = true;
    L_ = curL();
  }

  // -- which clock, and where it starts (SPEC 2.2 three bands, 2.4)
  bool edgeNow = modeSwitch;
  if (modeSwitch) {
    start_ = I128{};
    if (soundN_ > 0) ++counters_.rows[19];
  }
  if (!tm && tr.playing && tr.hasBeats && beatsUsable(tr)) {   // unusable beats (NaN, inf, absurd): no beats timeline, so the free clock
    const I128 rep = repOf(tr);
    if (clock_ != Clock::Host) {
      edgeNow = true;
      start_ = rep;
    } else {
      const I128 delta = rep - next_;
      const I128 mag = delta.isNegative() ? -delta : delta;
      if (delta == I128{}) start_ = next_;                              // continuous
      else if (mag <= I128::mul(960, D_)) start_ = rep;                 // re-base: silent, no terminals (1/16 beat)
      else { edgeNow = true; start_ = rep; }                            // jump: T6
    }
    clock_ = Clock::Host;
  } else {
    if (clock_ == Clock::Host) {
      edgeNow = true;
      start_ = I128{};
    } else if (!modeSwitch) {
      start_ = clock_ == Clock::None ? I128{} : next_;
    }
    clock_ = Clock::Free;   // the time clock IS the free clock, in microsecond units (SPEC 2.6)
  }
  // SPEC 4.5 bars: from barStartBeats (a PHASE, 13.12) when the host clock gives one, else from tick 0 of the active clock, with the
  // transport's time signature (a non-positive part reads as 4/4). SPEC-GAP: on the free clock (transport stopped or absent) the
  // bar phase is 0, since a stopped transport's barStartBeats says nothing about the key press the free clock counts from. Time mode
  // has no bars (restart is inert); only the timeBarsCount plant counts bars of 61,440 us.
  if (tm) {
    barTicks_ = 4 * kTpq;
    barPhase_ = 0;
  } else {
    const std::int64_t num = tr.timeSigNum > 0 && !ARP_PLANT(meterAs44) ? tr.timeSigNum : 4, den = tr.timeSigDen > 0 && !ARP_PLANT(meterAs44) ? tr.timeSigDen : 4;   // plant meterAs44: every meter reads as 4/4
    barTicks_ = std::max<std::int64_t>(1, (static_cast<std::int64_t>(4) * kTpq * num) / den);
    const bool phased = clock_ == Clock::Host && tr.hasBarStart && finiteBits(tr.barStartBeats) && std::fabs(tr.barStartBeats) <= 1e10;
    barPhase_ = phased ? mod(std::llround(tr.barStartBeats * kTpq), barTicks_) : 0;
  }
  if (ARP_PLANT(timeAnchorsTransport) && tm) freeRunning_ = true;   // plant: the time grid runs from the start of the run, not from the key press
#ifdef ARP_PLANTS
  if (plants.floatPosition) {
    tps_ = static_cast<double>(inc_) / static_cast<double>(D_);
    fstart_ = (!edgeNow && haveFnext_) ? static_cast<double>(fnext_) : start_.toDouble() / static_cast<double>(D_);
  }
#endif
  if (edgeNow) edge();
  if (ARP_PLANT(noOnceGuard)) {   // plant: re-derive the evaluated-step cursor from the position every block
    lastEvaluated_ = kAtOrAfter(start_) - 1;
    hasLastEvaluated_ = true;
  }

  // -- T9 bookkeeping, then the settings that act at offset 0: enabled, arp_enabled, latch
  const std::array<std::int64_t, 4> cont = {S_.gate, S_.swing, S_.velFixed, S_.stepTimeUs};
  if (havePrevCont_ && cont != prevCont_ && soundN_ > 0) ++counters_.rows[9];
  prevCont_ = cont;
  havePrevCont_ = true;

  const bool modOn = S_.enabled, arpNow = modOn && S_.arpEnabled;
  if (modOn != moduleOn_) {
    if (!modOn) {
      if (soundN_ > 0) ++counters_.rows[4];
      releaseAll(0);
    }
    moduleOn_ = modOn;
  }
  if (arpNow != enabled_) {
    if (!arpNow) {
      if (modOn) {   // the arp alone off: its notes end and its pending queue clears; chord notes are untouched (SPEC 14, T21)
        if (soundN_ - chordSoundN_ > 0) ++counters_.rows[4];
        if (ARP_PLANT(arpToggleEndsChord)) releaseAll(0); else releaseArp(0);
      }
      enabled_ = false;
    } else {
      ++counters_.rows[5];
      enabled_ = true;
      active_.rate = S_.rate;   // SPEC-GAP: the rate is adopted at enable (the grid restarts anyway)
      L_ = curL();
      reanchor(0, true);
      // SPEC 13b.17 / 2.4 H7b: turning the arp on with keys held under the free clock starts the clock at offset 0
      // (epoch + 1), even if it was left "running" while disabled: otherwise the grid keeps its old origin and the
      // draws replay the old epoch.
      if (clock_ == Clock::Free && refN_ > 0) freeStart(0);
    }
  }
  if (!(ARP_PLANT(timeEntryNoStart) && modeSwitch)) maybeFreeStart(0);   // a clock switch with keys held (T6 edge, or entering time mode) starts the free clock too
  if (timeMode_) timeRegrid();
  // SPEC 13b.12: `latch` applies at OFFSET 0 of its block (it changes H); turning it off drops the LATCHED refs here.
  if (!ARP_PLANT(latchAtNextStep) && S_.latch != active_.latch) {
    if (S_.latch == Latch::Off) eraseRefs([](const Ref& r) { return r.state == RefState::Latched; });
    switchTouch(0);
    active_.latch = S_.latch;
  }

  // SPEC 14: the chord_* settings, arp_enabled and the scale settings apply at offset 0 of the block whose snapshot changes them.
  // But when the first valid input sits at offset 0 too, the sync waits for the one after that sample's last input: the re-map and
  // the held-set change are then ONE pass, so a source count going 1 -> 0 -> 1 at that sample keeps its note (no off / on pair) and a
  // note whose only source is released at that sample never starts (no 1-sample note). Plant chordSyncBlockStartFirst syncs first.
  bool firstInputAtZero = false;
  for (const Event& e : b.in)
    if (e.offset >= 0 && e.offset < n_) { firstInputAtZero = e.offset == 0; break; }
  if (!firstInputAtZero || ARP_PLANT(chordSyncBlockStartFirst)) chordSync(0);

  // -- the timeline: input events, due offs, pending ons, grid-step evaluations, in time order
  dropMemos();   // start_ and inc_ are final for this block (freeStart drops them again when it moves start_)
  std::size_t ii = 0;
  std::int64_t cur = 0;
  for (;;) {
    while (ii < b.in.size() && (b.in[ii].offset < 0 || b.in[ii].offset >= n_)) {   // SPEC 1: offsets are never clamped; an outside one is refused
      ++ii;
      ++counters_.badInput;
    }
    if (clock_ == Clock::Free && freeRunning_ && refN_ == 0 && soundN_ == 0 && pendN_ == 0) freeRunning_ = false;   // idle (SPEC 2.4)
    // SPEC 2.5 / 2.6: in TIME mode, while H is empty the time clock evaluates NO grid step (no notes, no triggers, trigger-only
    // steps included) even if a note still rings or a sub-note is pending; those still sound and end on time (offs are never
    // dropped). The clock itself keeps running until idle (SPEC 2.4) and the next gesture start re-bases it. Sync mode is
    // unchanged: the free clock steps while anything sounds or is pending, the host clock regardless of H.
    const bool timeHeldOnly = timeMode_ && refN_ == 0 && !ARP_PLANT(timeStepsWhileRinging);
    // plant exhaustedStopsGuard: a spent arp stops consuming the once-guard (SPEC 4.8, r4 H4: a burst of overdue steps at the next gesture)
    const bool running = enabled_ && (clock_ == Clock::Host || freeRunning_) && !timeHeldOnly && !(ARP_PLANT(exhaustedStopsGuard) && exhausted_);
    if (running && !hasLastEvaluated_) {
      lastEvaluated_ = kAtOrAfter(posAt(static_cast<std::int32_t>(cur))) - 1;
      hasLastEvaluated_ = true;
    }
    // The next thing to happen. On a tie the earlier kind wins: input, then offs, then pending ons, then the grid step.
    std::int64_t t = kNever;
    int kind = -1;
    if (ii < b.in.size()) { t = b.in[ii].offset; kind = 0; }
    const std::int64_t offT = nextOffTime();   // kNever when no off is due
    if (offT < t) { t = offT; kind = 1; }
    if (pendN_ > 0) {   // the front of the ring is the soonest, the earliest queued among equals
      const std::int64_t s = std::max<std::int64_t>(0, ofsMemo(memoPend_, pendAt(0).pos));
      if (s < t) { t = s; kind = 2; }
    }
    if (running) {
      const I128 stepPos = I128::mul(gtick(lastEvaluated_ + 1), D_);
      const std::int64_t s = std::max<std::int64_t>(0, ofsMemo(memoStep_, stepPos));
      // SPEC 5.1 (E10): "at or after the step's on" includes a sub-note landing in the SAME sample as the step: the step is evaluated first so it can
      // drop that sub-note (it has no NoteOn yet), else the sub-note sounds first and the step's own on retriggers its key at that sample. Only TIME
      // mode drops, so only there does the tie-break apply. Swing also queues a sub-note at or after the next step's GRID tick (stepPos), in both
      // modes: an even step's span runs to the odd step's SWUNG on, so its later sub-notes can sit past the odd step's grid tick. The tie-break is
      // still right there: evaluating the step first reorders nothing the output shows, and the drop compares against the step's SWUNG on (on0), so a
      // sub-note between the grid tick and the swung on survives and is emitted in its own time; only one at or after the swung on (straight swing:
      // the grid tick itself) is dropped, which is what E10 asks. In sync mode nothing drops, so the earlier-kind-wins order stands.
      const bool dropsTie = s == t && kind == 2 && timeMode_ && !ARP_PLANT(keepSpilledRatchets) && pendAt(0).lane == 0 && !(pendAt(0).pos < stepPos);
      if (s < t || dropsTie) { t = s; kind = 3; }
    }
    const bool inWindow = ARP_PLANT(clampOffset) ? t <= n_ : t < n_;   // half-open window; the plant lets an event at n through
    if (kind < 0 || !inWindow) break;
    if (t < cur) t = cur;   // a LATE event (SPEC 13.1) is emitted now
    cur = t;
    const auto so = static_cast<std::int32_t>((ARP_PLANT(clampOffset) && t >= n_) ? n_ - 1 : t);
    if (kind == 0) {
      input(b.in[ii++], so);
      // The chord stage follows H sample-exactly, but syncs after the LAST input event of a sample, so a key count going 1 -> 0 -> 1
      // inside one sample keeps its note (SPEC 14, r4 M3). Plant chordSyncPerEvent syncs after every event.
      if (ARP_PLANT(chordSyncPerEvent) || !(ii < b.in.size() && b.in[ii].offset == b.in[ii - 1].offset)) {
        chordSync(so);
        updateCycle();   // SPEC 4.8 (E11): H' may have just become non-empty
      }
    } else if (kind == 1) {
      // Every off due at this offset goes out now, in ascending output id. Nothing can come between them: at a tie an input goes first (so none is left
      // at this offset), and a pending on or a grid step only goes after the offs, so what the old one-off-per-iteration loop did is exactly this.
      int due[kSoundingSlots];
      const int nd = takeDueOffs(offT, due);
      for (int q = 0; q < nd; ++q) {
        if (sounding_[static_cast<std::size_t>(due[q])].hasOff) ++counters_.rows[1];
        releaseSlot(due[q], so);
      }
    } else if (kind == 2) {
      const Pending p = popPending();
      if (p.lane > 0) {   // a TRIGGER (SPEC 8): a performance event with no state and no terminal
        Event ev;
        ev.type = EventType::Trigger;
        ev.offset = so;
        ev.lane = p.lane;
        ev.tick = tickOf(p.pos);
        pushNonNote(ev);
      } else {
        emitOn(p, so);
      }
    } else {
      visit(so);
    }
  }

  next_ = start_ + I128::mul(n_, inc_);
#ifdef ARP_PLANTS
  if (plants.floatPosition) {
    fnext_ = static_cast<float>(fstart_ + static_cast<double>(n_) * tps_);
    haveFnext_ = true;
  }
#endif
  absSample_ += n_;
  sortEvents();
  return {out_.data(), static_cast<std::size_t>(outN_)};
}

EventSpan Core::reset() {
  outN_ = 0;
  stepsN_ = 0;
  if (!prepared_) return {};
  if (ARP_PLANT(allocInReset)) plantAlloc();
  flushOwed();
  absBase_ = absSample_;
  n_ = 1;
  if (soundN_ > 0) ++counters_.rows[10];
  releaseAll(0);
  sortEvents();
  const EventSpan offs{out_.data(), static_cast<std::size_t>(outN_)};
  resetState();
  return offs;
}

// T10 across prepare(): the NoteOffs for notes that were sounding when prepare() was called, at offset 0.
void Core::flushOwed() {
  if (owedN_ == 0) return;
  ++counters_.rows[10];
  for (int i = 0; i < owedN_; ++i) {
    Event ev;
    ev.type = EventType::NoteOff;
    ev.offset = 0;
    ev.ref = owed_[static_cast<std::size_t>(i)];
    ev.vel = kReleaseVel;
    if (!pushEvent(ev)) ++counters_.offsLost;
  }
  owedN_ = 0;
}

void Core::sortEvents() {
  // The events come out of the timeline in nondecreasing offset order (only a plant breaks that), so what is left to fix is the order WITHIN one offset:
  // offs by id, then Panic, ons by (step, key), triggers by lane, pass-through last, and equal keys in emission order (stable; pass-through keeps input order).
  // The usual disorder is a retrigger or an eviction putting its NoteOff right before the NoteOn that caused it: [off on off on ...] at one offset, which a
  // plain insertion sort fixes by moving every off past all the ons before it, quadratic in the size of a chord step (1.4 of the 2.9 us a step cost under load).
  // One pass: find each run of equal offsets, note whether it is out of order, sort it when it is. An offset that goes DOWN (a plant) sends the whole buffer
  // to the insertion sort, which does not mind (segments already sorted stay sorted).
  int segStart = 0;
  bool bad = false;
  for (int i = 1; i <= outN_; ++i) {
    if (i < outN_ && out_[static_cast<std::size_t>(i)].offset == out_[static_cast<std::size_t>(segStart)].offset) {
      if (sortsBefore(out_[static_cast<std::size_t>(i)], out_[static_cast<std::size_t>(i - 1)])) bad = true;
      continue;
    }
    if (i < outN_ && out_[static_cast<std::size_t>(i)].offset < out_[static_cast<std::size_t>(segStart)].offset) { insertionSort(0, outN_); return; }
    if (bad) sortSegment(segStart, i);
    segStart = i;
    bad = false;
  }
}

void Core::insertionSort(int from, int to) {   // stable, near-linear on nearly sorted data
  for (int i = from + 1; i < to; ++i) {
    if (!sortsBefore(out_[static_cast<std::size_t>(i)], out_[static_cast<std::size_t>(i - 1)])) continue;   // already in place (the usual case): no copy
    const Event e = out_[static_cast<std::size_t>(i)];
    int j = i;
    while (j > from && sortsBefore(e, out_[static_cast<std::size_t>(j - 1)])) {
      out_[static_cast<std::size_t>(j)] = out_[static_cast<std::size_t>(j - 1)];
      --j;
    }
    out_[static_cast<std::size_t>(j)] = e;
  }
}

// Sorts the events [from, to), all at one offset and out of order (sortEvents found that). A short one is insertion-sorted. A long, disordered
// one (up to kSortScratch events: a step's worth) is sorted through a compact key per event: sortsBefore's keys (kind, then id / step and key / lane) and the
// event's position as the last key, so equal keys keep emission order, on 24 bytes instead of an 80-byte event behind an index. The events are then moved
// into place by following the permutation's cycles (one spare Event, no allocation). A longer run than that, which no capacity of the core can produce at
// one offset except pass-through events (the host's own count), falls back to the insertion sort: slower, equally right.
void Core::sortSegment(int from, int to) {   // the caller has seen that [from, to) is out of order
  const int n = to - from;
  if (n <= 24 || n > kSortScratch) { insertionSort(from, to); return; }
  struct Key { std::int64_t a; std::int32_t b, idx; std::int32_t rank; };
  Key keys[kSortScratch];
  for (int i = 0; i < n; ++i) {
    const Event& e = out_[static_cast<std::size_t>(from + i)];
    Key& k = keys[i];
    k.rank = rankOf(e.type);
    k.idx = i;
    k.a = 0;
    k.b = 0;
    switch (e.type) {   // the tie-break columns of sortsBefore, per kind
      case EventType::NoteOff: k.a = e.ref.id; break;
      case EventType::NoteOn: k.a = e.stepSeq; k.b = e.key; break;
      case EventType::Trigger: k.a = e.lane; break;
      default: break;
    }
  }
  std::sort(keys, keys + n, [](const Key& x, const Key& y) { return std::tie(x.rank, x.a, x.b, x.idx) < std::tie(y.rank, y.a, y.b, y.idx); });
  Event* ev = out_.data() + from;
  for (int i = 0; i < n; ++i) {   // keys[k].idx = which event now sorts k-th: new[k] = old[keys[k].idx]
    if (keys[i].idx == i) continue;
    const Event first = ev[i];
    int j = i;
    for (;;) {
      const int src = keys[j].idx;
      keys[j].idx = j;
      if (src == i) { ev[j] = first; break; }
      ev[j] = ev[src];
      j = src;
    }
  }
}

// ------------------------------------------------------------------------------------------ input (SPEC 3)

template <class Pred>
void Core::eraseRefs(Pred pred) {
  int i = 0;
  while (i < refN_ && !pred(refs_[static_cast<std::size_t>(i)])) ++i;
  if (i == refN_) return;   // nothing to remove: H is unchanged, so refsVer_ (and the chord stage's skip) must not move
  ++refsVer_;
  int w = i;
  for (; i < refN_; ++i) {
    const Ref& r = refs_[static_cast<std::size_t>(i)];
    if (pred(r)) refGone(r);
    else refs_[static_cast<std::size_t>(w++)] = r;
  }
  refN_ = w;
}

void Core::addRef(const Ref& r) {
  refs_[static_cast<std::size_t>(refN_++)] = r;
  ++refsVer_;   // a ref joined H
  markDirty(r.key);
  if (static_cast<std::uint32_t>(r.key) < 128u) { if (keyRefCnt_[static_cast<std::size_t>(r.key)]++ == 0) ++keysHeld_; }
  else ++refsOutOfRange_;
}

void Core::refGone(const Ref& r) {
  markDirty(r.key);
  if (static_cast<std::uint32_t>(r.key) < 128u) { if (--keyRefCnt_[static_cast<std::size_t>(r.key)] == 0) --keysHeld_; }
  else --refsOutOfRange_;
}

void Core::clearRefs() {
  refN_ = 0;
  dirtyAll_ = true;   // every key's refs went at once: the chord stage rebuilds
  keyRefCnt_.fill(0);
  keysHeld_ = 0;
  refsOutOfRange_ = 0;
}

int Core::refCountOf(std::int32_t key) const {
  if (static_cast<std::uint32_t>(key) < 128u) return keyRefCnt_[static_cast<std::size_t>(key)];
  int n = 0;
  for (int i = 0; i < refN_; ++i) n += refs_[static_cast<std::size_t>(i)].key == key;
  return n;
}

int Core::distinctHeld() const {
  int n = keysHeld_;
  if (refsOutOfRange_ == 0) return n;
  // a key outside 0..127 is held (a host bug): count the distinct ones among those entries, bounded by the table
  for (int i = 0; i < refN_; ++i) {
    const std::int32_t k = refs_[static_cast<std::size_t>(i)].key;
    if (static_cast<std::uint32_t>(k) < 128u) continue;
    bool seen = false;
    for (int j = 0; j < i && !seen; ++j) seen = refs_[static_cast<std::size_t>(j)].key == k;
    if (!seen) ++n;
  }
  return n;
}

bool Core::refIgnored(const NoteRef& r) const {
  for (int i = 0; i < ignoredN_; ++i)
    if (ignored_[static_cast<std::size_t>(i)] == r) return true;
  return false;
}

// SPEC-GAP: the "ignored" list holds 256 refs: the oldest is forgotten when it fills. It only stops a repeated NoteOn
// of a still-held ignored ref from being taken as new; the ref's NoteOff removes it (unignoreRef).
void Core::ignoreRef(const NoteRef& r) {
  if (ignoredN_ == kIgnoredRing) {   // full: forget the oldest
    for (int i = 1; i < ignoredN_; ++i) ignored_[static_cast<std::size_t>(i - 1)] = ignored_[static_cast<std::size_t>(i)];
    --ignoredN_;
  }
  ignored_[static_cast<std::size_t>(ignoredN_++)] = r;
}

// The ignored ref's NoteOff has arrived: the ref is free again. Without this a host that reuses one ref per key
// (no note ids) could never press that key again after a refused press.
bool Core::unignoreRef(const NoteRef& r) {
  for (int i = 0; i < ignoredN_; ++i)
    if (ignored_[static_cast<std::size_t>(i)] == r) {
      for (int j = i + 1; j < ignoredN_; ++j) ignored_[static_cast<std::size_t>(j - 1)] = ignored_[static_cast<std::size_t>(j)];
      --ignoredN_;
      return true;
    }
  return false;
}

void Core::input(const Event& e, std::int32_t s) {
  switch (e.type) {
    case EventType::NoteOn:
      noteOn(e, s);
      if (soundN_ > 0) ++counters_.rows[3];
      break;
    case EventType::NoteOff:
      noteOff(e);
      if (soundN_ > 0) ++counters_.rows[3];
      break;
    case EventType::Pedal:
      pedal(e.down);
      if (soundN_ > 0) ++counters_.rows[3];
      break;
    case EventType::Panic: {   // T7: release-all, clear H, latch state and the pedal, then forward Panic after the offs
      if (soundN_ > 0) ++counters_.rows[7];
      // SPEC 4.8 (E12): T7 resets the count and clears `exhausted` only while H' is non-empty. Panic empties H (and so H'), so neither happens: the count in
      // progress runs on and stops where an uninterrupted run would. Plants edgeResetsCountWhileEmpty / edgeRevivesWhileEmpty do each anyway.
      if (ARP_PLANT(edgeResetsCountWhileEmpty) || ARP_PLANT(edgeRevivesWhileEmpty)) repeatSteps_ = 0;
      if (ARP_PLANT(edgeRevivesWhileEmpty)) exhausted_ = false;
      clearRefs();
      ++refsVer_;   // H lost every key
      ignoredN_ = 0;
      pedalDown_ = false;
      releaseAll(s);
      Event p;
      p.type = EventType::Panic;
      p.offset = s;
      pushNonNote(p);   // SPEC 13.9: forwarded even while disabled
      break;
    }
    default: {   // pass-through: unchanged, at its offset
      Event p = e;
      p.offset = s;
      p.type = EventType::Other;
      pushNonNote(p);
    }
  }
}

void Core::noteOn(const Event& e, std::int32_t s) {
  const NoteRef id = e.ref;
  // Identity is the host's note id, or (port, channel, key) when it supplies none (intake/note-vocabulary.md 1): then
  // EVERY press of a key carries the same ref. So a same-ref entry that is no longer DOWN (released under the pedal, or
  // latched) is the PREVIOUS press of that key and this NoteOn is a fresh press; only a same-ref entry still DOWN is a
  // true duplicate. An ignored ref (refused or toggled out) stays ignored until its NoteOff arrives.
  int same = -1;
  for (int i = 0; i < refN_; ++i)
    if (refs_[static_cast<std::size_t>(i)].id == id) { same = i; break; }
  if (ARP_PLANT(sameRefDropsPress) ? same >= 0 : (same >= 0 && refs_[static_cast<std::size_t>(same)].state == RefState::Down)) return;
  if (refIgnored(id)) return;
  const std::int32_t key = e.key;
  // The gesture-start test sees the old same-ref entry too (a PEDAL one still counts as held), BEFORE it is replaced.
  bool noActive = true, hadLatched = false;
  for (int i = 0; i < refN_; ++i) {
    const RefState st = refs_[static_cast<std::size_t>(i)].state;
    if (st == RefState::Down || st == RefState::Pedal) noActive = false;
    if (st == RefState::Latched) hadLatched = true;
  }
  const Latch latch = active_.latch;
  // A gesture start under latch replace drops every LATCHED ref first.
  if (noActive && latch == Latch::Replace && !ARP_PLANT(latchReplaceKeeps))
    eraseRefs([](const Ref& r) { return r.state == RefState::Latched; });
  const int sameKey = refCountOf(key);   // (M3c: the per-key count, not a scan)
  const bool addsKey = sameKey == 0;   // SPEC 4.8: this press adds a key to H (counted before a toggle or a same-ref press gives way)
  // Under latch append a press on a key held only by LATCHED refs toggles it out; its own NoteOff is then ignored.
  if (latch == Latch::Append && sameKey > 0 && !ARP_PLANT(noLatchToggle)) {
    int sameLatched = 0;
    for (int i = 0; i < refN_; ++i) {
      const Ref& r = refs_[static_cast<std::size_t>(i)];
      sameLatched += r.key == key && r.state == RefState::Latched;
    }
    if (sameLatched == sameKey) {
      eraseRefs([key](const Ref& r) { return r.key == key; });
      ignoreRef(id);
      return;
    }
  }
  if (same >= 0) eraseRefs([id](const Ref& r) { return r.id == id; });   // the previous press of this ref (PEDAL or LATCHED) gives way to the fresh one (none to remove when the ref was not known)
  const int keyRefs = refCountOf(key);   // after the previous press of this ref went
  if (keyRefs == 0 && distinctHeld() >= caps_.held) { ++counters_.heldOverflow; ignoreRef(id); return; }
  if (keyRefs >= caps_.refsPerKey) { ++counters_.refOverflow; ignoreRef(id); return; }
  Ref r;
  r.id = id;
  r.key = key;
  r.vel = e.vel;
  r.ch = id.channel;
  r.state = RefState::Down;
  r.abs = absBase_ + s;
  addRef(r);
  // A gesture start (SPEC 3): no ref was DOWN or PEDAL. Under append, a press that joins a latched set is not one (4.5).
  const bool gestureStart = noActive && !(latch == Latch::Append && hadLatched);
  // SPEC 4.8 (r4): ANY NoteOn that adds a key to H (a gesture start, latch append, the pedal) restarts the repeat count and clears
  // `exhausted`; it re-anchors the pattern only when it is a gesture start. Plant onlyGestureResets: only a gesture start does.
  if (gestureStart || (addsKey && !ARP_PLANT(onlyGestureResets))) {
    repeatSteps_ = 0;
    exhausted_ = false;
  }
  if (gestureStart) gesture(s);
}

void Core::gesture(std::int32_t s) {
  if (!enabled_) return;
  if (clock_ == Clock::Free && !(ARP_PLANT(timeAnchorsTransport) && timeMode_)) { freeStart(s); return; }   // re-base the free (or time) clock at the press (H7a)
  // Restart `chord` on the host clock: the anchor is the first step not yet evaluated at this sample (SPEC 13.4).
  if (!hasLastEvaluated_) {
    lastEvaluated_ = kAtOrAfter(posAt(s)) - 1;
    hasLastEvaluated_ = true;
  }
  if (restartMode() != Restart::Chord) return;   // bar and free anchor by the bar grid / tick 0, not by the press (SPEC 4.5)
  anchorK_ = lastEvaluated_ + 1;
  hasAnchor_ = true;
  m_ = 0;
}

void Core::noteOff(const Event& e) {
  if (!ARP_PLANT(sameRefDropsPress) && unignoreRef(e.ref)) return;   // an ignored press ends: its ref is free again, nothing else to do
  int found = -1;
  for (int i = 0; i < refN_; ++i)
    if (refs_[static_cast<std::size_t>(i)].id == e.ref) { found = i; break; }
  if (found < 0) return;   // unknown or ignored: no effect
  Ref& r = refs_[static_cast<std::size_t>(found)];
  if (r.state != RefState::Down) return;
  if (pedalDown_ && !ARP_PLANT(pedalIgnored)) r.state = RefState::Pedal;
  else if (active_.latch == Latch::Off) eraseRefs([id = e.ref](const Ref& x) { return x.id == id; });
  else r.state = RefState::Latched;
}

void Core::pedal(bool down) {
  if (!down && pedalDown_) {
    const bool latch = active_.latch != Latch::Off;
    if (latch) {
      for (int i = 0; i < refN_; ++i)
        if (refs_[static_cast<std::size_t>(i)].state == RefState::Pedal) refs_[static_cast<std::size_t>(i)].state = RefState::Latched;
    } else {
      eraseRefs([](const Ref& r) { return r.state == RefState::Pedal; });
    }
  }
  pedalDown_ = down;
}

// H: the distinct keys over all live refs, in ascending key order; each carries the channel and
// velocity of its EARLIEST live ref (SPEC 3, 4.2).
int Core::collectHeld(HeldKey* out) const {
  int n = 0;
  for (int i = 0; i < refN_; ++i) {
    const Ref& r = refs_[static_cast<std::size_t>(i)];
    int at = -1;
    for (int j = 0; j < n; ++j)
      if (out[j].key == r.key) { at = j; break; }
    if (at < 0) {
      out[n++] = HeldKey{r.key, r.vel, r.ch, r.abs};
    } else if (r.abs < out[at].abs) {
      out[at] = HeldKey{r.key, r.vel, r.ch, r.abs};
    }
  }
  for (int i = 1; i < n; ++i) {   // insertion sort by key
    const HeldKey h = out[i];
    int j = i;
    while (j > 0 && out[j - 1].key > h.key) { out[j] = out[j - 1]; --j; }
    out[j] = h;
  }
  return n;
}

// H' (SPEC 14): what the arp reads. With the chord stage on it is the chord set (each key with its chord note's velocity and
// channel, in press order by first source then slot order); with the stage off it is H. Plant arpIgnoresChord reads the raw keys.
int Core::heldForArp(HeldKey* out) const {
  if (moduleOn_ && S_.chordEnabled && !ARP_PLANT(arpIgnoresChord)) {
    int n = 0;
    for (int k = 0; k < 128; ++k)
      if (chordNote_[static_cast<std::size_t>(k)].used) out[n++] = chordSet_[static_cast<std::size_t>(k)];
    return n;
  }
  return collectHeld(out);
}

int Core::heldCountForArp() const {
  if (moduleOn_ && S_.chordEnabled && !ARP_PLANT(arpIgnoresChord)) return chordSetN_;
  return distinctHeld();
}

// The lowest PLAYED key (over every live ref): chord_root's anchor, whatever the chord stage feeds the arp.
std::int32_t Core::lowestHeldKey() const {
  std::int32_t lo = 127;
  for (int i = 0; i < refN_; ++i) lo = std::min(lo, refs_[static_cast<std::size_t>(i)].key);
  return lo;
}

// ------------------------------------------------------------------------------------------ the grid walk (SPEC 4)

// Called when the next unevaluated grid step's GRID tick lands at offset `so`.
void Core::visit(std::int32_t so) {
  const std::int64_t kAbs = lastEvaluated_ + 1;
#ifdef ARP_PLANTS
  if (plants.inactiveReanchors) {   // plant: a change of the INACTIVE timing setting restarts the order
    const std::int64_t v = timeMode_ ? static_cast<std::int64_t>(S_.rate) : S_.stepTimeUs;
    if (inactSeen_ >= 0 && inactSeen_ != v) m_ = 0;
    inactSeen_ = v;
  }
#endif
  // SPEC 2.6: `rate` is ignored in time mode and step_time_us in sync mode (a morph of the inactive one does nothing:
  // no T15). In time mode a changed step_time_us was already folded into the grid at the block start (timeRegrid).
  if (timeMode_) {
    if (ARP_PLANT(regridAbsolute) && curL() != L_) { regridAbsolute(kAbs); return; }
  } else if (S_.rate != active_.rate) {
    switchRate(kAbs, so);   // T15: a rate change applies at the next step of the OLD rate (SPEC 9)
    return;
  }
  lastEvaluated_ = kAbs;
  adopt(so);
  chordSync(so);   // adopt() may have dropped latched refs (plant latchAtNextStep), which changes H and so the chord stage
  if (restartMode() != Restart::Chord) hasAnchor_ = true;   // bar and free anchor without a gesture (SPEC 2.5)
  // SPEC 5.1 (E6, D-027): swing is latched per step pair. The even step 2j reads `swing` now; the odd step 2j+1 reuses that value if 2j was
  // the last step EVALUATED, else reads the current one. "Evaluated" is SPEC 2.5's: the clock runs and an anchor exists. A step the once-guard
  // consumes with no anchor (restart `chord` before the first gesture) is not evaluated and latches nothing, so the odd step right after a
  // gesture start reads the current swing; a spent step (4.8, a silent rest WITH an anchor) is evaluated and does latch. Plant
  // swingLatchUnanchored latches before the anchor test (the M2b order).
  if (hasAnchor_ || ARP_PLANT(swingLatchUnanchored)) {
    const std::int64_t cur = clampi(S_.swing, 500, 750);
    const bool odd = mod(kAbs, 2) == 1;
    swingUsed_ = (odd && swingPairK_ == kAbs - 1 && !ARP_PLANT(swingPerStep)) ? swingPair_ : cur;
    if (!odd) { swingPair_ = swingUsed_; swingPairK_ = kAbs; }
  }
  if (!hasAnchor_) return;   // restart `chord`: no anchor before the first gesture (SPEC 2.5)
  evalStep(kAbs, so);
}

// T15: the new grid anchors at the first new-grid step at or after the old step's grid tick (SPEC 13.8).
void Core::switchRate(std::int64_t kAbs, std::int32_t so) {
  switchTouch(so);
  const std::int64_t oldTick = kAbs * L_;
  active_.rate = S_.rate;
  swingPairK_ = INT64_MIN;   // the new grid renumbers the steps
  L_ = rateTicks(S_.rate);
  const std::int64_t kNew = cdiv(oldTick, L_);
  lastEvaluated_ = kNew - 1;
  if (soundN_ > 0) ++counters_.rows[15];
  switch (restartMode()) {   // SPEC 13.8
    case Restart::Chord: if (hasAnchor_) { anchorK_ = kNew; m_ = 0; } break;
    case Restart::Free: anchorK_ = 0; break;   // m is kept
    case Restart::Bar: if (hasLastBar_) anchorK_ = cdiv(barPhase_ + lastBar_ * barTicks_, L_); break;   // the anchor re-expressed in the new rate; m is kept
  }
}

// Discrete settings apply from the next evaluated step (T8). `enabled`, `arp_enabled`, `latch` and `rate`
// are applied elsewhere (offset 0, or switchRate); the continuous ones are read live at evaluation.
// Plants only: a discrete switch that touches notes, which SPEC 7 T8 says it never does. Called wherever a discrete setting is adopted.
void Core::switchTouch(std::int32_t so) {
  if (ARP_PLANT(morphSwitchCuts)) releaseArp(so);
  if (ARP_PLANT(morphSwitchDropsPending)) pendN_ = 0;
}

void Core::adopt(std::int32_t so) {
  if (ARP_PLANT(latchAtNextStep) && S_.latch != active_.latch && S_.latch == Latch::Off)
    eraseRefs([](const Ref& r) { return r.state == RefState::Latched; });
  Settings t = S_;
  t.enabled = active_.enabled;
  t.arpEnabled = active_.arpEnabled;
  t.rate = active_.rate;
  t.gate = active_.gate;
  t.swing = active_.swing;
  t.velFixed = active_.velFixed;
  t.stepTimeUs = active_.stepTimeUs;
  if (t == active_) return;
  if (ARP_PLANT(allocInSettings)) plantAlloc();
  if (soundN_ > 0) ++counters_.rows[8];
  // SPEC 9 Q-A2: a change of `restart` takes effect from this, the next evaluated step. Into bar: the anchor is recomputed from the
  // bar grid (evalStep, since lastBar_ is cleared) and m := 0. Into free: anchorK := 0, m := 0. Into chord: the anchor is kept until
  // the next gesture start. Inert in time mode (there are no bars in time).
  if (!timeMode_ && t.restart != active_.restart) {
    if (t.restart == Restart::Chord) {
      if (ARP_PLANT(restartMorphReanchors)) { anchorK_ = lastEvaluated_; hasAnchor_ = true; m_ = 0; }   // plant: re-anchors although SPEC 9 keeps the anchor
    } else {
      // Forget the bar bookkeeping too: a return to bar inside the bar it left must re-anchor at that bar's start (the chord anchor
      // set by a gesture meanwhile is not the bar's). Plant adoptKeepsLastBar keeps the old bar index, so the return does nothing.
      if (!ARP_PLANT(adoptKeepsLastBar)) hasLastBar_ = false;
      hasAnchor_ = true;
      if (t.restart == Restart::Free) anchorK_ = 0;
      if (!ARP_PLANT(restartMorphKeepsM)) m_ = 0;
    }
  }
  switchTouch(so);
  active_ = t;
}

// SPEC 4.2: the note(s) of one step, before transposition, as (key, velocity, channel). E is the held keys in pitch order
// (no octave copies); several notes behave as a `chord` step. Returns the count (<= e, or 2 for pairs).
int Core::notesFor(const HeldKey* E, int e, std::int64_t kAbs, std::int32_t pick, Note* out) const {
  auto at = [&](int i, int shift = 0) { return Note{E[i].key + shift, E[i].vel, E[i].ch}; };
  auto all = [&]() { for (int i = 0; i < e; ++i) out[i] = at(i); return e; };
  if (pick == kPickChord) return all();
  if (pick != kPickOrder) {   // an explicit pick plays its note without consuming the order stream
    out[0] = at(pick >= 1 && pick <= 8 ? static_cast<int>(mod(pick - 1, e)) : pick == kPickLow ? 0 : e - 1);
    return 1;
  }
  const Order o = active_.order;
  const bool tables = ARP_PLANT(orderTables);
  if (o == Order::Chord) return all();
  if (o == Order::Played) {   // the press-order list Pp: by the earliest ref's press sample, ties by key
    HeldKey P[kMaxArpKeys];
    for (int i = 0; i < e; ++i) P[i] = E[i];
    for (int i = 1; i < e; ++i) {
      const HeldKey h = P[i];
      int j = i;
      const bool tieDesc = ARP_PLANT(playedTieByKeyDesc);   // plant: ties of one press sample resolve to the HIGHER key first
      // `Pp` (SPEC 4.2 / 14): by press sample, then (chord-stage keys of one source press) by slot, then by key
      while (j > 0 && (P[j - 1].abs > h.abs || (P[j - 1].abs == h.abs && (P[j - 1].slot > h.slot || (P[j - 1].slot == h.slot && (tieDesc ? P[j - 1].key < h.key : P[j - 1].key > h.key)))))) { P[j] = P[j - 1]; --j; }
      P[j] = h;
    }
    const HeldKey& h = P[mod(m_, e)];
    out[0] = Note{h.key, h.vel, h.ch};
    return 1;
  }
  if (o == Order::Random) {   // draw lane 0 (SPEC 10): a pure function of (seed, kAbs, epoch)
    out[0] = at(static_cast<int>(draw(active_.seed, kAbs, 0) % static_cast<std::uint32_t>(e)));
    return 1;
  }
  if (o == Order::Shuffle) {
    out[0] = at(shuffleAt(e, fdiv(m_, e), mod(m_, e)));
    return 1;
  }
  const std::int64_t p = mod(m_, periodOf(o, e, tables));
  switch (o) {
    case Order::Down: out[0] = at(static_cast<int>(e - 1 - p)); return 1;
    case Order::Bounce: out[0] = at(static_cast<int>(tables ? (p < e ? p : 2 * e - 1 - p) : (p < e ? p : 2 * e - 2 - p))); return 1;
    case Order::Bounce2: out[0] = at(static_cast<int>(p < e ? p : 2 * e - 1 - p)); return 1;
    case Order::Converge: out[0] = at(static_cast<int>(convergeIndex(tables ? p + 1 : p, e) % e)); return 1;
    case Order::Diverge: out[0] = at(static_cast<int>(convergeIndex(e - 1 - p, e))); return 1;
    case Order::PedalLow: out[0] = at(static_cast<int>(p % 2 == 0 ? 0 : 1 + (p - 1) / 2)); return 1;
    case Order::PedalHigh:
      if (tables) { out[0] = at(static_cast<int>(p % 2 == 0 ? 0 : 1 + (p - 1) / 2)); return 1; }
      out[0] = at(static_cast<int>(p % 2 == 0 ? e - 1 : e - 2 - (p - 1) / 2));
      return 1;
    case Order::ChordWalk:
      if (p == 0) return all();
      out[0] = at(static_cast<int>(p - 1));
      return 1;
    case Order::ChordBetween:
      if ((p % 2 == 0) != tables) return all();
      out[0] = at(static_cast<int>(tables ? p / 2 : (p - 1) / 2));
      return 1;
    case Order::Pairs:
      if (e == 1) { out[0] = at(0); return 1; }
      out[0] = at(static_cast<int>(p));
      out[1] = at(static_cast<int>(p + 1));
      return 2;
    case Order::Stride:
      if (p == 0) { out[0] = at(0); return 1; }   // the bass: the lowest held key
      if (e == 1) { out[0] = at(0, 12); return 1; }   // one key held: the "rest" is that key + 12
      for (int i = 1; i < e; ++i) out[i - 1] = at(i);
      return e - 1;
    default: break;   // Up (and a corrupt enum value)
  }
  out[0] = at(static_cast<int>(p));
  return 1;
}

// shuffle (SPEC 4.2, D-012a). P_c is a Fisher-Yates of E using draw(seed, c*256 + i, 1, epoch) for i = e-1 .. 1; for c > 0
// and e >= 2, if P_c[0] equals the LAST note of the previous cycle's (final) order, P_c[0] and P_c[1] swap. Returns the
// index into E of position `pos` of cycle c.
//
// Allocation-free and O(e) with no memo. The previous cycle's last note needs no chain: for e >= 3 the swap never touches
// the last slot, so it is the raw P_{c-1}[e-1]; for e = 2 the rule makes every cycle end on the same key (if P_c[0] equals
// the previous last the swap moves it to the front-of-two, otherwise P_c[1] already is the previous last), so it is
// P_0[1] for every c. tests/core/test_m1b.cpp (testOrderTables, shuffleReference) checks this against a literal replay of the spec's chain.
int Core::shuffleAt(int e, std::int64_t c, std::int64_t pos) const {
  auto raw = [&](std::int64_t cycle, int* P) {
    for (int i = 0; i < e; ++i) P[i] = i;
    for (int i = e - 1; i > 0; --i) {
      const int q = static_cast<int>(draw(active_.seed, cycle * 256 + i, 1) % static_cast<std::uint32_t>(i + 1));
      const int t = P[i]; P[i] = P[q]; P[q] = t;
    }
  };
  int P[kMaxArpKeys];
  raw(c, P);
  if (c > 0 && e >= 2 && !ARP_PLANT(shuffleBoundaryRepeat)) {
    int Q[kMaxArpKeys];
    int prevLast;
    if (e >= 3) { raw(c - 1, Q); prevLast = Q[e - 1]; } else { raw(0, Q); prevLast = Q[1]; }
    if (P[0] == prevLast) { const int t = P[0]; P[0] = P[1]; P[1] = t; }
  }
  return P[pos];
}

// SPEC 4.3 for a whole step: probability, ties, order, pick, step offset, transposition, ratchets, gate, triggers.
void Core::evalStep(std::int64_t kAbs, std::int32_t so) {
#ifdef ARP_PLANTS
  if (plants.midBlockHook) plants.midBlockHook();   // U1: stands for another thread's setUserPattern landing now
#endif
  const std::int64_t L = L_, D = D_;
  const Settings& a = active_;
  // -- the pattern: a bank entry, or the user pattern as it stood when this block began
  const PatternStep* steps;
  std::int64_t plen, pSize;
  StepUnit pUnit;
  activePattern(steps, plen, pSize, pUnit);
  if (ARP_PLANT(stepSizeIgnored)) { pSize = 12; pUnit = StepUnit::Semitones; }
  if (ARP_PLANT(stepOffsetReadsGlobal)) { pSize = a.stepSize; pUnit = a.stepUnit; }
  // SPEC 4.5: the anchor. `bar`: the first grid step at or after each bar start, m := 0 there. `free`: tick 0 of the active clock.
  // `chord`: set at the gesture start (gesture()), kept here. Inert in time mode (restartMode()).
  const Restart rs = restartMode();
  if (rs == Restart::Bar) {
    const std::int64_t b = fdiv(gtick(kAbs) - barPhase_, barTicks_);
    if (!hasLastBar_ || b != lastBar_) {
      lastBar_ = b;
      hasLastBar_ = true;
      anchorK_ = cdiv(barPhase_ + b * barTicks_, L);
      m_ = 0;
    }
  } else if (rs == Restart::Free) {
    anchorK_ = 0;
  }
  const std::int64_t k = kAbs - anchorK_, j = mod(k, plen);
  const PatternStep st = steps[j];
  HeldKey E[kMaxArpKeys];
  const int e = heldForArp(E);   // H': the chord set when the chord stage is on, else the held keys (SPEC 14)
  // SPEC 5.1: on-times anchor at kAbs * L (never k * L); swing parity comes from kAbs. `swing` is latched per step PAIR (E6, visit()): span(2j) and
  // on(2j+1) always use one value, so no step's ratchets can sound after the next step's on. The tie look-ahead below uses this step's value too.
  const std::int64_t swing = swingUsed_;
  const std::int64_t swingDelay = (timeMode_ && ARP_PLANT(timeIgnoresSwing)) ? 0 : fdiv(L * (2 * swing - 1000), 1000);
  const std::int64_t srSkew = ARP_PLANT(srTickMath) ? sr_ / 44100 : 0;
  auto onTick = [&](std::int64_t x) { return gtick(x) + (mod(ARP_PLANT(swingParityK) ? x - anchorK_ : x, 2) == 1 ? swingDelay : 0) + srSkew; };
  const std::int64_t on0 = onTick(kAbs);
  // SPEC 5.1 (E10): in time mode a step_time_us change can bring this step's on before a sub-note of an earlier step that is still pending.
  // Those are dropped here (none has a NoteOn yet, so none is owed a terminal). Done for every EVALUATED step, a spent one too.
  if (timeMode_ && !ARP_PLANT(keepSpilledRatchets)) dropSpilledSubnotes(on0);
  // SPEC 4.8: one repeat is one full cycle of the whole pattern, C grid steps: the step pattern, the order style (through every
  // held note) and the transposition passes all come round together. C is recomputed from the live H' at each step and also the moment H'
  // becomes non-empty (updateCycle, E11); while H' is EMPTY the last non-empty C is kept (E9, D-027), so the count keeps running after
  // the keys are released and an arp let go early still runs out (before the first gesture there is no C, hence no count).
  // SPEC-GAP: T6 and T7 keep the last C (and, with no keys held, the count itself); only T10 forgets it.
  // Once repeatSteps >= repeats * C the arp is EXHAUSTED, a latched flag: the step is still consumed by the once-guard (visit() already
  // advanced it) as a silent rest, with no note, no trigger and no step record (13 note 18). Morphs of repeats, steps or pattern never
  // revive it; only a gesture start, a key-adding NoteOn, T6/T7 with keys held (E12) or T10 does (noteOn, edge, resetState). C is worked
  // out whenever keys are held, whatever `repeats` is, so a repeats morph after release never reads the cycle length of an older gesture.
  // Plant repeatsPauseWhileEmpty: no check while H' is empty (the M2b behaviour).
  if (e > 0) {
    lastC_ = cycleLength(steps, plen, e);
    hasLastC_ = true;
  }
  if (a.repeats > 0 && (e > 0 || (hasLastC_ && !ARP_PLANT(repeatsPauseWhileEmpty))) && !ARP_PLANT(repeatsNeverStop) && (!exhausted_ || ARP_PLANT(morphRevives))) {
    const bool spent = repeatSteps_ >= a.repeats * lastC_;
    if (spent && !exhausted_) ++counters_.rows[20];   // T20: the repeats ran out
    exhausted_ = spent;
  }
  if (exhausted_) {
    if (ARP_PLANT(swingSpentNoLatch)) swingPairK_ = INT64_MIN;   // plant (E6 / L1): a spent step does not latch the swing pair
    return;
  }
  ++repeatSteps_;
  StepRecord rec;
  rec.offset = so;
  rec.kAbs = kAbs;
  rec.k = k;
  rec.mBefore = m_;
  const std::int64_t ord = stepSeq_++;
  // SPEC 4.3 step 1: prob 0 never fires and 100 always does, exactly (a draw is a uniform 32-bit number mod 100)
  const std::uint32_t dv = draw(a.seed, kAbs, 2) % 100;
  const bool fires = ARP_PLANT(probLE) ? dv <= static_cast<std::uint32_t>(st.prob) : dv < static_cast<std::uint32_t>(st.prob);
  auto trigger = [&]() { if (fires && st.trig > 0) queueTrigger(I128::mul(on0, D), st.trig, ord); };   // at the FIRST on, once per step (SPEC 8)

  if (st.tie && !ARP_PLANT(tieAtTieStep)) {
    trigger();   // 2. a tie step plays no note and leaves m alone: the tie was honoured by look-ahead at the step that played
  } else if (st.on && fires && e > 0) {
    rec.played = true;
    const std::int32_t pick = ARP_PLANT(ignorePick) ? kPickOrder : normPick(st.pick);
    // -- the held keys the style reads: the plant substitutes r2.6's merged octave range
    const HeldKey* Es = E;
    int es = e;
#ifdef ARP_PLANTS
    std::vector<HeldKey> merged;
    if (plants.transposeMerged && a.steps > 0) {
      for (std::int64_t t = 0; t <= a.steps; ++t)
        for (int i = 0; i < e; ++i) {
          HeldKey h = E[i];
          h.key += static_cast<std::int32_t>(t * a.stepSize);
          bool dup = false;
          for (const HeldKey& q : merged) dup = dup || q.key == h.key;
          if (!dup) merged.push_back(h);
        }
      std::sort(merged.begin(), merged.end(), [](const HeldKey& x, const HeldKey& y) { return x.key < y.key; });
      if (merged.size() > static_cast<std::size_t>(kMaxArpKeys)) merged.resize(static_cast<std::size_t>(kMaxArpKeys));
      Es = merged.data();
      es = static_cast<int>(merged.size());
    }
    const bool mergedOn = Es != E;
#else
    const bool mergedOn = false;
#endif
    Note nl[kMaxArpKeys];
    const int nn = notesFor(Es, es, kAbs, pick, nl);
    // -- SPEC 4.7: the pass index t = mod(fdiv(m, P), steps + 1), P the order style's period over the live H; an explicit
    // pick takes the CURRENT pass (13b.7). Two offsets, the pass shift (t * step_size in the GLOBAL step_unit) and the step
    // offset (step * the PATTERN's step_size in the pattern's unit), each a semitone part or a degree part; the parts sum by
    // kind into S semitones and G degrees.
    std::int64_t nSteps = a.steps;
    if (ARP_PLANT(stepsZeroShifts)) nSteps = std::max<std::int64_t>(nSteps, 2);
    const std::int64_t pass = (mergedOn || nSteps <= 0) ? 0 : mod(fdiv(m_, periodOf(a.order, es, ARP_PLANT(orderTables))), nSteps + 1);
    std::int64_t S = 0, G = 0;
    (a.stepUnit == StepUnit::Degrees && !ARP_PLANT(degreesAsSemitones) ? G : S) += pass * a.stepSize;
    (pUnit == StepUnit::Degrees && !ARP_PLANT(stepDegreesAsSemitones) ? G : S) += static_cast<std::int64_t>(st.step) * pSize;
    // -- SPEC 6: the effective scale. Source: horde's (the block input) or Arp's own; chord_root anchors the shape on the lowest
    // held key and always admits that key's own pitch class (13b.1: only when the mask is non-zero; a zero mask disables quantize).
    std::int32_t qRoot = 0;
    std::uint32_t qMask = 0;
    if (a.quantize != Quantize::Off) {
      if (a.scaleSource == ScaleSource::Own && !ARP_PLANT(ownIgnored)) {
        qRoot = static_cast<std::int32_t>(mod(a.scaleRoot, 12));
        for (int d = 0; d < 12; ++d) qMask |= a.scaleDeg[d] ? 1u << d : 0u;
      } else {
        qRoot = static_cast<std::int32_t>(mod(scale_.root, 12));
        qMask = scale_.mask & 0xFFFu;
      }
      if (a.quantize == Quantize::ChordRoot) {
        // SPEC 14 / 6: the anchor is the lowest PLAYED key (H), never E[0] of the arp's held set H' (the chord set when the stage is on).
        // Plant chordRootFromChordSet inherits E[0] of H'.
        if (!ARP_PLANT(chordRootAsRoot)) qRoot = static_cast<std::int32_t>(mod(ARP_PLANT(chordRootFromChordSet) ? E[0].key : lowestHeldKey(), 12));
        if (qMask != 0) qMask |= 1u;
      }
    }
    const bool qOn = qMask != 0 && !(ARP_PLANT(skipQuantizeChord) && nn > 1);
    // -- the keys, SPEC 4.7 "Applying offsets": per key, in ASCENDING pre-quantize order, each key through all six stages before
    // the next starts (so a tie in stage 3 reads lastQ as the previous key left it): 1. key + S; 2. drop outside 0-127 (T16);
    // 3. quantize; 4. move G in-scale keys; 5. drop outside 0-127; 6. lastQ := the final key, only if the key survived.
    // Collisions (two keys landing on one) keep the EARLIEST source in the style's order, which is the list order.
    int byKey[kMaxArpKeys];
    for (int i = 0; i < nn; ++i) {
      int at = i;
      while (at > 0 && nl[byKey[at - 1]].key > nl[i].key) { byKey[at] = byKey[at - 1]; --at; }
      byKey[at] = i;
    }
    std::int64_t fin[kMaxArpKeys];
    bool finSetsLastQ[kMaxArpKeys] = {};
    const std::uint32_t degMask = qOn ? qMask : 0;   // quantize off or a zero mask: a degree is one semitone
    for (int oi = 0; oi < nn; ++oi) {
      const int i = byKey[oi];
      fin[i] = -1;
      std::int64_t key = nl[i].key + (ARP_PLANT(semitoneAfterQuantize) ? 0 : S);
      if (!ARP_PLANT(noStage2Drop) && (key < 0 || key > 127)) { ++counters_.rows[16]; ++counters_.keysDropped; continue; }
      if (ARP_PLANT(degreeBeforeQuantize)) key = degreeMove(key, G, qRoot, degMask);
      if (qOn) key = quantizeKey(key, qRoot, qMask, hasLastQ_, lastQ_, ARP_PLANT(tieAlwaysDown));
      const std::int64_t quantized = key;
      if (ARP_PLANT(semitoneAfterQuantize)) key += S;
      if (!ARP_PLANT(degreeBeforeQuantize)) key = degreeMove(key, G, qRoot, degMask);
      if (key < 0 || key > 127) {
        ++counters_.rows[16];
        ++counters_.keysDropped;
        if (qOn && ARP_PLANT(droppedKeySetsLastQ)) { lastQ_ = quantized; hasLastQ_ = true; }
        continue;
      }
      if (qOn) {
        if (ARP_PLANT(lastQAtEmission)) { finSetsLastQ[i] = true; }
        else { lastQ_ = ARP_PLANT(lastQPreMove) ? quantized : key; hasLastQ_ = true; }
      }
      fin[i] = key;
    }
    struct Out { std::int32_t key, vel, ch; bool setsLastQ; };
    Out outs[kMaxArpKeys];
    int no = 0;
    std::int8_t outAt[128];   // key -> its index in outs (-1: none yet); a chord step has up to 112 keys, a linear search per key was quadratic
    std::memset(outAt, -1, sizeof outAt);
    for (int i = 0; i < nn; ++i) {
      if (fin[i] < 0) continue;
      const int dup = outAt[fin[i]];   // fin[i] is 0..127 here (stage 5 dropped the rest)
      // SPEC 4.4: round-half-up in integers
      const std::int64_t base = a.velMode == VelMode::Fixed ? clampi(S_.velFixed, 1, 127) : nl[i].vel;
      const auto vel = static_cast<std::int32_t>(clampi(fdiv(base * st.vel + 50, 100), 1, 127));
      if (dup >= 0) {
        if (ARP_PLANT(collisionKeepsLast)) { outs[dup].vel = vel; outs[dup].ch = nl[i].ch; }
        continue;
      }
      outAt[fin[i]] = static_cast<std::int8_t>(no);
      outs[no++] = Out{static_cast<std::int32_t>(fin[i]), vel, nl[i].ch, finSetsLastQ[i]};
    }
    // -- SPEC 5.2: ties by look-ahead: the consecutive tie steps after this one, cyclic, at most length - 1, ignoring prob
    std::int64_t t = 0;
    if (!ARP_PLANT(tieAtTieStep)) while (t < plen - 1 && steps[mod(j + 1 + t, plen)].tie) ++t;
    if (t > 0) ++counters_.rows[12];
    // -- ratchets: sub-note i starts at on + fdiv(i * span, r); the length is floored first, then gated
    const std::int64_t span = onTick(kAbs + 1) - on0, r = clampi(st.ratchet, 1, 4);
    const std::int64_t gate = clampi(S_.gate, 1, 200);
    for (std::int64_t i = 0; i < r; ++i) {
      const std::int64_t subOn = on0 + fdiv(i * span, r);
      std::int64_t len;
      if (r == 1) len = std::max<std::int64_t>(1, fdiv(span * gate * st.gate, 10000));
      else len = std::min(fdiv(span, r), std::max<std::int64_t>(1, fdiv(fdiv(span, r) * gate * st.gate, 10000)));
      std::int64_t off = subOn + len;
      if (i == r - 1 && t > 0) {   // the LAST sub-note runs on to the end of the last tied step
        const std::int64_t x = kAbs + t, sp = onTick(x + 1) - onTick(x);
        off = onTick(x) + std::max<std::int64_t>(1, fdiv(sp * gate * steps[mod(j + t, plen)].gate, 10000));
      }
      for (int q = 0; q < no; ++q) {
        Pending p;
        p.pos = I128::mul(subOn, D);
        p.offPos = I128::mul(off, D);   // offs are scheduled in TICKS, so a tempo change moves them with the music
        p.key = outs[q].key;
        p.vel = outs[q].vel;
        p.ch = outs[q].ch;
        p.ord = ord;
#ifdef ARP_PLANTS
        p.setsLastQ = outs[q].setsLastQ;
#endif
        queueOn(p);
      }
      if (ARP_PLANT(trigEveryRatchet) && i > 0 && fires && st.trig > 0) queueTrigger(I128::mul(subOn, D), st.trig, ord);   // plant: the trigger on every ratchet
    }
    if (pick == kPickOrder || ARP_PLANT(pickAdvancesM)) ++m_;   // only `order` picks consume the order stream, even if every key was dropped
    trigger();
  } else if (ARP_PLANT(emitOnEmptySet) && st.on && fires && e == 0) {
    Pending p;   // plant: a note out of nothing, key 60, one step long, on a step that found no key held
    p.pos = I128::mul(on0, D);
    p.offPos = I128::mul(on0 + std::max<std::int64_t>(1, onTick(kAbs + 1) - on0), D);
    p.key = 60;
    p.vel = 100;
    p.ord = ord;
    queueOn(p);
    trigger();
  } else if (fires && st.trig > 0) {
    trigger();   // 4. a rest (or e = 0) that fires its trigger: trigger-only (H11)
  } else if (ARP_PLANT(advanceOnRest)) {
    ++m_;
  }
  rec.mAfter = m_;
  if (static_cast<std::size_t>(stepsN_) < steps_.size()) steps_[static_cast<std::size_t>(stepsN_++)] = rec;
  else ++counters_.stepLogDropped;
}

// SPEC 5.1 (E10): drop every pending SUB-NOTE (not a trigger) whose start is at or after the evaluated step's on (tick on0). The queue keeps its order.
void Core::dropSpilledSubnotes(std::int64_t on0) {
  const I128 from = I128::mul(on0, D_);
  int w = 0;
  for (int i = 0; i < pendN_; ++i) {
    const Pending& p = pendAt(i);
    if (p.lane == 0 && p.pos >= from) continue;
    if (w != i) pendAt(w) = p;
    ++w;
  }
  pendN_ = w;
}

// The step pattern in force: a bank entry, or the user pattern as it stood when this block began (SPEC 4.6).
void Core::activePattern(const PatternStep*& steps, std::int64_t& plen, std::int64_t& pSize, StepUnit& pUnit) const {
  if (active_.pattern == Pattern::User) {
    const UserPattern& u = userSlots_[static_cast<std::size_t>(r_)];
    steps = u.steps; plen = u.length; pSize = u.stepSize; pUnit = u.stepUnit;
  } else {
    const std::size_t bi = static_cast<std::size_t>(active_.pattern) < static_cast<std::size_t>(kBankCount) ? static_cast<std::size_t>(active_.pattern) : 0;
    steps = kBank[bi].steps; plen = kBank[bi].length; pSize = kBank[bi].stepSize; pUnit = kBank[bi].stepUnit;
  }
}

// SPEC 4.8: C = plays > 0 ? Ls * (P*(steps+1) / gcd(P*(steps+1), plays)) : Ls, for e >= 1 keys of H'. `plays` is a static count: prob is ignored.
std::int64_t Core::cycleLength(const PatternStep* steps, std::int64_t plen, int e) const {
  std::int64_t plays = 0;
  for (std::int64_t q = 0; q < plen; ++q) plays += steps[q].on && !steps[q].tie && normPick(steps[q].pick) == kPickOrder;
  const std::int64_t pn = periodOf(active_.order, e, ARP_PLANT(orderTables)) * (active_.steps + 1);
  return ARP_PLANT(repeatsCountPatternPasses) ? plen : plays > 0 ? plen * (pn / std::gcd(pn, plays)) : plen;
}

// SPEC 4.8 (E11, D-028): C is also set the moment H' becomes non-empty, not only at an evaluated step with keys held. Otherwise a chord
// tapped and released before the next step never gets a C: the first such tap would count nothing (triggers without end) and a later one
// would borrow the previous chord's. Called after the post-input chordSync, so it sees the H' this input produced; uses the settings in
// force (active_), like the step that would have read it. Plant cOnlyAtStep: the M2b rule, C only at an evaluated step.
void Core::updateCycle() {
  if (ARP_PLANT(cOnlyAtStep)) return;
  const int e = heldCountForArp();   // (only the count is read: building H' for it copied up to 128 keys at every input sample)
  if (e == 0) return;
  const PatternStep* steps;
  std::int64_t plen, pSize;
  StepUnit pUnit;
  activePattern(steps, plen, pSize, pUnit);
  lastC_ = cycleLength(steps, plen, e);
  hasLastC_ = true;
}

void Core::queueTrigger(I128 pos, std::int32_t lane, std::int64_t ord) {
  Pending p;
  p.pos = pos;
  p.offPos = pos;
  p.lane = lane;
  p.ord = ord;
  queueOn(p);
}

void Core::queueOn(const Pending& p) {
  if (pendN_ >= caps_.pending) { ++counters_.pendingOverflow; return; }   // SPEC 1.3: the step's remaining ons are refused (counted)
  // File it by position, behind every entry at the same position (strictly later ones are passed): the front of the ring is then what the old
  // linear "first of the smallest pos" scan found. A step queues its ons in position order and the next step's lie after them, so j is usually pendN_.
  int j = pendN_;
  while (j > 0 && p.pos < pendAt(j - 1).pos) --j;
  for (int k = pendN_; k > j; --k) pendAt(k) = pendAt(k - 1);
  pendAt(j) = p;
  ++pendN_;
}

Core::Pending Core::popPending() {
  const Pending p = pendAt(0);
  pendHead_ = (pendHead_ + 1) & (kMaxPending - 1);
  --pendN_;
  return p;
}

// ------------------------------------------------------------------------------------------ emission and the ONE release path

bool Core::pushEvent(const Event& e) {
  if (outN_ >= capEv_) return false;
  out_[static_cast<std::size_t>(outN_++)] = e;
  return true;
}

// Triggers, a forwarded Panic and pass-through events use the same room test as a note (SPEC 13.3).
void Core::pushNonNote(const Event& e) {
  if (!room(1) || !pushEvent(e)) ++counters_.eventOverflow;
}

// ---- the sounding table (invariants: core.h, "sounding notes and the pending queue") ----

void Core::resetSounding() {
  for (std::size_t i = 0; i < sounding_.size(); ++i) {
    Sounding& x = sounding_[i];
    x = Sounding{};
    x.keyNext = i + 1 < sounding_.size() ? static_cast<std::int16_t>(i + 1) : static_cast<std::int16_t>(-1);
  }
  freeHead_ = 0;
  keyHead_.fill(-1);
  liveHead_ = liveTail_ = -1;
  soundN_ = chordSoundN_ = liveN_ = liveChordN_ = 0;
  schedHead_ = schedN_ = quickN_ = 0;
}

// The caller has checked the capacity (emitOn: the arp's 144, chordStart: the table), so a free slot exists.
int Core::allocSlot(const NoteRef& ref, bool chord, std::int64_t onAbs) {
  const int i = freeHead_;
  Sounding& x = sounding_[static_cast<std::size_t>(i)];
  freeHead_ = x.keyNext;
  x = Sounding{};
  x.used = true;
  x.ref = ref;
  x.chord = chord;
  x.onAbs = onAbs;
  std::int16_t& head = keyHead_[static_cast<std::size_t>(keyChain(ref.key))];
  x.keyNext = head;
  head = static_cast<std::int16_t>(i);
  ++soundN_;
  ++liveN_;
  if (chord) { ++chordSoundN_; ++liveChordN_; } else liveLink(i);
  return i;
}

// The live-arp list is appended to in allocation order, which is ascending output id (ids come from one counter, see allocSlot's callers).
void Core::liveLink(int i) {
  Sounding& x = sounding_[static_cast<std::size_t>(i)];
  x.livePrev = liveTail_;
  x.liveNext = -1;
  if (liveTail_ >= 0) sounding_[static_cast<std::size_t>(liveTail_)].liveNext = static_cast<std::int16_t>(i); else liveHead_ = static_cast<std::int16_t>(i);
  liveTail_ = static_cast<std::int16_t>(i);
}

void Core::liveUnlink(int i) {
  Sounding& x = sounding_[static_cast<std::size_t>(i)];
  if (x.livePrev >= 0) sounding_[static_cast<std::size_t>(x.livePrev)].liveNext = x.liveNext; else liveHead_ = x.liveNext;
  if (x.liveNext >= 0) sounding_[static_cast<std::size_t>(x.liveNext)].livePrev = x.livePrev; else liveTail_ = x.livePrev;
  x.liveNext = x.livePrev = -1;
}

void Core::freeSlot(int i) {
  Sounding& x = sounding_[static_cast<std::size_t>(i)];
  std::int16_t* link = &keyHead_[static_cast<std::size_t>(keyChain(x.ref.key))];
  while (*link != i) link = &sounding_[static_cast<std::size_t>(*link)].keyNext;
  *link = x.keyNext;
  if (!x.dying) { --liveN_; if (x.chord) --liveChordN_; else liveUnlink(i); }
  if (x.chord) --chordSoundN_;
  --soundN_;
  x = Sounding{};
  x.keyNext = freeHead_;
  freeHead_ = static_cast<std::int16_t>(i);
}

// A note is found by its key's chain: ids are unique, so the id decides.
int Core::findSounding(const NoteRef& ref) const {
  for (int i = keyHead_[static_cast<std::size_t>(keyChain(ref.key))]; i >= 0; i = sounding_[static_cast<std::size_t>(i)].keyNext)
    if (sounding_[static_cast<std::size_t>(i)].ref.id == ref.id) return i;
  return -1;
}

// The used slots (the chord stage's too, or only the arp's) in ascending output id, which is the order the old dense table listed them in:
// release-all walks them in it, so the first-release plants (dropRelease, doubleRelease) hit the same note as before.
int Core::collectSounding(int* slots, bool includeChord) const {
  int n = 0;
  for (int i = 0; i < kSoundingSlots; ++i) {
    const Sounding& x = sounding_[static_cast<std::size_t>(i)];
    if (x.used && (includeChord || !x.chord)) slots[n++] = i;
  }
  std::sort(slots, slots + n, [this](int a, int b) { return sounding_[static_cast<std::size_t>(a)].ref.id < sounding_[static_cast<std::size_t>(b)].ref.id; });
  return n;
}

int Core::oldestSounding() const {
  int best = -1;
  for (int i = 0; i < kSoundingSlots; ++i)
    if (sounding_[static_cast<std::size_t>(i)].used && (best < 0 || sounding_[static_cast<std::size_t>(i)].ref.id < sounding_[static_cast<std::size_t>(best)].ref.id)) best = i;
  return best;
}

// ---- the off schedule ----

void Core::schedInsert(int i) {
  Sounding& x = sounding_[static_cast<std::size_t>(i)];
  int j = schedN_;
  while (j > 0 && x.offPos < sounding_[static_cast<std::size_t>(schedAt(j - 1))].offPos) --j;   // a new off is usually the latest: j stays at the back
  for (int k = schedN_; k > j; --k) schedAt(k) = schedAt(k - 1);
  schedAt(j) = i;
  ++schedN_;
  x.off = kOffSched;
}

void Core::schedErase(int i) {
  if (schedN_ > 0 && schedAt(0) == i) {   // the usual one: an off that has just come due, or the oldest note retriggered or evicted
    schedHead_ = (schedHead_ + 1) & (kSchedRing - 1);
    --schedN_;
    sounding_[static_cast<std::size_t>(i)].off = kOffNone;
    return;
  }
  const I128 key = sounding_[static_cast<std::size_t>(i)].offPos;
  int lo = 0, hi = schedN_;   // the first entry whose key is not less than ours, then along the equal keys to the slot itself
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (sounding_[static_cast<std::size_t>(schedAt(mid))].offPos < key) lo = mid + 1; else hi = mid;
  }
  while (lo < schedN_ && schedAt(lo) != i) ++lo;
  sounding_[static_cast<std::size_t>(i)].off = kOffNone;
  if (lo == schedN_) return;   // not filed (cannot happen: off == kOffSched says it is)
  if (lo < schedN_ - 1 - lo) {   // close the gap from the nearer end: an eviction or a retrigger ends the OLDEST note, whose off is near the front
    for (int k = lo; k > 0; --k) schedAt(k) = schedAt(k - 1);
    schedHead_ = (schedHead_ + 1) & (kSchedRing - 1);
  } else {
    for (int k = lo; k + 1 < schedN_; ++k) schedAt(k) = schedAt(k + 1);
  }
  --schedN_;
}

void Core::quickAdd(int i, std::int64_t t) {
  Sounding& x = sounding_[static_cast<std::size_t>(i)];
  x.quickT = static_cast<std::int32_t>(t);   // a block offset: at most the block length
  if (x.off == kOffQuick) return;
  quick_[static_cast<std::size_t>(quickN_++)] = i;
  x.off = kOffQuick;
}

void Core::quickErase(int i) {
  for (int k = 0; k < quickN_; ++k)
    if (quick_[static_cast<std::size_t>(k)] == i) { quick_[static_cast<std::size_t>(k)] = quick_[static_cast<std::size_t>(--quickN_)]; break; }
  sounding_[static_cast<std::size_t>(i)].off = kOffNone;
}

void Core::unlinkOff(int i) {
  const std::uint8_t where = sounding_[static_cast<std::size_t>(i)].off;
  if (where == kOffSched) schedErase(i);
  else if (where == kOffQuick) quickErase(i);
}

// A quick entry was filed against the block it was born in (its off cannot come before the sample after its on). In the next block that sample
// is already past, so the clamp is moot: a note with a scheduled off joins sched_ by its position, a dying one is due at once (offset 0).
void Core::startBlockOffs() {
  for (int k = quickN_ - 1; k >= 0; --k) {
    const int i = quick_[static_cast<std::size_t>(k)];
    Sounding& x = sounding_[static_cast<std::size_t>(i)];
    if (x.dying) {
      x.quickT = static_cast<std::int32_t>(std::max<std::int64_t>(0, x.onAbs + 1 - absBase_));
    } else {
      quick_[static_cast<std::size_t>(k)] = quick_[static_cast<std::size_t>(--quickN_)];
      schedInsert(i);
    }
  }
}

// When the soonest off is due, as a block offset (kNever: none is). Late offs read 0 (SPEC 13.1); an off never comes before the sample after
// its own on (SPEC 1.1): sched_ holds only offs that already satisfy that, quick_ the others at their clamped offset.
std::int64_t Core::nextOffTime() const {
  std::int64_t best = kNever;
  if (schedN_ > 0) best = std::max<std::int64_t>(0, ofsMemo(memoOff_, sounding_[static_cast<std::size_t>(schedAt(0))].offPos));
  for (int k = 0; k < quickN_; ++k) best = std::min<std::int64_t>(best, sounding_[static_cast<std::size_t>(quick_[static_cast<std::size_t>(k)])].quickT);
  return best;
}

// Is an off at `pos` due by offset `t`, i.e. max(0, ofs(pos)) <= t? Without a division: ofs(pos) = ceil((pos - start) / inc) <= t exactly when
// pos <= start + t * inc (`bound`). The plants that change ofs() itself (floatPosition, roundNotCeil) are asked the long way round.
bool Core::offDueBy(const I128& pos, const I128& bound, std::int64_t t) const {
  assert(inc_ > 0);   // "ofs(pos) <= t  iff  pos <= start + t * inc" is the ceiling of a division by a POSITIVE increment (tempo * TPQ, or 10^6 in time mode)
#ifdef ARP_PLANTS
  if (plants.floatPosition || plants.roundNotCeil) return std::max<std::int64_t>(0, ofs(pos)) <= t;
#else
  (void)t;
#endif
  return pos <= bound;
}

// Removes from the schedule every off due at offset `t` (the value nextOffTime() returned) and returns their slots in ascending output id.
// In sched_ they are a prefix: the offsets are monotone in the position and `ofs(pos) <= t` is `pos <= start + t * inc` (no division).
int Core::takeDueOffs(std::int64_t t, int* out) {
  int n = 0;
  for (int k = quickN_ - 1; k >= 0; --k) {
    const int i = quick_[static_cast<std::size_t>(k)];
    if (sounding_[static_cast<std::size_t>(i)].quickT != t) continue;
    out[n++] = i;
    quick_[static_cast<std::size_t>(k)] = quick_[static_cast<std::size_t>(--quickN_)];
    sounding_[static_cast<std::size_t>(i)].off = kOffNone;
  }
  if (schedN_ > 0) {
    const I128 bound = start_ + I128::mul(t, inc_);
    int taken = 0;
    while (taken < schedN_ && offDueBy(sounding_[static_cast<std::size_t>(schedAt(taken))].offPos, bound, t)) {
      const int i = schedAt(taken++);
      out[n++] = i;
      sounding_[static_cast<std::size_t>(i)].off = kOffNone;
    }
    if (n == 0) {   // only a plant's float ofs() can disagree with the exact bound; the soonest is due by definition, so progress is guaranteed
      const int i = schedAt(taken++);
      out[n++] = i;
      sounding_[static_cast<std::size_t>(i)].off = kOffNone;
    }
    schedHead_ = (schedHead_ + taken) & (kSchedRing - 1);
    schedN_ -= taken;
  }
  for (int a = 1; a < n; ++a) {   // ascending id; the sched_ part is nearly in order already
    const int i = out[a];
    int j = a;
    while (j > 0 && sounding_[static_cast<std::size_t>(out[j - 1])].ref.id > sounding_[static_cast<std::size_t>(i)].ref.id) { out[j] = out[j - 1]; --j; }
    out[j] = i;
  }
  return n;
}

// ---- emission ----

void Core::emitOn(const Pending& p, std::int32_t s) {
  // T13: refuse an on whose own off would not be guaranteed room (the off-reserve): events + sounding + 2 <= cap.
  const bool admit = ARP_PLANT(noOffReserve) ? outN_ < capEv_ : room(2);
  if (!admit) { ++counters_.rows[13]; ++counters_.refused; return; }
  // T2: the same (channel, key) still sounding WITHIN THIS STREAM -> its off goes out first, at this sample. An arp note and a chord
  // note may sound on one key at once (SPEC 5.2, r4 H2). Plant t2AcrossStreams lets the arp retrigger end the chord note too.
  // The key's chain lists newest first; the victims are released oldest first (ascending id), as the table order gave them.
  int victims[kMaxSounding + kSoundingSlack];
  int nv = 0;
  for (int i = keyHead_[static_cast<std::size_t>(keyChain(p.key))]; i >= 0; i = sounding_[static_cast<std::size_t>(i)].keyNext) {
    const Sounding& x = sounding_[static_cast<std::size_t>(i)];
    if (x.chord && !ARP_PLANT(t2AcrossStreams)) continue;
    if (nv < static_cast<int>(std::size(victims)) && !x.dying && x.ref.key == p.key && x.ref.channel == p.ch) victims[nv++] = i;
  }
  // Kept for exactness, not for the shipped build: the chain lists newest first and the old table order was oldest first, and the order of releases is
  // observable through the dropRelease / doubleRelease plants (they hit "the first release"). nv > 1 needs two live notes on one (channel, key), which
  // only the t2AcrossStreams plant allows (a chord note and an arp note); otherwise nv <= 1 and this is a no-op.
  std::reverse(victims, victims + nv);
  for (int i = 0; i < nv; ++i) { ++counters_.rows[2]; releaseSlot(victims[i], s); }
  // T11: the sounding table is full -> the oldest note is released at this offset, before the new on. Notes already
  // released and waiting out their 1-sample minimum (dying) do not count as sounding, and a note whose own on is at THIS
  // sample cannot be released yet (SPEC 1.1): the victim is the oldest note that can. If there is none, the new on is
  // refused (counted) rather than let two notes sound against a one-note table.
  if (ARP_PLANT(t11EvictsFirst)) {   // plant: the first-version rule
    if (soundN_ - chordSoundN_ >= caps_.sounding) { ++counters_.rows[11]; ++counters_.soundingOverflow; releaseSlot(oldestSounding(), s); }
  } else for (;;) {
    // "live" = not dying, and (the chord stage has its own table, SPEC 14) not a chord note; the plant shares the arp's. Counted, not scanned.
    const int live = ARP_PLANT(sharedSoundingTable) ? liveN_ : liveN_ - liveChordN_;
    if (live < caps_.sounding) break;
    // The victim is the oldest live note whose own on is in an earlier sample (a note born in THIS sample cannot be released yet, SPEC 1.1). The live-arp list
    // is in id order, so it is the first one that qualifies: the head, unless the oldest were all born this sample. The plant that lets chord notes count
    // (sharedSoundingTable) has to look at them too, which the list does not hold: it scans the table.
    int victim = -1;
    const std::int64_t bornAfter = absBase_ + s - (ARP_PLANT(evictBornPrevSample) ? 1 : 0);   // evictable: onAbs + 1 <= bornAfter
    if (ARP_PLANT(sharedSoundingTable)) {
      for (int i = 0; i < kSoundingSlots; ++i) {
        const Sounding& x = sounding_[static_cast<std::size_t>(i)];
        if (!x.used || x.dying || x.onAbs + 1 > bornAfter) continue;
        if (victim < 0 || x.ref.id < sounding_[static_cast<std::size_t>(victim)].ref.id) victim = i;
      }
    } else {
      for (int i = liveHead_; i >= 0; i = sounding_[static_cast<std::size_t>(i)].liveNext)
        if (sounding_[static_cast<std::size_t>(i)].onAbs + 1 <= bornAfter) { victim = i; break; }
    }
    if (victim < 0) { ++counters_.refused; return; }
    ++counters_.rows[11];
    ++counters_.soundingOverflow;
    releaseSlot(victim, s);
  }
  // Only reachable if > kSoundingSlack notes die inside this one sample: refuse rather than overrun the table.
  if (soundN_ - chordSoundN_ >= kMaxSounding + kSoundingSlack) { ++counters_.refused; return; }
  const NoteRef ref{0, p.ch, p.key, idCounter_++};
  const int slot = allocSlot(ref, false, absBase_ + s);
  Sounding& x = sounding_[static_cast<std::size_t>(slot)];
  x.offPos = p.offPos;
  x.hasOff = true;
  // The off is due at max(0, ofs(offPos)), but never before the sample after the on (SPEC 1.1). Only a note shorter than a sample is held back
  // by that: it is filed with its clamped offset (quick_), every other one by its position (sched_).
  const std::int64_t due = std::max<std::int64_t>(0, ofs(p.offPos));
  if (!ARP_PLANT(sameSampleOff) && due < static_cast<std::int64_t>(s) + 1) quickAdd(slot, static_cast<std::int64_t>(s) + 1);
  else schedInsert(slot);
#ifdef ARP_PLANTS
  if (p.setsLastQ) { lastQ_ = p.key; hasLastQ_ = true; }   // lastQAtEmission
#endif
  Event ev;
  ev.type = EventType::NoteOn;
  ev.offset = s;
  ev.ref = ref;
  ev.key = p.key;
  ev.vel = p.vel;
  ev.stream = (ARP_PLANT(untaggedOn) && ref.id % 7 == 3) ? Stream::Chord : Stream::Arp;   // every note the arp emits carries `arp` (SPEC 1, D-009)
  ev.tick = tickOf(p.pos);
  ev.stepSeq = p.ord;
  pushEvent(ev);   // cannot fail: room(2) above reserved this slot and the off's
}

// release(ref): the ONLY path that emits a NoteOff. Idempotent: a ref no longer in the table is a no-op.
bool Core::release(const NoteRef& ref, std::int32_t s) {
  const int i = findSounding(ref);
  return i >= 0 && releaseSlot(i, s);
}

bool Core::releaseSlot(int i, std::int32_t s) {
  Sounding& x = sounding_[static_cast<std::size_t>(i)];
  // SPEC 1.1: never at the same sample as its own on -> keep it "dying"; the timeline emits it at on + 1.
  const bool defer = !ARP_PLANT(sameSampleOff);
  if (defer && x.onAbs + 1 > absBase_ + s) {
    if (!x.dying) { --liveN_; if (x.chord) --liveChordN_; else liveUnlink(i); }
    x.dying = true;
    if (x.hasOff) { unlinkOff(i); x.hasOff = false; }   // its scheduled off is void; it is due at the sample after its on, whatever offPos said
    quickAdd(i, x.onAbs + 1 - absBase_);
    return true;
  }
  Event ev;
  ev.type = EventType::NoteOff;
  ev.offset = s;
  ev.ref = x.ref;
  ev.vel = kReleaseVel;
  ev.tick = x.hasOff ? tickOf(x.offPos) : kNoTick;
  if (x.off != kOffNone) unlinkOff(i);   // a scheduled off that just came due was taken off the schedule already (takeDueOffs)
  freeSlot(i);
#ifdef ARP_PLANTS
  if (plants.dropRelease && !dropped_) { dropped_ = true; return true; }
#endif
  if (!pushEvent(ev)) ++counters_.offsLost;
#ifdef ARP_PLANTS
  if (plants.doubleRelease && !doubled_) { doubled_ = true; pushEvent(ev); }
#endif
  return true;
}

// Release-all (T4, T6, T7, T10): walks the sounding table and CLEARS the pending queue (SPEC 7). `includeChord` is false for the
// arp's own release-all (T6, the arp switching off alone): chord notes behave like held keys and are not touched (SPEC 14, T21).
void Core::releaseAll(std::int32_t s, bool includeChord) {
  if (!ARP_PLANT(releaseKeepsPending)) pendN_ = 0;
  if (!ARP_PLANT(lastQNoReset)) hasLastQ_ = false;   // SPEC 6 (r2 H2): every release-all forgets lastQ, so each play-from-here sounds the same
  if (includeChord) chordEndAll(s);
  int slots[kSoundingSlots];
  const int n = collectSounding(slots, includeChord);
  for (int i = 0; i < n; ++i) releaseSlot(slots[i], s);
}


// ------------------------------------------------------------------------------------------ the chord stage (SPEC 14)
//
// A second note processor in front of the arp. Each held key h (any of DOWN, PEDAL, LATCHED) becomes the played note plus up to six
// shifted notes on the `chord` stream. The mapping is computed ONCE when h enters H and STORED per (h, slot); it is re-mapped only at
// offset 0 of a block whose signature (chord_* settings, scale settings, the arp's quantize mode, the global scale when it is the
// source) changed, or when the stage turns on. Chord notes are deduplicated by KEY with a source count; a note starts when its count
// goes 0 -> 1 and ends (exactly one NoteOff, through release()) when it reaches 0, at chord_enabled off, module off, panic and reset.
// Never at a transport edge or an arp_enabled change (T21).

bool Core::chordStart(std::int32_t key, std::int32_t vel, std::int32_t ch, std::int32_t s, NoteRef& refOut) {
  if (ARP_PLANT(chordOnLate) && sr_ >= 96000 && s + 1 < n_) ++s;   // plant (G4 / CH1): the on lands a sample after its cause, at 96 kHz and up
  // T13 (SPEC 14): refused at the event cap, a chord note stays silent and is counted; no sounding entry, so no off is owed.
  if (!room(2)) { ++counters_.rows[13]; ++counters_.refused; return false; }
  if (chordSoundN_ >= kMaxChordSounding || soundN_ >= kSoundingSlots) { ++counters_.refused; return false; }
  const NoteRef ref{0, ch, key, idCounter_++};
  allocSlot(ref, true, absBase_ + s);   // a chord note has no scheduled off: it ends with its last source (chordStop)
  Event ev;
  ev.type = EventType::NoteOn;
  ev.offset = s;
  ev.ref = ref;
  ev.key = key;
  ev.vel = vel;
  ev.stream = (ARP_PLANT(chordTaggedArp) && ref.id % 2 == 1) ? Stream::Arp : Stream::Chord;
  ev.tick = tickOf(posAt(s)) + (ARP_PLANT(chordOnTickSkew) ? 1 : 0);
  ev.stepSeq = -1;   // chord-stream ons sort BEFORE the arp's at one offset (SPEC 1.1, r4 L2)
  pushEvent(ev);     // cannot fail: room(2) reserved this slot and the off's
  refOut = ref;
  return true;
}

void Core::chordStop(NoteRef& ref, bool& sounding, std::int32_t s) {
  if (!sounding) return;
  sounding = false;
  ++counters_.rows[21];
  release(ref, s);
}

void Core::chordEndAll(std::int32_t s) {
  // chordSetN_ is the number of used notes after every complete pass, and 0 once the stage has forgotten them, so with 0 the walk finds nothing: skip it.
  // chordSync lands here at every input sample while the stage is OFF, where 128 entries of ~80 bytes were scanned for nothing.
  if (chordSetN_ != 0)
    for (ChordNote& n : chordNote_) {
      if (!n.used) continue;
      chordStop(n.ref, n.sounding, s);
#ifdef ARP_PLANTS
      chordStop(n.ref2, n.sounding2, s);
#endif
      n = ChordNote{};
    }
  chordRecN_ = 0;
  chordSetN_ = 0;
  chordSigValid_ = false;
#ifdef ARP_PLANTS
  chordDrift_.fill(0);
#endif
}

// The stage's effective scale (SPEC 14 note 13): the arp's source, root and mask, with the chord_root anchor on the lowest PLAYED key (`lowest`, when a key is held).
Core::ChordQuant Core::chordQuant(const ChordSigData& sig, bool haveLowest, std::int32_t lowest) const {
  ChordQuant q;
  if (S_.quantize != Quantize::Off) {
    if (S_.scaleSource == ScaleSource::Own) { q.root = static_cast<std::int32_t>(mod(S_.scaleRoot, 12)); q.mask = sig.ownMask; }
    else { q.root = static_cast<std::int32_t>(mod(scale_.root, 12)); q.mask = scale_.mask & 0xFFFu; }
    if (S_.quantize == Quantize::ChordRoot) {
      if (haveLowest) q.root = static_cast<std::int32_t>(mod(lowest, 12));
      if (q.mask != 0) q.mask |= 1u;   // the anchor's own pitch class is always admitted (13b.1)
    }
  }
  q.on = q.mask != 0 && !ARP_PLANT(chordSkipQuantize);
  q.degrees = S_.chordUnit == StepUnit::Degrees;
  return q;
}

// A chord key is snapped to the nearest in-scale key; a tie ALWAYS goes to the lower key and the stage never reads or writes the
// arp's lastQ (D-020). Plants chordReadsLastQ / chordWritesLastQ do one each.
std::int64_t Core::chordSnap(const ChordQuant& q, std::int64_t k) {
  const bool reads = ARP_PLANT(chordReadsLastQ);
  const std::int64_t r = quantizeKey(k, q.root, q.mask, reads && hasLastQ_, lastQ_, !reads);
  if (ARP_PLANT(chordWritesLastQ)) { lastQ_ = r; hasLastQ_ = true; }
  return r;
}

// SPEC 14 "The mapping is stored": slot 0 is h snapped while quantize is on; semitones: h + shift, then snapped; degrees: from h's
// nearest in-scale key (ties lower), move `shift` in-scale keys (a degree is one semitone with quantize off or a zero mask). Keys
// outside 0-127 are dropped (T16). SPEC-GAP: an input key outside 0-127 is dropped before it is snapped (SPEC 6: out-of-range keys
// are dropped first), and in degrees mode it has no scale position, so all its slots are dropped.
void Core::chordMapRec(ChordRec& r, const ChordQuant& q) {
  r.n = 0;
  auto add = [&](int slot, std::int64_t k) {
    if (k < 0 || k > 127) { ++counters_.rows[16]; ++counters_.keysDropped; return; }
    r.key[r.n] = static_cast<std::int16_t>(k);
    r.slot[r.n] = static_cast<std::int8_t>(slot);
    ++r.n;
  };
  const std::int64_t hk = r.h.key;
  if (q.degrees) {
    if (hk < 0 || hk > 127) { ++counters_.rows[16]; ++counters_.keysDropped; return; }
    const std::int64_t base = q.on ? chordSnap(q, hk) : hk;
    add(0, (q.on && ARP_PLANT(chordPlusZeroUnquantized)) ? hk : base);
    for (int i = 0; i < 6; ++i)
      if (S_.chordOn[i]) add(i + 1, degreeMove(base, S_.chordShift[i], q.root, q.on ? q.mask : 0));
    return;
  }
  auto semis = [&](std::int64_t k, bool snapIt) { return (snapIt && q.on && k >= 0 && k <= 127) ? chordSnap(q, k) : k; };
  add(0, semis(hk, !ARP_PLANT(chordPlusZeroUnquantized)));
  for (int i = 0; i < 6; ++i)
    if (S_.chordOn[i]) add(i + 1, semis(hk + S_.chordShift[i], true));
}

// The plants that change what a pass does inside (they live in the full pass below), and the reference mode: those always rebuild the whole stage.
bool Core::chordFullPassForced() const {
#ifdef ARP_PLANTS
  return plants.chordFullPass || plants.chordNoDedupe || plants.chordDedupeByChannelKey || plants.chordRecomputeAtRelease || plants.chordRestartAll ||
         plants.chordEndsOnFirstSource || plants.chordStalePressTime || plants.chordSkipQuantize || plants.chordPlusZeroUnquantized ||
         plants.chordReadsLastQ || plants.chordWritesLastQ;
#else
  return false;
#endif
}

void Core::chordSync(std::int32_t s) {
  if (!(moduleOn_ && S_.chordEnabled)) {   // the stage is off (or the module is, T4): every chord note ends and the stage forgets its keys
    chordEndAll(s);
    return;
  }
  ChordSigData sig;
  sig.unit = S_.chordUnit;
  sig.source = S_.scaleSource;
  sig.quantize = S_.quantize;
  sig.root = S_.scaleRoot;
  for (int d = 0; d < 12; ++d) sig.ownMask = static_cast<std::uint16_t>(sig.ownMask | (S_.scaleDeg[d] ? 1u << d : 0u));
  if (S_.scaleSource == ScaleSource::Global) { sig.globalRoot = scale_.root; sig.globalMask = static_cast<std::uint16_t>(scale_.mask & 0xFFFu); }
  for (int i = 0; i < 6; ++i) { sig.on[i] = S_.chordOn[i]; sig.shift[i] = S_.chordShift[i]; }
  const bool remap = !chordSigValid_ || !(sig == chordSig_);
  // Nothing the stage reads has changed since its last pass (same settings and scale; the same keys in H: refsVer_ moves when a ref joins or leaves,
  // and a ref's Down / Pedal / Latched state is not part of H), so the pass would store, start and stop nothing and rebuild the same chord set:
  // skip it. The block-start and per-step syncs of a held chord are nearly all of this kind.
  // The skip RELIES ON A PASS BEING IDEMPOTENT: run twice on the same inputs it does nothing the second time. That holds because a chord note whose
  // chordStart was refused (no event room, T13) keeps `used` with no sounding and is never retried, in this version of the rules; a stage that retried
  // a refused start at every pass would have to run every time. The dedupe plants do exactly that (a refused second on), so they always run the pass.
  if (!remap && chordSyncVer_ == refsVer_ && !ARP_PLANT(chordNoDedupe) && !ARP_PLANT(chordDedupeByChannelKey)) return;
  // Same settings, a few keys changed: update the stage for those keys alone. Anything else (a re-map, many keys, a plant) rebuilds it.
  if (!remap && chordIncremental(s)) {
    chordSyncVer_ = refsVer_;
    keysDirty_.fill(0);
    dirtyAll_ = false;
    return;
  }
  chordRebuild(s, sig, remap);
}

// The whole pass: the definition of what the stage does (M3a's algorithm, unchanged). A function of its own so that chordSync, which also runs the incremental
// pass, does not hold this one's ~5 KB of stack arrays while it does.
void Core::chordRebuild(std::int32_t s, const ChordSigData& sig, bool remap) {
  chordSig_ = sig;
  chordSigValid_ = true;

  HeldKey H[kMaxHeldKeys];
  const int hn = collectHeld(H);   // H, ascending: H[0] is the lowest PLAYED key

  const ChordQuant quant = chordQuant(sig, hn > 0, hn > 0 ? H[0].key : 0);

  // -- the stored mappings: leavers go (their STORED keys stop counting), joiners are mapped once, a re-map recomputes every held key
  int w = 0;
  for (int i = 0; i < chordRecN_; ++i) {
    const ChordRec& r = chordRec_[static_cast<std::size_t>(i)];
    bool stays = false;
    for (int j = 0; j < hn; ++j) stays = stays || H[j].key == r.h.key;
    if (stays) { chordRec_[static_cast<std::size_t>(w++)] = r; continue; }
#ifdef ARP_PLANTS
    if (plants.chordRecomputeAtRelease) {   // plant (r4 H1): the leaver's chord keys are recomputed NOW and subtracted, not its stored ones
      ChordRec fresh = r;
      chordMapRec(fresh, quant);
      for (int t = 0; t < r.n; ++t) ++chordDrift_[static_cast<std::size_t>(r.key[t])];
      for (int t = 0; t < fresh.n; ++t) --chordDrift_[static_cast<std::size_t>(fresh.key[t])];
    }
#endif
  }
  chordRecN_ = w;
#ifdef ARP_PLANTS
  if (remap && plants.chordRestartAll)   // plant: a re-map ends and restarts every chord note
    for (ChordNote& n : chordNote_) if (n.used) { chordStop(n.ref, n.sounding, s); chordStop(n.ref2, n.sounding2, s); n = ChordNote{}; }
#endif
  for (int j = 0; j < hn; ++j) {
    ChordRec* rec = nullptr;
    for (int i = 0; i < chordRecN_; ++i) if (chordRec_[static_cast<std::size_t>(i)].h.key == H[j].key) rec = &chordRec_[static_cast<std::size_t>(i)];
    if (rec == nullptr) {
      if (chordRecN_ >= static_cast<int>(chordRec_.size())) continue;   // cannot happen: H has at most 32 keys
      rec = &chordRec_[static_cast<std::size_t>(chordRecN_++)];
      *rec = ChordRec{};
      rec->h = H[j];
      chordMapRec(*rec, quant);
    } else {
      // SPEC 3 / 14: H' orders keys by the press of the EARLIEST LIVE ref of a source key, which moves when the earliest of two refs on
      // one key is released. Refresh the record's held key (press time, velocity, channel) from H on every sync; the STORED keys are
      // untouched (a re-map below is the only thing that recomputes them). Plant chordStalePressTime keeps the entering press.
      if (!ARP_PLANT(chordStalePressTime)) rec->h = H[j];
      if (remap) chordMapRec(*rec, quant);
    }
  }

  // -- the source counts, and the earliest source of every key (press sample, then slot, then the source key): its channel and
  //    velocity are fixed at the note's NoteOn
  int cnt[128] = {};
  std::int64_t firstAbs[128] = {};   // zeroed, not left to firstRec[k] != nullptr: plant chordRecomputeAtRelease can count a key no record maps to
  std::int32_t firstSlot[128] = {}, firstSrc[128] = {};
  const ChordRec* firstRec[128] = {};
#ifdef ARP_PLANTS
  std::int32_t chA[128], chB[128];   // chordDedupeByChannelKey: the first two channels seen on a key
  bool chTwo[128] = {};
  for (int k = 0; k < 128; ++k) chA[k] = chB[k] = -1;
#endif
  for (int i = 0; i < chordRecN_; ++i) {
    const ChordRec& r = chordRec_[static_cast<std::size_t>(i)];
    for (int t = 0; t < r.n; ++t) {
      const int k = r.key[t];
      ++cnt[k];
      const bool better = firstRec[k] == nullptr || r.h.abs < firstAbs[k] || (r.h.abs == firstAbs[k] && (r.slot[t] < firstSlot[k] || (r.slot[t] == firstSlot[k] && r.h.key < firstSrc[k])));
      if (better) { firstRec[k] = &r; firstAbs[k] = r.h.abs; firstSlot[k] = r.slot[t]; firstSrc[k] = r.h.key; }
#ifdef ARP_PLANTS
      if (chA[k] < 0) chA[k] = r.h.ch; else if (chA[k] != r.h.ch) chTwo[k] = true;
#endif
    }
  }
  chordSetN_ = 0;
  for (int k = 0; k < 128; ++k) {
    ChordNote& n = chordNote_[static_cast<std::size_t>(k)];
    int c = cnt[k];
#ifdef ARP_PLANTS
    if (plants.chordRecomputeAtRelease) c = std::max(0, c + chordDrift_[static_cast<std::size_t>(k)]);
    if (plants.chordNoDedupe || plants.chordDedupeByChannelKey) {   // plants: a second on for a shared key (by source, or by channel)
      const bool want2 = c > 0 && (plants.chordNoDedupe ? c >= 2 : chTwo[k]);
      if (want2 && !n.sounding2 && n.used) { if (chordStart(k, n.vel, n.ch, s, n.ref2)) n.sounding2 = true; }
      if (!want2 && n.sounding2) chordStop(n.ref2, n.sounding2, s);
    }
#endif
    if (c == 0) {
      if (n.used) {
        chordStop(n.ref, n.sounding, s);
#ifdef ARP_PLANTS
        chordStop(n.ref2, n.sounding2, s);
#endif
        n = ChordNote{};
      }
      continue;
    }
#ifdef ARP_PLANTS
    if (plants.chordEndsOnFirstSource && n.used && c < n.maxCount) chordStop(n.ref, n.sounding, s);   // plant: ends when ANY source leaves
#endif
    if (!n.used) {   // count 0 -> 1: the note starts, with the channel and velocity of its earliest source
      n.used = true;
      n.vel = firstRec[k]->h.vel;
      n.ch = firstRec[k]->h.ch;
      n.sounding = chordStart(k, n.vel, n.ch, s, n.ref);
#ifdef ARP_PLANTS
      n.maxCount = 0;
      if ((plants.chordNoDedupe && c >= 2) || (plants.chordDedupeByChannelKey && chTwo[k])) {
        if (chordStart(k, n.vel, n.ch, s, n.ref2)) n.sounding2 = true;
      }
#endif
    }
    n.count = c;
#ifdef ARP_PLANTS
    n.maxCount = std::max(n.maxCount, c);
#endif
    // H' for the arp: this key with the chord note's velocity and channel, ordered by its first source's press, then slot
    chordSet_[static_cast<std::size_t>(k)] = HeldKey{k, n.vel, n.ch, firstAbs[k], firstSlot[k]};
    ++chordSetN_;
  }
  chordSyncVer_ = refsVer_;
  keysDirty_.fill(0);
  dirtyAll_ = false;
}

// The pass for a small change of H (M3c, D-032), with the signature unchanged. The full pass above is the definition; this does the same work for the keys
// that changed and leaves the rest of the stage as it is, and must leave EXACTLY the state the full pass would (tests: the digests, which come from the M3a
// core, and plant chordFullPass, which runs every pass whole). Why the rest of the stage is untouched:
//   * a record (a held key's stored mapping) changes only if its key left H, joined H, or its held entry changed (the earliest live ref moved: press sample,
//     velocity, channel); all of those are keys whose refs came or went, i.e. dirty keys. Every other record is what the full pass would rebuild it as;
//   * a chord key's source count and earliest source change only if a changed record maps to it. Those are recounted from ALL records (the same loop as the full
//     pass, over the keys concerned); every other key keeps its count, its note and its chord-set entry;
//   * the notes start and stop in ascending key order, as in the full pass, so output ids and the event order are the same.
// Returns false (having changed nothing that matters) when it cannot be sure: Panic / a reset / a key outside 0..127 / too many keys / more records than the table.
bool Core::chordIncremental(std::int32_t s) {
  constexpr int kMaxDirtyKeys = 16;   // beyond this a rebuild is as cheap
  if (dirtyAll_ || chordFullPassForced()) return false;
  int dk[kMaxDirtyKeys] = {};
  int nd = 0;
  for (std::size_t w = 0; w < 2; ++w)
    for (std::uint64_t bits = keysDirty_[w]; bits != 0; bits &= bits - 1) {
      if (nd == kMaxDirtyKeys) return false;
      dk[nd++] = static_cast<int>(w * 64) + std::countr_zero(bits);
    }

  // -- the held entry of each dirty key now: its EARLIEST live ref, the first of them in table order among equal press samples (collectHeld's rule)
  HeldKey cur[kMaxDirtyKeys];
  bool here[kMaxDirtyKeys] = {};
  for (int i = 0; i < refN_; ++i) {
    const Ref& r = refs_[static_cast<std::size_t>(i)];
    if (!isDirty(r.key)) continue;
    int j = 0;
    while (dk[j] != r.key) ++j;   // found: the key is dirty
    if (!here[j] || r.abs < cur[j].abs) { cur[j] = HeldKey{r.key, r.vel, r.ch, r.abs}; here[j] = true; }
  }

  // -- which records leave, which are refreshed, which keys join; and the chord keys these touch (bit k of `touched`)
  std::uint64_t touched[2] = {0, 0};
  auto touch = [&](const ChordRec& r) {
    for (int t = 0; t < r.n; ++t) touched[r.key[t] >> 6] |= std::uint64_t{1} << (r.key[t] & 63);
  };
  std::uint32_t leave = 0;   // bit i: chordRec_[i] leaves (at most kMaxHeldKeys = 32 records)
  int joiner[kMaxDirtyKeys] = {};
  int nj = 0, leavers = 0;
  for (int j = 0; j < nd; ++j) {
    int ri = -1;
    for (int i = 0; i < chordRecN_; ++i)
      if (chordRec_[static_cast<std::size_t>(i)].h.key == dk[j]) { ri = i; break; }
    if (ri < 0) {
      if (here[j]) joiner[nj++] = j;
      continue;
    }
    ChordRec& rec = chordRec_[static_cast<std::size_t>(ri)];
    if (!here[j]) { leave |= std::uint32_t{1} << ri; ++leavers; touch(rec); }
    else if (!ARP_PLANT(chordIncSkipsRefresh) && (rec.h.abs != cur[j].abs || rec.h.vel != cur[j].vel || rec.h.ch != cur[j].ch)) { rec.h = cur[j]; touch(rec); }   // (refreshing a record is harmless if we give up below: the full pass sets it again)
  }
  if (chordRecN_ - leavers + nj > static_cast<int>(chordRec_.size())) return false;   // cannot happen: H has at most 32 keys

  // -- apply: leavers out (the others keep their order), joiners in at the end in ascending key order, each mapped once
  if (leave != 0) {
    int w = 0;
    for (int i = 0; i < chordRecN_; ++i)
      if (((leave >> i) & 1u) == 0) chordRec_[static_cast<std::size_t>(w++)] = chordRec_[static_cast<std::size_t>(i)];
    chordRecN_ = w;
  }
  if (nj > 0) {
    // chord_root anchors on the lowest PLAYED key: a joiner is held, so there is one. (lowestHeldKey() starts at 127, which only matters when every key is above it.)
    const ChordQuant quant = chordQuant(chordSig_, true, S_.quantize == Quantize::ChordRoot ? lowestHeldKey() : 0);
    for (int q = 0; q < nj; ++q) {
      ChordRec& rec = chordRec_[static_cast<std::size_t>(chordRecN_++)];
      rec = ChordRec{};
      rec.h = cur[joiner[q]];
      chordMapRec(rec, quant);
      touch(rec);
    }
  }

  // -- recount the touched chord keys from all records (the full pass's loop, restricted): source count and earliest source (press sample, slot, source key)
  struct Acc { std::int64_t abs = 0; std::int32_t slot = 0, src = 0, vel = 0, ch = 0, count = 0; };
  Acc acc[128] = {};
  auto isTouched = [&](int k) { return ((touched[k >> 6] >> (k & 63)) & 1u) != 0; };
  for (int i = 0; i < chordRecN_; ++i) {
    const ChordRec& r = chordRec_[static_cast<std::size_t>(i)];
    for (int t = 0; t < r.n; ++t) {
      const int k = r.key[t];
      if (!isTouched(k)) continue;
      Acc& a = acc[k];
      const bool better = a.count == 0 || r.h.abs < a.abs || (r.h.abs == a.abs && (r.slot[t] < a.slot || (r.slot[t] == a.slot && r.h.key < a.src)));
      ++a.count;
      if (better) { a.abs = r.h.abs; a.slot = r.slot[t]; a.src = r.h.key; a.vel = r.h.vel; a.ch = r.h.ch; }
    }
  }

  // -- the notes of the touched keys, in ascending key order: the count went to 0 (the note ends), 0 -> 1 (it starts with its earliest source's channel and velocity), or neither
  auto settle = [&](int k) {
    ChordNote& n = chordNote_[static_cast<std::size_t>(k)];
    const Acc& a = acc[k];
    if (a.count == 0) {
      if (n.used) {
        chordStop(n.ref, n.sounding, s);
        n = ChordNote{};
        --chordSetN_;
      }
      return;
    }
    if (!n.used) {
      n.used = true;
      n.vel = a.vel;
      n.ch = a.ch;
      n.sounding = chordStart(k, n.vel, n.ch, s, n.ref);
      ++chordSetN_;
    }
    n.count = a.count;
    chordSet_[static_cast<std::size_t>(k)] = HeldKey{k, n.vel, n.ch, a.abs, a.slot};
  };
  if (ARP_PLANT(chordIncDescending)) {
    for (int k = 127; k >= 0; --k)
      if (isTouched(k)) settle(k);
  } else {
    for (std::size_t w = 0; w < 2; ++w)
      for (std::uint64_t bits = touched[w]; bits != 0; bits &= bits - 1) settle(static_cast<int>(w * 64) + std::countr_zero(bits));
  }
  return true;
}

}  // namespace arp
