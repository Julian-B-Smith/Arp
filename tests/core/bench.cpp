// bench.cpp - the Release CPU and memory benchmark of the SHIPPED core (ROADMAP T-007, M3a; horde's note-processor 1.0 bar rows
// "A measured CPU cost" and "A memory figure"). REPORTED, never gated: it does not run inside ./verify and nothing here can fail a build
// except an allocation inside process() / reset() (that breaks the real-time contract and exits 1).
//
// Method (the convention of horde's two-oscillator measurement, docs/research/2026-08-22-two-osc-cpu-measurement.md): Release (-O3, no
// -ffast-math, the core refuses it) on THIS machine, % of ONE core in real time, and the x4 derate as a min-spec heuristic. The x4 figure is
// a heuristic, NOT "min-spec verified": no min-spec hardware was measured.
//   * inputs are deterministic (a fixed key pool, a fixed press / release schedule, an exact host position); nothing reads a clock but the timer;
//   * the mean is the median over repetitions of a BATCH timing (one interval around many blocks, so the timer's resolution does not matter);
//     p50 / p99 / max come from a separate per-call pass (timer overhead, calibrated below, subtracted) and include OS preemption noise;
//   * every configuration is warmed up for one second of audio first; the spread is (max - min) / median over the repetitions.
//
// The memory figure reads the private tables of arp::Core by compiling THIS file's view of core.h with `private` made `public`
// (a measurement-only trick, safe in practice: access control does not change layout on the compilers we use). The library it links is
// the shipped one (no ARP_PLANTS), so the layout measured is the shipped layout.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "alloc_hook.h"
#include "stream_digest.h"
#include "arp/arith.h"
#include "arp/bank_data.h"
#include "arp/int128.h"
#include "arp/pattern.h"
#include "arp/types.h"
#define private public
#include "arp/core.h"
#undef private

#ifdef ARP_PLANTS
#error "the benchmark measures the shipped library: it must be built without ARP_PLANTS"
#endif

using namespace arp;
// `--cpu-time` swaps wall time for this thread's CPU time, so that other processes on a busy (shared) machine do not inflate a figure: for comparing two builds side by side. The per-call
// columns (p99, max) are only meaningful in wall time. The default, and every figure the docs quote, is wall time.
bool gCpuTime = false;
struct SteadyClock {
  using time_point = std::chrono::steady_clock::time_point;
  static time_point now() {
    if (!gCpuTime) return std::chrono::steady_clock::now();
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return time_point(std::chrono::nanoseconds(static_cast<std::int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec));
  }
};

