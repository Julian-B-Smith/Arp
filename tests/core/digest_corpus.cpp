// digest_corpus.cpp - a seeded random-session differential for the stream digest (M3b rework, D-030 critic MEDIUM-1).
//
// The suite's sessions are written by people, so they miss the corners nobody thought of: a mutant of the eviction test (a note born one sample
// earlier made unevictable) passed the whole suite and its digest. These 2,000 sessions are drawn, not written: tiny capacities (sounding 1-6,
// pending 1-10, events 2-40, held keys 1-4, refs per key 1-4) so every overflow path runs, 16 channels, id-less refs and duplicate ids, a block size
// that changes from block to block (1...4096), sample rates 8 kHz-768 kHz, reset() and prepare() in the middle, transport stops / jumps / loops /
// tempo changes, the chord stage, quantize, time mode, restart modes, repeats, user patterns with ratchet / tie / extreme-gate / trigger steps.
// Each session is hashed like any other run (stream_digest.h) and recorded in the digest, so a core that changes any of it is named.
//
// Everything is an explicit statement drawn from one generator: no draw sits inside an argument list (the order of those is the compiler's: clang
// left to right, g++ right to left, which is how the suite once hashed differently on the Linux CI), no distribution object (implementation-defined),
// no floating point beyond exact quarter beats. mulberry32 is the project's stream (D-000).
#include "digest_corpus.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "harness.h"

