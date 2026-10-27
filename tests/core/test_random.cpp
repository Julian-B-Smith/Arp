// test_random.cpp - RN1 (SPEC 4.4, 4.9, 5.2, 11; D-036): velocity and gate randomness with a shared curve.
//   1. byte-identity at 0:   with vel_random = gate_random = 0 the output is the core-without-it's. The proof is the COMMITTED digests (generated
//                            from 0ef0903, a core that has no randomness; the suite's own "stream digest" check and the bench digest); here also
//                            a same-session comparison and a check that the digest file still says where it was cut.
//   2. known answers:        exact velocity and length of every note of several sessions against a reference written HERE from SPEC 4.4 / 4.9 /
//                            5.2 (integer math, own floor division) and, for a few notes, against literals computed by a separate Python
//                            implementation of the same text (tools/lab_smoke.mjs checks the same literals against the lab's JS).
//   3. bounds, 4. the curve: |change| within the amount; at curve 0 a flat histogram, at 100 a variance of about a quarter.
//   5. chord-stage notes are never randomised.   6. N1 / G1 / G4 / A1 over a session with all three settings non-zero, changing mid-session,
//   and a seeded fuzz of NEW sessions.
// Same charter as the other suites: each detector is preceded by its plant (a plant that does not fire is a FAIL); "known answer" assertions carry
// no plant and say so. NONE of these sessions is recorded in the committed digest: they run through runOnce(), never run(), because the digest
// was cut before the feature existed and must not move.
//
// 48 kHz unless a check says otherwise: at 48 kHz one tick is 1.56 samples, so every note's scheduled off tick is reported (an off less than a
// sample after its on is deferred and carries no tick, SPEC 1.1). 120 bpm: a 1/16 step is 3,840 ticks = 6,000 samples.
#include "test_random.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "arp/draw.h"
#include "arp/pattern.h"
#include "harness.h"

#ifndef ARP_DIGEST_FILE
#error "ARP_DIGEST_FILE must name tests/core/stream_digest.txt (tests/core/CMakeLists.txt defines it)"
#endif

