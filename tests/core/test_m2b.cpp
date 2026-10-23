// test_m2b.cpp - the M2b checks (ROADMAP T-005): restart chord / bar / free (SPEC 4.5), repeats (4.8: RP1), the timing of a changed
// setting under horde's morph (SPEC 9: T8, T9, Q-A1, Q-A2), and the chord stage (SPEC 14: CH1, S1 extended, T20, T21). Same charter as
// the other suites: each detector is preceded by its plant, a plant that does not fire is a FAIL, and "known answer" assertions
// (exact expected values written from the spec text, not from the core's formulas) carry no plant and say so.
//
// 8 kHz and 120 bpm throughout unless a check says otherwise: 1 beat = 4000 samples = 15360 ticks, a 1/16 step = 1000 samples =
// 3840 ticks, a 4/4 bar = 16000 samples = 61440 ticks.
//
// About the morph: horde RESOLVES a morph (SPEC 9: discrete settings take the leading corner with 0.05 hysteresis, continuous ones
// blend) and hands Arp integer snapshots, so the core has no morph code and no interface for one. The `MorphModel` below is a
// HARNESS model of horde's rule, used to generate morph-driven snapshots; its own known answers pin the threshold, the hysteresis, the
// tie and the blend so the sessions cannot drift from the spec. What the CORE owes (and these checks test) is the landing of the
// resulting changes: T8 / T9, Q-A2, and one terminal per note over every discrete switch.
#include "test_m2b.h"

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

#include "arp/bank_data.h"
#include "harness.h"
#include "test_m1b.h"
#include "test_m2a.h"

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
UserPattern userPat(std::vector<PatternStep> steps, int stepSize = 12) {
  UserPattern u;
  u.length = static_cast<int>(steps.size());
  u.stepSize = stepSize;
  for (std::size_t i = 0; i < steps.size() && i < 16; ++i) u.steps[i] = steps[i];
  return u;
}

constexpr unsigned kCMajor = 0xAB5;   // root-relative degrees 0 2 4 5 7 9 11
const Scale kCMajorScale{0, static_cast<std::uint16_t>(kCMajor)};

std::string join(const std::vector<int>& v) { std::string s; for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]); return s; }
std::string joinS(const std::vector<std::int64_t>& v) { std::string s; for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]); return s; }
std::vector<int> keysOf(const Run& r, std::size_t n = 1000000) { std::vector<int> v; for (const Rec& e : onsOf(r)) if (v.size() < n) v.push_back(e.key); return v; }
std::vector<std::int64_t> onSamples(const Run& r, std::size_t n = 1000000) { std::vector<std::int64_t> v; for (const Rec& e : onsOf(r)) if (v.size() < n) v.push_back(e.a); return v; }
std::vector<std::int64_t> trigSamples(const Run& r) { std::vector<std::int64_t> v; for (const Rec& e : r.evs) if (e.t == EventType::Trigger) v.push_back(e.a); return v; }
std::vector<int> keysBetween(const Run& r, std::int64_t from, std::int64_t to) { std::vector<int> v; for (const Rec& e : onsOf(r)) if (e.a >= from && e.a < to) v.push_back(e.key); return v; }

// ====================================================================== RP1: repeats (SPEC 4.8)
// keys 60 64 67 pressed at 100: the gesture's first step is the one at 1000, 1000 samples apart
Session rp(const Plant& pl, int repeats, std::vector<InEv> ins = {}, int total = 40000) {
  Session s;
  s.total = total;
  s.plants = pl;
  s.base.gate = 30;
  s.base.repeats = repeats;
  s.ins = ins.empty() ? std::vector<InEv>{K(100, "k0", 60), K(100, "k1", 64), K(100, "k2", 67)} : std::move(ins);
  return s;
}
std::vector<InEv> cmajorOn(int at, const char* a, const char* b, const char* c) { return {K(at, a, 60), K(at, b, 64), K(at, c, 67)}; }

void testRP1() {
  std::puts("RP1: repeats = N full cycles of the whole pattern per chord, then the arp is spent");
  const std::string two = "60,64,67,60,64,67";
  // (a) 2 repeats of `up` over C E G with the plain pattern, then silence: no notes and no triggers
  auto a = [&](const Plant& pl) { return join(keysOf(run(rp(pl, 2), 256))) != two; };
  auto aTrig = [&](const Plant& pl) {
    Session s = rp(pl, 2);
    s.base.pattern = Pattern::User;
    s.userPattern = userPat({B().trig(1)});
    return trigSamples(run(s, 256)).size() != 6;
  };
  // (b) a step pattern and a transposition: C counts FULL cycles. gallop plays 3 of 4 steps, steps 1 (+12): P*(steps+1) = 6,
  //     gcd(6, 3) = 3, C = 4 * 2 = 8 steps; 2 repeats = 16 steps = 12 notes = both passes, twice
  auto b = [&](const Plant& pl) {
    Session s = rp(pl, 2);
    s.base.pattern = Pattern::Gallop;
    s.base.steps = 1;
    return join(keysOf(run(s, 256))) != "60,64,67,72,76,79,60,64,67,72,76,79";
  };
  // (b') tied_legato: 4 of 8 steps start a note (the ties do not), P = 3: gcd(3, 4) = 1, C = 8 * 3 = 24 steps = 12 notes = four orders
  auto b2 = [&](const Plant& pl) {
    Session s = rp(pl, 1);
    s.base.pattern = Pattern::TiedLegato;
    s.base.gate = 50;
    return join(keysOf(run(s, 256))) != "60,64,67,60,64,67,60,64,67,60,64,67";
  };
  // (b3) the static play count excludes ties, and the gcd bites with a long period: bounce2 over 4 keys has P = 8, tied_legato starts 4 notes
  //      in 8 steps: gcd(8, 4) = 4, C = 8 * 2 = 16 steps = 8 notes = one full bounce2 cycle (counting the ties would give C = 8)
  auto b3 = [&](const Plant& pl) {
    Session s = rp(pl, 1, {K(100, "k0", 60), K(100, "k1", 64), K(100, "k2", 67), K(100, "k3", 71)});
    s.base.pattern = Pattern::TiedLegato;
    s.base.order = Order::Bounce2;
    s.base.gate = 50;
    return join(keysOf(run(s, 256))) != "60,64,67,71,71,67,64,60";
  };
  // (b4) an explicit pick is not a note-starting `order` step: [order, low] over 4 keys with `up`: plays = 1, P = 4, C = 2 * 4 = 8 steps (4 order notes + 4 picks)
  auto b4 = [&](const Plant& pl) {
    Session s = rp(pl, 1, {K(100, "k0", 60), K(100, "k1", 64), K(100, "k2", 67), K(100, "k3", 71)});
    s.base.pattern = Pattern::User;
    s.userPattern = userPat({B(), B().pick(kPickLow)});
    return join(keysOf(run(s, 256))) != "60,60,64,60,67,60,71,60";
  };
  // (c) 0 means forever
  auto c = [&](const Plant& pl) { return onsOf(run(rp(pl, 0), 256)).size() < 30; };
  // (d) latch: the latched chord stops after its repeats; a NEW gesture restarts the count (from each gesture start, not from the transport)
  auto d = [&](const Plant& pl) {
    Session s = rp(pl, 2, {K(100, "k0", 60), K(100, "k1", 64), K(100, "k2", 67), O(3000, "k0"), O(3000, "k1"), O(3000, "k2"), K(20000, "n0", 60), K(20000, "n1", 64), K(20000, "n2", 67)});
    s.base.latch = Latch::Replace;
    const Run r = run(s, 256);
    return keysBetween(r, 0, 20000).size() != 6 || keysBetween(r, 20000, 40000).size() != 6;
  };
  // (e) the count runs from the gesture start whatever `restart` does (the host clock keeps stepping with no keys before the press)
  auto e = [&](const Plant& pl) {
    for (Restart rs : {Restart::Bar, Restart::Free, Restart::Chord}) {
      Session s = rp(pl, 2, cmajorOn(2345, "k0", "k1", "k2"));
      s.base.restart = rs;
      if (join(keysOf(run(s, 256))) != two) return true;
    }
    return false;
  };
  // (f) exhausted, then a new gesture under the host clock: the spent steps consumed the once-guard (silent rests), so the second
  //     gesture starts on its own grid steps 20000 21000 22000 and no burst of overdue steps lands at offset 0
  auto f = [&](const Plant& pl) {
    std::vector<InEv> ins = cmajorOn(100, "k0", "k1", "k2");
    ins.push_back(O(8000, "k0")); ins.push_back(O(8000, "k1")); ins.push_back(O(8000, "k2"));
    for (const InEv& x : cmajorOn(20000, "n0", "n1", "n2")) ins.push_back(x);
    Session s = rp(pl, 1, ins, 30000);
    const Run r = run(s, 256);
    std::vector<std::int64_t> late;
    for (const Rec& x : onsOf(r)) if (x.a >= 20000) late.push_back(x.a);
    return joinS(late) != "20000,21000,22000";
  };
  // (g) latch append: a press that adds a key to the latched set (NOT a gesture start) restarts the count: 4 keys x 2 repeats = 8 more notes
  auto g = [&](const Plant& pl) {
    std::vector<InEv> ins = cmajorOn(100, "k0", "k1", "k2");
    ins.push_back(O(3000, "k0")); ins.push_back(O(3000, "k1")); ins.push_back(O(3000, "k2"));
    ins.push_back(K(20000, "d", 72));
    Session s = rp(pl, 2, ins);
    s.base.latch = Latch::Append;
    const Run r = run(s, 256);
    return keysBetween(r, 0, 20000).size() != 6 || keysBetween(r, 20000, 40000).size() != 8;
  };
  // (h) a morph of repeats, steps or pattern never revives a spent arp
  auto h = [&](const Plant& pl) {
    Session s = rp(pl, 2);
    s.cfg = {C(15000, [](Settings& x) { x.repeats = 8; }), C(20000, [](Settings& x) { x.steps = 2; x.pattern = Pattern::Gallop; }), C(25000, [](Settings& x) { x.repeats = 0; })};
    return onsOf(run(s, 256)).size() != 6;
  };
  // (i) T6 restarts the count and clears `exhausted`: a transport stop / start with the keys still held plays the repeats again
  auto iT = [&](const Plant& pl) {
    Session s = rp(pl, 1);
    auto h = std::make_shared<SimHost>(8000, 120, 0, true);
    h->set(8000, false); h->set(10000, true, 0.0);
    s.host = h;
    const Run r = run(s, 256);
    return keysBetween(r, 0, 8000).size() != 3 || keysBetween(r, 10000, 40000).size() != 3;
  };
  // (j) with NO keys held the repeat count does not run out: trigger-only steps (restart free, host clock) go on firing
  auto jT = [&](const Plant& pl) {
    Session s;
    s.total = 52000; s.plants = pl; s.base.restart = Restart::Free; s.base.pattern = Pattern::TriggerPulse; s.base.repeats = 1;
    return trigSamples(run(s, 4096)).size() != 13;
  };
  plant(f({"exhaustedStopsGuard"}), "RP1 detects a spent arp that stops consuming the once-guard (a burst of overdue steps at the next gesture)");
  plant(g({"onlyGestureResets"}), "RP1 detects a key-adding press (latch append) that does not restart the count");
  plant(h({"morphRevives"}), "RP1 detects a repeats morph that revives an exhausted arp");
  plant(a({"repeatsNeverStop"}) && b({"repeatsNeverStop"}), "RP1 detects repeats that never stop");
  plant(aTrig({"repeatsNeverStop"}), "RP1 detects triggers still firing after the repeats ran out");
  plant(b({"repeatsCountPatternPasses"}) && b2({"repeatsCountPatternPasses"}) && b3({"repeatsCountPatternPasses"}), "RP1 detects step-pattern passes counted instead of full cycles (gallop + a transposition step, tied_legato)");
  check(!a({}) && !aTrig({}) && !b({}) && !b2({}) && !b3({}) && !b4({}) && !c({}) && !d({}) && !e({}) && !f({}) && !g({}) && !h({}) && !iT({}) && !jT({}),
        "RP1: 2 repeats of up over C E G = C E G C E G then silence (and no triggers); gallop + 1 step of +12 is 8 steps per cycle (12 notes in 2 cycles); tied_legato counts only the 4 note-starting steps (24 steps, 12 notes; with a period of 8 the gcd makes it 16 steps); an explicit pick is not counted; a transport edge (T6) restarts the count; with no keys held the count never runs out; 0 never stops; a latched chord stops and a new gesture restarts it; bar, free and chord all count from the gesture start; spent steps consume the once-guard (the next gesture lands on 20000 21000 22000, no burst); a key-adding latch press restarts the count; a repeats / steps / pattern morph never revives a spent arp");
  { // a pattern with no note-starting step (plays = 0): C = the step pattern's length, so 2 repeats of trigger_pulse (length 4) = 8 steps = 2 triggers
    Session s = rp({}, 2);
    s.base.pattern = Pattern::TriggerPulse;
    const Run r = run(s, 256);
    check(trigSamples(r).size() == 2 && onsOf(r).empty(), "known answer: a pattern with no note-starting step has C = its own length (trigger_pulse, 2 repeats = 8 steps = 2 triggers)");
  }
  { // the order's period comes from the live held set: add a key mid-cycle and C changes with it (recomputed at every step)
    Session s = rp({}, 1, {K(100, "k0", 60), K(100, "k1", 64), K(100, "k2", 67), K(2500, "k3", 72)});
    s.base.latch = Latch::Off;
    const Run r = run(s, 256);
    // the key-adding press at 2500 restarts the count (4 keys x 1 repeat = 4 steps), without a new gesture: after the first two notes (1000, 2000)
    // 60 64 | m = 2.. over 4 keys: 72 is held from 2500: step 3000 plays E[2] = 67, then 72, 60, 64 = 4 steps, then silence
    check(join(keysOf(r)) == "60,64,67,72,60,64", "known answer: C follows the live held set and a key-adding press restarts the count (3 keys: two notes, then a fourth key at 2500 restarts: 4 more steps)");
  }
  { // time mode counts the same way, in time steps
    Session s = rp({}, 2);
    s.base.rateMode = RateMode::Time;
    s.base.stepTimeUs = 125000;
    check(join(keysOf(run(s, 256))) == two, "known answer: in time mode repeats counts time steps the same way (2 repeats = C E G C E G)");
  }
  { // T20 acted
    const Run r = run(rp({}, 2), 256);
    check(r.core->counters().rows[20] == 1, "T20: the repeats ran out exactly once, no new steps, nothing sounding was cut (N1: " + std::string(n1(r).empty() ? "holds" : "BROKEN") + ")");
  }
}

