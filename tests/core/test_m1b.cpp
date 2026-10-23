// test_m1b.cpp - the M1b checks (ROADMAP T-004): every order style, every step-pattern field, transposition in semitones,
// the user pattern, time mode with the smooth sweep. Same charter as test_core.cpp: each detector is preceded by its
// plant, a plant that does not fire is a FAIL, and "known answer" assertions (exact expected values) carry no plant.
// The sessions press keys WITHOUT note ids (Kk): every press of a key carries the same (port, channel, key) ref.
//
// The order-style tables are written INDEPENDENTLY of core.cpp, from the spec text (SPEC 4.2): each style's whole pass is
// built as an explicit list by loops, not by the closed-form index formulas the core uses.
#include "test_m1b.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "arp/bank_data.h"
#include "arp/draw.h"
#include "harness.h"

namespace arp_test {
namespace {

Change C(int at, std::function<void(Settings&)> f) { return Change{at, std::move(f)}; }

// ---- builders ----
struct B {   // a PatternStep builder
  PatternStep s;
  B& on(bool v) { s.on = v; return *this; }
  B& tie() { s.tie = true; return *this; }
  B& vel(int v) { s.vel = v; return *this; }
  B& gate(int v) { s.gate = v; return *this; }
  B& step(int v) { s.step = v; return *this; }
  B& prob(int v) { s.prob = v; return *this; }
  B& ratchet(int v) { s.ratchet = v; return *this; }
  B& trig(int v) { s.trig = v; return *this; }
  B& pick(int v) { s.pick = v; return *this; }
  operator PatternStep() const { return s; }
};
UserPattern userPat(std::vector<PatternStep> steps, int stepSize = 12) {
  UserPattern u;
  u.length = static_cast<int>(steps.size());
  u.stepSize = stepSize;
  for (std::size_t i = 0; i < steps.size() && i < 16; ++i) u.steps[i] = steps[i];
  return u;
}

// 120 bpm at 8 kHz: 1 beat = 4000 samples, 1/16 = 1000 samples = 3840 ticks.
constexpr std::int64_t kStepTicks = 3840;

// The notes of every grid step, keyed by the step's tick (sorted keys). Steps with no note are absent.
std::map<std::int64_t, std::vector<int>> byTick(const Run& r) {
  std::map<std::int64_t, std::vector<int>> m;
  for (const Rec& e : r.evs) if (e.t == EventType::NoteOn) m[e.tick].push_back(e.key);
  for (auto& kv : m) std::sort(kv.second.begin(), kv.second.end());
  return m;
}
struct StepPoint { std::int64_t kAbs; };
std::vector<StepPoint> stepPoints(const Run& r) { std::vector<StepPoint> v; for (const StepRec& s : r.steps) v.push_back({s.s.kAbs}); return v; }
std::string join(const std::vector<int>& v) { std::string s; for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]); return s; }
std::vector<int> keysOf(const Run& r, std::size_t n) { std::vector<int> v; for (const Rec& e : onsOf(r)) { if (v.size() < n) v.push_back(e.key); } return v; }

// ====================================================================== the order-style reference tables (from the spec text)
using Pass = std::vector<std::vector<int>>;   // one element per step of the style's pass: the keys that step plays
Pass referencePass(Order o, const std::vector<int>& E, const std::vector<int>& Pp) {
  const int e = static_cast<int>(E.size());
  Pass p;
  auto one = [&](int k) { p.push_back({k}); };
  switch (o) {
    case Order::Up: for (int i = 0; i < e; ++i) one(E[i]); break;
    case Order::Down: for (int i = e - 1; i >= 0; --i) one(E[i]); break;
    case Order::Bounce:   // up then back down WITHOUT repeating either end: 2e - 2 steps
      for (int i = 0; i < e; ++i) one(E[i]);
      for (int i = e - 2; i >= 1; --i) one(E[i]);
      break;
    case Order::Bounce2:  // up then down repeating both ends: 2e steps
      for (int i = 0; i < e; ++i) one(E[i]);
      for (int i = e - 1; i >= 0; --i) one(E[i]);
      break;
    case Order::Played: for (int k : Pp) one(k); break;
    case Order::Chord: p.push_back(E); break;
    case Order::Converge: case Order::Diverge: {
      std::vector<int> c;
      int lo = 0, hi = e - 1;
      bool takeLow = true;
      while (static_cast<int>(c.size()) < e) { c.push_back(takeLow ? E[lo++] : E[hi--]); takeLow = !takeLow; }
      if (o == Order::Diverge) std::reverse(c.begin(), c.end());
      for (int k : c) one(k);
      break;
    }
    case Order::PedalLow:
      if (e == 1) one(E[0]);
      else for (int i = 1; i < e; ++i) { one(E[0]); one(E[i]); }
      break;
    case Order::PedalHigh:
      if (e == 1) one(E[e - 1]);
      else for (int i = e - 2; i >= 0; --i) { one(E[e - 1]); one(E[i]); }
      break;
    case Order::ChordWalk: p.push_back(E); for (int i = 0; i < e; ++i) one(E[i]); break;
    case Order::Stride:
      one(E[0]);
      if (e == 1) one(E[0] + 12); else p.push_back(std::vector<int>(E.begin() + 1, E.end()));
      break;
    case Order::ChordBetween: for (int i = 0; i < e; ++i) { p.push_back(E); one(E[i]); } break;
    case Order::Pairs:
      if (e == 1) one(E[0]);
      else for (int i = 0; i + 1 < e; ++i) p.push_back({E[i], E[i + 1]});
      break;
    default: break;
  }
  for (auto& s : p) std::sort(s.begin(), s.end());
  return p;
}

// shuffle, replayed LITERALLY from the spec's chain: P_c = Fisher-Yates over E with draw(seed, c*256+i, 1, epoch), i = e-1..1;
// for c > 0 and e >= 2, if P_c[0] equals the last of the previous cycle's final order, swap P_c[0] and P_c[1].
std::vector<int> shuffleReference(const std::vector<int>& E, std::uint32_t seed, std::int64_t steps, std::uint32_t epoch = 0) {
  const int e = static_cast<int>(E.size());
  std::vector<int> out, prev;
  for (std::int64_t c = 0; static_cast<std::int64_t>(out.size()) < steps; ++c) {
    std::vector<int> P = E;
    for (int i = e - 1; i >= 1; --i) std::swap(P[i], P[arp::draw(seed, c * 256 + i, 1, epoch) % static_cast<std::uint32_t>(i + 1)]);
    if (c > 0 && e >= 2 && P[0] == prev[e - 1]) std::swap(P[0], P[1]);
    prev = P;
    out.insert(out.end(), P.begin(), P.end());
  }
  out.resize(static_cast<std::size_t>(steps));
  return out;
}

const std::array<int, 5> kPressOrder = {67, 60, 74, 64, 71};
std::vector<int> pressed(int e) { return std::vector<int>(kPressOrder.begin(), kPressOrder.begin() + e); }
std::vector<int> sortedKeys(int e) { auto v = pressed(e); std::sort(v.begin(), v.end()); return v; }

// Plays one style over e held keys (pressed at samples 100, 101, ..., so `played` has an order of its own), host clock,
// 1000 samples per step. The gesture anchors at kAbs 1: step n of the run sits at tick 3840 * (1 + n).
Run playStyle(Order o, int e, int nSteps, const Plant& plants, const std::function<void(Settings&)>& extra = {}, bool timeIt = false) {
  (void)timeIt;
  Session s;
  s.total = 1000 * (nSteps + 2);
  s.plants = plants;
  s.base.order = o;
  s.base.gate = 50;
  s.base.seed = 7;
  if (extra) extra(s.base);
  const auto pr = pressed(e);
  for (int i = 0; i < e; ++i) s.ins.push_back(Kk(100 + i, pr[static_cast<std::size_t>(i)]));
  return run(s, 4096);
}
std::vector<std::vector<int>> stepSets(const Run& r, int nSteps) {
  const auto m = byTick(r);
  std::vector<std::vector<int>> v;
  for (int n = 0; n < nSteps; ++n) { const auto it = m.find(kStepTicks * (1 + n)); v.push_back(it == m.end() ? std::vector<int>{} : it->second); }
  return v;
}

// All 16 styles x e = 1, 2, 3, 5: returns "style/e" for each mismatch.
std::vector<std::string> orderTableMisses(const Plant& plants, int nSteps = 0) {
  std::vector<std::string> bad;
  for (int oi = 0; oi < 16; ++oi) {
    const Order o = static_cast<Order>(oi);
    for (int e : {1, 2, 3, 5}) {
      const auto E = sortedKeys(e), Pp = pressed(e);
      int n = nSteps;
      std::vector<std::vector<int>> want;
      if (o == Order::Random) {
        n = nSteps ? nSteps : 40;
        for (int i = 0; i < n; ++i) want.push_back({E[arp::draw(7, 1 + i, 0, 0) % static_cast<std::uint32_t>(e)]});   // kAbs = 1 + i
      } else if (o == Order::Shuffle) {
        n = nSteps ? nSteps : 60;
        for (int k : shuffleReference(E, 7, n)) want.push_back({k});
      } else {
        const Pass p = referencePass(o, E, Pp);
        n = nSteps ? nSteps : static_cast<int>(3 * p.size()) + 2;
        for (int i = 0; i < n; ++i) want.push_back(p[static_cast<std::size_t>(i) % p.size()]);
      }
      if (stepSets(playStyle(o, e, n, plants), n) != want) bad.push_back(std::string(kOrderNames[oi]) + "/" + std::to_string(e));
    }
  }
  return bad;
}

