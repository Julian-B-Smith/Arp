// harness.h - the test harness: a simulated EXACT host, a session runner that cuts blocks at every
// host edge and settings change (so the SAME session replays at any block size, SPEC G1), the event
// normaliser, and the N1 checker. Mirrors the lab's simHost / run() in tools/lab_smoke.mjs.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "alloc_hook.h"
#include "arp/core.h"
#include "check.h"

namespace arp_test {

using namespace arp;

// ---- plants, by name (the strings the checks and the report use) ----
#ifdef ARP_PLANTS
inline bool setPlant(Plants& p, const std::string& name) {
  struct Entry { const char* name; bool Plants::*flag; };
  static const Entry table[] = {
      {"dropRelease", &Plants::dropRelease},       {"doubleRelease", &Plants::doubleRelease},
      {"sameSampleOff", &Plants::sameSampleOff},   {"clampOffset", &Plants::clampOffset},
      {"floatPosition", &Plants::floatPosition},   {"roundNotCeil", &Plants::roundNotCeil},
      {"noOnceGuard", &Plants::noOnceGuard},       {"timeInDraw", &Plants::timeInDraw},
      {"noOffReserve", &Plants::noOffReserve},     {"allocInProcess", &Plants::allocInProcess},
      {"allocInReset", &Plants::allocInReset},     {"allocInSettings", &Plants::allocInSettings},
      {"srTickMath", &Plants::srTickMath},         {"latchAtNextStep", &Plants::latchAtNextStep},
      {"noLatchToggle", &Plants::noLatchToggle},   {"pedalIgnored", &Plants::pedalIgnored},
      {"owedIdCollision", &Plants::owedIdCollision},
      {"timeStepsWhileRinging", &Plants::timeStepsWhileRinging},
      {"playedTieByKeyDesc", &Plants::playedTieByKeyDesc},
      {"drawIgnoresEpoch", &Plants::drawIgnoresEpoch},
      {"orderTables", &Plants::orderTables},
      {"shuffleBoundaryRepeat", &Plants::shuffleBoundaryRepeat},
      {"advanceOnRest", &Plants::advanceOnRest},
      {"pickAdvancesM", &Plants::pickAdvancesM},
      {"ignorePick", &Plants::ignorePick},
      {"swapMidStep", &Plants::swapMidStep},
      {"userFirstWins", &Plants::userFirstWins},
      {"prepareResetsUser", &Plants::prepareResetsUser},
      {"transposeMerged", &Plants::transposeMerged},
      {"stepsZeroShifts", &Plants::stepsZeroShifts},
      {"stepSizeIgnored", &Plants::stepSizeIgnored},
      {"stepOffsetReadsGlobal", &Plants::stepOffsetReadsGlobal},
      {"tieAtTieStep", &Plants::tieAtTieStep},
      {"trigEveryRatchet", &Plants::trigEveryRatchet},
      {"probLE", &Plants::probLE},
      {"untaggedOn", &Plants::untaggedOn},
      {"timeUsesTempo", &Plants::timeUsesTempo},
      {"timeAnchorsTransport", &Plants::timeAnchorsTransport},
      {"regridAbsolute", &Plants::regridAbsolute},
      {"regridKAbs", &Plants::regridKAbs},
      {"timeBarsCount", &Plants::timeBarsCount},
      {"inactiveReanchors", &Plants::inactiveReanchors},
      {"timeEntryNoStart", &Plants::timeEntryNoStart},
      {"latchReplaceKeeps", &Plants::latchReplaceKeeps}, {"sameRefDropsPress", &Plants::sameRefDropsPress}, {"t11EvictsFirst", &Plants::t11EvictsFirst}, {"prepareForgetsNotes", &Plants::prepareForgetsNotes},
      // ---- M2a ----
      {"skipQuantizeChord", &Plants::skipQuantizeChord},       {"tieAlwaysDown", &Plants::tieAlwaysDown},
      {"ownIgnored", &Plants::ownIgnored},                     {"chordRootAsRoot", &Plants::chordRootAsRoot},
      {"lastQAtEmission", &Plants::lastQAtEmission},           {"lastQNoReset", &Plants::lastQNoReset},
      {"collisionKeepsLast", &Plants::collisionKeepsLast},     {"degreesAsSemitones", &Plants::degreesAsSemitones},
      {"stepDegreesAsSemitones", &Plants::stepDegreesAsSemitones}, {"semitoneAfterQuantize", &Plants::semitoneAfterQuantize},
      {"degreeBeforeQuantize", &Plants::degreeBeforeQuantize}, {"lastQPreMove", &Plants::lastQPreMove},
      {"droppedKeySetsLastQ", &Plants::droppedKeySetsLastQ},   {"noStage2Drop", &Plants::noStage2Drop},
      {"swingParityK", &Plants::swingParityK},                 {"timeIgnoresSwing", &Plants::timeIgnoresSwing},
      {"releaseKeepsPending", &Plants::releaseKeepsPending},
  };
  for (const Entry& e : table)
    if (name == e.name) { p.*(e.flag) = true; return true; }
  return false;
}
#endif

using Plant = std::vector<std::string>;   // the plants to turn on for a fresh core ({} = a clean core)

inline std::unique_ptr<Core> makeCore(const Caps& caps, const Plant& plants) {
  auto c = std::make_unique<Core>(caps);   // the core is ~50 KB of fixed tables: keep it off the stack
  for (const std::string& n : plants) {
#ifdef ARP_PLANTS
    if (!setPlant(c->plants, n)) { std::fprintf(stderr, "unknown plant %s\n", n.c_str()); std::abort(); }
#else
    (void)n;
    std::fprintf(stderr, "plants requested but the test target was built without ARP_PLANTS\n");
    std::abort();
#endif
  }
  return c;
}

// ---- the simulated exact host: position is an EXACT function of the absolute sample count ----
struct SimHost {
  struct Seg { int start; bool playing; double bpm; std::int64_t tm; I128 base; };
  int sr;
  std::int64_t D;
  std::vector<Seg> segs;