namespace arp_test {
namespace {

Change C(int at, std::function<void(Settings&)> f) { return Change{at, std::move(f)}; }
constexpr std::int64_t kL16 = 3840;   // a 1/16 step in ticks (TPQ 15360)

// ---------------------------------------------------------------------------------------------------- the reference: SPEC 4.4 / 4.9 / 5.2 as written
// Plain int64 and its own floor division: nothing here calls the core's arith.h or randomU. The draw is SPEC 10's, arp::draw (draw.h), whose
// known answers D1 pins against a separate Python implementation.
std::int64_t fl(std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }   // floor, b > 0
std::int64_t clampv(std::int64_t x, std::int64_t lo, std::int64_t hi) { return x < lo ? lo : x > hi ? hi : x; }

struct Knobs { std::uint32_t seed = 0, epoch = 0; int velR = 0, gateR = 0, curve = 0; };

// SPEC 4.9: U in permille, one draw split into its bytes b0..b3 (low first), U1 uniform, U4 the mean of four, blended by the curve.
std::int64_t refU(const Knobs& k, std::int64_t x, unsigned lane) {
  const std::uint32_t r = arp::draw(k.seed, x, lane, k.epoch);
  const std::int64_t b0 = r & 255u, b1 = (r >> 8) & 255u, b2 = (r >> 16) & 255u, b3 = (r >> 24) & 255u;
  const std::int64_t u1 = fl((2 * b0 - 255) * 1000, 255);
  const std::int64_t u4 = fl((2 * (b0 + b1 + b2 + b3) - 1020) * 1000, 1020);
  return fl(u1 * (100 - k.curve) + u4 * k.curve, 100);
}

// One arp note as the spec describes it: where it is (kAbs, ratchet sub-note, index n in style order) and what the step says about it.
struct Shape {
  int r = 1;          // ratchets of its step
  int stepVel = 100;  // the step's vel %
  int stepGate = 100; // the step's gate %
  int t = 0;          // consecutive tie steps that follow it (the LAST sub-note runs on through them)
  int tailGate = 100; // gate % of the last tied step
};
struct RefNote { int vel = 0; std::int64_t len = 0; bool velLo = false, velHi = false, lenMin = false, capped = false, tied = false; };

// base: the source key's velocity; gate: the setting (1..200); L: the step length in ticks, straight swing, so span = L everywhere.
RefNote refNote(const Knobs& k, std::int64_t kAbs, int sub, int n, int base, int gate, std::int64_t L, const Shape& sh) {
  RefNote o;
  const std::int64_t x = kAbs * 1024 + sub * 256 + n;
  std::int64_t v = clampv(fl(static_cast<std::int64_t>(base) * sh.stepVel + 50, 100), 1, 127);   // SPEC 4.4
  if (k.velR > 0) {
    const std::int64_t raw = v + fl(v * k.velR * refU(k, x, 3) + 50000, 100000);
    o.velLo = raw < 1;
    o.velHi = raw > 127;
    v = clampv(raw, 1, 127);
  }
  o.vel = static_cast<int>(v);
  if (sh.t > 0 && sub == sh.r - 1) {   // SPEC 5.2 H3: off = on(kAbs + t) + tail; the same factor applies to the tail term
    o.tied = true;
    std::int64_t tail = std::max<std::int64_t>(1, fl(L * gate * sh.tailGate, 10000));
    if (k.gateR > 0) {
      const std::int64_t raw = tail + fl(tail * k.gateR * refU(k, x, 4), 100000);
      o.lenMin = raw < 1;
      tail = std::max<std::int64_t>(1, raw);
    }
    o.len = sh.t * L - fl(sub * L, sh.r) + tail;   // from this sub-note's on to the end of the last tied step's gate
    return o;
  }
  const std::int64_t span = sh.r == 1 ? L : fl(L, sh.r);
  std::int64_t len = std::max<std::int64_t>(1, fl(span * gate * sh.stepGate, 10000));   // the gate-derived length, before the cap
  if (k.gateR > 0) {
    const std::int64_t raw = len + fl(len * k.gateR * refU(k, x, 4), 100000);
    o.lenMin = raw < 1;
    len = std::max<std::int64_t>(1, raw);
  }
  if (sh.r > 1 && len > span) { len = span; o.capped = true; }   // the ratchet cap, after the randomness
  o.len = len;
  return o;
}

// ---------------------------------------------------------------------------------------------------- reading a run
struct Obs { int key = 0, vel = 0, sub = -1, n = 0; std::int64_t on = 0, len = 0, kAbs = 0; };

// Every arp-stream note of the run with its scheduled length (off tick - on tick), its step, ratchet sub-note and index n. `ratchetsAt(kAbs)` is
// the session's own knowledge of its pattern; n is the rank of the key among the notes of one (step, sub-note), ascending: every style these
// sessions use (chord, user pick = chord, up) lists its notes in ascending key order. A note whose off carried no tick is reported in `err`.
std::vector<Obs> observe(const Run& r, std::int64_t L, const std::function<int(std::int64_t)>& ratchetsAt, std::string& err) {
  std::map<std::string, std::int64_t> offTick;
  for (const Rec& e : r.evs)
    if (e.t == EventType::NoteOff) offTick[std::to_string(e.epoch) + "/" + refStr(e.ref)] = e.tick;
  std::vector<Obs> v;
  for (const Rec& e : r.evs) {
    if (e.t != EventType::NoteOn || e.stream != Stream::Arp) continue;
    Obs o;
    o.key = e.key;
    o.vel = e.vel;
    o.on = e.tick;
    o.kAbs = fl(e.tick, L);
    const int rr = ratchetsAt(o.kAbs);
    for (int i = 0; i < rr; ++i) if (o.kAbs * L + fl(i * L, rr) == e.tick) o.sub = i;
    const auto it = offTick.find(std::to_string(e.epoch) + "/" + refStr(e.ref));
    if (it == offTick.end() || it->second == kNoTick) { if (err.empty()) err = "a note has no scheduled off tick (key " + std::to_string(e.key) + ", on tick " + std::to_string(e.tick) + ")"; continue; }
    if (o.sub < 0) { if (err.empty()) err = "on tick " + std::to_string(e.tick) + " is no ratchet sub-note of its step"; continue; }
    o.len = it->second - e.tick;
    v.push_back(o);
  }
  std::map<std::pair<std::int64_t, int>, std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < v.size(); ++i) groups[{v[i].kAbs, v[i].sub}].push_back(i);
  for (auto& g : groups) {
    std::sort(g.second.begin(), g.second.end(), [&](std::size_t a, std::size_t b) { return v[a].key < v[b].key; });
    for (std::size_t i = 0; i < g.second.size(); ++i) v[g.second[i]].n = static_cast<int>(i);
  }
  return v;
}

struct Tally { int notes = 0, velLo = 0, velHi = 0, lenMin = 0, capped = 0, tied = 0, velMoved = 0, lenMoved = 0; };

// "" when every note's velocity and length equal the reference's, else the first disagreement. `shapeAt(kAbs)` describes the step; `inVel[key]` the held key's velocity.
std::string verify(const Run& run, const Knobs& k, std::int64_t L, int gate, const std::function<Shape(std::int64_t)>& shapeAt, const std::map<int, int>& inVel, Tally& t) {
  std::string err;
  const std::vector<Obs> obs = observe(run, L, [&](std::int64_t kAbs) { return shapeAt(kAbs).r; }, err);
  if (!err.empty()) return err;
  Knobs off = k;
  off.velR = 0;
  off.gateR = 0;
  for (const Obs& o : obs) {
    const Shape sh = shapeAt(o.kAbs);
    const RefNote want = refNote(k, o.kAbs, o.sub, o.n, inVel.at(o.key), gate, L, sh);
    const RefNote plain = refNote(off, o.kAbs, o.sub, o.n, inVel.at(o.key), gate, L, sh);
    ++t.notes;
    t.velLo += want.velLo; t.velHi += want.velHi; t.lenMin += want.lenMin; t.capped += want.capped; t.tied += want.tied;
    t.velMoved += want.vel != plain.vel; t.lenMoved += want.len != plain.len;
    if (o.vel != want.vel || o.len != want.len)
      return "step " + std::to_string(o.kAbs) + " sub " + std::to_string(o.sub) + " n " + std::to_string(o.n) + " (key " + std::to_string(o.key) + "): core vel " + std::to_string(o.vel) + " len " + std::to_string(o.len) +
             ", spec vel " + std::to_string(want.vel) + " len " + std::to_string(want.len);
  }
  return obs.empty() ? "no notes" : "";
}

// ---------------------------------------------------------------------------------------------------- sessions (48 kHz, 120 bpm, keys held from sample 0 for the whole run)
struct Sess { Session s; std::function<Shape(std::int64_t)> shape; std::map<int, int> inVel; Knobs k; int gate = 80; std::int64_t L = kL16; };

Sess chordSess(int curve, int velR, int gateR, std::uint32_t seed) {   // order chord: all three keys every step; n = 0, 1, 2 by key; velocities at both clamps
  Sess c;
  c.k = Knobs{seed, 0, velR, gateR, curve};
  c.s.sr = 48000; c.s.total = 40 * 6000 - 1000;
  c.s.base.order = Order::Chord; c.s.base.seed = seed; c.s.base.velRandom = velR; c.s.base.gateRandom = gateR; c.s.base.randomCurve = curve;
  c.s.ins = {K(0, "a", 60, 127), K(0, "b", 64, 1), K(0, "c", 67, 64)};
  c.inVel = {{60, 127}, {64, 1}, {67, 64}};
  c.shape = [](std::int64_t) { return Shape{}; };
  return c;
}

// A user pattern: step 0 chord x2 ratchets at accent 150 and gate 150, step 1 chord x3 at gate 200, step 2 chord x4: ratchet caps, the accent, three ratchet counts.
Sess ratchetSess() {
  Sess c;
  c.k = Knobs{5, 0, 70, 100, 50};
  c.gate = 60;
  c.s.sr = 48000; c.s.total = 36 * 6000 - 1000;
  c.s.base.pattern = Pattern::User; c.s.base.gate = 60; c.s.base.seed = 5; c.s.base.velRandom = 70; c.s.base.gateRandom = 100; c.s.base.randomCurve = 50;
  UserPattern u;
  u.length = 3;
  const int rat[3] = {2, 3, 4}, sv[3] = {150, 100, 100}, sg[3] = {150, 200, 100};
  for (int i = 0; i < 3; ++i) { u.steps[i].pick = kPickChord; u.steps[i].ratchet = rat[i]; u.steps[i].vel = sv[i]; u.steps[i].gate = sg[i]; }
  c.s.userPattern = u;
  c.s.ins = {K(0, "a", 60, 127), K(0, "b", 64, 1), K(0, "c", 67, 64), K(0, "d", 72, 100)};
  c.inVel = {{60, 127}, {64, 1}, {67, 64}, {72, 100}};
  c.shape = [=](std::int64_t kAbs) { const int j = static_cast<int>(kAbs % 3); return Shape{rat[j], sv[j], sg[j], 0, 100}; };
  return c;
}

// s0 chord (tied to s1, gate 150), s1 tie, s2 chord x2 ratchets (its LAST sub-note tied to s3, gate 50), s3 tie: the tie tail, per note of a chord step, per sub-note.
Sess tieSess() {
  Sess c;
  c.k = Knobs{21, 0, 50, 100, 100};
  c.gate = 80;
  c.s.sr = 48000; c.s.total = 80 * 6000 - 1000;
  c.s.base.pattern = Pattern::User; c.s.base.seed = 21; c.s.base.velRandom = 50; c.s.base.gateRandom = 100; c.s.base.randomCurve = 100;
  UserPattern u;
  u.length = 4;
  u.steps[0].pick = kPickChord;
  u.steps[1].tie = true; u.steps[1].gate = 150;
  u.steps[2].pick = kPickChord; u.steps[2].ratchet = 2;
  u.steps[3].tie = true; u.steps[3].gate = 50;
  c.s.userPattern = u;
  c.s.ins = {K(0, "a", 60, 100), K(0, "b", 64, 100), K(0, "c", 67, 100)};
  c.inVel = {{60, 100}, {64, 100}, {67, 100}};
  c.shape = [](std::int64_t kAbs) { return (kAbs % 4) < 2 ? Shape{1, 100, 100, 1, 150} : Shape{2, 100, 100, 1, 50}; };
  return c;
}

// 1/64 steps (960 ticks), gate 1 % x step gate 1 %: the gate-derived length is 0 and floors to 1 tick, so the 1-tick minimum is the common case. 192 kHz so a
// 1-tick note is still 6 samples long and its off tick is reported.
Sess minSess() {
  Sess c;
  c.k = Knobs{8, 0, 100, 100, 0};
  c.gate = 1;
  c.L = 960;
  c.s.sr = 192000; c.s.total = 12 * 24000 - 4000;
  c.s.base.pattern = Pattern::User; c.s.base.rate = Rate::R1_64; c.s.base.gate = 1; c.s.base.seed = 8; c.s.base.velRandom = 100; c.s.base.gateRandom = 100;
  UserPattern u;
  u.length = 2;
  u.steps[0].pick = kPickChord; u.steps[0].gate = 1;
  u.steps[1].pick = kPickChord; u.steps[1].gate = 200; u.steps[1].ratchet = 4;   // 1 % x 200 % of a 240-tick sub-note: 4 ticks, so a draw at -1000 reaches the minimum
  c.s.userPattern = u;
  c.s.ins = {K(0, "a", 60, 50), K(0, "b", 64, 50)};
  c.inVel = {{60, 50}, {64, 50}};
  c.shape = [](std::int64_t kAbs) { return (kAbs % 2) == 0 ? Shape{1, 100, 1, 0, 100} : Shape{4, 100, 200, 0, 100}; };
  return c;
}

// the literal sessions: one key, order up, 1/16, gate 80: n = 0, sub = 0, x = kAbs * 1024
Sess litSess(std::uint32_t seed, int curve, int velR, int gateR) {
  Sess c;
  c.k = Knobs{seed, 0, velR, gateR, curve};
  c.s.sr = 48000; c.s.total = 16 * 6000 - 1000;
  c.s.base.seed = seed; c.s.base.velRandom = velR; c.s.base.gateRandom = gateR; c.s.base.randomCurve = curve;
  c.s.ins = {K(0, "a", 60, 100)};
  c.inVel = {{60, 100}};
  c.shape = [](std::int64_t) { return Shape{}; };
  return c;
}

Run play(const Sess& c, int block = 4096, const Plant& pl = {}) {
  Session s = c.s;
  s.plants = pl;
  return runOnce(s, block);
}

std::string joinI(const std::vector<std::int64_t>& v) {
  std::string s;
  for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]);
  return s;
}