void testOrderTables() {
  std::puts("Order styles: all 16 against tables written from the spec text (e = 1, 2, 3, 5)");
  const auto planted = orderTableMisses({"orderTables"});
  auto has = [&](const char* name) { for (const auto& b : planted) if (b.rfind(name, 0) == 0 && b[std::strlen(name)] == '/') return true; return false; };
  plant(has("bounce") && has("converge") && has("pedal_high") && has("chord_between"), "the order tables detect a bounce period, a converge parity, a pedal_high and a chord_between that are wrong (" + std::to_string(planted.size()) + " style/e cells red)");
  const auto bad = orderTableMisses({});
  check(bad.empty(), "all 16 order styles over e = 1, 2, 3, 5 match the independent tables (" + std::to_string(16 * 4) + " cells)" + (bad.empty() ? "" : " - wrong: " + bad[0]));
}

// N2: shuffle never repeats a key back to back and plays each key once per cycle
bool shuffleBad(const Plant& plants) {
  for (int e : {2, 3, 5})
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
      Session s;
      s.total = 150000;
      s.bpm = 999;
      s.plants = plants;
      s.base.order = Order::Shuffle;
      s.base.rate = Rate::R1_64;
      s.base.gate = 20;
      s.base.seed = seed;
      const auto keys = sortedKeys(e);
      for (int i = 0; i < e; ++i) s.ins.push_back(Kk(0, keys[static_cast<std::size_t>(i)]));
      const auto seq = keysOf(run(s, 4096), 100000);
      if (static_cast<int>(seq.size()) < 200 * e) return true;
      for (std::size_t i = 1; i < seq.size(); ++i) if (seq[i] == seq[i - 1]) return true;
      for (std::size_t c = 0; c + static_cast<std::size_t>(e) <= seq.size(); c += static_cast<std::size_t>(e)) {
        auto cyc = std::vector<int>(seq.begin() + static_cast<long>(c), seq.begin() + static_cast<long>(c) + e);
        std::sort(cyc.begin(), cyc.end());
        if (cyc != keys) return true;
      }
    }
  return false;
}
void testN2() {
  std::puts("N2: shuffle without back-to-back repeats");
  plant(shuffleBad({"shuffleBoundaryRepeat"}), "N2 detects a back-to-back repeat across a cycle boundary (the boundary swap skipped)");
  check(!shuffleBad({}), "N2: over >= 200 cycles x 6 seeds, shuffle (e = 2, 3, 5) never repeats a key back to back and plays each key once per cycle");
}

// ====================================================================== R1: only `order` picks advance m
void testR1() {
  std::puts("R1: rests, misses, ties, trigger-only steps and explicit picks do not advance m");
  // pattern: 0 order | 1 rest | 2 miss (prob 0) | 3 pick low | 4 trigger-only (rest + trig 2) | 5 order | 6 tie | 7 pick 2
  const UserPattern up = userPat({B(), B().on(false), B().prob(0), B().pick(kPickLow), B().on(false).trig(2), B(), B().tie(), B().pick(2)});
  auto run1 = [&](const Plant& pl) {
    Session s; s.total = 40000; s.plants = pl; s.userPattern = up; s.base.pattern = Pattern::User; s.base.gate = 50;
    s.ins = {Kk(0, 60), Kk(0, 64), Kk(0, 67)};
    return run(s, 64);
  };
  // model: the order stream advances only on steps 0 and 5; the picks play low (60) and the 2nd lowest (64)
  auto model = [&](int steps) {
    std::vector<std::pair<std::int64_t, int>> v;
    int m = 0;
    const int E[3] = {60, 64, 67};
    for (int k = 0; k < steps; ++k) {
      const int j = k % 8;
      if (j == 0 || j == 5) v.emplace_back(k * 1000, E[m++ % 3]);
      else if (j == 3) v.emplace_back(k * 1000, 60);
      else if (j == 7) v.emplace_back(k * 1000, 64);
    }
    return v;
  };
  auto bad = [&](const Plant& pl) {
    const Run r = run1(pl);
    std::vector<std::pair<std::int64_t, int>> got;
    for (const Rec& e : onsOf(r)) got.emplace_back(e.a, e.key);
    const auto want = model(36);
    return got.size() < want.size() || !std::equal(want.begin(), want.end(), got.begin());
  };
  plant(bad({"advanceOnRest"}), "R1 detects m advanced on a rest, a miss or a trigger-only step");
  plant(bad({"pickAdvancesM"}), "R1 detects m advanced on an explicit pick");
  check(!bad({}), "R1: the order stream 60 64 67 resumes where it left off across rests, a prob-0 miss, a trigger-only step, a tie and explicit picks (low, 2nd)");
  const Run r = run1({});
  int trigs = 0;
  for (const Rec& e : r.evs) trigs += e.t == EventType::Trigger && e.lane == 2;
  check(trigs >= 4 && n1(r).empty(), "R1: the trigger-only step fires lane 2 once per pass (" + std::to_string(trigs) + " seen) and N1 holds");
}

// ====================================================================== U1: the user pattern
void testU1() {
  std::puts("U1: the user pattern (SPEC 4.6, T17)");
  // (a) picks and rhythm, with the pattern set BEFORE prepare. Step j = (kAbs - 1) mod 8.
  const UserPattern U = userPat({B().pick(1), B().pick(3), B().pick(5), B().pick(kPickLow), B().pick(kPickHigh), B().pick(kPickChord), B().on(false), B()});
  auto sessA = [&](const Plant& pl) {
    Session s; s.total = 40 * 1000; s.plants = pl; s.userPattern = U; s.base.pattern = Pattern::User; s.base.gate = 50;
    s.ins = {Kk(100, 60), Kk(101, 64), Kk(102, 67)};
    return run(s, 64);
  };
  auto badA = [&](const Plant& pl) {
    const Run r = sessA(pl);
    const auto m = byTick(r);
    int up = 0;
    for (int k = 1; k < 38; ++k) {
      const int j = (k - 1) % 8;
      std::vector<int> want;
      if (j == 7) { const int E[3] = {60, 64, 67}; want = {E[up++ % 3]}; }
      else if (j == 6) want = {};
      else { const std::vector<std::vector<int>> t = {{60}, {67}, {64}, {60}, {67}, {60, 64, 67}}; want = t[static_cast<std::size_t>(j)]; }
      const auto it = m.find(kStepTicks * k);
      if ((it == m.end() ? std::vector<int>{} : it->second) != want) return true;
    }
    return !n1(r).empty();
  };
  plant(badA({"ignorePick"}), "U1 detects every pick read as `order`");
  plant(badA({"prepareResetsUser"}), "U1 detects a user pattern wiped by prepare() (set before prepare, SPEC 4.6)");
  check(!badA({}), "U1: picks 1, 3, 5 (cyclic), low, high, chord, a rest and an order step play in that rhythm, the pattern set BEFORE prepare");
  // (b) edits between blocks apply from the next evaluated step; of two writes at one sample the LATER wins
  auto badB = [&](const Plant& pl) {
    Session s; s.total = 14000; s.plants = pl; s.base.pattern = Pattern::User; s.base.gate = 50;
    s.ins = {Kk(100, 60), Kk(101, 64), Kk(102, 67)};
    auto all = [](int n, int pick) { std::vector<PatternStep> v; for (int i = 0; i < n; ++i) v.push_back(B().pick(pick)); return userPat(v); };
    s.userEdits = {{4300, all(4, kPickHigh)}, {8100, all(2, kPickHigh)}, {8100, all(2, 1)}};
    const auto m = byTick(run(s, 4096));
    const std::map<int, int> want = {{1, 60}, {2, 64}, {3, 67}, {4, 60}, {5, 67}, {6, 67}, {7, 67}, {8, 67}, {9, 60}, {10, 60}, {11, 60}, {12, 60}, {13, 60}};
    for (const auto& kv : want) { const auto it = m.find(kStepTicks * kv.first); if (it == m.end() || it->second != std::vector<int>{kv.second}) return true; }
    return false;
  };
  plant(badB({"userFirstWins"}), "U1 detects two writes between steps resolved to the FIRST");
  check(!badB({}), "U1: setUserPattern between blocks applies from the next step; of two writes at one sample the later wins");
  // (c) an edit landing in the MIDDLE of a block must not take effect until the next block: A = vel 50 %, B = vel 150 %
  auto sessC = [&](const Plant& pl) {
    Session s; s.total = 12000; s.plants = pl; s.maxBlock = 4096; s.base.pattern = Pattern::User; s.base.gate = 50;
    s.userPattern = userPat({B().vel(50)});
    s.ins = {Kk(0, 60)};
    int evals = 0;
    s.midBlockHook = [&evals](Core& c) { if (++evals == 2) c.setUserPattern(userPat({B().vel(150)})); };   // lands before the 2nd step is evaluated
    return run(s, 4096);
  };
  auto badC = [&](const Plant& pl) {
    const Run r = sessC(pl);
    for (const Rec& e : onsOf(r)) {
      if (e.a < 4096 && e.vel != 50) return true;     // the first block is whole: every step still reads A
      if (e.a >= 4096 && e.vel != 100 + 27) return true;   // 150 % of 100, clamped to 127, from the next block on
    }
    return onsOf(r).size() < 10 || !n1(r).empty();
  };
  plant(badC({"swapMidStep"}), "U1 detects an edit that lands mid-block (a write into the reader's slot)");
  check(!badC({}), "U1/T17: an edit landing before the 2nd step of a block leaves that block whole and applies from the next one; N1 holds");
  // (d) T17: a note already tied keeps its scheduled off when the pattern is replaced
  {
    Session s; s.total = 12000; s.base.pattern = Pattern::User; s.base.gate = 80;
    s.userPattern = userPat({B(), B().tie(), B(), B()});
    s.userEdits = {{1500, userPat({B(), B(), B(), B()})}};
    s.ins = {Kk(0, 60)};
    const Run r = run(s, 64);
    std::int64_t offTick = -1;
    const Rec first = onsOf(r)[0];
    for (const Rec& e : r.evs) if (e.t == EventType::NoteOff && e.ref == first.ref) { offTick = e.tick; break; }
    check(offTick == 6912 && r.core->counters().rows[17] > 0 && n1(r).empty(), "known answer (T17): the pattern replaced while a tied note sounds does not move its off (6912 ticks at gate 80); N1 holds");
  }
  // (e) validation and getUserPattern
  {
    auto c = makeCore({}, {});
    UserPattern in = userPat({B().vel(999).gate(0).step(9).prob(-4).ratchet(9).trig(12).pick(99), B().pick(7)});
    in.length = 99; in.stepSize = -99; in.stepUnit = static_cast<StepUnit>(9);
    in.steps[1].on = false;
    c->setUserPattern(in);
    const UserPattern g = c->getUserPattern();
    const bool ok = g.length == 16 && g.stepSize == -48 && g.stepUnit == StepUnit::Semitones && g.steps[0].vel == 200 && g.steps[0].gate == 1 && g.steps[0].step == 8 &&
                    g.steps[0].prob == 0 && g.steps[0].ratchet == 4 && g.steps[0].trig == 8 && g.steps[0].pick == kPickOrder && g.steps[1].pick == 7 && !g.steps[1].on;
    check(ok, "known answer: setUserPattern clamps length 1-16, step size, every field, reads an unknown pick / unit as its default; getUserPattern returns the stored copy");
    UserPattern odd = userPat({B().pick(12), B().pick(-1), B().pick(11), B().pick(0)});
    odd.length = 0;
    c->setUserPattern(odd);
    check(c->getUserPattern().length == 1, "known answer: a user pattern length of 0 clamps to 1");
    odd.length = 4;
    c->setUserPattern(odd);
    const UserPattern go = c->getUserPattern();
    check(go.steps[0].pick == kPickOrder && go.steps[1].pick == kPickOrder && go.steps[2].pick == kPickChord && go.steps[3].pick == kPickOrder, "known answer: picks 12 and -1 are read as order; 11 (chord) and 0 stay");
    UserPattern two = userPat({B().pick(3)});
    c->setUserPattern(two);
    c->setUserPattern(userPat({B().pick(4), B().pick(5)}));
    check(c->getUserPattern().length == 2 && c->getUserPattern().steps[0].pick == 4, "known answer: getUserPattern returns the writer's LATEST copy even before the audio thread has taken it");
    auto d = makeCore({}, {});
    const UserPattern def = d->getUserPattern();
    check(def.length == 8 && def.stepSize == 12 && def.steps[7].on && def.steps[0].pick == kPickOrder, "known answer: the default user pattern is 8 plain steps with pick = order");
  }
  // (f) the storage survives reset() and prepare()
  {
    auto c = makeCore({}, {});
    c->setUserPattern(userPat({B().pick(2), B().pick(3)}));
    c->prepare(8000, 64);
    c->reset();
    c->prepare(48000, 256);
    check(c->getUserPattern().length == 2, "known answer: the user pattern survives reset() and prepare()");
  }
  // (g) a real second thread: see testUserPatternThreads()
}

