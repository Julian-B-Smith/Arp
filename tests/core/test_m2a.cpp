// test_m2a.cpp - the M2a checks (ROADMAP T-005): scale quantize (SPEC 6: Q1-Q5), the degree unit and the six-stage offset order
// (SPEC 4.7: X1, ST1), swing (SPEC 5.1: G3, TM1's swing term), velocity (SPEC 4.4), and how a change of these settings takes
// effect (SPEC 9). Same charter as test_core.cpp / test_m1b.cpp: each detector is preceded by its plant, a plant that does not
// fire is a FAIL, and "known answer" assertions (exact expected values) carry no plant.
//
// The expected values are NOT computed with the core's own formulas: the quantize rule is re-derived by a brute-force oracle
// (collect every in-scale key, pick by distance then tie rule), the degree moves by an index walk over the list of in-scale
// keys, and swing / ratchet / tie ticks are written out as numbers from SPEC 5.1 / 5.2 (120 bpm at 8 kHz: a 1/16 step is 3840
// ticks = 1000 samples, so a swing of 750 permille delays an odd step by 1920 ticks = 500 samples).
#include "test_m2a.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "harness.h"

namespace arp_test {
namespace {

Change C(int at, std::function<void(Settings&)> f) { return Change{at, std::move(f)}; }

struct B {   // a PatternStep builder
  PatternStep s;
  B& on(bool v) { s.on = v; return *this; }
  B& tie() { s.tie = true; return *this; }
  B& vel(int v) { s.vel = v; return *this; }
  B& gate(int v) { s.gate = v; return *this; }
  B& step(int v) { s.step = v; return *this; }
  B& ratchet(int v) { s.ratchet = v; return *this; }
  B& trig(int v) { s.trig = v; return *this; }
  B& pick(int v) { s.pick = v; return *this; }
  operator PatternStep() const { return s; }
};
UserPattern userPat(std::vector<PatternStep> steps, int stepSize = 12, StepUnit unit = StepUnit::Semitones) {
  UserPattern u;
  u.length = static_cast<int>(steps.size());
  u.stepSize = stepSize;
  u.stepUnit = unit;
  for (std::size_t i = 0; i < steps.size() && i < 16; ++i) u.steps[i] = steps[i];
  return u;
}

constexpr unsigned kCMajor = 0xAB5;   // root-relative degrees 0 2 4 5 7 9 11
constexpr unsigned kMinPent = 1193;   // 0 3 5 7 10
constexpr std::int64_t kStepTicks = 3840;   // 120 bpm at 8 kHz: 1/16 = 1000 samples = 3840 ticks

std::string join(const std::vector<int>& v) { std::string s; for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]); return s; }
std::vector<int> keysOf(const Run& r, std::size_t n = 1000000) { std::vector<int> v; for (const Rec& e : onsOf(r)) if (v.size() < n) v.push_back(e.key); return v; }
std::vector<int> keysFrom(const Run& r, std::int64_t from, std::size_t n) { std::vector<int> v; for (const Rec& e : onsOf(r)) if (e.a >= from && v.size() < n) v.push_back(e.key); return v; }
std::vector<std::int64_t> ticksOf(const Run& r, std::size_t n = 1000000) { std::vector<std::int64_t> v; for (const Rec& e : onsOf(r)) if (v.size() < n) v.push_back(e.tick); return v; }
std::string joinT(const std::vector<std::int64_t>& v) { std::string s; for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]); return s; }
std::map<std::int64_t, std::vector<int>> byTick(const Run& r) {
  std::map<std::int64_t, std::vector<int>> m;
  for (const Rec& e : r.evs) if (e.t == EventType::NoteOn) m[e.tick].push_back(e.key);
  for (auto& kv : m) std::sort(kv.second.begin(), kv.second.end());
  return m;
}
// 120 bpm at 8 kHz: the sample of a tick is cdiv(tick * 25, 96) (tick / 3.84)
std::int64_t sampleOfTick(std::int64_t tick) { return (tick * 25 + 95) / 96; }
// the scheduled-off tick minus the on tick of the idx-th note
std::vector<std::int64_t> lengthsOf(const Run& r) {
  std::vector<std::int64_t> l;
  for (const Rec& o : onsOf(r)) for (const Rec& e : r.evs) if (e.t == EventType::NoteOff && e.ref == o.ref) { l.push_back(e.tick - o.tick); break; }
  return l;
}

void ownScale(Settings& x, int root, unsigned mask) {
  x.scaleSource = ScaleSource::Own;
  x.scaleRoot = root;
  for (int d = 0; d < 12; ++d) x.scaleDeg[d] = ((mask >> d) & 1u) != 0;
}
// quantize on, horde's scale = C major, 8 kHz / 120 bpm, gate 50
Session qs(int total = 10000, Quantize q = Quantize::Scale) {
  Session s;
  s.total = total;
  s.base.gate = 50;
  s.base.quantize = q;
  s.scale = Scale{0, static_cast<std::uint16_t>(kCMajor)};
  return s;
}

// ====================================================================== the independent oracle (SPEC 6)
struct RefScale {
  int root;
  unsigned mask;
  bool has(int k) const { return ((mask >> (((k - root) % 12 + 12) % 12)) & 1u) != 0; }
};
// every in-scale key within 0-127, ascending; the winner has the least distance, and on a tie (the lower one comes first)
// the higher one only if a previous output exists and is strictly nearer it
int refNearest(const RefScale& sc, int key, bool haveLast, int last) {
  int best = -1;
  for (int c = 0; c <= 127; ++c) {
    if (!sc.has(c)) continue;
    if (best < 0) { best = c; continue; }
    const int db = std::abs(best - key), dc = std::abs(c - key);
    if (dc < db) best = c;
    else if (dc == db && haveLast && std::abs(c - last) < std::abs(best - last)) best = c;
  }
  return best;
}
// the keys `up` plays over E (cyclic), each quantized with the tie chain
std::vector<int> refUp(const std::vector<int>& E, const RefScale& sc, int n) {
  std::vector<int> out;
  bool have = false;
  int last = 0;
  for (int i = 0; i < n; ++i) {
    const int q = refNearest(sc, E[static_cast<std::size_t>(i) % E.size()], have, last);
    out.push_back(q);
    have = true;
    last = q;
  }
  return out;
}
// the in-scale keys of a scale (C major by default), ascending: a degree move is an index walk (a key off the ends is dropped: -1)
int degShift(int key, int g, int root = 0, unsigned mask = kCMajor) {
  std::vector<int> in;
  for (int k = 0; k <= 127; ++k) if (RefScale{root, mask}.has(k)) in.push_back(k);
  const auto it = std::find(in.begin(), in.end(), key);
  if (it == in.end()) return -1;
  const long i = (it - in.begin()) + g;
  return i < 0 || i >= static_cast<long>(in.size()) ? -1 : in[static_cast<std::size_t>(i)];
}

// ====================================================================== Q1
struct Sweep { int notes = 0, bad = 0; std::string first; };
Sweep scaleSweep(const Plant& pl) {
  Sweep sw;
  struct Mode { const char* name; Quantize q; bool own; int root; unsigned mask; };
  const Mode modes[] = {
      {"global C major, scale", Quantize::Scale, false, 0, kCMajor},
      {"own A minor pentatonic, scale", Quantize::Scale, true, 9, kMinPent},
      {"global (degree 0 off), chord_root", Quantize::ChordRoot, false, 0, kCMajor & ~1u},
      {"own (root 4), chord_root", Quantize::ChordRoot, true, 4, kMinPent},
  };
  for (int oi = 0; oi < 16; ++oi) {
    for (const Mode& m : modes) {
      Session s = qs(30000, m.q);
      s.plants = pl;
      s.base.order = static_cast<Order>(oi);
      s.base.seed = 3;
      s.base.steps = 1;
      s.base.stepSize = 5;
      if (m.own) ownScale(s.base, m.root, m.mask); else s.scale = Scale{m.root, static_cast<std::uint16_t>(m.mask)};
      s.ins = {Kk(0, 61), Kk(0, 66), Kk(0, 70)};   // C#, F#, A#: none in C major, lowest held 61
      // the effective scale (source, mode, anchor): chord_root anchors the shape on the lowest held key and always admits its pitch class
      RefScale eff{m.root, m.mask};
      if (m.q == Quantize::ChordRoot) { eff.root = 61 % 12; eff.mask = m.mask | 1u; }
      for (const Rec& e : onsOf(run(s, 512))) {
        ++sw.notes;
        if (e.key < 0 || e.key > 127 || !eff.has(e.key)) { ++sw.bad; if (sw.first.empty()) sw.first = std::string(kOrderNames[oi]) + " / " + m.name + ": key " + std::to_string(e.key); }
      }
    }
  }
  return sw;
}