// ---------------------------------------------------------------------------------------------------- 1. byte-identity at 0
// The revision a digest file says it was cut from ("generated from core/ at commit <rev>:" in its header), "" when it names none.
std::string digestCutFrom(const std::string& text) {
  const std::string key = "generated from core/ at commit ";
  const std::size_t at = text.find(key);
  return at == std::string::npos ? "" : text.substr(at + key.size(), 7);
}
std::string slurp(const char* path) {
  std::string out;
  if (std::FILE* f = std::fopen(path, "r")) {
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
  }
  return out;
}

void testByteIdentity() {
  std::puts("RN1: at vel_random = gate_random = 0 the output is byte-identical to the core without the feature");
  // The committed digests ARE the check: tests/core/stream_digest.txt and stream_digest_bench.txt were generated from 0ef0903, which has no
  // randomness, and are NOT regenerated for this change; the suite's "stream digest" check (every run of the suite, on both int128 paths) and
  // tools/core_test.sh's bench digest compare this core to them. What this test adds: the digests still say where they were cut, and the
  // curve alone changes nothing.
  plant(digestCutFrom("# generated from core/ at commit 3f76354: core/src/core.cpp sha256 0") != "0ef0903", "a digest header naming another commit is not taken for the pre-randomness one");
  const std::string cut = digestCutFrom(slurp(ARP_DIGEST_FILE));
  check(cut == "0ef0903", "the committed stream digest was cut from 0ef0903 (a core without randomness) and has not been regenerated: header says \"" + cut + "\"");
  std::vector<Sess> all = {chordSess(0, 0, 0, 11), ratchetSess(), tieSess(), minSess()};
  int sameRuns = 0, diffRuns = 0;
  for (Sess& c : all) {
    Session base = c.s;
    base.base.velRandom = 0; base.base.gateRandom = 0; base.base.randomCurve = 0;
    Session curveOnly = base;
    curveOnly.base.randomCurve = 100;   // the curve is the shape of the dice and does nothing while both amounts are 0
    Session live = c.s;
    live.base.velRandom = 100; live.base.gateRandom = 100;
    const Run a = runOnce(base, 4096), b = runOnce(curveOnly, 4096), d = runOnce(live, 4096);
    diffRuns += d.digestStream != a.digestStream;
    sameRuns += a.digestStream == b.digestStream && a.digestCounters == b.digestCounters && a.nEvents > 50;
  }
  plant(diffRuns == static_cast<int>(all.size()), "the same sessions with both amounts at 100 are NOT byte-identical: the comparison can see randomness (" + std::to_string(diffRuns) + " of " + std::to_string(all.size()) + " differ)");
  check(sameRuns == static_cast<int>(all.size()), "known answer: with both amounts at 0 the stream, step records and counters hash identically whatever random_curve is (" + std::to_string(sameRuns) + " of " + std::to_string(all.size()) + " sessions)");
}