namespace {

constexpr double kBudgetPct = 2.0;   // horde: the modulation, morph and intent bus slice, <= 2 % of a core
constexpr double kDerate = 4.0;      // horde's min-spec heuristic

// ---------------------------------------------------------------- the scenarios
struct Scenario {
  std::string name;
  Settings st;
  double bpm = 120;
  int keys = 16;           // held from the first block
  int keyBase = 36, keyStride = 3;
  bool churn = false;      // every quarter second: release 4 keys and press 4 new ones
  int channels = 1;        // held key i is pressed on MIDI channel i % channels (MPE-style when 16)
  int refsPerKey = 1;      // every held key is pressed by this many refs (distinct ids; the core keeps at most 4 per key)
  // Dense host input (D-032): notes that keep arriving on top of the held ones. The settings-only scenarios above have none, which is what hid the cost.
  int restrikeKeys = 0;           // re-strike the first N held keys (every ref: an off, then an on with a fresh id) ...
  double restrikePerStep = 0;     // ... this many times per 1/64 step at the scenario's tempo (0.25 = every fourth step) ...
  int restrikeGap = 0;            // ... with the on this many samples after the off (0: the same sample, the 1 -> 0 -> 1 case)
  int restrikeRefs = 0;           // ... re-striking this many refs of each such key (0: all of them; 1: one ref of a key held by several)
  double floodEventsPerSec = 0;   // presses on keys that are NOT held, as note events per second (a press and its release half a period later)
  std::string family;             // scenarios of one family differ only in the input rate: the summary derives the largest rate inside the slice from them
  bool lightDigest = false;       // the stream digest covers a few rates / block sizes for 1 s, not the whole matrix (heavy loads, added with D-032: the digest's cost grows with them)
  const char* noDigest = nullptr;  // not null: left out of the stream digest, and this is why (D-036: the digest file was cut from a core without the feature this scenario uses)
  std::optional<UserPattern> user;
  Scale scale{0, 0xAB5};   // horde's global scale: C major
};

Settings baseSettings() {
  Settings s;
  s.enabled = true;
  s.rate = Rate::R1_64;
  s.gate = 80;
  return s;
}
std::vector<Scenario> scenarios() {
  std::vector<Scenario> v;
  auto add = [&](const std::string& name, const std::function<void(Scenario&)>& f) { Scenario s; s.name = name; s.st = baseSettings(); f(s); v.push_back(std::move(s)); };
  add("idle: no keys held, 1/16 @120", [](Scenario& s) { s.keys = 0; s.st.rate = Rate::R1_16; });
  add("typical: 3 keys, up, 1/16 @120", [](Scenario& s) { s.keys = 3; s.st.rate = Rate::R1_16; });
  add("1/64 @999, 16 keys, up", [](Scenario& s) { s.bpm = 999; });
  add("1/64 @120, 16 keys, up", [](Scenario& s) { s.bpm = 120; });
  for (int o = 1; o < 16; ++o) add(std::string("1/64 @999, 16 keys, order ") + kOrderNames[o], [o](Scenario& s) { s.bpm = 999; s.st.order = static_cast<Order>(o); });
  add("ratchet 4 on every step, 1/64 @999, 16 keys", [](Scenario& s) {
    s.bpm = 999; s.st.pattern = Pattern::User;
    UserPattern u; u.length = 1; u.steps[0].ratchet = 4; s.user = u;
  });
  add("ties (tied_legato), 1/64 @999, 16 keys", [](Scenario& s) { s.bpm = 999; s.st.pattern = Pattern::TiedLegato; });
  add("probability (sparse_chance), 1/64 @999, 16 keys", [](Scenario& s) { s.bpm = 999; s.st.pattern = Pattern::SparseChance; });
  add("triggers on lanes 1-8 every step, 1/64 @999, 16 keys", [](Scenario& s) {
    s.bpm = 999; s.st.pattern = Pattern::User;
    UserPattern u; u.length = 8; for (int i = 0; i < 8; ++i) u.steps[i].trig = i + 1; s.user = u;
  });
  add("transposition: 8 steps of 7 semitones, 1/64 @999, 16 keys", [](Scenario& s) { s.bpm = 999; s.st.steps = 8; s.st.stepSize = 7; });
  add("quantize (own scale) + degrees: 4 steps of 2 degrees, 1/64 @999, 16 keys", [](Scenario& s) {
    s.bpm = 999; s.st.quantize = Quantize::Scale; s.st.scaleSource = ScaleSource::Own; s.st.stepUnit = StepUnit::Degrees; s.st.steps = 4; s.st.stepSize = 2;
  });
  add("swing 750, 1/64 @999, 16 keys", [](Scenario& s) { s.bpm = 999; s.st.swing = 750; });
  add("chord stage with 6 shifts, 1/64 @999, 16 keys", [](Scenario& s) {
    s.bpm = 999; s.st.chordEnabled = true;
    for (int i = 0; i < 6; ++i) { s.st.chordOn[i] = true; s.st.chordShift[i] = 4 + 3 * i; }
  });
  add("time mode at the minimum step time (10 ms), 16 keys", [](Scenario& s) { s.st.rateMode = RateMode::Time; s.st.stepTimeUs = 10000; });
  add("chord stage + key churn (4 keys per 0.25 s), 1/16 @120, 16 keys", [](Scenario& s) {
    s.st.rate = Rate::R1_16; s.st.chordEnabled = true; s.churn = true;
    for (int i = 0; i < 6; ++i) { s.st.chordOn[i] = true; s.st.chordShift[i] = 4 + 3 * i; }
  });
  add("heavy but playable: order chord, chord stage with 6 shifts, 16 keys, 1/16 @120", [](Scenario& s) {
    s.st.rate = Rate::R1_16; s.st.order = Order::Chord; s.st.chordEnabled = true;
    for (int i = 0; i < 6; ++i) { s.st.chordOn[i] = true; s.st.chordShift[i] = 4 + 3 * i; }
  });
  add("WORST CASE for events per block: order chord over 112 distinct keys (16 keys x 6 chord shifts), ratchet 4, gate 200, 1/64 @999", [](Scenario& s) {
    s.bpm = 999; s.keyBase = 14; s.keyStride = 7;   // 14, 21 ... 119 each +0..+6: 112 contiguous keys 14..125, none shared, none quantized away
    s.st.order = Order::Chord; s.st.pattern = Pattern::User; s.st.gate = 200;
    s.st.chordEnabled = true;
    for (int i = 0; i < 6; ++i) { s.st.chordOn[i] = true; s.st.chordShift[i] = i + 1; }
    UserPattern u; u.length = 1; u.steps[0].ratchet = 4; s.user = u;
  });
  // M3b rework: two loads the critic found heavier than the "worst case" above, for different reasons (the table, not the event rate).
  // A: 32 held keys on 16 channels (MPE-style) through a 3-shift chord stage = 128 distinct keys, 8 transposition steps: more notes than the 128-entry
  //    arp table holds, so every new on evicts the oldest (T11), about 16,500 evictions a second.
  // B: the same on one channel without transposition: nothing is evicted, every on retriggers a still-sounding note (T2).
  auto loadAB = [](Scenario& s) {
    s.bpm = 999; s.keys = 32; s.keyBase = 0; s.keyStride = 4;
    s.st.order = Order::Chord; s.st.pattern = Pattern::User; s.st.gate = 200;
    s.st.chordEnabled = true;
    for (int i = 0; i < 6; ++i) { s.st.chordOn[i] = i < 3; s.st.chordShift[i] = i < 3 ? i + 1 : 0; }
    UserPattern u; u.length = 1; u.steps[0].ratchet = 1; u.steps[0].gate = 200; s.user = u;
  };
  auto loadA = [loadAB](Scenario& s) { loadAB(s); s.channels = 16; s.st.steps = 8; s.st.stepSize = 1; };
  add("WORST CASE A: 32 keys on 16 channels, chord stage +1/+2/+3 (128 notes), 8 transposition steps, order chord, gate 200, 1/64 @999 (every on evicts, T11)", loadA);
  add("WORST CASE B: 32 keys on one channel, chord stage +1/+2/+3 (128 notes), order chord, gate 200, 1/64 @999 (every on retriggers, T2)", [loadAB](Scenario& s) {
    loadAB(s);
  });
  // D-032 / M-1: the heaviest load built from settings alone that the critic found: B with ratchet 4, a trigger, quantize (own scale, ALL 12 degrees on: chromatic) and
  // the chord stage in degrees. A chromatic scale snaps nothing, so the 128 chord keys stay 128 distinct keys; a major scale (7 of 12) folds them onto ~75 and measured
  // 0.9-1.0 % where this measures 1.6-1.7 %. B+ has the +1/+2/+3 chord stage of A and B; B+6 has six shifts; B++ adds 4 transposition steps of 2 semitones.
  auto loadBHeavy = [loadAB](Scenario& s) {
    loadAB(s);
    s.user->steps[0].ratchet = 4; s.user->steps[0].trig = 1;
    s.st.quantize = Quantize::Scale; s.st.scaleSource = ScaleSource::Own; s.st.chordUnit = StepUnit::Degrees;
    for (bool& d : s.st.scaleDeg) d = true;
  };
  add("WORST CASE B+: B with ratchet 4, a trigger, quantize (own scale, all 12 degrees: chromatic), chord stage in degrees, 1/64 @999", [loadBHeavy](Scenario& s) { loadBHeavy(s); s.lightDigest = true; });
  add("WORST CASE B+6: B+ with six chord shifts +1..+6", [loadBHeavy](Scenario& s) {
    loadBHeavy(s); s.lightDigest = true;
    for (int i = 0; i < 6; ++i) { s.st.chordOn[i] = true; s.st.chordShift[i] = i + 1; }
  });
  add("WORST CASE B++: B+ with 4 transposition steps of 2 semitones", [loadBHeavy](Scenario& s) { loadBHeavy(s); s.st.steps = 4; s.st.stepSize = 2; s.lightDigest = true; });
  // D-036: note randomness at its most expensive, 100 % on all three settings: every sub-note of every note draws twice (velocity and length) and blends two
  // integer curves. These two have NO entry in tests/core/stream_digest_bench.txt (cut from 0ef0903, which has no randomness, and not regenerated): `noDigest`; they have
  // their own, tests/core/stream_digest_random_bench.txt (`arp_bench --digest-random`). The cores before D-036 have no such settings: ARP_NO_RANDOM (tools/core_digest.sh) leaves them out.
#ifndef ARP_NO_RANDOM
  const char* const kNoDigestWhy = "D-036: the committed bench digest was cut from 0ef0903, which has no note randomness";
  add("WORST CASE B++ with randomness: B++, vel_random = gate_random = random_curve = 100", [loadBHeavy, kNoDigestWhy](Scenario& s) {
    loadBHeavy(s); s.st.steps = 4; s.st.stepSize = 2; s.lightDigest = true;
    s.st.velRandom = 100; s.st.gateRandom = 100; s.st.randomCurve = 100; s.noDigest = kNoDigestWhy;
  });
  add("WORST CASE for events per block with randomness: the 112-key ratchet 4 chord, vel_random = gate_random = random_curve = 100", [kNoDigestWhy](Scenario& s) {
    s.bpm = 999; s.keyBase = 14; s.keyStride = 7;
    s.st.order = Order::Chord; s.st.pattern = Pattern::User; s.st.gate = 200;
    s.st.chordEnabled = true;
    for (int i = 0; i < 6; ++i) { s.st.chordOn[i] = true; s.st.chordShift[i] = i + 1; }
    UserPattern u; u.length = 1; u.steps[0].ratchet = 4; s.user = u;
    s.st.velRandom = 100; s.st.gateRandom = 100; s.st.randomCurve = 100; s.noDigest = kNoDigestWhy;
  });
  add("WORST CASE A at ratchet 4 with randomness: A (32 keys on 16 channels, 128 notes, 8 transposition steps, every on evicts) at ratchet 4, vel_random = gate_random = random_curve = 100", [loadA, kNoDigestWhy](Scenario& s) {
    loadA(s); s.user->steps[0].ratchet = 4; s.lightDigest = true;
    s.st.velRandom = 100; s.st.gateRandom = 100; s.st.randomCurve = 100; s.noDigest = kNoDigestWhy;
  });
#endif

  // D-032: dense incoming notes. Each family holds one load and varies only the input rate (rate 0 is the load alone); the bench summary derives from
  // a family the largest rate that stays inside the slice. The loads are A at ratchet 4 (the critic's: the heaviest table, every step ratcheted, so the load
  // alone is 1.7-1.9 % after x4 and leaves little room) and A at ratchet 1 (D-031's A: about 1.1 %), which shows how the room grows when the rest is lighter.
  auto rateName = [](double r) { char b[32]; std::snprintf(b, sizeof b, "%g", r); return std::string(b); };
  auto families = [&](const std::string& load, const std::function<void(Scenario&)>& loadFn, const std::vector<double>& restrike32, const std::vector<double>& restrike4, const std::vector<double>& flood) {
    auto loadA_r = loadFn;
    // the realistic shape: a held chord that the host keeps re-striking (all 32 keys on the same sample as the off: the 1 -> 0 -> 1 case)
    for (double per : restrike32) {
      Scenario sc; sc.st = baseSettings(); sc.family = "INPUT re-struck chord (32 keys, off and on in one sample), " + load;
      sc.name = "INPUT re-struck chord: " + load + ", all 32 keys re-struck " + rateName(per) + "x per 1/64 step (off and on in one sample)";
      loadA_r(sc); sc.restrikeKeys = 32; sc.restrikePerStep = per; sc.restrikeGap = 0; sc.lightDigest = true;
      v.push_back(std::move(sc));
    }
    // a few keys re-struck often, the on one sample after the off (the held set changes at two samples)
    for (double per : restrike4) {
      Scenario sc; sc.st = baseSettings(); sc.family = "INPUT 4 keys re-struck (on one sample after the off), " + load;
      sc.name = "INPUT 4 keys re-struck: " + load + ", 4 of the 32 keys re-struck " + rateName(per) + "x per 1/64 step (on one sample after the off)";
      loadA_r(sc); sc.restrikeKeys = 4; sc.restrikePerStep = per; sc.restrikeGap = 1; sc.lightDigest = true;
      v.push_back(std::move(sc));
    }
    // the pathological shape: 32 keys x 4 refs held (the table is full), a flood of presses on keys that are not held (each is refused), the chord stage off and on
    for (bool chord : {false, true})
      for (double rate : flood) {
        Scenario sc; sc.st = baseSettings(); sc.family = std::string("INPUT new-key flood, chord stage ") + (chord ? "on" : "off") + ", " + load;
        sc.name = "INPUT new-key flood: " + load + ", 32 keys x 4 refs, presses on new keys at " + rateName(rate) + " note events/s, chord stage " + (chord ? "on" : "off");
        loadA_r(sc); sc.refsPerKey = 4; sc.floodEventsPerSec = rate; sc.st.chordEnabled = chord; sc.lightDigest = true;
        v.push_back(std::move(sc));
      }
  };
  auto loadAr = [loadA](int ratchet) { return [loadA, ratchet](Scenario& s) { loadA(s); s.user->steps[0].ratchet = ratchet; }; };
  families("A at ratchet 4", loadAr(4), {0.0, 0.125, 0.25, 0.5, 1.0, 2.0}, {0.0, 1.0, 2.0, 4.0, 8.0}, {0.0, 1000.0, 2000.0, 4000.0, 8000.0, 17000.0});
  families("A at ratchet 1", loadAr(1), {0.0, 1.0}, {0.0, 4.0}, {0.0, 8000.0});
  // L-1: one ref of a key held by four refs, with six chord shifts: the key stays held, so every re-strike is a held-set change that is no join or leave
  for (double per : {0.0, 1.0, 4.0, 8.0}) {
    Scenario sc; sc.st = baseSettings(); sc.family = "INPUT 1 of 4 refs of 4 keys re-struck, six chord shifts, A at ratchet 4";
    sc.name = "INPUT 1 of 4 refs re-struck: A at ratchet 4 with six chord shifts +1..+6, 32 keys x 4 refs, one ref of 4 keys re-struck " + rateName(per) + "x per 1/64 step (on one sample after the off)";
    loadAr(4)(sc);
    for (int i = 0; i < 6; ++i) { sc.st.chordOn[i] = true; sc.st.chordShift[i] = i + 1; }
    sc.refsPerKey = 4; sc.restrikeKeys = 4; sc.restrikeRefs = 1; sc.restrikePerStep = per; sc.restrikeGap = 1; sc.lightDigest = true;
    v.push_back(std::move(sc));
  }
  // M-1: the room on the heaviest settings-only load (B+, chromatic)
  families("B+ chromatic", loadBHeavy, {0.0, 1.0}, {0.0, 1.0, 2.0, 4.0}, {0.0, 4000.0, 17000.0});
  // ... and on B++, the heaviest of all (B+ with 4 transposition steps): the load the quoted limit has to hold over
  families("B++ chromatic transposed", [loadBHeavy](Scenario& s) { loadBHeavy(s); s.st.steps = 4; s.st.stepSize = 2; }, {0.0, 1.0}, {0.0, 1.0, 2.0}, {0.0, 17000.0});
#ifndef ARP_NO_RANDOM
  // D-036: the same heaviest load with all three randomness settings at 100: the input room that is left (docs/horde-copy-in.md). Not in the main digest (kNoDigestWhy); in the randomness one.
  {
    const std::size_t from = v.size();
    families("B++ chromatic transposed + randomness 100", [loadBHeavy](Scenario& s) {
      loadBHeavy(s); s.st.steps = 4; s.st.stepSize = 2;
      s.st.velRandom = 100; s.st.gateRandom = 100; s.st.randomCurve = 100;
    }, {0.0, 1.0}, {0.0, 1.0, 2.0, 4.0}, {0.0, 17000.0});
    for (std::size_t k = from; k < v.size(); ++k) v[k].noDigest = kNoDigestWhy;
  }
#endif
  return v;
}

// ---------------------------------------------------------------- deterministic input and the exact host position
struct Feeder {
  const Scenario& sc;
  int sr;
  std::int64_t abs = 0;           // absolute sample of the next block
  std::int64_t nextId = 1;        // host note ids (nonzero: unique per press)
  std::vector<std::int64_t> idOf;      // the id of the press currently holding ref r of pool key i: idOf[i * refsPerKey + r]
  std::vector<int> freeKeys;           // the keys that are not held: what a flood of new-key presses cycles through
  std::vector<Event> evs;         // flat storage of the input events of the blocks filled so far
  std::vector<Block> blocks;
  std::vector<std::pair<std::size_t, std::size_t>> slice;
  Feeder(const Scenario& s, int rate) : sc(s), sr(rate), idOf(static_cast<std::size_t>(std::max(0, s.keys) * std::max(1, s.refsPerKey)), 0) {
    for (int k = 0; k < 128; ++k) {
      bool held = false;
      for (int i = 0; i < s.keys; ++i) held = held || keyOf(i) == k;
      if (!held) freeKeys.push_back(k);
    }
  }
  std::size_t slotOf(int i, int r) const { return static_cast<std::size_t>(i * std::max(1, sc.refsPerKey) + r); }