// ====================================================================== restart (SPEC 4.5, 9 Q-A2, 2.6)
Session rs(const Plant& pl, Restart mode, Pattern pat, std::vector<InEv> ins, int total = 24000) {
  Session s;
  s.total = total;
  s.plants = pl;
  s.base.gate = 30;
  s.base.restart = mode;
  s.base.pattern = pat;
  s.ins = std::move(ins);
  return s;
}

void testRestart() {
  std::puts("Restart (SPEC 4.5): chord anchors at the gesture, bar at each bar start, free at tick 0; Q-A2 under morph; inert in time mode");
  // (a) bar: `up` over C E G pressed mid-step at 2345 (first step 3000). A bar is 16 steps, so 13 notes fall before the bar at 16000:
  //     bar restarts the order there (m := 0 at the anchor), chord just carries on
  const std::string bar13 = "60,64,67,60,64,67,60,64,67,60,64,67,60";
  auto barLine = [&](const Plant& pl, Restart m) {
    const Run r = run(rs(pl, m, Pattern::None, {K(2345, "a", 60), K(2345, "b", 64), K(2345, "c", 67)}), 256);
    return join(keysBetween(r, 0, 16000)) + " | " + join(keysBetween(r, 16000, 19000));
  };
  plant(barLine({"restartBarAsChord"}, Restart::Bar) != bar13 + " | 60,64,67", "restart detects `bar` treated as `chord` (no restart of the order at the bar start)");
  check(barLine({}, Restart::Bar) == bar13 + " | 60,64,67", "known answer: restart bar: 13 notes 60 64 67 ... 60 before the bar at 16000, and the order restarts there (60 64 67); chord would play 64 there");
  check(barLine({}, Restart::Chord) == bar13 + " | 64,67,60", "known answer: restart chord carries the order straight through the bar line (64 67 60 after 16000)");

  // (b) free: the pattern is counted from tick 0 of the clock, not from the press: gallop (play, rest, play, play) pressed at 2345
  //     plays steps 3 4 6 7 8 10 11 12 under free (position = step mod 4) and 3 5 6 7 9 10 11 under chord
  auto freeLine = [&](const Plant& pl, Restart m) { return joinS(onSamples(run(rs(pl, m, Pattern::Gallop, {K(2345, "a", 60), K(2345, "b", 64), K(2345, "c", 67)}, 14000), 256))); };
  plant(freeLine({"restartFreeAsChord"}, Restart::Free) != "3000,4000,6000,7000,8000,10000,11000,12000", "restart detects `free` treated as `chord`");
  check(freeLine({}, Restart::Free) == "3000,4000,6000,7000,8000,10000,11000,12000", "known answer: restart free keeps the pattern on the song grid (gallop pressed at 2345 plays steps 3 4 6 7 8 10 11 12)");
  check(freeLine({}, Restart::Chord) == "3000,5000,6000,7000,9000,10000,11000,13000", "known answer: restart chord anchors the pattern at the press (steps 3 5 6 7 9 10 11 13)");

  // (b2) bar anchors by the BAR grid, not by the press: gallop pressed at 2345 under bar plays the same steps as under free (the first bar starts at tick 0)
  check(freeLine({}, Restart::Bar) == "3000,4000,6000,7000,8000,10000,11000,12000", "known answer: restart bar keeps the pattern on the bar grid: gallop pressed at 2345 plays steps 3 4 6 7 8 10 11 12, not from the press");
  // (c) triggers fire with NO keys held under bar and free on the host clock; under chord there is no anchor before the first gesture (SPEC 2.5)
  auto pulse = [&](const Plant& pl, Restart m, std::function<void(Session&)> extra = {}) {
    Session s = rs(pl, m, Pattern::TriggerPulse, {}, 52000);
    if (extra) extra(s);
    return joinS(trigSamples(run(s, 4096)));
  };
  check(pulse({}, Restart::Chord).empty() && pulse({}, Restart::Free) == "0,4000,8000,12000,16000,20000,24000,28000,32000,36000,40000,44000,48000" &&
            pulse({}, Restart::Bar) == pulse({}, Restart::Free),
        "known answer: with no keys held trigger_pulse fires every beat under bar and free from the first step (SPEC 2.5, H11), and not at all under chord");
  // (d) 10 of 22 rates do not divide a bar: 3/16 (11520 ticks = 3000 samples), a bar = 16000 samples. The anchor is the first step AT OR AFTER each
  //     bar start: steps 0 | 6 (tick 69120 >= 61440) | 11 (126720 >= 122880) | 16 (184320, exactly the bar) ...; the 4-step pattern restarts there
  const std::string bars316 = "0,12000,18000,30000,33000,45000,48000";
  auto r316 = [&](const Plant& pl, Restart m) { return pulse(pl, m, [](Session& s) { s.base.rate = Rate::R3_16; }); };
  plant(r316({"restartBarAsChord"}, Restart::Bar).size() != bars316.size() || r316({"restartBarAsChord"}, Restart::Bar) != bars316, "restart detects `bar` that ignores the bar grid at a rate that does not divide a bar");
  check(r316({}, Restart::Bar) == bars316, "known answer: 3/16 under bar: the pattern re-anchors at the first step at or after each bar start (steps 0, 6, 11, 16): triggers at " + bars316);
  check(r316({}, Restart::Free) == "0,12000,24000,36000,48000", "known answer: 3/16 under free: every 4th step from tick 0 (0, 12000, 24000, 36000, 48000)");
  // (d2) a rate change under bar re-expresses the bar anchor in the new rate and keeps m (SPEC 13.8): 1/8 -> 1/4 at 17000 with a 3-step pattern
  //      (trigger on its first step), bars of 16000 samples: bar 0 steps 0 3 6 (0 6000 12000), bar 1 anchor step 8 (16000); the switch lands at the next
  //      OLD step (18000 = step 9), the new grid's step 5 (20000) has the re-expressed anchor 4, so position 1: triggers at k = 7 (28000) and the bar 2 start k = 8 (32000)
  auto rateUnderBar = [&](const Plant& pl) {
    Session s = rs(pl, Restart::Bar, Pattern::User, {}, 40000);
    s.base.rate = Rate::R1_8;
    s.userPattern = userPat({B().on(false).trig(1), B().on(false), B().on(false)});
    s.cfg = {C(17000, [](Settings& x) { x.rate = Rate::R1_4; })};
    return joinS(trigSamples(run(s, 256)));
  };
  check(rateUnderBar({}) == "0,6000,12000,16000,28000,32000", "known answer (13.8): a rate change under bar re-expresses the bar anchor in the new rate (m kept): triggers 0 6000 12000 16000 | 28000 32000");
  // (e) the bar grid: barStartBeats is a PHASE (bars fall at barStart + i * barLength, SPEC 13.12), timeSigNum/Den set the bar length
  auto phased = [&](const Plant& pl) {
    return pulse(pl, Restart::Bar, [](Session& s) {
      s.total = 40000; s.base.rate = Rate::R1_4;
      s.tweak = [](int, Transport& t) { t.hasBarStart = true; t.barStartBeats = 1.0; };
    });
  };
  check(phased({}) == "4000,20000,36000", "known answer: barStartBeats = 1 beat is a phase: with 1/4 steps and a 4-step pattern the pattern restarts at beats 1, 5, 9 (4000, 20000, 36000), and step 0 (before the first bar start) is position 3, so silent");
  auto threeFour = [&](const Plant& pl) {
    return pulse(pl, Restart::Bar, [](Session& s) {
      s.total = 40000; s.base.rate = Rate::R1_4;
      s.tweak = [](int, Transport& t) { t.timeSigNum = 3; t.timeSigDen = 4; };
    });
  };
  check(threeFour({}) == "0,12000,24000,36000", "known answer: in 3/4 a bar is 3 beats (46080 ticks): the 4-step pattern restarts every bar, at 0, 12000, 24000, 36000");
  { // a transport that reports nonsense (time signature 0/0) reads as 4/4
    const std::string w = pulse({}, Restart::Bar, [](Session& s) { s.total = 20000; s.base.rate = Rate::R1_4; s.tweak = [](int, Transport& t) { t.timeSigNum = 0; t.timeSigDen = 0; }; });
    check(w == "0,16000", "known answer: a time signature of 0/0 reads as 4/4 (SPEC-GAP: the spec names no fallback): bars at 0 and 16000");
  }

  // (f) Q-A2, restart under morph: a change of `restart` takes effect from the NEXT EVALUATED step.
  //     chord -> free at 6500 (mid-step), `up` over C E G pressed at 2345: steps 3 4 5 6 play 60 64 67 60; at step 7 (7000) the change is adopted and
  //     m := 0, so it plays 60 again (not 64): 60 64 67 60 | 60 64 67
  auto intoFree = [&](const Plant& pl) {
    Session s = rs(pl, Restart::Chord, Pattern::None, {K(2345, "a", 60), K(2345, "b", 64), K(2345, "c", 67)}, 12000);
    s.cfg = {C(6500, [](Settings& x) { x.restart = Restart::Free; })};
    return join(keysOf(run(s, 256), 8));
  };
  plant(intoFree({"restartMorphKeepsM"}) != "60,64,67,60,60,64,67,60", "Q-A2 detects a morph into `free` that leaves m alone");
  check(intoFree({}) == "60,64,67,60,60,64,67,60", "known answer (Q-A2): a morph chord -> free at 6500 applies at the next evaluated step (7000) and zeroes m: 60 64 67 60 | 60 64 67 60");
  auto intoBar = [&](const Plant& pl) {
    Session s = rs(pl, Restart::Chord, Pattern::None, {K(2345, "a", 60), K(2345, "b", 64), K(2345, "c", 67)}, 12000);
    s.cfg = {C(6500, [](Settings& x) { x.restart = Restart::Bar; })};
    return join(keysOf(run(s, 256), 8));
  };
  check(intoBar({}) == "60,64,67,60,60,64,67,60", "known answer (Q-A2): a morph chord -> bar at 6500 re-anchors at the current bar's start and zeroes m at the next evaluated step: 60 64 67 60 | 60 64 67 60 (no plant: the bar anchor itself zeroes m, so the free case carries the m := 0 detector)");
  // free -> chord keeps the anchor (tick 0) until the next gesture start: the stream is identical to never having switched; then a new gesture anchors
  auto outOfFree = [&](const Plant& pl, bool morph) {
    Session s = rs(pl, Restart::Free, Pattern::Gallop, {K(2345, "a", 60), K(2345, "b", 64), K(2345, "c", 67), O(9000, "a"), O(9000, "b"), O(9000, "c"), K(10345, "d", 60)}, 20000);
    if (morph) s.cfg = {C(6500, [](Settings& x) { x.restart = Restart::Chord; })};
    return s;
  };
  auto stream1 = [&](const Plant& pl, bool morph, std::int64_t from, std::int64_t to) { return joinS([&] { std::vector<std::int64_t> v; for (std::int64_t x : onSamples(run(outOfFree(pl, morph), 256))) if (x >= from && x < to) v.push_back(x); return v; }()); };
  plant(stream1({"restartMorphReanchors"}, true, 0, 9000) != stream1({}, false, 0, 9000), "Q-A2 detects a morph into `chord` that re-anchors at once instead of keeping the anchor");
  check(stream1({}, true, 0, 10345) == stream1({}, false, 0, 10345) && stream1({}, true, 10345, 20000) != stream1({}, false, 10345, 20000),
        "known answer (Q-A2): a morph free -> chord keeps the anchor (identical notes until the next gesture) and the next gesture then anchors at the press (11000, 13000 ... instead of the song grid)");

  // (g) restart is INERT in time mode: bar, free and chord play the same stream; and the bar grid would have mattered
  auto timeLine = [&](const Plant& pl, Restart m) {
    Session s;
    s.total = 30000; s.plants = pl; s.base.rateMode = RateMode::Time; s.base.stepTimeUs = 62000; s.base.gate = 30; s.base.restart = m; s.base.pattern = Pattern::Gallop;
    s.ins = {K(1234, "a", 60), K(1234, "b", 64), K(1234, "c", 67)};
    return joinS(onSamples(run(s, 128)));
  };
  const std::string ref = timeLine({}, Restart::Chord);
  check(timeLine({}, Restart::Bar) == ref && timeLine({}, Restart::Free) == ref && ref.size() > 40, "TM3 (restart half): in time mode bar and free play exactly the chord stream (a 62 ms step against the 61,440 us 'bar' of the old rule)");
  plant(timeLine({"timeBarsCount"}, Restart::Bar) != ref, "TM3 detects restart bar APPLIED in time mode (bars of 61,440 us re-anchor the pattern)");
}

