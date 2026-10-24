// test_m3.cpp - the M3a checks (ROADMAP T-007, horde admission): the rows of horde's note-processor 1.0 bar that M1/M2 left uncovered.
//   1. "Sample-exact timing ... time signatures as data":   restart `bar` over several meters, tempos, sample rates and block sizes.
//   2. "No input, no output":                               nothing is held (and no latch) -> not one NoteOn, over the whole mode matrix.
//   3. "Every emitted note ends exactly once ... preset load": one snapshot changing EVERY setting plus a new user pattern, mid-state.
//   4. "The pattern bank selection is a morph target too":  `pattern` across every bank entry and `user`, `order` across all 16.
// Same charter as the other suites: each detector is preceded by its plant, a plant that does not fire is a FAIL, and "known answer"
// assertions (exact values written from the spec text, not from the core's formulas) carry no plant and say so.
//
// 8 kHz and 120 bpm unless a check says otherwise: 1 beat = 4000 samples, a 1/16 step = 1000 samples = 3840 ticks.
#include "test_m3.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "arp/bank_data.h"
#include "harness.h"

namespace arp_test {
namespace {

Change C(int at, std::function<void(Settings&)> f) { return Change{at, std::move(f)}; }
constexpr std::array<int, 5> kBlocks = {1, 7, 64, 512, 4096};   // the G1 block sizes of the bar's row
constexpr unsigned kCMajor = 0xAB5;                               // root-relative degrees 0 2 4 5 7 9 11
const Scale kCMajorScale{0, static_cast<std::uint16_t>(kCMajor)};

std::int64_t cdivI(std::int64_t a, std::int64_t b) { return (a + b - 1) / b; }   // a >= 0, b > 0 only: plain int64, independent of the core's arith.h
std::string joinS(const std::vector<std::int64_t>& v, std::size_t n = 8) {
  std::string s;
  for (std::size_t i = 0; i < v.size() && i < n; ++i) s += (i ? "," : "") + std::to_string(v[i]);
  if (v.size() > n) s += ",...(" + std::to_string(v.size()) + ")";
  return s;
}

struct B {   // a PatternStep builder
  PatternStep s;
  B& on(bool v) { s.on = v; return *this; }
  B& tie() { s.tie = true; return *this; }
  B& gate(int v) { s.gate = v; return *this; }
  B& step(int v) { s.step = v; return *this; }
  B& ratchet(int v) { s.ratchet = v; return *this; }
  B& trig(int v) { s.trig = v; return *this; }
  operator PatternStep() const { return s; }
};
UserPattern userPat(std::vector<PatternStep> steps, int stepSize = 12) {
  UserPattern u;
  u.length = static_cast<int>(steps.size());
  u.stepSize = stepSize;
  for (std::size_t i = 0; i < steps.size() && i < 16; ++i) u.steps[i] = steps[i];
  return u;
}

// One note's life in a run: its on, its off (the sample of the NoteOff with the same ref), the step that made it.
struct Span { std::int64_t on = 0, off = -1, ord = 0; int key = 0, vel = 0; };
std::vector<Span> spansOf(const Run& r) {
  std::vector<Span> v;
  for (const Rec& e : r.evs) {
    if (e.t != EventType::NoteOn) continue;
    Span s;
    s.on = e.a; s.ord = e.ord; s.key = e.key; s.vel = e.vel;
    for (const Rec& o : r.evs) if (o.t == EventType::NoteOff && o.epoch == e.epoch && o.ref == e.ref) s.off = o.a;
    v.push_back(s);
  }
  return v;
}
std::int64_t stepsBefore(const Run& r, std::int64_t at) {
  std::int64_t n = 0;
  for (const StepRec& st : r.steps) n += st.a < at;
  return n;
}

// ====================================================================== 1. several meters, tempos and block sizes (bar row: sample-exact timing, time signatures as data)
// restart = bar, `up` over ONE held key at 1/16, so every bar start is a grid step (a 4/4, 3/4, 6/8, 7/8 or 5/4 bar is a whole number of 3840-tick
// steps). The anchor of a bar is the step with k = 0 (StepRecord::k = kAbs - anchorK, SPEC 4.5), and k = 0 happens only AT a bar start.
// Each such step must land at cdiv(barStartTick * D, tempo * TPQ) samples (SPEC 2.2/2.3, written here in plain int64), and its NoteOn must carry
// that tick. The meter reaches the core two ways: only as timeSigNum / timeSigDen (bars counted from tick 0, SPEC 4.5), or with the host's
// barStartBeats as well (a PHASE, SPEC 13.12; a pickup beat is a phase that is not a multiple of the bar). A mid-session change is a block cut
// (the adapter splits the block where the meter changes, as it does for any host edge): a no-op Change at that sample cuts it here.
struct MeterSeg { std::int64_t startTick; int num, den; };
struct MeterScene { const char* name; bool hostBarStart; std::vector<MeterSeg> segs; std::int64_t endTick; };
struct TempoRow { double bpm; int sr; };

std::int64_t barLen(const MeterSeg& g) { return 4ll * kTpq * g.num / g.den; }
std::vector<MeterScene> meterScenes() {
  auto bars = [](std::int64_t start, int num, int den, int n) { return start + n * barLen(MeterSeg{0, num, den}); };
  return {
      {"3/4 by time signature alone", false, {{0, 3, 4}}, bars(0, 3, 4, 5)},
      {"6/8 by time signature alone", false, {{0, 6, 8}}, bars(0, 6, 8, 5)},
      {"7/8 by time signature alone", false, {{0, 7, 8}}, bars(0, 7, 8, 4)},
      {"5/4 by time signature alone", false, {{0, 5, 4}}, bars(0, 5, 4, 4)},
      {"4/4 -> 3/4 -> 5/4 by time signature alone (both changes on a bar line of both)", false, {{0, 4, 4}, {184320, 3, 4}, {230400, 5, 4}}, 230400 + 3 * barLen({0, 5, 4})},
      {"3/4 with the host's bar start, one beat of pickup", true, {{kTpq, 3, 4}}, bars(kTpq, 3, 4, 5)},
      {"6/8 with the host's bar start", true, {{0, 6, 8}}, bars(0, 6, 8, 5)},
      {"7/8 with the host's bar start 2.5 beats in", true, {{38400, 7, 8}}, bars(38400, 7, 8, 4)},
      {"5/4 with the host's bar start", true, {{0, 5, 4}}, bars(0, 5, 4, 4)},
      {"4/4 -> 7/8 -> 6/8 with the host's bar start (changes off the old grid)", true, {{0, 4, 4}, {122880, 7, 8}, {230400, 6, 8}}, 230400 + 3 * barLen({0, 6, 8})},
  };
}
// The bar starts a scene must produce, in ticks: segment by segment, every start + n * length inside the segment (and >= 0).
std::vector<std::int64_t> barStartTicks(const MeterScene& m) {
  std::vector<std::int64_t> v;
  for (std::size_t i = 0; i < m.segs.size(); ++i) {
    const std::int64_t len = barLen(m.segs[i]), to = i + 1 < m.segs.size() ? m.segs[i + 1].startTick : m.endTick;
    if (!m.hostBarStart && m.segs[i].startTick % len != 0) { std::fprintf(stderr, "test bug: scene '%s' changes meter off the tick-0 bar grid\n", m.name); std::abort(); }
    for (std::int64_t t = m.segs[i].startTick; t < to; t += len) if (t >= 0) v.push_back(t);
  }
  return v;
}
Session meterSession(const MeterScene& m, const TempoRow& t, const Plant& pl) {
  Session s;
  s.sr = t.sr; s.bpm = t.bpm; s.plants = pl;
  s.base.restart = Restart::Bar; s.base.rate = Rate::R1_16; s.base.gate = 30;
  const std::int64_t Tm = std::llround(t.bpm * 1000), D = 60ll * t.sr * 1000;
  auto sampleOf = [=](std::int64_t tick) { return cdivI(tick * D, Tm * kTpq); };
  std::vector<std::int64_t> cut;
  for (std::size_t i = 0; i < m.segs.size(); ++i) {
    cut.push_back(i == 0 ? 0 : sampleOf(m.segs[i].startTick));
    if (i > 0) s.cfg.push_back(C(static_cast<int>(cut.back()), [](Settings&) {}));   // only the CUT matters: no setting changes
  }
  s.total = static_cast<int>(sampleOf(m.endTick));   // the end tick itself is outside the window: its sample is not played
  s.tweak = [m, cut, Tm, D](int pos, Transport& tr) {
    std::size_t i = 0;
    for (std::size_t j = 0; j < cut.size(); ++j) if (pos >= cut[j]) i = j;
    const MeterSeg& g = m.segs[i];
    tr.timeSigNum = g.num; tr.timeSigDen = g.den;
    if (!m.hostBarStart) return;
    const std::int64_t tick = (static_cast<std::int64_t>(pos) * Tm * kTpq) / D, len = barLen(g), rel = tick - g.startTick;
    const std::int64_t n = rel >= 0 ? rel / len : -((-rel + len - 1) / len);   // floor division
    tr.hasBarStart = true;
    tr.barStartBeats = static_cast<double>(g.startTick + n * len) / kTpq;
  };
  s.ins = {K(0, "a", 60)};
  return s;
}
// "" when every bar start of the scene lands on its exact sample at every block size, else why not.
std::string meterBad(const MeterScene& m, const TempoRow& t, const Plant& pl, int* barsChecked = nullptr) {
  const std::int64_t Tm = std::llround(t.bpm * 1000), D = 60ll * t.sr * 1000;
  const std::vector<std::int64_t> ticks = barStartTicks(m);
  std::vector<std::int64_t> want;
  for (std::int64_t tick : ticks) want.push_back(cdivI(tick * D, Tm * kTpq));
  const Session s = meterSession(m, t, pl);
  std::string refStream;
  for (int block : kBlocks) {
    const Run r = run(s, block);
    std::vector<std::int64_t> got;
    for (const StepRec& st : r.steps) if (st.s.k == 0) got.push_back(st.a);
    if (got != want) return "block " + std::to_string(block) + ": bar-start steps at " + joinS(got) + ", expected " + joinS(want);
    const std::vector<Rec> ons = onsOf(r);
    for (std::size_t i = 0; i < want.size(); ++i) {
      const bool hit = std::any_of(ons.begin(), ons.end(), [&](const Rec& e) { return e.a == want[i] && e.tick == ticks[i]; });
      if (!hit) return "block " + std::to_string(block) + ": no NoteOn of tick " + std::to_string(ticks[i]) + " at sample " + std::to_string(want[i]);
    }
    if (!n1(r).empty()) return "block " + std::to_string(block) + ": N1: " + n1(r)[0];
    const std::string st = streamOf(r);
    if (refStream.empty()) refStream = st; else if (st != refStream) return "block " + std::to_string(block) + ": the event stream differs from block " + std::to_string(kBlocks[0]);
  }
  if (barsChecked) *barsChecked += static_cast<int>(want.size());
  return "";
}

void testMeters() {
  std::puts("Bar row: sample-exact timing, time signatures as data (restart bar over 3/4, 6/8, 7/8, 5/4 and a mid-session change)");
  const std::vector<MeterScene> scenes = meterScenes();
  const std::vector<TempoRow> rows = {{120, 8000}, {77.7, 8000}, {999, 8000}, {133.7, 44100}};
  // The plant first: every scene has a non-4/4 stretch, so the 4/4 reading must put at least one bar start on the wrong sample.
  int red = 0;
  std::string first;
  for (const MeterScene& m : scenes) {
    const std::string b = meterBad(m, rows[0], {"meterAs44"});
    red += !b.empty();
    if (first.empty() && !b.empty()) first = std::string(m.name) + ": " + b;
  }
  plant(red == static_cast<int>(scenes.size()), "the bar check detects the meter read as 4/4 (" + std::to_string(red) + " of " + std::to_string(scenes.size()) + " scenes red; e.g. " + first + ")");
  int bars = 0, cells = 0;
  std::string bad;
  for (const MeterScene& m : scenes)
    for (const TempoRow& t : rows) {
      const std::string b = meterBad(m, t, {}, &bars);
      ++cells;
      if (!b.empty() && bad.empty()) bad = std::string(m.name) + " @" + std::to_string(t.bpm) + " bpm / " + std::to_string(t.sr) + " Hz: " + b;
    }
  check(bad.empty() && bars > 100, "known answer: every bar's first step lands at the bar start's exact sample, cdiv(tick * D, tempo * TPQ): " + std::to_string(scenes.size()) + " meter scenes x " + std::to_string(rows.size()) +
        " tempo/rate rows x block sizes 1/7/64/512/4096 (" + std::to_string(cells) + " cells, " + std::to_string(bars) + " bar starts in all), the NoteOn carries the bar-start tick, N1 and G1 hold" + (bad.empty() ? "" : " - " + bad));
}

// ====================================================================== 2. no input, no output
// Nothing is ever held and latch is off. Over every order style, both rate modes, every restart value, the chord stage on and off, and three transports
// (playing, stopped, play -> stop -> play), with a plain pattern and with the two trigger patterns: not one NoteOn. A pedal press and a Panic with nothing held
// ride along (the pedal holds INPUT; with none there is nothing to hold). SPEC 2.5 FIRES trigger-only steps with no keys held under restart bar / free while the
// host clock plays (ratified), so triggers are counted SEPARATELY and reported; whether horde's row means them is asked of horde.
struct EmptyCell { Order order; RateMode rm; Restart rs; bool chord; int transport; Pattern pat; };
Session emptySession(const EmptyCell& c, const Plant& pl) {
  Session s;
  s.sr = 8000; s.total = 12000; s.maxBlock = 256; s.plants = pl;
  s.base.order = c.order; s.base.rateMode = c.rm; s.base.restart = c.rs; s.base.chordEnabled = c.chord; s.base.pattern = c.pat;
  s.base.latch = Latch::Off;
  s.base.stepTimeUs = 62500;
  auto h = std::make_shared<SimHost>(8000, 120, 0, c.transport != 1);
  if (c.transport == 2) { h->set(4000, false); h->set(8000, true); }
  s.host = h;
  s.ins = {Ped(3000, true), Ped(5000, false), Pan(9000)};
  return s;
}
struct EmptyTally { int runs = 0; std::int64_t ons = 0, offs = 0, trigs = 0, trigsAllowed = 0, trigsOther = 0; int trigPatternRuns = 0, trigPatternFired = 0; std::string firstOn, firstWrong; };
EmptyTally emptyMatrix(const Plant& pl) {
  EmptyTally t;
  for (int o = 0; o < 16; ++o)
    for (RateMode rm : {RateMode::Sync, RateMode::Time})
      for (Restart rs : {Restart::Chord, Restart::Bar, Restart::Free})
        for (bool chord : {false, true})
          for (int tp = 0; tp < 3; ++tp)
            for (Pattern pat : {Pattern::None, Pattern::BarTrigger, Pattern::TriggerPulse}) {
              const EmptyCell c{static_cast<Order>(o), rm, rs, chord, tp, pat};
              const Run r = run(emptySession(c, pl), 256);
              ++t.runs;
              std::int64_t trigs = 0;
              for (const Rec& e : r.evs) {
                t.ons += e.t == EventType::NoteOn;
                t.offs += e.t == EventType::NoteOff;
                trigs += e.t == EventType::Trigger;
                if (e.t == EventType::NoteOn && t.firstOn.empty())
                  t.firstOn = std::string(kOrderNames[o]) + "/" + kRateModeNames[static_cast<int>(rm)] + "/" + kRestartNames[static_cast<int>(rs)] + (chord ? "/chord" : "") + " transport " + std::to_string(tp) + " at sample " + std::to_string(e.a);
              }
              t.trigs += trigs;
              // SPEC 2.5 / 2.6: trigger-only steps fire with no keys held iff the HOST clock runs (transport playing), the grid is musical (sync) and an anchor exists
              // without a gesture (restart bar / free). Judged on the two pure transports; the play-stop-play one is counted only.
              const bool allowed = rs != Restart::Chord && rm == RateMode::Sync && tp == 0;
              if (tp == 2) { t.trigsOther += trigs; continue; }
              if (allowed) t.trigsAllowed += trigs;
              const bool trigPattern = pat != Pattern::None;
              const bool expectTrigs = trigPattern && allowed;
              if (trigPattern) { ++t.trigPatternRuns; t.trigPatternFired += trigs > 0; }
              if ((trigs > 0) != expectTrigs && t.firstWrong.empty())
                t.firstWrong = std::string(kOrderNames[o]) + "/" + kRateModeNames[static_cast<int>(rm)] + "/" + kRestartNames[static_cast<int>(rs)] + (chord ? "/chord" : "") + " transport " + std::to_string(tp) + " " + kPatternNames[static_cast<int>(pat)] + ": " + std::to_string(trigs) + " triggers";
            }
  return t;
}
void testNoInputNoOutput() {
  std::puts("Bar row: no input, no output (nothing held and no latch emits nothing)");
  const EmptyTally p = emptyMatrix({"emitOnEmptySet"});
  plant(p.ons > 0, "the no-input check detects a note played on an empty set (" + std::to_string(p.ons) + " NoteOns in " + std::to_string(p.runs) + " sessions; first: " + p.firstOn + ")");
  const EmptyTally t = emptyMatrix({});
  check(t.ons == 0 && t.offs == 0 && t.runs == 16 * 2 * 3 * 2 * 3 * 3,
        "no input, no output: 0 NoteOns and 0 NoteOffs over " + std::to_string(t.runs) + " sessions (16 orders x sync/time x restart chord/bar/free x chord stage on/off x transport playing/stopped/play-stop-play x plain/bar_trigger/trigger_pulse, a pedal press and a Panic with nothing held)" + (t.ons ? " - first: " + t.firstOn : ""));
  check(t.firstWrong.empty() && t.trigsAllowed > 0,
        "known answer (SPEC 2.5, REPORTED not asserted zero): Triggers with no keys held fire only with the transport playing, in sync mode, under restart bar / free: " + std::to_string(t.trigs) + " triggers in all (" + std::to_string(t.trigsAllowed) +
        " on the playing transport under bar / free, " + std::to_string(t.trigsOther) + " in the play-stop-play sessions); " + std::to_string(t.trigPatternFired) + " of " + std::to_string(t.trigPatternRuns) + " trigger-pattern sessions on the pure transports fired" + (t.firstWrong.empty() ? "" : " - UNEXPECTED: " + t.firstWrong));
  { // after the last key is released nothing new starts either (latch off): the arp does not keep playing a ghost chord
    Session s;
    s.total = 12000; s.base.gate = 50;
    s.ins = {K(100, "a", 60), K(100, "b", 64), K(100, "c", 67), O(3000, "a"), O(3000, "b"), O(3000, "c")};
    const Run r = run(s, 64);
    std::int64_t lastOn = -1;
    for (const Rec& e : onsOf(r)) lastOn = std::max(lastOn, e.a);
    check(lastOn >= 0 && lastOn < 3000 && onsOf(r).size() >= 2 && n1(r).empty(), "known answer: with the keys released (latch off) the last NoteOn is before the release (" + std::to_string(lastOn) + " < 3000) and nothing starts after it");
  }
}

// ====================================================================== 3. preset load as a terminal path
// A preset load, as horde delivers it, is ONE block-boundary snapshot: all settings at once, and a new user pattern published in the same gap (setUserPattern is
// called off the audio thread; process() takes it at the first step evaluated after). `scramble` builds the snapshot so that every one of the 47 settings differs
// from the session's (continuous and discrete, pattern / order / rate / rate_mode / restart / latch / chord stage / quantize and scale included); `unchanged`
// proves it, field by field, so the claim "every setting changes" is checked and not assumed.
const UserPattern kPresetUser = userPat({B().ratchet(2).trig(3), B().tie(), B().on(false), B().step(2), B().ratchet(3).gate(150), B()}, 7);

Settings scramble(const Settings& a, bool flipEnabled, bool flipArp, Latch bLatch) {
  Settings b = a;
  b.enabled = flipEnabled ? !a.enabled : a.enabled;
  b.arpEnabled = flipArp ? !a.arpEnabled : a.arpEnabled;
  b.order = static_cast<Order>((static_cast<int>(a.order) + 5) % 16);
  b.pattern = a.pattern == Pattern::User ? Pattern::Gallop : Pattern::User;
  b.rate = a.rate == Rate::R1_8 ? Rate::R1_16 : Rate::R1_8;
  b.rateMode = a.rateMode == RateMode::Sync ? RateMode::Time : RateMode::Sync;
  b.stepTimeUs = a.stepTimeUs == 125000 ? 80000 : 125000;
  b.steps = a.steps == 2 ? 1 : 2;
  b.stepSize = a.stepSize == 7 ? -5 : 7;
  b.stepUnit = a.stepUnit == StepUnit::Semitones ? StepUnit::Degrees : StepUnit::Semitones;
  b.gate = a.gate < 100 ? 150 : 60;
  b.swing = a.swing < 600 ? 700 : 520;
  b.latch = bLatch;
  b.velMode = a.velMode == VelMode::Played ? VelMode::Fixed : VelMode::Played;
  b.velFixed = a.velFixed == 100 ? 40 : 100;
  b.restart = static_cast<Restart>((static_cast<int>(a.restart) + 1) % 3);
  b.repeats = a.repeats == 0 ? 2 : 0;
  b.quantize = static_cast<Quantize>((static_cast<int>(a.quantize) + 1) % 3);
  b.scaleSource = a.scaleSource == ScaleSource::Global ? ScaleSource::Own : ScaleSource::Global;
  b.scaleRoot = (a.scaleRoot + 5) % 12;
  for (int d = 0; d < 12; ++d) b.scaleDeg[d] = !a.scaleDeg[d];
  b.seed = a.seed + 7;
  b.chordEnabled = !a.chordEnabled;
  b.chordUnit = a.chordUnit == StepUnit::Semitones ? StepUnit::Degrees : StepUnit::Semitones;
  for (int i = 0; i < 6; ++i) { b.chordOn[i] = !a.chordOn[i]; b.chordShift[i] = a.chordShift[i] == 9 ? 5 : 9; }
  return b;
}
// The name of the first setting that is EQUAL in a and b (the snapshot failed to change it), "" if all 47 differ.
std::string unchanged(const Settings& a, const Settings& b) {
#define SAME(f) if (a.f == b.f) return #f
  SAME(enabled); SAME(arpEnabled); SAME(order); SAME(pattern); SAME(rate); SAME(rateMode); SAME(stepTimeUs); SAME(steps); SAME(stepSize); SAME(stepUnit); SAME(gate); SAME(swing);
  SAME(latch); SAME(velMode); SAME(velFixed); SAME(restart); SAME(repeats); SAME(quantize); SAME(scaleSource); SAME(scaleRoot); SAME(seed); SAME(chordEnabled); SAME(chordUnit);
#undef SAME
  for (int d = 0; d < 12; ++d) if (a.scaleDeg[d] == b.scaleDeg[d]) return "scaleDeg" + std::to_string(d);
  for (int i = 0; i < 6; ++i) {
    if (a.chordOn[i] == b.chordOn[i]) return "chordOn" + std::to_string(i + 1);
    if (a.chordShift[i] == b.chordShift[i]) return "chordShift" + std::to_string(i + 1);
  }
  return "";
}

struct LoadScene {
  const char* name;
  int at;                                    // the sample the preset lands on (mid-step)
  Settings A;
  std::optional<UserPattern> user;           // the user pattern in force before the load
  std::vector<InEv> ins;
  Latch bLatch;
  std::function<std::string(const Run&, int)> reached;   // run WITHOUT the load: "" when the named state really holds at `at`, else why not
};
std::vector<LoadScene> loadScenes() {
  std::vector<LoadScene> v;
  const std::vector<InEv> keys = {K(0, "a", 61), K(0, "b", 66), K(0, "c", 70)};
  auto withKeys = [&](std::vector<InEv> more) { std::vector<InEv> r = keys; r.insert(r.end(), more.begin(), more.end()); return r; };
  { // mid-note: gate 150 keeps a note sounding across the next step
    Settings a; a.enabled = true; a.gate = 150;
    v.push_back({"mid-note", 4400, a, std::nullopt, withKeys({O(7000, "b"), K(8200, "d", 64), O(11000, "a")}), Latch::Append,
                 [](const Run& r, int at) { for (const Span& s : spansOf(r)) if (s.on <= at && s.off > at) return std::string(); return std::string("no note is sounding at the load"); }});
  }
  { // mid-ratchet: swing 750 and a 4-way ratchet leave a sub-note of an earlier step pending past the next grid step (the sub-note at 5125 of the step at 4000)
    Settings a; a.enabled = true; a.gate = 100; a.swing = 750; a.pattern = Pattern::User;
    v.push_back({"mid-ratchet", 4400, a, userPat({B().ratchet(4).trig(2)}), withKeys({O(7000, "b"), K(8200, "d", 64), O(11000, "a")}), Latch::Append,
                 [](const Run& r, int at) {
                   const std::int64_t n0 = stepsBefore(r, at);
                   for (const Span& s : spansOf(r)) if (s.on > at && s.ord < n0) return std::string();
                   return std::string("no sub-note of an earlier step is pending at the load");
                 }});
  }
  { // during a tie: step 0 plays, steps 1 and 2 are ties, step 3 rests: the note of step 4 (4000) is held to ~7000
    Settings a; a.enabled = true; a.gate = 100; a.pattern = Pattern::User;
    v.push_back({"during a tie", 5500, a, userPat({B(), B().tie(), B().tie(), B().on(false)}), withKeys({O(8000, "b"), K(9100, "d", 64)}), Latch::Append,
                 [](const Run& r, int at) { for (const Span& s : spansOf(r)) if (s.on <= at && s.off >= at + 500 && s.off - s.on >= 2500) return std::string(); return std::string("no tied note spans the load"); }});
  }
  { // while latched: the keys are released at 2000 and the latch keeps the arp playing; the preset turns the latch off, or to append
    Settings a; a.enabled = true; a.gate = 80; a.latch = Latch::Replace;
    const std::vector<InEv> ins = {K(0, "a", 61), K(0, "b", 66), K(0, "c", 70), O(2000, "a"), O(2000, "b"), O(2000, "c"), K(9000, "d", 64), O(10500, "d")};
    auto reached = [](const Run& r, int at) {
      int after = 0;
      for (const Span& s : spansOf(r)) after += s.on > at;
      return after >= 3 ? std::string() : std::string("the latched set is not playing at the load");
    };
    v.push_back({"while latched (preset turns the latch off)", 5500, a, std::nullopt, ins, Latch::Off, reached});
    v.push_back({"while latched (preset switches to append)", 5500, a, std::nullopt, ins, Latch::Append, reached});
  }
  { // while exhausted: repeats = 1 over 3 keys is 3 steps (0, 1000, 2000) and then silence with the keys still down
    Settings a; a.enabled = true; a.gate = 80; a.repeats = 1;
    v.push_back({"while exhausted", 7400, a, std::nullopt, withKeys({O(9500, "b"), K(10200, "d", 64), O(12500, "a")}), Latch::Append,
                 [](const Run& r, int at) {
                   int early = 0, quiet = 0;
                   for (const Span& s : spansOf(r)) { early += s.on < 3500; quiet += s.on >= 3500 && s.on < at + 2200; }
                   return early >= 3 && quiet == 0 ? std::string() : std::string("the arp is not spent at the load (early " + std::to_string(early) + ", later " + std::to_string(quiet) + ")");
                 }});
  }
  return v;
}
struct LoadVariant { const char* name; bool flipEnabled, flipArp; };
constexpr std::array<LoadVariant, 4> kVariants = {{{"all but enabled / arp_enabled", false, false}, {"also arp_enabled off", false, true}, {"also enabled off", true, false}, {"every setting incl. enabled and arp_enabled", true, true}}};

Session loadSession(const LoadScene& sc, const Settings& snap, bool withLoad, const Plant& pl) {
  Session s;
  s.total = 14000;
  s.plants = pl;
  s.base = sc.A;
  s.scale = kCMajorScale;
  s.userPattern = sc.user;
  s.ins = sc.ins;
  if (withLoad) {
    s.cfg = {C(sc.at, [snap](Settings& x) { x = snap; })};
    s.userEdits = {{sc.at, kPresetUser}};
  }
  return s;
}

void testPresetLoad() {
  std::puts("Bar row: a preset load is a terminal path (every setting + a new user pattern in one snapshot, mid-state)");
  const std::vector<LoadScene> scenes = loadScenes();
  // (a) the snapshot really changes every setting, and each scene really is in its named state when the load lands
  std::string notAll, notReached;
  for (const LoadScene& sc : scenes) {
    const Settings all = scramble(sc.A, true, true, sc.bLatch);
    if (const std::string u = unchanged(sc.A, all); !u.empty() && notAll.empty()) notAll = std::string(sc.name) + ": " + u;
    const Run base = run(loadSession(sc, all, false, {}), 64);
    if (const std::string w = sc.reached(base, sc.at); !w.empty() && notReached.empty()) notReached = std::string(sc.name) + ": " + w;
  }
  {   // the proof is itself shown to bite: a snapshot that leaves one setting alone is named
    Settings partial = scramble(scenes[0].A, true, true, scenes[0].bLatch);
    partial.chordShift[3] = scenes[0].A.chordShift[3];
    plant(unchanged(scenes[0].A, partial) == "chordShift4", "the every-setting-changes proof names a setting the snapshot left alone: " + unchanged(scenes[0].A, partial));
  }
  check(notAll.empty(), "the snapshot changes every one of the 47 settings (enabled, arp_enabled, order, pattern, rate, rate_mode, step_time_us, steps, step_size, step_unit, gate, swing, latch, vel_mode, vel_fixed, restart, repeats, quantize, scale_source, scale_root, 12 degrees, seed, chord_enabled, chord_unit, 6 x on + shift)" + (notAll.empty() ? "" : " - UNCHANGED " + notAll));
  check(notReached.empty(), "each scene is really in its state when the load lands: a note sounding, a ratchet sub-note pending, a tie held, a latched set playing with no key down, the repeats spent" + (notReached.empty() ? "" : " - " + notReached));

  // (b) the plants: a dropped or doubled release, and an allocating settings change, must each read red on EVERY load session
  struct Sess { const LoadScene* sc; const LoadVariant* v; Settings snap; };
  std::vector<Sess> all;
  for (const LoadScene& sc : scenes)
    for (const LoadVariant& v : kVariants) all.push_back({&sc, &v, scramble(sc.A, v.flipEnabled, v.flipArp, sc.bLatch)});
  int redDrop = 0, redDouble = 0, redAlloc = 0, nAlloc = 0;
  std::string firstDrop, firstDouble;
  for (const Sess& x : all) {
    const auto d = n1(run(loadSession(*x.sc, x.snap, true, {"dropRelease"}), 64)), db = n1(run(loadSession(*x.sc, x.snap, true, {"doubleRelease"}), 64));
    redDrop += !d.empty(); redDouble += !db.empty();
    if (firstDrop.empty() && !d.empty()) firstDrop = d[0];
    if (firstDouble.empty() && !db.empty()) firstDouble = db[0];
    if (x.v->flipEnabled || x.v->flipArp) continue;   // adopt() is where a changed discrete setting is taken, and it runs only while the arp does: with the module or the arp switched off by the snapshot there is no such step
    Session s = loadSession(*x.sc, x.snap, true, {"allocInSettings"});
    s.countAllocs = true;
    redAlloc += run(s, 64).allocs > 0;
    ++nAlloc;
  }
  const int n = static_cast<int>(all.size());
  plant(redDrop == n, "N1 detects a dropped release on every one of the " + std::to_string(n) + " preset-load sessions: " + firstDrop);
  plant(redDouble == n, "N1 detects a doubled release on every one of the " + std::to_string(n) + " preset-load sessions: " + firstDouble);
  plant(redAlloc == nAlloc && nAlloc > 0, "A1 detects a settings change that allocates on every one of the " + std::to_string(nAlloc) + " preset-load sessions whose snapshot leaves the arp running (" + std::to_string(redAlloc) + " red)");

  // G1 over the same sessions must also be able to go red: offsets clamped into the block, or a position accumulated in float32
  for (const char* pn : {"clampOffset", "floatPosition"}) {
    int redG1 = 0;
    std::string firstG1;
    for (const Sess& x : all) {
      const Session s = loadSession(*x.sc, x.snap, true, {pn});
      std::string ref;
      for (int block : {4096, 333, 64, 7, 1}) {
        const std::string st = streamOf(run(s, block));
        if (ref.empty()) ref = st; else if (st != ref) { ++redG1; if (firstG1.empty()) firstG1 = std::string(x.sc->name) + " block " + std::to_string(block); break; }
      }
    }
    plant(redG1 > 0, std::string("G1 detects ") + (pn[0] == 'c' ? "offsets clamped into the block" : "a position accumulated in float32") + " on the preset-load sessions (" + std::to_string(redG1) + " of " + std::to_string(all.size()) + " red; first: " + firstG1 + ")");
  }

  // (c) the real thing: N1, no lost off, no allocation in process(), the load really acted, G1 across block sizes
  int n1bad = 0, g1bad = 0, lost = 0, inert = 0;
  std::int64_t allocs = 0, events = 0;
  std::string first;
  for (const Sess& x : all) {
    const std::string tag = std::string(x.sc->name) + " / " + x.v->name;
    Session s = loadSession(*x.sc, x.snap, true, {});
    s.countAllocs = true;
    std::string ref;
    bool g1 = false;
    for (int block : {4096, 333, 64, 7, 1}) {
      const Run r = run(s, block);
      events += static_cast<std::int64_t>(r.evs.size());
      allocs += r.allocs;
      lost += static_cast<int>(r.core->counters().offsLost);
      if (const auto b = n1(r); !b.empty()) { ++n1bad; if (first.empty()) first = tag + " block " + std::to_string(block) + ": " + b[0]; }
      const std::string st = streamOf(r);
      if (ref.empty()) ref = st; else if (st != ref && !g1) { g1 = true; ++g1bad; if (first.empty()) first = tag + " G1 differs at block " + std::to_string(block); }
    }
    inert += streamOf(run(s, 64)) == streamOf(run(loadSession(*x.sc, x.snap, false, {}), 64));
  }
  check(n1bad == 0 && g1bad == 0 && lost == 0 && allocs == 0 && inert == 0 && events > 1000,
        "N1 / G1 / A1: " + std::to_string(n) + " preset-load sessions (" + std::to_string(scenes.size()) + " states x " + std::to_string(kVariants.size()) + " snapshots; mid-note, mid-ratchet, during a tie, while latched twice, while exhausted) at block sizes 4096/333/64/7/1: N1 violations " + std::to_string(n1bad) +
        ", G1 divergences " + std::to_string(g1bad) + ", lost offs " + std::to_string(lost) + ", allocations inside process() " + std::to_string(allocs) + ", loads that changed nothing " + std::to_string(inert) + " (" + std::to_string(events) + " events)" + (first.empty() ? "" : " - " + first));
}

// ====================================================================== 4. the pattern-bank selection (and the order style) as a morph target
// horde RESOLVES a morph and hands Arp integer snapshots (SPEC 9); the harness model below is the same rule as test_m2b.cpp's MorphModel (pinned there by its
// own known answers): a discrete setting takes the leading corner, which must lead by MORE than 0.05 to take over; ties go to the lower corner.
struct MorphModel {
  Settings A, B;
  int cur = 0;
  Settings at(int wB) {
    const int wA = 1000 - wB;
    const int lead = wB > wA ? 1 : 0;
    const int leadW = lead ? wB : wA, curW = cur ? wB : wA;
    if (lead != cur && leadW - curW > 50) cur = lead;
    Settings o = cur ? B : A;
    auto blend = [&](int a, int b) { return (a * wA + b * wB + 500) / 1000; };
    o.gate = blend(A.gate, B.gate);
    o.swing = blend(A.swing, B.swing);
    o.velFixed = blend(A.velFixed, B.velFixed);
    o.stepTimeUs = blend(A.stepTimeUs, B.stepTimeUs);
    return o;
  }
};
const std::array<int, 24> kWeights = {0, 200, 400, 520, 526, 510, 490, 476, 475, 474, 500, 525, 526, 700, 1000, 800, 600, 500, 474, 300, 0, 600, 1000, 0};
constexpr int kMorphPeriod = 1500;

void morphInputs(Session& s) {
  s.ins = {K(0, "a", 61), K(0, "b", 66), K(0, "c", 70), O(9000, "b"), Ped(12000, true), K(13000, "d", 64), O(14500, "d"), Ped(16000, false),
           O(20000, "a"), K(21000, "g", 58), K(24000, "h", 72), O(26000, "h"), Pan(30500), K(31000, "e", 62), K(31000, "f", 67), O(35000, "e")};
}
// The user pattern every morph session carries, so `pattern = user` has something with ratchets, a tie, a rest and a trigger to play.
const UserPattern kMorphUser = userPat({B().ratchet(2).trig(1), B(), B().tie(), B().on(false), B().ratchet(4).step(1), B()}, 12);

Session morphSession(const Settings& A, const Settings& Bc, int* switches) {
  Session s;
  s.total = static_cast<int>(kWeights.size() + 1) * kMorphPeriod;
  s.scale = kCMajorScale;
  s.base = A;
  s.base.enabled = true;
  s.userPattern = kMorphUser;
  MorphModel m{A, Bc};
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
Settings morphBase() {
  Settings a;
  a.enabled = true; a.gate = 90; a.swing = 560; a.seed = 5;
  return a;
}
// A chain: every pattern value, then every order, visited one after another by hard snapshots at off-grid samples (1500 = a step and a half).
Session chainSession(int* switches) {
  Session s;
  s.scale = kCMajorScale;
  s.base = morphBase();
  s.userPattern = kMorphUser;
  int at = kMorphPeriod, sw = 0;
  for (int p = 1; p <= 11; ++p, at += kMorphPeriod, ++sw) s.cfg.push_back(C(at, [p](Settings& x) { x.pattern = static_cast<Pattern>(p); }));
  for (int o = 1; o < 16; ++o, at += kMorphPeriod, ++sw) s.cfg.push_back(C(at, [o](Settings& x) { x.order = static_cast<Order>(o); }));
  s.cfg.push_back(C(at, [](Settings& x) { x.pattern = Pattern::None; x.order = Order::Up; }));
  at += kMorphPeriod;
  s.total = at + kMorphPeriod;
  s.ins = {K(0, "a", 61), K(0, "b", 66), K(0, "c", 70), K(0, "d", 74)};
  if (switches) *switches = sw + 1;
  return s;
}

void testBankMorph() {
  std::puts("Bar row: the pattern-bank selection (and the order style) is a morph target (N1, G1, and where each switch lands)");
  // (a) N1 / G1: one morph session per destination value, plus a chain through all of them
  int sessions = 0, n1bad = 0, g1bad = 0, lost = 0, minSwitches = 99;
  std::int64_t events = 0;
  std::string first;
  auto judge = [&](const Session& s, const std::string& name, int sw) {
    ++sessions;
    minSwitches = std::min(minSwitches, sw);
    std::string ref;
    bool g1 = false, bad = false;
    for (int block : {4096, 333, 64, 1}) {
      const Run r = run(s, block);
      events += static_cast<std::int64_t>(r.evs.size());
      if (const auto v = n1(r); !v.empty() && !bad) { bad = true; ++n1bad; if (first.empty()) first = name + " block " + std::to_string(block) + ": " + v[0]; }
      lost += static_cast<int>(r.core->counters().offsLost);
      const std::string st = streamOf(r);
      if (ref.empty()) ref = st; else if (st != ref && !g1) { g1 = true; ++g1bad; if (first.empty()) first = name + " G1 differs at block " + std::to_string(block); }
    }
  };
  { // the detectors first, on the chain session: a dropped release reads red for N1, offsets clamped into the block read red for G1
    int sw = 0;
    Session s = chainSession(&sw);
    s.plants = {"dropRelease"};
    const auto d = n1(run(s, 64));
    plant(!d.empty(), "N1 detects a dropped release in the bank-morph chain session: " + (d.empty() ? std::string() : d[0]));
    s.plants = {"clampOffset"};
    std::string ref, diff;
    for (int block : {4096, 333, 64, 1}) {
      const std::string st = streamOf(run(s, block));
      if (ref.empty()) ref = st; else if (st != ref && diff.empty()) diff = "block " + std::to_string(block) + " differs";
    }
    plant(!diff.empty(), "G1 detects offsets clamped into the block in the bank-morph chain session: " + diff);
  }
  for (int p = 0; p < 12; ++p) {   // 0..10 the bank (none, offbeat ... trigger_pulse), 11 user
    Settings A = morphBase(), Bc = A;
    A.pattern = p == 0 ? Pattern::Gallop : Pattern::None;
    Bc.pattern = static_cast<Pattern>(p);
    int sw = 0;
    const Session s = morphSession(A, Bc, &sw);
    judge(s, std::string("pattern ") + kPatternNames[p], sw);
  }
  for (int o = 0; o < 16; ++o) {
    Settings A = morphBase(), Bc = A;
    A.order = o == 0 ? Order::Down : Order::Up;
    Bc.order = static_cast<Order>(o);
    int sw = 0;
    const Session s = morphSession(A, Bc, &sw);
    judge(s, std::string("order ") + kOrderNames[o], sw);
  }
  { int sw = 0; const Session s = chainSession(&sw); judge(s, "chain through every pattern and order", sw); }
  check(n1bad == 0 && g1bad == 0 && lost == 0 && minSwitches >= 5 && events > 5000,
        "N1 / G1: " + std::to_string(sessions) + " morph sessions (pattern across all 12 values - the 11 bank entries and user - alone, order across all 16 alone, and one chain through every value), corner switches >= " + std::to_string(minSwitches) + " each, at block sizes 4096/333/64/1: N1 violations " + std::to_string(n1bad) + ", G1 divergences " + std::to_string(g1bad) + ", lost offs " + std::to_string(lost) + " (" + std::to_string(events) + " events)" + (first.empty() ? "" : " - " + first));

  // (b) where each switch lands: a hard switch at 4400 (mid-step; steps are 1000 samples) takes effect at the first step evaluated after it, 5000. Three keys
  // held from 0, gate 80 (every note ends inside its own step), restart chord, so step kAbs is at sample 1000 * kAbs and k = kAbs.
  constexpr int kFlipAt = 4400, kFirstNew = 5000;
  auto landSession = [&](const Settings& A, const Settings& Bc, const Plant& pl, int gate = 80) {
    Session s;
    s.total = 9000; s.plants = pl; s.base = A; s.base.gate = gate; s.base.enabled = true; s.userPattern = kMorphUser;
    s.ins = {K(0, "a", 60), K(0, "b", 64), K(0, "c", 67), K(0, "d", 71)};
    s.cfg = {C(kFlipAt, [Bc, gate](Settings& x) { x = Bc; x.gate = gate; x.enabled = true; })};
    return s;
  };
  auto spanText = [](const Span& s) { return std::to_string(s.on) + ":" + std::to_string(s.key) + ":" + std::to_string(s.vel) + " off " + std::to_string(s.off); };
  // Notes of the steps evaluated before the switch (ord < n0) must be exactly what the unswitched session plays: same on, key, velocity, same off.
  auto earlyBad = [&](const Settings& A, const Settings& Bc, const Plant& pl, int gate = 80) {
    const Run base = run(landSession(A, A, {}, gate), 512), flip = run(landSession(A, Bc, pl, gate), 512);
    const std::int64_t n0 = stepsBefore(base, kFlipAt);
    auto early = [&](const Run& r) { std::vector<std::string> v; for (const Span& s : spansOf(r)) if (s.ord < n0 && s.ord >= 0) v.push_back(spanText(s)); return v; };
    const auto e0 = early(base), e1 = early(flip);
    if (e0.size() < 3) return std::string("too few early notes");
    for (std::size_t i = 0; i < std::min(e0.size(), e1.size()); ++i) if (e0[i] != e1[i]) return "note " + e0[i] + " became " + e1[i];
    return e0.size() == e1.size() ? std::string() : "note count " + std::to_string(e0.size()) + " became " + std::to_string(e1.size());
  };
  { // the plant, on a bank switch and an order switch
    Settings A = morphBase(), Bc = A;
    Bc.pattern = Pattern::Gallop;
    const std::string cp = earlyBad(A, Bc, {"morphSwitchCuts"}, 150);   // gate 150: the note of the step at 4000 still sounds when the switch lands at 5000
    Settings A2 = morphBase(), B2 = A2;
    B2.order = Order::Down;
    const std::string co = earlyBad(A2, B2, {"morphSwitchCuts"}, 150);
    const bool cleanOk = earlyBad(A, Bc, {}, 150).empty() && earlyBad(A2, B2, {}, 150).empty();   // the same two switches, unplanted, read green
    plant(!cp.empty() && !co.empty() && cleanOk, "the landing check detects a switch that cuts the notes of already-evaluated steps (pattern: " + cp + "; order: " + co + ")");
  }
  // pattern: after the switch, step kAbs plays iff the pattern IN FORCE at that step says so (the old pattern before 5000, the new one from 5000 on)
  auto patternLanding = [&](int p) -> std::string {
    Settings A = morphBase(), Bc = A;
    A.pattern = p == 0 ? Pattern::Gallop : Pattern::None;
    Bc.pattern = static_cast<Pattern>(p);
    if (const std::string e = earlyBad(A, Bc, {}); !e.empty()) return "early notes: " + e;
    const Run r = run(landSession(A, Bc, {}), 512);
    int checkedNew = 0;
    for (const StepRec& st : r.steps) {
      const Pattern inForce = st.a >= kFirstNew ? Bc.pattern : A.pattern;
      const PatternStep* steps = inForce == Pattern::User ? kMorphUser.steps : kBank[static_cast<int>(inForce)].steps;
      const int len = inForce == Pattern::User ? kMorphUser.length : kBank[static_cast<int>(inForce)].length;
      const PatternStep& ps = steps[st.s.k % len];
      const bool mayPlay = ps.on && !ps.tie && ps.prob > 0, mustPlay = mayPlay && ps.prob == 100;   // prob in 1..99 depends on the seeded draw: only "may"
      if ((mustPlay && !st.s.played) || (!mayPlay && st.s.played)) return "step " + std::to_string(st.s.kAbs) + " at sample " + std::to_string(st.a) + (st.s.played ? " played" : " did not play") + " under " + kPatternNames[static_cast<int>(inForce)];
      checkedNew += st.a >= kFirstNew;
    }
    return checkedNew >= 4 ? "" : "too few steps after the switch";
  };
  std::string patBad;
  for (int p = 0; p < 12 && patBad.empty(); ++p) if (const std::string b = patternLanding(p); !b.empty()) patBad = std::string(kPatternNames[p]) + ": " + b;
  check(patBad.empty(), "known answer (SPEC 9): a `pattern` switch at 4400 (mid-step) lands at the next evaluated step, 5000, for each of the 12 values (the 11 bank entries and user): earlier notes untouched, every later step plays as the new pattern's step data says (on / tie / prob)" + (patBad.empty() ? "" : " - " + patBad));
  // order: from the first new step on, the notes are those of a session that had the new order from the start (m carries over: every step so far played)
  auto orderLanding = [&](int o, bool* differs) -> std::string {
    Settings A = morphBase(), Bc = A;
    A.order = o == 0 ? Order::Down : Order::Up;
    Bc.order = static_cast<Order>(o);
    if (const std::string e = earlyBad(A, Bc, {}); !e.empty()) return "early notes: " + e;
    const Run flip = run(landSession(A, Bc, {}), 512), fromStart = run(landSession(Bc, Bc, {}), 512), unswitched = run(landSession(A, A, {}), 512);
    auto late = [&](const Run& r) { std::vector<std::string> v; for (const Span& s : spansOf(r)) if (s.on >= kFirstNew) v.push_back(spanText(s)); return v; };
    const auto f = late(flip), s = late(fromStart);
    *differs = f != late(unswitched);
    if (f.size() < 4) return "too few notes after the switch";
    for (std::size_t i = 0; i < std::min(f.size(), s.size()); ++i) if (f[i] != s[i]) return "note " + f[i] + " where a session that began on this order plays " + s[i];
    return f.size() == s.size() ? "" : "note count " + std::to_string(f.size()) + " against " + std::to_string(s.size());
  };
  std::string ordBad;
  int differing = 0;
  for (int o = 0; o < 16 && ordBad.empty(); ++o) {
    bool d = false;
    if (const std::string b = orderLanding(o, &d); !b.empty()) ordBad = std::string(kOrderNames[o]) + ": " + b;
    differing += d;
  }
  check(ordBad.empty() && differing >= 12, "known answer (SPEC 9): an `order` switch at 4400 lands at the next evaluated step, 5000, for each of the 16 values: earlier notes untouched, and from 5000 on exactly what a session that began on that order plays (m carries over); " + std::to_string(differing) + " of 16 switches change the notes (the check is not vacuous)" + (ordBad.empty() ? "" : " - " + ordBad));
}

}  // namespace

void runM3Tests() {
  testMeters();
  testNoInputNoOutput();
  testPresetLoad();
  testBankMorph();
}

}  // namespace arp_test
