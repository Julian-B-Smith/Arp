// test_input.cpp - dense host input (M3c, D-032): the held-key aggregates and the chord stage's incremental pass.
//
// Two optimisations changed how the core treats a host that keeps sending notes: noteOn counts a key's refs and the distinct held keys from per-key
// counters instead of scanning (core.h, keyRefCnt_), and the chord stage updates itself for the keys that changed instead of rebuilding (chordIncremental).
// The committed digest pins the output against the M3a core on every session of the suite; these checks pin the two mechanisms directly, on drawn sessions
// built to churn the held set (big chords, several refs per key, off and on at one sample, id-less refs, latch and pedal, chord settings changed
// mid-session, keys outside 0-127, Panic and reset), which the written sessions do not reach in that density:
//   * the counters equal a recount of the ref table after every block, and the chord set count equals the used chord notes;
//   * the output with the incremental pass equals the output with plant chordFullPass (every pass rebuilds the whole stage: the M3a algorithm), byte for byte.
// Each detector has its planted control: a corrupted counter, and two faults in the incremental pass (a stale record, notes started in the wrong order).
//
// This file reads the core's private tables (`private` made `public` around the one include of core.h, as test_evict.cpp and the benchmark do).
// It records nothing in the committed digest: the plants it needs do not exist in the M3a core that digest is generated from.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "arp/arith.h"
#include "arp/int128.h"
#include "arp/pattern.h"
#include "arp/types.h"
#define private public
#include "arp/core.h"
#undef private
#include "check.h"
#include "harness.h"
#include "test_input.h"