// Two user patterns that differ in EVERY field and in length (both semitone units: the degree unit is M2):
//   A: 1 step  -> key + 12, vel 50 %, gate 40 %, 1 note, trigger 3          (size 12)
//   B: 3 steps -> the lowest key - 7, vel 150 %, gate 120 %, 4 ratchets, trigger 5, then two tie steps (gate 77 %, trigger 8, prob 10)  (size 7)
UserPattern patternA() {
  return userPat({B().on(true).vel(50).gate(40).step(1).prob(100).ratchet(1).trig(3).pick(kPickOrder)}, 12);
}
UserPattern patternB() {
  return userPat({B().on(true).vel(150).gate(120).step(-1).prob(90).ratchet(4).trig(5).pick(kPickLow),
                  B().on(false).tie().vel(0).gate(77).step(2).prob(10).ratchet(2).trig(8).pick(kPickChord),
                  B().on(false).tie().vel(0).gate(77).step(2).prob(10).ratchet(2).trig(8).pick(kPickChord)}, 7);
}

struct BlockLog { std::vector<Event> ev; };
// 'A' or 'B' for one event, '?' for anything that belongs to neither pattern (a torn step)
char classOf(const Event& e) {
  if (e.type == EventType::NoteOn) return e.key == 72 && e.vel == 50 ? 'A' : e.key == 53 && e.vel == 127 ? 'B' : '?';
  if (e.type == EventType::Trigger) return e.lane == 3 ? 'A' : (e.lane == 5 || e.lane == 8) ? 'B' : '?';
  return 0;
}
// Runs `blocks` blocks of 4096 samples on a stopped transport with one key held. Returns the number of blocks whose events are
// not wholly one pattern, and the number of notes whose length is neither pattern's.
std::pair<int, int> runWholeBlocks(Core& c, int blocks) {
  int torn = 0, badLen = 0;
  std::map<std::int64_t, std::pair<int, std::int64_t>> sounding;   // ref id -> (key, on tick)
  std::int64_t evaluated = 0;   // steps evaluated before this block: a NoteOn with stepSeq >= this belongs to a step of THIS block
  for (int blk = 0; blk < blocks; ++blk) {
    Event k; k.type = EventType::NoteOn; k.ref = NoteRef{0, 0, 60, 0}; k.key = 60; k.vel = 100;
    Block b; b.n = 4096; b.settings.enabled = true; b.settings.gate = 50; b.settings.pattern = Pattern::User; b.transport.playing = false;
    if (blk == 0) b.in = std::span<const Event>(&k, 1);
    std::set<char> classes;
    std::vector<Event> evs;
    bool unknown = false;
    for (const Event& e : c.process(b)) {
      evs.push_back(e);
      const char cl = classOf(e);
      if (cl == '?') unknown = true;
      // a ratchet sub-note frozen at an EARLIER step's evaluation may be emitted in this block (SPEC 5.1): it belongs to
      // that step's pattern, so only the steps evaluated in THIS block decide which pattern the block read
      if (cl && !(e.type == EventType::NoteOn && e.stepSeq < evaluated)) classes.insert(cl);
    }
    evaluated += static_cast<std::int64_t>(c.lastSteps().size());
    if (classes.size() > 1 || unknown) ++torn;
    for (const Event& e : evs) {
      if (e.type == EventType::NoteOn) sounding[e.ref.id] = {e.key, e.tick};
      if (e.type == EventType::NoteOff) {
        const auto it = sounding.find(e.ref.id);
        if (it == sounding.end()) continue;
        const std::int64_t len = e.tick - it->second.second, onTick = it->second.second;
        const bool okA = it->second.first == 72 && len == 768;
        const bool okB = it->second.first == 53 && (onTick % 3840 == 2880 ? len == 6278 : len == 576);
        if (!okA && !okB) ++badLen;
        sounding.erase(it);
      }
    }
  }
  return {torn, badLen};
}

void testUserPatternThreads() {
  std::puts("U1 (threads): the triple buffer hands over whole patterns");
  // the deterministic form: another thread's write lands before the 2nd step of the first block
  auto hooked = [&](const Plant& pl) {
    auto c = makeCore({}, pl);
    c->setUserPattern(patternA());
    c->prepare(8000, 4096);
    int evals = 0;
#ifdef ARP_PLANTS
    c->plants.midBlockHook = [&] { if (++evals == 2) c->setUserPattern(patternB()); };
#endif
    return runWholeBlocks(*c, 3);
  };
  const auto planted = hooked({"swapMidStep"});
  plant(planted.first > 0, "U1 detects a write that lands mid-block: " + std::to_string(planted.first) + " of 3 blocks mixed the two patterns");
  const auto clean = hooked({});
  check(clean.first == 0 && clean.second == 0, "U1: a write landing before the 2nd step of a block leaves that block wholly pattern A and the next one wholly B (note lengths exact)");
  // the real form: a second thread rewrites the two patterns as fast as it can while the audio thread runs
  auto c = makeCore({}, {});
  c->setUserPattern(patternA());
  c->prepare(8000, 4096);
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    const UserPattern a = patternA(), b = patternB();
    for (int i = 0; !stop.load(std::memory_order_relaxed); ++i) c->setUserPattern(i % 2 ? a : b);
  });
  const auto real = runWholeBlocks(*c, 800);
  stop = true;
  writer.join();
  check(real.first == 0 && real.second == 0, "U1 (real thread): 800 blocks while another thread rewrote two patterns that differ in every field and in length: every block is wholly one pattern, every note length is one pattern's");
}