void testQ1() {
  std::puts("Q1: every quantized key is in the EFFECTIVE scale (source, mode, anchor) and in 0-127; the nearest rule");
  const Sweep red = scaleSweep({"skipQuantizeChord"});
  plant(red.bad > 0, "Q1 detects a chord step that skips quantize (" + std::to_string(red.bad) + " out-of-scale notes, e.g. " + red.first + ")");
  const Sweep ok = scaleSweep({});
  check(ok.bad == 0 && ok.notes > 400, "Q1: all 16 orders x {global scale, own scale, chord_root on a global, chord_root on an own scale} over keys that are all out of scale, with a transposition step: " + std::to_string(ok.notes) + " notes, every one in the effective scale and in 0-127" + (ok.first.empty() ? "" : " - " + ok.first));
  // the nearest rule at EVERY key against the brute-force oracle, in four scales including a one-note scale (no candidate on one side) and a two-note one
  struct Tab { const char* name; bool own; int root; unsigned mask; };
  const Tab tabs[] = {{"C major, global", false, 0, kCMajor}, {"A minor pentatonic, own", true, 9, kMinPent}, {"F only, own", true, 5, 1}, {"G and G#, global", false, 7, 3},
                      {"G# only, own", true, 8, 1}, {"B only, global", false, 11, 1}};   // G# only: key 127's upper candidate 128 is off the top; B only: key 0's lower candidate -1 is off the bottom
  std::vector<std::string> bad;
  int cells = 0;
  for (const Tab& t : tabs)
    for (int key = 0; key < 128; ++key) {
      Session s = qs(1200);
      if (t.own) ownScale(s.base, t.root, t.mask); else s.scale = Scale{t.root, static_cast<std::uint16_t>(t.mask)};
      s.ins = {Kk(0, key)};
      const auto k = keysOf(run(s, 4096), 1);
      ++cells;
      const int want = refNearest(RefScale{t.root, t.mask}, key, false, 0);
      if (k.size() != 1 || k[0] != want) bad.push_back(std::string(t.name) + " key " + std::to_string(key) + ": got " + (k.empty() ? "none" : std::to_string(k[0])) + ", want " + std::to_string(want));
    }
  check(bad.empty(), "known answer: with no previous output, every key 0-127 goes to the nearest in-scale key within 0-127 (ties to the lower) in 6 scales, two of them with one candidate off the end of 0-127 (" + std::to_string(cells) + " cells)" + (bad.empty() ? "" : " - " + bad[0]));
  // a key already in the scale is unchanged, and a zero mask disables quantize (13b.1)
  Session z = qs(5000);
  ownScale(z.base, 0, 0);
  z.ins = {Kk(0, 61), Kk(0, 64)};
  Session zc = z;
  zc.base.quantize = Quantize::ChordRoot;
  check(keysOf(run(z, 64), 4) == std::vector<int>({61, 64, 61, 64}) && keysOf(run(zc, 64), 4) == std::vector<int>({61, 64, 61, 64}), "known answer: a zero mask disables quantize, also under chord_root (whose always-admitted anchor applies only to a non-zero mask)");
  { // SEEDED sweep (M2a critic L1): random scales, random keys, `played` order, so every note after the first resolves its tie against
    // a RANDOM previous output (the chain), not only against none. The oracle is the brute-force nearest rule with the chain.
    std::mt19937 g(4242);
    int notes = 0, badN = 0;
    std::string first;
    for (int c = 0; c < 300; ++c) {
      const int root = static_cast<int>(g() % 12);
      unsigned mask = static_cast<unsigned>(g() & 0xFFF);
      if (mask == 0) mask = 1;
      std::vector<int> pressed;
      for (int i = 2 + static_cast<int>(g() % 3); i > 0; --i) pressed.push_back(static_cast<int>(g() % 128));
      Session s = qs(7000);
      s.base.order = Order::Played;
      s.scale = Scale{root, static_cast<std::uint16_t>(mask)};
      for (std::size_t i = 0; i < pressed.size(); ++i) s.ins.push_back(Kk(static_cast<int>(i), pressed[i]));
      std::vector<int> E;   // played order, distinct keys only (a repeated key is one held key)
      for (std::size_t i = 0; i < pressed.size(); ++i) {
        bool dup = false;
        for (int e : E) dup = dup || e == pressed[i];
        if (!dup) E.push_back(pressed[i]);
      }
      const auto got = keysOf(run(s, 4096), 12);
      const auto want = refUp(E, RefScale{root, mask}, static_cast<int>(got.size()));
      notes += static_cast<int>(got.size());
      if (got.size() < 6 || got != want) { ++badN; if (first.empty()) first = "root " + std::to_string(root) + " mask " + std::to_string(mask) + ": got " + join(got) + " want " + join(want); }
    }
    check(badN == 0 && notes > 1500, "known answer: 300 seeded cases (random scale, random 2-4 keys in press order) match the brute-force nearest rule WITH the lastQ chain, so most ties resolve against a random previous output (" + std::to_string(notes) + " notes)" + (first.empty() ? "" : " - " + first));
  }
  { // SEEDED sweep: degree moves on IRREGULAR masks (an index walk over that scale's in-scale keys), up and down, off both ends
    std::mt19937 g(777);
    int notes = 0, badN = 0;
    std::string first;
    for (int c = 0; c < 200; ++c) {
      const int root = static_cast<int>(g() % 12);
      unsigned mask = static_cast<unsigned>(g() & 0xFFF);
      if (mask == 0) mask = 1;
      std::vector<int> in;
      for (int k = 0; k <= 127; ++k) if (RefScale{root, mask}.has(k)) in.push_back(k);
      const int key = in[g() % in.size()];
      const int deg = static_cast<int>(g() % 9) - 4;
      Session s = qs(9000);
      ownScale(s.base, root, mask);
      s.base.steps = 1;
      s.base.stepSize = deg;
      s.base.stepUnit = StepUnit::Degrees;
      s.ins = {Kk(0, key)};
      const Run r = run(s, 4096);
      const int moved = degShift(key, deg, root, mask);
      std::vector<int> want;
      for (int i = 0; i < 8; ++i) { const int v = i % 2 ? moved : key; if (v >= 0) want.push_back(v); }
      const auto got = keysOf(r, want.size());
      notes += static_cast<int>(got.size());
      if (got != want) { ++badN; if (first.empty()) first = "root " + std::to_string(root) + " mask " + std::to_string(mask) + " key " + std::to_string(key) + " deg " + std::to_string(deg) + ": got " + join(got) + " want " + join(want); }
    }
    check(badN == 0 && notes > 600, "known answer: 200 seeded degree moves on irregular masks (a step of -4..+4 degrees from an in-scale key, up and down, off either end of 0-127) equal an index walk over that scale's own in-scale keys (" + std::to_string(notes) + " notes)" + (first.empty() ? "" : " - " + first));
  }
}