// ---------------------------------------------------------------------------------------------------- 2. known answers
void testKnownAnswers() {
  std::puts("RN1: known answers: the exact velocity and length of every note against the spec's formulas, and against Python literals");
  // -- literals computed by tools-independent Python (a separate implementation of SPEC 10 and 4.9): one key, order up, 1/16, gate 80 % (3,072 ticks), steps 0..15
  struct Lit { std::uint32_t seed; int curve, amount; std::vector<std::int64_t> vel, len; };
  const std::vector<Lit> lits = {
      {0, 0, 100, {127, 65, 86, 127, 90, 11, 87, 19, 127, 119, 127, 88, 63, 59, 127, 127}, {4985, 2479, 4144, 4647, 4816, 4359, 5563, 3876, 746, 672, 1661, 577, 1010, 623, 1492, 2433}},
      {7, 100, 100, {127, 97, 86, 127, 108, 72, 80, 70, 63, 127, 106, 52, 106, 127, 104, 73}, {2967, 3594, 2208, 2433, 2472, 2915, 2171, 1612, 2282, 3342, 3173, 2617, 1858, 3612, 2780, 4125}},
      {12345, 50, 60, {123, 59, 72, 90, 77, 103, 74, 127, 70, 97, 123, 112, 126, 127, 89, 125}, {2472, 3177, 3427, 3505, 2568, 2045, 2015, 3051, 3698, 3173, 3965, 3204, 2622, 2780, 2506, 2683}},
  };
  for (const Lit& l : lits) {
    const Sess c = litSess(l.seed, l.curve, l.amount, l.amount);
    const Run r = play(c);
    std::string err;
    const std::vector<Obs> obs = observe(r, kL16, [](std::int64_t) { return 1; }, err);
    std::vector<std::int64_t> vel, len;
    for (std::size_t i = 0; i < obs.size() && i < 16; ++i) { vel.push_back(obs[i].vel); len.push_back(obs[i].len); }
    // plant: the literals of ANOTHER curve are not these
    Lit other = l;
    other.curve = l.curve == 0 ? 100 : 0;
    const Run ro = play(litSess(l.seed, other.curve, l.amount, l.amount));
    std::string err2;
    const std::vector<Obs> obs2 = observe(ro, kL16, [](std::int64_t) { return 1; }, err2);
    std::vector<std::int64_t> vel2;
    for (std::size_t i = 0; i < obs2.size() && i < 16; ++i) vel2.push_back(obs2[i].vel);
    plant(vel2 != l.vel, "the literal velocities (seed " + std::to_string(l.seed) + ", curve " + std::to_string(l.curve) + ") are not what curve " + std::to_string(other.curve) + " plays");
    check(err.empty() && !obs.empty() && vel == l.vel && len == l.len && obs[0].kAbs == 0,
          "known answer (Python literals): seed " + std::to_string(l.seed) + ", curve " + std::to_string(l.curve) + ", amounts " + std::to_string(l.amount) + ": the first 16 steps play velocity [" + joinI(vel) + "] and length [" + joinI(len) + "] ticks" + (err.empty() ? "" : " - " + err));
  }
  {   // a chord step's per-note index and the ratchet sub-note: step 5 of "chord x2 ratchets" (3 keys): x = 5*1024 + sub*256 + n
    Sess c;
    c.k = Knobs{2, 0, 100, 100, 25};
    c.gate = 60;
    c.s.sr = 48000; c.s.total = 8 * 6000;
    c.s.base.pattern = Pattern::User; c.s.base.gate = 60; c.s.base.seed = 2; c.s.base.velRandom = 100; c.s.base.gateRandom = 100; c.s.base.randomCurve = 25;
    UserPattern u;
    u.length = 1;
    u.steps[0].pick = kPickChord; u.steps[0].ratchet = 2; u.steps[0].gate = 150;
    c.s.userPattern = u;
    c.s.ins = {K(0, "a", 60, 100), K(0, "b", 64, 100), K(0, "c", 67, 100)};
    c.inVel = {{60, 100}, {64, 100}, {67, 100}};
    c.shape = [](std::int64_t) { return Shape{2, 100, 150, 0, 100}; };
    const Run r = play(c);
    std::string err;
    const std::vector<Obs> obs = observe(r, kL16, [](std::int64_t) { return 2; }, err);
    // (sub, n) -> velocity, length from the Python literals
    const std::map<std::pair<int, int>, std::pair<int, std::int64_t>> want = {{{0, 0}, {127, 1468}}, {{0, 1}, {29, 1920}}, {{0, 2}, {94, 1441}}, {{1, 0}, {77, 1920}}, {{1, 1}, {123, 1118}}, {{1, 2}, {96, 1242}}};
    std::string got, bad;
    int seen = 0;
    for (const Obs& o : obs) {
      if (o.kAbs != 5) continue;
      ++seen;
      const auto it = want.find({o.sub, o.n});
      got += "(" + std::to_string(o.sub) + "," + std::to_string(o.n) + ")=" + std::to_string(o.vel) + "/" + std::to_string(o.len) + " ";
      if (it == want.end() || it->second.first != o.vel || it->second.second != o.len) bad = "wrong";
    }
    plant(want.at({0, 0}) != want.at({0, 1}) && want.at({0, 1}) != want.at({1, 1}), "the three notes of a chord step and its two sub-notes draw differently: the literals differ per (sub, n)");
    check(err.empty() && seen == 6 && bad.empty(), "known answer (Python literals): step 5 of a chord x2 ratchets step, seed 2, curve 25: (sub,n)=vel/len " + got + "(vel clamped at 127, a length capped at the ratchet span 1920)" + (err.empty() ? "" : " - " + err));
  }
  {   // the tie tail, per note of a chord step and per sub-note (SPEC 5.2): literals of a Python implementation of the tail rule
    const Sess c = tieSess();
    const Run r = play(c);
    std::string err;
    const std::vector<Obs> obs = observe(r, kL16, [&](std::int64_t k) { return c.shape(k).r; }, err);
    // kAbs -> sub -> {n: (length, velocity)}
    struct W { std::int64_t kAbs; int sub; std::int64_t len[3]; int vel[3]; };
    const W ws[] = {{0, 0, {11000, 8779, 8535}, {74, 93, 120}}, {2, 1, {3256, 3422, 3873}, {82, 103, 115}}, {4, 0, {10217, 7199, 10106}, {114, 106, 113}}, {6, 1, {3620, 2500, 3566}, {100, 114, 113}}};
    int matched = 0;
    for (const W& w : ws)
      for (const Obs& o : obs)
        if (o.kAbs == w.kAbs && o.sub == w.sub) matched += o.len == w.len[o.n] && o.vel == w.vel[o.n];
    plant(ws[0].len[0] != ws[0].len[1] && ws[1].len[0] != ws[1].len[2], "the tails of a chord step's notes differ note to note: the literals are per note");
    check(err.empty() && matched == 12, "known answer (Python literals): the tied tail of every note of steps 0, 2, 4 and 6 of the tie pattern (a plain tie, and the last sub-note of a x2 ratchet) is the spec's: " + std::to_string(matched) + " of 12 lengths and velocities" + (err.empty() ? "" : " - " + err));
  }
  // -- the formula reference over whole sessions: velocity, length, clamps, the 1-tick minimum, the ratchet cap, ties
  struct Named { const char* name; Sess c; };
  std::vector<Named> sessions;
  sessions.push_back({"chord order, curve 0, velocities 127 / 1 / 64", chordSess(0, 100, 100, 11)});
  sessions.push_back({"chord order, curve 40, amounts 35 / 80", chordSess(40, 35, 80, 12)});
  sessions.push_back({"user pattern chord x2 / x3 / x4 ratchets with accent and step gate", ratchetSess()});
  sessions.push_back({"ties: a plain tie and a ratcheted last sub-note", tieSess()});
  sessions.push_back({"1-tick minimum at 192 kHz", minSess()});
  for (Named& n : sessions) {
    Tally t;
    const Run r = play(n.c);
    const std::string good = verify(r, n.c.k, n.c.L, n.c.gate, n.c.shape, n.c.inVel, t);
    // plant: the same run against a reference whose curve is off by one notch (or whose amount is wrong) disagrees somewhere
    Tally tp;
    Sess wrong = n.c;
    wrong.k.curve = wrong.k.curve == 100 ? 99 : wrong.k.curve + 1;
    const std::string bad = verify(r, wrong.k, n.c.L, n.c.gate, n.c.shape, n.c.inVel, tp);
    plant(!bad.empty(), std::string("a reference with the wrong curve (") + std::to_string(wrong.k.curve) + " for " + std::to_string(n.c.k.curve) + ") disagrees with the core on '" + n.name + "': " + bad);
    check(good.empty() && t.notes > 100, std::string("known answer (spec formulas): '") + n.name + "': " + std::to_string(t.notes) + " notes, every velocity and length equals the reference (velocity moved on " + std::to_string(t.velMoved) + ", length on " + std::to_string(t.lenMoved) +
          "; clamp at 1: " + std::to_string(t.velLo) + ", at 127: " + std::to_string(t.velHi) + ", length floored to 1 tick: " + std::to_string(t.lenMin) + ", ratchet cap: " + std::to_string(t.capped) + ", tied tails: " + std::to_string(t.tied) + ")" + (good.empty() ? "" : " - " + good));
    if (std::string(n.name).find("curve 0, velocities") == 0) check(t.velLo > 5 && t.velHi > 5, "the clamps at velocity 1 and 127 are both exercised (" + std::to_string(t.velLo) + " and " + std::to_string(t.velHi) + " notes)");
    if (std::string(n.name).find("user pattern") == 0) check(t.capped > 20 && t.lenMoved > 20, "the ratchet cap is exercised after the randomness (" + std::to_string(t.capped) + " notes capped)");
    if (std::string(n.name).find("1-tick") == 0) check(t.lenMin > 20, "the 1-tick minimum is exercised (" + std::to_string(t.lenMin) + " notes floored)");
    if (std::string(n.name).find("ties") == 0) check(t.tied > 40, "tied tails are exercised (" + std::to_string(t.tied) + " notes)");
  }
}