  static Event on(int key, int channel, std::int64_t id, int offset) {
    Event e; e.type = EventType::NoteOn; e.key = key; e.vel = 90 + (key % 30); e.ref.id = id; e.ref.channel = channel; e.offset = offset; return e;
  }
  static Event off(int channel, std::int64_t id, int offset) {
    Event e; e.type = EventType::NoteOff; e.ref.id = id; e.ref.channel = channel; e.offset = offset; return e;   // the same ref as its NoteOn (id and channel; the key stays 0 in both)
  }
  int channelOf(int i) const { return i % sc.channels; }
  int keyOf(int i) const { return sc.keyBase + sc.keyStride * i; }

  // Fill `count` consecutive blocks of `n` samples starting at the current position. Pointers into `evs` are taken only after it stops growing.
  void fill(int count, int n) {
    evs.clear();
    blocks.assign(static_cast<std::size_t>(count), Block{});
    slice.assign(static_cast<std::size_t>(count), {0, 0});
    const std::int64_t inc = std::llround(sc.bpm * 1000) * kTpq;
    const std::int64_t period = sr / 4;
    for (int b = 0; b < count; ++b) {
      const std::size_t first = evs.size();
      if (abs == 0)
        for (int i = 0; i < sc.keys; ++i)
          for (int r = 0; r < std::max(1, sc.refsPerKey); ++r) { idOf[slotOf(i, r)] = nextId; evs.push_back(on(keyOf(i), channelOf(i), nextId++, 0)); }
      if (sc.churn && sc.keys >= 4) {
        for (std::int64_t t = (abs / period + (abs % period ? 1 : 0)) * period; t < abs + n; t += period) {
          if (t == 0) continue;
          const std::int64_t j = t / period;
          for (int q = 0; q < 4; ++q) {
            const int i = static_cast<int>((4 * j + q) % sc.keys);
            evs.push_back(off(channelOf(i), idOf[slotOf(i, 0)], static_cast<int>(t - abs)));
          }
          for (int q = 0; q < 4; ++q) {
            const int i = static_cast<int>((4 * j + q) % sc.keys);
            idOf[slotOf(i, 0)] = nextId;
            evs.push_back(on(keyOf(i), channelOf(i), nextId++, static_cast<int>(t - abs)));
          }
        }
      }
      if (sc.restrikeKeys > 0 && sc.restrikePerStep > 0) {   // a 1/64 step is 60 ticks = 3.75 / bpm seconds
        const double restrikePeriod = sr * 3.75 / sc.bpm / sc.restrikePerStep;
        const int nKeys = std::min(sc.restrikeKeys, sc.keys), refs = std::max(1, sc.restrikeRefs > 0 ? std::min(sc.restrikeRefs, sc.refsPerKey) : sc.refsPerKey);
        // the id of the press of (key i, ref r) made by re-strike k is a pure function of the three, so a block boundary between an off and its on changes nothing
        auto pressId = [&](std::int64_t k, int i, int r) { return k == 0 ? idOf[slotOf(i, r)] : 2000000000 + (k * 64 + i) * 4 + r; };
        for (std::int64_t k = std::max<std::int64_t>(1, static_cast<std::int64_t>(static_cast<double>(abs) / restrikePeriod) - 2);; ++k) {
          const std::int64_t t = std::llround(static_cast<double>(k) * restrikePeriod);
          if (t >= abs + n) break;
          if (t >= abs)
            for (int i = 0; i < nKeys; ++i)
              for (int r = 0; r < refs; ++r) evs.push_back(off(channelOf(i), pressId(k - 1, i, r), static_cast<int>(t - abs)));
          const std::int64_t tOn = t + sc.restrikeGap;
          if (tOn >= abs && tOn < abs + n)   // (an on may land in the block after its off)
            for (int i = 0; i < nKeys; ++i)
              for (int r = 0; r < refs; ++r) evs.push_back(on(keyOf(i), channelOf(i), pressId(k, i, r), static_cast<int>(tOn - abs)));
        }
      }
      if (sc.floodEventsPerSec > 0) {
        const double floodPeriod = 2.0 * sr / sc.floodEventsPerSec;   // one press (and its release) per period
        const std::int64_t gap = std::max<std::int64_t>(1, std::llround(floodPeriod / 2));
        for (std::int64_t k = std::max<std::int64_t>(1, static_cast<std::int64_t>(static_cast<double>(abs) / floodPeriod) - 2);; ++k) {
          const std::int64_t t = std::llround(static_cast<double>(k) * floodPeriod);
          if (t >= abs + n) break;
          const int key = freeKeys[static_cast<std::size_t>(k) % freeKeys.size()];
          const std::int64_t id = 1000000000 + k;
          if (t >= abs) evs.push_back(on(key, 0, id, static_cast<int>(t - abs)));
          if (t + gap >= abs && t + gap < abs + n) evs.push_back(off(0, id, static_cast<int>(t + gap - abs)));
        }
      }
      std::stable_sort(evs.begin() + static_cast<std::ptrdiff_t>(first), evs.end(), [](const Event& a, const Event& b) { return a.offset < b.offset; });
      slice[static_cast<std::size_t>(b)] = {first, evs.size() - first};
      Block& blk = blocks[static_cast<std::size_t>(b)];
      blk.n = n;
      blk.settings = sc.st;
      blk.scale = sc.scale;
      blk.transport.playing = true;
      blk.transport.hasBeats = true;
      blk.transport.hasRep = true;
      blk.transport.rep = I128::mul(abs, inc);   // the exact position: posNum = sample * tempo(milli-bpm) * TPQ (D cancels, as in tests/core/shipped_smoke.cpp)
      blk.transport.hasTempo = true;
      blk.transport.tempo = sc.bpm;
      abs += n;
    }
    for (int b = 0; b < count; ++b) blocks[static_cast<std::size_t>(b)].in = std::span<const Event>(evs.data() + slice[static_cast<std::size_t>(b)].first, slice[static_cast<std::size_t>(b)].second);
  }
};

// ---------------------------------------------------------------- measurement
// The input families (D-032) are 31 of the 66 scenarios and the heavy ones: by default they take fewer repetitions of a shorter pass, which keeps the whole report
// (and so ./verify full) to a few minutes. `--reps N` sets both; the figures in docs/horde-copy-in.md were taken with 15 repetitions of 1 s (`--reps 15 --input-seconds 1`).
struct Params { int reps = 15; double secondsPerRep = 1.0; double warmupSeconds = 1.0; int inputReps = 5; double inputSecondsPerRep = 0.5; };

struct Result {
  double meanBlockNs = 0, minBlockNs = 0, maxBlockNs = 0;   // over the repetitions (batch timing)
  double p50 = 0, p99 = 0, worst = 0;                        // per call, overhead subtracted
  double nsPerSample = 0, pctCore = 0, derated = 0, p99Deadline = 0, worstDeadline = 0, spread = 0;
  int maxEvents = 0;
  double eventsPerSec = 0, nsPerEvent = 0;   // output events (NoteOn + NoteOff + Trigger + pass-through) per second of audio, and the mean cost of one
  double inPerSec = 0;                       // input note events (NoteOn + NoteOff) fed per second of audio, the held keys' first presses not counted (D-032)
  std::int64_t allocs = 0, overflow = 0, offsLost = 0;
};

// The smallest nonzero step steady_clock is seen to take: the granularity of every per-call figure below (about 42 ns on Apple silicon).
double timerQuantumNs() {
  double q = 1e9;
  auto prev = SteadyClock::now();
  for (int i = 0; i < 2000000; ++i) {
    const auto now = SteadyClock::now();
    const double d = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - prev).count());
    if (d > 0) q = std::min(q, d);
    prev = now;
  }
  return q;
}
double timerOverheadNs() {
  std::vector<double> d;
  d.reserve(200000);
  for (int i = 0; i < 200000; ++i) {
    const auto a = SteadyClock::now();
    const auto b = SteadyClock::now();
    d.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count()));
  }
  std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
  return d[d.size() / 2];
}