// ====================================================================== T1, S1, K1, P1
void testT1() {
  std::puts("T1: triggers fire once per firing step, at its first on");
  // 0: ratchet 4 + trig 3 | 1: trigger-only (rest) trig 5 | 2: ratchet 2 + trig 2 | 3: plain
  const UserPattern up = userPat({B().ratchet(4).trig(3), B().on(false).trig(5), B().ratchet(2).trig(2), B()});
  auto sess = [&](const Plant& pl, int gate) {
    Session s; s.total = 24000; s.plants = pl; s.userPattern = up; s.base.pattern = Pattern::User; s.base.gate = gate;
    s.ins = {Kk(0, 60)};
    return run(s, 64);
  };
  auto bad = [&](const Plant& pl) {
    const Run r = sess(pl, 80);
    std::map<int, int> perLane;
    for (const Rec& e : r.evs) if (e.t == EventType::Trigger) perLane[e.lane]++;
    // steps 0..23: lane 3 on steps 0,4,..; lane 5 on 1,5,..; lane 2 on 2,6,..: six passes
    return perLane[3] != 6 || perLane[5] != 6 || perLane[2] != 6 || perLane.size() != 3;
  };
  plant(bad({"trigEveryRatchet"}), "T1 detects a trigger fired on every ratchet");
  check(!bad({}), "T1: lane 3 (4 ratchets), lane 5 (trigger-only) and lane 2 (2 ratchets) each fire exactly once per pass");
  const Run r = sess({}, 80);
  // a trigger sits at its step's first on (same sample), after the ons of that sample (SPEC 1.1)
  bool at = true, order = true;
  for (std::size_t i = 0; i < r.evs.size(); ++i) {
    const Rec& e = r.evs[i];
    if (e.t == EventType::Trigger && e.lane == 3) {
      at = at && e.a % 4000 == 0;
      order = order && i > 0 && r.evs[i - 1].t == EventType::NoteOn && r.evs[i - 1].a == e.a;
    }
  }
  std::vector<std::int64_t> ratch;
  for (const Rec& e : onsOf(r)) if (e.tick < kStepTicks) ratch.push_back(e.tick);
  check(at && order && ratch == std::vector<std::int64_t>{0, 960, 1920, 2880}, "known answer: the lane-3 trigger fires at the first on, after the ons of that sample; 4 ratchets start at ticks 0 960 1920 2880");
  // ratchet lengths: floor first, then gate, capped at the sub-step (SPEC 5.2)
  auto lens = [&](int gate) {
    const Run q = sess({}, gate);
    std::vector<std::int64_t> l;
    for (const Rec& o : onsOf(q)) {
      if (o.tick >= 3840) break;
      for (const Rec& e : q.evs) if (e.t == EventType::NoteOff && e.ref == o.ref) { l.push_back(e.tick - o.tick); break; }
    }
    return l;
  };
  const auto l80 = lens(80), l200 = lens(200);
  check(l80 == std::vector<std::int64_t>(4, 768) && l200 == std::vector<std::int64_t>(4, 960), "known answer: a 4-way ratchet at gate 80 lasts 768 ticks per sub-note (floor(3840/4) * 80 %), at gate 200 it is capped at the sub-step, 960");
}


Session timeSession(int sr, int total, int stepUs);

void testStepFields() {
  std::puts("Step fields: ratchet floor-first, tie gate and trigger, velocity rounding, P1 against the draw");
  { // ratchet 3 at 1/8T (5120 ticks): sub-steps 1706.67 do not divide, so FLOOR first, then the gate: floor(5120/3) = 1706, 1706 * 80 % = 1364
    Session s; s.total = 12000; s.userPattern = userPat({B().ratchet(3)}); s.base.pattern = Pattern::User; s.base.rate = Rate::R1_8T; s.base.gate = 80; s.ins = {Kk(0, 60)};
    const Run r = run(s, 64);
    std::vector<std::int64_t> starts, lens;
    for (const Rec& o : onsOf(r)) {
      if (o.tick >= 5120) break;
      starts.push_back(o.tick);
      for (const Rec& e : r.evs) if (e.t == EventType::NoteOff && e.ref == o.ref) { lens.push_back(e.tick - o.tick); break; }
    }
    check(starts == std::vector<std::int64_t>({0, 1706, 3413}) && lens == std::vector<std::int64_t>(3, 1364), "known answer: ratchet 3 over a 5120-tick step starts at 0 1706 3413 and each sub-note lasts 1364 (floor first, then gate)");
  }
  { // a tie step: the playing note is extended by the TIED step's own gate, and the tie step still fires its trigger
    Session s; s.total = 9000; s.userPattern = userPat({B(), B().tie().gate(50).trig(4)}); s.base.pattern = Pattern::User; s.base.gate = 80; s.ins = {Kk(0, 60)};
    const Run r = run(s, 64);
    const Rec first = onsOf(r)[0];
    std::int64_t len = -1;
    for (const Rec& e : r.evs) if (e.t == EventType::NoteOff && e.ref == first.ref) { len = e.tick - first.tick; break; }
    bool trig = false;
    for (const Rec& e : r.evs) trig = trig || (e.t == EventType::Trigger && e.lane == 4 && e.a == 1000);
    check(len == 3840 + 1536 && trig, "known answer: [note, tie gate 50 trig 4] at gate 80: the note lasts 3840 + floor(3840 * 80 % * 50 %) = 5376 ticks and the tie step fires lane 4 at its own position");
  }
  { // SPEC 4.4: base * vel % + 50, floored: 77 * 50 % = 38.5 rounds UP to 39; a 0 % step clamps to 1; 200 % of 100 clamps to 127
    auto velOf = [](int inVel, int stepVel) { Session s; s.total = 1500; s.userPattern = userPat({B().vel(stepVel)}); s.base.pattern = Pattern::User; s.ins = {Kk(0, 60, inVel)}; return onsOf(run(s, 64))[0].vel; };
    check(velOf(77, 50) == 39 && velOf(100, 0) == 1 && velOf(100, 200) == 127 && velOf(100, 130) == 127 && velOf(60, 130) == 78, "known answer: velocity = floor((base * step vel % + 50) / 100) clamped to 1..127 (77 at 50 % -> 39, 60 at 130 % -> 78)");
  }
  { // T16 under transposition: the shifted key is dropped (counted) and m still advances
    Session s; s.total = 12000; s.base.steps = 1; s.base.stepSize = 1; s.base.gate = 50; s.ins = {Kk(0, 127)};
    const Run r = run(s, 64);
    std::vector<std::int64_t> at;
    for (const Rec& e : onsOf(r)) { at.push_back(e.a); }
    bool alt = at.size() >= 5;
    for (std::size_t i = 0; i < at.size(); ++i) alt = alt && at[i] == static_cast<std::int64_t>(i) * 2000;   // pass 0 plays 127, pass 1 would be 128: dropped
    check(alt && r.core->counters().keysDropped >= 5, "known answer: key 127 with 1 transposition step of +1: every second step is dropped at output (128 is outside 0-127, T16) and counted");
    Session s2; s2.total = 8000; s2.base.gate = 50; s2.ins = {Kk(0, 127), Kk(0, 128)};
    check(keysOf(run(s2, 64), 4) == std::vector<int>({127, 127, 127, 127}), "known answer: an input key 128 stays in E (the period is 2) but is dropped at output: 127 plays every second step");
  }
  { // P1: which steps fire is the draw on lane 2, mod 100, against prob: reference from arp::draw (itself pinned by D1's known answers)
    Session s; s.total = 60000; s.bpm = 999; s.userPattern = userPat({B().prob(50)}); s.base.pattern = Pattern::User; s.base.rate = Rate::R1_64; s.base.gate = 20; s.base.seed = 9; s.ins = {Kk(0, 60)};
    const Run r = run(s, 4096);
    std::set<std::int64_t> played;
    for (const Rec& e : onsOf(r)) played.insert(e.tick / 960);
    bool ok = r.steps.size() > 400;
    for (const StepPoint& sp : stepPoints(r)) ok = ok && ((arp::draw(9, sp.kAbs, 2, 0) % 100 < 50) == (played.count(sp.kAbs) > 0));
    check(ok, "known answer: a prob-50 step fires exactly when draw(seed, kAbs, lane 2, epoch) mod 100 < 50, for " + std::to_string(r.steps.size()) + " steps");
  }
  { // TM2 at a rate where a sample is NOT a whole microsecond: changeTick is the block-start position ROUNDED UP (13b.8), then converted to samples with cdiv
    constexpr int SR = 44100, S0 = 100001;
    Session s = timeSession(SR, 110000, 2000000);
    s.base.gate = 10; s.cfg = {C(S0, [](Settings& x) { x.stepTimeUs = 10000; })}; s.ins = {Kk(0, 60)};
    const auto on = onsOf(run(s, 4096));
    const std::int64_t changeUs = (static_cast<std::int64_t>(S0) * 1000000 + SR - 1) / SR;          // ceil(sample in microseconds)
    const std::int64_t want = (changeUs * SR + 999999) / 1000000;                                      // cdiv back to samples
    check(on.size() > 4 && on[1].a == 88200 && on[2].a == want && on[2].tick == changeUs && on[3].tick == changeUs + 10000, "known answer: a 2 s -> 10 ms change at sample 100001 (44.1 kHz) puts the next step at tick ceil(change in us) = " + std::to_string(changeUs) + ", sample " + std::to_string(want));
  }
}

void testS1() {
  std::puts("S1: every note Arp emits is tagged `arp`");
  auto bad = [&](const Plant& pl) {
    Session s; s.total = 30000; s.plants = pl; s.base.order = Order::ChordWalk; s.base.gate = 50;
    s.ins = {Kk(0, 60), Kk(0, 64), Kk(0, 67)};
    const Run r = run(s, 64);
    int n = 0;
    for (const Rec& e : r.evs) if (e.t == EventType::NoteOn) { ++n; if (e.stream != Stream::Arp) return true; }
    return n < 20;
  };
  plant(bad({"untaggedOn"}), "S1 detects a NoteOn that lost its `arp` stream tag");
  check(!bad({}), "S1: every NoteOn (steps, chord steps) carries stream = arp");
}

void testK1() {
  std::puts("K1: ties engage at gate 80 by look-ahead");
  auto durs = [&](const Plant& pl, Pattern pat) {
    Session s; s.total = 40000; s.plants = pl; s.base.pattern = pat; s.base.gate = 80; s.ins = {Kk(0, 60)};
    const Run r = run(s, 64);
    std::vector<std::pair<std::int64_t, std::int64_t>> d;
    for (const Rec& o : onsOf(r)) for (const Rec& e : r.evs) if (e.t == EventType::NoteOff && e.ref == o.ref) { d.emplace_back(o.tick, e.tick - o.tick); break; }
    return d;
  };
  const std::vector<std::pair<std::int64_t, std::int64_t>> want = {{0, 6912}, {7680, 6912}, {15360, 3072}, {19200, 10752}};   // off = on(k+t) + floor(span * gate * steps[j+t].gate / 10000)
  auto bad = [&](const Plant& pl) { const auto d = durs(pl, Pattern::TiedLegato); return d.size() < 5 || !std::equal(want.begin(), want.end(), d.begin()) || d[4].first != 30720; };
  plant(bad({"tieAtTieStep"}), "K1 detects ties evaluated at the tie step (tied_legato collapses to plain notes at gate 80)");
  const auto plain = durs({}, Pattern::None);
  bool plainOk = !plain.empty();
  for (const auto& d : plain) plainOk = plainOk && d.second == 3072;
  check(!bad({}) && plainOk, "K1: tied_legato at gate 80 holds notes 6912 / 6912 / 3072 / 10752 ticks and the tie steps emit nothing; `none` stays 3072");
}