// ---------------------------------------------------------------------------------------------------- 3. bounds
void testBounds() {
  std::puts("RN1: bounds: a note never moves by more than the amount");
  // |change| <= amount x value, with the rounding the formulas carry: the velocity rounds half up, so 100 |dvel| <= vel x amount + 50; the length floors, so
  // 100 |dlen| <= len x amount + 99. `claimed` is the amount the CHECKER believes is set (the plant gives it a smaller one than the core was run with).
  auto violations = [](const Run& r, const std::map<int, int>& inVel, std::int64_t len0, int claimedVel, int claimedGate, int& notes) {   // len0: also the step length (gate 100 %)
    std::string err;
    int bad = 0;
    notes = 0;
    for (const Obs& o : observe(r, len0, [](std::int64_t) { return 1; }, err)) {
      ++notes;
      const int v0 = inVel.at(o.key);
      bad += 100 * std::abs(o.vel - v0) > v0 * claimedVel + 50;
      bad += 100 * std::abs(o.len - len0) > len0 * claimedGate + 99;
    }
    return err.empty() ? bad : 1 << 20;
  };
  for (int amount : {10, 40, 100}) {
    for (int curve : {0, 100}) {
      Sess c = chordSess(curve, amount, amount, 31 + static_cast<std::uint32_t>(amount));
      c.s.total = 80 * 6000 - 1000;
      c.s.base.gate = 100;   // len0 = 3,840
      c.s.ins = {K(0, "a", 60, 100), K(0, "b", 64, 60), K(0, "c", 67, 30), K(0, "d", 71, 20)};
      c.inVel = {{60, 100}, {64, 60}, {67, 30}, {71, 20}};
      const Run r = play(c);
      int notes = 0;
      if (amount == 100 && curve == 0) {
        const int planted = violations(r, c.inVel, kL16, 10, 10, notes);
        plant(planted > 20, "a checker told the amount is 10 % finds the core's 100 % notes out of bounds (" + std::to_string(planted) + " violations)");
      }
      const int bad = violations(r, c.inVel, kL16, amount, amount, notes);
      check(bad == 0 && notes >= 300, "bounds: amount " + std::to_string(amount) + " %, curve " + std::to_string(curve) + ": all " + std::to_string(notes) + " notes within +-" + std::to_string(amount) + " % in velocity and length (rounding included)" + (bad ? " - " + std::to_string(bad) + " violations" : ""));
    }
  }
}

