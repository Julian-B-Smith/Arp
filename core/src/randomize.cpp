// randomize.cpp - SPEC 15 (D-044, D-048): Arp's own randomize. See arp/randomize.h for the contract and
// docs/design/randomize.md for the reasoning behind each rule (section numbers below are the design's).
//
// Determinism rules this file keeps (RZ1): integers only; unsigned 32-bit wrap for the draw; units in the fixed order of
// SPEC 15; ONE draw per full expression (every rdraw() result goes into a named local first), so argument evaluation order
// can never reorder draws; no container that iterates in hash order. Real time: nothing here allocates (RZ13); the largest
// buffer is a 256-entry candidate array on the stack.
#include "arp/randomize.h"

#include <cstddef>
#include <cstdint>
#ifdef ARP_PLANTS
#include <vector>
#endif

#include "arp/draw.h"
#include "arp/randomize_data.h"

namespace arp::rnd {
namespace {

namespace D = data;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;

constexpr u32 fnv1a(const char* s) {
  u32 h = 0x811C9DC5u;
  for (; *s; ++s) h = (h ^ static_cast<unsigned char>(*s)) * 0x01000193u;
  return h;
}
struct KeyIds {
  u32 v[kParamCount] = {};
  constexpr KeyIds() { for (int i = 0; i < kParamCount; ++i) v[i] = fnv1a(D::kParamNames[i]); }
};
constexpr KeyIds kKeyIds{};
constexpr u32 kIdScale = fnv1a("scale"), kIdTransposition = fnv1a("transposition"), kIdChord = fnv1a("chord"),
              kIdScaleMask = fnv1a("scale_mask");
#ifdef ARP_PLANTS
constexpr u32 kIdLatch = fnv1a("latch");   // drawLatch only: latch is never drawn (SPEC 15)
#endif
constexpr u32 id(ParamId p) { return kKeyIds.v[static_cast<int>(p)]; }
constexpr ParamId chordOn(int i) { return static_cast<ParamId>(static_cast<int>(ParamId::ChordOn1) + 2 * i); }
constexpr ParamId chordShift(int i) { return static_cast<ParamId>(static_cast<int>(ParamId::ChordShift1) + 2 * i); }
constexpr ParamId scaleDeg(int i) { return static_cast<ParamId>(static_cast<int>(ParamId::ScaleDeg0) + i); }

enum Idx : u32 { kAmount = 0, kKeep = 1, kPick = 2, kMust = 3 };
constexpr int kMaxCands = 256;

i64 iabs(i64 x) { return x < 0 ? -x : x; }
i64 clampv(i64 x, i64 lo, i64 hi) { return x < lo ? lo : x > hi ? hi : x; }
// r / 2^32 below pm / 1000, exactly as SPEC 15.2 states it: r < floor(pm * 2^32 / 1000).
bool below(u32 r, u32 pm) { return static_cast<u64>(r) < ((static_cast<u64>(pm) << 32) / 1000u); }

struct Ctx {
  const Settings& cur;   // decides liveness and locks; keys not drawn keep it (SPEC 15.3)
  const Settings& home;  // the bell's centre (already clamped into range)
  Settings out;
  u32 seed, click, corner, chaos, amount;
  LockSet locks;
  bool structurePhase = false;
  int empty = 0;
#ifdef ARP_PLANTS
  Plants pl{};
  u32 stream = 0;   // carriedStream's counter
#endif