namespace arp_test {
namespace {

using namespace arp;

struct Rng {   // mulberry32, the project's stream; every draw is a statement (the order of draws inside an argument list is the compiler's)
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

// The aggregates of core.h ("Per-key aggregates of refs_") against a recount, and the chord set count against the used notes.
template <class C>
bool aggregatesOk(const C& c) {
  std::array<int, 128> cnt{};
  int odd = 0, distinct = 0;
  for (int i = 0; i < c.refN_; ++i) {
    const std::int32_t k = c.refs_[static_cast<std::size_t>(i)].key;
    if (static_cast<std::uint32_t>(k) < 128u) ++cnt[static_cast<std::size_t>(k)]; else ++odd;
  }
  for (int k = 0; k < 128; ++k) {
    if (cnt[static_cast<std::size_t>(k)] != c.keyRefCnt_[static_cast<std::size_t>(k)]) return false;
    distinct += cnt[static_cast<std::size_t>(k)] > 0;
  }
  if (distinct != c.keysHeld_ || odd != c.refsOutOfRange_) return false;
  int used = 0;
  for (const auto& n : c.chordNote_) used += n.used;
  if (used != c.chordSetN_) return false;
  typename C::HeldKey tmp[kMaxHeldKeys];
  return c.distinctHeld() == c.collectHeld(tmp) && c.heldCountForArp() == (c.moduleOn_ && c.S_.chordEnabled ? used : c.collectHeld(tmp));
}

// One drawn session: a held set that churns, with the chord stage mostly on.
Session drawSession(std::uint32_t seed) {
  Rng r(0x1D0C0000u + seed * 2654435761u);
  Session s;
  s.sr = 8000;
  s.total = r.in(2000, 6000);
  s.bpm = r.pct(40) ? 999 : r.in(90, 400);
  s.maxBlock = 4096;
  Settings& b = s.base;
  b.enabled = true;
  b.rate = static_cast<Rate>(r.in(15, 21));
  b.order = static_cast<Order>(r.in(0, 15));
  b.gate = r.pct(50) ? 80 : r.in(20, 200);
  b.steps = r.pct(60) ? 0 : r.in(1, 3);
  b.stepSize = r.in(-7, 7);
  b.latch = r.pct(70) ? Latch::Off : static_cast<Latch>(r.in(1, 2));
  b.quantize = r.pct(50) ? Quantize::Off : static_cast<Quantize>(r.in(1, 2));
  b.scaleSource = r.pct(50) ? ScaleSource::Global : ScaleSource::Own;
  b.scaleRoot = r.in(0, 11);
  for (int d = 0; d < 12; ++d) b.scaleDeg[d] = r.pct(60);
  b.chordEnabled = r.pct(88);
  b.chordUnit = r.pct(35) ? StepUnit::Degrees : StepUnit::Semitones;
  for (int i = 0; i < 6; ++i) {
    b.chordOn[i] = r.pct(50);
    b.chordShift[i] = r.in(-14, 14);
  }
  s.scale = Scale{r.in(0, 11), static_cast<std::uint16_t>(r.next() & 0xFFF)};
  const int nScale = r.in(0, 2);
  for (int i = 0; i < nScale; ++i) {
    const int at = r.in(1, s.total - 1);
    const int root = r.in(0, 11);
    const auto mask = static_cast<std::uint16_t>(r.next() & 0xFFF);
    s.scaleAt.push_back({at, Scale{root, mask}});
  }
  const int nCfg = r.in(0, 4);
  for (int i = 0; i < nCfg; ++i) {
    const int at = r.in(1, s.total - 1);
    const int what = r.in(0, 5);
    const int x = r.in(0, 15), y = r.in(-14, 14);
    const bool flag = r.pct(50);
    s.cfg.push_back({at, [what, x, y, flag](Settings& st) {
      switch (what) {
        case 0: st.chordEnabled = flag; break;
        case 1: st.chordOn[x % 6] = flag; st.chordShift[x % 6] = y; break;
        case 2: st.quantize = static_cast<Quantize>(x % 3); break;
        case 3: st.latch = static_cast<Latch>(x % 3); break;
        case 4: st.chordUnit = flag ? StepUnit::Degrees : StepUnit::Semitones; break;
        default: st.scaleSource = flag ? ScaleSource::Own : ScaleSource::Global; break;
      }
    }});
  }
  if (r.pct(8)) s.resets.push_back(r.in(1, s.total - 1));

  // the input
  struct Held { int key, ch; std::int64_t id; };
  std::vector<Held> held;
  const int base = r.in(10, 90), spacing = r.in(1, 3), pool = r.in(3, 40);
  const int nEv = r.in(40, 220);
  int t = r.in(0, 40);
  std::int64_t nextId = 1;
  auto press = [&](int at, int key, bool idless) {
    const int ch = r.in(0, 15);
    Event e;
    e.type = EventType::NoteOn;
    e.key = key;
    e.vel = r.in(1, 127);
    const std::int64_t id = idless ? 0 : nextId++;
    e.ref = idless ? NoteRef{0, ch, key, 0} : NoteRef{0, ch, 0, id};
    s.ins.push_back({at, e});
    held.push_back({key, ch, id});
  };
  auto release = [&](int at, std::size_t i) {
    const Held h = held[i];
    Event e;
    e.type = EventType::NoteOff;
    e.ref = h.id == 0 ? NoteRef{0, h.ch, h.key, 0} : NoteRef{0, h.ch, 0, h.id};
    s.ins.push_back({at, e});
    held.erase(held.begin() + static_cast<std::ptrdiff_t>(i));
  };
  const bool idless = r.pct(25);
  for (int i = 0; i < nEv && t < s.total - 1; ++i) {
    if (r.pct(70)) t += r.in(1, 60);   // else: the same sample as the last event
    if (t >= s.total) t = s.total - 1;
    const int what = r.in(0, 99);
    if (what < 52) {
      const int key = r.pct(4) ? (r.pct(50) ? -3 : 130 + r.in(0, 80)) : base + spacing * r.in(0, pool - 1);
      press(t, key, idless);
    } else if (what < 82) {
      if (!held.empty()) release(t, static_cast<std::size_t>(r.in(0, static_cast<int>(held.size()) - 1)));
    } else if (what < 92) {   // a re-strike: off and on of one key, at one sample or the next
      if (!held.empty()) {
        const std::size_t i0 = static_cast<std::size_t>(r.in(0, static_cast<int>(held.size()) - 1));
        const int key = held[i0].key;
        release(t, i0);
        press(std::min(s.total - 1, t + (r.pct(50) ? 0 : 1)), key, idless);
      }
    } else if (what < 94) {   // a whole chord at one sample, then released together later
      const int n = r.in(3, 14);
      const int k0 = base + spacing * r.in(0, std::max(0, pool - n));
      for (int j = 0; j < n; ++j) press(t, k0 + spacing * j, idless);
    } else if (what < 97) {
      Event e;
      e.type = EventType::Pedal;
      e.down = r.pct(55);
      s.ins.push_back({t, e});
    } else if (what < 98) {
      Event e;
      e.type = EventType::Panic;
      s.ins.push_back({t, e});
      held.clear();
    } else {   // release everything
      while (!held.empty()) release(t, held.size() - 1);
    }
  }
  return s;
}

struct Outcome { bool same = true; bool aggregates = true; std::uint64_t events = 0; };

int pickBlock(std::uint32_t seed) {
  static const int kBlocks[] = {1, 7, 64, 333, 4096};
  return kBlocks[seed % 5];
}

// The session with the incremental pass against the same session in reference mode (plant chordFullPass), optionally with a fault in the incremental pass.
template <class C>
Outcome compare(std::uint32_t seed, const std::string& fault) {
  Outcome o;
  Session s = drawSession(seed);
  const int block = pickBlock(seed / 5);
  bool agg = true;
  s.probe = [&agg](int, int, const auto& c) { agg = agg && aggregatesOk<C>(c); };
  if (!fault.empty()) s.plants = {fault};
  const Run a = runOnce(s, block, true);
  Session ref = drawSession(seed);
  ref.plants = {"chordFullPass"};
  const Run b = runOnce(ref, block, true);
  o.same = a.digestStream == b.digestStream && a.digestCounters == b.digestCounters && a.nEvents == b.nEvents;
  o.aggregates = agg;
  o.events = a.nEvents;
  return o;
}

// A template on the core type, so that this file builds (and does nothing) against the pre-optimisation core the digests are generated from
// (tools/core_digest.sh regen REV): that core has neither the counters nor the plants these checks need, and a `requires` on a dependent type is
// the one way to ask for a member that may not exist without a hard error.
template <class C>
void runInputTestsFor() {
  if constexpr (requires(C& c) { c.keyRefCnt_; c.chordSetN_; c.plants.chordFullPass; c.plants.chordIncSkipsRefresh; c.plants.chordIncDescending; }) {
    std::puts("Dense host input: the held-key counters, and the chord stage's incremental pass against its full-pass reference");
    DigestLog& d = digestLog();
    const bool was = d.enabled;
    d.enabled = false;   // these sessions are not part of the committed digest

    // -- the counters detector first: a corrupted counter must read red
    {
      Session s;
      s.total = 3000;
      s.ins = {Kk(0, 60), Kk(0, 64), Kk(10, 67), Kk(10, 64, 100, 3)};
      bool clean = true, corrupted = false;
      s.probe = [&](int pos, int, const auto& c) {
        clean = clean && aggregatesOk<C>(c);
        if (pos != 0) return;
        C& live = const_cast<C&>(c);   // broken for one look, on purpose, and put back
        ++live.keyRefCnt_[64];
        corrupted = !aggregatesOk<C>(c);
        --live.keyRefCnt_[64];
      };
      runOnce(s, 64, true);
      plant(corrupted, "a held-key counter that is off by one (key 64) is named by the recount");
      check(clean, "the held-key counters equal a recount of the ref table after every block (a small chord, a doubled key)");
    }

    // -- a known answer for the record refresh (L-3): H' is ordered by the press of the EARLIEST LIVE ref of a key, which MOVES when the earliest of two refs on one key is
    //    released (SPEC 3 / 14). Key 60 is pressed (ref a1, sample 0), key 64 (sample 10), key 60 again (ref a2, sample 20); a1 is released at sample 30. Until then 60 is
    //    first in press order; after, its earliest live ref is a2 (sample 20), so the order is 64, 60. The arp's first step (sample 0) played 60 alone; from the next step
    //    on `played` alternates over the two keys, and the position in the alternation (m = 1) makes the order visible: 60 64 60 64 with the order refreshed, 64 60 64 60
    //    when the stage keeps the stale press time of key 60 (sample 0) and so the order 60, 64 (plant chordIncSkipsRefresh).
    {
      auto notesAfterFirstStep = [](const std::string& plantName) {
        Session s;
        s.sr = 8000;
        s.total = 4500;
        s.base.order = Order::Played;
        s.base.chordEnabled = true;
        for (bool& on : s.base.chordOn) on = false;
        s.ins = {K(0, "a1", 60), K(10, "b", 64), K(20, "a2", 60), O(30, "a1")};
        if (!plantName.empty()) s.plants = {plantName};
        const Run r = runOnce(s, 64);
        std::string keys;
        for (const Rec& e : r.evs)
          if (e.t == EventType::NoteOn && e.stream == Stream::Arp && e.a >= 500) keys += std::to_string(e.key) + " ";
        return keys;
      };
      const std::string clean = notesAfterFirstStep(""), reference = notesAfterFirstStep("chordFullPass"), faulty = notesAfterFirstStep("chordIncSkipsRefresh");
      plant(faulty != clean, "a stale record (plant chordIncSkipsRefresh) keeps key 60 first after its earliest ref is released: the arp plays \"" + faulty + "\" instead of \"" + clean + "\"");
      check(clean == "60 64 60 64 " && reference == clean, "known answer (SPEC 3 / 14): two refs on key 60 at samples 0 and 20, key 64 at 10, the earliest ref of 60 released at 30: `played` continues 60 64 60 64 (incremental pass \"" + clean + "\", full pass \"" + reference + "\")");
    }

    // -- the drawn sessions
    constexpr int kSessions = 600;
    int differ = 0, aggBad = 0;
    std::uint64_t events = 0;
    for (std::uint32_t seed = 0; seed < kSessions; ++seed) {
      const Outcome o = compare<C>(seed, "");
      differ += !o.same;
      aggBad += !o.aggregates;
      events += o.events;
    }
    int skipsRefresh = 0, descending = 0;
    for (std::uint32_t seed = 0; seed < kSessions; ++seed) {
      skipsRefresh += !compare<C>(seed, "chordIncSkipsRefresh").same;
      descending += !compare<C>(seed, "chordIncDescending").same;
    }
    d.enabled = was;
    plant(skipsRefresh > 0, "a fault in the incremental chord pass (it does not refresh a stored record) changes the output of " + std::to_string(skipsRefresh) + " of " + std::to_string(kSessions) + " drawn sessions");
    plant(descending > 0, "a fault in the incremental chord pass (notes started in descending key order) changes the output of " + std::to_string(descending) + " of " + std::to_string(kSessions) + " drawn sessions");
    check(events > 100000, "the drawn sessions play: " + std::to_string(events) + " output events over " + std::to_string(kSessions) + " sessions");
    check(aggBad == 0, "the held-key counters and the chord set count are exact after every block of " + std::to_string(kSessions) + " drawn sessions (" + std::to_string(aggBad) + " sessions broke them)");
    check(differ == 0, "the incremental chord pass gives the same bytes as the full pass on " + std::to_string(kSessions) + " drawn sessions of churning input (" + std::to_string(differ) + " differ)");
  }
}

}  // namespace

void runInputTests() { runInputTestsFor<Core>(); }

}  // namespace arp_test