void testP1() {
  std::puts("P1: probability");
  auto count = [&](const Plant& pl, int prob) {
    Session s; s.total = 1230000; s.bpm = 999; s.plants = pl; s.userPattern = userPat({B().prob(prob)}); s.base.pattern = Pattern::User; s.base.rate = Rate::R1_64; s.base.gate = 20; s.base.seed = 3;
    s.ins = {Kk(0, 60)};
    const Run r = run(s, 4096);
    return std::make_pair(onsOf(r).size(), r.steps.size());
  };
  plant(count({"probLE"}, 0).first > 0, "P1 detects `<=` (prob 0 fires on the 1 % of draws that are exactly 0)");
  const auto p0 = count({}, 0), p100 = count({}, 100), p30 = count({}, 30), p50 = count({}, 50);
  auto within = [](const std::pair<std::size_t, std::size_t>& c, double target) { const double f = static_cast<double>(c.first) / static_cast<double>(c.second); return c.second >= 10000 && std::fabs(f - target) <= 0.02; };
  check(p0.first == 0 && p100.first == p100.second && p100.second >= 10000 && within(p30, 0.30) && within(p50, 0.50),
        "P1: prob 0 never and 100 always fire (exact, " + std::to_string(p100.second) + " steps); 30 and 50 land within 2 % over 10,000+ steps (" + std::to_string(p30.first * 1000 / p30.second) + " and " + std::to_string(p50.first * 1000 / p50.second) + " per mille)");
}

// ====================================================================== X1 / ST1
void testX1() {
  std::puts("X1: transposition steps in semitones; ST1: step x the PATTERN's step size");
  auto upKeys = [&](const Plant& pl, const std::function<void(Settings&)>& f, std::size_t n, const UserPattern* up = nullptr, std::vector<int> keys = {60, 64, 67}) {
    Session s; s.total = 40000; s.plants = pl; s.base.gate = 50; f(s.base);
    if (up) { s.userPattern = *up; s.base.pattern = Pattern::User; }
    for (int k : keys) s.ins.push_back(Kk(0, k));
    return join(keysOf(run(s, 64), n));
  };
  auto badA = [&](const Plant& pl) { return upKeys(pl, [](Settings& x) { x.steps = 2; x.stepSize = 7; }, 15) != "60,64,67,67,71,74,74,78,81,60,64,67,67,71,74"; };
  plant(badA({"transposeMerged"}), "X1 detects the passes merged into one range (r2.6 octaves)");
  check(!badA({}), "X1(a): up over C E G, 2 steps of +7 semitones: C E G | G B D | D F# A | C E G");
  auto badB = [&](const Plant& pl) { return upKeys(pl, [](Settings& x) { x.steps = 0; x.stepSize = 7; }, 12) != "60,64,67,60,64,67,60,64,67,60,64,67"; };
  plant(badB({"stepsZeroShifts"}), "X1 detects a shift with steps = 0");
  check(!badB({}), "X1(b): steps = 0 never transposes (step_size +7 ignored)");
  const UserPattern pk2 = userPat({B(), B().pick(kPickLow)});
  auto badD = [&](const Plant& pl) { return upKeys(pl, [](Settings& x) { x.steps = 1; x.stepSize = 12; }, 14, &pk2) != "60,60,64,60,67,72,72,72,76,72,79,60,60,60"; };
  plant(badD({"transposeMerged"}), "X1 detects a picked note that does not follow its pass");
  check(!badD({}), "X1(d): a `low` pick takes the pass current at its step (t from m): 60 60 64 60 67 | 72 72 72 76 72 79 | 60 60 60");
  // (e) every deterministic style: pass t is the base cycle shifted by t * 7, 3 passes then back to 0 (e = 3, 5)
  std::vector<std::string> bad;
  for (int oi = 0; oi < 16; ++oi) {
    const Order o = static_cast<Order>(oi);
    if (o == Order::Random || o == Order::Shuffle) continue;
    for (int e : {3, 5}) {
      const auto E = sortedKeys(e);
      const Pass cyc = referencePass(o, E, pressed(e));
      std::vector<std::vector<int>> want;
      for (int loop = 0; loop < 2; ++loop) for (int t = 0; t < 3; ++t) for (auto st : cyc) { for (int& k : st) k += 7 * t; want.push_back(st); }
      const int n = static_cast<int>(want.size());
      if (stepSets(playStyle(o, e, n, {}, [](Settings& x) { x.steps = 2; x.stepSize = 7; }), n) != want) bad.push_back(std::string(kOrderNames[oi]) + "/" + std::to_string(e));
    }
  }
  check(bad.empty(), "X1(e): all 14 deterministic styles (e = 3, 5): each pass is the base cycle shifted by t * 7 semitones, 3 passes then back to 0" + (bad.empty() ? "" : " - wrong: " + bad[0]));
  // ST1: octave_leap at the bank's +12; a global step_size that differs must not matter
  auto leap = [&](const Plant& pl, std::vector<int> keys) {
    Session s; s.total = 30000; s.plants = pl; s.base.pattern = Pattern::OctaveLeap; s.base.gate = 40; s.base.stepSize = 5;
    for (int k : keys) s.ins.push_back(Kk(0, k));
    return join(keysOf(run(s, 64), 8));
  };
  auto badST = [&](const Plant& pl) {
    if (leap(pl, {60}) != "60,72,60,48,60,72,60,48" || leap(pl, {60, 64}) != "60,76,60,52,60,76,60,52") return true;
    const UserPattern p7 = userPat({B().step(2), B(), B().step(-1)}, 7);   // +7 semitones per step: +14, 0, -7
    return upKeys(pl, [](Settings& x) { x.stepSize = 3; }, 6, &p7, {60}) != "74,60,53,74,60,53";
  };
  plant(badST({"stepSizeIgnored"}), "ST1 detects every step size read as 12 semitones");
  plant(badST({"stepOffsetReadsGlobal"}), "ST1 detects the step offset reading the GLOBAL step_size instead of the pattern's");
  check(!badST({}), "ST1: octave_leap is the old octave (+-12), a user pattern with step_size +7 shifts by step x 7 (74 60 53), whatever the global step_size is");
}

// ====================================================================== time mode
Session timeSession(int sr, int total, int stepUs) {
  Session s; s.sr = sr; s.total = total; s.base.rateMode = RateMode::Time; s.base.stepTimeUs = stepUs; s.base.gate = 30;
  return s;
}

void testTM1() {
  std::puts("TM1: time mode, step k sounds at cdiv(k * step_time_us * sr, 1e6) after the key press");
  constexpr int U = 125000, PRESS = 1234, NSTEP = 8;
  auto sess = [&](const Plant& pl, int sr, int block, double bpm, std::int64_t* t6) {
    Session s = timeSession(sr, static_cast<int>(sr * 1.4), U);
    s.plants = pl;
    auto h = std::make_shared<SimHost>(sr, bpm, 0, true);
    h->set(static_cast<int>(sr * 0.2), std::nullopt, 7.0); h->set(static_cast<int>(sr * 0.35), false); h->set(static_cast<int>(sr * 0.5), true, 0.3, 77.0);   // the host seeks, stops, restarts: all ignored
    s.host = h;
    s.ins = {Kk(PRESS * sr / 8000, 60)};
    Run r = run(s, block);
    if (t6) *t6 = r.core->counters().rows[6];
    return r;
  };
  auto exact = [&](const Run& r, int sr) {
    const auto on = onsOf(r);
    if (on.size() < static_cast<std::size_t>(NSTEP)) return false;
    for (int i = 0; i < NSTEP; ++i) {
      const std::int64_t want = static_cast<std::int64_t>(PRESS) * sr / 8000 + (static_cast<std::int64_t>(i) * U * sr + 999999) / 1000000;
      if (on[static_cast<std::size_t>(i)].a != want || on[static_cast<std::size_t>(i)].tick != static_cast<std::int64_t>(i) * U) return false;   // swing term 0 at 500
    }
    return true;
  };
  auto badTM = [&](const Plant& pl) { for (double bpm : {60.0, 120.0, 174.0}) if (!exact(sess(pl, 8000, 512, bpm, nullptr), 8000)) return true; return false; };
  plant(badTM({"timeUsesTempo"}), "TM1 detects the step derived from the host tempo (60 and 174 bpm)");
  plant(badTM({"timeAnchorsTransport"}), "TM1 detects the grid anchored at the transport instead of the key press");
  std::string ref;
  bool bsOk = true;
  for (int block : {4096, 512, 64, 7, 1}) { const std::string st = streamOf(sess({}, 8000, block, 174, nullptr)); if (ref.empty()) ref = st; else bsOk = bsOk && st == ref; }
  bool srOk = true;
  std::int64_t t6 = -1;
  for (int sr : {44100, 48000, 96000, 192000}) { const Run r = sess({}, sr, 4096, 174, &t6); srOk = srOk && exact(r, sr) && n1(r).empty() && t6 == 0; }
  check(!badTM({}) && bsOk && srOk, "TM1: step k sounds at cdiv(k * 125000 us * sr, 1e6) after the press at 60 / 120 / 174 bpm through host seeks and a stop / start (ignored: no T6), identical at block sizes 4096/512/64/7/1, and at 44.1 / 48 / 96 / 192 kHz the ticks are the same microseconds");
}