// ====================================================================== morph (SPEC 9)
// A HARNESS model of horde's morph resolution (see the file header). Weights are in thousandths of corner B.
struct MorphModel {
  Settings A, B;
  int cur = 0;   // the corner the discrete settings currently take
  Settings at(int wB) {
    const int wA = 1000 - wB;
    const int lead = wB > wA ? 1 : 0;   // ties go to the lower corner index
    const int leadW = lead ? wB : wA, curW = cur ? wB : wA;
    if (lead != cur && leadW - curW > 50) cur = lead;   // the leading corner must beat the current one by MORE than 0.05
    Settings o = cur ? B : A;
    auto blend = [&](int a, int b) { return (a * wA + b * wB + 500) / 1000; };   // horde rounds the blend to the nearest integer (half up; values are positive)
    o.gate = blend(A.gate, B.gate);
    o.swing = blend(A.swing, B.swing);
    o.velFixed = blend(A.velFixed, B.velFixed);
    o.stepTimeUs = blend(A.stepTimeUs, B.stepTimeUs);
    return o;
  }
};

void testMorphModel() {
  std::puts("Morph (SPEC 9, Q-A1): the harness model of horde's rule, pinned to the spec's numbers");
  Settings a, b;
  a.gate = 40; b.gate = 120; a.order = Order::Up; b.order = Order::Down; a.swing = 500; b.swing = 750; a.velFixed = 1; b.velFixed = 2;
  // discrete: a corner switch needs the leading corner's weight to exceed the current one's by MORE than 0.05; ties go to the lower corner
  auto corners = [&](std::vector<int> ws) { MorphModel m{a, b}; std::string out; for (int w : ws) out += m.at(w).order == Order::Up ? 'A' : 'B'; return out; };
  check(corners({0, 500, 525, 526, 700}) == "AAABB", "known answer (harness model): from corner A the discrete setting switches to B at wB = 0.526 and not at 0.525 (0.526 - 0.474 = 0.052 > 0.05; 0.525 - 0.475 = 0.050); an exact tie stays");
  check(corners({1000, 600, 525, 500, 475, 474, 300}) == "BBBBBAA", "known answer (harness model): from corner B it stays B through 0.475 and returns to A at 0.474 (hysteresis); at 0.5 the tie goes to the lower corner but does not beat the current one by 0.05");
  check(corners({526, 500, 526, 475, 525, 474}) == "BBBBBA", "known answer (harness model): dithering inside the 0.05 band never flips the setting (526 500 526 475 525 stay B; 474 returns to A)");
  auto blend = [&](int w) { MorphModel m{a, b}; return m.at(w); };
  check(blend(0).gate == 40 && blend(250).gate == 60 && blend(333).gate == 67 && blend(500).gate == 80 && blend(1000).gate == 120 && blend(500).swing == 625 && blend(500).velFixed == 2 && blend(499).velFixed == 1,
        "known answer (harness model): continuous settings blend linearly and round to the nearest integer, half up (gate 40..120 at 0.333 = 67; velocity 1..2 at 0.5 = 2)");
}

// The continuous settings, end to end: a gate that blends 40 -> 120 over the run. Each step reads the integer snapshot of ITS block at its
// evaluation (T9), so the scheduled off of the note at step n is on + floor(3840 * round-blend / 100) ticks, written out here from SPEC 5.2.
void testContinuousBlend() {
  std::puts("Morph: continuous settings blend and are read at each step's evaluation (T9)");
  Settings a, b;
  a.gate = 40; b.gate = 120; a.swing = 500; b.swing = 500;
  MorphModel m{a, b};
  Session s;
  s.total = 12000;
  s.base = a;
  s.base.enabled = true;
  s.ins = {K(100, "k", 60)};
  std::vector<int> gates;   // the gate in force at the block starting at sample i*500
  for (int i = 0; i <= 24; ++i) {
    Settings snap = m.at(i * 1000 / 20);
    snap.enabled = true;
    gates.push_back(snap.gate);
    s.cfg.push_back(C(i * 500, [snap](Settings& x) { x = snap; }));
  }
  const Run r = run(s, 128);
  bool ok = onsOf(r).size() >= 9;
  std::string first;
  for (const Rec& on : onsOf(r)) {
    const int n = static_cast<int>(on.a / 1000);   // step n at sample 1000 n reads the block that starts at 1000 n = control 2n
    const int g = gates[static_cast<std::size_t>(2 * n)];
    std::int64_t off = -1;
    for (const Rec& e : r.evs) if (e.t == EventType::NoteOff && e.ref == on.ref) off = e.tick;
    const std::int64_t want = on.tick + 3840 * g / 100;
    if (off != want) { ok = false; if (first.empty()) first = "step " + std::to_string(n) + " gate " + std::to_string(g) + ": off tick " + std::to_string(off) + " want " + std::to_string(want); }
  }
  check(ok && n1(r).empty() && gates[0] == 40 && gates[20] == 120, "known answer (T9): the gate blends 40 -> 120 and every note's scheduled off follows the blended gate of ITS step (40, 44, 48 ... 120 %), no sounding note moved" + (first.empty() ? "" : " - " + first));
}

// ---- the discrete settings as a TABLE, derived from spec/params.json (LIBRARY L0001: one row per setting that can change while a note sounds).
// `prep` makes the setting matter (it edits the base); `set(x, false)` / `set(x, true)` write corner A / corner B. `offsetZero` marks the
// three settings SPEC 7 / 9 apply at offset 0 of their block and that legitimately end notes (T4, T19).
struct Flip {
  std::string key;
  std::function<void(Settings&)> prep;
  std::function<void(Settings&, bool)> set;
  bool offsetZero = false;
};
std::vector<Flip> discreteTable() {
  std::vector<Flip> t;
  auto add = [&](const char* key, std::function<void(Settings&)> prep, std::function<void(Settings&, bool)> set, bool off0 = false) { t.push_back({key, std::move(prep), std::move(set), off0}); };
  auto none = [](Settings&) {};
  auto ownScale = [](Settings& x) { x.quantize = Quantize::Scale; x.scaleSource = ScaleSource::Own; x.scaleRoot = 0; };
  add("enabled", none, [](Settings& x, bool b) { x.enabled = !b; }, true);
  add("arp_enabled", none, [](Settings& x, bool b) { x.arpEnabled = !b; }, true);
  add("order", none, [](Settings& x, bool b) { x.order = b ? Order::Down : Order::Up; });
  add("pattern", none, [](Settings& x, bool b) { x.pattern = b ? Pattern::Gallop : Pattern::User; });
  add("rate", none, [](Settings& x, bool b) { x.rate = b ? Rate::R1_8 : Rate::R1_16; });
  add("rate_mode", none, [](Settings& x, bool b) { x.rateMode = b ? RateMode::Time : RateMode::Sync; }, true);
  add("steps", [](Settings& x) { x.stepSize = 7; }, [](Settings& x, bool b) { x.steps = b ? 2 : 0; });
  add("step_size", [](Settings& x) { x.steps = 1; }, [](Settings& x, bool b) { x.stepSize = b ? -5 : 12; });
  add("step_unit", [ownScale](Settings& x) { ownScale(x); x.steps = 1; x.stepSize = 2; }, [](Settings& x, bool b) { x.stepUnit = b ? StepUnit::Degrees : StepUnit::Semitones; });
  add("latch", none, [](Settings& x, bool b) { x.latch = b ? Latch::Replace : Latch::Off; });
  add("vel_mode", none, [](Settings& x, bool b) { x.velMode = b ? VelMode::Fixed : VelMode::Played; });
  add("restart (bar)", none, [](Settings& x, bool b) { x.restart = b ? Restart::Bar : Restart::Chord; });
  add("restart (free)", none, [](Settings& x, bool b) { x.restart = b ? Restart::Free : Restart::Chord; });
  add("repeats", none, [](Settings& x, bool b) { x.repeats = b ? 2 : 0; });
  add("quantize (scale)", none, [](Settings& x, bool b) { x.quantize = b ? Quantize::Scale : Quantize::Off; });
  add("quantize (chord_root)", [](Settings& x) { x.quantize = Quantize::Scale; }, [](Settings& x, bool b) { x.quantize = b ? Quantize::ChordRoot : Quantize::Scale; });
  add("scale_source", [](Settings& x) { x.quantize = Quantize::Scale; x.scaleRoot = 5; }, [](Settings& x, bool b) { x.scaleSource = b ? ScaleSource::Own : ScaleSource::Global; });
  add("scale_root", ownScale, [](Settings& x, bool b) { x.scaleRoot = b ? 5 : 0; });
  for (int d = 0; d < 12; ++d)
    add(("scale_deg" + std::to_string(d)).c_str(), ownScale, [d](Settings& x, bool b) { x.scaleDeg[d] = b ? !x.scaleDeg[d] : x.scaleDeg[d]; });
  add("seed", [](Settings& x) { x.order = Order::Random; }, [](Settings& x, bool b) { x.seed = b ? 9 : 1; });
  add("chord_enabled", none, [](Settings& x, bool b) { x.chordEnabled = b; });
  add("chord_unit", [](Settings& x) { x.chordEnabled = true; x.quantize = Quantize::Scale; }, [](Settings& x, bool b) { x.chordUnit = b ? StepUnit::Degrees : StepUnit::Semitones; });
  for (int i = 0; i < 6; ++i) {
    add(("chord_on" + std::to_string(i + 1)).c_str(), [](Settings& x) { x.chordEnabled = true; }, [i](Settings& x, bool b) { x.chordOn[i] = b ? !(i < 2) : (i < 2); });
    add(("chord_shift" + std::to_string(i + 1)).c_str(), [](Settings& x) { x.chordEnabled = true; for (bool& o : x.chordOn) o = true; }, [i](Settings& x, bool b) { x.chordShift[i] = b ? 9 : (i < 3 ? (i == 0 ? 4 : i == 1 ? 7 : 12) : 0); });
  }
  return t;
}

