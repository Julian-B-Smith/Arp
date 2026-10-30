// test_randomize.cpp - RZ1..RZ16 (SPEC 15; D-044, D-048): Arp's own randomize (arp/randomize.h).
// Same charter as the other suites: every detector is preceded by its plant (a plant that does not fire is a FAIL).
// The oracle for classes is a LITERAL list here (and spec/randomize.json via tools/gen_randomize.py --check), never the
// function's own classOf(), so a plant that moves a key cannot move the oracle with it (design §7).
// The core sessions of RZ14 run through runOnce(), never run(): they are kept out of the committed stream digest, which
// predates randomize. RZ1's digest of 10,000 calls is pinned in tests/core/randomize_digest.txt and must be identical on
// every platform (CI runs this suite on Linux g++; the dev Mac runs it on clang).
#include "test_randomize.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "arp/randomize.h"
#include "harness.h"

#ifndef ARP_RANDOMIZE_DIGEST_FILE
#error "ARP_RANDOMIZE_DIGEST_FILE must name tests/core/randomize_digest.txt (tests/core/CMakeLists.txt defines it)"
#endif

namespace arp_test {
namespace {

using namespace arp::rnd;
using P = ParamId;
using Plants = arp::rnd::Plants;   // arp::Plants (the core's) is visible here too

// ---- the oracle: literal class lists (SPEC 15 §3; spec/randomize.json) ----
const std::vector<P> kNever = {P::Enabled, P::ArpEnabled, P::RateMode, P::Latch, P::ScaleSource};
const std::vector<P> kValue = {P::StepTimeUs, P::Gate, P::Swing, P::VelFixed, P::VelRandom, P::GateRandom, P::RandomCurve, P::Seed};
// One statement per line throughout: g++'s -Wmisleading-indentation is an error under -Werror (CI builds on Linux g++).
bool isNever(P p) {
  for (P q : kNever)
    if (q == p) return true;
  return false;
}
bool isValue(P p) {
  for (P q : kValue)
    if (q == p) return true;
  return false;
}
bool isStructure(P p) { return !isNever(p) && !isValue(p); }
const std::vector<P> kArpKeys = {P::Order, P::Pattern, P::Rate, P::Restart, P::Repeats, P::VelMode, P::Steps, P::StepSize, P::StepUnit,
                                 P::Gate, P::Swing, P::StepTimeUs, P::VelFixed, P::VelRandom, P::GateRandom, P::RandomCurve, P::Seed};
P chordOnId(int i) { return static_cast<P>(static_cast<int>(P::ChordOn1) + 2 * i); }
P deg(int i) { return static_cast<P>(static_cast<int>(P::ScaleDeg0) + i); }
std::int64_t g(const Settings& s, P p) { return getParam(s, p); }

// ---- a test-side generator (splitmix64): only builds inputs, never touches the draw under test ----
struct Gen {
  std::uint64_t x;
  explicit Gen(std::uint64_t s) : x(s) {}
  std::uint64_t next() { std::uint64_t z = (x += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
  std::int64_t in(std::int64_t lo, std::int64_t hi) { return lo + static_cast<std::int64_t>(next() % static_cast<std::uint64_t>(hi - lo + 1)); }
  bool chance(int pct) { return in(0, 99) < pct; }
};
const std::int64_t kMin[50] = {0, 0, 0, 0, 0, 0, 10000, 0, -48, 0, 1, 500, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -24, 0, -24, 0, -24, 0, -24, 0, -24, 0, -24};
const std::int64_t kMax[50] = {1, 1, 15, 11, 21, 1, 2000000, 8, 48, 1, 200, 750, 2, 1, 127, 100, 100, 100, 2, 16, 2, 1, 11, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4294967295LL, 1, 1, 1, 24, 1, 24, 1, 24, 1, 24, 1, 24, 1, 24};
Settings randomSettings(Gen& r) {
  Settings s;
  for (int i = 0; i < kParamCount; ++i) setParam(s, static_cast<P>(i), r.in(kMin[i], kMax[i]));
  s.enabled = r.chance(92);
  s.arpEnabled = r.chance(85);
  if (r.chance(60)) s.rateMode = RateMode::Sync;
  if (r.chance(50)) s.repeats = 0;
  if (r.chance(40)) s.steps = static_cast<std::int32_t>(r.in(0, 3));
  return s;
}
LockSet randomLocks(Gen& r) {
  LockSet l;
  const int kind = static_cast<int>(r.in(0, 3));
  if (kind == 1) l = defaultLocks();
  if (kind >= 2) for (int i = 0; i < kParamCount; ++i) if (r.chance(kind == 2 ? 10 : 40)) l.set(static_cast<P>(i));
  return l;
}
Request randomRequest(Gen& r) {
  Request q;
  q.verb = static_cast<Verb>(r.in(0, 2));
  q.seed = static_cast<std::uint32_t>(r.next());
  q.click = static_cast<std::uint32_t>(r.in(0, 1000));
  q.corner = static_cast<std::uint32_t>(r.in(0, 3));
  const int c = static_cast<int>(r.in(0, 9));
  q.chaosPm = c == 0 ? 0 : c == 1 ? 1000 : c == 2 ? static_cast<std::uint32_t>(r.in(1, 20)) : static_cast<std::uint32_t>(r.in(0, 1000));
  q.amountPm = r.chance(30) ? 1000 : static_cast<std::uint32_t>(r.in(0, 1000));
  q.mustChange = r.chance(70);
  q.locks = randomLocks(r);
  return q;
}
// A home: cur itself, another valid setting, or one with raw out-of-range numbers (randomize clamps a home before use).
Settings randomHome(Gen& r, const Settings& cur) {
  const int k = static_cast<int>(r.in(0, 3));
  if (k == 0) return cur;
  Settings h = randomSettings(r);
  if (k == 3) { h.gate = 999; h.repeats = 99; h.steps = 40; h.stepSize = -200; h.chordShift[2] = 77; h.stepTimeUs = 5; h.scaleRoot = 30; }
  return h;
}
bool sameSettings(const Settings& a, const Settings& b) {
  for (int i = 0; i < kParamCount; ++i) if (g(a, static_cast<P>(i)) != g(b, static_cast<P>(i))) return false;
  return true;
}
Settings clampedHome(const Settings& h) {
  Settings o = h;
  for (int i = 0; i < kParamCount; ++i) setParam(o, static_cast<P>(i), g(h, static_cast<P>(i)));
  return o;
}

// One case of the shared corpus.
struct Case { Settings cur, home; Request rq; };
Case makeCase(Gen& r) { Case c; c.cur = randomSettings(r); c.home = randomHome(r, c.cur); c.rq = randomRequest(r); return c; }

std::uint64_t fnvMix(std::uint64_t h, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    h ^= (v >> (8 * i)) & 0xFF;
    h *= 0x100000001B3ull;
  }
  return h;
}
std::uint64_t digestOf(const Plants& pl, int n) {
  Gen r(20261029);
  std::uint64_t h = 0xCBF29CE484222325ull;
  for (int i = 0; i < n; ++i) {
    Case c = makeCase(r);
    c.rq.plants = pl;
    const Result res = randomize(c.cur, c.home, c.rq);
    for (int k = 0; k < kParamCount; ++k) h = fnvMix(h, static_cast<std::uint64_t>(g(res.settings, static_cast<P>(k))));
    h = fnvMix(h, res.changed);
    h = fnvMix(h, (res.nothingToDraw ? 1u : 0u) | (static_cast<std::uint64_t>(res.emptyPicks) << 1));
  }
  return h;
}
std::string hex(std::uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v)); return b; }

// ---------------------------------------------------------------------------------------------------- RZ1, RZ2
void testRZ1() {
  std::puts("RZ1: determinism (a pinned digest of 10,000 calls, identical on every platform)");
  Plants a; a.carriedStream = true;
  Plants b; b.pointerInSeed = true;
  const std::uint64_t clean = digestOf(Plants{}, 10000);
  plant(digestOf(a, 10000) != clean, "RZ1 detects a carried stream (one mulberry32 advanced per draw)");
  plant(digestOf(b, 10000) != clean, "RZ1 detects an address in the draw");
  check(digestOf(Plants{}, 10000) == clean, "RZ1: the same 10,000 inputs give the same results twice in one process");
  std::string pinned;
  { std::ifstream f(ARP_RANDOMIZE_DIGEST_FILE); std::string line; while (std::getline(f, line)) if (!line.empty() && line[0] != '#') { pinned = line; break; } }
  check(pinned == hex(clean), "RZ1: the 10,000-call digest equals tests/core/randomize_digest.txt (" + hex(clean) + (pinned == hex(clean) ? "" : ", pinned " + (pinned.empty() ? std::string("missing") : pinned)) + ")");
}

// RZ2: locking Order never changes any other key's result (must change off, so a lock cannot trigger it).
int rz2Violations(const Plants& pl) {
  Gen r(2);
  int bad = 0;
  for (int i = 0; i < 4000; ++i) {
    Case c = makeCase(r);
    c.rq.mustChange = false;
    c.rq.plants = pl;
    c.rq.locks.set(P::Order, false);
    const Result a = randomize(c.cur, c.home, c.rq);
    c.rq.locks.set(P::Order, true);
    const Result b = randomize(c.cur, c.home, c.rq);
    for (int k = 0; k < kParamCount; ++k) if (static_cast<P>(k) != P::Order && g(a.settings, static_cast<P>(k)) != g(b.settings, static_cast<P>(k))) { ++bad; break; }
  }
  return bad;
}
void testRZ2() {
  std::puts("RZ2: position-free draws (locking one key never changes another)");
  Plants a; a.carriedStream = true;
  plant(rz2Violations(a) > 0, "RZ2 detects a carried stream (locking order shifts the next key's draw)");
  check(rz2Violations(Plants{}) == 0, "RZ2: over 4,000 cases, locking order changes no other key");
}

// ---------------------------------------------------------------------------------------------------- RZ3 validity
struct Rz3 { int invalid = 0, empty = 0, n = 0; };
Rz3 rz3(const Plants& pl, int n) {
  Gen r(3);
  Rz3 o;
  for (int i = 0; i < n; ++i) {
    Case c = makeCase(r);
    if (i % 7 == 0) { c.cur.repeats = static_cast<std::int32_t>(r.in(9, 16)); c.home.repeats = static_cast<std::int32_t>(r.in(0, 8)); }
    if (i % 5 == 0) { c.home.steps = static_cast<std::int32_t>(r.in(0, 8)); c.home.stepUnit = r.chance(50) ? StepUnit::Semitones : StepUnit::Degrees; }
    if (i % 11 == 0) { c.cur.steps = 3; c.cur.stepSize = 12; c.home.steps = 3; c.rq.locks.set(P::StepSize); c.rq.chaosPm = static_cast<std::uint32_t>(r.in(1, 200)); }
    if (i % 13 == 0) { c.cur.chordEnabled = true; c.cur.enabled = true; }
    c.rq.plants = pl;
    const Result res = randomize(c.cur, c.home, c.rq);
    if (!validSettings(res.settings)) ++o.invalid;
    o.empty += res.emptyPicks;
    ++o.n;
  }
  return o;
}
void testRZ3() {
  std::puts("RZ3: validity (every result valid; no empty set, no zero total weight)");
  Plants a; a.shiftOutOfRange = true;
  Plants b; b.noOwnShapeRule = true;
  Plants c; c.centreNotCandidate = true;
  plant(rz3(a, 100000).invalid > 0, "RZ3 detects a chord shift drawn as +25");
  plant(rz3(b, 100000).empty > 0, "RZ3 detects the span filter applied to a steps above 3 (an empty size table)");
  plant(rz3(c, 100000).empty > 0, "RZ3 detects a bell whose centre is not a candidate (zero total weight)");
  const Rz3 o = rz3(Plants{}, 100000);
  check(o.invalid == 0 && o.empty == 0, "RZ3: 100,000 calls (extreme cur and home, locks swept, chaos 0..1000): every result valid, no empty pick (" +
        std::to_string(o.invalid) + " invalid, " + std::to_string(o.empty) + " empty)");
}

// ---------------------------------------------------------------------------------------------------- RZ4, RZ5
int rz4Violations(const Plants& pl) {
  Gen r(4);
  int bad = 0;
  for (int i = 0; i < 20000; ++i) {
    Case c = makeCase(r);
    c.rq.plants = pl;
    for (int k = 0; k < kParamCount; ++k) if (r.chance(25)) c.rq.locks.set(static_cast<P>(k));
    const Result res = randomize(c.cur, c.home, c.rq);
    for (int k = 0; k < kParamCount; ++k) if (c.rq.locks.test(static_cast<P>(k)) && g(res.settings, static_cast<P>(k)) != g(c.cur, static_cast<P>(k))) { ++bad; break; }
  }
  return bad;
}
void testRZ4() {
  std::puts("RZ4: locks");
  Plants a; a.locksValuesOnly = true;
  plant(rz4Violations(a) > 0, "RZ4 detects locks honoured by Values only");
  check(rz4Violations(Plants{}) == 0, "RZ4: over 20,000 cases, every locked key equals cur on every verb");
}
int rz5Violations(const Plants& pl) {
  Gen r(5);
  int bad = 0;
  for (int i = 0; i < 20000; ++i) {
    Case c = makeCase(r);
    c.rq.plants = pl;
    c.rq.locks = LockSet{};   // unlocked: never-draw must hold without any lock
    const Result res = randomize(c.cur, c.home, c.rq);
    for (P p : kNever) if (g(res.settings, p) != g(c.cur, p)) { ++bad; break; }
  }
  return bad;
}
void testRZ5() {
  std::puts("RZ5: never-draw keys untouched");
  Plants a; a.drawLatch = true;
  plant(rz5Violations(a) > 0, "RZ5 detects latch drawn");
  check(rz5Violations(Plants{}) == 0, "RZ5: over 20,000 unlocked cases, enabled, arp_enabled, rate_mode, latch and scale_source equal cur");
}

// ---------------------------------------------------------------------------------------------------- RZ6 chaos 0
struct Rz6 { int identity = 0, homeOrCur = 0; };
Rz6 rz6(const Plants& pl) {
  Gen r(6);
  Rz6 o;
  for (int i = 0; i < 20000; ++i) {
    Case c = makeCase(r);
    c.rq.chaosPm = 0;
    c.rq.plants = pl;
    c.home = c.cur;
    if (!sameSettings(randomize(c.cur, c.cur, c.rq).settings, c.cur)) ++o.identity;
    Settings h = randomHome(r, c.cur);
    if (r.chance(50)) { h.steps = 2; h.stepSize = 24; }   // a home combination past the span bound: its own shape, kept whole at chaos 0
    const Settings hc = clampedHome(h);
    const Result res = randomize(c.cur, h, c.rq);
    for (int k = 0; k < kParamCount; ++k) {
      const std::int64_t v = g(res.settings, static_cast<P>(k));
      if (v != g(c.cur, static_cast<P>(k)) && v != g(hc, static_cast<P>(k))) { ++o.homeOrCur; break; }
    }
  }
  return o;
}
void testRZ6() {
  std::puts("RZ6: chaos 0");
  Plants a; a.mustChangeAtChaos0 = true;
  Plants b; b.forcedAtChaos0 = true;
  plant(rz6(a).identity > 0, "RZ6 detects must change firing at chaos 0");
  plant(rz6(b).homeOrCur > 0, "RZ6 detects a group's forced redraw at chaos 0");
  const Rz6 o = rz6(Plants{});
  check(o.identity == 0, "RZ6: with home = cur, chaos 0 returns cur exactly (20,000 cases, every verb, amount and lock set, must change on and off)");
  check(o.homeOrCur == 0, "RZ6: with a separate home, chaos 0 gives every key cur's or home's value (20,000 cases)");
}

// ---------------------------------------------------------------------------------------------------- RZ7 nesting
int rz7Violations(const Plants& pl) {
  Gen r(7);
  int bad = 0;
  for (int i = 0; i < 12000; ++i) {
    Case c = makeCase(r);
    if (i % 2 == 0) {   // the controlling keys on (design §9): steps 1 at +12, vel_random 20, gate_random 0 locked, fixed velocity, the stage on with slots off
      Settings s;
      s.enabled = true; s.steps = 1; s.stepSize = 12; s.velRandom = 20; s.gateRandom = 0; s.velMode = VelMode::Fixed; s.chordEnabled = true;
      for (bool& on : s.chordOn) on = false;
      c.cur = s; c.home = s;
      c.rq.locks = LockSet{}; c.rq.locks.set(P::GateRandom);
      c.rq.verb = r.chance(50) ? Verb::Values : Verb::Structure;
    }
    if (c.rq.verb == Verb::Both) c.rq.verb = Verb::Values;   // the law is per verb (Both's documented exceptions, D-048 answer 11)
    c.rq.mustChange = false;
    c.rq.chaosPm = static_cast<std::uint32_t>(r.chance(50) ? 1000 : r.in(1, 1000));
    c.rq.plants = pl;
    Request a = c.rq, b = c.rq;
    a.amountPm = static_cast<std::uint32_t>(r.in(0, 700));
    b.amountPm = static_cast<std::uint32_t>(r.in(a.amountPm, 1000));
    const Result ra = randomize(c.cur, c.home, a), rb = randomize(c.cur, c.home, b);
    for (int k = 0; k < kParamCount; ++k) {
      const P p = static_cast<P>(k);
      if (g(ra.settings, p) != g(c.cur, p) && g(rb.settings, p) != g(ra.settings, p)) { ++bad; break; }
    }
  }
  return bad;
}
void testRZ7() {
  std::puts("RZ7: amount nesting, per verb, must change off");
  Plants a; a.livenessFromOut = true;
  Plants b; b.pickDependsOnAmount = true;
  plant(rz7Violations(a) > 0, "RZ7 detects liveness read from the keys just drawn (revision 1's rule)");
  plant(rz7Violations(b) > 0, "RZ7 detects a value pick that depends on the amount");
  check(rz7Violations(Plants{}) == 0, "RZ7: over 12,000 amount pairs (half with the controlling keys on, home != cur included), the keys changed at a lower amount change at a higher one, to the same values");
}

// ---------------------------------------------------------------------------------------------------- RZ8 verbs
struct Rz8 { int structure = 0, values = 0, both = 0; };
Rz8 rz8(const Plants& pl) {
  Gen r(8);
  Rz8 o;
  for (int i = 0; i < 10000; ++i) {
    Case c = makeCase(r);
    c.rq.plants = pl;
    Request s = c.rq; s.verb = Verb::Structure;
    Request v = c.rq; v.verb = Verb::Values;
    Request b = c.rq; b.verb = Verb::Both;
    const Result rs = randomize(c.cur, c.home, s), rv = randomize(c.cur, c.home, v), rb = randomize(c.cur, c.home, b);
    for (int k = 0; k < kParamCount; ++k) {
      const P p = static_cast<P>(k);
      if (!isStructure(p) && g(rs.settings, p) != g(c.cur, p)) { ++o.structure; break; }
    }
    for (int k = 0; k < kParamCount; ++k) {
      const P p = static_cast<P>(k);
      if (!isValue(p) && g(rv.settings, p) != g(c.cur, p)) { ++o.values; break; }
    }
    if (!sameSettings(rb.settings, randomize(rs.settings, c.home, v).settings)) ++o.both;
  }
  return o;
}
void testRZ8() {
  std::puts("RZ8: verbs (against the literal class list)");
  Plants a; a.wrongClass = true;
  plant(rz8(a).structure > 0, "RZ8 detects swing drawn by Structure");
  const Rz8 o = rz8(Plants{});
  check(o.structure == 0, "RZ8: Structure changes only structure keys (10,000 cases)");
  check(o.values == 0, "RZ8: Values changes only value keys (10,000 cases)");
  check(o.both == 0, "RZ8: Both equals Values run on Structure's result at the same click (10,000 cases)");
}

// ---------------------------------------------------------------------------------------------------- RZ9 live when
int rz9Violations(const Plants& pl) {
  Gen r(9);
  int bad = 0;
  for (int i = 0; i < 20000; ++i) {
    Case c = makeCase(r);
    c.rq.plants = pl;
    c.rq.locks = LockSet{};
    const Result res = randomize(c.cur, c.home, c.rq);
    const Settings& o = res.settings;
    bool b = false;
    if (c.cur.rateMode == RateMode::Sync && o.stepTimeUs != c.cur.stepTimeUs) b = true;
    if (c.cur.velMode == VelMode::Played && c.rq.verb == Verb::Values && o.velFixed != c.cur.velFixed) b = true;
    if (!c.cur.enabled && res.changed != 0) b = true;
    if (!c.cur.arpEnabled) for (P p : kArpKeys) if (g(o, p) != g(c.cur, p)) b = true;
    if (!o.chordEnabled) {
      if (o.chordUnit != c.cur.chordUnit) b = true;
      for (int k = 0; k < 6; ++k) if (o.chordOn[k] != c.cur.chordOn[k] || o.chordShift[k] != c.cur.chordShift[k]) b = true;
    }
    // The rest of the "live" rules (the critic's M3): a seed nothing reads, the curve with both amounts at 0, rate and restart in time
    // mode, the unit and size with no passes, the shift of a slot that is off.
    const bool seedRead = c.cur.order == Order::Random || c.cur.order == Order::Shuffle || c.cur.velRandom > 0 || c.cur.gateRandom > 0 ||
                          c.cur.pattern == Pattern::SparseChance || c.cur.pattern == Pattern::User;
    if (c.rq.verb == Verb::Values && !seedRead && o.seed != c.cur.seed) b = true;
    if (c.rq.verb == Verb::Values && c.cur.velRandom == 0 && c.cur.gateRandom == 0 && o.randomCurve != c.cur.randomCurve) b = true;
    if (c.cur.rateMode == RateMode::Time && (o.rate != c.cur.rate || o.restart != c.cur.restart)) b = true;
    if (o.steps == 0 && (o.stepSize != c.cur.stepSize || o.stepUnit != c.cur.stepUnit)) b = true;
    for (int k = 0; k < 6; ++k)
      if (!o.chordOn[k] && o.chordShift[k] != c.cur.chordShift[k]) b = true;
    if (c.cur.scaleSource == ScaleSource::Global) {
      if (o.scaleRoot != c.cur.scaleRoot) b = true;
      for (int k = 0; k < 12; ++k) if (o.scaleDeg[k] != c.cur.scaleDeg[k]) b = true;
    }
    bad += b;
  }
  return bad;
}
void testRZ9() {
  std::puts("RZ9: live when, read on cur");
  Plants a; a.stepTimeInSync = true;
  Plants b; b.ignoreArpEnabled = true;
  plant(rz9Violations(a) > 0, "RZ9 detects step_time_us drawn in sync mode");
  plant(rz9Violations(b) > 0, "RZ9 detects the arp's keys drawn with the arp off");
  Plants c; c.seedAlwaysRead = true;
  Plants d; d.curveAlwaysLive = true;
  plant(rz9Violations(c) > 0, "RZ9 detects the seed drawn when nothing reads it");
  plant(rz9Violations(d) > 0, "RZ9 detects random_curve drawn with both randomness amounts at 0");
  check(rz9Violations(Plants{}) == 0, "RZ9: over 20,000 unlocked cases, no key is drawn that cur cannot hear (step time in sync, fixed velocity in played, the arp off, the stage off, the own scale with source global, the module off, a seed nothing reads, the curve at zero amounts, rate and restart in time mode, unit and size with no passes, the shift of an off slot)");
}

// ---------------------------------------------------------------------------------------------------- RZ10 distribution
struct Rz10 { double move100 = 0, move350 = 0, move750 = 0, ks1000 = 1, rateWithin = 0; int rootF = 0, rootG = 0; };
Rz10 rz10(const Plants& pl) {
  Rz10 o;
  Settings home;
  home.enabled = true; home.gate = 80;
  auto meanMove = [&](std::uint32_t chaos) {
    double sum = 0;
    for (std::uint32_t s = 0; s < 4000; ++s) {
      Request q; q.verb = Verb::Values; q.seed = s; q.chaosPm = chaos; q.amountPm = 1000; q.plants = pl;
      sum += std::abs(randomize(home, q).settings.gate - 80);
    }
    return sum / 4000;
  };
  o.move100 = meanMove(100); o.move350 = meanMove(350); o.move750 = meanMove(750);
  { // KS distance from flat over the band 25..100 at chaos 1000
    std::vector<int> hist(201, 0);
    const int n = 20000;
    for (std::uint32_t s = 0; s < static_cast<std::uint32_t>(n); ++s) {
      Request q; q.verb = Verb::Values; q.seed = s; q.click = 7; q.chaosPm = 1000; q.amountPm = 1000; q.plants = pl;
      ++hist[randomize(home, q).settings.gate];
    }
    double cum = 0, worst = 0;
    for (int v = 25; v <= 100; ++v) { cum += hist[v]; const double want = static_cast<double>(v - 24) / 76.0; worst = std::max(worst, std::abs(cum / n - want)); }
    o.ks1000 = worst;
  }
  { // rate from 1/16, unlocked, chaos 350: share within one rung of its family
    Settings s = home; s.rate = Rate::R1_16;
    int within = 0;
    for (std::uint32_t k = 0; k < 4000; ++k) {
      Request q; q.verb = Verb::Structure; q.seed = k; q.chaosPm = 350; q.amountPm = 1000; q.mustChange = false; q.locks = LockSet{}; q.plants = pl;
      const Rate x = randomize(s, q).settings.rate;
      within += (x == Rate::R1_8 || x == Rate::R1_16 || x == Rate::R1_32);
    }
    o.rateWithin = within / 4000.0;
  }
  { // the own scale's root from C at chaos 350: both neighbours on the circle of fifths (F = 5, G = 7)
    Settings s = home; s.scaleSource = ScaleSource::Own; s.quantize = Quantize::Scale; s.scaleRoot = 0;
    for (std::uint32_t k = 0; k < 4000; ++k) {
      Request q; q.verb = Verb::Structure; q.seed = k; q.chaosPm = 350; q.amountPm = 1000; q.mustChange = false; q.locks = LockSet{}; q.locks.set(P::Quantize); q.plants = pl;
      const int root = randomize(s, q).settings.scaleRoot;
      o.rootF += root == 5; o.rootG += root == 7;
    }
  }
  return o;
}
bool rz10Ok(const Rz10& o) {
  return o.move100 < o.move350 && o.move350 < o.move750 && o.move100 < 3.0 && o.ks1000 < 0.05 && o.rateWithin >= 0.90 && o.rootF > 0 && o.rootG > 0 && o.rootF * 4 > o.rootG;
}
void testRZ10() {
  std::puts("RZ10: distribution sanity (exact on fixed seeds)");
  Plants a; a.ignoreChaos = true;
  Plants b; b.reverseSigma = true;
  Plants c; c.linearRoot = true;
  plant(!rz10Ok(rz10(a)), "RZ10 detects a bell that ignores chaos");
  plant(!rz10Ok(rz10(b)), "RZ10 detects the sigma table read backwards");
  plant(!rz10Ok(rz10(c)), "RZ10 detects a root distance that is not circular");
  const Rz10 o = rz10(Plants{});
  char m[400];
  std::snprintf(m, sizeof m, "RZ10: gate mean move %.2f < %.2f < %.2f (chaos 100/350/750; the first under 3); KS from flat at 1000 = %.4f (< 0.05); rate within one rung at 350 = %.1f %% (>= 90 %%); root F %d, G %d",
                o.move100, o.move350, o.move750, o.ks1000, 100 * o.rateWithin, o.rootF, o.rootG);
  check(rz10Ok(o), m);
}

// ---------------------------------------------------------------------------------------------------- RZ11 must change
bool structureDiffers(const Settings& a, const Settings& b) {
  for (int k = 0; k < kParamCount; ++k) if (isStructure(static_cast<P>(k)) && g(a, static_cast<P>(k)) != g(b, static_cast<P>(k))) return true;
  return false;
}
LockSet allStructureLocked() {
  LockSet l;
  for (int k = 0; k < kParamCount; ++k)
    if (isStructure(static_cast<P>(k))) l.set(static_cast<P>(k));
  return l;
}
struct Rz11 { int silent = 0, special = 0; };
Rz11 rz11(const Plants& pl) {
  Gen r(11);
  Rz11 o;
  for (int i = 0; i < 20000; ++i) {
    Case c = makeCase(r);
    c.rq.verb = Verb::Structure;
    c.rq.mustChange = true;
    c.rq.chaosPm = static_cast<std::uint32_t>(i % 4 == 0 ? r.in(1, 5) : r.in(1, 1000));
    c.rq.plants = pl;
    const Result res = randomize(c.cur, c.home, c.rq);
    if (!res.nothingToDraw && !structureDiffers(res.settings, c.cur)) ++o.silent;
  }
  // The special cases (design §7): each group with its usual first move locked.
  auto one = [&](Settings s, LockSet unlockOnly, std::function<bool(const Result&)> ok) {
    for (std::uint32_t k = 0; k < 200; ++k) {
      Request q; q.verb = Verb::Structure; q.seed = k; q.chaosPm = 1 + k % 5; q.amountPm = 0; q.mustChange = true; q.plants = pl;
      q.locks.bits = allStructureLocked().bits & ~unlockOnly.bits;
      if (!ok(randomize(s, q))) { ++o.special; return; }
    }
  };
  Settings base; base.enabled = true;
  { LockSet u; u.set(P::StepSize); Settings s = base; s.steps = 2; s.stepSize = 7;          // steps locked: the size moves
    one(s, u, [&](const Result& x) { return x.settings.stepSize != 7 && !x.nothingToDraw; }); }
  { LockSet u; u.set(P::ScaleRoot); for (int d = 0; d < 12; ++d) u.set(deg(d));            // quantize exempt, the own scale unlocked
    Settings s = base; s.scaleSource = ScaleSource::Own; s.quantize = Quantize::Scale;
    one(s, u, [&](const Result& x) { return structureDiffers(x.settings, s) && !x.nothingToDraw; }); }
  { LockSet u; for (int k = 0; k < 6; ++k) u.set(chordOnId(k));                              // every shift locked, slots free
    Settings s = base; s.chordEnabled = true;
    one(s, u, [&](const Result& x) { return structureDiffers(x.settings, s) && !x.nothingToDraw; }); }
  { LockSet u; u.set(P::ChordEnabled);                                                       // the stage off, every other chord member locked
    Settings s = base; s.chordEnabled = false;
    one(s, u, [&](const Result& x) { Settings w = s; w.chordEnabled = true; return sameSettings(x.settings, w); }); }
  { LockSet u; u.set(P::ChordEnabled);                                                       // arp off, stage on, all else locked: not movable
    Settings s = base; s.arpEnabled = false; s.chordEnabled = true;
    one(s, u, [&](const Result& x) { return x.nothingToDraw && sameSettings(x.settings, s); }); }
  return o;
}
void testRZ11() {
  std::puts("RZ11: must change");
  Plants a; a.mustChangeOff = true;
  Plants b; b.mustChangeBell = true;
  Plants c; c.mustChangeFirstMoveOnly = true;
  plant(rz11(a).silent > 0, "RZ11 detects must change turned off");
  plant(rz11(b).silent > 0, "RZ11 detects an ordered key moved by the bell (zero weights at low chaos)");
  plant(rz11(c).special > 0, "RZ11 detects groups that try only their first move");
  const Rz11 o = rz11(Plants{});
  check(o.silent == 0, "RZ11: over 20,000 Structure clicks at chaos 1..1000, must change always changes a key unless it reports nothing to draw");
  check(o.special == 0, "RZ11: groups whose usual first move is locked still move; the stage turns on alone; arp off + stage on + all else locked reports nothing to draw");
}

// ---------------------------------------------------------------------------------------------------- RZ12 musical safety
const Rate kStraight[] = {Rate::R2_1, Rate::R1_1, Rate::R1_2, Rate::R1_4, Rate::R1_8, Rate::R1_16, Rate::R1_32, Rate::R1_64};
const Rate kDotted[] = {Rate::R3_2, Rate::R3_4, Rate::R3_8, Rate::R3_16, Rate::R3_32, Rate::R3_64, Rate::R3_128};
const Rate kTriplet[] = {Rate::R2_1T, Rate::R1_1T, Rate::R1_2T, Rate::R1_4T, Rate::R1_8T, Rate::R1_16T, Rate::R1_32T};
int famPos(Rate x, int& fam) {
  for (int i = 0; i < 8; ++i) if (kStraight[i] == x) { fam = 0; return i; }
  for (int i = 0; i < 7; ++i) if (kDotted[i] == x) { fam = 1; return i; }
  for (int i = 0; i < 7; ++i) if (kTriplet[i] == x) { fam = 2; return i; }
  fam = -1; return -1;
}
bool inBandOrHome(std::int64_t v, std::int64_t lo, std::int64_t hi, std::int64_t home, std::int64_t cur) { return v == cur || v == home || (v >= lo && v <= hi); }
int rz12Violations(const Plants& pl, std::string* why = nullptr) {
  Gen r(12);
  int bad = 0;
  for (int i = 0; i < 30000; ++i) {
    Case c = makeCase(r);
    c.rq.plants = pl;
    if (i % 3 == 0) c.rq.locks = LockSet{};
    const Settings h = clampedHome(c.home);
    const Result res = randomize(c.cur, c.home, c.rq);
    const Settings& o = res.settings;
    std::string w;
    const bool homeShape = o.steps == h.steps && o.stepSize == h.stepSize && o.stepUnit == h.stepUnit;
    const bool curShape = o.steps == c.cur.steps && o.stepSize == c.cur.stepSize && o.stepUnit == c.cur.stepUnit;
    const std::int64_t bound = o.stepUnit == StepUnit::Semitones ? 24 : 14;
    // The span bound limits drawn values; at chaos 0 nothing is drawn and members return to their centre (SPEC 15.9), like any band.
    if (c.rq.chaosPm > 0 && o.steps >= 1 && o.steps <= 3 && !homeShape && !curShape && std::abs(static_cast<std::int64_t>(o.steps) * o.stepSize) > bound) w = "span";
    if (o.steps > 3 && o.stepSize != h.stepSize && o.stepSize != c.cur.stepSize) w = "a steps above 3 got a new size";
    if (c.cur.repeats == 0 && o.repeats != 0) w = "forever became a count";
    if (c.cur.repeats != 0 && o.repeats != 0 && o.repeats < c.cur.repeats) w = "repeats went down";
    if (o.rate != c.cur.rate) {
      // A drawn rate stays in home's family (band or home); must change moves from cur, so it stays in cur's family (band, cur or home).
      // Bands as member positions: straight 1/2..1/32 = 2..6; dotted 3/8..3/64 = 2..5; triplet 1/2T..1/32T = 2..6.
      int fh = 0, fc = 0, fo = 0;
      const int ph = famPos(h.rate, fh), pc = famPos(c.cur.rate, fc), po = famPos(o.rate, fo);
      auto inBand = [](int fam, int pos) { return pos >= 2 && pos <= (fam == 1 ? 5 : 6); };
      const bool viaHome = fo == fh && (po == ph || inBand(fo, po));
      const bool viaCur = fo == fc && (po == pc || inBand(fo, po) || (fh == fc && po == ph));
      if (!viaHome && !viaCur) w = "rate left its family or band";
    }
    if ((o.pattern == Pattern::User || o.pattern == Pattern::TriggerPulse || o.pattern == Pattern::BarTrigger) && o.pattern != h.pattern && o.pattern != c.cur.pattern) w = "drew into an unlit pattern";
    if (c.cur.quantize == Quantize::Off && o.stepUnit == StepUnit::Degrees && c.cur.stepUnit != StepUnit::Degrees && h.stepUnit != StepUnit::Degrees) w = "step degrees drawn with quantize off";
    if (c.cur.quantize == Quantize::Off && o.chordUnit == StepUnit::Degrees && c.cur.chordUnit != StepUnit::Degrees && h.chordUnit != StepUnit::Degrees) w = "chord degrees drawn with quantize off";
    if (!inBandOrHome(o.gate, 25, 100, h.gate, c.cur.gate)) w = "gate";
    if (!inBandOrHome(o.swing, 500, 667, h.swing, c.cur.swing)) w = "swing";
    if (!inBandOrHome(o.velFixed, 64, 120, h.velFixed, c.cur.velFixed)) w = "vel_fixed";
    if (!inBandOrHome(o.velRandom, 0, 40, h.velRandom, c.cur.velRandom)) w = "vel_random";
    if (!inBandOrHome(o.gateRandom, 0, 40, h.gateRandom, c.cur.gateRandom)) w = "gate_random";
    if (!inBandOrHome(o.stepTimeUs, 40000, 1000000, h.stepTimeUs, c.cur.stepTimeUs)) w = "step_time_us";
    if (!w.empty()) { ++bad; if (why && why->empty()) *why = w; }
  }
  return bad;
}
void testRZ12() {
  std::puts("RZ12: musical safety");
  Plants a; a.noSpanFilter = true;
  Plants b; b.repeatsDown = true;
  Plants c; c.lightTriggerPulse = true;
  plant(rz12Violations(a) > 0, "RZ12 detects a dropped span filter");
  plant(rz12Violations(b) > 0, "RZ12 detects repeats drawn below cur");
  plant(rz12Violations(c) > 0, "RZ12 detects trigger_pulse lit");
  Plants d; d.ownShapeRedraw = true;
  plant(rz12Violations(d) > 0, "RZ12 detects a new size drawn for a steps above 3 (an unbounded span)");
  std::string why;
  check(rz12Violations(Plants{}, &why) == 0, "RZ12: over 30,000 cases the span, a steps above 3 keeping its own size, repeats, rate family and band, unlit patterns, degrees with quantize off and every value band hold" + (why.empty() ? std::string() : " (first: " + why + ")"));
}

// ---------------------------------------------------------------------------------------------------- RZ13 allocation
std::int64_t rz13Allocs(const Plants& pl) {
  Gen r(13);
  std::vector<Case> cases;
  for (int i = 0; i < 2000; ++i) { cases.push_back(makeCase(r)); cases.back().rq.plants = pl; }
  AllocScope a;
  std::int64_t sink = 0;
  for (const Case& c : cases) sink += randomize(c.cur, c.home, c.rq).changed & 1u;
  (void)sink;
  return a.count();
}
void testRZ13() {
  std::puts("RZ13: no allocation");
  Plants a; a.allocScratch = true;
  plant(rz13Allocs(a) > 0, "RZ13 detects an allocation inside randomize");
  check(rz13Allocs(Plants{}) == 0, "RZ13: 2,000 calls allocate nothing");
}

// ---------------------------------------------------------------------------------------------------- RZ14 settings jumps in the core
Session rz14Session(const Plant& plants) {
  Session s;
  s.sr = 8000;
  s.total = 48000;
  s.plants = plants;
  s.base.enabled = true;
  s.base.chordEnabled = true;
  s.base.seed = 5;
  s.ins = {K(0, "c", 60), K(10, "e", 64), K(20, "g", 67), O(17000, "e"), K(23000, "e2", 65), O(36000, "c"), K(40000, "a", 69)};
  Settings cur = s.base;
  for (int k = 0; k < 30; ++k) {
    Request q; q.verb = Verb::Both; q.seed = 99; q.click = static_cast<std::uint32_t>(k); q.chaosPm = 1000; q.amountPm = 1000; q.locks = LockSet{};
    q.locks.set(P::RateMode);   // never drawn anyway; kept explicit
    cur = randomize(cur, cur, q).settings;
    const Settings next = cur;
    s.cfg.push_back(Change{1500 * (k + 1) + (k % 3), [next](Settings& x) { x = next; }});
  }
  return s;
}
void testRZ14() {
  std::puts("RZ14: settings jumps in the core (a Both result at chaos 1000 replaces the snapshot 30 times)");
  plant(!n1(runOnce(rz14Session({"dropRelease"}), 64)).empty(), "RZ14: the N1 plant (one release dropped) still goes red under the jump fuzz");
  const Session s = rz14Session({});
  const Run base = runOnce(s, 64);
  const std::vector<std::string> bad = n1(base);
  check(bad.empty() && !base.evs.empty(), "RZ14: N1 holds across 30 randomized snapshots (" + std::to_string(base.evs.size()) + " events" + (bad.empty() ? "" : ", first: " + bad[0]) + ")");
  check(base.core->counters().offsLost == 0, "RZ14: no NoteOff lost");
  bool same = true;
  for (int b : {1, 7, 512, 4096}) same = same && streamOf(runOnce(s, b)) == streamOf(base);
  check(same, "RZ14: G1, the stream is identical at block sizes 1, 7, 64, 512 and 4096");
  const ChordCauses cc = chordCauses(s, base);
  check(cc.err.empty() && cc.events > 0, "RZ14: CH1, every chord note starts and ends at its cause's sample (" + std::to_string(cc.events) + " chord events" + (cc.err.empty() ? "" : ", " + cc.err) + ")");
}

// ---------------------------------------------------------------------------------------------------- RZ16 the module never goes silent
int rz16Violations(const Plants& pl) {
  Gen r(16);
  int bad = 0;
  for (int i = 0; i < 20000; ++i) {
    Case c = makeCase(r);
    c.cur.enabled = true; c.cur.arpEnabled = false; c.cur.chordEnabled = true;
    if (r.chance(60)) c.home.chordEnabled = false;
    if (i % 4 == 0) c.rq.chaosPm = 0;
    c.rq.locks.set(P::ChordEnabled, false);
    c.rq.plants = pl;
    if (!randomize(c.cur, c.home, c.rq).settings.chordEnabled) ++bad;
  }
  return bad;
}
void testRZ16() {
  std::puts("RZ16: a click never silences the module (arp off, stage on)");
  Plants a; a.keepUnlitChordHome = true;
  plant(rz16Violations(a) > 0, "RZ16 detects the keep test returning home's off");
  check(rz16Violations(Plants{}) == 0, "RZ16: over 20,000 cases (every verb, chaos 0 included, home's stage off included), the stage stays on");
}

// ---------------------------------------------------------------------------------------------------- the API's small promises
void testApi() {
  std::puts("randomize API");
  int counts[3] = {0, 0, 0};
  for (int k = 0; k < kParamCount; ++k) ++counts[static_cast<int>(classOf(static_cast<P>(k)))];
  check(counts[0] == 37 && counts[1] == 8 && counts[2] == 5, "classOf: 37 structure, 8 value, 5 never drawn");
  bool agree = true;
  for (int k = 0; k < kParamCount; ++k) {
    const P p = static_cast<P>(k);
    const Class want = isNever(p) ? Class::Never : isValue(p) ? Class::Value : Class::Structure;
    agree = agree && classOf(p) == want;
  }
  check(agree, "classOf agrees with the literal class list");
  int exempt = 0;
  for (int k = 0; k < kParamCount; ++k) exempt += defaultLocks().test(static_cast<P>(k));
  check(exempt == 15 && defaultLocks().test(P::Rate) && defaultLocks().test(P::Quantize) && defaultLocks().test(P::ScaleDeg11), "defaultLocks: the 15 exempt keys (rate, quantize, the own scale)");
  check(std::string(paramName(P::ChordShift6)) == "chord_shift6" && std::string(paramName(P::Seed)) == "seed", "paramName follows params.json");
  Settings s;
  check(validSettings(s), "validSettings: the defaults are valid");
  s.gate = 0;
  check(!validSettings(s), "validSettings: gate 0 is invalid");
  Settings t;
  Request q; q.chaosPm = 5000; q.amountPm = 5000; q.seed = 1;
  Request q2 = q; q2.chaosPm = 1000; q2.amountPm = 1000;
  t.enabled = true;
  check(sameSettings(randomize(t, q).settings, randomize(t, q2).settings), "chaos and amount above 1000 are read as 1000");
}

}  // namespace