void testTM2() {
  std::puts("TM2: a step_time_us change mid-gesture is a piecewise grid (SPEC 2.6)");
  auto timeRun = [&](const Plant& pl, int stepUs, std::vector<Change> cfg, int total) {
    Session s = timeSession(8000, total, stepUs);
    s.plants = pl; s.base.gate = 20; s.cfg = std::move(cfg);
    s.ins = {Kk(0, 60)};
    return run(s, 64);
  };
  auto samples = [](const Run& r) { std::vector<std::int64_t> v; for (const Rec& e : onsOf(r)) v.push_back(e.a); return v; };
  // (a) 125 -> 250 ms at sample 3500 (steps at 0 1000 2000 3000 so far): the next step is exactly 250 ms after the last (5000), then every 2000
  auto badA = [&](const Plant& pl) { const auto a = samples(timeRun(pl, 125000, {C(3500, [](Settings& x) { x.stepTimeUs = 250000; })}, 10000)); return a.size() < 7 || !std::equal(a.begin(), a.begin() + 7, std::vector<std::int64_t>{0, 1000, 2000, 3000, 5000, 7000, 9000}.begin()); };
  // (b) 2 s -> 10 ms one second after a step (step at 16000, change at 24000): the next step fires AT the change point, then every 80 samples; no burst
  auto badB = [&](const Plant& pl) {
    const auto a = samples(timeRun(pl, 2000000, {C(24000, [](Settings& x) { x.stepTimeUs = 10000; })}, 25000));
    const auto it = std::find(a.begin(), a.end(), 24000);
    if (it == a.end() || std::vector<std::int64_t>(a.begin(), it) != std::vector<std::int64_t>{0, 16000}) return true;
    for (auto q = it; q != a.end(); ++q) if (*q != 24000 + 80 * (q - it)) return true;
    return false;
  };
  // (c) a slow sweep up and back down: no gap longer than max(L_old, L_new), no burst, N1 holds
  std::vector<Change> sweep;
  std::vector<std::pair<int, int>> sweepAt;
  { int us = 125000; bool up = true; for (int at = 1300; at < 30000; at += 1300) { us += up ? 25000 : -25000; if (us >= 250000) up = false; if (us <= 125000) up = true; sweepAt.emplace_back(at, us); sweep.push_back(C(at, [us](Settings& x) { x.stepTimeUs = us; })); } }
  auto badC = [&](const Plant& pl) {
    const Run r = timeRun(pl, 125000, sweep, 32000);
    const auto a = samples(r);
    auto usAt = [&](std::int64_t x) { int v = 125000; for (const auto& c : sweepAt) if (c.first <= x) v = c.second; return v; };
    for (std::size_t i = 1; i < a.size(); ++i) {
      std::vector<int> vals = {usAt(a[i - 1]), usAt(a[i])};
      for (const auto& c : sweepAt) if (c.first > a[i - 1] && c.first <= a[i]) vals.push_back(c.second);
      const std::int64_t gap = a[i] - a[i - 1];
      const std::int64_t hi = (*std::max_element(vals.begin(), vals.end()) * 8000ll + 999999) / 1000000 + 1, lo = *std::min_element(vals.begin(), vals.end()) * 8000ll / 1000000 - 1;
      if (gap > hi || gap < lo) return true;
    }
    return a.size() < 20 || !n1(r).empty();
  };
  for (const char* pl : {"regridAbsolute", "regridKAbs"}) plant(badA({pl}) && badB({pl}), std::string("TM2 detects ") + pl + " (the old absolute re-grid / kAbs * L_new): (a) and (b) both go red");
  plant(badC({"regridKAbs"}), "TM2 detects a sweep that bursts or gaps under tick = kAbs * L_new");
  check(!badA({}) && !badB({}) && !badC({}), "TM2: 125 -> 250 ms gives exactly one 250 ms gap (0 1000 2000 3000 5000 7000 9000); 2 s -> 10 ms fires at the change point then every 10 ms with no burst; a slow sweep never gaps past max(L_old, L_new) and never bursts; N1 holds");
}

void testTM3() {
  std::puts("TM3: restart is inert in time mode; entering time mode with keys held; the inactive setting does nothing");
  const std::vector<int> keys = {60, 64, 67};
  auto seq = [&](const Plant& pl, Restart rs) {
    Session s = timeSession(8000, 20000, 125000);
    s.plants = pl; s.base.restart = rs; s.base.gate = 30;
    for (int k : keys) s.ins.push_back(Kk(1234, k));
    for (int k : keys) s.ins.push_back(Ok(6500, k));
    for (int k : keys) s.ins.push_back(Kk(9000, k));
    return run(s, 128);
  };
  auto line = [](const Run& r) { std::string t; for (const Rec& e : onsOf(r)) t += std::to_string(e.a) + ":" + std::to_string(e.key) + " "; return t; };
  const std::string refLine = line(seq({}, Restart::Chord));
  auto badTM3 = [&](const Plant& pl) {
    if (line(seq(pl, Restart::Bar)) != refLine || line(seq(pl, Restart::Free)) != refLine) return true;
    const auto o = onsOf(seq(pl, Restart::Bar));
    std::vector<int> g1, g2;
    for (const Rec& e : o) (e.a < 9000 ? g1 : g2).push_back(e.key);
    return g1.size() < 3 || g2.size() < 3 || join(std::vector<int>(g1.begin(), g1.begin() + 3)) != "60,64,67" || join(std::vector<int>(g2.begin(), g2.begin() + 3)) != "60,64,67";
  };
  plant(badTM3({"timeBarsCount"}), "TM3 detects bars of 61,440 us counted in time mode");
  check(!badTM3({}) && refLine.size() > 40, "TM3: in time mode restart bar and free play exactly the chord stream (the pattern anchors at each gesture start: 60 64 67 from the top both times)");
  auto enter = [&](const Plant& pl) {
    Session s; s.total = 14000; s.plants = pl; s.base.stepTimeUs = 125000; s.base.gate = 30;
    s.cfg = {C(5001, [](Settings& x) { x.rateMode = RateMode::Time; })};
    s.ins = {Kk(100, 60)};
    return run(s, 128);
  };
  auto badEnter = [&](const Plant& pl) {
    const Run r = enter(pl);
    std::vector<std::int64_t> t;
    for (const Rec& e : onsOf(r)) if (e.a >= 5001) t.push_back(e.a);
    if (t.size() < 5) return true;
    for (std::size_t i = 0; i < t.size(); ++i) if (t[i] != 5001 + (static_cast<std::int64_t>(i) * 125000 * 8000 + 999999) / 1000000) return true;
    return !n1(r).empty() || r.core->counters().rows[19] == 0;
  };
  plant(badEnter({"timeEntryNoStart"}), "TM3 detects a time clock that does not start when time mode is entered with keys held");
  check(!badEnter({}), "TM3: entering time mode with a key held starts the time clock AT that offset (5001, 6001, ...), release-all first (T19); N1 holds");
  auto inact = [&](const Plant& pl, RateMode mode, std::vector<Change> cfg) {
    Session s; s.total = 14000; s.plants = pl; s.base.rateMode = mode; s.base.stepTimeUs = 125000; s.base.rate = Rate::R1_16; s.base.gate = 30; s.cfg = std::move(cfg);
    for (int k : keys) s.ins.push_back(Kk(100, k));
    return streamOf(run(s, 128));
  };
  auto badInact = [&](const Plant& pl) {
    return inact(pl, RateMode::Sync, {C(3500, [](Settings& x) { x.stepTimeUs = 200000; })}) != inact(pl, RateMode::Sync, {}) ||
           inact(pl, RateMode::Time, {C(3500, [](Settings& x) { x.rate = Rate::R1_8; })}) != inact(pl, RateMode::Time, {});
  };
  plant(badInact({"inactiveReanchors"}), "TM3 detects a change of the inactive timing setting that restarts the order");
  check(!badInact({}), "TM3: step_time_us is ignored in sync mode and rate in time mode: changing the inactive one leaves the stream byte-identical");
}


// ====================================================================== the bank's order vs the Pattern enum
void testBankOrder() {
  std::puts("The generated bank is indexed by the Pattern enum");
  std::array<const char*, kBankCount> swapped{};
  for (int i = 0; i < kBankCount; ++i) swapped[static_cast<std::size_t>(i)] = kBankIds[i];
  std::swap(swapped[1], swapped[2]);
  const int user = static_cast<int>(Pattern::User);
  plant(!bankOrderMatchesPatternEnum(swapped.data(), kBankCount, kPatternNames, user), "the bank-order check detects two patterns swapped in the generated bank (offbeat / gallop)");
  plant(!bankOrderMatchesPatternEnum(kBankIds, kBankCount, kPatternNames, user + 1), "the bank-order check detects a bank one pattern shorter or longer than the enum before `user`");
  check(bankOrderMatchesPatternEnum(kBankIds, kBankCount, kPatternNames, user) && kBankCount == user,
        "kBankIds[i] == kPatternNames[i] for all " + std::to_string(kBankCount) + " bank patterns and kBankCount == (int)Pattern::User (also a static_assert in core.cpp)");
}

// ====================================================================== L5: `played` ties, and the epoch in every draw
void testPlayedTie() {
  std::puts("`played`: two keys pressed on the same sample");
  auto seq = [&](const Plant& pl) {
    Session s; s.total = 8000; s.plants = pl; s.base.order = Order::Played; s.base.gate = 50;
    s.ins = {Kk(100, 67), Kk(100, 60), Kk(101, 64)};   // 67 arrives before 60 on the SAME sample; 64 a sample later
    return join(keysOf(run(s, 64), 6));
  };
  plant(seq({"playedTieByKeyDesc"}) != "60,67,64,60,67,64", "`played` detects a same-sample tie resolved to the higher key first: " + seq({"playedTieByKeyDesc"}));
  check(seq({}) == "60,67,64,60,67,64", "known answer: `played` orders keys by press sample and breaks a same-sample tie by key (SPEC 4.2): 67 and 60 on one sample play 60, 67, then the later 64");
}