Result measure(const Scenario& sc, int sr, int block, const Params& base, double overhead) {
  Params p = base;
  if (!sc.family.empty()) { p.reps = base.inputReps; p.secondsPerRep = base.inputSecondsPerRep; }
  Result r;
  auto core = std::make_unique<Core>();
  if (!core->prepare(sr, block)) { std::fprintf(stderr, "prepare(%d, %d) refused\n", sr, block); std::exit(2); }
  if (sc.user) core->setUserPattern(*sc.user);   // M3b: M3a built the user patterns of three scenarios (ratchet 4, 8 trigger lanes, the worst case) and never installed them
  Feeder f(sc, sr);
  const int warmBlocks = std::max(1, static_cast<int>(p.warmupSeconds * sr / block));
  const int repBlocks = std::max(1, static_cast<int>(p.secondsPerRep * sr / block));
  volatile std::int64_t sink = 0;
  f.fill(warmBlocks, block);
  for (const Block& b : f.blocks) sink = sink + static_cast<std::int64_t>(core->process(b).size());
  std::vector<double> batch;
  std::int64_t eventsTotal = 0, inTotal = 0;
  std::vector<float> calls;
  calls.reserve(static_cast<std::size_t>(repBlocks) * static_cast<std::size_t>(p.reps));
  for (int rep = 0; rep < p.reps; ++rep) {
    f.fill(repBlocks, block);
    inTotal += static_cast<std::int64_t>(f.evs.size());
    {
      arp_test::AllocScope guard;   // the real-time contract: nothing may allocate inside process()
      const auto t0 = SteadyClock::now();
      for (const Block& b : f.blocks) { const auto res = core->process(b); r.maxEvents = std::max(r.maxEvents, static_cast<int>(res.size())); eventsTotal += static_cast<std::int64_t>(res.size()); sink = sink + static_cast<std::int64_t>(res.size()); }
      const auto t1 = SteadyClock::now();
      r.allocs += guard.count();
      batch.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / repBlocks);
    }
    f.fill(repBlocks, block);
    {
      arp_test::AllocScope guard;
      for (const Block& b : f.blocks) {
        const auto t0 = SteadyClock::now();
        const auto res = core->process(b);
        const auto t1 = SteadyClock::now();
        sink = sink + static_cast<std::int64_t>(res.size());
        calls.push_back(static_cast<float>(std::max(0.0, static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) - overhead)));
      }
      r.allocs += guard.count();
    }
  }
  {
    arp_test::AllocScope guard;
    (void)core->reset();
    r.allocs += guard.count();
  }
  std::sort(batch.begin(), batch.end());
  std::sort(calls.begin(), calls.end());
  r.meanBlockNs = batch[batch.size() / 2];
  r.minBlockNs = batch.front();
  r.maxBlockNs = batch.back();
  r.spread = r.meanBlockNs > 0 ? (r.maxBlockNs - r.minBlockNs) / r.meanBlockNs * 100 : 0;
  r.p50 = calls[calls.size() / 2];
  r.p99 = calls[static_cast<std::size_t>(static_cast<double>(calls.size() - 1) * 0.99)];
  r.worst = calls.back();
  r.nsPerSample = r.meanBlockNs / block;
  r.pctCore = r.nsPerSample * sr / 1e9 * 100;
  r.derated = r.pctCore * kDerate;
  const double audioSeconds = static_cast<double>(repBlocks) * p.reps * block / sr;
  r.eventsPerSec = static_cast<double>(eventsTotal) / audioSeconds;
  r.inPerSec = static_cast<double>(inTotal) / audioSeconds;
  r.nsPerEvent = eventsTotal > 0 ? r.meanBlockNs * repBlocks * p.reps / static_cast<double>(eventsTotal) : 0;
  const double deadline = static_cast<double>(block) / sr * 1e9;
  r.p99Deadline = r.p99 / deadline * 100;
  r.worstDeadline = r.worst / deadline * 100;
  const Counters& c = core->counters();
  r.overflow = c.heldOverflow + c.refOverflow + c.soundingOverflow + c.pendingOverflow + c.eventOverflow + c.refused;
  r.offsLost = c.offsLost;
  return r;
}

