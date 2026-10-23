// core.cpp - Arp's note processor, M2a scope (ROADMAP T-003, T-004, T-005).
//
// Implemented: SPEC 1 (block I/O, 1.1 event order, 1.2 identity, 1.3 capacities), 2.1-2.6 (tick grid, exact three-band
// position, host + free clocks, epoch, TIME MODE with the smooth step-time sweep), 3 (held set), 4.1 (counters,
// once-guard), 4.2 all 16 order styles, 4.3 every step-pattern field (bank embedded from spec/bank/starter.json, plus the
// user pattern, 4.6), 4.4 velocity, 4.5 restart `chord`, 4.7 transposition steps and step offsets in semitones AND degrees
// (the six-stage order), 5.1-5.2 (on-times with swing, span, ratchets, ties by look-ahead, gate incl. > 100 %, T2), 6 scale
// quantize (global or own scale, `scale` / `chord_root`, the lastQ tie chain), 7 (terminals), 8 (triggers), 9, 10.
//
// SPEC-GAP: M2b - settings outside that scope are ACCEPTED AND IGNORED, never half-implemented:
//   restart bar / free (play as `chord`), repeats, chord_* and the chord stage, the morph timing of discrete settings.
//
// Everything is fixed-capacity: the only allocations are in prepare().
// (__FINITE_MATH_ONLY__ is predefined as 0 normally and 1 under -ffinite-math-only, so test its VALUE.)
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
#error "arp core requires IEEE semantics (NaN/inf checks on the host's floats): build without -ffast-math / -ffinite-math-only"
#endif

#include "arp/core.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

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
}