// ---------------------------------------------------------------------------------------------------- 4. the curve: distribution
// 24,000 notes of one session; the length's U is read back from the output: len = 960 + floor(0.96 U) ticks at gate 100 % of a 1/64 step.
struct Dist { std::array<int, 10> hist{}; double var = 0, lo = 0, hi = 0; int notes = 0; std::string err; };
Dist distribution(int curve, const Plant& pl) {
  Session s;
  s.sr = 48000; s.total = 3000 * 1500;
  s.base.order = Order::Chord; s.base.rate = Rate::R1_64; s.base.gate = 100; s.base.gateRandom = 100; s.base.randomCurve = curve; s.base.seed = 4;
  for (int i = 0; i < 8; ++i) s.ins.push_back(K(0, std::string(1, static_cast<char>('a' + i)).c_str(), 40 + 4 * i, 100));
  s.plants = pl;
  const Run r = runOnce(s, 4096);
  Dist d;
  std::string err;
  const std::vector<Obs> obs = observe(r, 960, [](std::int64_t) { return 1; }, err);
  d.err = err;
  double sum2 = 0;
  d.lo = 1e9; d.hi = -1e9;
  for (const Obs& o : obs) {
    const double u = static_cast<double>(o.len - 960) * 1000.0 / 960.0;   // permille, to within a tick of resolution
    sum2 += u * u;
    d.lo = std::min(d.lo, u); d.hi = std::max(d.hi, u);
    d.hist[static_cast<std::size_t>(std::min(9, static_cast<int>((u + 1000.0) / 200.0)))]++;
    ++d.notes;
  }
  d.var = d.notes ? sum2 / d.notes / 1e6 : 0;
  return d;
}

void testCurve() {
  std::puts("RN1: the curve: uniform at 0, a bell at 100");
  const Dist u0 = distribution(0, {}), u100 = distribution(100, {}), u50 = distribution(50, {}), planted = distribution(100, {"randomIgnoresCurve"});
  // the check, as a function of two distributions: "curve 100's variance is a quarter of curve 0's (0.23-0.27) and curve 0 is flat (every bin within 10 % of even)"
  auto flat = [](const Dist& d) { bool ok = d.notes >= 20000; for (int h : d.hist) ok = ok && std::abs(h - d.notes / 10) * 10 <= d.notes / 10; return ok; };
  auto quarter = [](const Dist& at0, const Dist& at100) { const double q = at100.var / at0.var; return q > 0.23 && q < 0.27; };
  plant(!quarter(u0, planted) && planted.var / u0.var > 0.9, "plant randomIgnoresCurve: curve 100 is read as 0, and its variance is " + std::to_string(planted.var / u0.var) + " of uniform's, not a quarter");
  std::string h;
  for (int x : u0.hist) h += std::to_string(x) + " ";
  check(u0.err.empty() && flat(u0), "distribution: curve 0, " + std::to_string(u0.notes) + " notes: ten equal bins over -1000..1000 hold [" + h + "] (even = " + std::to_string(u0.notes / 10) + "), each within 10 %");
  check(u0.var > 0.31 && u0.var < 0.355 && u0.lo < -980 && u0.hi > 980, "distribution: curve 0 has the variance of a uniform on -1..1 (" + std::to_string(u0.var) + ", 1/3 expected) and reaches both ends (" + std::to_string(static_cast<int>(u0.lo)) + " .. " + std::to_string(static_cast<int>(u0.hi)) + ")");
  check(u100.err.empty() && quarter(u0, u100) && u100.hi <= 1001 && u100.lo >= -1001, "distribution: curve 100 clusters: variance " + std::to_string(u100.var) + " = " + std::to_string(u100.var / u0.var) + " of uniform's (a quarter expected), range " + std::to_string(static_cast<int>(u100.lo)) + " .. " + std::to_string(static_cast<int>(u100.hi)) + ", never beyond +-1000");
  check(u50.var < u0.var && u50.var > u100.var, "distribution: curve 50 sits between the two (variance " + std::to_string(u50.var) + ")");
  const std::int64_t centre0 = u0.hist[4] + u0.hist[5], centre100 = u100.hist[4] + u100.hist[5];
  check(centre100 > 2 * centre0, "distribution: at curve 100 the middle two bins hold " + std::to_string(centre100) + " notes, at curve 0 " + std::to_string(centre0) + ": most land near the setting");
}

// ---------------------------------------------------------------------------------------------------- 5. the chord stage is never randomised
std::string stageStream(const Run& r, bool chordSide) {
  std::vector<std::string> chordRefs;
  for (const Rec& e : r.evs) if (e.t == EventType::NoteOn && e.stream == Stream::Chord) chordRefs.push_back(std::to_string(e.epoch) + "/" + refStr(e.ref));
  std::string out;
  for (const Rec& e : r.evs) {
    if (e.t != EventType::NoteOn && e.t != EventType::NoteOff) continue;
    const std::string k = std::to_string(e.epoch) + "/" + refStr(e.ref);
    const bool isChord = std::find(chordRefs.begin(), chordRefs.end(), k) != chordRefs.end();
    if (isChord != chordSide) continue;
    out += std::to_string(e.a) + (e.t == EventType::NoteOn ? " on " : " off ") + std::to_string(e.key) + " v" + std::to_string(e.vel) + " t" + std::to_string(e.tick) + "\n";
  }
  return out;
}
Session stageSession(int velR, int gateR, int curve, const Plant& pl) {
  Session s;
  s.sr = 8000; s.total = 30000; s.plants = pl;
  s.base.chordEnabled = true; s.base.order = Order::Up; s.base.gate = 100;   // the arp plays the chord set; the stage holds the keys' own notes
  s.base.velRandom = velR; s.base.gateRandom = gateR; s.base.randomCurve = curve; s.base.seed = 77;
  s.cfg = {C(9500, [](Settings&) {})};
  s.ins = {K(0, "a", 60, 90), K(0, "b", 67, 40), O(20500, "a"), K(21500, "c", 62, 110)};
  return s;
}
void testChordStage() {
  std::puts("RN1: chord-stage notes follow the hands: never randomised");
  const Run off = runOnce(stageSession(0, 0, 0, {}), 64), on = runOnce(stageSession(100, 100, 40, {}), 64), planted = runOnce(stageSession(100, 100, 40, {"randomChordStage"}), 64);
  const std::string chordOff = stageStream(off, true), chordOn = stageStream(on, true), chordPlanted = stageStream(planted, true);
  plant(chordOff != chordPlanted && !chordOff.empty(), "plant randomChordStage: chord-stage notes randomised, and the chord-stage stream differs from the unrandomised one");
  check(!chordOff.empty() && chordOff == chordOn, "chord-stage notes are identical with vel_random = gate_random = 100 (same samples, keys, velocities, ticks, offs): " + std::to_string(std::count(chordOn.begin(), chordOn.end(), '\n')) + " chord-stage events");
  check(stageStream(off, false) != stageStream(on, false), "and the arp's own notes in the same run DO change (the comparison is not vacuous)");
  check(n1(on).empty(), "N1 holds on the chord-stage session with randomness on");
}