// ---------------------------------------------------------------- the stream digest (D-030)
// `arp_bench --digest` prints, for every scenario x rate x block, the hash of the SHIPPED core's whole output over kDigestSeconds of audio (every
// event field, step record and the final counters: tests/core/stream_digest.h). tools/core_test.sh diffs it against tests/core/stream_digest_bench.txt,
// generated from the pre-optimisation core, so the CPU work of M3b provably moved no byte. The scenarios are the benchmark's own, the heaviest
// loads there are: the worst case (112 notes, ratchet 4, 1/64 at 999 bpm) is exactly where an index or a merge could go wrong.
// `perturb` is the must-fail control: it changes one field of one event in what is HASHED (never in the core), and the diff must see it.
constexpr double kDigestSeconds = 2.0, kLightDigestSeconds = 1.0;

// `randomOnly`: the scenarios the main digest leaves out (noDigest: they use note randomness, which the file cut from 0ef0903 cannot hold) - their own file, cut from ed943b2.
void digestAll(bool perturb, bool randomOnly) {
  struct Cfg { int sr, block; };
  std::vector<Cfg> full, light;
  for (int sr : {48000, 44100, 96000, 192000})
    for (int block : {32, 64, 128, 512, 4096}) full.push_back({sr, block});
  light = {{48000, 16}, {48000, 4096}, {192000, 128}};   // the heavy D-032 loads: the rate, a tiny and a huge block; the engine is block-partition independent, the matrix above proves that
  if (randomOnly) std::printf("# the note-randomness scenarios only (D-036; tools/core_digest.sh regen-random): cut from ed943b2, the first core with randomness, before it was optimised\n");
  std::printf("# arp bench stream digest: scenario <TAB> rate block events stream-hash counters-hash (%.0f s of audio each, %.0f s for the heavy input scenarios; tests/core/stream_digest.h)\n", kDigestSeconds, kLightDigestSeconds);
  for (const Scenario& sc : scenarios()) {
    if ((sc.noDigest != nullptr) != randomOnly) continue;
    for (const Cfg& c : sc.lightDigest ? light : full) {
      const int sr = c.sr, block = c.block;
      auto core = std::make_unique<Core>();
      if (!core->prepare(sr, block)) { std::fprintf(stderr, "prepare(%d, %d) refused\n", sr, block); std::exit(2); }
      if (sc.user) core->setUserPattern(*sc.user);
      Feeder f(sc, sr);
      arp_test::Fnv fs;
      std::int64_t nEv = 0, base = 0;
      const int total = std::max(1, static_cast<int>((sc.lightDigest ? kLightDigestSeconds : kDigestSeconds) * sr / block));
      for (int done = 0; done < total;) {
        const int cnt = std::min(total - done, 2048);   // bound the Feeder's storage; the position carries over between fills
        f.fill(cnt, block);
        for (const Block& b : f.blocks) {
          const EventSpan res = core->process(b);
          for (Event e : res) {
            if (perturb && nEv == 3) e.vel += 1;
            arp_test::hashEvent(fs, base, 0, e);
            ++nEv;
          }
          for (const StepRecord& r : core->lastSteps()) arp_test::hashStep(fs, base, r);
          base += block;
        }
        done += cnt;
      }
      arp_test::Fnv fc;
      arp_test::hashCounters(fc, core->counters());
      std::printf("%s\t%d %d %lld %s %s\n", sc.name.c_str(), sr, block, static_cast<long long>(nEv), arp_test::hex16(fs.h).c_str(), arp_test::hex16(fc.h).c_str());
    }
  }
}