void testEpochInDraws() {
  std::puts("The free-clock epoch is in every draw (random, shuffle, prob)");
  const std::vector<int> E = {60, 64, 67};
  // Two gestures on a STOPPED transport: epoch 1 for the first free-clock start, 2 for the second. Step k of each gesture is kAbs k.
  auto good = [&](const Plant& pl, const char* what) {
    const std::string w = what;
    Session s; s.plants = pl; s.host = std::make_shared<SimHost>(8000, 120, 0, false); s.base.gate = 20; s.base.seed = 5;
    int steps;
    if (w == "prob") { s.base.pattern = Pattern::User; s.userPattern = userPat({B().prob(50)}); s.base.rate = Rate::R1_64; steps = 60; }
    else { s.base.order = w == "random" ? Order::Random : Order::Shuffle; steps = 8; }
    const int len = (w == "prob" ? 250 : 1000) * steps, g2 = len + 5000;
    for (int k : E) { s.ins.push_back(Kk(100, k)); s.ins.push_back(Ok(100 + len, k)); s.ins.push_back(Kk(g2, k)); s.ins.push_back(Ok(g2 + len, k)); }
    s.total = g2 + len + 3000;
    std::uint32_t ep1 = 0, ep2 = 0;
    s.probe = [&](int pos, int, const Core& c) { if (pos < g2 - 100) ep1 = c.epoch(); ep2 = c.epoch(); };
    const Run r = run(s, 64);
    if (ep1 != 1 || ep2 != 2) return false;
    auto expect = [&](std::uint32_t epoch) {
      std::vector<int> v;
      if (w == "random") for (int k = 0; k < steps; ++k) v.push_back(E[arp::draw(5, k, 0, epoch) % 3]);
      else if (w == "shuffle") v = shuffleReference(E, 5, steps, epoch);
      else for (int k = 0; k < steps; ++k) v.push_back(arp::draw(5, k, 2, epoch) % 100 < 50 ? 1 : 0);   // 1 = the step fires
      return v;
    };
    auto got = [&](bool second) {
      std::vector<int> v;
      if (w == "prob") { v.assign(static_cast<std::size_t>(steps), 0); for (const Rec& e : onsOf(r)) if ((e.a >= g2) == second) v[static_cast<std::size_t>(e.tick / 960)] = 1; }
      else for (const Rec& e : onsOf(r)) if ((e.a >= g2) == second) v.push_back(e.key);
      return v;
    };
    return expect(1) != expect(2) && got(false) == expect(1) && got(true) == expect(2);   // the two epochs must give DIFFERENT sequences, else the check is blind
  };
  for (const char* w : {"random", "shuffle", "prob"}) plant(!good({"drawIgnoresEpoch"}, w), std::string("the epoch check detects a draw that ignores the epoch (") + w + ")");
  check(good({}, "random") && good({}, "shuffle") && good({}, "prob"), "known answer: with the transport stopped, the 1st gesture draws with epoch 1 and the 2nd with epoch 2, so random, shuffle and prob each play a DIFFERENT sequence the second time, equal to the reference from arp::draw");
}

// ====================================================================== time mode stops evaluating steps at key release (SPEC 2.5, 2.6)
void testTimeModeRelease() {
  std::puts("Time mode: no grid step (trigger-only steps included) once H is empty, even while a note rings");
  struct Out { std::vector<std::int64_t> trigs; std::vector<std::int64_t> noteOffs; std::vector<std::int64_t> ons; bool n1ok; };
  // press at 0, release at `rel`; pattern in `up`; time mode 125 ms = 1000 samples per step (or sync at 1/16 on a stopped transport)
  auto go = [&](const Plant& pl, bool time, UserPattern up, int gateGlobal, int rel) {
    Session s;
    s.total = 9000; s.plants = pl; s.userPattern = up; s.base.pattern = Pattern::User; s.base.gate = gateGlobal;
    if (time) { s.base.rateMode = RateMode::Time; s.base.stepTimeUs = 125000; }
    else s.host = std::make_shared<SimHost>(8000, 120, 0, false);
    s.ins = {Kk(0, 60), Ok(rel, 60)};
    const Run r = run(s, 64);
    Out o;
    for (const Rec& e : r.evs) {
      if (e.t == EventType::Trigger) o.trigs.push_back(e.a);
      if (e.t == EventType::NoteOn) o.ons.push_back(e.a);
      if (e.t == EventType::NoteOff && e.a < s.total) o.noteOffs.push_back(e.a);
    }
    o.n1ok = n1(r).empty();
    return o;
  };
  // A: [note (gate 200 %), rest + trigger 3]; global gate 200 -> the note lasts 4000 samples; released at 1500
  const UserPattern restTrig = userPat({B().gate(200), B().on(false).trig(3)});
  // B: [note, tie + trigger 3, tie + trigger 3]; the note is held through both ties (off at 2000 + 800 = 2800); released at 500
  const UserPattern tied = userPat({B(), B().tie().trig(3), B().tie().trig(3)});
  using V = std::vector<std::int64_t>;
  auto badA = [&](const Plant& pl) { const Out o = go(pl, true, restTrig, 200, 1500); return o.trigs != V{1000} || o.noteOffs != V{4000} || o.ons != V{0} || !o.n1ok; };
  auto badB = [&](const Plant& pl) { const Out o = go(pl, true, tied, 80, 500); return !o.trigs.empty() || o.noteOffs != V{2800} || o.ons != V{0} || !o.n1ok; };
  plant(badA({"timeStepsWhileRinging"}), "time mode detects a trigger-only step still firing after the key was released while the note rings");
  plant(badB({"timeStepsWhileRinging"}), "time mode detects tie steps' triggers still firing after the release while the tied note rings");
  check(!badA({}), "known answer: time mode, [note gate 200 %, rest + trigger 3], key released at 1500: the trigger fires once (1000, key held) and NOT at 3000 while the note rings; the note's off stays at 4000; N1 holds");
  check(!badB({}), "known answer: time mode, [note, tie + trigger, tie + trigger], key released at 500: no trigger after the release; the tied note still ends at 2800");
  // sync mode is unchanged: the stopped free clock keeps stepping while a note rings (SPEC 2.4's idle: H empty AND nothing sounding AND nothing pending)
  const Out syncA = go({}, false, restTrig, 200, 1500);
  check(syncA.trigs == V({1000, 3000}) && syncA.noteOffs == V{4000} && syncA.n1ok, "known answer: SYNC mode on a stopped transport, the same pattern and release: the trigger-only step at 3000 still fires while the note rings (only time mode changed)");
}

// ====================================================================== LOW-A: ids across prepare()
void testOwedIds() {
  std::puts("LOW-A: the id counter starts above the owed ids after prepare()");
  auto collide = [&](const Plant& pl) {
    auto c = makeCore({}, pl);
    c->prepare(8000, 64);
    std::set<std::int64_t> owed;
    for (int b = 0; b < 20; ++b) {
      Event k; k.type = EventType::NoteOn; k.key = 60; k.vel = 100; k.ref = NoteRef{0, 0, 60, 0};
      Block blk; blk.n = 64; blk.settings.enabled = true; blk.settings.gate = 200; blk.transport.playing = false;
      if (b == 0) blk.in = std::span<const Event>(&k, 1);
      for (const Event& e : c->process(blk)) if (e.type == EventType::NoteOn) owed.insert(e.ref.id);
    }
    c->prepare(48000, 256);   // the notes are still sounding: their offs are owed
    Event k; k.type = EventType::NoteOn; k.key = 64; k.vel = 100; k.ref = NoteRef{0, 0, 64, 1};
    Block blk; blk.n = 256; blk.settings.enabled = true; blk.settings.gate = 200; blk.transport.playing = false;
    blk.in = std::span<const Event>(&k, 1);
    std::set<std::int64_t> fresh;
    for (const Event& e : c->process(blk)) if (e.type == EventType::NoteOn) fresh.insert(e.ref.id);
    bool hit = fresh.empty() || owed.empty();
    for (std::int64_t id : fresh) hit = hit || owed.count(id) > 0;
    return hit;
  };
  plant(collide({"owedIdCollision"}), "LOW-A detects a new note that shares a ref id with a note still owed an off");
  check(!collide({}), "LOW-A: after prepare() with notes still owed offs, new notes get ids above every owed id");
}