bool Core::prepare(int sampleRate, int maxBlock) {
  prepared_ = false;
  if (sampleRate < kMinSampleRate || sampleRate > kMaxSampleRate || maxBlock < 1 || maxBlock > kMaxBlock) return false;
  // T10: a note still owed a NoteOff is not forgotten: its ref moves to storage that survives prepare()
  // and is released at offset 0 of the next process() / reset().
  if (!ARP_PLANT(prepareForgetsNotes))
    for (int i = 0; i < soundN_ && owedN_ < static_cast<int>(owed_.size()); ++i) owed_[static_cast<std::size_t>(owedN_++)] = sounding_[static_cast<std::size_t>(i)].ref;
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
  refN_ = 0;
  ignoredN_ = 0;
  pedalDown_ = false;
  moduleOn_ = false;
  enabled_ = false;
  idCounter_ = 0;
  soundN_ = 0;
  pendN_ = 0;
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

// When a sounding note's off is due, as a block offset: its scheduled off, but never earlier than
// the sample after its own on (SPEC 1.1). kNever for a note with nothing scheduled.
std::int64_t Core::offTime(Sounding& s) {
  if (!s.hasOff && !s.dying) return kNever;
  const std::int64_t earliest = s.onAbs + 1 - absBase_;
  std::int64_t base = 0;
  if (s.hasOff) {
    if (s.cacheBlk != blk_) {   // the offset is invariant within a block (a free-clock re-base shifts offPos and start together)
      s.ofsCache = std::max<std::int64_t>(0, ofs(s.offPos));
      s.cacheBlk = blk_;
    }
    base = s.ofsCache;
  }
  return ARP_PLANT(sameSampleOff) ? base : std::max(base, earliest);
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
  if (soundN_) ++counters_.rows[6];
  releaseAll(0);
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
  if (hasAnchor_ || refN_ > 0) {
    anchorK_ = lastEvaluated_ + 1;
    hasAnchor_ = true;
  } else {
    hasAnchor_ = false;
  }
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
  for (int i = 0; i < soundN_; ++i)
    if (sounding_[i].hasOff) sounding_[i].offPos -= shift;
  for (int i = 0; i < pendN_; ++i) {
    pending_[i].pos -= shift;
    pending_[i].offPos -= shift;
  }
  epoch_ += 1;   // uint32: wraps mod 2^32
  freeRunning_ = true;
  lastEvaluated_ = -1;
  hasLastEvaluated_ = true;
  gO_ = 0;           // time mode: step 0 lands on the press
  gK1_ = 0;
  m_ = 0;
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
  ++blk_;
  if (ARP_PLANT(allocInProcess)) plantAlloc();
  flushOwed();
  n_ = b.n;
  S_ = b.settings;
  scale_ = b.scale;   // SPEC 6: horde's scale is read, never owned; steps evaluated in this block quantize to it
  absBase_ = absSample_;
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
      if (modOn) {   // the arp alone off: its notes end (the chord stage, M2, would be untouched)
        if (soundN_ > 0) ++counters_.rows[4];
        releaseAll(0);
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
    active_.latch = S_.latch;
  }

  // -- the timeline: input events, due offs, pending ons, grid-step evaluations, in time order
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
    const bool running = enabled_ && (clock_ == Clock::Host || freeRunning_) && !timeHeldOnly;
    if (running && !hasLastEvaluated_) {
      lastEvaluated_ = kAtOrAfter(posAt(static_cast<std::int32_t>(cur))) - 1;
      hasLastEvaluated_ = true;
    }
    // The next thing to happen. On a tie the earlier kind wins: input, then offs, then pending ons, then the grid step.
    std::int64_t t = kNever;
    int kind = -1, idx = -1;
    if (ii < b.in.size()) { t = b.in[ii].offset; kind = 0; }
    for (int q = 0; q < soundN_; ++q) {
      const std::int64_t s = offTime(sounding_[q]);
      if (s < t) { t = s; kind = 1; idx = q; }
    }
    int pi = -1;
    for (int q = 0; q < pendN_; ++q)
      if (pi < 0 || pending_[q].pos < pending_[pi].pos) pi = q;
    if (pi >= 0) {
      const std::int64_t s = std::max<std::int64_t>(0, ofs(pending_[pi].pos));
      if (s < t) { t = s; kind = 2; idx = pi; }
    }
    if (running) {
      const std::int64_t s = std::max<std::int64_t>(0, ofs(I128::mul(gtick(lastEvaluated_ + 1), D_)));
      if (s < t) { t = s; kind = 3; }
    }
    const bool inWindow = ARP_PLANT(clampOffset) ? t <= n_ : t < n_;   // half-open window; the plant lets an event at n through
    if (kind < 0 || !inWindow) break;
    if (t < cur) t = cur;   // a LATE event (SPEC 13.1) is emitted now
    cur = t;
    const auto so = static_cast<std::int32_t>((ARP_PLANT(clampOffset) && t >= n_) ? n_ - 1 : t);
    if (kind == 0) {
      input(b.in[ii++], so);
    } else if (kind == 1) {
      const NoteRef ref = sounding_[idx].ref;
      if (sounding_[idx].hasOff) ++counters_.rows[1];
      release(ref, so);
    } else if (kind == 2) {
      const Pending p = pending_[idx];
      for (int q = idx + 1; q < pendN_; ++q) pending_[q - 1] = pending_[q];
      --pendN_;
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
  // Events are produced in time order, so this only has to fix same-offset order; insertion sort
  // is stable (pass-through keeps input order) and near-linear on nearly-sorted data.
  for (int i = 1; i < outN_; ++i) {
    const Event e = out_[static_cast<std::size_t>(i)];
    int j = i;
    while (j > 0 && sortsBefore(e, out_[static_cast<std::size_t>(j - 1)])) {
      out_[static_cast<std::size_t>(j)] = out_[static_cast<std::size_t>(j - 1)];
      --j;
    }
    out_[static_cast<std::size_t>(j)] = e;
  }
}

// ------------------------------------------------------------------------------------------ input (SPEC 3)

template <class Pred>
void Core::eraseRefs(Pred pred) {
  int w = 0;
  for (int i = 0; i < refN_; ++i)
    if (!pred(refs_[static_cast<std::size_t>(i)])) refs_[static_cast<std::size_t>(w++)] = refs_[static_cast<std::size_t>(i)];
  refN_ = w;
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
      refN_ = 0;
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
  auto countKey = [&](int& total, int& latched) {
    total = latched = 0;
    for (int i = 0; i < refN_; ++i) {
      const Ref& r = refs_[static_cast<std::size_t>(i)];
      if (r.key == key) { ++total; if (r.state == RefState::Latched) ++latched; }
    }
  };
  int sameKey = 0, sameLatched = 0;
  countKey(sameKey, sameLatched);
  // Under latch append a press on a key held only by LATCHED refs toggles it out; its own NoteOff is then ignored.
  if (latch == Latch::Append && sameKey > 0 && sameLatched == sameKey && !ARP_PLANT(noLatchToggle)) {
    eraseRefs([key](const Ref& r) { return r.key == key; });
    ignoreRef(id);
    return;
  }
  eraseRefs([id](const Ref& r) { return r.id == id; });   // the previous press of this ref (PEDAL or LATCHED) gives way to the fresh one
  countKey(sameKey, sameLatched);
  if (sameKey == 0) {
    int distinct = 0;
    for (int i = 0; i < refN_; ++i) {
      bool seen = false;
      for (int j = 0; j < i && !seen; ++j) seen = refs_[static_cast<std::size_t>(j)].key == refs_[static_cast<std::size_t>(i)].key;
      if (!seen) ++distinct;
    }
    if (distinct >= caps_.held) { ++counters_.heldOverflow; ignoreRef(id); return; }
  }
  if (sameKey >= caps_.refsPerKey) { ++counters_.refOverflow; ignoreRef(id); return; }
  Ref r;
  r.id = id;
  r.key = key;
  r.vel = e.vel;
  r.ch = id.channel;
  r.state = RefState::Down;
  r.abs = absBase_ + s;
  refs_[static_cast<std::size_t>(refN_++)] = r;
  // A gesture start (SPEC 3): no ref was DOWN or PEDAL. Under append, a press that joins a latched set is not one (4.5).
  if (noActive && !(latch == Latch::Append && hadLatched)) gesture(s);
}

void Core::gesture(std::int32_t s) {
  if (!enabled_) return;
  if (clock_ == Clock::Free && !(ARP_PLANT(timeAnchorsTransport) && timeMode_)) { freeStart(s); return; }   // re-base the free (or time) clock at the press (H7a)
  // Restart `chord` on the host clock: the anchor is the first step not yet evaluated at this sample (SPEC 13.4).
  if (!hasLastEvaluated_) {
    lastEvaluated_ = kAtOrAfter(posAt(s)) - 1;
    hasLastEvaluated_ = true;
  }
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
    switchRate(kAbs);   // T15: a rate change applies at the next step of the OLD rate (SPEC 9)
    return;
  }
  lastEvaluated_ = kAbs;
  adopt();
  if (!hasAnchor_) return;   // restart `chord`: no anchor before the first gesture (SPEC 2.5)
  evalStep(kAbs, so);
}

// T15: the new grid anchors at the first new-grid step at or after the old step's grid tick (SPEC 13.8).
void Core::switchRate(std::int64_t kAbs) {
  const std::int64_t oldTick = kAbs * L_;
  active_.rate = S_.rate;
  L_ = rateTicks(S_.rate);
  const std::int64_t kNew = cdiv(oldTick, L_);
  lastEvaluated_ = kNew - 1;
  if (soundN_ > 0) ++counters_.rows[15];
  if (hasAnchor_) {   // restart `chord`
    anchorK_ = kNew;
    m_ = 0;
  }
}

// Discrete settings apply from the next evaluated step (T8). `enabled`, `arp_enabled`, `latch` and `rate`
// are applied elsewhere (offset 0, or switchRate); the continuous ones are read live at evaluation.
void Core::adopt() {
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
    HeldKey P[kMaxHeldKeys];
    for (int i = 0; i < e; ++i) P[i] = E[i];
    for (int i = 1; i < e; ++i) {
      const HeldKey h = P[i];
      int j = i;
      const bool tieDesc = ARP_PLANT(playedTieByKeyDesc);   // plant: ties of one press sample resolve to the HIGHER key first
      while (j > 0 && (P[j - 1].abs > h.abs || (P[j - 1].abs == h.abs && (tieDesc ? P[j - 1].key < h.key : P[j - 1].key > h.key)))) { P[j] = P[j - 1]; --j; }
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
  int P[kMaxHeldKeys];
  raw(c, P);
  if (c > 0 && e >= 2 && !ARP_PLANT(shuffleBoundaryRepeat)) {
    int Q[kMaxHeldKeys];
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
  if (a.pattern == Pattern::User) {
    const UserPattern& u = userSlots_[static_cast<std::size_t>(r_)];
    steps = u.steps; plen = u.length; pSize = u.stepSize; pUnit = u.stepUnit;
  } else {
    const std::size_t bi = static_cast<std::size_t>(a.pattern) < static_cast<std::size_t>(kBankCount) ? static_cast<std::size_t>(a.pattern) : 0;
    steps = kBank[bi].steps; plen = kBank[bi].length; pSize = kBank[bi].stepSize; pUnit = kBank[bi].stepUnit;
  }
  if (ARP_PLANT(stepSizeIgnored)) { pSize = 12; pUnit = StepUnit::Semitones; }
  if (ARP_PLANT(stepOffsetReadsGlobal)) { pSize = a.stepSize; pUnit = a.stepUnit; }
#ifdef ARP_PLANTS
  if (plants.timeBarsCount && timeMode_ && a.restart != Restart::Chord) {   // plant (r2.8): bars of 61,440 us count in time mode
    const std::int64_t bar = fdiv(kAbs * L_, 61440);
    if (bar != plantLastBar_) { plantLastBar_ = bar; anchorK_ = cdiv(bar * 61440, L_); m_ = 0; }
  }
#endif
  const std::int64_t k = kAbs - anchorK_, j = mod(k, plen);
  const PatternStep st = steps[j];
  HeldKey E[kMaxHeldKeys];
  const int e = collectHeld(E);
  StepRecord rec;
  rec.offset = so;
  rec.kAbs = kAbs;
  rec.k = k;
  rec.mBefore = m_;
  const std::int64_t ord = stepSeq_++;
  // SPEC 4.3 step 1: prob 0 never fires and 100 always does, exactly (a draw is a uniform 32-bit number mod 100)
  const std::uint32_t dv = draw(a.seed, kAbs, 2) % 100;
  const bool fires = ARP_PLANT(probLE) ? dv <= static_cast<std::uint32_t>(st.prob) : dv < static_cast<std::uint32_t>(st.prob);
  // SPEC 5.1: on-times anchor at kAbs * L (never k * L); swing parity comes from kAbs. `swing` is continuous: read here, at
  // evaluation, and one value shapes every on, span and tie look-ahead of this step. SPEC-GAP (observation, not a conflict): a
  // swing change between two steps leaves the earlier step's span/off computed with the old value while the later step's own
  // on is placed with the new one; the literal reading is kept (each step is frozen at its own evaluation).
  const std::int64_t swing = clampi(S_.swing, 500, 750);
  const std::int64_t swingDelay = (timeMode_ && ARP_PLANT(timeIgnoresSwing)) ? 0 : fdiv(L * (2 * swing - 1000), 1000);
  const std::int64_t srSkew = ARP_PLANT(srTickMath) ? sr_ / 44100 : 0;
  auto onTick = [&](std::int64_t x) { return gtick(x) + (mod(ARP_PLANT(swingParityK) ? x - anchorK_ : x, 2) == 1 ? swingDelay : 0) + srSkew; };
  const std::int64_t on0 = onTick(kAbs);
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
      if (merged.size() > static_cast<std::size_t>(kMaxHeldKeys)) merged.resize(static_cast<std::size_t>(kMaxHeldKeys));
      Es = merged.data();
      es = static_cast<int>(merged.size());
    }
    const bool mergedOn = Es != E;
#else
    const bool mergedOn = false;
#endif
    Note nl[kMaxHeldKeys];
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
        if (!ARP_PLANT(chordRootAsRoot)) qRoot = static_cast<std::int32_t>(mod(E[0].key, 12));   // E is ascending: E[0] is the lowest held key
        if (qMask != 0) qMask |= 1u;
      }
    }
    const bool qOn = qMask != 0 && !(ARP_PLANT(skipQuantizeChord) && nn > 1);
    // -- the keys, SPEC 4.7 "Applying offsets": per key, in ASCENDING pre-quantize order, each key through all six stages before
    // the next starts (so a tie in stage 3 reads lastQ as the previous key left it): 1. key + S; 2. drop outside 0-127 (T16);
    // 3. quantize; 4. move G in-scale keys; 5. drop outside 0-127; 6. lastQ := the final key, only if the key survived.
    // Collisions (two keys landing on one) keep the EARLIEST source in the style's order, which is the list order.
    int byKey[kMaxHeldKeys];
    for (int i = 0; i < nn; ++i) {
      int at = i;
      while (at > 0 && nl[byKey[at - 1]].key > nl[i].key) { byKey[at] = byKey[at - 1]; --at; }
      byKey[at] = i;
    }
    std::int64_t fin[kMaxHeldKeys];
    bool finSetsLastQ[kMaxHeldKeys] = {};
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
    Out outs[kMaxHeldKeys];
    int no = 0;
    for (int i = 0; i < nn; ++i) {
      if (fin[i] < 0) continue;
      int dup = -1;
      for (int q = 0; q < no; ++q) if (outs[q].key == fin[i]) dup = q;
      // SPEC 4.4: round-half-up in integers
      const std::int64_t base = a.velMode == VelMode::Fixed ? clampi(S_.velFixed, 1, 127) : nl[i].vel;
      const auto vel = static_cast<std::int32_t>(clampi(fdiv(base * st.vel + 50, 100), 1, 127));
      if (dup >= 0) {
        if (ARP_PLANT(collisionKeepsLast)) { outs[dup].vel = vel; outs[dup].ch = nl[i].ch; }
        continue;
      }
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
  } else if (fires && st.trig > 0) {
    trigger();   // 4. a rest (or e = 0) that fires its trigger: trigger-only (H11)
  } else if (ARP_PLANT(advanceOnRest)) {
    ++m_;
  }
  rec.mAfter = m_;
  if (static_cast<std::size_t>(stepsN_) < steps_.size()) steps_[static_cast<std::size_t>(stepsN_++)] = rec;
  else ++counters_.stepLogDropped;
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
  pending_[static_cast<std::size_t>(pendN_++)] = p;
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

void Core::emitOn(const Pending& p, std::int32_t s) {
  // T13: refuse an on whose own off would not be guaranteed room (the off-reserve): events + sounding + 2 <= cap.
  const bool admit = ARP_PLANT(noOffReserve) ? outN_ < capEv_ : room(2);
  if (!admit) { ++counters_.rows[13]; ++counters_.refused; return; }
  // T2: the same (channel, key) still sounding -> its off goes out first, at this sample (one stream in M1a).
  NoteRef victims[kMaxSounding + kSoundingSlack];
  int nv = 0;
  for (int i = 0; i < soundN_; ++i) {
    const Sounding& x = sounding_[static_cast<std::size_t>(i)];
    if (!x.dying && x.ref.key == p.key && x.ref.channel == p.ch) victims[nv++] = x.ref;
  }
  for (int i = 0; i < nv; ++i) { ++counters_.rows[2]; release(victims[i], s); }
  // T11: the sounding table is full -> the oldest note is released at this offset, before the new on. Notes already
  // released and waiting out their 1-sample minimum (dying) do not count as sounding, and a note whose own on is at THIS
  // sample cannot be released yet (SPEC 1.1): the victim is the oldest note that can. If there is none, the new on is
  // refused (counted) rather than let two notes sound against a one-note table.
  if (ARP_PLANT(t11EvictsFirst)) {   // plant: the first-version rule
    if (soundN_ >= caps_.sounding) { ++counters_.rows[11]; ++counters_.soundingOverflow; release(sounding_[0].ref, s); }
  } else for (;;) {
    int live = 0, victim = -1;
    for (int i = 0; i < soundN_; ++i) {
      const Sounding& x = sounding_[static_cast<std::size_t>(i)];
      if (x.dying) continue;
      ++live;
      if (victim < 0 && x.onAbs + 1 <= absBase_ + s) victim = i;
    }
    if (live < caps_.sounding) break;
    if (victim < 0) { ++counters_.refused; return; }
    ++counters_.rows[11];
    ++counters_.soundingOverflow;
    release(sounding_[static_cast<std::size_t>(victim)].ref, s);
  }
  // Only reachable if > kSoundingSlack notes die inside this one sample: refuse rather than overrun the table.
  if (soundN_ >= static_cast<int>(sounding_.size())) { ++counters_.refused; return; }
  const NoteRef ref{0, p.ch, p.key, idCounter_++};
  Sounding& x = sounding_[static_cast<std::size_t>(soundN_++)];
  x = Sounding{};
  x.ref = ref;
  x.offPos = p.offPos;
  x.hasOff = true;
  x.onAbs = absBase_ + s;
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
  int i = -1;
  for (int q = 0; q < soundN_; ++q)
    if (sounding_[static_cast<std::size_t>(q)].ref.id == ref.id) { i = q; break; }
  if (i < 0) return false;
  Sounding& x = sounding_[static_cast<std::size_t>(i)];
  // SPEC 1.1: never at the same sample as its own on -> keep it "dying"; the timeline emits it at on + 1.
  const bool defer = !ARP_PLANT(sameSampleOff);
  if (defer && x.onAbs + 1 > absBase_ + s) {
    x.dying = true;
    x.hasOff = false;
    return true;
  }
  Event ev;
  ev.type = EventType::NoteOff;
  ev.offset = s;
  ev.ref = x.ref;
  ev.vel = kReleaseVel;
  ev.tick = x.hasOff ? tickOf(x.offPos) : kNoTick;
  for (int q = i + 1; q < soundN_; ++q) sounding_[static_cast<std::size_t>(q - 1)] = sounding_[static_cast<std::size_t>(q)];
  --soundN_;
#ifdef ARP_PLANTS
  if (plants.dropRelease && !dropped_) { dropped_ = true; return true; }
#endif
  if (!pushEvent(ev)) ++counters_.offsLost;
#ifdef ARP_PLANTS
  if (plants.doubleRelease && !doubled_) { doubled_ = true; pushEvent(ev); }
#endif
  return true;
}

// Release-all (T4, T6, T7, T10): walks the sounding table and CLEARS the pending queue (SPEC 7).
void Core::releaseAll(std::int32_t s) {
  if (!ARP_PLANT(releaseKeepsPending)) pendN_ = 0;
  if (!ARP_PLANT(lastQNoReset)) hasLastQ_ = false;   // SPEC 6 (r2 H2): every release-all forgets lastQ, so each play-from-here sounds the same
  NoteRef snapshot[kMaxSounding + kSoundingSlack];
  const int n = soundN_;
  for (int i = 0; i < n; ++i) snapshot[i] = sounding_[static_cast<std::size_t>(i)].ref;
  for (int i = 0; i < n; ++i) release(snapshot[i], s);
}

}  // namespace arp