  u32 rd(u32 key, u32 idx) {
#ifdef ARP_PLANTS
    if (pl.carriedStream) return mulberry32Once(seed + 0x6D2B79F5u * stream++);
    u32 s = seed;
    if (pl.pointerInSeed) s ^= static_cast<u32>(reinterpret_cast<std::uintptr_t>(this) >> 4);
    if (pl.pickDependsOnAmount && idx == kPick) s ^= amount * 0x9E3779B9u;
#else
    const u32 s = seed;
#endif
    return mulberry32Once(s ^ D::kSalt ^ (click * D::kClick) ^ (corner * D::kCorner) ^ (key * D::kKey) ^ ((idx + 1u) * D::kIdx));
  }
  bool locked(ParamId p) const {
#ifdef ARP_PLANTS
    if (pl.locksValuesOnly && structurePhase) return false;
#endif
    return locks.test(p);
  }
  bool considered(u32 unitKey) {
    const u32 r = rd(unitKey, kAmount);
    return below(r, amount);
  }
  u32 sigma() const {
#ifdef ARP_PLANTS
    if (pl.ignoreChaos && chaos > 0) return D::kSigmaQ16[1000];
    if (pl.reverseSigma && chaos > 0) return D::kSigmaQ16[1000 - chaos];
#endif
    return D::kSigmaQ16[chaos];
  }
};

// A pick among n weights with draw `r` (SPEC 15.5): t = floor(r * total / 2^32), the first running sum above t. -1 if the total is 0.
int pickWeighted(u32 r, const u64* w, int n) {
  u64 total = 0;
  for (int i = 0; i < n; ++i) total += w[i];
  if (total == 0) return -1;
  const u64 t = (static_cast<u64>(r) * total) >> 32;
  u64 run = 0;
  for (int i = 0; i < n; ++i) {
    run += w[i];
    if (run > t) return i;
  }
  return n - 1;
}
int pickTaste(Ctx& c, u32 key, u32 idx, const u32* taste, int n) {
  u64 w[kMaxCands];
  for (int i = 0; i < n; ++i) w[i] = taste[i];
  const u32 r = c.rd(key, idx);
  return pickWeighted(r, w, n);
}

// SPEC 15.4, an unordered choice: home with probability 1 - chaos (or at chaos 0), else a taste-weighted pick among the lit values
// (which may be home again). `home` may be -1 (a value outside the list, e.g. a custom scale mask): it is kept by the keep test, and
// the function returns -1 for it. A list with no lit value keeps home.
int unordered(Ctx& c, u32 key, const u32* taste, int n, int home) {
  if (c.chaos == 0) return home;
  const u32 rk = c.rd(key, kKeep);
  if (!below(rk, c.chaos)) return home;
  const int i = pickTaste(c, key, kPick, taste, n);
  if (i < 0) { ++c.empty; return home; }
  return i;
}

// SPEC 15.5, the discrete bell over ladder indices: `cands` ascending (it must hold `centre`), `n` the full ladder's length.
// Weight E = 2^16 exp(-d^2 / 2 sigma^2) from the integer table, sigma = sigma(chaos) * n in ladder steps; d is circular on a
// ring (the circle of fifths). Every product is a u64 well inside its range: s8 <= 262144 * 251 / 256 < 2^18, s2 < 2^36,
// d^2 << 24 < 2^40, a weight < 2^17, a total < 2^25, r * total < 2^57.
int bell(Ctx& c, u32 key, const int* cands, int m, int centre, int n, bool ring) {
  if (c.chaos == 0) return centre;
  const u64 s8 = (static_cast<u64>(c.sigma()) * static_cast<u64>(n)) >> 8;
  const u64 s2 = s8 * s8;
  u64 w[kMaxCands];
  for (int i = 0; i < m; ++i) {
    i64 d = iabs(cands[i] - centre);
    if (ring && n - d < d) d = n - d;
    u64 e = 0;
    if (d == 0) e = D::kExpNegQ16[0];
    else if (s2 > 0) {
      const u64 x8 = (static_cast<u64>(d * d) << 24) / (2 * s2);
      e = x8 < 4096 ? D::kExpNegQ16[x8] : 0;
    }
    w[i] = e;
  }
  const u32 r = c.rd(key, kPick);
  const int i = pickWeighted(r, w, m);
  if (i < 0) { ++c.empty; return centre; }
  return cands[i];
}

// Candidate lists: an ascending, duplicate-free index list.
struct Cands {
  int v[kMaxCands];
  int n = 0;
  void add(int x) {
    int i = n;
    while (i > 0 && v[i - 1] > x) --i;
    if (i > 0 && v[i - 1] == x) return;
    for (int j = n; j > i; --j) v[j] = v[j - 1];
    v[i] = x;
    ++n;
  }
  bool has(int x) const { for (int i = 0; i < n; ++i) if (v[i] == x) return true; return false; }
};

// A bell over every integer lo..hi (the ladder), band [bandLo, bandHi] widened to include home.
i64 bellRange(Ctx& c, ParamId p, i64 lo, i64 hi, i64 bandLo, i64 bandHi, i64 homeV) {
  const i64 h = clampv(homeV, lo, hi);
  Cands cs;
  for (i64 v = bandLo; v <= bandHi; ++v) cs.add(static_cast<int>(v - lo));
#ifdef ARP_PLANTS
  if (!(c.pl.centreNotCandidate && (h < bandLo || h > bandHi)))
#endif
  cs.add(static_cast<int>(h - lo));
  return lo + bell(c, id(p), cs.v, cs.n, static_cast<int>(h - lo), static_cast<int>(hi - lo + 1), false);
}

bool arpLive(const Ctx& c) {
#ifdef ARP_PLANTS
  if (c.pl.ignoreArpEnabled) return c.cur.enabled;
#endif
  return c.cur.enabled && c.cur.arpEnabled;
}
std::uint16_t maskOf(const Settings& s) {
  std::uint16_t m = 0;
  for (int i = 0; i < 12; ++i) if (s.scaleDeg[i]) m = static_cast<std::uint16_t>(m | (1u << i));
  return m;
}
int namedScaleOf(std::uint16_t m) {
  for (int i = 0; i < static_cast<int>(sizeof D::kScaleMask / sizeof D::kScaleMask[0]); ++i) if (D::kScaleMask[i] == m) return i;
  return -1;
}
int fifthsPos(int root) { for (int i = 0; i < 12; ++i) if (D::kFifths[i] == root) return i; return 0; }
bool seedRead(const Settings& s) {
  if (s.order == Order::Random || s.order == Order::Shuffle || s.velRandom > 0 || s.gateRandom > 0) return true;
  for (std::uint8_t p : D::kSeedPatterns) if (static_cast<std::uint8_t>(s.pattern) == p) return true;
  return false;
}
i64 spanBound(StepUnit u) { return u == StepUnit::Semitones ? D::kSpanSemitones : D::kSpanDegrees; }

// Rate families: which family a division belongs to, and its members.
struct Family { const std::uint8_t* m; int n; int lo, hi; };
Family familyOf(Rate r) {
  const auto in = [&](const std::uint8_t* m, int n) { for (int i = 0; i < n; ++i) if (m[i] == static_cast<std::uint8_t>(r)) return true; return false; };
  if (in(D::kRateDotted, 7)) return {D::kRateDotted, 7, D::kRateDottedBand[0], D::kRateDottedBand[1]};
  if (in(D::kRateTriplet, 7)) return {D::kRateTriplet, 7, D::kRateTripletBand[0], D::kRateTripletBand[1]};
  return {D::kRateStraight, 8, D::kRateStraightBand[0], D::kRateStraightBand[1]};
}
int posIn(const Family& f, Rate r) { for (int i = 0; i < f.n; ++i) if (f.m[i] == static_cast<std::uint8_t>(r)) return i; return 0; }

// The transposition / chord interval table of a unit, as weights with a filter applied by the caller.
struct Table { const std::int32_t (*rows)[2]; int n; };
Table stepTable(StepUnit u) { return u == StepUnit::Semitones ? Table{D::kStepSemis, 8} : Table{D::kStepDegs, 8}; }
Table chordTable(StepUnit u) {
  return u == StepUnit::Semitones ? Table{D::kChordSemis, static_cast<int>(sizeof D::kChordSemis / sizeof D::kChordSemis[0])}
                                  : Table{D::kChordDegs, static_cast<int>(sizeof D::kChordDegs / sizeof D::kChordDegs[0])};
}
std::int32_t chordValue(const Ctx& c, const Table& t, int i) {
  std::int32_t v = t.rows[i][0];
#ifdef ARP_PLANTS
  if (c.pl.shiftOutOfRange && v == 24) v = 25;
#else
  (void)c;
#endif
  return v;
}

// ------------------------------------------------------------------------------------------------ Structure (SPEC 15.6, 15.8)
void orderKey(Ctx& c, bool arp) {
  if (!arp || c.locked(ParamId::Order) || !c.considered(id(ParamId::Order))) return;
  c.out.order = static_cast<Order>(unordered(c, id(ParamId::Order), D::kTasteOrder, 16, static_cast<int>(c.home.order)));
}
void patternTaste(const Ctx& c, u32* t) {
  for (int i = 0; i < 12; ++i) t[i] = D::kTastePattern[i];
#ifdef ARP_PLANTS
  if (c.pl.lightTriggerPulse) t[static_cast<int>(Pattern::TriggerPulse)] = 1;
#else
  (void)c;
#endif
}
void patternKey(Ctx& c, bool arp) {
  if (!arp || c.locked(ParamId::Pattern) || !c.considered(id(ParamId::Pattern))) return;
  u32 t[12];
  patternTaste(c, t);
  c.out.pattern = static_cast<Pattern>(unordered(c, id(ParamId::Pattern), t, 12, static_cast<int>(c.home.pattern)));
}
void rateKey(Ctx& c, bool arp) {
  if (!arp || c.cur.rateMode != RateMode::Sync || c.locked(ParamId::Rate) || !c.considered(id(ParamId::Rate))) return;
  const Family f = familyOf(c.home.rate);
  const int h = posIn(f, c.home.rate);
  Cands cs;
  for (int i = f.lo; i <= f.hi; ++i) cs.add(i);
  cs.add(h);
  const int i = bell(c, id(ParamId::Rate), cs.v, cs.n, h, f.n, false);
  c.out.rate = static_cast<Rate>(f.m[i]);
}
void restartKey(Ctx& c, bool arp) {
  if (!arp || c.cur.rateMode != RateMode::Sync || c.locked(ParamId::Restart) || !c.considered(id(ParamId::Restart))) return;
  c.out.restart = static_cast<Restart>(unordered(c, id(ParamId::Restart), D::kTasteRestart, 3, static_cast<int>(c.home.restart)));
}
// Repeats' admitted set (SPEC 15, row 20): cur; every count above it up to 8; home if home is 0 (forever) or above cur.
void repeatsAdmitted(const Ctx& c, Cands& cs, bool withCur) {
  const int cr = static_cast<int>(c.cur.repeats), hr = static_cast<int>(c.home.repeats);
  if (withCur) cs.add(cr);
  for (int v = cr + 1; v <= D::kBandRepeats[1]; ++v) cs.add(v);
  if (hr == 0 || hr > cr) cs.add(hr);
#ifdef ARP_PLANTS
  if (c.pl.repeatsDown) for (int v = D::kBandRepeats[0]; v < cr; ++v) cs.add(v);
#endif
}
void repeatsKey(Ctx& c, bool arp) {
  if (!arp || c.cur.repeats == 0 || c.locked(ParamId::Repeats) || !c.considered(id(ParamId::Repeats))) return;
  Cands cs;
  repeatsAdmitted(c, cs, true);
  const int hr = static_cast<int>(c.home.repeats);
  const int centre = cs.has(hr) ? hr : static_cast<int>(c.cur.repeats);
  c.out.repeats = bell(c, id(ParamId::Repeats), cs.v, cs.n, centre, 17, false);
}
void velModeKey(Ctx& c, bool arp) {
  if (!arp || c.locked(ParamId::VelMode) || !c.considered(id(ParamId::VelMode))) return;
  c.out.velMode = static_cast<VelMode>(unordered(c, id(ParamId::VelMode), D::kTasteVelMode, 2, static_cast<int>(c.home.velMode)));
}
bool ownScaleLocked(const Ctx& c) {
  if (c.locked(ParamId::ScaleRoot)) return true;
  for (int i = 0; i < 12; ++i) if (c.locked(scaleDeg(i))) return true;
  return false;
}
bool scaleLive(const Ctx& c, bool arp) { return c.cur.enabled && (arp || c.cur.chordEnabled); }
void writeMask(Settings& s, std::uint16_t m) { for (int i = 0; i < 12; ++i) s.scaleDeg[i] = (m >> i) & 1u; }

void scaleGroup(Ctx& c, bool arp) {
  if (!scaleLive(c, arp)) return;
  const bool qLocked = c.locked(ParamId::Quantize), ownLocked = ownScaleLocked(c);
  if (qLocked && ownLocked) return;
  if (!c.considered(kIdScale)) return;
  if (!qLocked) c.out.quantize = static_cast<Quantize>(unordered(c, id(ParamId::Quantize), D::kTasteQuantize, 3, static_cast<int>(c.home.quantize)));
  if (ownLocked || c.cur.scaleSource != ScaleSource::Own || c.out.quantize == Quantize::Off) return;
  const std::uint16_t hm = maskOf(c.home);
  const int named = unordered(c, kIdScaleMask, D::kScaleTaste, static_cast<int>(sizeof D::kScaleTaste / sizeof D::kScaleTaste[0]), namedScaleOf(hm));
  writeMask(c.out, named < 0 ? hm : D::kScaleMask[named]);
  Cands cs;
  for (int i = 0; i < 12; ++i) cs.add(i);
  bool ring = true;
#ifdef ARP_PLANTS
  if (c.pl.linearRoot) ring = false;
#endif
  const int pos = bell(c, id(ParamId::ScaleRoot), cs.v, cs.n, fifthsPos(c.home.scaleRoot), 12, ring);
  c.out.scaleRoot = D::kFifths[pos];
}

// Steps' admitted set (SPEC 15.6): 0..3 and home; with step_size locked, only values inside the span bound (in cur's unit,
// which the lock pins), plus cur.steps (the player's own shape).
void stepsAdmitted(const Ctx& c, Cands& cs) {
  const bool sizeLocked = c.locked(ParamId::StepSize);
  const int hs = static_cast<int>(c.home.steps), cs0 = static_cast<int>(c.cur.steps);
  auto ok = [&](int v) { return !sizeLocked || iabs(static_cast<i64>(v) * c.cur.stepSize) <= spanBound(c.cur.stepUnit) || v == cs0; };
  for (int v = D::kBandSteps[0]; v <= D::kBandSteps[1]; ++v) if (ok(v)) cs.add(v);
  if (ok(hs)) cs.add(hs);
  if (sizeLocked) cs.add(cs0);
}
// The size table's weights for `steps` passes in `unit`: zero for an interval past the span bound (a steps above 3 brings its own
// shape and is not filtered), and for `exclude` when it is given.
void sizeWeights(const Ctx& c, StepUnit unit, i64 steps, const i64* exclude, u32* w) {
  const Table t = stepTable(unit);
  bool filter = steps <= D::kBandSteps[1];
#ifdef ARP_PLANTS
  if (c.pl.noOwnShapeRule) filter = true;
  if (c.pl.noSpanFilter) filter = false;
#else
  (void)c;
#endif
  for (int i = 0; i < t.n; ++i) {
    const i64 v = t.rows[i][0];
    w[i] = static_cast<u32>(t.rows[i][1]);
    if (filter && iabs(steps * v) > spanBound(unit)) w[i] = 0;
    if (exclude && v == *exclude) w[i] = 0;
  }
}
bool forcedAllowed(const Ctx& c) {
#ifdef ARP_PLANTS
  if (c.pl.forcedAtChaos0) return true;
#endif
  return c.chaos > 0;
}

void transpositionGroup(Ctx& c, bool arp) {
  if (!arp) return;
  const bool sL = c.locked(ParamId::Steps), uL = c.locked(ParamId::StepUnit), zL = c.locked(ParamId::StepSize);
  if (sL && uL && zL) return;
  if (!c.considered(kIdTransposition)) return;
  i64 steps = c.cur.steps;
  if (!sL) {
    Cands cs;
    stepsAdmitted(c, cs);
    const int hs = static_cast<int>(c.home.steps);
    const int centre = cs.has(hs) ? hs : static_cast<int>(c.cur.steps);
    steps = bell(c, id(ParamId::Steps), cs.v, cs.n, centre, 9, false);
  }
  c.out.steps = static_cast<std::int32_t>(steps);
  if (steps <= 0) return;   // no passes: the unit and size keep cur (a flip would only wait, unheard, in the new unit)
  StepUnit unit = c.cur.stepUnit;
  if (!uL && !zL) {
    const u32 taste[2] = {D::kTasteStepUnit[0], c.cur.quantize != Quantize::Off ? D::kTasteStepUnit[1] : 0u};
    unit = static_cast<StepUnit>(unordered(c, id(ParamId::StepUnit), taste, 2, static_cast<int>(c.home.stepUnit)));
  }
  c.out.stepUnit = unit;
  if (zL) return;
  u32 w[8];
  sizeWeights(c, unit, steps, nullptr, w);
  const Table t = stepTable(unit);
  const bool flipped = unit != c.home.stepUnit;
  const bool homeOk = !flipped && (steps > D::kBandSteps[1] || iabs(steps * c.home.stepSize) <= spanBound(unit));
  std::int32_t size = c.home.stepSize;
  if (forcedAllowed(c) && (flipped || !homeOk)) {
    const int i = pickTaste(c, id(ParamId::StepSize), kPick, w, t.n);
    if (i < 0) { ++c.empty; size = c.cur.stepSize; }
    else size = t.rows[i][0];
  } else if (c.chaos == 0) {
    // Chaos 0 draws nothing, so no band applies (the span bound included): the size returns to home's, unless home's interval
    // is in the other unit (the unit is pinned to cur's by a lock): then the centre is cur's size (SPEC 15.5's centre rule).
    if (flipped) size = c.cur.stepSize;
  } else {
    const u32 rk = c.rd(id(ParamId::StepSize), kKeep);
    if (below(rk, c.chaos)) {
      const int i = pickTaste(c, id(ParamId::StepSize), kPick, w, t.n);
      if (i >= 0) size = t.rows[i][0];
      else ++c.empty;
    }
  }
  c.out.stepSize = size;
}

bool anyShiftLocked(const Ctx& c) { for (int i = 0; i < 6; ++i) if (c.locked(chordShift(i))) return true; return false; }
bool chordAllLocked(const Ctx& c) {
  if (!c.locked(ParamId::ChordEnabled) || !c.locked(ParamId::ChordUnit)) return false;
  for (int i = 0; i < 6; ++i) if (!c.locked(chordOn(i)) || !c.locked(chordShift(i))) return false;
  return true;
}
std::int32_t drawShift(Ctx& c, int slot, StepUnit unit, bool forced, bool unitFlipped, std::int32_t homeV) {
  const Table t = chordTable(unit);
  u32 w[32];
  for (int i = 0; i < t.n; ++i) w[i] = static_cast<u32>(t.rows[i][1]);
  const u32 key = id(chordShift(slot));
  if (forced && forcedAllowed(c)) {
    const int i = pickTaste(c, key, kPick, w, t.n);
    if (i < 0) { ++c.empty; return homeV; }
    return chordValue(c, t, i);
  }
  if (c.chaos == 0) return unitFlipped ? c.cur.chordShift[slot] : homeV;   // home's interval belongs to the other unit: the centre is cur's
  const u32 rk = c.rd(key, kKeep);
  if (!below(rk, c.chaos)) return homeV;
  const int i = pickTaste(c, key, kPick, w, t.n);
  if (i < 0) { ++c.empty; return homeV; }
  return chordValue(c, t, i);
}

void chordGroup(Ctx& c) {
  if (!c.cur.enabled || chordAllLocked(c)) return;
  if (!c.considered(kIdChord)) return;
  bool en = c.cur.chordEnabled;
  if (!c.locked(ParamId::ChordEnabled)) {
    bool keepCur = !c.cur.arpEnabled && c.cur.chordEnabled;   // the admission rule: with the arp off only on and cur are admitted
#ifdef ARP_PLANTS
    if (c.pl.keepUnlitChordHome) keepCur = false;
#endif
    if (!keepCur) {
      const u32 taste[2] = {c.cur.arpEnabled ? D::kTasteChordEnabled[0] : 0u, D::kTasteChordEnabled[1]};
      en = unordered(c, id(ParamId::ChordEnabled), taste, 2, c.home.chordEnabled ? 1 : 0) == 1;
    }
  }
  c.out.chordEnabled = en;
  if (!en) return;   // nothing in the stage can be heard: every other member keeps cur
  bool on[6];
  for (int i = 0; i < 6; ++i)
    on[i] = c.locked(chordOn(i)) ? c.cur.chordOn[i] : unordered(c, id(chordOn(i)), D::kTasteChordOn, 2, c.home.chordOn[i] ? 1 : 0) == 1;
  StepUnit unit = c.cur.chordUnit;
  if (!c.locked(ParamId::ChordUnit) && !anyShiftLocked(c)) {
    const u32 taste[2] = {D::kTasteChordUnit[0], c.cur.quantize != Quantize::Off ? D::kTasteChordUnit[1] : 0u};
    unit = static_cast<StepUnit>(unordered(c, id(ParamId::ChordUnit), taste, 2, static_cast<int>(c.home.chordUnit)));
  }
  c.out.chordUnit = unit;
  const bool flipped = unit != c.home.chordUnit;
  for (int i = 0; i < 6; ++i) {
    c.out.chordOn[i] = on[i];
    if (!on[i] || c.locked(chordShift(i))) continue;
    const bool newlyOn = !c.cur.chordOn[i];
    c.out.chordShift[i] = drawShift(c, i, unit, newlyOn || flipped, flipped, static_cast<std::int32_t>(clampv(c.home.chordShift[i], -24, 24)));
  }
}

void structure(Ctx& c) {
  c.structurePhase = true;
  const bool arp = arpLive(c);
  orderKey(c, arp);
  patternKey(c, arp);
  rateKey(c, arp);
  restartKey(c, arp);
  repeatsKey(c, arp);
  velModeKey(c, arp);
#ifdef ARP_PLANTS
  if (c.pl.drawLatch && !c.locked(ParamId::Latch) && c.considered(kIdLatch)) {
    const u32 t3[3] = {1, 1, 1};
    c.out.latch = static_cast<Latch>(unordered(c, kIdLatch, t3, 3, static_cast<int>(c.home.latch)));
  }
  if (c.pl.wrongClass && arp && !c.locked(ParamId::Swing) && c.considered(id(ParamId::Swing)))
    c.out.swing = static_cast<std::int32_t>(bellRange(c, ParamId::Swing, 500, 750, D::kBandSwing[0], D::kBandSwing[1], c.home.swing));
#endif
  scaleGroup(c, arp);
  transpositionGroup(c, arp);
  chordGroup(c);
  c.structurePhase = false;
}

// ------------------------------------------------------------------------------------------------ Values
i64 nearestRung(i64 v) {
  int best = 0;
  for (int i = 1; i < static_cast<int>(sizeof D::kStepTimeLadder / sizeof D::kStepTimeLadder[0]); ++i) {
    // closer by ratio: max(a,v)/min(a,v) smaller, compared by cross-multiplication; ties keep the lower rung
    const u64 a = static_cast<u64>(D::kStepTimeLadder[i]), b = static_cast<u64>(D::kStepTimeLadder[best]), x = static_cast<u64>(v);
    const u64 aHi = a > x ? a : x, aLo = a > x ? x : a, bHi = b > x ? b : x, bLo = b > x ? x : b;
    if (aHi * bLo < bHi * aLo) best = i;
  }
  return best;
}
void values(Ctx& c) {
  const bool arp = arpLive(c);
  const Settings* ref = &c.cur;   // liveness reads cur (SPEC 15.3)
#ifdef ARP_PLANTS
  if (c.pl.livenessFromOut) ref = &c.out;
#endif
  if (arp && !c.locked(ParamId::Gate) && c.considered(id(ParamId::Gate)))
    c.out.gate = static_cast<std::int32_t>(bellRange(c, ParamId::Gate, 1, 200, D::kBandGate[0], D::kBandGate[1], c.home.gate));
  bool swingHere = true;
#ifdef ARP_PLANTS
  if (c.pl.wrongClass) swingHere = false;
#endif
  if (swingHere && arp && !c.locked(ParamId::Swing) && c.considered(id(ParamId::Swing)))
    c.out.swing = static_cast<std::int32_t>(bellRange(c, ParamId::Swing, 500, 750, D::kBandSwing[0], D::kBandSwing[1], c.home.swing));
  bool timeLive = c.cur.rateMode == RateMode::Time;
#ifdef ARP_PLANTS
  if (c.pl.stepTimeInSync) timeLive = true;
#endif
  if (arp && timeLive && !c.locked(ParamId::StepTimeUs) && c.considered(id(ParamId::StepTimeUs))) {
    const int n = static_cast<int>(sizeof D::kStepTimeLadder / sizeof D::kStepTimeLadder[0]);
    const i64 hv = clampv(c.home.stepTimeUs, 10000, 2000000);
    const int h = static_cast<int>(nearestRung(hv));
    Cands cs;
    for (int i = 0; i < n; ++i) if (D::kStepTimeLadder[i] >= D::kBandStepTime[0] && D::kStepTimeLadder[i] <= D::kBandStepTime[1]) cs.add(i);
    cs.add(h);
    const int i = bell(c, id(ParamId::StepTimeUs), cs.v, cs.n, h, n, false);
    c.out.stepTimeUs = i == h ? static_cast<std::int32_t>(hv) : D::kStepTimeLadder[i];
  }
  if (arp && ref->velMode == VelMode::Fixed && !c.locked(ParamId::VelFixed) && c.considered(id(ParamId::VelFixed)))
    c.out.velFixed = static_cast<std::int32_t>(bellRange(c, ParamId::VelFixed, 1, 127, D::kBandVelFixed[0], D::kBandVelFixed[1], c.home.velFixed));
  if (arp && !c.locked(ParamId::VelRandom) && c.considered(id(ParamId::VelRandom)))
    c.out.velRandom = static_cast<std::int32_t>(bellRange(c, ParamId::VelRandom, 0, 100, D::kBandVelRandom[0], D::kBandVelRandom[1], c.home.velRandom));
  if (arp && !c.locked(ParamId::GateRandom) && c.considered(id(ParamId::GateRandom)))
    c.out.gateRandom = static_cast<std::int32_t>(bellRange(c, ParamId::GateRandom, 0, 100, D::kBandGateRandom[0], D::kBandGateRandom[1], c.home.gateRandom));
  if (arp && (ref->velRandom > 0 || ref->gateRandom > 0) && !c.locked(ParamId::RandomCurve) && c.considered(id(ParamId::RandomCurve)))
    c.out.randomCurve = static_cast<std::int32_t>(bellRange(c, ParamId::RandomCurve, 0, 100, D::kBandRandomCurve[0], D::kBandRandomCurve[1], c.home.randomCurve));
  if (arp && seedRead(*ref) && !c.locked(ParamId::Seed) && c.considered(id(ParamId::Seed))) {
    if (c.chaos == 0) c.out.seed = c.home.seed;
    else {
      const u32 rk = c.rd(id(ParamId::Seed), kKeep);
      if (!below(rk, c.chaos)) c.out.seed = c.home.seed;
      else c.out.seed = c.rd(id(ParamId::Seed), kPick);
    }
  }
}

// ------------------------------------------------------------------------------------------------ must change (SPEC 15.8)
enum Unit : int { kUOrder, kUPattern, kURate, kURestart, kURepeats, kUVelMode, kUScale, kUTransposition, kUChord, kUnitCount };
constexpr u32 unitKey(int u) {
  switch (u) {
    case kUOrder: return id(ParamId::Order);
    case kUPattern: return id(ParamId::Pattern);
    case kURate: return id(ParamId::Rate);
    case kURestart: return id(ParamId::Restart);
    case kURepeats: return id(ParamId::Repeats);
    case kUVelMode: return id(ParamId::VelMode);
    case kUScale: return kIdScale;
    case kUTransposition: return kIdTransposition;
    default: return kIdChord;
  }
}

// An unordered key's move: a taste pick among its lit values other than cur's. Returns the index, or -1 when none is lit.
int unorderedMove(Ctx& c, u32 key, const u32* taste, int n, int curIdx, bool apply) {
  u32 w[32];
  bool any = false;
  for (int i = 0; i < n; ++i) { w[i] = i == curIdx ? 0u : taste[i]; any = any || w[i] > 0; }
  if (!any) return -1;
  if (!apply) return 0;
  return pickTaste(c, key, kMust, w, n);
}
// An ordered key's move: the nearest admitted index above or below cur, the side from the draw's top bit. -1 if none.
int orderedMove(Ctx& c, u32 key, const Cands& cs, int curIdx, int n, bool apply) {
  int above = -1, below_ = -1;
  for (int i = 0; i < cs.n; ++i) {
    if (cs.v[i] > curIdx && above < 0) above = cs.v[i];
    if (cs.v[i] < curIdx) below_ = cs.v[i];
  }
  if (above < 0 && below_ < 0) return -1;
  if (!apply) return 0;
#ifdef ARP_PLANTS
  if (c.pl.mustChangeBell) {   // the bell around cur, cur excluded: at low chaos every weight is 0 and nothing moves
    Cands ex;
    for (int i = 0; i < cs.n; ++i) if (cs.v[i] != curIdx) ex.add(cs.v[i]);
    const u64 s8 = (static_cast<u64>(c.sigma()) * static_cast<u64>(n)) >> 8, s2 = s8 * s8;
    u64 w[kMaxCands];
    for (int i = 0; i < ex.n; ++i) {
      const i64 d = iabs(ex.v[i] - curIdx);
      const u64 x8 = s2 ? (static_cast<u64>(d * d) << 24) / (2 * s2) : 4096;
      w[i] = x8 < 4096 ? D::kExpNegQ16[x8] : 0;
    }
    const u32 r = c.rd(key, kMust);
    const int i = pickWeighted(r, w, ex.n);
    if (i < 0) { ++c.empty; return curIdx; }
    return ex.v[i];
  }
#else
  (void)n;
#endif
  const u32 r = c.rd(key, kMust);
  if (above >= 0 && below_ >= 0) return (r >> 31) ? above : below_;
  return above >= 0 ? above : below_;
}
bool firstMoveOnly(const Ctx& c) {
#ifdef ARP_PLANTS
  return c.pl.mustChangeFirstMoveOnly;
#else
  (void)c;
  return false;
#endif
}

// Tries unit u's moves in order (apply = false: only "is one possible?"). Returns true when a move exists (and, applying, was made).
bool moveUnit(Ctx& c, int u, bool arp, bool apply) {
  const Settings& cur = c.cur;
  Settings& out = c.out;
  switch (u) {
    case kUOrder:
      if (!arp || c.locked(ParamId::Order)) return false;
      { const int i = unorderedMove(c, unitKey(u), D::kTasteOrder, 16, static_cast<int>(cur.order), apply);
        if (i < 0) return false; if (apply) out.order = static_cast<Order>(i); return true; }
    case kUPattern:
      if (!arp || c.locked(ParamId::Pattern)) return false;
      { u32 t[12]; patternTaste(c, t);
        const int i = unorderedMove(c, unitKey(u), t, 12, static_cast<int>(cur.pattern), apply);
        if (i < 0) return false; if (apply) out.pattern = static_cast<Pattern>(i); return true; }
    case kURestart:
      if (!arp || cur.rateMode != RateMode::Sync || c.locked(ParamId::Restart)) return false;
      { const int i = unorderedMove(c, unitKey(u), D::kTasteRestart, 3, static_cast<int>(cur.restart), apply);
        if (i < 0) return false; if (apply) out.restart = static_cast<Restart>(i); return true; }
    case kUVelMode:
      if (!arp || c.locked(ParamId::VelMode)) return false;
      { const int i = unorderedMove(c, unitKey(u), D::kTasteVelMode, 2, static_cast<int>(cur.velMode), apply);
        if (i < 0) return false; if (apply) out.velMode = static_cast<VelMode>(i); return true; }
    case kURate: {
      if (!arp || cur.rateMode != RateMode::Sync || c.locked(ParamId::Rate)) return false;
      const Family f = familyOf(cur.rate);
      Cands cs;
      for (int i = f.lo; i <= f.hi; ++i) cs.add(i);
      if (familyOf(c.home.rate).m == f.m) cs.add(posIn(f, c.home.rate));
      const int i = orderedMove(c, unitKey(u), cs, posIn(f, cur.rate), f.n, apply);
      if (i < 0) return false;
      if (apply) out.rate = static_cast<Rate>(f.m[i]);
      return true;
    }
    case kURepeats: {
      if (!arp || cur.repeats == 0 || c.locked(ParamId::Repeats)) return false;
      Cands cs;
      repeatsAdmitted(c, cs, false);
      const int i = orderedMove(c, unitKey(u), cs, static_cast<int>(cur.repeats), 17, apply);
      if (i < 0) return false;
      if (apply) out.repeats = i;
      return true;
    }
    case kUScale: {
      if (!scaleLive(c, arp)) return false;
      if (!c.locked(ParamId::Quantize)) {
        const int i = unorderedMove(c, kIdScale, D::kTasteQuantize, 3, static_cast<int>(cur.quantize), apply);
        if (i >= 0) { if (apply) out.quantize = static_cast<Quantize>(i); return true; }
      }
      if (firstMoveOnly(c)) return false;
      if (ownScaleLocked(c) || cur.scaleSource != ScaleSource::Own || cur.quantize == Quantize::Off) return false;
      const int ns = static_cast<int>(sizeof D::kScaleTaste / sizeof D::kScaleTaste[0]);
      u32 w[32];
      bool any = false;
      for (int i = 0; i < ns; ++i) { w[i] = D::kScaleMask[i] == maskOf(cur) ? 0u : D::kScaleTaste[i]; any = any || w[i] > 0; }
      if (any) {
        if (apply) { const int i = pickTaste(c, kIdScaleMask, kMust, w, ns); writeMask(out, D::kScaleMask[i]); }
        return true;
      }
      if (apply) {   // the root, one fifth either way
        const u32 r = c.rd(id(ParamId::ScaleRoot), kMust);
        const int p = fifthsPos(cur.scaleRoot);
        out.scaleRoot = D::kFifths[(p + ((r >> 31) ? 1 : 11)) % 12];
      }
      return true;
    }
    case kUTransposition: {
      if (!arp) return false;
      const bool sL = c.locked(ParamId::Steps), uL = c.locked(ParamId::StepUnit), zL = c.locked(ParamId::StepSize);
      if (!sL) {
        Cands cs;
        stepsAdmitted(c, cs);
        const int i = orderedMove(c, unitKey(u), cs, static_cast<int>(cur.steps), 9, apply);
        if (i >= 0) {
          if (apply) {
            out.steps = i;
            if (!zL && i > 0 && i <= D::kBandSteps[1] && iabs(static_cast<i64>(i) * cur.stepSize) > spanBound(cur.stepUnit)) {
              u32 w[8];
              sizeWeights(c, cur.stepUnit, i, nullptr, w);
              const int k = pickTaste(c, id(ParamId::StepSize), kMust, w, 8);
              if (k < 0) ++c.empty; else out.stepSize = stepTable(cur.stepUnit).rows[k][0];
            }
          }
          return true;
        }
      }
      if (firstMoveOnly(c)) return false;
      if (zL || cur.steps <= 0) return false;
      {
        u32 w[8];
        const i64 ex = cur.stepSize;
        sizeWeights(c, cur.stepUnit, cur.steps, &ex, w);
        bool any = false;
        for (u32 x : w) any = any || x > 0;
        if (any) {
          if (apply) out.stepSize = stepTable(cur.stepUnit).rows[pickTaste(c, id(ParamId::StepSize), kMust, w, 8)][0];
          return true;
        }
      }
      if (uL) return false;
      const StepUnit other = cur.stepUnit == StepUnit::Semitones ? StepUnit::Degrees : StepUnit::Semitones;
      if (other == StepUnit::Degrees && cur.quantize == Quantize::Off) return false;
      if (apply) {
        out.stepUnit = other;
        u32 w[8];
        sizeWeights(c, other, cur.steps, nullptr, w);
        const int k = pickTaste(c, id(ParamId::StepSize), kMust, w, 8);
        if (k < 0) ++c.empty; else out.stepSize = stepTable(other).rows[k][0];
      }
      return true;
    }
    case kUChord: {
      if (!cur.enabled) return false;
      if (!cur.chordEnabled) {
        if (c.locked(ParamId::ChordEnabled)) return false;
        if (apply) out.chordEnabled = true;   // alone: every other member keeps cur
        return true;
      }
      for (int s = 0; s < 6; ++s) {   // (a) redraw the first on, unlocked shift
        if (!cur.chordOn[s] || c.locked(chordShift(s))) continue;
        const Table t = chordTable(cur.chordUnit);
        u32 w[32];
        bool any = false;
        for (int i = 0; i < t.n; ++i) { w[i] = chordValue(c, t, i) == cur.chordShift[s] ? 0u : static_cast<u32>(t.rows[i][1]); any = any || w[i] > 0; }
        if (!any) continue;
        if (apply) out.chordShift[s] = chordValue(c, t, pickTaste(c, id(chordShift(s)), kMust, w, t.n));
        return true;
      }
      if (firstMoveOnly(c)) return false;
      for (int s = 0; s < 6; ++s) {   // (b) turn on the first off, unlocked slot, its shift forced unless locked
        if (cur.chordOn[s] || c.locked(chordOn(s))) continue;
        if (apply) {
          out.chordOn[s] = true;
          if (!c.locked(chordShift(s))) {
            const Table t = chordTable(cur.chordUnit);
            u32 w[32];
            for (int i = 0; i < t.n; ++i) w[i] = static_cast<u32>(t.rows[i][1]);
            out.chordShift[s] = chordValue(c, t, pickTaste(c, id(chordShift(s)), kMust, w, t.n));
          }
        }
        return true;
      }
      for (int s = 5; s >= 0; --s) {   // (c) turn off the last on, unlocked slot
        if (!cur.chordOn[s] || c.locked(chordOn(s))) continue;
        if (apply) out.chordOn[s] = false;
        return true;
      }
      if (!c.locked(ParamId::ChordUnit) && !anyShiftLocked(c)) {   // (d) flip the unit, every on shift forced
        const StepUnit other = cur.chordUnit == StepUnit::Semitones ? StepUnit::Degrees : StepUnit::Semitones;
        if (other == StepUnit::Semitones || cur.quantize != Quantize::Off) {
          if (apply) {
            out.chordUnit = other;
            const Table t = chordTable(other);
            u32 w[32];
            for (int i = 0; i < t.n; ++i) w[i] = static_cast<u32>(t.rows[i][1]);
            for (int s = 0; s < 6; ++s) if (cur.chordOn[s]) out.chordShift[s] = chordValue(c, t, pickTaste(c, id(chordShift(s)), kMust, w, t.n));
          }
          return true;
        }
      }
      if (!c.locked(ParamId::ChordEnabled) && cur.arpEnabled) {   // (e) the switch, alone; never off while the arp is off
        if (apply) out.chordEnabled = false;
        return true;
      }
      return false;
    }
    default: return false;
  }
}

bool structureChanged(const Settings& a, const Settings& b) {
  for (int i = 0; i < kParamCount; ++i)
    if (D::kClass[i] == 0 && getParam(a, static_cast<ParamId>(i)) != getParam(b, static_cast<ParamId>(i))) return true;
  return false;
}

// Must change: when Structure changed nothing, the movable unit with the lowest amount draw moves by its own rule.
bool mustChange(Ctx& c) {
  c.structurePhase = true;
  const bool arp = arpLive(c);
  int best = -1;
  u32 bestR = 0;
  for (int u = 0; u < kUnitCount; ++u) {
    if (!moveUnit(c, u, arp, false)) continue;
    const u32 r = c.rd(unitKey(u), kAmount);
    if (best < 0 || r < bestR) { best = u; bestR = r; }
  }
  if (best >= 0) moveUnit(c, best, arp, true);
  c.structurePhase = false;
  return best >= 0;
}

Settings clampedCopy(const Settings& s) {
  Settings o = s;
  for (int i = 0; i < kParamCount; ++i) setParam(o, static_cast<ParamId>(i), getParam(s, static_cast<ParamId>(i)));
  return o;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ public
std::int64_t getParam(const Settings& s, ParamId p) noexcept {
  const int i = static_cast<int>(p);
  if (p >= ParamId::ScaleDeg0 && p <= ParamId::ScaleDeg11) return s.scaleDeg[i - static_cast<int>(ParamId::ScaleDeg0)];
  if (p >= ParamId::ChordOn1 && p <= ParamId::ChordShift6) {
    const int k = i - static_cast<int>(ParamId::ChordOn1);
    return (k % 2 == 0) ? static_cast<i64>(s.chordOn[k / 2]) : static_cast<i64>(s.chordShift[k / 2]);
  }
  switch (p) {
    case ParamId::Enabled: return s.enabled;
    case ParamId::ArpEnabled: return s.arpEnabled;
    case ParamId::Order: return static_cast<i64>(s.order);
    case ParamId::Pattern: return static_cast<i64>(s.pattern);
    case ParamId::Rate: return static_cast<i64>(s.rate);
    case ParamId::RateMode: return static_cast<i64>(s.rateMode);
    case ParamId::StepTimeUs: return s.stepTimeUs;
    case ParamId::Steps: return s.steps;
    case ParamId::StepSize: return s.stepSize;
    case ParamId::StepUnit: return static_cast<i64>(s.stepUnit);
    case ParamId::Gate: return s.gate;
    case ParamId::Swing: return s.swing;
    case ParamId::Latch: return static_cast<i64>(s.latch);
    case ParamId::VelMode: return static_cast<i64>(s.velMode);
    case ParamId::VelFixed: return s.velFixed;
    case ParamId::VelRandom: return s.velRandom;
    case ParamId::GateRandom: return s.gateRandom;
    case ParamId::RandomCurve: return s.randomCurve;
    case ParamId::Restart: return static_cast<i64>(s.restart);
    case ParamId::Repeats: return s.repeats;
    case ParamId::Quantize: return static_cast<i64>(s.quantize);
    case ParamId::ScaleSource: return static_cast<i64>(s.scaleSource);
    case ParamId::ScaleRoot: return s.scaleRoot;
    case ParamId::Seed: return s.seed;
    case ParamId::ChordEnabled: return s.chordEnabled;
    case ParamId::ChordUnit: return static_cast<i64>(s.chordUnit);
    default: return 0;
  }
}

void setParam(Settings& s, ParamId p, std::int64_t v) noexcept {
  const int i = static_cast<int>(p);
  if (i < 0 || i >= kParamCount) return;
  v = clampv(v, D::kParamMin[i], D::kParamMax[i]);
  if (p >= ParamId::ScaleDeg0 && p <= ParamId::ScaleDeg11) { s.scaleDeg[i - static_cast<int>(ParamId::ScaleDeg0)] = v != 0; return; }
  if (p >= ParamId::ChordOn1 && p <= ParamId::ChordShift6) {
    const int k = i - static_cast<int>(ParamId::ChordOn1);
    if (k % 2 == 0) s.chordOn[k / 2] = v != 0; else s.chordShift[k / 2] = static_cast<std::int32_t>(v);
    return;
  }
  const auto i32 = static_cast<std::int32_t>(v);
  switch (p) {
    case ParamId::Enabled: s.enabled = v != 0; break;
    case ParamId::ArpEnabled: s.arpEnabled = v != 0; break;
    case ParamId::Order: s.order = static_cast<Order>(v); break;
    case ParamId::Pattern: s.pattern = static_cast<Pattern>(v); break;
    case ParamId::Rate: s.rate = static_cast<Rate>(v); break;
    case ParamId::RateMode: s.rateMode = static_cast<RateMode>(v); break;
    case ParamId::StepTimeUs: s.stepTimeUs = i32; break;
    case ParamId::Steps: s.steps = i32; break;
    case ParamId::StepSize: s.stepSize = i32; break;
    case ParamId::StepUnit: s.stepUnit = static_cast<StepUnit>(v); break;
    case ParamId::Gate: s.gate = i32; break;
    case ParamId::Swing: s.swing = i32; break;
    case ParamId::Latch: s.latch = static_cast<Latch>(v); break;
    case ParamId::VelMode: s.velMode = static_cast<VelMode>(v); break;
    case ParamId::VelFixed: s.velFixed = i32; break;
    case ParamId::VelRandom: s.velRandom = i32; break;
    case ParamId::GateRandom: s.gateRandom = i32; break;
    case ParamId::RandomCurve: s.randomCurve = i32; break;
    case ParamId::Restart: s.restart = static_cast<Restart>(v); break;
    case ParamId::Repeats: s.repeats = i32; break;
    case ParamId::Quantize: s.quantize = static_cast<Quantize>(v); break;
    case ParamId::ScaleSource: s.scaleSource = static_cast<ScaleSource>(v); break;
    case ParamId::ScaleRoot: s.scaleRoot = i32; break;
    case ParamId::Seed: s.seed = static_cast<std::uint32_t>(v); break;
    case ParamId::ChordEnabled: s.chordEnabled = v != 0; break;
    case ParamId::ChordUnit: s.chordUnit = static_cast<StepUnit>(v); break;
    default: break;
  }
}

bool validSettings(const Settings& s) noexcept {
  for (int i = 0; i < kParamCount; ++i) {
    const i64 v = getParam(s, static_cast<ParamId>(i));
    if (v < D::kParamMin[i] || v > D::kParamMax[i]) return false;
  }
  return true;
}

LockSet defaultLocks() noexcept {
  LockSet l;
  for (int i = 0; i < kParamCount; ++i) if (D::kExempt[i]) l.set(static_cast<ParamId>(i));
  return l;
}
Class classOf(ParamId p) noexcept {
  const int i = static_cast<int>(p);
  return (i < 0 || i >= kParamCount) ? Class::Never : static_cast<Class>(D::kClass[i]);
}
const char* paramName(ParamId p) noexcept {
  const int i = static_cast<int>(p);
  return (i < 0 || i >= kParamCount) ? "" : D::kParamNames[i];
}

Result randomize(const Settings& cur, const Settings& homeIn, const Request& rq) noexcept {
  const Settings home = clampedCopy(homeIn);
  const u32 chaos = rq.chaosPm > 1000 ? 1000 : rq.chaosPm, amount = rq.amountPm > 1000 ? 1000 : rq.amountPm;
  Result res;
#ifdef ARP_PLANTS
  if (rq.plants.allocScratch) { std::vector<int> scratch(16, 1); res.emptyPicks += scratch[3] - 1; }
#endif
  Settings mid = cur;
  if (rq.verb != Verb::Values) {
    Ctx c{cur, home, cur, rq.seed, rq.click, rq.corner, chaos, amount, rq.locks};
#ifdef ARP_PLANTS
    c.pl = rq.plants;
#endif
    structure(c);
    bool mustAllowed = chaos > 0;
#ifdef ARP_PLANTS
    if (c.pl.mustChangeAtChaos0) mustAllowed = true;
    if (c.pl.mustChangeOff) mustAllowed = false;
#endif
    if (rq.mustChange && mustAllowed && !structureChanged(c.out, cur)) res.nothingToDraw = !mustChange(c);
    mid = c.out;
    res.emptyPicks += c.empty;
  }
  Settings fin = mid;
  if (rq.verb != Verb::Structure) {
    Ctx c{mid, home, mid, rq.seed, rq.click, rq.corner, chaos, amount, rq.locks};   // Both: Values reads liveness from Structure's result
#ifdef ARP_PLANTS
    c.pl = rq.plants;
#endif
    values(c);
    fin = c.out;
    res.emptyPicks += c.empty;
  }
  res.settings = fin;
  for (int i = 0; i < kParamCount; ++i)
    if (getParam(fin, static_cast<ParamId>(i)) != getParam(cur, static_cast<ParamId>(i))) res.changed |= std::uint64_t{1} << i;
  return res;
}

}  // namespace arp::rnd