// T8 landing: a hard flip of ONE discrete setting at 4400 (mid-step, mid-ratchet). Swing 750 and a 4-way ratchet leave a sub-note PENDING past
// the next grid step (the sub-note at 5125 of the step at 4000), and a note sounding across it. Nothing evaluated before the change may move:
// every note of a step evaluated before 4400 sounds the same and ends where it ended, its pending sub-notes included.
constexpr int kFlipAt = 4400;
Session landBase(const Plant& pl, const Flip& f) {
  Session s;
  s.total = 9000;
  s.plants = pl;
  s.base.gate = 100;
  s.base.swing = 750;
  s.base.seed = 3;
  s.base.steps = 1;
  s.base.quantize = Quantize::Scale;
  s.base.pattern = Pattern::User;
  s.scale = kCMajorScale;
  s.userPattern = userPat({B().ratchet(4).trig(2)});
  s.ins = {K(0, "a", 61), K(0, "b", 66), K(0, "c", 70)};
  f.prep(s.base);
  f.set(s.base, false);
  return s;
}
std::string landingBad(const Plant& pl, const Flip& f) {
  const Run base = run(landBase({}, f), 512);
  Session fs = landBase(pl, f);
  fs.cfg = {C(kFlipAt, [&f](Settings& x) { f.set(x, true); })};
  const Run flip = run(fs, 512);
  if (!n1(flip).empty()) return "N1: " + n1(flip)[0];
  std::int64_t n0 = 0;
  for (const StepRec& st : base.steps) if (st.a < kFlipAt) ++n0;
  auto early = [&](const Run& r) {
    std::vector<std::string> v;
    for (const Rec& on : r.evs) {
      if (on.t != EventType::NoteOn || on.ord >= n0 || on.ord < 0) continue;
      std::int64_t off = -1;
      for (const Rec& e : r.evs) if (e.t == EventType::NoteOff && e.ref == on.ref) off = e.a;
      v.push_back(std::to_string(on.a) + ":" + std::to_string(on.key) + ":" + std::to_string(on.vel) + " off " + std::to_string(off));
    }
    return v;
  };
  const auto e0 = early(base), e1 = early(flip);
  if (e0.size() < 12) return "too few early notes (" + std::to_string(e0.size()) + ")";
  if (e0 != e1) {
    for (std::size_t i = 0; i < std::min(e0.size(), e1.size()); ++i) if (e0[i] != e1[i]) return "note " + e0[i] + " became " + e1[i];
    return "note count " + std::to_string(e0.size()) + " became " + std::to_string(e1.size());
  }
  return "";
}

void testLanding() {
  std::puts("Morph: a discrete switch lands at the next evaluated step and touches no sounding note (T8)");
  const std::vector<Flip> table = discreteTable();
  std::vector<std::string> bad;
  int checked = 0;
  for (const Flip& f : table) {
    if (f.offsetZero) continue;
    ++checked;
    const std::string b = landingBad({}, f);
    if (!b.empty()) bad.push_back(f.key + ": " + b);
  }
  int redCuts = 0, redDrops = 0;
  std::string firstCut, firstDrop, notCut, notDrop;
  for (const Flip& f : table) {
    if (f.offsetZero) continue;
    const std::string c = landingBad({"morphSwitchCuts"}, f), d = landingBad({"morphSwitchDropsPending"}, f);
    redCuts += !c.empty();
    redDrops += !d.empty();
    if (c.empty()) notCut += f.key + " ";
    if (d.empty()) notDrop += f.key + " ";
    if (firstCut.empty()) firstCut = f.key + ": " + c;
    if (firstDrop.empty()) firstDrop = f.key + ": " + d;
  }
  plant(redCuts == checked, "T8 detects a discrete switch that releases the arp's sounding notes (all " + std::to_string(checked) + " settings read red; e.g. " + firstCut + ")" + (redCuts == checked ? "" : " NOT RED: " + notCut));
  plant(redDrops == checked, "T8 detects a discrete switch that clears the pending queue (queued ratchet sub-notes vanish; all " + std::to_string(checked) + " settings read red; e.g. " + firstDrop + ")" + (redDrops == checked ? "" : " NOT RED: " + notDrop));
  check(bad.empty() && checked >= 40, "T8: for each of the " + std::to_string(checked) + " discrete settings (order, pattern, rate, steps, step_size/unit, latch, vel_mode, restart, repeats, quantize, the scale group, seed, chord_*), a flip at 4400 leaves every note of an already-evaluated step - its pending sub-note at 5125 included - on and off exactly where they were, N1 holds" + (bad.empty() ? "" : " - " + bad[0]));
}

// Landing known answers: where the first change shows.
void testLandingKnown() {
  std::puts("Morph: known answers for where a switch lands");
  auto keys = [&](Session s, std::size_t n) { return join(keysOf(run(s, 512), n)); };
  { // order up -> down at 5500, mid-step: steps 0..5 play 60 64 67 60 64 67 and step 6 (6000) is the first `down` step, with m = 6 carried over
    Session s;
    s.total = 10000; s.base.gate = 50; s.ins = {K(0, "a", 60), K(0, "b", 64), K(0, "c", 67)};
    s.cfg = {C(5500, [](Settings& x) { x.order = Order::Down; })};
    check(keys(s, 9) == "60,64,67,60,64,67,67,64,60", "known answer: order up -> down at 5500 lands at the next evaluated step, 6000: 60 64 67 60 64 67 | 67 64 60 (m carries over)");
  }
  { // a long old rate: 1/2 (8000 samples) -> 1/16 at 100: the change waits for the next step of the OLD rate (8000), then the new grid runs (SPEC 9, accepted lag)
    Session s;
    s.total = 12000; s.base.gate = 50; s.base.rate = Rate::R1_2; s.ins = {K(0, "a", 60), K(0, "b", 64)};
    s.cfg = {C(100, [](Settings& x) { x.rate = Rate::R1_16; })};
    check(joinS(onSamples(run(s, 512), 6)) == "0,8000,9000,10000,11000", "known answer: rate 1/2 -> 1/16 at 100 applies at the next step of the OLD rate (8000), then 9000, 10000, 11000 (SPEC 9: the lag is accepted)");
  }
  { // vel_mode flips at the next step; the note sounding at the change keeps its velocity
    Session s;
    s.total = 6000; s.base.gate = 50; s.base.velFixed = 33; s.ins = {K(0, "a", 60, 90)};
    s.cfg = {C(2500, [](Settings& x) { x.velMode = VelMode::Fixed; })};
    std::vector<int> v;
    for (const Rec& e : onsOf(run(s, 512))) v.push_back(e.vel);
    check(join(v) == "90,90,90,33,33,33", "known answer: vel_mode played -> fixed at 2500 starts at step 3000 (90 90 90 | 33 33 33)");
  }
  { // the scale group switches as ONE: global C major -> own {root 1, pcs 1 3 5 8 10}: no hybrid of one's root with the other's degrees
    Session s;
    s.total = 12000; s.base.gate = 50; s.base.quantize = Quantize::Scale; s.scale = kCMajorScale;
    s.base.order = Order::Chord;
    s.ins = {K(0, "a", 61), K(0, "b", 63), K(0, "c", 66), K(0, "d", 68), K(0, "e", 70)};
    s.cfg = {C(5500, [](Settings& x) { x.scaleSource = ScaleSource::Own; x.scaleRoot = 1; for (int d = 0; d < 12; ++d) x.scaleDeg[d] = d == 0 || d == 2 || d == 4 || d == 7 || d == 9; })};
    const Run r = run(s, 512);
    bool ok = true;
    for (const Rec& e : onsOf(r)) {
      const int pc = e.key % 12;
      const bool inA = ((kCMajor >> pc) & 1u) != 0, inB = pc == 1 || pc == 3 || pc == 5 || pc == 8 || pc == 10;
      ok = ok && (e.a < 6000 ? inA : inB);
    }
    check(ok && onsOf(r).size() > 10, "known answer: a scale-group switch (global C major -> own root 1 with degrees 0 2 4 7 9) applies as one: every note before 6000 is in C major, every note from 6000 on in the new scale, none in a hybrid");
  }
}

// ---- morph-driven sessions: N1 and G1 over every discrete setting flipped back and forth by the harness model ----
const std::array<int, 24> kWeights = {0, 200, 400, 520, 526, 510, 490, 476, 475, 474, 500, 525, 526, 700, 1000, 800, 600, 500, 474, 300, 0, 600, 1000, 0};
constexpr int kMorphPeriod = 1500;

void morphInputs(Session& s) {
  s.ins = {K(0, "a", 61), K(0, "b", 66), K(0, "c", 70), O(9000, "b"), Ped(12000, true), K(13000, "d", 64), O(14500, "d"), Ped(16000, false),
           O(20000, "a"), K(21000, "g", 58), K(24000, "h", 72), O(26000, "h"), Pan(30500), K(31000, "e", 62), K(31000, "f", 67), O(35000, "e")};
}
// A session whose settings come from the harness model sweeping corner A -> B and back (kWeights, one control every 1500 samples).
Session morphSession(const Settings& A, const Settings& B, int* switches = nullptr) {
  Session s;
  s.total = static_cast<int>(kWeights.size() + 1) * kMorphPeriod;
  s.scale = kCMajorScale;
  s.base = A;
  s.base.enabled = true;
  MorphModel m{A, B};
  int last = 0, sw = 0;
  for (std::size_t i = 0; i < kWeights.size(); ++i) {
    Settings snap = m.at(kWeights[i]);
    if (m.cur != last) { ++sw; last = m.cur; }
    s.cfg.push_back(C(static_cast<int>(i + 1) * kMorphPeriod, [snap](Settings& x) { x = snap; }));
  }
  if (switches) *switches = sw;
  morphInputs(s);
  return s;
}
Session morphSessionFor(const Flip& f, int* switches = nullptr) {
  Settings base;
  base.enabled = true;
  base.gate = 90;
  base.swing = 560;
  base.seed = 5;
  f.prep(base);
  Settings A = base, Bc = base;
  f.set(A, false);
  f.set(Bc, true);
  if (f.key != "enabled") A.enabled = Bc.enabled = true;
  return morphSession(A, Bc, switches);
}
// every discrete setting at once (corner B flips all of them), with or without the three settings that apply at offset 0
Session morphAllSession(bool withOffsetZero, int* switches = nullptr) {
  Settings A;
  A.enabled = true;
  A.gate = 60; A.swing = 520; A.velFixed = 40; A.seed = 5; A.quantize = Quantize::Scale; A.steps = 1; A.stepSize = 7;
  Settings Bc = A;
  Bc.gate = 150; Bc.swing = 700; Bc.velFixed = 110; Bc.stepTimeUs = 80000;
  std::vector<Flip> t = discreteTable();
  for (const Flip& f : t) {
    if (f.offsetZero && (!withOffsetZero || f.key == "enabled")) continue;
    if (f.key == "step_size" || f.key == "steps" || f.key == "quantize (scale)" || f.key == "quantize (chord_root)") continue;   // handled by the explicit B values below
    f.prep(A);
    f.prep(Bc);
  }
  A.quantize = Bc.quantize = Quantize::Scale;
  Bc.quantize = Quantize::ChordRoot;
  Bc.steps = 2; Bc.stepSize = -5;
  for (const Flip& f : t) {
    if (f.offsetZero && (!withOffsetZero || f.key == "enabled")) continue;
    if (f.key == "step_size" || f.key == "steps" || f.key == "quantize (scale)" || f.key == "quantize (chord_root)") continue;
    f.set(Bc, true);
  }
  A.chordEnabled = false;
  A.rate = Rate::R1_32;     // denser than the table's 1/16 -> 1/8, so a session has notes to follow through the switches
  Bc.rate = Rate::R1_16;
  Bc.repeats = 8;
  return morphSession(A, Bc, switches);
}