namespace arp_test {
namespace {

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

void drawSettings(Rng& r, Settings& s) {
  s.enabled = r.pct(94);
  s.arpEnabled = r.pct(90);
  s.order = static_cast<Order>(r.in(0, 15));
  s.pattern = static_cast<Pattern>(r.in(0, 11));
  s.rate = static_cast<Rate>(r.in(0, 21));
  s.rateMode = r.pct(15) ? RateMode::Time : RateMode::Sync;
  s.stepTimeUs = r.in(10000, 300000);
  s.steps = r.pct(50) ? 0 : r.in(0, 4);
  s.stepSize = r.in(-24, 24);
  s.stepUnit = r.pct(30) ? StepUnit::Degrees : StepUnit::Semitones;
  const int gateKind = r.in(0, 3);
  s.gate = gateKind == 0 ? 1 : gateKind == 1 ? 200 : r.in(1, 200);
  s.swing = r.in(500, 750);
  s.latch = r.pct(70) ? Latch::Off : static_cast<Latch>(r.in(1, 2));
  s.velMode = r.pct(70) ? VelMode::Played : VelMode::Fixed;
  s.velFixed = r.in(1, 127);
  s.restart = r.pct(60) ? Restart::Chord : static_cast<Restart>(r.in(1, 2));
  s.repeats = r.pct(75) ? 0 : r.in(1, 4);
  s.quantize = r.pct(60) ? Quantize::Off : static_cast<Quantize>(r.in(1, 2));
  s.scaleSource = r.pct(50) ? ScaleSource::Global : ScaleSource::Own;
  s.scaleRoot = r.in(0, 11);
  for (int d = 0; d < 12; ++d) s.scaleDeg[d] = r.pct(60);
  s.seed = r.next();
  s.chordEnabled = r.pct(45);
  s.chordUnit = r.pct(30) ? StepUnit::Degrees : StepUnit::Semitones;
  for (int i = 0; i < 6; ++i) {
    s.chordOn[i] = r.pct(55);
    s.chordShift[i] = r.in(-14, 14);
  }
}

UserPattern drawUser(Rng& r) {
  UserPattern u;
  u.length = r.in(1, 16);
  u.stepSize = r.in(-12, 12);
  u.stepUnit = r.pct(30) ? StepUnit::Degrees : StepUnit::Semitones;
  for (PatternStep& p : u.steps) {
    p.on = r.pct(85);
    p.tie = r.pct(15);
    p.vel = r.in(0, 200);
    const int g = r.in(0, 3);
    p.gate = g == 0 ? 1 : g == 1 ? 200 : r.in(1, 200);
    p.step = r.in(-3, 3);
    p.prob = r.pct(70) ? 100 : r.in(0, 100);
    p.ratchet = r.in(1, 4);
    p.trig = r.pct(70) ? 0 : r.in(1, 8);
    p.pick = r.pct(70) ? kPickOrder : r.in(0, 12);
  }
  return u;
}

// One timed input event of the session, in absolute samples.
struct Timed { std::int64_t at; Event ev; };

}  // namespace

void recordDigestCorpus() {
  static const int kRates[] = {8000, 11025, 22050, 44100, 48000, 96000, 192000, 768000};
  static const int kBlocks[] = {1, 7, 64, 333, 512, 4096};
  static const int kBpm[] = {20, 77, 120, 120, 333, 999, 600, 5, 2000};   // 5 and 2000 are clamped by the core
  DigestLog& d = digestLog();
  for (std::uint32_t seed = 0; seed < static_cast<std::uint32_t>(kDigestCorpusSessions); ++seed) {
    Rng r(0xD16E5700u + seed * 2654435761u);
    const int sr = kRates[r.in(0, 7)];
    const int total = r.in(300, sr >= 192000 ? 2500 : 9000);
    Caps caps;
    caps.sounding = r.in(1, 6);
    caps.pending = r.in(1, 10);
    caps.events = r.pct(20) ? 0 : r.in(2, 40);
    caps.held = r.in(1, 4);
    caps.refsPerKey = r.in(1, 4);
    Settings st;
    drawSettings(r, st);
    st.enabled = st.enabled || r.pct(50);
    const bool hasUser = r.pct(60);
    UserPattern user = drawUser(r);
    UserPattern userEdit = drawUser(r);
    const int userEditAt = r.in(1, total - 1);
    const bool doUserEdit = r.pct(15);
    Scale scale{r.in(0, 11), static_cast<std::uint16_t>(r.next() & 0xFFF)};

    // the input: key presses (some id-less, some with ids that repeat), releases, pedal, panic, pass-through
    std::vector<Timed> ins;
    const int nIn = r.in(1, 36);
    for (int i = 0; i < nIn; ++i) {
      Timed t;
      t.at = r.in(0, total - 1);
      const int what = r.in(0, 9);
      const int key = r.pct(97) ? r.in(0, 127) : r.in(-4, 135);
      const int ch = r.in(0, 15);
      const int idKind = r.in(0, 2);
      const int id = idKind == 0 ? 0 : idKind == 1 ? r.in(1, 6) : 100 + i;   // id-less, a small set of repeating ids, a unique id
      const int vel = r.in(1, 127);
      if (what < 5) {
        t.ev.type = EventType::NoteOn;
        t.ev.key = key;
        t.ev.vel = vel;
        t.ev.ref = NoteRef{0, ch, key, id};
      } else if (what < 8) {
        t.ev.type = EventType::NoteOff;
        t.ev.ref = NoteRef{0, ch, key, id};
      } else if (what == 8) {
        t.ev.type = r.pct(50) ? EventType::Pedal : EventType::Panic;
        t.ev.down = r.pct(50);
      } else {
        t.ev.type = EventType::Other;
        t.ev.data[0] = i;
      }
      ins.push_back(t);
    }
    // a burst of presses at one sample (a big chord), released later
    if (r.pct(40)) {
      const std::int64_t at = r.in(0, total / 2);
      const int n = r.in(3, 14), base = r.in(10, 100), span = r.in(1, 3), ch0 = r.in(0, 15);
      for (int i = 0; i < n; ++i) {
        Timed t;
        t.at = at;
        t.ev.type = EventType::NoteOn;
        t.ev.key = base + i * span;
        t.ev.vel = 90;
        t.ev.ref = NoteRef{0, (ch0 + i) % 16, t.ev.key, 200 + i};
        ins.push_back(t);
        Timed o;
        o.at = std::min<std::int64_t>(total - 1, at + r.in(1, total / 2));
        o.ev.type = EventType::NoteOff;
        o.ev.ref = t.ev.ref;
        ins.push_back(o);
      }
    }
    // a stutter: presses on CONSECUTIVE samples, each released at once. On a stopped transport (the free clock) every press starts the pattern over at its own
    // sample, so the notes come one sample apart: the only way to put two arp ons exactly one sample apart, which is where T11's "born in the previous sample"
    // boundary sits (a mutant that moved it passed the suite and the digest until this motif existed).
    if (r.pct(45)) {
      const std::int64_t at = r.in(0, total / 2);
      const int n = r.in(2, 8), base = r.in(20, 100), ch0 = r.in(0, 15);
      for (int i = 0; i < n; ++i) {
        Timed t;
        t.at = at + i;
        t.ev.type = EventType::NoteOn;
        t.ev.key = base + i;
        t.ev.vel = 100;
        t.ev.ref = NoteRef{0, (ch0 + i) % 16, t.ev.key, 300 + i};
        ins.push_back(t);
        Timed o;
        o.at = at + i + (r.pct(30) ? 1 : 0);
        o.ev.type = EventType::NoteOff;
        o.ev.ref = t.ev.ref;
        ins.push_back(o);
      }
    }
    std::stable_sort(ins.begin(), ins.end(), [](const Timed& a, const Timed& b) { return a.at < b.at; });

    // transport events and the other mid-session happenings, at block boundaries (sample positions drawn here, applied when a block starts at or after them)
    struct Op { std::int64_t at; int kind; int arg; };   // 0 stop, 1 start, 2 jump to arg quarter beats, 3 tempo arg bpm, 4 reset(), 5 prepare(), 6 settings change, 7 time signature
    std::vector<Op> ops;
    const int nOps = r.in(0, 7);
    for (int i = 0; i < nOps; ++i) {
      Op o;
      o.at = r.in(1, total - 1);
      o.kind = r.in(0, 7);
      o.arg = o.kind == 2 ? r.in(0, 40) : o.kind == 3 ? kBpm[r.in(0, 8)] : o.kind == 7 ? r.in(1, 9) : r.in(0, 1 << 20);
      ops.push_back(o);
    }
    std::stable_sort(ops.begin(), ops.end(), [](const Op& a, const Op& b) { return a.at < b.at; });
    Settings changed;
    drawSettings(r, changed);
    const std::uint32_t changeMask = r.next();
    int bpm = kBpm[r.in(0, 8)];
    bool playing = !r.pct(40);
    const bool usesRep = !r.pct(10);   // else the floating `beats` fallback, at exact quarter beats
    const bool loops = r.pct(15);
    const int loopBeats = r.in(1, 6);
    int tsNum = 4, tsDen = 4;
    const std::uint32_t blockSeed = r.next();

    auto core = std::make_unique<Core>(caps);
    if (hasUser) core->setUserPattern(user);
    core->prepare(sr, 4096);
    Rng blocks(blockSeed);
    Fnv fs;
    std::uint64_t nEv = 0;
    std::int64_t pos = 0;           // absolute sample of the next block
    I128 rep = I128{};              // the host position, in posNum units (ticks x D)
    std::size_t ii = 0, oi = 0;
    int epoch = 0;
    std::vector<Event> inb;
    while (pos < total) {
      int n = blocks.pct(50) ? kBlocks[blocks.in(0, 5)] : blocks.in(1, 4096);
      n = static_cast<int>(std::min<std::int64_t>(n, total - pos));
      // operations due at or before this block's start
      while (oi < ops.size() && ops[oi].at <= pos) {
        const Op& o = ops[oi++];
        switch (o.kind) {
          case 0: playing = false; break;
          case 1: playing = true; break;
          case 2: rep = I128::mul(static_cast<std::int64_t>(o.arg) * (kTpq / 4), static_cast<std::int64_t>(60) * sr * 1000); break;
          case 3: bpm = o.arg; break;
          case 4: { const EventSpan offs = core->reset(); for (const Event& e : offs) { hashEvent(fs, pos, epoch, e); ++nEv; } ++epoch; break; }
          case 5: core->prepare(sr, 4096); break;
          case 6: {
            // One statement per line: g++'s -Wmisleading-indentation (an error under -Werror) rejects several on one line.
            if (changeMask & 1) st.order = changed.order;
            if (changeMask & 2) st.pattern = changed.pattern;
            if (changeMask & 4) st.rate = changed.rate;
            if (changeMask & 8) st.gate = changed.gate;
            if (changeMask & 16) st.latch = changed.latch;
            if (changeMask & 32) st.enabled = changed.enabled;
            if (changeMask & 64) {
              st.chordEnabled = changed.chordEnabled;
              for (int k = 0; k < 6; ++k) { st.chordOn[k] = changed.chordOn[k]; st.chordShift[k] = changed.chordShift[k]; }
            }
            if (changeMask & 128) st.rateMode = changed.rateMode;
            if (changeMask & 256) st.restart = changed.restart;
            if (changeMask & 512) st.arpEnabled = changed.arpEnabled;
            if (changeMask & 1024) { st.quantize = changed.quantize; st.scaleSource = changed.scaleSource; }
            if (changeMask & 2048) st.repeats = changed.repeats;
            break;
          }
          default: tsNum = o.arg % 9 + 1; tsDen = (o.arg % 2) ? 8 : 4; break;
        }
      }
      if (doUserEdit && userEditAt >= pos && userEditAt < pos + n) core->setUserPattern(userEdit);
      inb.clear();
      while (ii < ins.size() && ins[ii].at < pos + n) {
        Event e = ins[ii].ev;
        e.offset = static_cast<std::int32_t>(ins[ii].at - pos);
        inb.push_back(e);
        ++ii;
      }
      if (loops && playing && rep >= I128::mul(static_cast<std::int64_t>(loopBeats) * kTpq, static_cast<std::int64_t>(60) * sr * 1000)) rep = I128{};   // a loop wrap, at a block start
      Block b;
      b.n = n;
      b.settings = st;
      b.scale = scale;
      b.in = std::span<const Event>(inb.data(), inb.size());
      b.transport.playing = playing;
      b.transport.hasTempo = true;
      b.transport.tempo = bpm;
      b.transport.timeSigNum = tsNum;
      b.transport.timeSigDen = tsDen;
      b.transport.hasBeats = true;
      if (usesRep) {
        b.transport.hasRep = true;
        b.transport.rep = rep;
      } else {
        b.transport.hasRep = false;
        const std::int64_t quarters = rep.floorDiv(static_cast<std::int64_t>(60) * sr * 1000 * (kTpq / 4));   // whole quarter beats: k * 0.25 is exact in a double
        b.transport.beats = static_cast<double>(quarters) * 0.25;
      }
      const EventSpan res = core->process(b);
      for (const Event& e : res) { hashEvent(fs, pos, epoch, e); ++nEv; }
      for (const StepRecord& s : core->lastSteps()) hashStep(fs, pos, s);
      if (playing) {
        const std::int64_t milli = static_cast<std::int64_t>(std::max(20, std::min(999, bpm))) * 1000;   // the core clamps the tempo the same way
        rep += I128::mul(n, milli * kTpq);
      }
      pos += n;
    }
    const EventSpan tail = core->reset();
    for (const Event& e : tail) { hashEvent(fs, total, epoch, e); ++nEv; }
    Fnv fc;
    hashCounters(fc, core->counters());
    DigestEntry e;
    e.kind = 'F';
    e.sr = sr;
    e.block = 0;   // varies per block
    e.total = total;
    e.events = nEv;
    e.stream = fs.h;
    e.counters = fc.h;
    d.entries.push_back(e);
  }
}

}  // namespace arp_test