  explicit SimHost(int sampleRate, double bpm = 120, double startBeats = 0, bool playing = true)
      : sr(sampleRate), D(static_cast<std::int64_t>(60) * sampleRate * 1000) {
    segs.push_back(mk(0, playing, bpm, I128::mul(std::llround(startBeats * kTpq), D)));
  }
  static Seg mk(int start, bool playing, double bpm, I128 base) { return Seg{start, playing, bpm, std::llround(bpm * 1000), base}; }
  const Seg& segAt(int i) const {
    const Seg* g = &segs[0];
    for (const Seg& s : segs) if (s.start <= i) g = &s;
    return *g;
  }
  I128 posAt(int i) const {
    const Seg& g = segAt(i);
    return g.playing ? g.base + I128::mul(i - g.start, g.tm * kTpq) : g.base;
  }
  // A transport change at sample `at`: any of play/stop, seek (in beats), tempo.
  void set(int at, std::optional<bool> playing, std::optional<double> seekBeats = std::nullopt, std::optional<double> bpm = std::nullopt) {
    const Seg prev = segAt(at);
    const I128 pos = posAt(at);
    segs.push_back(mk(at, playing.value_or(prev.playing), bpm.value_or(prev.bpm), seekBeats ? I128::mul(std::llround(*seekBeats * kTpq), D) : pos));
    std::stable_sort(segs.begin(), segs.end(), [](const Seg& a, const Seg& b) { return a.start < b.start; });
  }
  Transport at(int i) const {
    const Seg& g = segAt(i);
    Transport t;
    t.playing = g.playing;
    t.hasBeats = true;
    t.hasRep = true;
    t.rep = posAt(i);
    t.hasTempo = true;
    t.tempo = g.bpm;
    return t;
  }
  std::vector<int> edges(int a, int b) const {
    std::vector<int> e;
    for (const Seg& s : segs) if (s.start > a && s.start < b) e.push_back(s.start);
    return e;
  }
};

// ---- input event builders ----
struct InEv { int at; Event ev; };
inline NoteRef refOf(const char* label) {
  NoteRef r;
  for (const char* p = label; *p; ++p) r.id = r.id * 31 + static_cast<unsigned char>(*p);
  return r;   // the id alone identifies an input ref in the tests
}
inline InEv K(int at, const char* label, int key, int vel = 100, int ch = 0) {
  Event e;
  e.type = EventType::NoteOn;
  e.ref = refOf(label);
  e.ref.channel = ch;
  e.key = key;
  e.vel = vel;
  return {at, e};
}
inline InEv O(int at, const char* label, int ch = 0) {
  Event e;
  e.type = EventType::NoteOff;
  e.ref = refOf(label);
  e.ref.channel = ch;
  return {at, e};
}
// A host that supplies NO note ids: the ref is (port 0, channel, key, id 0), so every press of a key carries the SAME ref.
inline InEv Kk(int at, int key, int vel = 100, int ch = 0) { InEv e = K(at, "", key, vel, ch); e.ev.ref = NoteRef{0, ch, key, 0}; return e; }
inline InEv Ok(int at, int key, int ch = 0) { InEv e = O(at, "", ch); e.ev.ref = NoteRef{0, ch, key, 0}; return e; }
inline InEv Ped(int at, bool down) { Event e; e.type = EventType::Pedal; e.down = down; return {at, e}; }
inline InEv Pan(int at) { Event e; e.type = EventType::Panic; return {at, e}; }
inline InEv Other(int at, int value) { Event e; e.type = EventType::Other; e.data[0] = value; return {at, e}; }

// ---- a session: one musical script, replayable at any block size ----
struct Change { int at; std::function<void(Settings&)> f; };
struct Session {
  int sr = 8000;
  int total = 0;
  int maxBlock = 4096;
  double bpm = 120;
  std::shared_ptr<SimHost> host;           // null = a plain playing host at `bpm`
  Settings base;                           // enabled = true unless the script says otherwise
  std::vector<Change> cfg;                 // settings changes, applied from sample `at` on
  std::vector<InEv> ins;
  std::vector<int> resets;                 // samples at which reset() is called between blocks
  std::vector<int> prepares;               // samples at which prepare() is called again between blocks (T10: it wipes the state)
  std::function<int(int)> jitter;          // host-position jitter in ticks, per block start
  std::function<void(int pos, Transport&)> tweak;   // edit the transport the host reports (e.g. no tempo, an absurd tempo)
  Scale scale{};
  std::vector<std::pair<int, Scale>> scaleAt;   // horde's global scale changes from sample `at` on (the last entry <= the block start wins)
  Caps caps{};
  Plant plants;
  bool finalReset = true;
  std::optional<UserPattern> userPattern;  // set BEFORE prepare (SPEC 4.6: the user pattern survives it)
  std::vector<std::pair<int, UserPattern>> userEdits;   // setUserPattern calls between blocks, at a sample, in list order
  std::function<void(Core&)> midBlockHook;  // called before every step is evaluated (stands for a writer thread landing mid-block)
  bool keyRefs = false;                    // rewrite every input NoteOn / NoteOff ref to the id-less (port, channel, key) form
  bool countAllocs = false;                // count operator-new calls made INSIDE process() / reset()
  std::function<void(int pos, int n, const Core&)> probe;   // called after each block
  Session() { base.enabled = true; }
};

struct Rec {
  std::int64_t a = 0;      // absolute sample
  EventType t = EventType::Other;
  int key = 0, vel = 0, lane = 0, data0 = 0, epoch = 0;
  NoteRef ref{};
  Stream stream = Stream::Arp;
  std::int64_t tick = kNoTick;
};
struct StepRec { std::int64_t a; StepRecord s; };

struct Run {
  std::vector<Rec> evs;
  std::vector<StepRec> steps;
  std::unique_ptr<Core> core;
  std::int64_t allocs = 0;
  int blocks = 0;
};

inline Rec norm(const Event& e, std::int64_t base, int epoch) {
  Rec r;
  r.a = base + e.offset;
  r.t = e.type;
  r.key = e.type == EventType::NoteOff ? e.ref.key : e.key;
  r.vel = e.vel;
  r.lane = e.lane;
  r.data0 = e.data[0];
  r.epoch = epoch;
  r.ref = e.ref;
  r.stream = e.stream;
  r.tick = e.tick;
  return r;
}

inline Run run(const Session& s, int block) {
  Run out;
  out.core = makeCore(s.caps, s.plants);
  Core& c = *out.core;
  if (s.userPattern) c.setUserPattern(*s.userPattern);
#ifdef ARP_PLANTS
  if (s.midBlockHook) c.plants.midBlockHook = [&c, &s] { s.midBlockHook(c); };
#endif
  if (!c.prepare(s.sr, s.maxBlock)) { std::fprintf(stderr, "harness: prepare(%d, %d) refused\n", s.sr, s.maxBlock); std::abort(); }
  const std::shared_ptr<SimHost> host = s.host ? s.host : std::make_shared<SimHost>(s.sr, s.bpm);
  std::vector<Change> cfg = s.cfg;
  std::stable_sort(cfg.begin(), cfg.end(), [](const Change& a, const Change& b) { return a.at < b.at; });
  std::vector<InEv> ins = s.ins;
  if (s.keyRefs) {   // a NoteOff carries no key, so learn each label's key from its NoteOn
    std::vector<std::pair<std::int64_t, int>> keyOf;
    for (const InEv& ie : ins) if (ie.ev.type == EventType::NoteOn) keyOf.emplace_back(ie.ev.ref.id, ie.ev.key);
    for (InEv& ie : ins) {
      if (ie.ev.type != EventType::NoteOn && ie.ev.type != EventType::NoteOff) continue;
      int key = ie.ev.key;
      if (ie.ev.type == EventType::NoteOff) for (const auto& kv : keyOf) if (kv.first == ie.ev.ref.id) key = kv.second;
      ie.ev.ref = NoteRef{0, ie.ev.ref.channel, key, 0};
    }
  }
  std::stable_sort(ins.begin(), ins.end(), [](const InEv& a, const InEv& b) { return a.at < b.at; });

  std::vector<int> cuts;
  for (int e : host->edges(0, s.total)) cuts.push_back(e);
  for (const Change& ch : cfg) cuts.push_back(ch.at);
  for (int r : s.resets) cuts.push_back(r);
  for (int r : s.prepares) cuts.push_back(r);
  for (const auto& ed : s.userEdits) cuts.push_back(ed.first);
  for (const auto& sc : s.scaleAt) cuts.push_back(sc.first);
  cuts.erase(std::remove_if(cuts.begin(), cuts.end(), [&](int x) { return x <= 0 || x >= s.total; }), cuts.end());
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

  int epoch = 0, pos = 0;
  std::size_t ci = 0;
  std::vector<Event> inb;
  inb.reserve(256);
  while (pos < s.total) {
    if (std::find(s.resets.begin(), s.resets.end(), pos) != s.resets.end()) {
      EventSpan offs;
      if (s.countAllocs) { AllocScope sc; offs = c.reset(); out.allocs += sc.count(); } else offs = c.reset();
      for (const Event& e : offs) out.evs.push_back(norm(e, pos, epoch));
      ++epoch;
    }
    if (std::find(s.prepares.begin(), s.prepares.end(), pos) != s.prepares.end() && !c.prepare(s.sr, s.maxBlock)) { std::fprintf(stderr, "harness: re-prepare refused\n"); std::abort(); }
    for (const auto& ed : s.userEdits) if (ed.first == pos) c.setUserPattern(ed.second);
    while (ci < cuts.size() && cuts[ci] <= pos) ++ci;
    const int n = std::min(block, (ci < cuts.size() ? cuts[ci] : s.total) - pos);
    inb.clear();
    for (const InEv& ie : ins)
      if (ie.at >= pos && ie.at < pos + n) { Event e = ie.ev; e.offset = ie.at - pos; inb.push_back(e); }
    Block b;
    b.n = n;
    b.transport = host->at(pos);
    if (s.jitter) b.transport.rep += I128::mul(s.jitter(pos), host->D);
    if (s.tweak) s.tweak(pos, b.transport);
    b.settings = s.base;
    for (const Change& ch : cfg) if (ch.at <= pos) ch.f(b.settings);
    b.scale = s.scale;
    for (const auto& sc : s.scaleAt) if (sc.first <= pos) b.scale = sc.second;
    b.in = std::span<const Event>(inb.data(), inb.size());
    EventSpan res;
    if (s.countAllocs) { AllocScope sc; res = c.process(b); out.allocs += sc.count(); } else res = c.process(b);
    if (static_cast<int>(res.size()) > c.eventCapacity()) check(false, "block exceeded the event cap");
    for (const Event& e : res) out.evs.push_back(norm(e, pos, epoch));
    for (const StepRecord& r : c.lastSteps()) out.steps.push_back({pos + r.offset, r});
    if (s.probe) s.probe(pos, n, c);
    ++out.blocks;
    pos += n;
  }
  if (s.finalReset) {
    EventSpan offs = c.reset();
    for (const Event& e : offs) out.evs.push_back(norm(e, s.total, epoch));
  }
  return out;
}

// ---- views and checkers over a run ----
inline std::vector<Rec> onsOf(const Run& r) {
  std::vector<Rec> v;
  for (const Rec& e : r.evs) if (e.t == EventType::NoteOn) v.push_back(e);
  return v;
}
inline std::string refStr(const NoteRef& r) {
  return std::to_string(r.port) + ":" + std::to_string(r.channel) + ":" + std::to_string(r.key) + ":" + std::to_string(r.id);
}
inline const char* typeStr(EventType t) {
  switch (t) {
    case EventType::NoteOn: return "on";
    case EventType::NoteOff: return "off";
    case EventType::Pedal: return "pedal";
    case EventType::Panic: return "panic";
    case EventType::Trigger: return "trig";
    default: return "pass";
  }
}
// The stream the block partition must not change (absolute samples).
inline std::string streamOf(const Run& r) {
  std::ostringstream o;
  for (const Rec& e : r.evs)
    o << e.a << ' ' << typeStr(e.t) << ' ' << e.lane << ' ' << e.key << ' ' << e.vel << ' ' << e.data0 << ' ' << refStr(e.ref) << ' ' << static_cast<int>(e.stream) << ' ' << e.epoch << '\n';
  return o.str();
}
// The stream the sample rate must not change (ticks, no samples).
inline std::string tickStreamOf(const Run& r) {
  std::ostringstream o;
  for (const Rec& e : r.evs) o << typeStr(e.t) << ' ' << e.lane << ' ' << e.key << ' ' << e.vel << ' ' << e.data0 << ' ' << refStr(e.ref) << ' ' << e.tick << '\n';
  return o.str();
}

// N1: exactly one NoteOff per NoteOn (same ref), at least one sample later.
inline std::vector<std::string> n1(const Run& r) {
  std::vector<std::string> bad;
  std::vector<std::pair<std::string, std::int64_t>> on;
  std::vector<std::pair<std::string, std::int64_t>> off;
  for (const Rec& e : r.evs) {
    const std::string k = std::to_string(e.epoch) + "/" + refStr(e.ref);
    if (e.t == EventType::NoteOn) on.emplace_back(k, e.a);
    else if (e.t == EventType::NoteOff) off.emplace_back(k, e.a);
  }
  for (std::size_t i = 0; i < on.size(); ++i) {
    int cnt = 0;
    std::int64_t at = 0;
    for (const auto& o : off) if (o.first == on[i].first) { ++cnt; at = o.second; }
    for (std::size_t j = 0; j < i; ++j) if (on[j].first == on[i].first) bad.push_back("duplicate on " + on[i].first);
    if (cnt == 0) bad.push_back("on without off " + on[i].first);
    else if (cnt > 1) bad.push_back("double off " + on[i].first);
    else if (at < on[i].second + 1) bad.push_back("off " + std::to_string(at - on[i].second) + " samples after its on " + on[i].first);
  }
  for (const auto& o : off) {
    bool seen = false;
    for (const auto& n : on) seen = seen || n.first == o.first;
    if (!seen) bad.push_back("off without on " + o.first);
  }
  return bad;
}

}  // namespace arp_test