// ====================================================================== Q2 / Q5
Session tieBase() {   // C major, `up` over C# below and C above: every pass over the C# is a tie between 60 and 62
  Session s = qs(9000);
  s.ins = {K(0, "a", 61), K(0, "b", 72)};
  return s;
}
void testQ2Q5() {
  std::puts("Q2 / Q5: a tie goes toward the previous quantized output (lower when there is none); lastQ follows evaluation and resets at every release-all");
  auto above = [&](const Plant& pl) { Session s = tieBase(); s.plants = pl; return join(keysOf(run(s, 64), 8)); };
  plant(above({"tieAlwaysDown"}) != "60,72,62,72,62,72,62,72", "Q2 / Q5 detect a tie that always resolves downward");
  check(above({}) == "60,72,62,72,62,72,62,72", "known answer: up over C# and C in C major plays 60 (no previous output: the lower key) 72, then 62: the previous output 72 is ABOVE the tie, so the tie resolves UP (Q5)");
  Session below = qs(9000);
  below.ins = {Kk(0, 55), Kk(0, 61)};
  check(join(keysOf(run(below, 64), 6)) == "55,60,55,60,55,60", "known answer: with the previous output 55 BELOW the tie, the tie goes to the lower key, 60 (Q2)");
  // a previous output EQUALLY near both candidates (it came from an earlier, chromatic scale): the lower key
  Session eq = qs(6000);
  ownScale(eq.base, 0, 0xFFF);
  eq.cfg = {C(1500, [](Settings& x) { ownScale(x, 0, kCMajor); })};
  eq.ins = {Kk(0, 61)};
  check(join(keysOf(run(eq, 64), 5)) == "61,61,60,60,60", "known answer: 61 played in a chromatic scale, then C major from the next step: the tie 60 / 62 is equally near the previous output 61, so the lower key, 60");
  { // lastQ is not touched while quantize is off or the mask is zero: switching quantize on later starts the chain from nothing
    auto late = [&](bool zeroMask) {
      Session s = qs(9000, zeroMask ? Quantize::Scale : Quantize::Off);
      if (zeroMask) ownScale(s.base, 0, 0);
      s.cfg = {C(3500, [zeroMask](Settings& x) { if (zeroMask) ownScale(x, 0, kCMajor); else x.quantize = Quantize::Scale; })};
      s.ins = {Kk(0, 61), Kk(0, 72)};
      return join(keysOf(run(s, 64), 8));
    };
    check(late(false) == "61,72,61,72,60,72,62,72" && late(true) == "61,72,61,72,60,72,62,72", "known answer: lastQ is untouched while quantize is off or the mask is zero (72 played unquantized does not pull the first tie up): 60 at step 4, then 72, then 62");
  }
  // lastQ resets at every release-all: T7 panic, T6 transport stop, T4 enable off/on, T10 reset() and prepare()
  const std::string want = "60,72,62,72";
  auto afterPanic = [&](const Plant& pl) { Session s = tieBase(); s.plants = pl; s.ins = {K(0, "a", 61), K(0, "b", 72), Pan(3500), K(4100, "c", 61), K(4100, "d", 72)}; return join(keysFrom(run(s, 64), 4000, 4)); };
  auto afterStop = [&](const Plant& pl) { Session s = tieBase(); s.plants = pl; auto h = std::make_shared<SimHost>(8000, 120, 0, true); h->set(3500, false); s.host = h; return join(keysFrom(run(s, 64), 3500, 4)); };
  auto afterEnable = [&](const Plant& pl) { Session s = tieBase(); s.plants = pl; s.cfg = {C(3500, [](Settings& x) { x.enabled = false; }), C(4000, [](Settings& x) { x.enabled = true; })}; return join(keysFrom(run(s, 64), 4000, 4)); };
  auto afterReset = [&](const Plant& pl) { Session s = tieBase(); s.plants = pl; s.resets = {3500}; s.ins = {K(0, "a", 61), K(0, "b", 72), K(3600, "c", 61), K(3600, "d", 72)}; return join(keysFrom(run(s, 64), 3600, 4)); };
  auto afterPrepare = [&](const Plant& pl) { Session s = tieBase(); s.plants = pl; s.prepares = {3500}; s.ins = {K(0, "a", 61), K(0, "b", 72), K(3600, "c", 61), K(3600, "d", 72)}; return join(keysFrom(run(s, 64), 3600, 4)); };
  // A STOPPED transport: the first process() after prepare() runs the free clock with no clock edge, so no releaseAll runs; only
  // resetState's own clear keeps SPEC 6's T10 reset (M2a critic M1: it is not redundant).
  auto afterPrepareStopped = [&](const Plant& pl) {
    Session s = tieBase();
    s.plants = pl;
    s.host = std::make_shared<SimHost>(8000, 120, 0, false);
    s.prepares = {3500};
    s.ins = {K(0, "a", 61), K(0, "b", 72), K(3600, "c", 61), K(3600, "d", 72)};
    return join(keysFrom(run(s, 64), 3600, 4));
  };
  const bool fired = afterPanic({"lastQNoReset"}) != want && afterStop({"lastQNoReset"}) != want && afterEnable({"lastQNoReset"}) != want && afterReset({"lastQNoReset"}) != want && afterPrepare({"lastQNoReset"}) != want &&
                     afterPrepareStopped({"lastQNoReset"}) != want;
  plant(fired, "Q5 detects a lastQ that survives a release-all: Panic, transport stop, enable off/on, reset(), prepare() and prepare() on a stopped transport all play 62 first (" + afterPanic({"lastQNoReset"}) + ")");
  check(afterPanic({}) == want && afterStop({}) == want && afterEnable({}) == want && afterReset({}) == want && afterPrepare({}) == want && afterPrepareStopped({}) == want,
        "Q5: after Panic (T7), a transport stop (T6), enabled off/on (T4), reset(), prepare() and prepare() on a STOPPED transport (T10; no clock edge runs there) the tie chain starts over: 60 72 62 72 each time (lastQ was 72 when the release-all hit)");
  // lastQ follows EVALUATION, not emission: a pending queue of 1 refuses the second key of every chord step, but it still counts
  auto refused = [&](const Plant& pl, std::int64_t* overflow) {
    Session s = qs(6000);
    s.plants = pl;
    s.caps.pending = 1;
    s.base.order = Order::Chord;
    s.ins = {Kk(0, 61), Kk(0, 63)};
    const Run r = run(s, 64);
    if (overflow) *overflow = r.core->counters().pendingOverflow;
    return join(keysOf(r, 5));
  };
  std::int64_t ovf = 0;
  plant(refused({"lastQAtEmission"}, nullptr) != "60,62,62,62,62", "Q5 detects lastQ updated at emission (the refused 63 would never count)");
  check(refused({}, &ovf) == "60,62,62,62,62" && ovf > 0, "known answer: with room for one pending on, the chord 61 + 63 emits only its first key (" + std::to_string(ovf) + " refused), yet the refused key still moved lastQ: 60, then 62 62 62 (lastQ follows evaluation)");
  // G1 with quantize ties in play: a tie-heavy session, absolute samples identical at every block size
  auto g1 = [&](const char* name) {
    std::string ref;
    for (int b : {4096, 512, 64, 7, 1}) { const std::string st = streamOf(run(m2aSession(name), b)); if (ref.empty()) ref = st; else if (st != ref) return false; }
    return !ref.empty();
  };
  const std::size_t tieEvents = run(m2aSession("ties"), 64).evs.size();
  check(g1("ties") && tieEvents > 100, "Q5: G1 holds with quantize ties in play (bounce over out-of-scale keys, degrees, swing, tempo changes, a stop / start and a Panic): identical at block sizes 4096/512/64/7/1 (" + std::to_string(tieEvents) + " events)");
}

// ====================================================================== Q3 / Q4
void testQ3() {
  std::puts("Q3: scale_source = own follows Arp's own scale, not the global one");
  const std::vector<int> E = {61, 66, 70};
  auto seq = [&](const Plant& pl) {
    Session s = qs(9000);
    s.plants = pl;
    ownScale(s.base, 9, kMinPent);
    s.ins = {Kk(0, 61), Kk(0, 66), Kk(0, 70)};
    return join(keysOf(run(s, 64), 9));
  };
  const std::string own = join(refUp(E, RefScale{9, kMinPent}, 9)), glob = join(refUp(E, RefScale{0, kCMajor}, 9));
  plant(seq({"ownIgnored"}) != own, "Q3 detects an own scale that is ignored (the global C major is used)");
  check(seq({}) == own && own != glob, "Q3: with scale_source = own (A minor pentatonic) up over 61 66 70 plays " + own + ", the oracle's line for the OWN scale, not the global scale's " + glob);
  // the global source: the block's scale is read live
  Session g = qs(9000);
  g.scale = Scale{2, static_cast<std::uint16_t>(kMinPent)};
  g.ins = {Kk(0, 61), Kk(0, 66), Kk(0, 70)};
  check(join(keysOf(run(g, 64), 9)) == join(refUp(E, RefScale{2, kMinPent}, 9)), "Q3: with scale_source = global the block's {root, mask} is the scale (D minor pentatonic from horde)");
}

void testQ4() {
  std::puts("Q4: chord_root anchors the scale shape on the LOWEST HELD key");
  auto run2 = [&](const Plant& pl, bool own, std::vector<InEv> ins, std::function<void(Settings&)> extra = {}) {
    Session s = qs(9000, Quantize::ChordRoot);
    s.plants = pl;
    if (own) ownScale(s.base, 0, kCMajor); else s.scale = Scale{7, static_cast<std::uint16_t>(kCMajor)};   // the root setting must be ignored either way
    if (extra) extra(s.base);
    s.ins = std::move(ins);
    return join(keysOf(run(s, 64), 6));
  };
  // held C# and F#: in a major shape anchored on C# the F# is degree 5, so both stay; anchored on the root setting they would move
  auto bad = [&](const Plant& pl) { return run2(pl, true, {Kk(0, 61), Kk(0, 66)}) != "61,66,61,66,61,66" || run2(pl, false, {Kk(0, 61), Kk(0, 66)}) != "61,66,61,66,61,66"; };
  plant(bad({"chordRootAsRoot"}), "Q4 detects chord_root anchored on the root setting instead of the lowest held key");
  check(!bad({}), "Q4: held C# and F# stay as played in a major shape anchored on C# (scale_root 0 own, or horde's root 7 global: the root setting is ignored)");
  // the anchor follows the lowest held key: {61, 64} -> 61 63 61 63 (64 is not in the shape; the tie goes toward 61); 61 released -> {64} anchors on 64: 64 64
  check(run2({}, true, {Kk(0, 61), Kk(0, 64), Ok(3500, 61)}) == "61,63,61,63,64,64", "known answer: held {61, 64}: 64 snaps to 63 (toward the previous output 61); when 61 is released the lowest held key is 64, the anchor moves, and 64 stays");
  // the anchor's own pitch class is always admitted, even with degree 0 off (r2 M4): mask {2,4,7} anchored on C#
  auto degree0Off = [&](Quantize q) { Session s = qs(5000, q); ownScale(s.base, 1, 4 + 16 + 128); s.ins = {Kk(0, 61)}; return join(keysOf(run(s, 64), 3)); };
  check(degree0Off(Quantize::ChordRoot) == "61,61,61" && degree0Off(Quantize::Scale) == "63,63,63", "known answer: with degree 0 off, chord_root still admits the anchor's own pitch class (61 stays) while plain `scale` anchored on root 1 moves it to 63");
}