void testMorphN1() {
  std::puts("Morph: N1 and G1 over sessions whose discrete settings are switched by the morph (one terminal per note)");
  const std::vector<Flip> table = discreteTable();
  int sessions = 0, n1bad = 0, g1bad = 0, lost = 0, minSwitches = 99;
  std::int64_t events = 0;
  std::string first;
  auto judge = [&](const Session& s, const std::string& name) {
    ++sessions;
    std::string ref;
    bool g1 = false, bad = false;
    for (int block : {4096, 333, 64, 1}) {
      const Run r = run(s, block);
      events += static_cast<std::int64_t>(r.evs.size());
      const auto v = n1(r);
      if (!v.empty() && !bad) { bad = true; ++n1bad; if (first.empty()) first = name + " block " + std::to_string(block) + ": " + v[0]; }
      lost += static_cast<int>(r.core->counters().offsLost);
      const std::string st = streamOf(r);
      if (ref.empty()) ref = st; else if (st != ref && !g1) { g1 = true; ++g1bad; if (first.empty()) first = name + " G1 differs at block " + std::to_string(block); }
    }
  };
  for (const Flip& f : table) {
    int sw = 0;
    const Session s = morphSessionFor(f, &sw);
    minSwitches = std::min(minSwitches, sw);
    judge(s, f.key);
  }
  for (bool off0 : {false, true}) {
    int sw = 0;
    const Session s = morphAllSession(off0, &sw);
    minSwitches = std::min(minSwitches, sw);
    judge(s, off0 ? "all (with offset-0 settings)" : "all");
  }
  check(n1bad == 0 && g1bad == 0 && lost == 0 && minSwitches >= 5 && events > 10000,
        "N1 / G1: " + std::to_string(sessions) + " morph sessions (each of the " + std::to_string(table.size()) + " discrete settings alone, plus every setting at once, with and without the offset-0 ones), corner switches >= " + std::to_string(minSwitches) + " each, at block sizes 4096/333/64/1: N1 violations " + std::to_string(n1bad) + ", G1 divergences " + std::to_string(g1bad) + ", lost offs " + std::to_string(lost) + " (" + std::to_string(events) + " events)" + (first.empty() ? "" : " - " + first));
}

// ====================================================================== CH1: the chord stage (SPEC 14)
// The arp is OFF by default here (the module stays on): a pure chord generator. Defaults: +4 and +7 (slots 1, 2), gate 30.
Session cs(const Plant& pl, std::function<void(Settings&)> f = {}, std::vector<InEv> ins = {}, int total = 20000) {
  Session s;
  s.total = total;
  s.plants = pl;
  s.base.arpEnabled = false;
  s.base.chordEnabled = true;
  s.base.gate = 30;
  if (f) f(s.base);
  s.ins = std::move(ins);
  return s;
}
std::vector<Rec> chordOns(const Run& r) { std::vector<Rec> v; for (const Rec& e : onsOf(r)) if (e.stream == Stream::Chord) v.push_back(e); return v; }
std::set<std::int64_t> chordIds(const Run& r) { std::set<std::int64_t> v; for (const Rec& e : chordOns(r)) v.insert(e.ref.id); return v; }
// the samples at which the CHORD note(s) on `key` end (the final reset at the end of the session excluded)
std::vector<std::int64_t> chordOffs(const Run& r, int key, int total) {
  std::vector<std::int64_t> v;
  const auto ids = chordIds(r);
  for (const Rec& e : r.evs) if (e.t == EventType::NoteOff && e.key == key && ids.count(e.ref.id) && e.a < total) v.push_back(e.a);
  return v;
}
std::string chordSetAt(const Run& r, std::int64_t at) {
  std::vector<int> v;
  for (const Rec& e : chordOns(r)) if (e.a == at) v.push_back(e.key);
  std::sort(v.begin(), v.end());
  return join(v);
}
std::vector<int> arpKeys(const Run& r, std::size_t n = 1000000) { std::vector<int> v; for (const Rec& e : onsOf(r)) if (e.stream == Stream::Arp && v.size() < n) v.push_back(e.key); return v; }
std::vector<std::int64_t> arpSamples(const Run& r) { std::vector<std::int64_t> v; for (const Rec& e : onsOf(r)) if (e.stream == Stream::Arp) v.push_back(e.a); return v; }