// ---------------------------------------------------------------- the memory figure
#define MEM_ROW(member, label) do { \
    const std::size_t total = sizeof(Core::member); \
    const std::size_t count = std::tuple_size<decltype(Core::member)>::value; \
    std::printf("  %-34s %6zu x %4zu B = %8zu B\n", label, count, total / count, total); \
    sum += total; } while (0)

void memoryFigure(bool& ok) {
  std::printf("\n== memory: sizeof and every allocation, worst case (all capacities full), shipped layout ==\n");
  std::printf("sizeof(arp::Core) = %zu B  (%.1f KiB): every fixed table below lives INSIDE the object, so it is the worst case already;\n", sizeof(Core), static_cast<double>(sizeof(Core)) / 1024);
  std::printf("  place it on the heap or as a member of a long-lived object, not on a small stack.\n");
  std::size_t sum = 0;
  MEM_ROW(userSlots_, "user pattern slots (triple buffer)");
  MEM_ROW(chordRec_, "chord stage records (32 held keys)");
  MEM_ROW(chordNote_, "chord stage notes (by key 0..127)");
  MEM_ROW(chordSet_, "chord set H' (distinct keys 0..127)");
  MEM_ROW(refs_, "held-key refs (32 keys x 4)");
  MEM_ROW(ignored_, "ignored-ref ring");
  MEM_ROW(sounding_, "sounding table (128 + 16 slack + 224 chord)");
  MEM_ROW(pending_, "pending queue");
  MEM_ROW(owed_, "owed-off refs (T10, survive prepare)");
  std::printf("  %-34s %33zu B  (counters, scalars, the sounding table's indexes, the vectors' headers, latest user pattern)\n", "everything else", sizeof(Core) - sum);
  std::printf("capacities: held keys %d, refs per key %d, sounding %d (+%d slack, +%d chord), pending %d, off reserve %d of max(%d, 4 x maxBlock) events per block\n",
              kMaxHeldKeys, kMaxRefsPerKey, kMaxSounding, Core::kSoundingSlack, Core::kMaxChordSounding, kMaxPending, kOffReserve, kMinEventsPerBlock);
  std::printf("sizeof(Event) = %zu B, sizeof(StepRecord) = %zu B, sizeof(Block) = %zu B (the adapter owns the Blocks)\n", sizeof(Event), sizeof(StepRecord), sizeof(Block));

  std::printf("\nallocations (operator new, counted by the A1 hook; a malloc()/mmap() inside the core would not show, the core includes none):\n");
  std::unique_ptr<Core> core;
  {
    arp_test::AllocScope g;
    core = std::make_unique<Core>();
    std::printf("  make_unique<Core>()           %lld call(s), %lld B   (the object itself; the constructor allocates nothing else)\n", static_cast<long long>(g.count()), static_cast<long long>(g.bytes()));
    if (g.count() != 1 || g.bytes() != static_cast<std::int64_t>(sizeof(Core))) ok = false;
  }
  struct Cfg { int sr, block; };
  for (const Cfg c : {Cfg{48000, 32}, Cfg{48000, 512}, Cfg{48000, 4096}, Cfg{8000, 4096}}) {
    auto fresh = std::make_unique<Core>();
    arp_test::AllocScope g;
    fresh->prepare(c.sr, c.block);
    const std::int64_t bound = cdiv(static_cast<std::int64_t>(c.block) * 999 * kTpq, static_cast<std::int64_t>(60) * c.sr * 960) + 1;
    const std::int64_t want = static_cast<std::int64_t>(std::max(kMinEventsPerBlock, 4 * c.block)) * static_cast<std::int64_t>(sizeof(Event)) + (bound + 8) * static_cast<std::int64_t>(sizeof(StepRecord));
    std::printf("  prepare(%6d, %4d)           %lld call(s), %8lld B   = %d events x %zu B + %lld step records x %zu B%s\n", c.sr, c.block, static_cast<long long>(g.count()), static_cast<long long>(g.bytes()),
                fresh->eventCapacity(), sizeof(Event), static_cast<long long>(bound + 8), sizeof(StepRecord), g.bytes() == want && g.count() == 2 ? "" : "   MISMATCH with the formula");
    if (g.bytes() != want || g.count() != 2) ok = false;
  }
  {
    arp_test::AllocScope g;
    core->prepare(48000, 512);
    const std::int64_t first = g.count();
    core->prepare(48000, 512);
    std::printf("  prepare(48000, 512) twice     %lld call(s) in all (the second one reuses the vectors: %lld)\n", static_cast<long long>(g.count()), static_cast<long long>(g.count() - first));
  }
  {
    arp_test::AllocScope g;
    UserPattern u;
    core->setUserPattern(u);
    const UserPattern back = core->getUserPattern();
    (void)back;
    (void)core->heldKeyCount();
    std::printf("  setUserPattern / getUserPattern / heldKeyCount: %lld call(s) (fixed slots, sized at construction)\n", static_cast<long long>(g.count()));
    if (g.count() != 0) ok = false;
  }
  std::printf("  process() and reset(): counted around every timed loop below (total reported at the end); must be 0\n");
  std::printf("WORST-CASE TOTAL (maxBlock 4096 at 8 kHz, the largest steps bound): sizeof(Core) %zu B + prepare %lld B = %.2f MiB; allocated at construction and prepare only\n", sizeof(Core),
              static_cast<long long>(static_cast<std::int64_t>(std::max(kMinEventsPerBlock, 4 * 4096)) * static_cast<std::int64_t>(sizeof(Event)) +
                                     (cdiv(static_cast<std::int64_t>(4096) * 999 * kTpq, static_cast<std::int64_t>(60) * 8000 * 960) + 1 + 8) * static_cast<std::int64_t>(sizeof(StepRecord))),
              static_cast<double>(sizeof(Core) + static_cast<std::size_t>(std::max(kMinEventsPerBlock, 4 * 4096)) * sizeof(Event) +
                                  static_cast<std::size_t>(cdiv(static_cast<std::int64_t>(4096) * 999 * kTpq, static_cast<std::int64_t>(60) * 8000 * 960) + 1 + 8) * sizeof(StepRecord)) / (1024.0 * 1024.0));
}

}  // namespace