// ====================================================================== collisions (SPEC 6)
void testCollisions() {
  std::puts("Quantize collisions: two notes of one step landing on one key keep the EARLIEST source in the style's order");
  // 60 stays; 61 ties between 60 and 62 toward the previous output 60 and lands on 60: one note, 60's velocity and channel
  auto fwd = [&](const Plant& pl) {
    Session s = qs(3000);
    s.plants = pl;
    s.base.order = Order::Chord;
    s.ins = {Kk(0, 60, 90, 1), Kk(0, 61, 50, 2)};
    const Run r = run(s, 64);
    std::vector<Rec> at0;
    for (const Rec& e : onsOf(r)) if (e.tick == 0) at0.push_back(e);
    return at0.size() == 1 && at0[0].key == 60 && at0[0].vel == 90 && at0[0].ref.channel == 1;
  };
  plant(!fwd({"collisionKeepsLast"}), "the collision check detects the LAST source's velocity and channel being kept");
  check(fwd({}), "known answer: keys 60 (vel 90, ch 1) and 61 (vel 50, ch 2) in one chord step both land on 60: ONE note, with 60's velocity 90 and channel 1");
  // reversed: 61 goes UP to 62 (the previous output is 72) and meets 62 itself; the earlier source (61) still wins
  auto rev = [&](const Plant& pl) {
    Session s = qs(9000);
    s.plants = pl;
    s.base.pattern = Pattern::User;
    s.userPattern = userPat({B().pick(kPickHigh), B().pick(kPickChord)});
    s.ins = {Kk(0, 61, 40, 1), Kk(0, 62, 110, 2), Kk(0, 72, 80, 3)};
    const Run r = run(s, 64);
    std::map<int, std::pair<int, int>> at;   // key -> (vel, channel) at the second step
    int n = 0;
    for (const Rec& e : onsOf(r)) if (e.tick == kStepTicks) { at[e.key] = {e.vel, e.ref.channel}; ++n; }
    return n == 2 && at.count(62) && at.count(72) && at[62] == std::make_pair(40, 1) && at[72] == std::make_pair(80, 3);
  };
  plant(!rev({"collisionKeepsLast"}), "the collision check detects the later key's velocity / channel on a reversed collision");
  check(rev({}), "known answer: after a high pick leaves lastQ at 72, the chord 61 62 72 sends 61 UP to 62: ONE note on 62 carrying 61's velocity 40 and channel 1 (the earlier source), plus 72");
}

// ====================================================================== X1 (degree part)
Run playStyle(Order o, int e, int nSteps, const Plant& plants, const std::function<void(Settings&)>& extra) {
  static const std::array<int, 5> pressOrder = {67, 60, 74, 64, 71};   // all in C major
  Session s = qs(1000 * (nSteps + 2));
  s.plants = plants;
  s.base.order = o;
  s.base.seed = 7;
  if (extra) extra(s.base);
  for (int i = 0; i < e; ++i) s.ins.push_back(Kk(100 + i, pressOrder[static_cast<std::size_t>(i)]));
  return run(s, 4096);
}
std::vector<std::vector<int>> stepSets(const Run& r, int nSteps) {
  const auto m = byTick(r);
  std::vector<std::vector<int>> v;
  for (int n = 0; n < nSteps; ++n) { const auto it = m.find(kStepTicks * (1 + n)); v.push_back(it == m.end() ? std::vector<int>{} : it->second); }
  return v;
}
int periodRef(Order o, int e) {
  switch (o) {
    case Order::Bounce: return e == 1 ? 1 : 2 * e - 2;
    case Order::Bounce2: return 2 * e;
    case Order::Chord: return 1;
    case Order::PedalLow: case Order::PedalHigh: return e == 1 ? 1 : 2 * (e - 1);
    case Order::ChordWalk: return e + 1;
    case Order::Stride: return 2;
    case Order::ChordBetween: return 2 * e;
    case Order::Pairs: return e == 1 ? 1 : e - 1;
    default: return e;
  }
}

void testX1() {
  std::puts("X1 (degree part): a pass moves t * step_size IN-SCALE keys; with no scale a degree is a semitone");
  auto keys = [&](const Plant& pl, const std::function<void(Settings&)>& f, std::size_t n, const std::vector<int>& held = {60, 64, 67}) {
    Session s = qs(40000);
    s.plants = pl;
    f(s.base);
    for (int k : held) s.ins.push_back(Kk(0, k));
    return join(keysOf(run(s, 64), n));
  };
  auto deg2 = [](Settings& x) { x.steps = 2; x.stepSize = 2; x.stepUnit = StepUnit::Degrees; };
  const std::string want = "60,64,67,64,67,71,67,71,74,60,64,67,64,67,71";
  plant(keys({"degreesAsSemitones"}, deg2, 15) != want, "X1 detects degrees counted as semitones while a scale is on");
  check(keys({}, deg2, 15) == want, "X1(a): up over C E G in C major, 2 steps of +2 DEGREES: C E G | E G B | G B D | C E G (in-scale keys, not +2 semitones)");
  check(keys({}, [&](Settings& x) { deg2(x); x.quantize = Quantize::Off; }, 9) == "60,64,67,62,66,69,64,68,71", "X1(b): with quantize off a degree is one semitone (C E G | D F# A | E G# B)");
  { // a pass that runs off the bottom of 0-127 drops its key (T16, counted) and the pass counter keeps going
    Session s = qs(18000);
    s.base.steps = 1; s.base.stepSize = -3; s.base.stepUnit = StepUnit::Degrees;
    s.ins = {Kk(0, 4)};
    const Run r = run(s, 64);
    bool alt = onsOf(r).size() >= 4;
    for (std::size_t i = 0; i < onsOf(r).size(); ++i) alt = alt && onsOf(r)[i].key == 4 && onsOf(r)[i].a == static_cast<std::int64_t>(i) * 2000;
    check(alt && r.core->counters().keysDropped >= 4, "X1(c): key 4 (E) with 1 step of -3 degrees: pass 1 would be E D C B(-1) below 0, so it is dropped (counted) and 4 plays every second step");
  }
  { // the top of the range: a degree move off 127 drops the key at stage 5 (counted), the pass counter keeps going
    Session s = qs(14000);
    s.base.steps = 1; s.base.stepSize = 1; s.base.stepUnit = StepUnit::Degrees;
    s.ins = {Kk(0, 127)};
    const Run r = run(s, 64);
    bool alt = onsOf(r).size() >= 3;
    for (std::size_t i = 0; i < onsOf(r).size(); ++i) alt = alt && onsOf(r)[i].key == 127 && onsOf(r)[i].a == static_cast<std::int64_t>(i) * 2000;
    check(alt && r.core->counters().keysDropped >= 3, "X1(c'): key 127 (G) with 1 step of +1 degree: there is no in-scale key above 127, so pass 1 is dropped (counted) and 127 plays every second step");
  }
  // (d) every deterministic style: pass t is the base cycle (read at steps = 0) moved t * 2 degrees; 3 passes, then back to 0
  std::vector<std::string> bad;
  auto badStyles = [&](const Plant& pl) {
    bad.clear();
    for (int oi = 0; oi < 16; ++oi) {
      const Order o = static_cast<Order>(oi);
      if (o == Order::Random || o == Order::Shuffle) continue;
      for (int e : {3, 5}) {
        const int P = periodRef(o, e), n = 2 * 3 * P;
        const auto base = stepSets(playStyle(o, e, n, {}, [](Settings& x) { x.steps = 0; }), n);
        std::vector<std::vector<int>> wantSets;
        for (int i = 0; i < n; ++i) {
          const int t = (i / P) % 3;
          std::vector<int> st;
          for (int k : base[static_cast<std::size_t>(i)]) st.push_back(degShift(k, 2 * t));
          std::sort(st.begin(), st.end());
          wantSets.push_back(st);
        }
        if (stepSets(playStyle(o, e, n, pl, deg2), n) != wantSets) bad.push_back(std::string(kOrderNames[oi]) + "/" + std::to_string(e));
      }
    }
    return !bad.empty();
  };
  plant(badStyles({"degreesAsSemitones"}), "X1 detects degrees as semitones across the styles");
  const bool styleBad = badStyles({});
  check(!styleBad, "X1(d): all 14 deterministic styles (e = 3, 5): each pass is the base cycle moved t * 2 in-scale keys, 3 passes then back to 0" + (bad.empty() ? "" : " - wrong: " + bad[0]));
}