void dumpRandomize() {
  static const char* kClassNames[] = {"structure", "value", "never"};
  for (int k = 0; k < kParamCount; ++k)
    std::printf("rnd.%s=%s %d\n", paramName(static_cast<P>(k)), kClassNames[static_cast<int>(classOf(static_cast<P>(k)))], defaultLocks().test(static_cast<P>(k)) ? 1 : 0);
}

// The known answers for the lab twin (tools/lab_smoke.mjs) and their drift guard (tools/core_params_check.py): 300 cases of the shared corpus,
// settings as 50-int arrays in ParamId order (home RAW, so some values are out of range: randomize clamps a home before use).
void dumpRandomizeCases() {
  Gen r(42);
  auto arr = [](const Settings& s) {
    std::string o = "[";
    for (int k = 0; k < kParamCount; ++k) {
      if (k) o += ",";
      o += std::to_string(g(s, static_cast<P>(k)));
    }
    return o + "]";
  };
  std::vector<Case> cases;
  for (int i = 0; i < 300; ++i) cases.push_back(makeCase(r));
  // Targeted cases (the critic, round 2): random sampling never exercised every must-change move with both directions open, so a port
  // that keyed one move wrongly passed. Each must-change move once per seed: steps 1..3 with only steps unlocked; the RZ11 settings.
  auto targeted = [&](Settings s, LockSet unlockOnly, std::uint32_t seed) {
    Case c;
    c.cur = s;
    c.home = s;
    c.rq.verb = seed % 2 ? Verb::Structure : Verb::Both;
    c.rq.seed = seed;
    c.rq.click = seed * 3;
    c.rq.chaosPm = 1 + (seed * 97) % 1000;
    c.rq.amountPm = 0;
    c.rq.mustChange = true;
    c.rq.locks.bits = allStructureLocked().bits & ~unlockOnly.bits;
    cases.push_back(c);
  };
  Settings base;
  base.enabled = true;
  for (std::uint32_t k = 0; k < 20; ++k) {
    for (int st = 1; st <= 3; ++st) { LockSet u; u.set(P::Steps); Settings s = base; s.steps = st; s.stepSize = 5; targeted(s, u, k * 10 + static_cast<std::uint32_t>(st)); }
    { LockSet u; u.set(P::StepSize); Settings s = base; s.steps = 2; s.stepSize = 7; targeted(s, u, k * 10 + 4); }
    { LockSet u; u.set(P::StepUnit); Settings s = base; s.steps = 2; s.stepSize = 5; s.quantize = Quantize::Scale; targeted(s, u, k * 10 + 5); }
    { LockSet u; u.set(P::ScaleRoot); for (int d = 0; d < 12; ++d) u.set(deg(d)); Settings s = base; s.scaleSource = ScaleSource::Own; s.quantize = Quantize::Scale; targeted(s, u, k * 10 + 6); }
    { LockSet u; for (int q = 0; q < 6; ++q) u.set(chordOnId(q)); Settings s = base; s.chordEnabled = true; targeted(s, u, k * 10 + 7); }
    { LockSet u; u.set(P::ChordEnabled); Settings s = base; s.chordEnabled = (k % 2) == 0; targeted(s, u, k * 10 + 8); }
    { LockSet u; u.set(P::Rate); u.set(P::Repeats); Settings s = base; s.repeats = 3; s.rate = Rate::R1_8; targeted(s, u, k * 10 + 9); }
  }
  std::printf("{\"format\": \"arp-randomize-cases/1\", \"note\": \"GENERATED by arp_tests --dump-randomize-cases (SPEC 15 known answers for the lab twin: 300 random cases, then targeted must-change cases); tools/core_params_check.py checks it is current\", \"cases\": [\n");
  for (std::size_t i = 0; i < cases.size(); ++i) {
    const Case& c = cases[i];
    const Result res = randomize(c.cur, c.home, c.rq);
    std::printf("%s{\"verb\": %d, \"seed\": %u, \"click\": %u, \"corner\": %u, \"chaos\": %u, \"amount\": %u, \"mustChange\": %s, \"locks\": \"%016llx\", \"cur\": %s, \"home\": %s, \"out\": %s, \"changed\": \"%016llx\", \"nothingToDraw\": %s}",
                i ? ",\n" : "", static_cast<int>(c.rq.verb), c.rq.seed, c.rq.click, c.rq.corner, c.rq.chaosPm, c.rq.amountPm, c.rq.mustChange ? "true" : "false",
                static_cast<unsigned long long>(c.rq.locks.bits), arr(c.cur).c_str(), arr(c.home).c_str(), arr(res.settings).c_str(), static_cast<unsigned long long>(res.changed), res.nothingToDraw ? "true" : "false");
  }
  std::printf("\n]}\n");
}

void runRandomizeTests() {
  std::puts("== randomize (SPEC 15) ==");
  testApi();
  testRZ1();
  testRZ2();
  testRZ3();
  testRZ4();
  testRZ5();
  testRZ6();
  testRZ7();
  testRZ8();
  testRZ9();
  testRZ10();
  testRZ11();
  testRZ12();
  testRZ13();
  testRZ14();
  testRZ16();
}

}  // namespace arp_test