// ---------------------------------------------------------------------------------------------------- 6. N1 / G1 / G4 / A1 over a session with all three non-zero and changing
Session mixSession(const Plant& pl) {
  Session s;
  s.sr = 8000; s.total = 48000; s.plants = pl;
  s.base.pattern = Pattern::User; s.base.order = Order::Up; s.base.gate = 90; s.base.seed = 9;
  s.base.velRandom = 100; s.base.gateRandom = 100; s.base.randomCurve = 0;
  UserPattern u;
  u.length = 5;
  u.steps[0].ratchet = 2;
  u.steps[1].tie = true; u.steps[1].gate = 120;
  u.steps[2].pick = kPickChord; u.steps[2].vel = 140; u.steps[2].ratchet = 3;
  u.steps[3].gate = 40;
  u.steps[4].ratchet = 4; u.steps[4].gate = 200;
  s.userPattern = u;
  auto h = std::make_shared<SimHost>(8000, 120, 0, true);
  h->set(20500, std::nullopt, std::nullopt, 133.7);
  s.host = h;
  s.cfg = {C(9500, [](Settings& x) { x.velRandom = 40; x.randomCurve = 60; }), C(14500, [](Settings& x) { x.gateRandom = 30; x.order = Order::Bounce; }),
           C(21500, [](Settings& x) { x.randomCurve = 100; x.velRandom = 80; }), C(26500, [](Settings& x) { x.pattern = Pattern::Gallop; x.gateRandom = 100; }),
           C(31500, [](Settings& x) { x.rate = Rate::R1_16T; x.randomCurve = 20; }), C(36500, [](Settings& x) { x.velRandom = 0; x.gateRandom = 55; })};
  s.ins = {K(0, "a", 60, 100), K(0, "b", 64, 70), K(0, "c", 67, 110), K(6500, "d", 72, 90), O(18000, "b"), K(24500, "e", 55, 80), O(40500, "a")};
  return s;
}
Session atRate(Session s, int sr) {   // the same musical script at another rate (cf. test_core.cpp): positions in samples scale, those in musical units stay
  auto sc = [sr](int at) { return static_cast<int>((static_cast<long long>(at) * sr + 4000) / 8000); };
  s.sr = sr;
  s.total = sc(s.total);
  for (auto& i : s.ins) i.at = sc(i.at);
  for (auto& c : s.cfg) c.at = sc(c.at);
  if (s.host) s.host = s.host->rescaled(sr, sc);
  return s;
}
std::string g1Diverges(const Plant& pl) {
  const Session s = mixSession(pl);
  const Run ref = runOnce(s, 4096);
  if (ref.evs.size() < 150) return "too few events";
  const std::string want = streamOf(ref);
  for (int blk : {512, 64, 7, 1})
    if (streamOf(runOnce(s, blk)) != want) return "absolute samples differ at block " + std::to_string(blk);
  return "";
}
std::string g4Diverges(const Plant& pl) {
  std::string ref;
  for (int sr : {48000, 44100, 96000, 192000}) {
    Session s = atRate(mixSession(pl), sr);
    const Run r = runOnce(s, 1024);
    if (r.evs.size() < 150) return "too few events at " + std::to_string(sr);
    const std::string t = tickStreamOf(r);
    if (ref.empty()) ref = t; else if (t != ref) return "ticks (or velocities) differ at " + std::to_string(sr) + " Hz";
  }
  return "";
}

void testInvariants() {
  std::puts("RN1: N1 / G1 / G4 / A1 over a session with all three settings non-zero and changing mid-session");
  const std::string pg1 = g1Diverges({"randomFromBlock"});
  plant(!pg1.empty(), "G1 detects a draw position taken from the block (plant randomFromBlock): " + pg1);
  const std::string g1 = g1Diverges({});
  const Run base = runOnce(mixSession({}), 64);
  check(g1.empty(), "G1: the session's " + std::to_string(base.evs.size()) + " events (velocity and length randomness, ratchets, ties, a chord step, tempo change, six settings changes) have identical absolute samples and velocities at blocks 4096 / 512 / 64 / 7 / 1" + (g1.empty() ? "" : " - " + g1));
  const std::string pg4 = g4Diverges({"srTickMath"});
  plant(!pg4.empty(), "G4 detects a sample-rate-dependent value in tick math on the randomness session: " + pg4);
  const std::string g4 = g4Diverges({});
  check(g4.empty(), "G4: the randomness session has identical event ticks, velocities and lengths at 44.1 / 48 / 96 / 192 kHz" + (g4.empty() ? "" : " - " + g4));
  Session pd = mixSession({"dropRelease"});
  plant(!n1(runOnce(pd, 64)).empty(), "N1 detects a dropped release on the randomness session");
  check(n1(base).empty(), "N1: exactly one NoteOff per NoteOn, at least a sample later, over the randomness session (lengths that reach 1 tick included)");
  // N1 where the randomness lands lengths on the minimum: 1/64 notes of 1-2 ticks at 8 kHz (fractions of a sample: the off is deferred a sample, SPEC 1.1)
  Session tiny = minSess().s;
  tiny.sr = 8000; tiny.total = 40000;
  plant(!n1(runOnce([&] { Session p = tiny; p.plants = {"sameSampleOff"}; return p; }(), 64)).empty(), "N1 detects a 1-tick note whose off lands on its own on sample");
  check(n1(runOnce(tiny, 64)).empty() && n1(runOnce(tiny, 7)).empty(), "N1: 1-tick randomised notes at 8 kHz (a fraction of a sample long) still end exactly once, a sample after their on");
  auto allocs = [](const Plant& pl) { Session s = mixSession(pl); s.countAllocs = true; return runOnce(s, 64).allocs; };
  plant(allocs({"allocInProcess"}) > 0, "A1 detects an allocation in process() on the randomness session");
  check(allocs({}) == 0, "A1: no operator-new inside process() over the randomness session, settings changes included");
  // randomness moves a note's velocity and length and nothing else: the same session with both amounts forced to 0 has the very same ons (sample, key, tick, step)
  Session z = mixSession({});
  z.base.velRandom = 0; z.base.gateRandom = 0;
  for (Change& c : z.cfg) { auto f = c.f; c.f = [f](Settings& x) { f(x); x.velRandom = 0; x.gateRandom = 0; }; }
  const Run rz = runOnce(z, 64);
  auto onSig = [](const Run& r) { std::string o; for (const Rec& e : onsOf(r)) o += std::to_string(e.a) + " " + std::to_string(e.key) + " " + std::to_string(e.tick) + " " + std::to_string(e.ord) + "\n"; return o; };
  const std::string withR = onSig(base), without = onSig(rz);
  check(withR == without && !withR.empty() && streamOf(rz) != streamOf(base), "randomness moves velocity and length only: the ons (sample, key, tick, step) of the randomness session equal those of the same session with both amounts at 0, while the streams differ");
  check(base.core->counters().rows[9] > 0, "the amounts and the curve are continuous settings: changing them while notes sound counts as T9 (" + std::to_string(base.core->counters().rows[9]) + "), SPEC 7");
}