// ====================================================================== ST1 (degree part) and the six-stage order (SPEC 4.7)
void testST1AndStages() {
  std::puts("ST1 (degree part): step x the PATTERN's step_size in the pattern's unit; SPEC 4.7: the six-stage offset order");
  auto keysOfPattern = [&](const Plant& pl, UserPattern up, std::vector<int> held, std::function<void(Settings&)> extra, std::size_t n, std::vector<int> vel = {}) {
    Session s = qs(40000);
    s.plants = pl;
    s.base.pattern = Pattern::User;
    s.userPattern = up;
    if (extra) extra(s.base);
    for (std::size_t i = 0; i < held.size(); ++i) s.ins.push_back(Kk(0, held[i], vel.empty() ? 100 : vel[i]));
    return join(keysOf(run(s, 64), n));
  };
  const UserPattern deg = userPat({B().step(2), B(), B().step(-1)}, 1, StepUnit::Degrees);
  auto stBad = [&](const Plant& pl) { return keysOfPattern(pl, deg, {60}, {}, 6) != "64,60,59,64,60,59"; };
  plant(stBad({"stepDegreesAsSemitones"}), "ST1 detects the step offset's degrees counted as semitones");
  plant(stBad({"stepOffsetReadsGlobal"}), "ST1 detects the step offset reading the GLOBAL step_size / unit (tested with a pattern unit that differs)");
  plant(stBad({"stepSizeIgnored"}), "ST1 detects every step size read as 12 semitones (degree pattern)");
  check(!stBad({}), "ST1: a user pattern in degrees (step_size 1, steps +2 0 -1) over C in C major plays 64 (E) 60 59 (B): degrees count in-scale keys");
  check(keysOfPattern({}, userPat({B().step(1)}, 2, StepUnit::Degrees), {60}, {}, 3) == "64,64,64", "ST1: step +1 of a pattern step_size 2 in degrees is TWO in-scale keys (C -> E)");

  // ---- the six stages, in order
  const UserPattern semi1 = userPat({B().step(1)}, 1);
  auto s1 = [&](const Plant& pl) { return keysOfPattern(pl, semi1, {60}, {}, 4); };
  plant(s1({"semitoneAfterQuantize"}) != "60,60,60,60", "SPEC 4.7 stage 1 -> 3 is detected: the semitone part must be added BEFORE quantize");
  check(s1({}) == "60,60,60,60", "stages 1-3: C with a +1 semitone step offset: 61 is quantized afterwards (tie, toward the previous output 60), so every note is 60 (never 61, out of scale)");
  const UserPattern deg1 = userPat({B().step(-1)}, 1, StepUnit::Degrees);
  auto s2 = [&](const Plant& pl) { return keysOfPattern(pl, deg1, {61}, {}, 4); };
  plant(s2({"degreeBeforeQuantize"}) != "59,59,59,59", "SPEC 4.7 stage 3 -> 4 is detected: the degree move must run AFTER quantize");
  check(s2({}) == "59,59,59,59", "stages 3-4: C# with a -1 degree step offset: quantized to 60 first, THEN one in-scale key down: 59 (moving first from 61 would give 60)");
  const UserPattern deg4 = userPat({B().step(4)}, 1, StepUnit::Degrees);
  auto s3 = [&](const Plant& pl) { Session s = qs(3000); s.plants = pl; s.base.order = Order::Chord; s.base.pattern = Pattern::User; s.userPattern = deg4; s.ins = {Kk(0, 60), Kk(0, 61)}; return byTick(run(s, 64)); };
  plant(s3({"lastQPreMove"})[0] != std::vector<int>({67, 69}), "SPEC 4.7 stage 6 is detected: lastQ must be the key AFTER the degree move");
  check(s3({})[0] == std::vector<int>({67, 69}), "stage 6: C and C# in one chord step, +4 degrees: C goes 60 -> 67 and sets lastQ = 67, so C# ties UP to 62 and goes 69 (a lastQ of 60 would send it to 60 -> 67)");
  const UserPattern drop = userPat({B().pick(kPickHigh), B().pick(kPickChord).step(-1)}, 1, StepUnit::Degrees);
  auto s4 = [&](const Plant& pl, std::int64_t* dropped) {
    Session s = qs(5000); s.plants = pl; s.base.pattern = Pattern::User; s.userPattern = drop;
    s.ins = {Kk(0, 0), Kk(0, 1), Kk(0, 100)};
    const Run r = run(s, 64);
    if (dropped) *dropped = r.core->counters().keysDropped;
    return byTick(r)[kStepTicks];
  };
  std::int64_t dropped = 0;
  plant(s4({"droppedKeySetsLastQ"}, nullptr) != std::vector<int>({0, 98}), "SPEC 4.7 stage 6 is detected: a key dropped at stage 5 must NOT set lastQ");
  check(s4({}, &dropped) == std::vector<int>({0, 98}) && dropped >= 1, "stages 5-6: keys 0, 1, 100; step 0 picks 100 (lastQ 100); step 1 plays all with -1 degree: 0 falls below 0 and is dropped WITHOUT touching lastQ, so 1 ties UP to 2 and goes to 0; 100 goes to 98");
  const UserPattern semiTop = userPat({B().step(1)}, 1);
  auto s5 = [&](const Plant& pl, std::int64_t* dropped2) {
    Session s = qs(8000); s.plants = pl; s.base.pattern = Pattern::User; s.userPattern = semiTop;
    s.base.steps = 1; s.base.stepSize = -1; s.base.stepUnit = StepUnit::Degrees;
    s.ins = {Kk(0, 127)};
    const Run r = run(s, 64);
    if (dropped2) *dropped2 = r.core->counters().keysDropped;
    return onsOf(r).size();
  };
  plant(s5({"noStage2Drop"}, nullptr) != 0, "SPEC 4.7 stage 2 is detected: a key past 127 is dropped BEFORE quantize and the degree move");
  std::int64_t dropped2 = 0;
  check(s5({}, &dropped2) == 0 && dropped2 >= 4, "stage 2 (r3 LOW-7): G with a +1 semitone offset is 128: dropped at once, even though the pass's -1 degree would have brought it back (" + std::to_string(dropped2) + " dropped, nothing plays)");
  // all six at once: semitones from the pattern, degrees from the pass
  auto s6 = [&](const Plant& pl) { return keysOfPattern(pl, semi1, {60}, [](Settings& x) { x.steps = 1; x.stepSize = 1; x.stepUnit = StepUnit::Degrees; }, 6); };
  plant(s6({"semitoneAfterQuantize"}) != "60,62,62,64,62,64", "SPEC 4.7 mixed offsets are detected when the semitone part comes after quantize");
  check(s6({}) == "60,62,62,64,62,64", "mixed kinds: a +1 SEMITONE pattern step with a global +1 DEGREE pass: 61 is quantized first (ties toward lastQ), then pass 1 moves one in-scale key: 60 62 62 64 62 64");
}