void testCH1() {
  std::puts("CH1: the chord stage - each held key plus its shifts on stream `chord`, deduplicated by key with a source count");
  const int T = 20000;
  // (a) C + {+4, +7} = C E G on stream chord at the press; with the arp off nothing else sounds; N1 holds
  auto a = [&](const Plant& pl) {
    const Run r = run(cs(pl, {}, {K(100, "a", 60), O(5000, "a")}), 128);
    for (const Rec& e : onsOf(r)) if (e.stream != Stream::Chord) return true;
    return chordSetAt(r, 100) != "60,64,67" || !n1(r).empty() || chordOffs(r, 60, T) != std::vector<std::int64_t>{5000};
  };
  // (b) C and E held with +4 only: E (64) is made twice (by E and by C + 4): ONE note, ending only when BOTH sources are gone
  auto b = [&](const Plant& pl) {
    const Run r = run(cs(pl, [](Settings& x) { x.chordOn[1] = false; }, {K(100, "c", 60), K(100, "e", 64), O(5000, "c"), O(9000, "e")}), 128);
    int on64 = 0;
    for (const Rec& e : chordOns(r)) on64 += e.key == 64;
    return on64 != 1 || chordOffs(r, 64, T) != std::vector<std::int64_t>{9000} || chordOffs(r, 60, T) != std::vector<std::int64_t>{5000} || chordOffs(r, 68, T) != std::vector<std::int64_t>{9000} || !n1(r).empty();
  };
  // (c) degrees +2 and +4 in C major on D: D F A
  auto c = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.chordUnit = StepUnit::Degrees; x.chordShift[0] = 2; x.chordShift[1] = 4; x.quantize = Quantize::Scale; }, {K(100, "d", 62)});
    s.scale = kCMajorScale;
    return chordSetAt(run(s, 128), 100) != "62,65,69";
  };
  // (d) semitones +4 with quantize on, in C minor: C + 4 = E snaps to Eb (a tie goes down, with nothing before it); +7 = G is in the scale
  auto d = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.quantize = Quantize::Scale; }, {K(100, "c", 60)});
    s.scale = Scale{0, 0x5AD};
    return chordSetAt(run(s, 128), 100) != "60,63,67";
  };
  // (e) the arp plays the chord set when the stage is on (up over C E G on stream arp), the chord notes sound alongside
  auto e = [&](const Plant& pl) {
    const Run r = run(cs(pl, [](Settings& x) { x.arpEnabled = true; x.order = Order::Up; }, {K(100, "c", 60)}), 128);
    const auto ak = arpKeys(r, 9);
    return ak.size() < 9 || join(ak) != "60,64,67,60,64,67,60,64,67" || chordOns(r).size() != 3;
  };
  // (f) a shape change mid-hold: +7 becomes +9 at 5000: G leaves, A joins, both at offset 0 of that block; C and E keep sounding
  auto f = [&](const Plant& pl) {
    Session s = cs(pl, {}, {K(100, "c", 60)});
    s.cfg = {C(5000, [](Settings& x) { x.chordShift[1] = 9; })};
    const Run r = run(s, 128);
    std::vector<std::string> at5k;
    const auto ids = chordIds(r);
    for (const Rec& x : r.evs) if (x.a == 5000 && ((x.t == EventType::NoteOn && x.stream == Stream::Chord) || (x.t == EventType::NoteOff && ids.count(x.ref.id)))) at5k.push_back(std::string(typeStr(x.t)) + std::to_string(x.key));
    std::sort(at5k.begin(), at5k.end());
    int c60 = 0, c64 = 0;
    for (const Rec& x : chordOns(r)) { c60 += x.key == 60; c64 += x.key == 64; }
    return at5k.size() != 2 || at5k[0] != "off67" || at5k[1] != "on69" || c60 != 1 || c64 != 1 || !n1(r).empty();
  };
  // (g) turning the stage off ends every chord note at that offset, and nothing more starts
  auto g = [&](const Plant& pl) {
    Session s = cs(pl, {}, {K(100, "c", 60)});
    s.cfg = {C(5000, [](Settings& x) { x.chordEnabled = false; })};
    const Run r = run(s, 128);
    int offs5k = 0;
    for (const Rec& x : r.evs) offs5k += x.t == EventType::NoteOff && x.a == 5000;
    return offs5k != 3 || chordOns(r).size() != 3;
  };
  // (h) the pedal holds the chord: a key released under the pedal keeps its chord until the pedal comes up
  auto h = [&](const Plant& pl) {
    const Run r = run(cs(pl, {}, {Ped(50, true), K(100, "c", 60), O(2000, "c"), Ped(6000, false)}), 128);
    return chordOffs(r, 60, T) != std::vector<std::int64_t>{6000} || chordOffs(r, 67, T) != std::vector<std::int64_t>{6000} || !n1(r).empty();
  };
  // (i) the strand case: chord_root quantize, D held, then C pressed, then D released. The mapping is STORED when a key enters H, so releasing D
  //     drops exactly the keys it made (62, 66, 69) at its release although the anchor moved from D to C; C's own (60, 64, 67) end with C
  auto iT = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.quantize = Quantize::ChordRoot; }, {K(100, "d", 62), K(3000, "c", 60), O(6000, "d"), O(9000, "c")}, 14000);
    s.scale = kCMajorScale;
    const Run r = run(s, 128);
    if (chordOns(r).size() != 6 || !n1(r).empty()) return true;
    for (int k : {62, 66, 69}) if (chordOffs(r, k, 14000) != std::vector<std::int64_t>{6000}) return true;
    for (int k : {60, 64, 67}) if (chordOffs(r, k, 14000) != std::vector<std::int64_t>{9000}) return true;
    return false;
  };
  // (j) both streams on, and the arp plays the chord's own keys: an arp note on a chord key must NOT end the chord note (T2 is within one stream)
  auto jT = [&](const Plant& pl) {
    const Run r = run(cs(pl, [](Settings& x) { x.arpEnabled = true; x.order = Order::Up; }, {K(100, "c", 60), O(9000, "c")}), 128);
    if (arpKeys(r).size() < 6 || chordOns(r).size() != 3 || !n1(r).empty()) return true;
    for (int k : {60, 64, 67}) if (chordOffs(r, k, T) != std::vector<std::int64_t>{9000}) return true;
    return false;
  };
  // (k) the arp switching off and on leaves the chord notes sounding (no off, no restart); the arp itself stops at 4000 and resumes at 6000
  auto kT = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.arpEnabled = true; x.order = Order::Up; }, {K(100, "c", 60), O(9000, "c")});
    s.cfg = {C(4000, [](Settings& x) { x.arpEnabled = false; }), C(6000, [](Settings& x) { x.arpEnabled = true; })};
    const Run r = run(s, 128);
    const auto as = arpSamples(r);
    bool inGap = false, before = false, after = false;
    for (std::int64_t x : as) { inGap = inGap || (x > 4000 && x < 6000); before = before || x < 4000; after = after || x >= 6000; }
    if (chordOns(r).size() != 3 || inGap || !before || !after || !n1(r).empty()) return true;
    for (int k : {60, 64, 67}) if (chordOffs(r, k, T) != std::vector<std::int64_t>{9000}) return true;
    return false;
  };
  // (l) a transport stop / start (T6) leaves chord notes alone: three ons, offs at the key release only
  auto lT = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.arpEnabled = true; x.order = Order::Up; }, {K(100, "c", 60), O(9000, "c")});
    auto h = std::make_shared<SimHost>(8000, 120, 0, true);
    h->set(3000, false);
    h->set(6000, true, 0.0);
    s.host = h;
    const Run r = run(s, 128);
    if (chordOns(r).size() != 3 || !n1(r).empty()) return true;
    for (int k : {60, 64, 67}) if (chordOffs(r, k, T) != std::vector<std::int64_t>{9000}) return true;
    return false;
  };
  // (m) the arp's T11 eviction (a 1-entry table, long gates) never touches chord notes: they end at the release only
  auto mT = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.arpEnabled = true; x.order = Order::Up; x.gate = 200; }, {K(100, "c", 60), O(9000, "c")}, 14000);
    s.caps.sounding = 1;
    const Run r = run(s, 128);
    if (r.core->counters().soundingOverflow < 1 || chordOns(r).size() != 3 || !n1(r).empty()) return true;
    for (int k : {60, 64, 67}) if (chordOffs(r, k, 14000) != std::vector<std::int64_t>{9000}) return true;
    return false;
  };
  // (n) a count going 1 -> 0 -> 1 inside one sample keeps its note: C released and re-pressed at 4000 gives no new on and no off
  auto nT = [&](const Plant& pl) {
    const Run r = run(cs(pl, {}, {K(100, "c", 60), O(4000, "c"), K(4000, "c2", 60), O(9000, "c2")}), 128);
    if (chordOns(r).size() != 3) return true;
    for (int k : {60, 64, 67}) if (chordOffs(r, k, T) != std::vector<std::int64_t>{9000}) return true;
    return false;
  };
  // (o) dedupe by KEY alone: C on channel 0 and E on channel 1 both make E (C + 4); it is one chord note
  auto oT = [&](const Plant& pl) {
    const Run r = run(cs(pl, [](Settings& x) { x.chordOn[1] = false; }, {K(100, "c", 60), K(100, "e", 64, 100, 1), O(9000, "c"), O(9000, "e", 1)}), 128);
    int on64 = 0;
    for (const Rec& x : chordOns(r)) on64 += x.key == 64;
    return on64 != 1;
  };
  // (p) latch applies at OFFSET 0 of its block: with the arp OFF there are no arp steps, so a latched chord must end exactly when latch goes off
  auto pT = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.latch = Latch::Replace; }, {K(100, "c", 60), O(2000, "c")});
    s.cfg = {C(6000, [](Settings& x) { x.latch = Latch::Off; })};
    const Run r = run(s, 128);
    if (chordOns(r).size() != 3 || !n1(r).empty()) return true;
    for (int k : {60, 64, 67}) if (chordOffs(r, k, T) != std::vector<std::int64_t>{6000}) return true;
    return false;
  };
  // (q) with the arp ON the latched arp stops at the latch-off block too: no arp note from 6000 on, the chord ends there, N1 holds
  auto qT = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.arpEnabled = true; x.order = Order::Up; x.latch = Latch::Replace; }, {K(100, "c", 60), O(2000, "c")});
    s.cfg = {C(6000, [](Settings& x) { x.latch = Latch::Off; })};
    const Run r = run(s, 128);
    bool late = false;
    for (std::int64_t x : arpSamples(r)) late = late || x >= 6000;
    if (arpKeys(r).size() < 4 || late || !n1(r).empty()) return true;
    for (int k : {60, 64, 67}) if (chordOffs(r, k, T) != std::vector<std::int64_t>{6000}) return true;
    return false;
  };
  // (r) ONE quantize switch (D-020): C# held in C major with quantize on and +4 +7: EVERY chord key is snapped, the played note included
  //     (61 -> 60, a tie goes down), so the chord is C F G (60 65 67); with quantize OFF the +0 slot is the played key (61, 65, 68)
  auto rT = [&](const Plant& pl) {
    Session on = cs(pl, [](Settings& x) { x.quantize = Quantize::Scale; }, {K(100, "c", 61)});
    on.scale = kCMajorScale;
    Session off = cs(pl, [](Settings& x) { x.quantize = Quantize::Off; }, {K(100, "c", 61)});
    off.scale = kCMajorScale;
    return chordSetAt(run(on, 128), 100) != "60,65,67" || chordSetAt(run(off, 128), 100) != "61,65,68";
  };
  // (s) a session of out-of-scale presses with both streams on: every arp AND chord NoteOn is in the effective scale while quantize is on
  auto sT = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.arpEnabled = true; x.quantize = Quantize::Scale; x.order = Order::Up; x.chordOn[2] = true; x.chordShift[2] = 12; });
    s.scale = kCMajorScale;
    int i = 0;
    for (int k : {61, 63, 66, 68, 70}) { s.ins.push_back(K(100 + 3000 * i, ("k" + std::to_string(i)).c_str(), k)); ++i; }
    const Run r = run(s, 128);
    bool anyChord = false, anyArp = false;
    for (const Rec& x : onsOf(r)) { anyChord = anyChord || x.stream == Stream::Chord; anyArp = anyArp || x.stream == Stream::Arp; if (((kCMajor >> (x.key % 12)) & 1u) == 0) return true; }
    return onsOf(r).size() < 20 || !anyChord || !anyArp;
  };
  // (t) the arp's chord_root anchors on the lowest PLAYED key, never on the lowest key of the chord set (critic L3): C# held with a -3 chord key
  //     (58): the set is 58 61 68, all in C major anchored on 61; anchored on the set's own lowest (58) the arp would move 61 and 68
  auto tT = [&](const Plant& pl) {
    Session s = cs(pl, [](Settings& x) { x.arpEnabled = true; x.order = Order::Up; x.quantize = Quantize::ChordRoot; x.chordShift[0] = -3; x.chordShift[1] = 7; x.gate = 30; }, {K(100, "c", 61)}, 12000);
    s.scale = kCMajorScale;
    const Run r = run(s, 128);
    const auto ak = arpKeys(r, 6);
    return chordSetAt(r, 100) != "58,61,68" || join(ak) != "58,61,68,58,61,68";
  };
  plant(pT({"latchAtNextStep"}), "CH1 detects latch applied at the next arp step (with the arp off a latched chord never ends)");
  plant(rT({"chordPlusZeroUnquantized"}), "CH1 detects the +0 slot (the played note) left unquantized while quantize is on");
  plant(sT({"chordSkipQuantize"}), "CH1 / Q1 detects chord-stream notes out of the effective scale while quantize is on");
  plant(iT({"chordRecomputeAtRelease"}), "CH1 detects a mapping recomputed at release (the stranded note after an anchor change)");
  plant(jT({"t2AcrossStreams"}), "CH1 detects a retrigger on one stream ending the other stream's note on that key");
  plant(kT({"arpToggleEndsChord"}), "CH1 detects the arp's on/off ending chord notes");
  plant(lT({"chordEndsAtEdge"}), "CH1 detects a transport edge ending and restarting chord notes");
  plant(mT({"sharedSoundingTable"}), "CH1 detects arp eviction (T11) reaching a chord note");
  plant(nT({"chordSyncPerEvent"}), "CH1 detects a count going 1-0-1 within one sample that ends and restarts its note");
  plant(oT({"chordDedupeByChannelKey"}), "CH1 detects dedupe by (channel, key) instead of key");
  plant(b({"chordNoDedupe"}), "CH1 detects no dedupe (two ons for a shared key)");
  plant(b({"chordEndsOnFirstSource"}), "CH1 detects a chord note that ends when its FIRST source is released");
  plant(e({"arpIgnoresChord"}), "CH1 detects the arp reading the raw keys instead of the chord set");
  plant(f({"chordRestartAll"}), "CH1 detects a shape change that restarts the unchanged chord notes");
  plant(tT({"chordRootFromChordSet"}), "CH1 detects the arp's chord_root anchored on the chord set's lowest key instead of the lowest PLAYED key");
  const std::vector<std::pair<const char*, std::function<bool(const Plant&)>>> all = {
      {"a", a}, {"b", b}, {"c", c}, {"d", d}, {"e", e}, {"f", f}, {"g", g}, {"h", h}, {"i", iT}, {"j", jT}, {"k", kT}, {"l", lT}, {"m", mT}, {"n", nT}, {"o", oT}, {"p", pT}, {"q", qT}, {"r", rT}, {"s", sT}, {"t", tT}};
  std::string bad;
  for (const auto& kv : all) if (kv.second({})) bad += std::string(kv.first) + " ";
  check(bad.empty(), "CH1: C + {+4, +7} = C E G as stream chord at the press (arp off: only chord notes); a shared key is emitted once and ends with its LAST source; degrees +2 +4 on D in C major = D F A; semitones +4 with quantize on in C minor snaps to Eb; the arp plays the chord set; a shape change moves only leavers and joiners at offset 0; stage off ends all; the pedal holds the chord; a stored mapping survives an anchor change (no stranded note); both streams sound on one key; the arp toggle, a transport edge and arp eviction never touch chord notes; 1-0-1 in one sample keeps its note; dedupe is by key; latch-off applies at offset 0; one quantize switch snaps the chord too, the played note included (C# in C major = C F G); every arp and chord NoteOn is in the scale; chord_root anchors the arp on the lowest played key" + (bad.empty() ? "" : " - FAILED: " + bad));
}