// ---------------------------------------------------------------------------------------------------- a seeded fuzz of NEW sessions
struct Rng {
  std::uint32_t a;
  explicit Rng(std::uint32_t seed) : a(seed) {}
  std::uint32_t next() {
    a += 0x6D2B79F5u;
    std::uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
  }
  int in(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }
  bool pct(int p) { return in(0, 99) < p; }
};
Session fuzzSession(std::uint32_t id, const Plant& pl) {
  Rng r(0x52414E44u + id * 2654435761u);
  Session s;
  s.sr = 8000; s.total = 30000; s.plants = pl;
  s.base.order = static_cast<Order>(r.in(0, 15));
  s.base.pattern = static_cast<Pattern>(r.in(0, 11));
  static const Rate kRates[] = {Rate::R1_16, Rate::R1_8, Rate::R1_32, Rate::R1_16T, Rate::R1_4};
  s.base.rate = kRates[r.in(0, 4)];
  s.base.gate = r.in(1, 200);
  s.base.swing = r.in(500, 750);
  s.base.velMode = r.pct(30) ? VelMode::Fixed : VelMode::Played;
  s.base.velFixed = r.in(1, 127);
  s.base.chordEnabled = r.pct(30);
  s.base.seed = r.next();
  s.base.velRandom = r.pct(25) ? 0 : r.in(1, 100);
  s.base.gateRandom = r.pct(25) ? 0 : r.in(1, 100);
  s.base.randomCurve = r.in(0, 100);
  UserPattern u;
  u.length = r.in(1, 16);
  for (int i = 0; i < u.length; ++i) {
    PatternStep& p = u.steps[i];
    p.on = r.pct(85); p.tie = r.pct(20); p.vel = r.in(0, 200); p.gate = r.in(1, 200); p.ratchet = r.in(1, 4); p.pick = r.pct(60) ? kPickOrder : r.in(0, 12);
  }
  s.userPattern = u;
  const int nKeys = r.in(1, 5);
  for (int i = 0; i < nKeys; ++i) {
    const std::string label = "k" + std::to_string(i);
    const int at = r.in(0, 20000);
    s.ins.push_back(K(at, label.c_str(), r.in(36, 84), r.in(1, 127)));
    if (r.pct(50)) s.ins.push_back(O(at + r.in(1000, 9000), label.c_str()));
  }
  for (int i = 0; i < 3; ++i) {
    const int at = r.in(1, 29) * 1000 + 500;
    const int which = r.in(0, 2), val = r.in(0, 100);
    s.cfg.push_back(C(at, [which, val](Settings& x) { (which == 0 ? x.velRandom : which == 1 ? x.gateRandom : x.randomCurve) = val; }));
  }
  return s;
}
void testFuzz() {
  std::puts("RN1: a seeded fuzz of 40 new sessions with randomness on (not in the committed digest)");
  constexpr int kSessions = 40;
  int g1Bad = 0, n1Bad = 0, notes = 0, drifted = 0;
  std::string first;
  for (std::uint32_t id = 0; id < kSessions; ++id) {
    const Session s = fuzzSession(id, {});
    const Run ref = runOnce(s, 4096);
    notes += static_cast<int>(onsOf(ref).size());
    const std::string want = streamOf(ref);
    for (int blk : {512, 64, 7})
      if (streamOf(runOnce(s, blk)) != want) { ++g1Bad; if (first.empty()) first = "session " + std::to_string(id) + " at block " + std::to_string(blk); }
    n1Bad += !n1(ref).empty();
    Session z = s;   // the same session with randomness off must differ (the settings do something)
    z.base.velRandom = 0; z.base.gateRandom = 0;
    for (Change& c : z.cfg) { auto f = c.f; c.f = [f](Settings& x) { f(x); x.velRandom = 0; x.gateRandom = 0; }; }
    drifted += streamOf(runOnce(z, 4096)) != want;
  }
  int planted = 0;
  for (std::uint32_t id = 0; id < kSessions && !planted; ++id) {
    Session s = fuzzSession(id, {"randomFromBlock"});
    if (s.base.velRandom == 0 && s.base.gateRandom == 0) continue;
    planted += streamOf(runOnce(s, 4096)) != streamOf(runOnce(s, 7));
  }
  plant(planted > 0, "the fuzz reads red on a draw position taken from the block (plant randomFromBlock)");
  check(g1Bad == 0 && n1Bad == 0 && notes > 500, "fuzz: " + std::to_string(kSessions) + " seeded sessions (all 16 orders, bank and user patterns, ratchets, ties, the chord stage, swing, three random settings changing mid-session): " + std::to_string(notes) + " notes, absolute samples identical at blocks 4096 / 512 / 64 / 7, N1 holds" + (first.empty() ? "" : " - " + first));
  check(drifted > kSessions / 2, "fuzz: switching randomness off changes the stream in " + std::to_string(drifted) + " of the sessions (so the sessions use it)");
}

}  // namespace

void runRandomTests() {
  std::puts("Note randomness (SPEC 4.4, 4.9, 5.2; D-036; RN1)");
  testByteIdentity();
  testKnownAnswers();
  testBounds();
  testCurve();
  testChordStage();
  testInvariants();
  testFuzz();
}

}  // namespace arp_test
