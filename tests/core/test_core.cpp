// test_core.cpp - the M1a suite for core/ (ROADMAP T-003). The C++ twin of tools/lab_smoke.mjs.
//
// Charter: every detector is preceded by its planted fault, and a plant that does not fire is a
// blind detector and is itself a FAIL. Plants are the core's own `Plants` switches (compiled in
// only because this target defines ARP_PLANTS) turned on for a FRESH core. A "known answer"
// assertion (an exact expected value, not an invariant checker) carries no plant and says so.
//
//   N1 over every applicable SPEC 7 row (+ coverage), G1, G2 (ons and offs), G4, G5, D1, A1, C1,
//   the held set (SPEC 3), the free clock, the three-band edge, and `--dump-defaults` for
//   tools/core_params_check.py.
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <tuple>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
#include <string>

#include "arp/bank_data.h"
#include "arp/draw.h"
#include "harness.h"
#include "test_int128.h"
#include "test_m1b.h"
#include "test_m2a.h"
#include "test_m2b.h"
#include "test_m3.h"
#include "test_digest.h"

using namespace arp_test;

namespace {

Change C(int at, std::function<void(Settings&)> f) { return Change{at, std::move(f)}; }
// Two block sizes per session (the first also feeds the row coverage). The cap session needs a block big enough to fill a 4-slot buffer.
std::vector<int> blocksFor(const char* session) { return std::strcmp(session, "cap") == 0 ? std::vector<int>{4096, 1024} : std::vector<int>{64, 7}; }

// 120 bpm at 8 kHz: 1 beat = 4000 samples, 1/16 = 1000 samples = 3840 ticks.
constexpr std::int64_t kD8k = 60ll * 8000 * 1000;

// ------------------------------------------------------------------ N1 sessions: every applicable SPEC 7 row
Session sHeld() {   // T1 gate end, T2 same-key retrigger (one key, gate 150), T3 held-set changes, latch replace/append, pedal, T8, T9
  Session s; s.total = 44000; s.base.gate = 150;
  s.cfg = {C(20000, [](Settings& x) { x.latch = Latch::Replace; x.velMode = VelMode::Fixed; }), C(27000, [](Settings& x) { x.latch = Latch::Append; }),
           C(34000, [](Settings& x) { x.latch = Latch::Off; }), C(36000, [](Settings& x) { x.gate = 200; x.velFixed = 50; })};
  s.ins = {K(100, "A", 60), K(5100, "B", 64), O(9100, "A"), Ped(12000, true), K(12500, "C", 67), O(14500, "B"), O(15500, "C"), Ped(18000, false),
           K(21000, "D", 60), O(23000, "D"), K(25000, "E", 64), O(26000, "E"), K(28000, "F", 60), O(29000, "F"), K(30000, "G", 64), O(31000, "G"),
           K(32000, "H", 60), O(33000, "H"), K(35000, "X", 62), K(36500, "I", 60)};
  return s;
}
Session sEnable() {   // T4 / T5 via `enabled`, with pass-through events while disabled
  Session s; s.total = 30000; s.base.gate = 150;
  s.cfg = {C(8000, [](Settings& x) { x.enabled = false; }), C(12000, [](Settings& x) { x.enabled = true; }), C(20000, [](Settings& x) { x.enabled = false; }), C(24000, [](Settings& x) { x.enabled = true; })};
  s.ins = {K(100, "a", 60), K(100, "b", 64), Other(5000, 100), Other(10000, 200)};
  return s;
}
Session sTransport() {   // T6 stop / start / jump / seek back (a loop wrap is a jump) / clock switch, T14 free-clock re-base with a note sounding, H7b
  Session s; s.total = 48000; s.base.gate = 150;
  auto h = std::make_shared<SimHost>(8000, 120, 0, true);
  h->set(12000, false); h->set(20000, true, 0.0); h->set(28000, std::nullopt, 7.0); h->set(34000, std::nullopt, 1.0); h->set(38000, std::nullopt, std::nullopt, 133.7);
  s.host = h;
  s.ins = {K(100, "a", 60), K(100, "b", 64), O(14000, "a"), O(14000, "b"), K(14200, "c", 67)};
  return s;
}
Session sPanic() {   // T7: releases first, then Panic forwarded
  Session s; s.total = 24000; s.base.gate = 150;
  s.ins = {K(100, "a", 60), K(100, "b", 64), K(100, "c", 67), Pan(15000), K(17000, "d", 60)};
  return s;
}
Session sDiscrete() {   // T8 discrete changes and T15 rate changes, each mid-note
  Session s; s.total = 46000; s.base.gate = 200;
  s.cfg = {C(8000, [](Settings& x) { x.velMode = VelMode::Fixed; }), C(12000, [](Settings& x) { x.rate = Rate::R1_8; }), C(16000, [](Settings& x) { x.velMode = VelMode::Played; }),
           C(20000, [](Settings& x) { x.rate = Rate::R1_16T; }), C(24000, [](Settings& x) { x.rate = Rate::R1_32; }), C(28000, [](Settings& x) { x.latch = Latch::Replace; }),
           C(32000, [](Settings& x) { x.rate = Rate::R1_4; }), C(40000, [](Settings& x) { x.rate = Rate::R1_16; })};
  s.ins = {K(100, "a", 61), K(100, "b", 64), K(100, "c", 66)};
  return s;
}
Session sReset() {   // T10: reset() mid-run (ids restart), then a new gesture
  Session s; s.total = 30000; s.base.gate = 150; s.resets = {20000};
  s.ins = {K(100, "a", 60), K(100, "b", 64), K(21000, "c", 67)};
  return s;
}
Session sOverflow() {   // T11: a 1-entry sounding table with notes that overlap (gate 200)
  Session s; s.total = 12000; s.base.gate = 200; s.caps.sounding = 1;
  s.ins = {K(100, "a", 60), K(100, "b", 64)};
  return s;
}
Session sShort() {   // 1-sample notes (SPEC 1.1: an off never shares its on's sample) and T16 keys outside 0-127 that still count toward the period (13.11)
  Session s; s.total = 8000; s.bpm = 999; s.base.rate = Rate::R1_64; s.base.gate = 1;
  s.ins = {K(100, "a", 60), K(100, "b", 200), K(100, "c", -3)};
  return s;
}
Session sCap() {   // T13 / C1: a 4-slot event buffer; two keys at gate 200 overlap, so refusals must keep every owed off in
  Session s; s.total = 8000; s.base.gate = 200; s.caps.events = 4;
  s.ins = {K(100, "a", 60), K(100, "b", 64)};
  return s;
}
Session sFree() {   // the free clock: transport stopped, live play; T14 on a gesture start with notes still sounding
  Session s; s.total = 40000; s.base.gate = 150;
  s.host = std::make_shared<SimHost>(8000, 120, 0, false);
  s.ins = {K(100, "a", 60), K(100, "b", 64), O(6000, "a"), O(6000, "b"), K(6500, "c", 67), O(9000, "c"), K(25000, "d", 62), O(26000, "d")};
  return s;
}

Session sStyles() { return m1bSession("styles"); }
Session sPatterns() { return m1bSession("patterns"); }
Session sTimeMode() { return m1bSession("timemode"); }
Session sScaleFeel() { return m2aSession("scalefeel"); }
Session sTies() { return m2aSession("ties"); }
Session sRestartRepeats() { return m2bSession("restartrepeats"); }
Session sMorph() { return m2bSession("morph"); }
Session sChordStage() { return m2bSession("chordstage"); }

const std::vector<std::pair<const char*, Session (*)()>> kSessions = {
    {"held", sHeld}, {"enable", sEnable}, {"transport", sTransport}, {"panic", sPanic}, {"discrete", sDiscrete},
    {"reset", sReset}, {"overflow", sOverflow}, {"short", sShort}, {"cap", sCap}, {"free", sFree},
    {"styles", sStyles}, {"patterns", sPatterns}, {"timemode", sTimeMode}, {"scalefeel", sScaleFeel}, {"ties", sTies},
    {"restartrepeats", sRestartRepeats}, {"morph", sMorph}, {"chordstage", sChordStage}};

Session sessionNamed(const char* name, const Plant& plants = {}) {
  for (const auto& kv : kSessions)
    if (std::strcmp(kv.first, name) == 0) { Session s = kv.second(); s.plants = plants; return s; }
  std::abort();
}

// SPEC 7 rows this scope exercises (T20 repeats run out and T21 chord-stage notes join in M2b).
const int kRowsRequired[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
std::vector<int> missingRows(const std::int64_t* rows) {
  std::vector<int> m;
  for (int r : kRowsRequired) if (rows[r] == 0) m.push_back(r);
  return m;
}

void testN1() {
  std::puts("N1: one NoteOff per NoteOn over every applicable SPEC 7 row");
  // Plants first: each must make the N1 checker read red on a session where the clean core is green.
  auto firstViolation = [](const std::vector<std::string>& v) { return v.empty() ? std::string() : v[0]; };
  const auto dr = n1(run(sessionNamed("held", {"dropRelease"}), 64)), db = n1(run(sessionNamed("held", {"doubleRelease"}), 64)), ss = n1(run(sessionNamed("short", {"sameSampleOff"}), 64));
  plant(!dr.empty(), "N1 detects a dropped release: " + firstViolation(dr));
  plant(!db.empty(), "N1 detects a doubled release: " + firstViolation(db));
  plant(!ss.empty(), "N1 detects a 1-tick note whose off lands at its own on sample: " + firstViolation(ss));
  { // the coverage detector itself: an empty set of sessions must read as missing rows
    std::int64_t none[kRowCount] = {};
    plant(missingRows(none).size() == sizeof(kRowsRequired) / sizeof(int), "the row-coverage check reads red when no row acted");
  }
  std::vector<std::string> bad;
  std::int64_t rows[kRowCount] = {}, events = 0;
  Counters sum;
  for (const auto& kv : kSessions)
    for (int block : blocksFor(kv.first)) {
      const Run r = run(sessionNamed(kv.first), block);
      events += static_cast<std::int64_t>(r.evs.size());
      for (const std::string& b : n1(r)) bad.push_back(std::string(kv.first) + "@" + std::to_string(block) + ": " + b);
      const bool first = block == blocksFor(kv.first)[0];
      if (first) for (int i = 0; i < kRowCount; ++i) rows[i] += r.core->counters().rows[i];
      if (first) sum.refused += r.core->counters().refused, sum.soundingOverflow += r.core->counters().soundingOverflow, sum.keysDropped += r.core->counters().keysDropped;
      if (r.core->counters().offsLost != 0) bad.push_back(std::string(kv.first) + ": a NoteOff found no room");
    }
  check(bad.empty(), "N1: one terminal per note, >= 1 sample later, over " + std::to_string(kSessions.size()) + " sessions x 2 block sizes (" + std::to_string(events) + " events)" + (bad.empty() ? "" : " - " + bad[0]));
  const std::vector<int> miss = missingRows(rows);
  std::string rs;
  for (int r : kRowsRequired) rs += "T" + std::to_string(r) + "=" + std::to_string(rows[r]) + " ";
  check(miss.empty(), "row coverage: every applicable row really acted: " + rs + (miss.empty() ? "" : " - MISSING row " + std::to_string(miss[0])));
  check(sum.refused > 0 && sum.soundingOverflow > 0 && sum.keysDropped > 0, "overflow / refusal / drop paths acted (refused " + std::to_string(sum.refused) + ", T11 evictions " + std::to_string(sum.soundingOverflow) + ", keys dropped " + std::to_string(sum.keysDropped) + ")");

  { // the same sessions from a host WITHOUT note ids: every press of a key carries the same (port, channel, key) ref
    std::vector<std::string> badKey;
    for (const auto& kv : kSessions)
      for (int block : blocksFor(kv.first)) {
        Session q = sessionNamed(kv.first);
        q.keyRefs = true;
        const Run r = run(q, block);
        for (const std::string& b : n1(r)) badKey.push_back(std::string(kv.first) + "@" + std::to_string(block) + ": " + b);
        if (r.core->counters().offsLost != 0) badKey.push_back(std::string(kv.first) + ": a NoteOff found no room");
      }
    check(badKey.empty(), "N1 holds over the same sessions with id-less (port, channel, key) refs" + (badKey.empty() ? "" : " - " + badKey[0]));
  }
  { // pass-through: non-note events come out unchanged at their offsets, also while disabled; Panic is forwarded AFTER the offs
    const Run r = run(sessionNamed("enable"), 64);
    std::string pass;
    for (const Rec& e : r.evs) if (e.t == EventType::Other) pass += std::to_string(e.a) + ":" + std::to_string(e.data0) + ",";
    check(pass == "5000:100,10000:200,", "pass-through: events arrive unchanged at their offsets, also while disabled (" + pass + ")");
    const Run p = run(sessionNamed("panic"), 64);
    int lastOff = -1, panicAt = -1, i = 0;
    for (const Rec& e : p.evs) { if (e.a == 15000 && e.t == EventType::NoteOff) lastOff = i; if (e.a == 15000 && e.t == EventType::Panic) panicAt = i; ++i; }
    check(lastOff >= 0 && panicAt > lastOff, "panic: all releases first, then Panic is forwarded (SPEC 1.1, T7)");
    bool anyOnWhileDisabled = false;
    for (const Rec& e : r.evs) if (e.t == EventType::NoteOn && ((e.a >= 8100 && e.a < 12000) || (e.a >= 20100 && e.a < 24000))) anyOnWhileDisabled = true;
    check(!anyOnWhileDisabled, "T4: no note starts while the module is disabled (and input is never echoed)");
  }
  { // T16 (SPEC 13.11): keys 200 and -3 stay in E, so `up` over [-3, 60, 200] plays 60 once per THREE steps
    const Run r = run(sessionNamed("short"), 64);
    const std::vector<Rec> on = onsOf(r);
    bool ok = on.size() > 10;
    for (std::size_t i = 0; i < on.size(); ++i) ok = ok && on[i].key == 60 && (i == 0 || on[i].tick - on[i - 1].tick == 3 * 960);
    check(ok, "known answer: out-of-range keys are dropped at output but still count toward the period (60 every 3 steps, " + std::to_string(on.size()) + " notes)");
  }
}

// ------------------------------------------------------------------ G1 / G2 / G4 / G5
Session g1Session(const Plant& plants, bool keyRefs = false) {
  Session s; s.total = 52000; s.plants = plants; s.keyRefs = keyRefs;
  auto h = std::make_shared<SimHost>(8000, 120, 0, true);
  h->set(9500, std::nullopt, std::nullopt, 133.7); h->set(15003, std::nullopt, std::nullopt, 90.5); h->set(22001, false); h->set(26007, true, 3.0); h->set(33000, std::nullopt, 0.5);
  s.host = h;
  s.base.gate = 130; s.base.rate = Rate::R1_32;
  s.cfg = {C(18007, [](Settings& x) { x.rate = Rate::R1_16; x.latch = Latch::Replace; }), C(30011, [](Settings& x) { x.rate = Rate::R1_32; }), C(37000, [](Settings& x) { x.velMode = VelMode::Fixed; x.velFixed = 70; }),
           C(44001, [](Settings& x) { x.rate = Rate::R1_8; }), C(47000, [](Settings& x) { x.latch = Latch::Off; }), C(49001, [](Settings& x) { x.enabled = false; }), C(50001, [](Settings& x) { x.enabled = true; })};
  s.ins = {K(37, "a", 60), K(41, "b", 64), K(3011, "c", 67), O(12003, "a"), Ped(13000, true), O(14001, "b"), K(17777, "d", 72), O(19000, "c"), Other(20000, 5), Ped(24000, false),
           K(25100, "e", 55), K(36000, "f", 59), Pan(38000), K(38500, "g", 62), K(41000, "h", 66), O(45000, "h"), K(47000, "i", 69)};
  return s;
}
std::string g1Diverges(const Plant& plants, bool keyRefs = false) {
  std::string ref;
  for (int b : {4096, 512, 64, 7, 1}) {
    const std::string s = streamOf(run(g1Session(plants, keyRefs), b));
    if (ref.empty()) ref = s; else if (s != ref) return "block " + std::to_string(b) + " differs";
  }
  return "";
}

void testG1() {
  std::puts("G1: absolute samples identical at any block size");
  const std::string c = g1Diverges({"clampOffset"}), f = g1Diverges({"floatPosition"});
  plant(!c.empty(), "G1 detects offsets clamped into the block: " + c);
  plant(!f.empty(), "G1 detects a position accumulated in float32: " + f);
  const std::string r = g1Diverges({});
  const Run base = run(g1Session({}), 64);
  check(r.empty() && base.evs.size() > 100, "G1: " + std::to_string(base.evs.size()) + " events, absolute samples identical at block sizes 4096/512/64/7/1 (tempo changes, stop/start, seeks, rate changes, latch, pedal, panic, enable)" + (r.empty() ? "" : " - " + r));
  check(n1(base).empty(), "N1 holds on the G1 session");
  const std::string rk = g1Diverges({}, true);
  check(rk.empty() && n1(run(g1Session({}, true), 64)).empty(), "G1 and N1 also hold with id-less (port, channel, key) refs" + rk);
}

// G2: ons land at cdiv of their exact position, and so do offs (floor-then-gate length, SPEC 5.2)
std::string g2Bad(const Plant& plants, int gate) {
  Session s; s.total = 60000; s.plants = plants; s.bpm = 133.7; s.base.gate = gate;
  s.ins = {K(0, "a", 60), K(0, "b", 64)};
  const Run r = run(s, 512);
  const std::int64_t inc = 133700ll * kTpq;
  std::int64_t checkedOffs = 0;
  std::vector<Rec> on;
  for (const Rec& e : r.evs) if (e.t == EventType::NoteOn) on.push_back(e);
  for (std::size_t k = 0; k < on.size() && k < 24; ++k) {
    const std::int64_t expect = ((static_cast<std::int64_t>(k) * 3840) * kD8k + inc - 1) / inc;   // independent: plain int64, non-negative
    if (on[k].a != expect) return "on " + std::to_string(k) + " at " + std::to_string(on[k].a) + ", expected " + std::to_string(expect);
  }
  if (on.size() < 24) return "too few ons";
  const std::int64_t len = std::max<std::int64_t>(1, (3840ll * gate * 100) / 10000);   // gate 100 % x the setting; positive so `/` is the floor
  for (const Rec& e : r.evs) {
    if (e.t != EventType::NoteOff || e.a >= s.total) continue;   // the final reset's offs are forced, not scheduled
    for (const Rec& o : on) if (o.ref == e.ref) {
      const std::int64_t exp = std::max<std::int64_t>(((o.tick + len) * kD8k + inc - 1) / inc, o.a + 1);
      if (e.a != exp) return "off of tick " + std::to_string(o.tick) + " at " + std::to_string(e.a) + ", expected " + std::to_string(exp);
      ++checkedOffs;
    }
  }
  return checkedOffs < 20 ? "too few offs checked" : "";
}
void testG2() {
  std::puts("G2: ons and offs at the ceiling of their exact position");
  plant(!g2Bad({"roundNotCeil"}, 80).empty(), "G2 detects round() in place of ceil in the offset: " + g2Bad({"roundNotCeil"}, 80));
  const std::string a = g2Bad({}, 80), b = g2Bad({}, 200), c = g2Bad({}, 150);
  check(a.empty() && b.empty() && c.empty(), "G2: the first 24 ons at 133.7 bpm sit at cdiv(tick * D, inc) exactly, and every off at cdiv(on + len) (gate 80 / 150 / 200 %)" + a + b + c);
}


// G2b: against the HOST's own exact position function, across tempo changes (offs are scheduled in ticks, so they follow the music)
std::string g2bBad(const Plant& plants) {
  Session s; s.total = 30000; s.plants = plants; s.base.gate = 80; s.base.rate = Rate::R1_32T;
  auto h = std::make_shared<SimHost>(8000, 120, 0, true);
  h->set(9500, std::nullopt, std::nullopt, 133.7); h->set(15003, std::nullopt, std::nullopt, 90.5); h->set(21001, std::nullopt, std::nullopt, 175.25);
  s.host = h;
  s.ins = {K(0, "a", 60), K(0, "b", 64)};
  const Run r = run(s, 512);
  auto firstSampleAtOrAfter = [&](std::int64_t tick) {   // the smallest sample whose exact host position has reached `tick`
    const I128 target = I128::mul(tick, h->D);
    int lo = 0, hi = s.total + 1;
    while (lo < hi) { const int mid = (lo + hi) / 2; if (h->posAt(mid) >= target) hi = mid; else lo = mid + 1; }
    return static_cast<std::int64_t>(lo);
  };
  int ons = 0, offs = 0;
  std::vector<Rec> on;
  for (const Rec& e : r.evs) if (e.t == EventType::NoteOn) on.push_back(e);
  for (const Rec& e : r.evs) {
    if (e.t == EventType::NoteOn) {
      ++ons;
      if (e.a != firstSampleAtOrAfter(e.tick)) return "on at tick " + std::to_string(e.tick) + " sits at " + std::to_string(e.a) + ", the host reaches it at " + std::to_string(firstSampleAtOrAfter(e.tick));
    } else if (e.t == EventType::NoteOff && e.a < s.total) {
      for (const Rec& o : on) if (o.ref == e.ref) {
        ++offs;
        const std::int64_t want = std::max(firstSampleAtOrAfter(e.tick), o.a + 1);
        if (e.a != want) return "off of tick " + std::to_string(e.tick) + " at " + std::to_string(e.a) + ", expected " + std::to_string(want);
      }
    }
  }
  return ons < 60 || offs < 50 ? "too few events" : "";
}
void testG2b() {
  std::puts("G2b: events sit where the host's exact position reaches their tick, across tempo changes");
  plant(!g2bBad({"roundNotCeil"}).empty(), "G2b detects round() in place of ceil: " + g2bBad({"roundNotCeil"}));
  const std::string b = g2bBad({});
  check(b.empty(), "G2b: every on and natural off lands on the first sample whose exact host position has reached it (tempo 120 -> 133.7 -> 90.5 -> 175.25, 1/32T)" + b);
}

std::string g4Diverges(const Plant& plants) {
  std::string ref;
  for (int sr : {48000, 44100, 96000, 192000}) {
    Session s; s.sr = sr; s.total = sr * 3; s.plants = plants; s.base.gate = 80; s.base.rate = Rate::R1_32;
    s.ins = {K(0, "a", 60), K(0, "b", 64), K(0, "c", 67), O(sr * 2, "b")};
    const Run r = run(s, 1024);
    if (r.evs.size() < 60) return "too few events at " + std::to_string(sr);
    const std::string t = tickStreamOf(r);
    if (ref.empty()) ref = t; else if (t != ref) return "ticks differ at " + std::to_string(sr) + " Hz";
  }
  return "";
}
// The M1b sessions are written in samples at 8 kHz; replaying one at another rate scales every position (rounded to the
// nearest sample). Inputs and settings land on or between grid steps either way, and an input that sits exactly ON a step tick
// still precedes that step at the rounded sample (the step is at cdiv, the input at the nearest), so the musical content is
// the same and only the sample rate differs.
Session atRate(Session s, int sr) {
  auto sc = [sr](int at) { return static_cast<int>((static_cast<long long>(at) * sr + 4000) / 8000); };
  s.sr = sr;
  s.total = sc(s.total);
  for (auto& i : s.ins) i.at = sc(i.at);
  for (auto& c : s.cfg) c.at = sc(c.at);
  for (auto& u : s.userEdits) u.first = sc(u.first);
  for (auto& r : s.resets) r = sc(r);
  for (auto& a : s.scaleAt) a.first = sc(a.first);
  for (auto& p : s.prepares) p = sc(p);
  if (s.host) s.host = s.host->rescaled(sr, sc);
  return s;
}
// `chordEvents` / `chordTicks` (optional) receive the number of chord-stream events and of chord NoteOn ticks the exact check covered, summed over the rates.
// `chordOnly` skips the tick comparison, so a plant is read by the chord check ALONE (a late chord on can also reorder the tick stream by accident).
std::string g4M1bDiverges(const Plant& plants, const char* name, std::size_t minEvents = 100, int* chordEvents = nullptr, int* chordTicks = nullptr, bool chordOnly = false) {
  std::string ref, chordRef;
  for (int sr : {48000, 44100, 96000, 192000}) {
    Session s = atRate(sessionNamed(name), sr);
    s.plants = plants;
    const Run r = run(s, 1024);
    if (r.evs.size() < minEvents) return "too few events at " + std::to_string(sr);
    const std::string t = tickStreamOf(r);
    if (!chordOnly) { if (ref.empty()) ref = t; else if (t != ref) return "ticks differ at " + std::to_string(sr) + " Hz"; }
    // the chord stage's ons leave the tick comparison above (their tick is the position at an input's sample); this is their exact check (L3)
    const ChordCauses cc = chordCauses(s, r);
    if (!cc.err.empty()) return cc.err + " at " + std::to_string(sr) + " Hz";
    if (chordRef.empty()) chordRef = cc.sig; else if (cc.sig != chordRef) return "chord events follow other causes at " + std::to_string(sr) + " Hz";
    if (chordEvents) *chordEvents += cc.events;
    if (chordTicks) *chordTicks += cc.tickChecked;
  }
  return "";
}

void testG4() {
  std::puts("G4: event ticks identical at 44.1 / 48 / 96 / 192 kHz");
  for (const char* name : {"styles", "patterns", "scalefeel", "restartrepeats", "chordstage", "morph"}) {
    const std::size_t minEv = std::strcmp(name, "restartrepeats") == 0 ? 60 : 100;   // its repeats spend the arp on purpose
    const std::string bad = g4M1bDiverges({"srTickMath"}, name, minEv);
    plant(!bad.empty(), std::string("G4 detects a sample-rate-dependent value in tick math on the session '") + name + "': " + bad);
    const std::string d = g4M1bDiverges({}, name, minEv);
    check(d.empty(), std::string("G4: the session '") + name + "' (sync mode: M1b order styles and pattern fields, M2a quantize / degrees / swing) has identical event ticks at 44.1, 48, 96 and 192 kHz" + d);
  }
  {   // L3: the chord stage's ons are timed by their inputs, not the grid; their ticks are left out of the tick comparison, so they get an exact check
    const std::string late = g4M1bDiverges({"chordOnLate"}, "chordstage", 60, nullptr, nullptr, true);
    plant(!late.empty(), "G4 detects a chord-stream NoteOn that lands a sample after the input that caused it at 96 kHz: " + late);
    const std::string skew = g4M1bDiverges({"chordOnTickSkew"}, "chordstage", 60, nullptr, nullptr, true);
    plant(!skew.empty(), "G4 detects a chord-stream NoteOn whose tick is not the host's exact position at its sample: " + skew);
    int events = 0, ticks = 0;
    const std::string c1 = g4M1bDiverges({}, "chordstage", 100, &events, &ticks), c2 = g4M1bDiverges({}, "morph", 100, &events, &ticks);
    check(c1.empty() && c2.empty() && events >= 400 && ticks >= 100,
          "G4 (L3): at 44.1 / 48 / 96 / 192 kHz every chord-stream NoteOn and NoteOff of the 'chordstage' and 'morph' sessions lands at exactly the sample of the input, settings change or reset that caused it, the same cause at every rate, and while the transport plays each on's tick is the host's exact position there (" + std::to_string(events) + " chord events, " + std::to_string(ticks) + " on ticks checked)" + c1 + c2);
  }
  plant(!g4Diverges({"srTickMath"}).empty(), "G4 detects a sample-rate-dependent value read into tick math: " + g4Diverges({"srTickMath"}));
  const std::string d = g4Diverges({});
  check(d.empty(), "G4: the tick of every event is identical at 44.1, 48, 96 and 192 kHz (three keys, a key released mid-run)" + d);
}

// G5: host-position jitter of +-1/32 beat -> every grid step is evaluated exactly once
Session g5Session(const Plant& plants) {
  Session s; s.total = 40000; s.plants = plants; s.base.gate = 80;
  s.ins = {K(0, "a", 60)};
  s.jitter = [x = std::uint32_t{12345}](int pos) mutable { x = (x ^ static_cast<std::uint32_t>(pos)) * 1664525u + 1013904223u; return static_cast<int>(x % 961) - 480; };   // +-480 ticks = 1/32 beat
  return s;
}
bool g5Once(const Run& r) {
  if (r.steps.size() < 30) return false;
  for (std::size_t i = 1; i < r.steps.size(); ++i) if (r.steps[i].s.kAbs != r.steps[i - 1].s.kAbs + 1) return false;
  return true;
}
void testG5() {
  std::puts("G5: every grid step evaluated exactly once under re-base jitter");
  plant(!g5Once(run(g5Session({"noOnceGuard"}), 64)), "G5 detects a dropped once-guard (a step evaluated twice or skipped under jitter)");
  const Run r = run(g5Session({}), 64);
  check(g5Once(r) && n1(r).empty() && r.core->counters().rows[6] == 0, "G5: " + std::to_string(r.steps.size()) + " grid steps evaluated exactly once, consecutively, under jittered re-base; N1 holds; no T6 fired (re-base is silent)");
  // known answers for the band edge (no plant: exact boundaries, not an invariant checker): |delta| = 960 ticks re-bases, 961 jumps (T6)
  auto edgeRun = [](int ticks) {
    Session s; s.total = 20000; s.base.gate = 200;
    s.ins = {K(0, "a", 60)};
    s.jitter = [ticks](int pos) { return pos == 10240 ? ticks : 0; };   // one jittered block, mid-run, with a note sounding
    return run(s, 64);
  };
  const Run in = edgeRun(960), out = edgeRun(961), fwd = edgeRun(-961);
  check(in.core->counters().rows[6] == 0 && out.core->counters().rows[6] > 0 && fwd.core->counters().rows[6] > 0,
        "known answer: a position error of exactly 960 ticks (1/16 beat) re-bases silently; 961 ticks, either way, is a jump (T6)");
}

// ------------------------------------------------------------------ D1: the draw
void testD1() {
  std::puts("D1: the draw is a pure function of (seed, x, lane, epoch)");
  struct Kat { std::uint32_t seed; std::int64_t x; std::uint32_t lane, epoch, want; };
  // computed independently by the lab's JS draw and by a separate Python implementation of the SPEC 10 formula
  const Kat kats[] = {{0, 0, 0, 0, 1144304738u}, {1, 2, 0, 0, 2224532361u}, {42, 100, 2, 0, 846768311u}, {42, 100, 2, 1, 2410867527u},
                      {0xFFFFFFFFu, -1, 1, 7, 679003129u}, {12345, 987654321, 0, 0xFFFFFFFFu, 4061104088u}, {7, -5, 2, 3, 3587580109u}, {0, 4294967301ll, 0, 0, 940613922u}};
  bool kat = true;
  for (const Kat& k : kats) kat = kat && arp::draw(k.seed, k.x, k.lane, k.epoch) == k.want;
  auto stream = [](const Plant& pl, std::uint32_t seed) {
    auto c = makeCore({}, pl);
    std::string s;
    for (int x = -8; x < 56; ++x) s += std::to_string(c->draw(seed, x, 2)) + ",";
    return s;
  };
  plant(stream({"timeInDraw"}, 42) != stream({"timeInDraw"}, 42), "D1 detects a time-varying value in the draw (same seed, different stream)");
  check(kat, "D1 known answers: 8 draws equal the lab's JS draw and an independent Python implementation (negative x, x > 2^32, extreme epoch)");
  const std::string a = stream({}, 42);
  check(a == stream({}, 42), "D1: same (seed, x, lane, epoch) -> the identical 64-draw stream from two fresh cores");
  auto c = makeCore({}, {});
  bool vary = true;
  for (int x = 0; x < 64; ++x) vary = vary && c->draw(42, x, 2) != c->draw(43, x, 2) && c->draw(42, x, 2) != c->draw(42, x, 0) && c->draw(42, x, 2) != c->draw(42, x + 1, 2);
  check(vary && a != stream({}, 43), "D1: a different seed, lane or x changes every draw");
  // the epoch moves at every free-clock start and is mixed into the draw (SPEC 2.4, H7c)
  Session s; s.total = 20000; s.host = std::make_shared<SimHost>(8000, 120, 0, false); s.ins = {K(100, "a", 60), O(2000, "a"), K(9000, "b", 62), O(9500, "b"), K(15000, "c", 64)};
  std::vector<std::uint32_t> epochs, draws;
  s.probe = [&](int, int, const Core& core) { if (epochs.empty() || epochs.back() != core.epoch()) { epochs.push_back(core.epoch()); draws.push_back(core.draw(42, 7, 2)); } };
  run(s, 64);
  bool distinct = epochs.size() >= 4;
  for (std::size_t i = 1; i < draws.size(); ++i) distinct = distinct && draws[i] != draws[i - 1];
  check(distinct, "D1: each free-clock gesture start increments the epoch (" + std::to_string(epochs.size()) + " values seen) and the draw at a fixed (seed, x, lane) changes with it");
  check(streamOf(run(g1Session({}), 64)) == streamOf(run(g1Session({}), 64)), "D1: the same input, transport, settings and seed give an identical output stream");
}

// ------------------------------------------------------------------ A1: no allocation in process / reset / a settings change
void testA1() {
  std::puts("A1: no allocation in process, reset or a settings change");
  auto allocs = [](const Plant& pl) {
    Session s = sessionNamed("held", pl);
    s.resets = {30500};   // reset() with a note sounding
    s.countAllocs = true;
    return run(s, 64).allocs;
  };
  const std::int64_t ap = allocs({"allocInProcess"}), ar = allocs({"allocInReset"}), as = allocs({"allocInSettings"});
  plant(ap > 0, "A1 detects an allocation in process(): " + std::to_string(ap) + " calls");
  plant(ar > 0, "A1 detects an allocation in reset(): " + std::to_string(ar) + " calls");
  plant(as > 0, "A1 detects an allocation when a discrete setting changes: " + std::to_string(as) + " calls");
  const std::int64_t n = allocs({});
  check(n == 0, "A1: " + std::to_string(n) + " operator-new calls inside process() / reset() over the whole held session (settings changes, latch, pedal, resets)");
}

// ------------------------------------------------------------------ C1: the event cap and its off-reserve
void testC1() {
  std::puts("C1: at the event cap no NoteOn is emitted without room for its off");
  const auto lost = n1(run(sessionNamed("cap", {"noOffReserve"}), 4096));
  plant(!lost.empty(), "C1 detects an on admitted into the off-reserve (its off is lost): " + (lost.empty() ? std::string() : lost[0]));
  const Run r = run(sessionNamed("cap"), 4096);
  check(n1(r).empty() && r.core->counters().refused > 0 && r.core->counters().offsLost == 0 && !onsOf(r).empty(),
        "C1: with a 4-slot buffer " + std::to_string(r.core->counters().refused) + " ons were refused and counted (T13), " + std::to_string(onsOf(r).size()) + " emitted, every one with its off");
  { // the reserve at every small capacity: no cap loses an off, whatever the overlap
    bool clean = true;
    int refusedSomewhere = 0;
    for (int cap = 3; cap <= 14; ++cap) {
      Session c = sessionNamed("cap");
      c.caps.events = cap;
      const Run q = run(c, 4096);
      clean = clean && n1(q).empty() && q.core->counters().offsLost == 0;
      refusedSomewhere += q.core->counters().refused > 0;
    }
    check(clean && refusedSomewhere > 3, "C1: for every event capacity 3..14 no on is emitted without room for its off (" + std::to_string(refusedSomewhere) + " of 12 capacities refused ons)");
  }
  { // non-note events share the buffer: a flood of pass-through events is dropped and counted, never overruns, and never costs a note its off
    Session s; s.total = 6000; s.maxBlock = 256; s.base.gate = 150; s.ins = {K(100, "a", 60), K(100, "b", 64)};
    for (int i = 0; i < 1500; ++i) s.ins.push_back(Other(2560 + i % 256, i));
    const Run f = run(s, 256);
    check(n1(f).empty() && f.core->counters().eventOverflow > 0 && f.core->counters().offsLost == 0 && f.core->eventCapacity() == 1024,
          "C1: a 1500-event pass-through flood into a 1024-slot block is cut at the cap (" + std::to_string(f.core->counters().eventOverflow) + " dropped, counted); N1 holds");
  }
}

// ------------------------------------------------------------------ the held set (SPEC 3) and the free clock (SPEC 2.4)
std::vector<Rec> onsFrom(const Run& r, std::int64_t from) {
  std::vector<Rec> v;
  for (const Rec& e : r.evs) if (e.t == EventType::NoteOn && e.a >= from) v.push_back(e);
  return v;
}
bool allKeys(const std::vector<Rec>& v, int key) {
  for (const Rec& e : v) if (e.key != key) return false;
  return !v.empty();
}

void testHeldSet() {
  std::puts("Held set (SPEC 3)");
  { // latch append: a press on a key held only by LATCHED refs toggles it out; the toggling ref's own NoteOff is ignored
    auto sess = [](const Plant& pl) {
      Session s; s.total = 12000; s.plants = pl; s.base.latch = Latch::Append;
      s.ins = {K(0, "A", 60), K(0, "B", 64), O(2000, "A"), O(2000, "B"), K(5000, "C", 64), O(5500, "C")};
      return run(s, 64);
    };
    auto bad = [&](const Plant& pl) { const Run r = sess(pl); const auto before = onsFrom(r, 0); bool has64 = false; for (const Rec& e : before) if (e.a < 5000 && e.key == 64) has64 = true; return !(has64 && allKeys(onsFrom(r, 5000), 60) && onsFrom(r, 5000).size() >= 5); };
    plant(bad({"noLatchToggle"}), "held set detects latch append that adds instead of toggling a latched key out");
    check(!bad({}), "latch append: pressing a latched key toggles it out (64 plays until the toggle, then only 60); its NoteOff is ignored");
  }
  { // the pedal holds Arp's INPUT: released keys keep playing until the pedal comes up
    auto sess = [](const Plant& pl) {
      Session s; s.total = 12000; s.plants = pl;
      s.ins = {Ped(0, true), K(100, "A", 60), O(1500, "A"), Ped(6000, false)};
      return run(s, 64);
    };
    auto bad = [&](const Plant& pl) { const Run r = sess(pl); int held = 0; for (const Rec& e : onsFrom(r, 2000)) if (e.a < 6000) ++held; return !(held >= 3 && onsFrom(r, 6000).empty()); };
    plant(bad({"pedalIgnored"}), "held set detects a NoteOff that releases the key although the pedal is down");
    check(!bad({}), "pedal: a key released under the pedal keeps playing until the pedal comes up, then stops");
  }
  { // latch replace: a press with nothing DOWN is a gesture start, so it drops every LATCHED ref and re-anchors on the step at its sample
    auto sess = [](const Plant& pl) {
      Session s; s.total = 12000; s.plants = pl; s.base.latch = Latch::Replace;
      s.ins = {K(0, "A", 60), K(0, "B", 64), O(2000, "A"), O(2000, "B"), K(5000, "C", 67)};
      return run(s, 64);
    };
    auto bad = [&](const Plant& pl) { const auto v = onsFrom(sess(pl), 5000); return !(allKeys(v, 67) && v.size() >= 5 && v[0].a == 5000); };
    plant(bad({"latchReplaceKeeps"}), "held set detects latch replace that keeps the latched refs");
    check(!bad({}), "latch replace: the re-press drops the latched keys; only 67 plays, starting on the step at the press");
  }
  { // SPEC 13b.12: `latch` applies at OFFSET 0 of its block, not at the next arp step
    auto heldAfterChange = [](const Plant& pl) {
      Session s; s.total = 4500; s.plants = pl; s.base.latch = Latch::Replace;
      s.cfg = {C(3500, [](Settings& x) { x.latch = Latch::Off; })};
      s.ins = {K(0, "A", 60), O(1000, "A")};
      std::map<int, std::size_t> seen;
      s.probe = [&seen](int pos, int, const Core& c) { seen[pos] = c.heldKeyCount(); };
      run(s, 100);   // blocks start at multiples of 100, so 3000 and 3500 are block starts
      return std::make_pair(seen[3000], seen[3500]);
    };
    auto bad = [&](const Plant& pl) { const auto h = heldAfterChange(pl); return !(h.first == 1 && h.second == 0); };
    plant(bad({"latchAtNextStep"}), "held set detects latch-off applied at the next arp step instead of offset 0");
    check(!bad({}), "latch off: the LATCHED refs are dropped at offset 0 of the block whose snapshot changes it");
  }
  { // known answers (caps from SPEC 1.3): no plant, exact counts
    Session s; s.total = 2000; s.ins = {};
    std::vector<std::string> labels;
    for (int i = 0; i < 33; ++i) labels.push_back("k" + std::to_string(i));
    for (int i = 0; i < 33; ++i) s.ins.push_back(K(10, labels[static_cast<std::size_t>(i)].c_str(), 40 + i));
    std::vector<std::string> same;   // key 41 already has one ref ("k1"); the 4th extra one is its 5th
    for (int i = 0; i < 4; ++i) same.push_back("s" + std::to_string(i));
    for (int i = 0; i < 4; ++i) s.ins.push_back(K(20, same[static_cast<std::size_t>(i)].c_str(), 41));
    s.ins.push_back(O(30, "k32"));   // the ignored 33rd key's NoteOff has no effect
    std::size_t heldAfter = 0;
    s.probe = [&heldAfter](int pos, int, const Core& c) { if (pos == 0) heldAfter = c.heldKeyCount(); };
    const Run r = run(s, 64);
    check(r.core->counters().heldOverflow == 1 && r.core->counters().refOverflow == 1 && heldAfter == 32, "known answer: the 33rd distinct key and the 5th ref on one key are refused and counted (SPEC 1.3)");
  }
  { // velocity (SPEC 4.4): known answers
    auto vels = [](bool fixed, int inVel) {
      Session s; s.total = 6000; s.base.velMode = fixed ? VelMode::Fixed : VelMode::Played; s.base.velFixed = 77; s.ins = {K(0, "a", 60, inVel)};
      std::set<int> v;
      for (const Rec& e : onsOf(run(s, 64))) v.insert(e.vel);
      return v;
    };
    check(vels(false, 100) == std::set<int>{100} && vels(false, 0) == std::set<int>{1} && vels(true, 100) == std::set<int>{77}, "known answer: velocity played (0 clamps to 1) and fixed");
  }
}

// Known answers for the musical behaviour that no invariant checker sees: no plant, exact expected values.
void testUpModel() {
  std::puts("Order `up` over the held set (a model check) and the T2 order");
  struct Key { int key, vel, ch, on, off; };
  // the second gesture has TWO keys, so a stale m (9 plays so far, odd) would pick the wrong one of them
  const Key keys[] = {{67, 90, 2, 0, 8500}, {60, 80, 0, 0, 3500}, {64, 70, 0, 0, 8500}, {72, 60, 0, 5500, 8500}, {62, 50, 0, 12300, 1 << 30}, {70, 40, 0, 12300, 1 << 30}};
  Session s; s.total = 20000;
  s.ins = {K(0, "a", 67, 90, 2), K(0, "b", 60, 80), K(0, "c", 64, 70), O(3500, "b"), K(5500, "d", 72, 60), O(8500, "a", 2), O(8500, "c"), O(8500, "d"), K(12300, "e", 62, 50), K(12300, "f", 70, 40)};
  const Run modelRun = run(s, 64);
  const std::vector<Rec> on = onsOf(modelRun);
  { // the anchor: k = kAbs - anchorK is 0 exactly at the first step of each gesture (kAbs 0 and 13), where m restarts
    std::vector<std::int64_t> zeros;
    bool mReset = true;
    for (const StepRec& r : modelRun.steps) if (r.s.k == 0) { zeros.push_back(r.s.kAbs); mReset = mReset && r.s.mBefore == 0; }
    check(zeros == std::vector<std::int64_t>({0, 13}) && mReset, "known answer: the step records show k = 0 and m = 0 at the first step of each gesture (kAbs 0 and 13)");
  }
  // the model: steps every 1000 samples; m := 0 at the first step at/after each gesture start (0 and 12300 -> steps 0 and 13)
  std::vector<std::array<int, 5>> want;   // sample, key, vel, ch
  int m = 0;
  for (int k = 0; k < 20; ++k) {
    const int at = 1000 * k;
    if (k == 0 || k == 13) m = 0;
    std::vector<Key> held;
    for (const Key& q : keys) if (q.on <= at && at < q.off) held.push_back(q);
    std::sort(held.begin(), held.end(), [](const Key& a, const Key& b) { return a.key < b.key; });
    if (held.empty()) continue;
    const Key& h = held[static_cast<std::size_t>(m % static_cast<int>(held.size()))];
    want.push_back({at, h.key, h.vel, h.ch, 0});
    ++m;
  }
  bool modelOk = on.size() == want.size();
  for (std::size_t i = 0; modelOk && i < on.size(); ++i)
    modelOk = on[i].a == want[i][0] && on[i].key == want[i][1] && on[i].vel == want[i][2] && on[i].ref.channel == want[i][3] && on[i].ref.port == 0 && on[i].ref.key == on[i].key && on[i].ref.id == static_cast<int>(i);
  check(modelOk, "known answer: `up` walks the held keys in pitch order, m resets at each gesture start and not when a key joins, a rest (no keys) leaves m alone; channel and velocity come from the source key; output refs are (0, ch, key, counter) (" + std::to_string(on.size()) + " notes)");
  { // T2: the old note's NoteOff and the new note's NoteOn go out at one sample, in that order
    Session t; t.total = 8000; t.base.gate = 150; t.ins = {K(0, "a", 60)};
    const Run r = run(t, 64);
    int pairs = 0;
    bool order = true;
    for (std::size_t i = 1; i < r.evs.size(); ++i)
      if (r.evs[i].t == EventType::NoteOn && r.evs[i].a > 0) {
        const bool paired = r.evs[i - 1].t == EventType::NoteOff && r.evs[i - 1].a == r.evs[i].a && r.evs[i - 1].ref.id + 1 == r.evs[i].ref.id;
        order = order && paired;
        ++pairs;
      }
    check(order && pairs >= 6, "known answer: T2 puts the old note's NoteOff immediately before the new NoteOn at one sample (" + std::to_string(pairs) + " retriggers)");
  }
  { // T14: a free-clock re-base keeps a sounding note's REMAINING duration exact (its off shifts with the origin)
    Session t; t.total = 6000; t.base.gate = 150; t.host = std::make_shared<SimHost>(8000, 120, 0, false);
    t.ins = {K(100, "a", 60), O(200, "a"), K(700, "b", 64), O(750, "b")};
    const Run r = run(t, 64);
    std::int64_t offA = -1, onB = -1;
    for (const Rec& e : r.evs) {
      if (e.t == EventType::NoteOff && e.ref.key == 60 && offA < 0) offA = e.a;
      if (e.t == EventType::NoteOn && e.key == 64 && onB < 0) onB = e.a;
    }
    check(offA == 1600 && onB == 700 && r.core->counters().rows[14] > 0, "known answer: a gesture start re-bases the free clock (the new step 0 lands on the press, 700) while the old note's off stays at 100 + 1500 = " + std::to_string(offA));
  }
  { // SPEC 1.3: the steps-per-block bound, asserted in C++ (13.16), at its tightest: 999 bpm, 1/64, the largest block
    Session t; t.total = 20000; t.bpm = 999; t.base.rate = Rate::R1_64; t.ins = {K(0, "a", 60)};
    std::size_t most = 0;
    std::int64_t dropped = 0;
    t.probe = [&](int, int, const Core& c) { most = std::max(most, c.lastSteps().size()); dropped = c.counters().stepLogDropped; };
    run(t, 4096);
    const std::int64_t bound = arp::cdiv(4096ll * 999 * kTpq, 60ll * 8000 * 960) + 1;
    check(most <= static_cast<std::size_t>(bound) && most + 8 >= static_cast<std::size_t>(bound) && dropped == 0, "known answer: at most " + std::to_string(bound) + " steps per 4096-sample block at 999 bpm 1/64 (saw " + std::to_string(most) + ")");
  }

  { // latch append: a press that JOINS a non-empty latched set is not a gesture start, so the order carries on (m is not reset)
    Session t; t.total = 12000; t.base.latch = Latch::Append; t.ins = {K(0, "A", 60), O(1000, "A"), K(6500, "B", 64)};
    const std::vector<Rec> after = onsFrom(run(t, 64), 7000);
    check(!after.empty() && after[0].a == 7000 && after[0].key == 64, "known answer: latch append, a key joining the latched set continues the order (m = 7 over two keys -> 64), it does not restart it");
  }
  { // SPEC 3: a key held by two refs carries the channel and velocity of its EARLIEST live ref
    Session t; t.total = 6000;
    t.ins = {K(0, "a", 60, 90, 1), K(100, "b", 60, 50, 2), O(2500, "a", 1)};
    bool ok = true;
    for (const Rec& e : onsOf(run(t, 64))) ok = ok && (e.a < 3000 ? (e.vel == 90 && e.ref.channel == 1) : (e.vel == 50 && e.ref.channel == 2));
    check(ok, "known answer: two refs on one key -> the earliest ref's velocity and channel, until it is released");
  }
  { // SPEC 1.1: offs at one sample go out by ascending output id, then Panic
    Session t; t.total = 4000; t.base.gate = 200; t.ins = {K(0, "a", 60), K(0, "b", 64), Pan(1500)};
    const Run r = run(t, 64);
    std::vector<std::int64_t> ids;
    int panicAt = -1, i = 0;
    for (const Rec& e : r.evs) { if (e.a == 1500 && e.t == EventType::NoteOff) ids.push_back(e.ref.id); if (e.a == 1500 && e.t == EventType::Panic) panicAt = i; ++i; }
    check(ids == std::vector<std::int64_t>({0, 1}) && panicAt > 0, "known answer: two notes released by a Panic at one sample go out in ascending id order, before the forwarded Panic");
  }
  { // T11: the sounding table never holds more than its capacity: the oldest note is released AT the new on's sample, before it
    Session t = sessionNamed("overflow");
    const Run r = run(t, 64);
    int active = 0, most = 0;
    for (const Rec& e : r.evs) { active += e.t == EventType::NoteOn ? 1 : e.t == EventType::NoteOff ? -1 : 0; most = std::max(most, active); }
    check(most <= 1 && r.core->counters().soundingOverflow > 0, "known answer: with a 1-entry sounding table at most 1 note sounds at any event (T11 evicted " + std::to_string(r.core->counters().soundingOverflow) + ")");
  }
  { // T4 / T6 cut a SOUNDING note at the edge (gate 200: a note started at 11000 / 5000 would otherwise last to 13000 / 7000)
    auto offOfNoteStartedAt = [](const Run& r, std::int64_t at) { std::int64_t off = -1; NoteRef ref; bool have = false; for (const Rec& e : r.evs) { if (e.t == EventType::NoteOn && e.a == at) { ref = e.ref; have = true; } if (have && e.t == EventType::NoteOff && e.ref == ref) { off = e.a; break; } } return off; };
    Session stop; stop.total = 16000; stop.base.gate = 200; stop.ins = {K(0, "a", 60), K(0, "b", 64)};   // two keys: the note from 11000 is key 64, which the restart at 12000 (key 60) does not retrigger
    auto h = std::make_shared<SimHost>(8000, 120, 0, true); h->set(12000, false); stop.host = h;
    Session dis; dis.total = 9000; dis.base.gate = 200; dis.ins = {K(0, "a", 60)}; dis.cfg = {C(5500, [](Settings& x) { x.enabled = false; })};
    check(offOfNoteStartedAt(run(stop, 64), 11000) == 12000 && offOfNoteStartedAt(run(dis, 64), 5000) == 5500, "known answer: a transport stop (T6) and enabled -> off (T4) cut the sounding note exactly at their sample");
  }
  { // SPEC 1.3: a tempo <= 1 means 120, then [20, 999]; no tempo reported = the last valid one, else 120. (Free clock: its pace IS the tempo.)
    auto onsAt = [](bool hasTempo, double tempo, bool onlyFirstBlock) {
      Session t; t.total = 30000; t.host = std::make_shared<SimHost>(8000, 120, 0, false); t.ins = {K(0, "a", 60)};
      t.tweak = [=](int pos, Transport& tr) { tr.hasTempo = onlyFirstBlock ? pos == 0 : hasTempo; tr.tempo = tempo; };
      std::vector<std::int64_t> v;
      for (const Rec& e : onsOf(run(t, 64))) v.push_back(e.a);
      return v;
    };
    auto expect = [](double bpm, std::size_t n) {   // cdiv(k * 3840 * D, bpm * 1000 * TPQ)
      std::vector<std::int64_t> v;
      const std::int64_t inc = std::llround(bpm * 1000) * kTpq;
      for (std::size_t k = 0; k < n; ++k) v.push_back((static_cast<std::int64_t>(k) * 3840 * kD8k + inc - 1) / inc);
      return v;
    };
    auto same = [&](const std::vector<std::int64_t>& got, double bpm) { const auto e = expect(bpm, got.size()); return got.size() >= 4 && got == e; };
    check(same(onsAt(true, 0.5, false), 120) && same(onsAt(true, 10, false), 20) && same(onsAt(true, 5000, false), 999) && same(onsAt(false, 77, false), 120) && same(onsAt(true, 90, true), 90),
          "known answer: tempo 0.5 -> 120, 10 -> 20, 5000 -> 999 bpm; no tempo reported -> 120, or the last valid one (90)");
  }
}

// Known answers for WHEN things change: settings, enable, transport edges. No plants: exact expected values.
void testTiming() {
  std::puts("Timing of settings, enable and transport edges");
  using AtKey = std::vector<std::pair<std::int64_t, int>>;
  auto atKey = [](const Run& r) { AtKey v; for (const Rec& e : onsOf(r)) v.emplace_back(e.a, e.key); return v; };
  { // T8: a discrete setting applies from the next EVALUATED step, never retroactively
    Session s; s.total = 7000; s.base.velFixed = 40; s.cfg = {C(3500, [](Settings& x) { x.velMode = VelMode::Fixed; })}; s.ins = {K(0, "a", 60, 90)};
    bool ok = true;
    for (const Rec& e : onsOf(run(s, 64))) ok = ok && e.vel == (e.a < 4000 ? 90 : 40);
    check(ok, "known answer: vel_mode played -> fixed at sample 3500 changes the first note at the next step (4000), not before");
  }
  { // T15: a rate change applies at the next step of the OLD rate; the new grid takes over there and m := 0 (SPEC 9, 13.8)
    Session s; s.total = 12000; s.cfg = {C(3500, [](Settings& x) { x.rate = Rate::R1_8; })}; s.ins = {K(0, "a", 60), K(0, "b", 64), K(0, "c", 67)};
    const AtKey want = {{0, 60}, {1000, 64}, {2000, 67}, {3000, 60}, {4000, 60}, {6000, 64}, {8000, 67}, {10000, 60}};
    check(atKey(run(s, 64)) == want, "known answer: rate 1/16 -> 1/8 at 3500 lands on the next old-grid step (4000), restarts the order there and steps every 2000 after");
  }
  { // T15 where the grids do not align: 1/16 -> 3/16 at 3500; the new grid takes the first of ITS steps at or after the old step's tick (cdiv, never the floor)
    Session s; s.total = 14000; s.cfg = {C(3500, [](Settings& x) { x.rate = Rate::R3_16; })}; s.ins = {K(0, "a", 60), K(0, "b", 64), K(0, "c", 67)};
    const AtKey want = {{0, 60}, {1000, 64}, {2000, 67}, {3000, 60}, {6000, 60}, {9000, 64}, {12000, 67}};
    check(atKey(run(s, 64)) == want, "known answer: rate 1/16 -> 3/16 at 3500: the old step at 4000 hands over to the first 3/16 step at or after it (6000), never to one in the past");
  }
  { // T4 / T5: off at 5500, on at 9500 with keys held: silence between, then the grid restarts at the next step (m := 0)
    Session s; s.total = 14000; s.cfg = {C(5500, [](Settings& x) { x.enabled = false; }), C(9500, [](Settings& x) { x.enabled = true; })};
    s.ins = {K(0, "a", 60), K(0, "b", 64), K(0, "c", 67)};
    const AtKey want = {{0, 60}, {1000, 64}, {2000, 67}, {3000, 60}, {4000, 64}, {5000, 67}, {10000, 60}, {11000, 64}, {12000, 67}, {13000, 60}};
    const Run r = run(s, 64);
    check(atKey(r) == want && n1(r).empty(), "known answer: disabled at 5500 and re-enabled at 9500 with keys held: nothing between, then step 10000 plays the first key again");
  }
  { // T6 + the free clock (H7b) + re-anchor: stop, start, jump and seek all land on step boundaries here, so a note sounds every 1000 samples throughout
    Session s; s.total = 40000;
    auto h = std::make_shared<SimHost>(8000, 120, 0, true);
    h->set(12000, false); h->set(20000, true, 0.0); h->set(28000, std::nullopt, 7.0); h->set(34000, std::nullopt, 1.5);
    s.host = h;
    s.ins = {K(0, "a", 60), K(0, "b", 64)};
    AtKey want;
    const int anchors[] = {0, 12, 20, 28, 34};   // in steps: start, stop (free clock starts, keys held), start, jump, seek back
    for (int k = 0; k < 40; ++k) {
      int a = 0;
      for (int x : anchors) if (x <= k) a = x;
      want.emplace_back(1000 * k, (k - a) % 2 == 0 ? 60 : 64);
    }
    const Run r = run(s, 64);
    check(atKey(r) == want && n1(r).empty(), "known answer: transport stop (free clock starts at the stop sample), start, jump and seek each release, re-anchor and restart the order on the new grid position");
  }
}

void testClamps() {
  std::puts("Out-of-range continuous settings are clamped, not trusted");
  auto lenOf = [](int gate) {   // off tick - on tick of the first note
    Session s; s.total = 6000; s.base.gate = gate; s.ins = {K(0, "a", 60)};
    const Run r = run(s, 64);
    for (const Rec& e : r.evs) if (e.t == EventType::NoteOff) return e.tick - onsOf(r)[0].tick;
    return std::int64_t{-1};
  };
  auto velOf = [](int fixed) { Session s; s.total = 1500; s.base.velMode = VelMode::Fixed; s.base.velFixed = fixed; s.ins = {K(0, "a", 60)}; return onsOf(run(s, 64))[0].vel; };
  check(lenOf(0) == 38 && lenOf(1) == 38 && lenOf(500) == 7680 && lenOf(200) == 7680 && velOf(0) == 1 && velOf(999) == 127,
        "known answer: gate 0 -> 1 % (38 ticks), gate 500 -> 200 % (7680 ticks), vel_fixed 0 -> 1 and 999 -> 127");
}

void testFreeClock() {
  std::puts("Free clock (SPEC 2.4)");
  Session s = sessionNamed("free");
  s.ins = {K(137, "a", 60), O(9000, "a"), K(20033, "b", 64), O(21000, "b")};
  const Run r = run(s, 64);
  const std::vector<Rec> on = onsOf(r);
  // step 0 lands ON the press (H7a), then every 1000 samples; the second gesture re-bases again
  bool ok = on.size() > 8 && on[0].a == 137 && on[1].a == 1137 && on[2].a == 2137;
  bool second = false;
  for (const Rec& e : on) if (e.a == 20033) second = e.key == 64;
  check(ok && second && n1(r).empty(), "known answer: with the transport stopped the first note lands exactly on the key press and every 1000 samples after; a later press re-bases to its own sample");
}

// ------------------------------------------------------------------ hosts without note ids (intake/note-vocabulary.md 1)
// Identity is then (port, channel, key): every press of a key carries the SAME ref. The first suite gave every label a
// unique id, which is why it never saw a re-pressed key dropped.
struct KeyScenario { const char* what; std::function<bool(const Plant&)> good; };

void testKeyRefs() {
  std::puts("Hosts without note ids: a re-pressed key is a fresh press");
  auto heldEnd = [](Session& s, std::size_t& out) { s.probe = [&out](int, int, const Core& c) { out = c.heldKeyCount(); }; };
  std::vector<KeyScenario> sc;
  sc.push_back({"latch append: pressing a latched key toggles it out, pressing it again adds it back", [&](const Plant& pl) {
    Session s; s.total = 12000; s.plants = pl; s.base.latch = Latch::Append; s.base.gate = 50;
    s.ins = {Kk(0, 60), Kk(0, 64), Ok(500, 60), Ok(500, 64), Kk(4100, 64), Ok(4300, 64), Kk(8100, 64), Ok(8300, 64)};
    std::size_t held = 0; heldEnd(s, held);
    const Run r = run(s, 64);
    std::vector<Rec> mid; bool back = false;
    for (const Rec& e : onsOf(r)) { if (e.a >= 4100 && e.a < 8100) mid.push_back(e); if (e.a >= 8200 && e.key == 64) back = true; }
    return !mid.empty() && allKeys(mid, 60) && back && held == 2;
  }});
  sc.push_back({"latch replace: re-pressing the SAME chord is a gesture start and re-anchors (m := 0)", [&](const Plant& pl) {
    Session s; s.total = 9000; s.plants = pl; s.base.latch = Latch::Replace;
    s.ins = {Kk(0, 60), Kk(0, 64), Kk(0, 67), Ok(500, 60), Ok(500, 64), Ok(500, 67), Kk(4500, 60), Kk(4500, 64), Kk(4500, 67), Ok(4700, 60), Ok(4700, 64), Ok(4700, 67)};
    const auto v = onsFrom(run(s, 64), 5000);
    return !v.empty() && v[0].a == 5000 && v[0].key == 60;   // without the re-anchor, m = 5 and the step plays 67
  }});
  sc.push_back({"latch replace: latched C+E, then a NEW chord C+F: C and F play, E is gone", [&](const Plant& pl) {
    Session s; s.total = 9000; s.plants = pl; s.base.latch = Latch::Replace;
    s.ins = {Kk(0, 60), Kk(0, 64), Ok(500, 60), Ok(500, 64), Kk(4500, 60), Kk(4500, 65), Ok(4700, 60), Ok(4700, 65)};
    std::size_t held = 0; heldEnd(s, held);
    bool has60 = false, has65 = false, has64 = false;
    for (const Rec& e : onsFrom(run(s, 64), 5000)) { has60 = has60 || e.key == 60; has65 = has65 || e.key == 65; has64 = has64 || e.key == 64; }
    return has60 && has65 && !has64 && held == 2;
  }});
  sc.push_back({"pedal: released under the pedal, re-pressed and held: the pedal coming up must not drop it", [&](const Plant& pl) {
    Session s; s.total = 6000; s.plants = pl;
    s.ins = {Ped(0, true), Kk(0, 60), Ok(500, 60), Kk(1500, 60), Ped(3000, false)};
    std::size_t held = 0; heldEnd(s, held);
    const auto v = onsFrom(run(s, 64), 4000);
    return held == 1 && !v.empty() && allKeys(v, 60);
  }});
  sc.push_back({"overflow recovery: the refused 33rd key can be pressed once room frees", [&](const Plant& pl) {
    Session s; s.total = 5000; s.plants = pl;
    for (int k = 40; k <= 72; ++k) s.ins.push_back(Kk(0, k));
    s.ins.push_back(Ok(100, 72));
    for (int k = 40; k <= 71; ++k) s.ins.push_back(Ok(200, k));
    s.ins.push_back(Kk(3000, 72));
    std::size_t held = 0; heldEnd(s, held);
    const Run r = run(s, 64);
    const auto v = onsFrom(r, 3000);
    return held == 1 && !v.empty() && allKeys(v, 72) && r.core->counters().heldOverflow == 1;
  }});
  int fired = 0;
  std::string firedBy;
  for (const KeyScenario& k : sc) if (!k.good({"sameRefDropsPress"})) { ++fired; firedBy += "."; }
  plant(fired == static_cast<int>(sc.size()), "each id-less scenario detects the first-version rule (a known ref's NoteOn dropped; an ignored ref never expires): " + std::to_string(fired) + " of " + std::to_string(sc.size()) + " red");
  for (const KeyScenario& k : sc) check(k.good({}), std::string("id-less refs, ") + k.what);
}

// ------------------------------------------------------------------ enabling with keys held under the free clock (SPEC 13b.17, 2.4 H7b)
void testEnableRestartsFreeClock() {
  std::puts("Enable with keys held under the free clock restarts the clock");
  auto outcome = [](bool moduleToggle, const Plant& pl) {
    Session s; s.total = 9000; s.plants = pl; s.host = std::make_shared<SimHost>(8000, 120, 0, false); s.base.latch = Latch::Replace;
    s.ins = {K(37, "a", 60), K(37, "b", 64), O(300, "a"), O(300, "b")};
    s.cfg = {C(3000, [moduleToggle](Settings& x) { (moduleToggle ? x.enabled : x.arpEnabled) = false; }), C(5000, [moduleToggle](Settings& x) { (moduleToggle ? x.enabled : x.arpEnabled) = true; })};
    std::uint32_t before = 0, after = 0;
    s.probe = [&](int pos, int, const Core& c) { if (pos == 4900) before = c.epoch(); if (pos == 5000) after = c.epoch(); };
    const Run r = run(s, 100);
    const auto v = onsFrom(r, 5000);
    // first note exactly at 5000 (step 0 of the restarted clock, m = 0 -> key 60), then every 1000, epoch + 1
    return v.size() >= 3 && v[0].a == 5000 && v[0].key == 60 && v[1].a == 6000 && v[1].key == 64 && after == before + 1 && n1(r).empty();
  };
  check(outcome(false, {}), "known answer: arp_enabled off 3000-5000 with a latched chord, transport stopped: the first note is exactly at 5000 and the epoch advanced");
  check(outcome(true, {}), "known answer: the same with the module's `enabled`");
}

// ------------------------------------------------------------------ T11 against the 1-sample minimum
void testT11OneSample() {
  std::puts("T11: never two notes sounding against a one-note table");
  auto maxSimultaneous = [](const Plant& pl) {
    Session s; s.total = 12000; s.plants = pl; s.caps.sounding = 1; s.base.rate = Rate::R1_64; s.base.gate = 200;
    s.ins = {K(0, "a", 60), K(0, "b", 64)};
    s.jitter = [](int pos) { return pos >= 10250 ? 960 : 0; };   // ONE forward re-base of 960 ticks: two late steps land on one sample
    const Run r = run(s, 50);
    int active = 0, most = 0;
    for (const Rec& e : r.evs) { active += e.t == EventType::NoteOn ? 1 : e.t == EventType::NoteOff ? -1 : 0; most = std::max(most, active); }
    return std::make_pair(most, n1(r).size());
  };
  plant(maxSimultaneous({"t11EvictsFirst"}).first > 1, "T11 detects an eviction of a note that cannot be released yet: " + std::to_string(maxSimultaneous({"t11EvictsFirst"}).first) + " notes at once");
  const auto ok = maxSimultaneous({});
  check(ok.first <= 1 && ok.second == 0, "T11: with a 1-entry table at most " + std::to_string(ok.first) + " note sounds at any event, also across a forward re-base at 1/64; N1 holds");
}

// ------------------------------------------------------------------ T10 across prepare()
void testPrepareOwesOffs() {
  std::puts("T10: prepare() does not strand a sounding note");
  auto run1 = [](const Plant& pl) {
    auto c = makeCore({}, pl);
    c->prepare(8000, 64);
    std::vector<std::int64_t> ons, offsBefore;
    Event k1; k1.type = EventType::NoteOn; k1.key = 60; k1.vel = 100; k1.ref = NoteRef{0, 0, 60, 7};
    Event k2 = k1; k2.key = 64; k2.ref = NoteRef{0, 0, 64, 8};
    for (int b = 0; b < 20; ++b) {   // 1280 samples: steps at 0 and 1000, both still sounding (gate 200)
      std::vector<Event> in;
      if (b == 0) { k1.offset = 0; k2.offset = 0; in = {k1, k2}; }
      Block blk; blk.n = 64; blk.settings.enabled = true; blk.settings.gate = 200; blk.transport.playing = false; blk.in = std::span<const Event>(in.data(), in.size());
      for (const Event& e : c->process(blk)) { if (e.type == EventType::NoteOn) ons.push_back(e.ref.id); if (e.type == EventType::NoteOff) offsBefore.push_back(e.ref.id); }
    }
    const std::size_t owedCount = c->soundingCount();
    c->prepare(48000, 256);   // a new rate: the adapter did not call reset() first
    std::vector<std::int64_t> offsAfter;
    bool allAtZeroFirst = true;
    bool seenNonOff = false;
    Block blk; blk.n = 256; blk.settings.enabled = false; blk.transport.playing = false;
    for (const Event& e : c->process(blk)) {
      if (e.type == EventType::NoteOff) { offsAfter.push_back(e.ref.id); allAtZeroFirst = allAtZeroFirst && e.offset == 0 && !seenNonOff; } else seenNonOff = true;
    }
    // life 0: every on must end exactly once, before or at offset 0 of the first block after prepare
    int ended = 0;
    for (std::int64_t id : ons) {
      int cnt = static_cast<int>(std::count(offsBefore.begin(), offsBefore.end(), id)) + static_cast<int>(std::count(offsAfter.begin(), offsAfter.end(), id));
      if (cnt == 1) ++ended;
    }
    return std::make_tuple(owedCount, ons.size(), static_cast<std::size_t>(ended), allAtZeroFirst, offsAfter.size());
  };
  const auto bad = run1({"prepareForgetsNotes"});
  plant(std::get<0>(bad) > 0 && std::get<2>(bad) < std::get<1>(bad), "T10 detects a prepare() that drops the notes still owed a NoteOff (" + std::to_string(std::get<1>(bad) - std::get<2>(bad)) + " stranded)");
  const auto g = run1({});
  check(std::get<0>(g) > 0 && std::get<1>(g) > 1 && std::get<2>(g) == std::get<1>(g) && std::get<3>(g) && std::get<4>(g) == std::get<0>(g),
        "T10: " + std::to_string(std::get<0>(g)) + " notes sounding at prepare() (new rate) are each released exactly once, at offset 0 of the next process(), before anything else");
}

// ------------------------------------------------------------------ host floats: NaN, inf, absurd values (SPEC 2.2 adapter rules)
void testHostFloats() {
  std::puts("Host floats: NaN / inf / absurd beats and tempo");
  const double nan = std::nan(""), inf = HUGE_VAL;
  for (double beats : {nan, inf, -inf, 1e300, -1e300}) {   // no usable beats timeline: the free clock, so the first note lands ON the press
    Session s; s.total = 12000; s.ins = {Kk(137, 60)};
    s.tweak = [beats](int, Transport& t) { t.hasRep = false; t.beats = beats; };
    const Run r = run(s, 64);
    const std::vector<Rec> on = onsOf(r);
    check(on.size() > 8 && on[0].a == 137 && on[1].a == 1137 && n1(r).empty(), "known answer: beats = " + std::to_string(beats) + " is treated as no beats timeline (free clock: the first note on the press, 1000 apart)");
  }
  { // a few bad blocks in the middle of an exact timeline: no undefined behaviour, N1 holds
    Session s; s.total = 20000; s.base.gate = 150; s.ins = {Kk(0, 60), Kk(0, 64)};
    s.tweak = [nan, inf](int pos, Transport& t) { if (pos >= 8000 && pos < 8400) { t.hasRep = false; t.beats = (pos / 64) % 2 ? nan : inf; } if (pos >= 12000 && pos < 12300) t.tempo = (pos / 64) % 2 ? nan : inf; };
    const Run r = run(s, 64);
    check(n1(r).empty() && r.core->counters().offsLost == 0, "N1 holds across blocks that report NaN / inf beats and NaN / inf tempo in the middle of an exact timeline");
  }
  { // tempo: NaN / inf = as if none were reported (the last valid one stays; else 120)
    auto spacing = [&](auto tweak) {
      Session s; s.total = 12000; s.host = std::make_shared<SimHost>(8000, 120, 0, false); s.ins = {Kk(0, 60)};
      s.tweak = tweak;
      const std::vector<Rec> on = onsOf(run(s, 64));
      return on.size() > 3 ? on[2].a - on[1].a : -1;
    };
    const bool a = spacing([=](int, Transport& t) { t.hasTempo = true; t.tempo = nan; }) == 1000;
    const bool b = spacing([=](int, Transport& t) { t.hasTempo = true; t.tempo = inf; }) == 1000;
    const bool c = spacing([=](int pos, Transport& t) { t.hasTempo = true; t.tempo = pos == 0 ? 90 : nan; }) == 1333;   // 90 bpm: 1/16 = 3840 ticks = 1333.33 samples
    check(a && b && c, "known answer: tempo NaN / inf means no tempo reported: 120 bpm from the start, or the last valid tempo (90) kept");
  }
  { // the host's loop: a wrap is a position jump (T6); the loop flags themselves are carried, not interpreted
    Session s; s.total = 20000; s.base.gate = 200; s.ins = {Kk(0, 60), Kk(0, 64)};
    auto h = std::make_shared<SimHost>(8000, 120, 0, true);
    h->set(12000, std::nullopt, 0.0);
    s.host = h;
    s.tweak = [](int, Transport& t) { t.loopActive = true; t.loopStartBeats = 0; t.loopEndBeats = 3; };
    const Run r = run(s, 64);
    check(n1(r).empty() && r.core->counters().rows[6] > 0, "a reported loop wrap is a jump: T6 acted, N1 holds");
  }
}

}  // namespace

// ------------------------------------------------------------------ --dump-defaults, for tools/core_params_check.py
static void dumpDefaults() {
  const Settings s;
  auto b = [](bool v) { return v ? "true" : "false"; };
  auto list = [](const char* key, const char* const* names, std::size_t n) {
    std::string j;
    for (std::size_t i = 0; i < n; ++i) j += std::string(i ? "," : "") + names[i];
    std::printf("values.%s=%s\n", key, j.c_str());
  };
#define ENUM(key, val, names) std::printf("%s=%s\n", key, names[static_cast<std::size_t>(val)]); list(key, names, std::size(names))
  std::printf("enabled=%s\narp_enabled=%s\n", b(s.enabled), b(s.arpEnabled));
  ENUM("order", s.order, arp::kOrderNames);
  ENUM("pattern", s.pattern, arp::kPatternNames);
  ENUM("rate", s.rate, arp::kRateNames);
  ENUM("rate_mode", s.rateMode, arp::kRateModeNames);
  std::printf("step_time_us=%d\nsteps=%d\nstep_size=%d\n", s.stepTimeUs, s.steps, s.stepSize);
  ENUM("step_unit", s.stepUnit, arp::kStepUnitNames);
  std::printf("gate=%d\nswing=%d\n", s.gate, s.swing);
  ENUM("latch", s.latch, arp::kLatchNames);
  ENUM("vel_mode", s.velMode, arp::kVelModeNames);
  std::printf("vel_fixed=%d\n", s.velFixed);
  ENUM("restart", s.restart, arp::kRestartNames);
  std::printf("repeats=%d\n", s.repeats);
  ENUM("quantize", s.quantize, arp::kQuantizeNames);
  ENUM("scale_source", s.scaleSource, arp::kScaleSourceNames);
  std::printf("scale_root=%d\n", s.scaleRoot);
  for (int i = 0; i < 12; ++i) std::printf("scale_deg%d=%s\n", i, b(s.scaleDeg[i]));
  std::printf("seed=%u\nchord_enabled=%s\n", s.seed, b(s.chordEnabled));
  ENUM("chord_unit", s.chordUnit, arp::kStepUnitNames);
  for (int i = 0; i < 6; ++i) std::printf("chord_on%d=%s\nchord_shift%d=%d\n", i + 1, b(s.chordOn[i]), i + 1, s.chordShift[i]);
#undef ENUM
  std::printf("tpq=%d\n", arp::kTpq);
  for (std::size_t i = 0; i < std::size(arp::kRateNames); ++i) std::printf("rate_ticks.%s=%d\n", arp::kRateNames[i], arp::kRateTicks[i]);
}

// The COMPILED bank, for tools/core_params_check.py: one line per pattern header and per step.
static void dumpBank() {
  for (int i = 0; i < arp::kBankCount; ++i) {
    const arp::PatternDef& p = arp::kBank[i];
    std::printf("pattern.%s=%d,%d,%s\n", arp::kBankIds[i], p.length, p.stepSize, arp::kStepUnitNames[static_cast<std::size_t>(p.stepUnit)]);
    for (int j = 0; j < p.length; ++j) {
      const arp::PatternStep& s = p.steps[j];
      std::printf("pattern.%s.%d=%d,%d,%d,%d,%d,%d,%d,%d,%d\n", arp::kBankIds[i], j, s.on, s.tie, s.vel, s.gate, s.step, s.prob, s.ratchet, s.trig, s.pick);
    }
  }
}

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--dump-defaults") == 0) {
    dumpDefaults();
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "--dump-bank") == 0) {
    dumpBank();
    return 0;
  }
  std::printf("arp core suite (int128 path: %s, plants: %s)\n", arp::kInt128IsNative ? "native" : "two-word fallback", arp::plantsCompiledIntoLibrary() ? "compiled in" : "OFF");
  check(arp::plantsCompiledIntoLibrary(), "this test target's core was built with ARP_PLANTS");
  runInt128Tests();
  testN1();
  testG1();
  testG2();
  testG2b();
  testG4();
  testG5();
  testD1();
  testA1();
  testC1();
  testHeldSet();
  testFreeClock();
  testClamps();
  testKeyRefs();
  testEnableRestartsFreeClock();
  testT11OneSample();
  testPrepareOwesOffs();
  testHostFloats();
  runM1bTests();
  runM2aTests();
  runM2bTests();
  runM3Tests();
  testTiming();
  testUpModel();
  runDigestTests();   // LAST: it compares every run() recorded above against the committed pre-optimisation digest
  std::printf("%s: %d passed, %d failed\n", tally().fail ? "FAIL" : "OK", tally().pass, tally().fail);
  return tally().fail ? 1 : 0;
}