// More chord-stage known answers (no plant: exact values, not an invariant checker)
void testChordKnown() {
  std::puts("CH1: more known answers (order of H', range, cap, module switch, T21)");
  const int T = 20000;
  { // the order of H' under `played`, worked out: step 1000 -> Pp = [60 64 67] m = 0 -> 60; 2000 -> 64; at 3000 E joins: Pp = [60 64 67 68 71]; m = 2 -> 67, then 68, 71, 60, 64
    Session s = cs({}, [](Settings& x) { x.arpEnabled = true; x.order = Order::Played; });
    s.ins = {K(100, "c", 60), K(3000, "e", 64)};
    const auto k = arpKeys(run(s, 128), 9);
    check(join(k) == "60,64,67,68,71,60,64,67,68", "known answer: `played` over the chord set orders by first source press, then slot: C (60 64 67) then E adds 68 71 after them");
  }
  { // an input key whose chord keys fall off the top of 0-127 is dropped (T16, counted): 125 + {+4, +7} = 129, 132 are dropped; 125 sounds
    Session s = cs({}, {}, {K(100, "h", 125)});
    const Run r = run(s, 128);
    check(chordSetAt(r, 100) == "125" && r.core->counters().keysDropped >= 2 && n1(r).empty(), "known answer: 125 + 4 and + 7 fall off 127: only the played key sounds, two keys dropped and counted (T16)");
  }
  { // an event cap of 6 admits three chord ons (events + sounding + 2 <= 6 each time); the fourth is refused, stays silent, is counted, and owes no off
    Session s = cs({}, [](Settings& x) { x.chordOn[2] = true; x.chordShift[2] = 12; }, {K(100, "c", 60), O(5000, "c")});
    s.caps.events = 6;
    const Run r = run(s, 128);
    check(chordOns(r).size() == 3 && r.core->counters().refused >= 1 && n1(r).empty() && r.core->counters().offsLost == 0, "known answer (T13): at an event cap of 6 the 4th chord note is refused (counted), silent, with no off owed; N1 holds");
  }
  { // the module's `enabled` ends every chord note at offset 0 (T4) and the stage forgets its keys; on again with the key still held it maps afresh at that offset
    Session s = cs({}, {}, {K(100, "c", 60)}, 12000);
    s.cfg = {C(5000, [](Settings& x) { x.enabled = false; }), C(7000, [](Settings& x) { x.enabled = true; })};
    const Run r = run(s, 128);
    int offs5k = 0;
    for (const Rec& e : r.evs) offs5k += e.t == EventType::NoteOff && e.a == 5000;
    check(offs5k == 3 && chordSetAt(r, 100) == "60,64,67" && chordSetAt(r, 7000) == "60,64,67" && n1(r).empty(), "known answer (T4): enabled off ends all three chord notes at 5000; enabled on at 7000 with the key still held starts them afresh");
  }
  { // Panic ends everything once and clears H (T7); a later press maps afresh
    Session s = cs({}, {}, {K(100, "c", 60), Pan(5000), K(6000, "d", 62)}, 12000);
    const Run r = run(s, 128);
    check(chordSetAt(r, 100) == "60,64,67" && chordSetAt(r, 6000) == "62,66,69" && n1(r).empty() && chordOffs(r, 60, 12000) == std::vector<std::int64_t>{5000}, "known answer (T7): a Panic at 5000 ends the three chord notes there (once each); the next press maps afresh");
  }
  { // a latched chord persists after the key is released and ends with the latch
    Session s = cs({}, [](Settings& x) { x.latch = Latch::Append; }, {K(100, "c", 60), O(1000, "c"), K(3000, "c2", 60)});
    const Run r = run(s, 128);
    check(chordOns(r).size() == 3 && chordOffs(r, 60, T) == std::vector<std::int64_t>{3000}, "known answer: under latch append the chord outlives the key release (1000) and a second press of the key toggles it out (3000)");
  }
  { // the global scale is part of the re-map signature: C# held in C major (60 65 67), then horde's scale becomes C minor at 5000: 67 -> 68 at offset 0,
    // 60 and 65 keep sounding
    Session s = cs({}, [](Settings& x) { x.quantize = Quantize::Scale; }, {K(100, "c", 61)});
    s.scale = kCMajorScale;
    s.scaleAt = {{5000, Scale{0, 0x5AD}}};
    const Run r = run(s, 128);
    std::vector<std::string> at5k;
    const auto ids = chordIds(r);
    for (const Rec& x : r.evs) if (x.a == 5000 && ((x.t == EventType::NoteOn && x.stream == Stream::Chord) || (x.t == EventType::NoteOff && ids.count(x.ref.id)))) at5k.push_back(std::string(typeStr(x.t)) + std::to_string(x.key));
    std::sort(at5k.begin(), at5k.end());
    check(chordSetAt(r, 100) == "60,65,67" && at5k == std::vector<std::string>({"off67", "on68"}) && n1(r).empty(), "known answer: a change of horde's global scale re-maps the stored chord at offset 0: 67 leaves, 68 joins, 60 and 65 keep sounding");
  }
  { // degrees start from the played key's NEAREST in-scale key: C# in C major snaps to 60, so -1 degree is 59 and +2 is 64 (a raw 61 would give 60 and 64)
    Session s = cs({}, [](Settings& x) { x.chordUnit = StepUnit::Degrees; x.chordShift[0] = -1; x.chordShift[1] = 2; x.quantize = Quantize::Scale; }, {K(100, "c", 61)});
    s.scale = kCMajorScale;
    check(chordSetAt(run(s, 128), 100) == "59,60,64", "known answer: degrees count from the played key's snapped position (C# -> C): -1 is B (59), +2 is E (64), the +0 slot is 60");
  }
  { // Pp is ordered by first source press, then SLOT (not key): C with -5 and +7 makes slot 0 = 60, slot 1 = 55, slot 2 = 67, so `played` plays 60 55 67
    Session s = cs({}, [](Settings& x) { x.arpEnabled = true; x.order = Order::Played; x.chordShift[0] = -5; x.chordShift[1] = 7; }, {K(100, "c", 60)});
    check(join(arpKeys(run(s, 128), 6)) == "60,55,67,60,55,67", "known answer: `played` over one source's chord goes by slot, not by pitch: 60 55 67 60 55 67");
  }
  { // capacities: 32 held keys with +1 +2 +3 make all 128 keys (the chord stage's own table), and the arp's `chord` order sounds all 128 beside them
    Session s = cs({}, [](Settings& x) { x.arpEnabled = true; x.order = Order::Chord; x.gate = 100; x.chordOn[2] = true; x.chordShift[0] = 1; x.chordShift[1] = 2; x.chordShift[2] = 3; }, {}, 3000);
    for (int i = 0; i < 32; ++i) s.ins.push_back(Kk(100, 4 * i));
    const Run r = run(s, 128);
    int arpAtFirstStep = 0;
    for (const Rec& e : onsOf(r)) arpAtFirstStep += e.stream == Stream::Arp && e.a == 1000;
    check(chordOns(r).size() == 128 && arpAtFirstStep == 128 && n1(r).empty() && r.core->counters().offsLost == 0, "known answer (SPEC 14 capacity): 32 held keys x (+0 +1 +2 +3) sound 128 chord notes, and the arp's 128 notes sound beside them (separate tables); N1 holds");
  }
  { // arp_enabled off clears the arp's pending queue (SPEC 14 note 14): ratchet sub-notes frozen at evaluation never sound after it
    Session s = cs({}, [](Settings& x) { x.chordEnabled = false; x.arpEnabled = true; x.swing = 750; x.pattern = Pattern::User; x.gate = 100; }, {K(0, "a", 60)}, 6000);
    s.userPattern = userPat({B().ratchet(4)});
    s.cfg = {C(600, [](Settings& x) { x.arpEnabled = false; })};
    const Run r = run(s, 64);
    int late = 0;
    for (const Rec& e : onsOf(r)) late += e.a > 600;
    check(late == 0 && onsOf(r).size() == 2 && n1(r).empty(), "known answer: arp_enabled off at 600 (sub-notes at 0 and 375 sounded, 750 and 1125 pending) ends the arp's notes and clears its pending queue: nothing sounds after");
  }
  { // T10 across prepare(): chord notes still sounding when prepare() is called are owed exactly one NoteOff, delivered at offset 0 of the next block
    Session s = cs({}, {}, {K(100, "c", 60)}, 8000);
    s.prepares = {3500};
    const Run r = run(s, 128);
    int offsAt3500 = 0;
    for (const Rec& e : r.evs) offsAt3500 += e.t == EventType::NoteOff && e.a == 3500;
    check(offsAt3500 == 3 && chordOns(r).size() == 3 && n1(r).empty(), "known answer (T10): prepare() with a held chord owes its three chord notes one NoteOff each, at offset 0 of the next block (3500); N1 holds");
  }
  { // T21 acted: a chord note ended by its last source, and not at a transport edge
    const Run r = run(cs({}, {}, {K(100, "c", 60), O(5000, "c")}), 128);
    check(r.core->counters().rows[21] == 3, "T21: three chord notes ended (the stage's last source left), counted as the row's evidence");
  }
  { // chord notes carry the channel and velocity of their EARLIEST source, fixed at the NoteOn: E (ch 1, vel 40) pressed first makes 64 first, then C (ch 0, vel 90) joins: 64 keeps E's
    Session s = cs({}, [](Settings& x) { x.chordOn[1] = false; }, {K(100, "e", 64, 40, 1), K(2000, "c", 60, 90, 0)});
    const Run r = run(s, 128);
    int ch = -1, vel = -1;
    for (const Rec& e : chordOns(r)) if (e.key == 64) { ch = e.ref.channel; vel = e.vel; }
    check(ch == 1 && vel == 40 && chordOns(r).size() == 3, "known answer: the shared key 64 keeps the channel (1) and velocity (40) of its earliest source, fixed at its NoteOn (E makes 64 and 68, C then adds only 60: three chord notes)");
  }
}

// ====================================================================== S1 extended: every arp NoteOn carries `arp`, every chord-stage NoteOn carries `chord`
void testS1() {
  std::puts("S1: arp ons carry `arp`, chord-stage ons carry `chord`");
  // The chord stage does not depend on the arp, so the chord-tagged ons of a stage + arp run must be EXACTLY those of the same stage run
  // with the arp off; every other on is the arp's and must be tagged `arp`.
  auto session = [&](const Plant& pl, bool arpOn) {
    Session s = cs(pl, [arpOn](Settings& x) { x.arpEnabled = arpOn; x.order = Order::ChordWalk; x.gate = 40; }, {K(100, "a", 60), K(3100, "b", 65), O(6100, "a"), K(7100, "c", 52)}, 14000);
    return s;
  };
  auto bad = [&](const Plant& pl) {
    auto sig = [](const Run& r) { std::string t; for (const Rec& e : chordOns(r)) t += std::to_string(e.a) + ":" + std::to_string(e.key) + ":" + std::to_string(e.vel) + ","; return t; };
    const Run ref = run(session({}, false), 64), both = run(session(pl, true), 64);
    std::size_t arpTagged = 0, ons = 0;
    for (const Rec& e : onsOf(both)) { ++ons; arpTagged += e.stream == Stream::Arp; }
    for (const Rec& e : onsOf(ref)) if (e.stream != Stream::Chord) return true;
    return sig(ref) != sig(both) || arpTagged < 20 || ons - arpTagged != chordOns(ref).size();
  };
  plant(bad({"untaggedOn"}), "S1 detects an arp NoteOn that lost its `arp` tag (some, not all)");
  plant(bad({"chordTaggedArp"}), "S1 detects a chord-stage NoteOn tagged `arp` (some, not all)");
  check(!bad({}), "S1: in a stage + arp run every chord-stage NoteOn carries `chord` (exactly the ons of the stage-only run) and every arp NoteOn (steps, chord steps) carries `arp`");
}

// ====================================================================== lastQ belongs to the arp (D-020): the stage neither reads nor writes it
// Searched once, then pinned: C major, the arp plays `up` with a +1 semitone pass (so its ties follow lastQ), and a key is pressed mid-run whose
// snap is a TIE (C#: 60 or 62) or has none. The stage must resolve its tie to the LOWER key and leave the arp's lastQ alone.
Session lastQScene(const Plant& pl, int pressKey, int pressAt) {
  Session s = cs(pl, [](Settings& x) { x.arpEnabled = true; x.order = Order::Up; x.quantize = Quantize::Scale; x.steps = 1; x.stepSize = 1; x.chordOn[0] = false; x.chordOn[1] = false; x.gate = 40; }, {K(100, "a", 72), K(100, "b", 79), K(pressAt, "p", pressKey)}, 16000);
  s.scale = kCMajorScale;
  return s;
}
void testLastQ() {
  std::puts("CH1 / D-020: the chord stage never reads or writes the arp's lastQ");
  // READ: C# (61) pressed at 2000 while the arp has just played 79 (above): the stage's snap tie 60 / 62 must still go to the LOWER key. (Scene found
  // by searching press times 2000..9000 once; pinned here.)
  auto chordKey = [&](const Plant& pl) { const Run r = run(lastQScene(pl, 61, 2000), 128); for (const Rec& e : chordOns(r)) if (e.a == 2000) return e.key; return -1; };
  plant(chordKey({"chordReadsLastQ"}) == 62, "CH1 detects a chord stage that resolves its snap tie toward the arp's lastQ (C# goes UP to 62 after the arp's 79)");
  check(chordKey({}) == 60, "known answer: the stage snaps C# to the LOWER key 60 whatever the arp's lastQ is (ties always go down in the stage, D-020)");
  // WRITE: a C# pressed at 4000 is snapped to 60 by the stage; that must leave the arp's lastQ at the arp's own last output (79, above). The arp's next
  // note is then a tie (73 -> 72 / 74 in its +1 pass, shifted over the new key to 61 -> 60 / 62) that follows ITS lastQ: UP to 62. A stage that wrote its
  // 60 into lastQ would send the tie down to 60.
  auto arpAfter = [&](const Plant& pl) { const Run r = run(lastQScene(pl, 61, 4000), 128); std::vector<int> v; for (const Rec& e : onsOf(r)) if (e.stream == Stream::Arp && e.a >= 4000 && v.size() < 6) v.push_back(e.key); return join(v); };
  plant(arpAfter({"chordWritesLastQ"}) == "60,72,79,60,72,79", "CH1 detects a chord stage that writes its snapped keys into the arp's lastQ (the arp's next tie goes DOWN to 60)");
  check(arpAfter({}) == "62,72,79,60,72,79", "known answer: after the stage snapped a pressed C# the arp's own tie still resolves toward ITS lastQ (79, above): 62 72 79 60 72 79");
}