int main(int argc, char** argv) {
#ifndef NDEBUG
  std::fprintf(stderr, "arp_bench: this is not a Release build (NDEBUG is not defined); refusing to report CPU numbers from it\n");
  return 2;
#endif
  Params params;
  std::string only;
  bool digest = false, perturb = false, randomDigest = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quick") == 0) { params.reps = 5; params.secondsPerRep = 0.15; params.warmupSeconds = 0.3; params.inputReps = 5; params.inputSecondsPerRep = 0.15; }
    else if (std::strcmp(argv[i], "--reps") == 0 && i + 1 < argc) { params.reps = std::max(3, std::atoi(argv[++i])); params.inputReps = params.reps; }
    else if (std::strcmp(argv[i], "--input-seconds") == 0 && i + 1 < argc) params.inputSecondsPerRep = std::max(0.05, std::atof(argv[++i]));
    else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = argv[++i];   // a substring of a scenario name: profile one scenario
    else if (std::strcmp(argv[i], "--cpu-time") == 0) gCpuTime = true;
    else if (std::strcmp(argv[i], "--digest") == 0) { digest = true; }
    else if (std::strcmp(argv[i], "--digest-perturb") == 0) { digest = true; perturb = true; }
    else if (std::strcmp(argv[i], "--digest-random") == 0) { digest = true; randomDigest = true; }
    else if (std::strcmp(argv[i], "--digest-random-perturb") == 0) { digest = true; perturb = true; randomDigest = true; }
    else { std::fprintf(stderr, "usage: arp_bench [--quick] [--reps N] [--input-seconds S] [--only SUBSTRING] [--cpu-time] [--digest | --digest-perturb | --digest-random | --digest-random-perturb]\n"); return 2; }
  }
  if (digest) { digestAll(perturb, randomDigest); return 0; }   // the stream digest: no timing, so it is meaningful in any build type and on a loaded machine
  std::printf("arp core benchmark - int128 path: %s, compiler: %s\n", kInt128IsNative ? "native __int128" : "two-word fallback (ARP_FORCE_INT128_FALLBACK)", __VERSION__);
  std::printf("method: %d repetitions of %.2f s of audio per configuration after %.1f s warm-up (the INPUT scenarios: %d of %.2f s); mean = median of batch timings; p50/p99/max per call, timer overhead subtracted\n", params.reps, params.secondsPerRep, params.warmupSeconds, params.inputReps, params.inputSecondsPerRep);
  std::printf("%%core = ns per sample x sample rate / 1e9 (one core, real time); 'x4' = horde's min-spec derate heuristic (NOT min-spec verified) against the %.0f %% budget\n", kBudgetPct);
  const double overhead = timerOverheadNs();
  std::printf("timer: overhead (median of back-to-back steady_clock reads) %.1f ns, granularity %.1f ns: per-call p50/p99/max are quantised to that; the mean comes from batch timing and is not\n", overhead, timerQuantumNs());

  bool ok = true;
  memoryFigure(ok);

  std::vector<Scenario> list = scenarios();
  if (!only.empty()) list.erase(std::remove_if(list.begin(), list.end(), [&](const Scenario& s) { return s.name.find(only) == std::string::npos; }), list.end());
  if (list.empty()) { std::fprintf(stderr, "no scenario matches '%s'\n", only.c_str()); return 2; }
  const int rates[] = {48000, 44100, 96000, 192000};
  const int blocks[] = {16, 32, 64, 128, 512};
  struct Row { std::string name, family; int sr, block; Result r; };
  std::vector<Row> rows;
  std::int64_t allocs = 0, lost = 0;
  std::printf("\n== CPU: %zu scenarios x %zu sample rates x %zu block sizes ==\n", list.size(), std::size(rates), std::size(blocks));
  for (const Scenario& sc : list) {
    std::printf("\n%s\n", sc.name.c_str());
    std::printf("  %-6s %-5s %10s %10s %11s %9s %8s %8s %8s %8s %8s %6s %6s %10s %9s %7s\n", "sr", "blk", "mean ns/b", "p99 ns/b", "max ns/b", "ns/smp", "%core", "x4 %", "p99/dl%", "max/dl%", "spread%", "evmax", "ovf", "ev/s", "in/s", "ns/ev");
    for (int sr : rates)
      for (int block : blocks) {
        const Result r = measure(sc, sr, block, params, overhead);
        rows.push_back({sc.name, sc.family, sr, block, r});
        allocs += r.allocs;
        lost += r.offsLost;
        std::printf("  %-6d %-5d %10.0f %10.0f %11.0f %9.2f %8.4f %8.4f %8.3f %8.3f %8.1f %6d %6lld %10.0f %9.0f %7.0f\n", sr, block, r.meanBlockNs, r.p99, r.worst, r.nsPerSample, r.pctCore, r.derated, r.p99Deadline, r.worstDeadline, r.spread, r.maxEvents, static_cast<long long>(r.overflow), r.eventsPerSec, r.inPerSec, r.nsPerEvent);
        std::fflush(stdout);
      }
  }

  const auto isInput = [](const Row& r) { return !r.family.empty(); };   // D-032: the scenarios that add dense incoming notes to a load
  std::printf("\n== summary: worst scenario per sample rate (mean %%core, then x4 against the %.0f %% slice; max over block sizes 16/32/64/128/512) ==\n", kBudgetPct);
  for (int sr : rates) {
    const Row* worst = nullptr;
    const Row* worstTypical = nullptr;
    for (const Row& r : rows) {
      if (r.sr != sr || isInput(r)) continue;   // the settings-only scenarios here; the input families have their own lines below
      if (!worst || r.r.pctCore > worst->r.pctCore) worst = &r;
      if (r.name.rfind("WORST CASE", 0) != 0 && (!worstTypical || r.r.pctCore > worstTypical->r.pctCore)) worstTypical = &r;
    }
    if (!worst) continue;   // (--only picked input families alone)
    std::printf("  %6d Hz  heaviest scenario built from settings alone: %.4f %% of a core, x4 = %.4f %%  [%s, block %d]  -> %s the %.0f %% slice\n", sr, worst->r.pctCore, worst->r.derated, worst->name.c_str(), worst->block, worst->r.derated <= kBudgetPct ? "WITHIN" : "OVER", kBudgetPct);
    int over = 0, total = 0;
    for (const Row& r : rows) if (r.sr == sr && !isInput(r)) { ++total; over += r.r.derated > kBudgetPct; }
    std::printf("            configurations over the %.0f %% slice after x4: %d of %d (every settings-only scenario x every block size)\n", kBudgetPct, over, total);
    if (worstTypical) std::printf("            heaviest excluding the deliberate worst cases: %.4f %%, x4 = %.4f %%  [%s, block %d]  -> %s\n", worstTypical->r.pctCore, worstTypical->r.derated, worstTypical->name.c_str(), worstTypical->block, worstTypical->r.derated <= kBudgetPct ? "WITHIN" : "OVER");
  }
  {   // D-032: per family of input scenarios, the largest incoming note-event rate that keeps the load inside the slice after x4. The rate-0 member of a family is the load alone.
    // Cost is taken as piecewise linear between the measured rates (the cost per input event is nearly constant); the first measured rate that is over the slice and the one before it bracket the answer.
    std::printf("\n== input envelope: the largest incoming note-event rate (NoteOn + NoteOff per second, host side) that stays within the %.0f %% slice after x4 (linear between measured rates; the worst block size of %zu) ==\n", kBudgetPct, std::size(blocks));
    std::vector<std::string> fams;
    for (const Row& r : rows) if (isInput(r) && std::find(fams.begin(), fams.end(), r.family) == fams.end()) fams.push_back(r.family);
    for (const std::string& fam : fams) {
      std::printf("  %s\n", fam.c_str());
      for (int sr : rates) {
        double best = 1e18;
        int bestBlock = 0;
        bool baseOver = false, capped = true;
        double maxMeasured = 0;
        for (int block : blocks) {
          std::vector<std::pair<double, double>> pts;   // (input events per second, x4 %)
          for (const Row& r : rows) if (r.family == fam && r.sr == sr && r.block == block) pts.emplace_back(r.r.inPerSec, r.r.derated);
          std::sort(pts.begin(), pts.end());
          if (pts.empty()) continue;
          maxMeasured = std::max(maxMeasured, pts.back().first);
          double limit = 1e18;
          if (pts[0].second > kBudgetPct) { baseOver = true; limit = 0; }
          else
            for (std::size_t i = 1; i < pts.size(); ++i)
              if (pts[i].second > kBudgetPct) { limit = pts[i - 1].first + (pts[i].first - pts[i - 1].first) * (kBudgetPct - pts[i - 1].second) / (pts[i].second - pts[i - 1].second); capped = false; break; }
          if (limit < best) { best = limit; bestBlock = block; }
        }
        if (baseOver) std::printf("    %6d Hz  the load alone (no input) is already over the slice at block %d\n", sr, bestBlock);
        else if (capped) std::printf("    %6d Hz  not reached: every measured rate (up to %.0f events/s) is within the slice\n", sr, maxMeasured);
        else std::printf("    %6d Hz  %.0f events/s (block %d)\n", sr, best, bestBlock);
      }
    }
    int over = 0, total = 0;
    for (const Row& r : rows) if (isInput(r)) { ++total; over += r.r.derated > kBudgetPct; }
    std::printf("  input families: configurations over the %.0f %% slice after x4: %d of %d (the families run past their envelope on purpose)\n", kBudgetPct, over, total);
  }
  {   // what the slice buys in events: the budget is 2 % of the min-spec core, i.e. 2 / 4 = 0.5 % of THIS machine's core under the x4 heuristic
    const Row* w = nullptr;
    for (const Row& r : rows) if (r.sr == 48000 && r.block == 128 && r.name.rfind("WORST CASE for events", 0) == 0) w = &r;
    if (w && w->r.nsPerEvent > 0)
      std::printf("  events: the deliberate worst case costs %.0f ns per output event at 48 kHz / 128; the %.0f %% slice at x%.0f is %.2f %% of this core = about %.0f output events per second at that cost\n", w->r.nsPerEvent, kBudgetPct, kDerate, kBudgetPct / kDerate, kBudgetPct / kDerate / 100 * 1e9 / w->r.nsPerEvent);
  }
  double maxP99 = 0, maxWorst = 0;
  for (const Row& r : rows) { maxP99 = std::max(maxP99, r.r.p99Deadline); maxWorst = std::max(maxWorst, r.r.worstDeadline); }
  std::printf("  per-block deadline: the largest p99 over any configuration is %.3f %% of its block period, the largest single call %.3f %% (OS preemption noise included)\n", maxP99, maxWorst);
  std::printf("  allocations inside process() / reset() over every timed loop: %lld %s; NoteOffs lost: %lld %s\n", static_cast<long long>(allocs), allocs == 0 ? "(OK)" : "(VIOLATION)", static_cast<long long>(lost), lost == 0 ? "(OK)" : "(VIOLATION)");
  std::printf("  caveat: this machine is NOT min-spec; the x4 derate is horde's heuristic, so the figure is an estimate, not a min-spec measurement.\n");
  if (allocs != 0 || lost != 0 || !ok) { std::printf("BENCH FAILED: a real-time or memory contract was broken (see above)\n"); return 1; }
  std::printf("BENCH OK (reported, never gated)\n");
  return 0;
}