// ====================================================================== swing (SPEC 5.1): G3
void testSwing() {
  std::puts("G3 / swing: odd steps are delayed by parity of kAbs; ons go through the pending queue");
  auto onTicks = [&](const Plant& pl, int swing, int press, std::function<void(Settings&)> extra = {}, std::vector<Change> cfg = {}) {
    Session s;
    s.total = 30000;
    s.plants = pl;
    s.base.gate = 50;
    s.base.swing = swing;
    if (extra) extra(s.base);
    s.cfg = std::move(cfg);
    s.ins = {Kk(press, 60)};
    return run(s, 64);
  };
  // 667 permille at a 3840-tick step: fdiv(3840 * 334, 1000) = 1282. The gesture anchors at kAbs 1 (ODD), so k = kAbs - 1 has the other parity.
  auto g3 = [&](const Plant& pl) { return joinT(ticksOf(onTicks(pl, 667, 100), 4)); };
  plant(g3({"swingParityK"}) != "5122,7680,12802,15360", "G3 detects swing parity taken from k (the steps since the anchor) instead of kAbs");
  const Run r3 = onTicks({}, 667, 100);
  bool smp = true;
  const std::vector<std::int64_t> wantT = {5122, 7680, 12802, 15360};
  for (std::size_t i = 0; i < 4; ++i) smp = smp && onsOf(r3)[i].a == sampleOfTick(wantT[i]);
  check(g3({}) == "5122,7680,12802,15360" && smp, "G3: swing 667 with the anchor on an ODD absolute step: kAbs 1 and 3 are delayed by 1282 ticks (5122, 12802), the even ones are not (7680, 15360); the ons land on cdiv of those ticks (samples 1334, 2000, 3334, 4000)");
  check(joinT(ticksOf(onTicks({}, 500, 0), 4)) == "0,3840,7680,11520" && joinT(ticksOf(onTicks({}, 750, 0), 4)) == "0,5760,7680,13440", "known answer: swing 500 is straight; swing 750 (the maximum) delays every odd step by half a step, 1920 ticks");
  check(ticksOf(onTicks({}, 900, 100), 1)[0] == 5760 && ticksOf(onTicks({}, 100, 100), 1)[0] == 3840, "known answer: swing is clamped to 500..750 (900 acts as 750, 100 as 500)");
  { // pre-roll (Q-A5): the grid extends to NEGATIVE steps, whose parity is mod(kAbs, 2), not C++ `%` (-3 % 2 is -1, not 1)
    Session s;
    s.total = 6000; s.base.gate = 50; s.base.swing = 667;
    s.host = std::make_shared<SimHost>(8000, 120, -1.0, true);   // one beat before the origin: kAbs -4 sits at sample 0
    s.ins = {Kk(100, 60)};
    check(joinT(ticksOf(run(s, 64), 5)) == "-10238,-7680,-2558,0,5122", "known answer: pre-roll with swing 667, anchored on kAbs -3 (odd): -3 and -1 are delayed (-11520 -> -10238, -3840 -> -2558), -2 and 0 are not: -10238 -7680 -2558 0 5122");
  }
  { // swing is latched per step PAIR (SPEC 5.1, E6): the even step reads it, its odd partner reuses it. No re-anchor, nothing sounding is touched
    const Run r = onTicks({}, 500, 0, {}, {C(1500, [](Settings& x) { x.swing = 750; })});
    check(joinT(ticksOf(r, 6)) == "0,3840,7680,13440,15360,21120" && n1(r).empty(), "known answer: swing 500 -> 750 at sample 1500 (mid-pair 0/1) applies from the next EVEN step (2): 0 3840 7680 13440 15360 21120, and N1 holds");
    // a change between step 2 and its odd partner 3 (at 2500) does NOT reach step 3: it keeps step 2's swing and applies from step 4
    auto mid = [&](const Plant& pl) { return joinT(ticksOf(onTicks(pl, 750, 0, {}, {C(2500, [](Settings& x) { x.swing = 500; })}), 6)); };
    plant(mid({"swingPerStep"}) != "0,5760,7680,13440,15360,21120", "E6 detects swing read at every step instead of latched per pair (the odd step 3 would go straight: 11520)");
    check(mid({}) == "0,5760,7680,13440,15360,19200", "known answer (E6): swing 750 -> 500 at sample 2500 (between step 2 and its partner 3): step 3 stays swung (13440), the change applies from step 4, so 5 is straight (19200)");
  }
  { // the critic's case: swing 750, one step with 4 ratchets, swing set to 500 at 2500: every sub-note of a step starts before the NEXT step's on
    auto scene = [&](const Plant& pl) {
      Session s;
      s.total = 9000; s.plants = pl; s.base.gate = 100; s.base.swing = 750; s.base.pattern = Pattern::User; s.userPattern = userPat({B().ratchet(4)});
      s.cfg = {C(2500, [](Settings& x) { x.swing = 500; })};
      s.ins = {Kk(0, 60)};
      return s;
    };
    auto overlap = [&](const Plant& pl) {
      std::string first;
      for (int block : {4096, 64, 1}) {
        const Run r = run(scene(pl), block);
        std::map<std::int64_t, std::pair<std::int64_t, std::int64_t>> span;   // step ordinal -> (first sub-note tick, last sub-note tick)
        for (const Rec& e : onsOf(r)) { auto it = span.find(e.ord); if (it == span.end()) span[e.ord] = {e.tick, e.tick}; else { it->second.first = std::min(it->second.first, e.tick); it->second.second = std::max(it->second.second, e.tick); } }
        if (span.size() < 8 || !n1(r).empty()) return std::string("too few steps / N1");
        for (auto it = span.begin(); std::next(it) != span.end(); ++it)
          if (it->second.second >= std::next(it)->second.first && first.empty()) first = "step " + std::to_string(it->first) + " has a sub-note at tick " + std::to_string(it->second.second) + " but the next step starts at " + std::to_string(std::next(it)->second.first) + " (block " + std::to_string(block) + ")";
      }
      return first;
    };
    plant(!overlap({"swingPerStep"}).empty(), "E6 detects a ratcheted step whose sub-notes sound after the next step's on (" + overlap({"swingPerStep"}) + ")");
    const Run r = run(scene({}), 64);
    std::map<std::int64_t, std::int64_t> startOf;
    for (const Rec& e : onsOf(r)) if (!startOf.count(e.ord) || e.tick < startOf[e.ord]) startOf[e.ord] = e.tick;
    check(overlap({}).empty() && startOf[3] == 13440 && startOf[4] == 15360 && startOf[5] == 19200,
          "known answer (E6, the critic's case): swing 750 with 4 ratchets, swing 500 from sample 2500: no step has a sub-note at or after the next step's on at block sizes 4096/64/1; step 3 (the odd partner of 2) starts at 13440 still swung, the change takes effect at the next even step 4, so step 5 starts straight at 19200");
  }
  { // an odd step evaluated WITHOUT its even partner reads the CURRENT swing. A seek (T6) at 4500 lands on ODD step 5 (beat 1.25) right after step 4
    // (the last even step, which latched 750) was evaluated: the new numbering has no partner, so the stale 750 must NOT be reused; the swing was set to 500 at 4200
    Session s;
    s.total = 9000; s.base.gate = 50; s.base.swing = 750;
    auto h = std::make_shared<SimHost>(8000, 120, 0, true);
    h->set(4500, std::nullopt, 1.25);   // a +1920 tick jump (> 1/16 beat): T6
    s.host = h;
    s.cfg = {C(4200, [](Settings& x) { x.swing = 500; })};
    s.ins = {Kk(0, 60)};
    const Run r = run(s, 64);
    std::int64_t first = -1;
    for (const Rec& e : onsOf(r)) if (e.a >= 4500 && first < 0) first = e.tick;
    check(first == 19200 && n1(r).empty(), "known answer (E6): after a seek that lands on ODD step 5 there is no even partner in the new numbering, so it reads the current swing (500, straight): tick 19200, not the stale 750's 21120");
  }
  { // the same through a RATE change: 1/16 -> 1/8T lands the new grid on odd step 5 right after step 5 was the last one visited (its even partner 4 latched 750)
    Session s;
    s.total = 9000; s.base.gate = 50; s.base.swing = 750;
    s.cfg = {C(5200, [](Settings& x) { x.swing = 500; }), C(5500, [](Settings& x) { x.rate = Rate::R1_8T; })};
    s.ins = {Kk(0, 60)};
    const Run r = run(s, 64);
    std::int64_t first = -1;
    for (const Rec& e : onsOf(r)) if (e.a > 5600 && first < 0) first = e.tick;
    check(first == 25600 && n1(r).empty(), "known answer (E6): a rate change renumbers the steps; the new grid's odd step 5 reads the current swing (500): tick 25600, not 28160");
  }
  { // ratchets split the SWUNG span: odd step 1920 ticks, even steps 5760
    Session s;
    s.total = 20000; s.base.gate = 80; s.base.swing = 750; s.base.pattern = Pattern::User; s.userPattern = userPat({B().ratchet(2)});
    s.ins = {Kk(0, 60)};
    const Run q = run(s, 64);
    check(joinT(ticksOf(q, 8)) == "0,2880,5760,6720,7680,10560,13440,14400" && joinT([&] { auto l = lengthsOf(q); l.resize(8); return l; }()) == "2304,2304,768,768,2304,2304,768,768",
          "known answer: 2 ratchets at swing 750 split each swung span (even steps 5760 ticks, odd 1920): starts 0 2880 5760 6720 7680 10560 13440 14400, lengths 2304 / 768 (floor of the sub-step, then gate 80 %)");
  }
  { // a tie's off uses the swung position and span of the LAST tied step
    Session s;
    s.total = 20000; s.base.gate = 80; s.base.swing = 750; s.base.pattern = Pattern::User; s.userPattern = userPat({B(), B().tie().gate(50)});
    s.ins = {Kk(0, 60)};
    const Run q = run(s, 64);
    std::vector<std::int64_t> offs;
    for (const Rec& e : q.evs) if (e.t == EventType::NoteOff && e.a < 6000) offs.push_back(e.tick);
    check(joinT(ticksOf(q, 2)) == "0,7680" && offs.size() >= 2 && offs[0] == 6528 && offs[1] == 14208, "known answer: [note, tie at gate 50] at swing 750 and gate 80: the first note ends at on(1) 5760 + 80 % x 50 % x span(1) 1920 = 6528, the second at 13440 + 768 = 14208");
  }
  // a pending on or trigger is cancelled by release-all, and a trigger already emitted is not
  auto pend = [&](const Plant& pl, int panicAt) {
    Session s;
    s.total = 6000; s.plants = pl; s.base.gate = 150; s.base.swing = 750; s.base.pattern = Pattern::User; s.userPattern = userPat({B(), B().trig(2)});
    s.ins = {K(0, "a", 60)};
    if (panicAt >= 0) s.ins.push_back(Pan(panicAt));
    const Run r = run(s, 64);
    struct R { std::vector<std::int64_t> ons, trigs; bool n1ok; };
    R out{{}, {}, n1(r).empty()};
    // later trigger-only steps (H is empty after the Panic) legitimately fire: judge the window around the swung step only
    for (const Rec& e : r.evs) { if (e.a >= 3000) continue; if (e.t == EventType::NoteOn) out.ons.push_back(e.a); if (e.t == EventType::Trigger) out.trigs.push_back(e.a); }
    return out;
  };
  const auto none = pend({}, -1), early = pend({}, 1250), late = pend({}, 1600), planted = pend({"releaseKeepsPending"}, 1250);
  plant(planted.ons.size() > 1 || !planted.trigs.empty(), "release-all detects a pending on / trigger that survives a Panic (the swung step still sounds after it)");
  check(none.ons.size() >= 2 && none.ons[1] == 1500 && !none.trigs.empty() && none.trigs[0] == 1500, "control: without a Panic the swung step 1 (evaluated at 1000) sounds at 1500 and fires lane 2 at 1500");
  check(early.ons == std::vector<std::int64_t>({0}) && early.trigs.empty() && early.n1ok, "known answer: a Panic at 1250, between the evaluation of step 1 (1000) and its swung on (1500), cancels both the pending on and the pending trigger; N1 holds");
  check(late.ons == std::vector<std::int64_t>({0, 1500}) && late.trigs == std::vector<std::int64_t>({1500}) && late.n1ok, "known answer: a Panic at 1600 finds the trigger already emitted: it is not cancelled (SPEC 8)");
}