// ====================================================================== seeded sessions: the M2a and M2b features on top of the M1b sessions
// M2b's draws, from their own generator: restart, repeats, the chord stage (on/off, unit, slots, shifts), and settings changes of the same.
void applyM2bDraws(Session& s, std::uint32_t seed) {
  std::mt19937 g(seed * 2246822519u + 0x2B);
  auto rnd = [&](int n) { return static_cast<int>(g() % static_cast<std::uint32_t>(n)); };
  s.base.restart = static_cast<Restart>(rnd(3));
  s.base.repeats = rnd(4) == 0 ? 1 + rnd(4) : 0;
  s.base.chordEnabled = rnd(3) != 0;
  s.base.chordUnit = rnd(2) ? StepUnit::Degrees : StepUnit::Semitones;
  for (int i = 0; i < 6; ++i) { s.base.chordOn[i] = rnd(2) == 0; s.base.chordShift[i] = rnd(49) - 24; }
  if (rnd(3) == 0) {   // a plain, fast pattern with a short repeat count, so the repeats really run out in this session
    s.base.pattern = Pattern::None;
    s.base.steps = 0;
    s.base.repeats = 1 + rnd(2);
    s.base.rate = static_cast<Rate>(12 + rnd(10));
  }
  const int num = 2 + rnd(5), den = rnd(2) ? 4 : 8;
  const bool phased = rnd(2) == 1;
  const double phase = rnd(16) / 4.0;
  if (!s.tweak) s.tweak = [num, den, phased, phase](int, Transport& t) { t.timeSigNum = num; t.timeSigDen = den; t.hasBarStart = phased; t.barStartBeats = phase; };   // constant per session, so no block cut is needed
  for (int i = rnd(8); i > 0; --i) {
    const int at = 1 + rnd(s.total - 1), what = rnd(9), v = static_cast<int>(g() % 1000);
    s.cfg.push_back(C(at, [what, v](Settings& x) {
      switch (what) {
        case 0: x.restart = static_cast<Restart>(v % 3); break;
        case 1: x.repeats = v % 5 == 0 ? 0 : v % 5; break;
        case 2: x.chordEnabled = v % 2 == 0; break;
        case 3: x.chordUnit = v % 2 ? StepUnit::Degrees : StepUnit::Semitones; break;
        case 4: x.chordOn[v % 6] = (v / 6) % 2 == 0; break;
        case 5: x.chordShift[v % 6] = (v / 6) % 49 - 24; break;
        case 6: x.arpEnabled = v % 3 != 0; break;
        case 7: x.quantize = static_cast<Quantize>(v % 3); x.scaleRoot = v % 12; break;
        default: x.latch = static_cast<Latch>(v % 3); break;
      }
    }));
  }
}
Session featureFuzz(std::uint32_t seed, bool keyRefs) {
  Session s = m1bFuzzSession(seed, keyRefs);
  applyM2aDraws(s, seed);
  applyM2bDraws(s, seed);
  return s;
}
void testFuzzFeatures() {
  std::puts("Seeded random sessions (NEW set of 30): restart, repeats, the chord stage, quantize, degrees, swing on top of the M1b sessions");
  int n1bad = 0, g1bad = 0, lost = 0, chordSeen = 0, exhaustedSeen = 0;
  std::string first;
  std::int64_t events = 0;
  constexpr int kSessions = 30;
  for (std::uint32_t i = 0; i < kSessions; ++i) {
    const Session s = featureFuzz(3000 + i, i % 2 == 1);
    std::string ref;
    bool g1 = false, bad = false;
    for (int block : {4096, 333, 64, 1}) {
      const Run r = run(s, block);
      events += static_cast<std::int64_t>(r.evs.size());
      const auto v = n1(r);
      if (!v.empty() && !bad) { bad = true; ++n1bad; if (first.empty()) first = "session " + std::to_string(i) + " block " + std::to_string(block) + ": " + v[0]; }
      lost += static_cast<int>(r.core->counters().offsLost);
      if (block == 4096) { chordSeen += !chordOns(r).empty(); exhaustedSeen += r.core->counters().rows[20] > 0; }
      const std::string st = streamOf(r);
      if (ref.empty()) ref = st; else if (st != ref && !g1) { g1 = true; ++g1bad; if (first.empty()) first = "session " + std::to_string(i) + " G1 differs at block " + std::to_string(block); }
    }
  }
  check(n1bad == 0 && g1bad == 0 && lost == 0 && events > 2000 && chordSeen >= 8 && exhaustedSeen >= 2,
        std::to_string(kSessions) + " seeded sessions (the M1b draws plus restart bar / free, repeats, the chord stage with all its settings and changes, quantize, degrees, swing; id-ful and id-less refs): N1 violations " + std::to_string(n1bad) + ", G1 divergences " + std::to_string(g1bad) + ", lost offs " + std::to_string(lost) + " (" + std::to_string(events) + " events; " + std::to_string(chordSeen) + " sessions sounded chord notes, " + std::to_string(exhaustedSeen) + " ran their repeats out)" + (first.empty() ? "" : " - " + first));
}

}  // namespace

// ====================================================================== the N1 / G1 / G4 / A1 sessions
Session m2bSession(const char* name) {
  const std::string n = name;
  Session s;
  if (n == "restartrepeats") {   // restart bar / free / chord switching under the host clock (stop, start, a jump), a 3/4 bar, a rate that does not divide a bar, repeats running out
    s.total = 48000; s.base.gate = 70; s.base.seed = 4; s.base.order = Order::Up; s.base.rate = Rate::R1_32; s.base.pattern = Pattern::None;
    auto h = std::make_shared<SimHost>(8000, 120, 0, true);
    h->set(14000, false); h->set(18000, true, 0.0); h->set(30000, std::nullopt, 9.0);
    s.host = h;
    s.cfg = {C(2000, [](Settings& x) { x.repeats = 6; }),
             C(8000, [](Settings& x) { x.restart = Restart::Bar; x.pattern = Pattern::Gallop; x.repeats = 3; }),
             C(12000, [](Settings& x) { x.restart = Restart::Free; x.repeats = 0; }),
             C(16000, [](Settings& x) { x.rate = Rate::R3_32; }),
             C(20000, [](Settings& x) { x.restart = Restart::Chord; x.pattern = Pattern::TiedLegato; x.repeats = 3; }),
             C(26000, [](Settings& x) { x.restart = Restart::Bar; x.rate = Rate::R1_32; x.repeats = 4; x.pattern = Pattern::None; }),
             C(32000, [](Settings& x) { x.repeats = 0; x.pattern = Pattern::TriggerPulse; }),
             C(36000, [](Settings& x) { x.pattern = Pattern::None; x.restart = Restart::Free; }),
             C(40000, [](Settings& x) { x.order = Order::Bounce; x.restart = Restart::Chord; x.repeats = 2; })};
    s.tweak = [](int, Transport& t) { t.timeSigNum = 3; t.timeSigDen = 4; t.hasBarStart = true; t.barStartBeats = 0.25; };   // constant, so no block cut is needed
    s.ins = {K(100, "a", 60), K(100, "b", 64), K(100, "c", 67), O(9000, "a"), O(9000, "b"), O(9000, "c"), K(10000, "d", 71), K(10000, "e", 59), O(13500, "d"), O(13500, "e"),
             K(14500, "f", 62), O(17000, "f"), K(18500, "g", 60), K(18500, "h", 67), Pan(24000), K(24500, "i", 60), K(24500, "j", 67), K(24500, "k", 72), O(27000, "i"), O(27000, "j"),
             O(27000, "k"), K(28000, "l", 65), O(31000, "l"), K(33000, "m", 72), K(33000, "n", 55), O(38000, "m"), O(38000, "n"), Ped(39000, true), K(40000, "o", 64), O(41000, "o"),
             Ped(43000, false), K(45000, "p", 67)};
  } else if (n == "morph") {   // every discrete setting (but the three that act at offset 0, which testMorphN1 covers) switched back and forth by the harness model of horde's morph, hysteresis included
    s = morphAllSession(false);
  } else if (n == "chordstage") {   // the chord stage with the arp, quantize, degrees, latch, pedal, panic, module and arp switches, transport edges, scale changes
    s.total = 48000; s.base.chordEnabled = true; s.base.arpEnabled = true; s.base.order = Order::Up; s.base.gate = 60; s.base.swing = 560; s.base.seed = 2;
    s.scale = kCMajorScale;
    s.scaleAt = {{30000, Scale{2, 1193}}};
    auto h = std::make_shared<SimHost>(8000, 120, 0, true);
    h->set(10000, false); h->set(11000, true, 0.0); h->set(23000, std::nullopt, 5.0);
    s.host = h;
    s.cfg = {C(4000, [](Settings& x) { x.quantize = Quantize::Scale; }),
             C(8000, [](Settings& x) { x.chordUnit = StepUnit::Degrees; x.chordShift[0] = 2; x.chordShift[1] = 4; }),
             C(12000, [](Settings& x) { x.arpEnabled = false; }),
             C(16000, [](Settings& x) { x.arpEnabled = true; x.order = Order::ChordWalk; }),
             C(20000, [](Settings& x) { x.chordOn[2] = true; x.chordShift[2] = 12; }),
             C(24000, [](Settings& x) { x.quantize = Quantize::ChordRoot; x.chordUnit = StepUnit::Semitones; }),
             C(28000, [](Settings& x) { x.latch = Latch::Replace; }),
             C(32000, [](Settings& x) { x.latch = Latch::Off; }),
             C(34000, [](Settings& x) { x.chordEnabled = false; }),
             C(36000, [](Settings& x) { x.chordEnabled = true; x.scaleSource = ScaleSource::Own; x.scaleRoot = 2; }),
             C(40000, [](Settings& x) { x.enabled = false; }),
             C(42000, [](Settings& x) { x.enabled = true; }),
             C(44000, [](Settings& x) { x.repeats = 2; })};
    s.ins = {K(1000, "a", 60), K(2000, "b", 64), O(6000, "a"), K(7000, "c", 62), Ped(13000, true), O(14000, "b"), Ped(17000, false), K(18000, "d", 61), K(18000, "e", 66),
             O(22000, "d"), K(26000, "f", 59), O(27000, "f"), K(29000, "g", 65), O(30000, "g"), Pan(37000), K(38000, "h", 60), K(38000, "i", 72), K(41000, "j", 64),
             O(43000, "h"), K(45000, "k", 67)};
  } else {
    std::abort();
  }
  return s;
}

namespace {
void testSessionsG1A1() {
  std::puts("G1 / N1 / A1 over the M2b sessions (restart, repeats, morph-driven switches, the chord stage)");
  for (const char* name : {"restartrepeats", "morph", "chordstage"}) {
    for (bool keyRefs : {false, true}) {
      Session s = m2bSession(name);
      s.keyRefs = keyRefs;
      std::string ref;
      bool same = true;
      std::int64_t events = 0;
      for (int block : {4096, 512, 64, 7, 1}) {
        const Run r = run(s, block);
        const std::string st = streamOf(r);
        if (ref.empty()) { ref = st; events = static_cast<std::int64_t>(r.evs.size()); } else same = same && st == ref;
        same = same && n1(r).empty() && r.core->counters().offsLost == 0;
      }
      check(same && events > 60, std::string("G1 / N1: session '") + name + "'" + (keyRefs ? " (id-less refs)" : "") + " is identical at block sizes 4096/512/64/7/1 with one terminal per note (" + std::to_string(events) + " events)");
    }
    Session a = m2bSession(name);
    a.countAllocs = true;
    a.resets = {a.total / 2};
    const Run r = run(a, 64);
    check(r.allocs == 0, std::string("A1: session '") + name + "' makes " + std::to_string(r.allocs) + " operator-new calls inside process() / reset() (restart and repeats changes, morph switches, chord re-maps, chord note starts and ends)");
  }
}
}  // namespace

void runM2bTests() {
  testRP1();
  testRestart();
  testMorphModel();
  testContinuousBlend();
  testLanding();
  testLandingKnown();
  testMorphN1();
  testCH1();
  testChordKnown();
  testS1();
  testLastQ();
  testSessionsG1A1();
  testFuzzFeatures();
}

}  // namespace arp_test