// ====================================================================== sessions for N1 / G1 / A1, and a seeded fuzz
// A deterministic generator (std::mt19937 is specified by the standard, so a seed means the same session everywhere).
Session fuzzSession(std::uint32_t seed, bool keyRefs) {
  std::mt19937 g(seed);
  auto rnd = [&](int n) { return static_cast<int>(g() % static_cast<std::uint32_t>(n)); };
  Session s;
  s.sr = 8000;
  s.total = 20000 + rnd(15000);
  s.keyRefs = keyRefs;
  s.base.gate = 1 + rnd(200);
  s.base.rate = static_cast<Rate>(rnd(22));
  s.base.order = static_cast<Order>(rnd(16));
  s.base.pattern = static_cast<Pattern>(rnd(12));
  s.base.seed = g();
  s.base.steps = rnd(3);
  s.base.stepSize = rnd(25) - 12;
  s.base.stepTimeUs = 10000 + rnd(300000);
  auto h = std::make_shared<SimHost>(8000, 60 + rnd(200), (rnd(8)) * 0.25 - 1.0, rnd(2) == 1);
  for (int i = rnd(4); i > 0; --i) { const int at = 1 + rnd(s.total - 1); const int kind = rnd(3); if (kind == 0) h->set(at, !h->segAt(at).playing); else if (kind == 1) h->set(at, std::nullopt, rnd(60) / 4.0 - 2); else h->set(at, std::nullopt, std::nullopt, 20.0 + rnd(900)); }
  s.host = h;
  for (int i = rnd(7); i > 0; --i) {
    const int at = 1 + rnd(s.total - 1), what = rnd(11), v = static_cast<int>(g() % 1000);
    s.cfg.push_back(C(at, [what, v](Settings& x) {
      switch (what) {
        case 0: x.latch = static_cast<Latch>(v % 3); break;
        case 1: x.enabled = v % 3 != 0; break;
        case 2: x.rate = static_cast<Rate>(v % 22); break;
        case 3: x.gate = 1 + v % 200; break;
        case 4: x.arpEnabled = v % 3 != 0; break;
        case 5: x.order = static_cast<Order>(v % 16); break;
        case 6: x.pattern = static_cast<Pattern>(v % 12); break;
        case 7: x.steps = v % 4; x.stepSize = v % 25 - 12; break;
        case 8: x.rateMode = v % 2 ? RateMode::Time : RateMode::Sync; break;
        case 9: x.stepTimeUs = 10000 + (v * 997) % 400000; break;
        default: x.velMode = v % 2 ? VelMode::Fixed : VelMode::Played; x.seed = static_cast<std::uint32_t>(v); break;
      }
    }));
  }
  auto randomUser = [&] {
    std::vector<PatternStep> v;
    for (int i = 1 + rnd(16); i > 0; --i) {
      B b;
      b.on(rnd(5) != 0).vel(rnd(201)).gate(1 + rnd(200)).step(rnd(17) - 8).prob(rnd(4) == 0 ? rnd(101) : 100).ratchet(1 + rnd(4)).trig(rnd(4) == 0 ? 1 + rnd(8) : 0).pick(rnd(14) - 1);
      if (rnd(5) == 0) b.tie();
      v.push_back(b);
    }
    return userPat(v, rnd(25) - 12);
  };
  s.userPattern = randomUser();
  for (int i = rnd(3); i > 0; --i) s.userEdits.emplace_back(1 + rnd(s.total - 1), randomUser());
  static const std::vector<std::string> labels = [] { std::vector<std::string> l; for (int i = 0; i < 64; ++i) l.push_back("f" + std::to_string(i)); return l; }();
  int li = 0;
  for (int i = rnd(14); i > 0; --i) {
    const int at = rnd(s.total), r = rnd(10);
    if (r < 6) { const char* L = labels[static_cast<std::size_t>(li++ % 64)].c_str(); s.ins.push_back(K(at, L, 48 + rnd(14), 1 + rnd(127), rnd(2))); if (rnd(3)) { const int ch = s.ins.back().ev.ref.channel; s.ins.push_back(O(std::min(s.total - 1, at + rnd(6000)), L, ch)); } }
    else if (r < 8) s.ins.push_back(Ped(at, rnd(2) == 1));
    else if (r < 9) s.ins.push_back(Pan(at));
    else s.ins.push_back(Other(at, rnd(1000)));
  }
  return s;
}

void testFuzz() {
  std::puts("Seeded random sessions: every feature at once, N1 at every block size, G1 across them");
  int n1bad = 0, g1bad = 0, lost = 0;
  std::string first;
  std::int64_t events = 0;
  constexpr int kSessions = 30;
  for (std::uint32_t i = 0; i < kSessions; ++i) {
    const Session s = fuzzSession(1000 + i, i % 2 == 1);
    std::string ref;
    bool g1 = false, bad = false;
    for (int block : {4096, 333, 64, 1}) {
      const Run r = run(s, block);
      events += static_cast<std::int64_t>(r.evs.size());
      const auto v = n1(r);
      if (!v.empty() && !bad) { bad = true; ++n1bad; if (first.empty()) first = "session " + std::to_string(i) + " block " + std::to_string(block) + ": " + v[0]; }
      lost += static_cast<int>(r.core->counters().offsLost);
      const std::string st = streamOf(r);
      if (ref.empty()) ref = st; else if (st != ref && !g1) { g1 = true; ++g1bad; if (first.empty()) first = "session " + std::to_string(i) + " G1 differs at block " + std::to_string(block); }
    }
  }
  check(n1bad == 0 && g1bad == 0 && lost == 0 && events > 2000, std::to_string(kSessions) + " seeded sessions (all orders, patterns, user edits, ratchets, ties, triggers, transposition, time mode, latch, pedal, panic, tempo and seeks; id-ful and id-less refs): N1 violations " + std::to_string(n1bad) + ", G1 divergences " + std::to_string(g1bad) + ", lost offs " + std::to_string(lost) + " (" + std::to_string(events) + " events)" + (first.empty() ? "" : " - " + first));
}

}  // namespace

// The N1 sessions every feature must pass through (test_core.cpp's kSessions appends these).
Session m1bSession(const char* name) {
  const std::string n = name;
  Session s;
  if (n == "styles") {   // every order style, one after another, over three keys; transposition steps; id-less refs only through keyRefs
    s.total = 36000; s.base.rate = Rate::R1_32; s.base.gate = 90; s.base.seed = 5; s.base.steps = 1; s.base.stepSize = 5;
    for (int i = 1; i < 16; ++i) s.cfg.push_back(C(i * 2200, [i](Settings& x) { x.order = static_cast<Order>(i); }));
    s.cfg.push_back(C(33500, [](Settings& x) { x.steps = 0; }));
    s.ins = {K(0, "a", 60), K(0, "b", 64), K(0, "c", 67), K(7000, "d", 72), O(15000, "b"), Pan(27000), K(27500, "e", 62), K(27500, "f", 69)};
  } else if (n == "patterns") {   // every bank pattern, then the user pattern with every field, edits between blocks
    s.total = 48000; s.base.rate = Rate::R1_32; s.base.gate = 85; s.base.seed = 11; s.base.order = Order::Bounce;
    for (int i = 1; i < kBankCount; ++i) s.cfg.push_back(C(i * 3000, [i](Settings& x) { x.pattern = static_cast<Pattern>(i); }));
    s.cfg.push_back(C(34000, [](Settings& x) { x.pattern = Pattern::User; x.steps = 1; x.stepSize = 7; }));
    s.userPattern = userPat({B().ratchet(3).trig(1), B().tie(), B().pick(kPickChord).vel(150), B().on(false).trig(4), B().prob(50).step(2), B().pick(3).gate(30), B().tie(), B().tie()}, 5);
    s.userEdits = {{40000, userPat({B().pick(kPickHigh).ratchet(4), B().tie(), B().step(-1), B()}, -7)}};
    s.ins = {K(0, "a", 60), K(0, "b", 64), K(0, "c", 67), K(0, "d", 71), O(30000, "c"), K(31000, "e", 55)};
  } else if (n == "timemode") {   // time mode with a sweep, a mode switch each way, restarts, host edges that must be ignored
    s.total = 44000; s.base.rateMode = RateMode::Time; s.base.stepTimeUs = 90000; s.base.gate = 70; s.base.order = Order::Up;
    auto h = std::make_shared<SimHost>(8000, 120, 0, true);
    h->set(9000, std::nullopt, 5.0); h->set(15000, false); h->set(21000, true, 0.0, 90.0);
    s.host = h;
    s.cfg = {C(6000, [](Settings& x) { x.stepTimeUs = 250000; }), C(13000, [](Settings& x) { x.stepTimeUs = 20000; }), C(19000, [](Settings& x) { x.rateMode = RateMode::Sync; }),
             C(26000, [](Settings& x) { x.rateMode = RateMode::Time; x.stepTimeUs = 150000; x.restart = Restart::Bar; }), C(32000, [](Settings& x) { x.stepTimeUs = 400000; }), C(38000, [](Settings& x) { x.stepTimeUs = 60000; })};
    s.ins = {K(100, "a", 60), K(100, "b", 64), O(17000, "a"), K(17500, "c", 67), O(24000, "b"), O(24000, "c"), K(29000, "d", 62), K(34000, "e", 71)};
  } else {
    std::abort();
  }
  return s;
}

// G1 and A1 over the three M1b sessions, id-ful and id-less
void testSessionsG1A1() {
  std::puts("G1 / A1 over the M1b sessions (every order style, every pattern field, time mode)");
  for (const char* name : {"styles", "patterns", "timemode"}) {
    for (bool keyRefs : {false, true}) {
      Session s = m1bSession(name);
      s.keyRefs = keyRefs;
      std::string ref;
      bool same = true;
      std::int64_t events = 0;
      for (int block : {4096, 512, 64, 7, 1}) {
        const Run r = run(s, block);
        const std::string st = streamOf(r);
        if (ref.empty()) { ref = st; events = static_cast<std::int64_t>(r.evs.size()); } else same = same && st == ref;
      }
      check(same && events > 80, std::string("G1: session '") + name + "'" + (keyRefs ? " (id-less refs)" : "") + " is identical at block sizes 4096/512/64/7/1 (" + std::to_string(events) + " events)");
    }
    Session a = m1bSession(name);
    a.countAllocs = true;
    a.resets = {a.total / 2};
    const Run r = run(a, 64);
    check(r.allocs == 0, std::string("A1: session '") + name + "' makes " + std::to_string(r.allocs) + " operator-new calls inside process() / reset() (settings changes, user-pattern adoption, time-mode switches)");
  }
}

void runM1bTests() {
  testTimeModeRelease();
  testBankOrder();
  testPlayedTie();
  testEpochInDraws();
  testUserPatternThreads();
  testStepFields();
  testOwedIds();
  testOrderTables();
  testN2();
  testR1();
  testU1();
  testT1();
  testS1();
  testK1();
  testP1();
  testX1();
  testTM1();
  testTM2();
  testTM3();
  testSessionsG1A1();
  testFuzz();
}

}  // namespace arp_test