void testTM1Swing() {
  std::puts("TM1 (swing term): time mode, step k sounds at cdiv((k * step_time_us + swingDelay(k)) * sr, 1e6) after the key press");
  constexpr int U = 125000, PRESS = 1234, NSTEP = 8, DELAY = 41750;   // swing 667: fdiv(125000 * 334, 1000)
  auto sess = [&](const Plant& pl, int sr, int block, double bpm) {
    Session s;
    s.sr = sr; s.total = static_cast<int>(sr * 1.4); s.plants = pl;
    s.base.rateMode = RateMode::Time; s.base.stepTimeUs = U; s.base.gate = 30; s.base.swing = 667;
    auto h = std::make_shared<SimHost>(sr, bpm, 0, true);
    h->set(static_cast<int>(sr * 0.2), std::nullopt, 7.0); h->set(static_cast<int>(sr * 0.35), false); h->set(static_cast<int>(sr * 0.5), true, 0.3, 77.0);   // the host seeks, stops, restarts: all ignored
    s.host = h;
    s.ins = {Kk(PRESS * sr / 8000, 60)};
    return run(s, block);
  };
  auto exact = [&](const Run& r, int sr) {
    const auto on = onsOf(r);
    if (on.size() < static_cast<std::size_t>(NSTEP)) return false;
    for (int i = 0; i < NSTEP; ++i) {
      const std::int64_t tick = static_cast<std::int64_t>(i) * U + (i % 2 == 1 ? DELAY : 0);
      const std::int64_t want = static_cast<std::int64_t>(PRESS) * sr / 8000 + (tick * sr + 999999) / 1000000;
      if (on[static_cast<std::size_t>(i)].a != want || on[static_cast<std::size_t>(i)].tick != tick) return false;
    }
    return true;
  };
  auto bad = [&](const Plant& pl) { for (double bpm : {60.0, 174.0}) if (!exact(sess(pl, 8000, 512, bpm), 8000)) return true; return false; };
  plant(bad({"timeIgnoresSwing"}), "TM1 detects time mode playing every step straight");
  std::string ref;
  bool bsOk = true, srOk = true;
  for (int block : {4096, 512, 64, 7, 1}) { const std::string st = streamOf(sess({}, 8000, block, 174)); if (ref.empty()) ref = st; else bsOk = bsOk && st == ref; }
  for (int sr : {44100, 48000, 96000, 192000}) srOk = srOk && exact(sess({}, sr, 4096, 174), sr);
  check(!bad({}) && bsOk && srOk, "TM1: with swing 667 each odd step k sounds 41,750 us late (cdiv((k * 125000 + delay) * sr, 1e6) samples after the press), at 60 / 174 bpm, identical at block sizes 4096/512/64/7/1 and at 44.1 / 48 / 96 / 192 kHz");
}

// ====================================================================== velocity (SPEC 4.4)
void testVelocity() {
  std::puts("Velocity (SPEC 4.4): played / fixed, accented by the step");
  auto vels = [&](std::function<void(Settings&)> f, std::vector<InEv> ins, std::size_t n, std::vector<Change> cfg = {}) {
    Session s;
    s.total = 20000;
    s.base.gate = 50;
    f(s.base);
    s.cfg = std::move(cfg);
    s.ins = std::move(ins);
    std::vector<int> v;
    for (const Rec& e : onsOf(run(s, 64))) if (v.size() < n) v.push_back(e.vel);
    return join(v);
  };
  auto accent = [](Settings& x) { x.pattern = Pattern::AccentOne; };   // 130 / 85 / 85 / 85 %
  check(vels(accent, {Kk(0, 60, 100)}, 5) == "127,85,85,85,127" && vels(accent, {Kk(0, 60, 60)}, 5) == "78,51,51,51,78", "known answer: accent_one over a played velocity 100 gives 127 (130 % clamps) 85 85 85; over 60 it gives 78 51 51 51 (60 x 85 % = 51, 60 x 130 % = 78)");
  auto fixed = [&](int velFixed) { return [velFixed](Settings& x) { x.velMode = VelMode::Fixed; x.velFixed = velFixed; }; };
  check(vels(fixed(100), {Kk(0, 60, 30)}, 3) == "100,100,100" && vels([&](Settings& x) { fixed(90)(x); accent(x); }, {Kk(0, 60, 30)}, 5) == "117,77,77,77,117",
        "known answer: fixed velocity ignores the played velocity (30 -> 100), and the step accent still scales it: vel_fixed 90 under accent_one is 117 (90 x 130 %) 77 (90 x 85 % = 76.5 rounds up)");
  check(vels(fixed(0), {Kk(0, 60, 30)}, 1) == "1" && vels(fixed(200), {Kk(0, 60, 30)}, 1) == "127" && vels([&](Settings& x) { fixed(200)(x); accent(x); }, {Kk(0, 60, 30)}, 3) == "127,108,108",
        "known answer: vel_fixed is clamped to 1..127 BEFORE the step accent applies (0 -> 1, 200 -> 127, and 200 under accent_one gives 127 x 85 % = 108)");
  check(vels(fixed(100), {Kk(0, 60, 30)}, 5, {C(1500, [](Settings& x) { x.velFixed = 60; })}) == "100,100,60,60,60", "known answer: vel_fixed is continuous: changed at sample 1500 it applies from the next evaluated step (2000), with no restart");
  check(vels([](Settings& x) { x.order = Order::Chord; }, {Kk(0, 60, 30), Kk(0, 64, 60), Kk(0, 67, 90)}, 6) == "30,60,90,30,60,90", "known answer: in played mode every key of a chord step keeps its OWN velocity (30 60 90)");
  check(vels(fixed(100), {Kk(0, 60, 30)}, 3, {C(1500, [](Settings& x) { x.velMode = VelMode::Played; })}) == "100,100,30", "known answer: vel_mode is discrete: played from the next evaluated step (30)");
}

// ====================================================================== SPEC 9: how a change of these settings takes effect
void testTiming() {
  std::puts("SPEC 9: quantize / scale are discrete (next evaluated step, no sounding note moves)");
  { // quantize off -> scale at 1500, the gate is 200 % so the step-1 note is still sounding when the change lands
    Session s = qs(9000, Quantize::Off);
    s.base.gate = 200;
    s.cfg = {C(1500, [](Settings& x) { x.quantize = Quantize::Scale; })};
    s.ins = {Kk(0, 61)};
    const Run r = run(s, 64);
    const Rec second = onsOf(r)[1];
    std::int64_t offTick = -1;
    for (const Rec& e : r.evs) if (e.t == EventType::NoteOff && e.ref == second.ref) offTick = e.tick;
    check(join(keysOf(r, 4)) == "61,61,60,60" && second.key == 61 && offTick == 3840 + 7680 && n1(r).empty(), "known answer: quantize off -> scale at sample 1500 quantizes from step 2 (61 61 60 60); the sounding 61 of step 1 keeps its scheduled off at tick 11520 (T8)");
  }
  { // horde's scale changes from C major to C# major at 1500: the block's scale is read at evaluation
    Session s = qs(9000);
    s.scaleAt = {{1500, Scale{1, static_cast<std::uint16_t>(kCMajor)}}};
    s.ins = {Kk(0, 61)};
    const Run r = run(s, 64);
    check(join(keysOf(r, 4)) == "60,60,61,61" && n1(r).empty(), "known answer: horde's global scale changing to C# major at 1500 applies from the next evaluated step (60 60 61 61)");
  }
  { // scale_source global -> own
    Session s = qs(9000);
    s.cfg = {C(1500, [](Settings& x) { ownScale(x, 1, kCMajor); })};
    s.ins = {Kk(0, 61)};
    check(join(keysOf(run(s, 64), 4)) == "60,60,61,61", "known answer: scale_source global -> own (C# major) at 1500 applies from the next evaluated step");
  }
}

// ====================================================================== sessions
}  // namespace

Session m2aSession(const char* name) {
  const std::string n = name;
  Session s;
  if (n == "scalefeel") {   // quantize in both modes and both sources, global scale changes, degrees (global + per pattern), swing, velocity modes
    s.total = 40000; s.base.gate = 85; s.base.seed = 13; s.base.swing = 640; s.base.rate = Rate::R1_32; s.base.quantize = Quantize::Scale;
    s.base.steps = 1; s.base.stepSize = 2; s.base.stepUnit = StepUnit::Degrees;
    s.scale = Scale{0, static_cast<std::uint16_t>(kCMajor)};
    s.scaleAt = {{10000, Scale{2, static_cast<std::uint16_t>(kMinPent)}}, {26000, Scale{7, static_cast<std::uint16_t>(kCMajor)}}};
    s.userPattern = userPat({B().step(1), B(), B().step(-1).ratchet(2), B().tie().gate(60), B().vel(150).trig(3)}, 2, StepUnit::Degrees);
    s.cfg = {C(3000, [](Settings& x) { x.pattern = Pattern::User; }),
             C(6000, [](Settings& x) { x.quantize = Quantize::ChordRoot; }),
             C(9000, [](Settings& x) { ownScale(x, 9, kMinPent); }),
             C(12000, [](Settings& x) { x.swing = 750; x.order = Order::Bounce; }),
             C(15000, [](Settings& x) { x.quantize = Quantize::Scale; x.scaleSource = ScaleSource::Global; }),
             C(18000, [](Settings& x) { x.stepUnit = StepUnit::Semitones; x.velMode = VelMode::Fixed; x.velFixed = 90; x.pattern = Pattern::TiedLegato; }),
             C(21000, [](Settings& x) { x.stepUnit = StepUnit::Degrees; x.steps = 2; x.stepSize = -3; x.swing = 500; }),
             C(24000, [](Settings& x) { x.pattern = Pattern::User; x.swing = 700; }),
             C(27000, [](Settings& x) { x.quantize = Quantize::Off; }),
             C(30000, [](Settings& x) { x.quantize = Quantize::Scale; x.order = Order::Chord; }),
             C(34000, [](Settings& x) { x.velMode = VelMode::Played; x.order = Order::ChordWalk; })};
    s.ins = {K(0, "a", 61), K(0, "b", 66), K(2500, "c", 70), O(14000, "b"), Pan(23000), K(23500, "d", 64), K(23500, "e", 127), K(25000, "f", 1), O(31000, "a"), K(32000, "g", 59)};
  } else if (n == "ties") {   // quantize ties everywhere: out-of-scale keys, bounce, degrees, swing, tempo changes, a stop / start, a Panic
    s.total = 30000; s.base.quantize = Quantize::Scale; s.base.order = Order::Bounce; s.base.gate = 120; s.base.swing = 700; s.base.rate = Rate::R1_32;
    s.base.steps = 1; s.base.stepSize = 3; s.base.stepUnit = StepUnit::Degrees;
    s.scale = Scale{0, static_cast<std::uint16_t>(kCMajor)};
    auto h = std::make_shared<SimHost>(8000, 120, 0, true);
    h->set(12000, std::nullopt, std::nullopt, 141.3); h->set(20000, false); h->set(24000, true, 1.5);
    s.host = h;
    s.ins = {K(0, "a", 61), K(0, "b", 63), K(300, "c", 66), K(300, "d", 68), K(5000, "e", 70), O(9000, "b"), Pan(21000), K(22000, "f", 61), K(22000, "g", 66)};
  } else {
    std::abort();
  }
  return s;
}

// The M2a features layered on a (seeded) session: quantize, own and global scales, the degree unit, swing and velocity modes. Drawn from
// its OWN generator, so the M1b session underneath keeps exactly the structure M1b gave it (M2a critic L2: the 30 M1b sessions are
// restored as they were; these features live in the new set of 30 in test_m2b.cpp).
void applyM2aDraws(Session& s, std::uint32_t seed) {
  std::mt19937 g(seed * 2654435761u + 0x2A);
  auto rnd = [&](int n) { return static_cast<int>(g() % static_cast<std::uint32_t>(n)); };
  s.base.quantize = static_cast<Quantize>(rnd(3));
  s.base.scaleSource = rnd(2) ? ScaleSource::Own : ScaleSource::Global;
  s.base.scaleRoot = rnd(12);
  for (bool& d : s.base.scaleDeg) d = rnd(3) != 0;   // about a third of the degrees off; the odd all-off scale is a zero mask (quantize disabled)
  s.base.swing = 500 + rnd(251);
  s.base.stepUnit = rnd(2) ? StepUnit::Degrees : StepUnit::Semitones;
  s.scale = Scale{rnd(12), static_cast<std::uint16_t>(g() & 0xFFF)};
  if (s.userPattern && rnd(2)) s.userPattern->stepUnit = StepUnit::Degrees;
  for (auto& ed : s.userEdits) if (rnd(2)) ed.second.stepUnit = StepUnit::Degrees;
  for (int i = rnd(6); i > 0; --i) {
    const int at = 1 + rnd(s.total - 1), what = rnd(7), v = static_cast<int>(g() % 1000);
    s.cfg.push_back(C(at, [what, v](Settings& x) {
      switch (what) {
        case 0: x.quantize = static_cast<Quantize>(v % 3); break;
        case 1: x.scaleSource = v % 2 ? ScaleSource::Own : ScaleSource::Global; break;
        case 2: x.scaleRoot = v % 12; for (int d = 0; d < 12; ++d) x.scaleDeg[d] = ((v * 7 + d * 5) % 3) != 0; break;
        case 3: x.swing = 500 + v % 251; break;
        case 4: x.stepUnit = v % 2 ? StepUnit::Degrees : StepUnit::Semitones; x.steps = v % 3; x.stepSize = v % 15 - 7; break;
        case 5: x.velFixed = 1 + v % 127; x.velMode = v % 2 ? VelMode::Fixed : VelMode::Played; break;
        default: x.quantize = Quantize::Scale; x.order = static_cast<Order>(v % 16); break;
      }
    }));
  }
  for (int i = rnd(3); i > 0; --i) s.scaleAt.emplace_back(1 + rnd(s.total - 1), Scale{rnd(12), static_cast<std::uint16_t>(g() & 0xFFF)});
}

namespace {
void testSessionsG1A1() {
  std::puts("G1 / A1 over the M2a sessions (quantize, degrees, swing, velocity modes, global scale changes)");
  for (const char* name : {"scalefeel", "ties"}) {
    for (bool keyRefs : {false, true}) {
      Session s = m2aSession(name);
      s.keyRefs = keyRefs;
      std::string ref;
      bool same = true;
      std::int64_t events = 0;
      for (int block : {4096, 512, 64, 7, 1}) {
        const Run r = run(s, block);
        const std::string st = streamOf(r);
        if (ref.empty()) { ref = st; events = static_cast<std::int64_t>(r.evs.size()); } else same = same && st == ref;
        same = same && n1(r).empty();
      }
      check(same && events > 80, std::string("G1 / N1: session '") + name + "'" + (keyRefs ? " (id-less refs)" : "") + " is identical at block sizes 4096/512/64/7/1 with one terminal per note (" + std::to_string(events) + " events)");
    }
    Session a = m2aSession(name);
    a.countAllocs = true;
    a.resets = {a.total / 2};
    const Run r = run(a, 64);
    check(r.allocs == 0, std::string("A1: session '") + name + "' makes " + std::to_string(r.allocs) + " operator-new calls inside process() / reset() (quantize, scale and swing changes, degree moves, pending swung ons)");
  }
}
}  // namespace

void runM2aTests() {
  testQ1();
  testQ2Q5();
  testQ3();
  testQ4();
  testCollisions();
  testX1();
  testST1AndStages();
  testSwing();
  testTM1Swing();
  testVelocity();
  testTiming();
  testSessionsG1A1();
}

}  // namespace arp_test
