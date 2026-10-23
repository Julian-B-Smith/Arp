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
      // ---- M2b ----
      {"restartBarAsChord", &Plants::restartBarAsChord},       {"restartFreeAsChord", &Plants::restartFreeAsChord},
      {"repeatsCountPatternPasses", &Plants::repeatsCountPatternPasses}, {"repeatsNeverStop", &Plants::repeatsNeverStop},
      {"exhaustedStopsGuard", &Plants::exhaustedStopsGuard},   {"onlyGestureResets", &Plants::onlyGestureResets},
      {"morphRevives", &Plants::morphRevives},                 {"restartMorphKeepsM", &Plants::restartMorphKeepsM},
      {"restartMorphReanchors", &Plants::restartMorphReanchors}, {"morphSwitchCuts", &Plants::morphSwitchCuts},
      {"morphSwitchDropsPending", &Plants::morphSwitchDropsPending},
      {"chordNoDedupe", &Plants::chordNoDedupe},               {"chordEndsOnFirstSource", &Plants::chordEndsOnFirstSource},
      {"arpIgnoresChord", &Plants::arpIgnoresChord},           {"chordRestartAll", &Plants::chordRestartAll},
      {"chordRecomputeAtRelease", &Plants::chordRecomputeAtRelease}, {"t2AcrossStreams", &Plants::t2AcrossStreams},
      {"arpToggleEndsChord", &Plants::arpToggleEndsChord},     {"chordEndsAtEdge", &Plants::chordEndsAtEdge},
      {"sharedSoundingTable", &Plants::sharedSoundingTable},   {"chordSyncPerEvent", &Plants::chordSyncPerEvent},
      {"chordDedupeByChannelKey", &Plants::chordDedupeByChannelKey}, {"chordPlusZeroUnquantized", &Plants::chordPlusZeroUnquantized},
      {"chordSkipQuantize", &Plants::chordSkipQuantize},       {"chordReadsLastQ", &Plants::chordReadsLastQ}, {"chordWritesLastQ", &Plants::chordWritesLastQ},
      {"chordTaggedArp", &Plants::chordTaggedArp},             {"chordRootFromChordSet", &Plants::chordRootFromChordSet}, {"swingPerStep", &Plants::swingPerStep},
      // ---- M2b critic follow-up (D-027) ----
      {"chordSyncBlockStartFirst", &Plants::chordSyncBlockStartFirst}, {"adoptKeepsLastBar", &Plants::adoptKeepsLastBar},
      {"reanchorKeepsLastBar", &Plants::reanchorKeepsLastBar},         {"repeatsPauseWhileEmpty", &Plants::repeatsPauseWhileEmpty},
      {"keepSpilledRatchets", &Plants::keepSpilledRatchets},           {"swingLatchUnanchored", &Plants::swingLatchUnanchored},
      {"chordStalePressTime", &Plants::chordStalePressTime},           {"chordOnLate", &Plants::chordOnLate},
      {"chordOnTickSkew", &Plants::chordOnTickSkew},
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
  // The script that built this host, so G4 can replay it at another sample rate (positions in musical units stay put; sample positions scale).
  struct Op { int at; std::optional<bool> playing; std::optional<double> seekBeats; std::optional<double> bpm; };
  std::vector<Op> ops;
  double startBpm = 120, startBeats = 0;
  bool startPlaying = true;

  explicit SimHost(int sampleRate, double bpm = 120, double startBeats_ = 0, bool playing = true)
      : sr(sampleRate), D(static_cast<std::int64_t>(60) * sampleRate * 1000), startBpm(bpm), startBeats(startBeats_), startPlaying(playing) {
    segs.push_back(mk(0, playing, bpm, I128::mul(std::llround(startBeats_ * kTpq), D)));
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
    ops.push_back({at, playing, seekBeats, bpm});
    const Seg prev = segAt(at);
    const I128 pos = posAt(at);
    segs.push_back(mk(at, playing.value_or(prev.playing), bpm.value_or(prev.bpm), seekBeats ? I128::mul(std::llround(*seekBeats * kTpq), D) : pos));
    std::stable_sort(segs.begin(), segs.end(), [](const Seg& a, const Seg& b) { return a.start < b.start; });
  }
  // The same script at another sample rate, every sample position mapped through `sc`.
  std::shared_ptr<SimHost> rescaled(int newSr, const std::function<int(int)>& sc) const {
    auto h = std::make_shared<SimHost>(newSr, startBpm, startBeats, startPlaying);
    for (const Op& o : ops) h->set(sc(o.at), o.playing, o.seekBeats, o.bpm);
    return h;
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
  std::int64_t ord = 0;    // NoteOn: which evaluated step produced it (-1 for the chord stage)
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
  r.ord = e.stepSeq;
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
// A chord-stage NoteOn starts at the sample of the input (or block start) that caused it, so its tick is the floor of the position AT THAT
// SAMPLE, which another sample rate rounds differently (by a tick or so): it is left out of THIS comparison and held to an exact check of
// its own, chordCauses() below (its sample is the cause's, its tick the host's exact position there). The grid-bound events (the arp's
// notes, triggers) and every off's scheduled tick are compared as before.
inline std::string tickStreamOf(const Run& r) {
  std::ostringstream o;
  for (const Rec& e : r.evs) o << typeStr(e.t) << ' ' << e.lane << ' ' << e.key << ' ' << e.vel << ' ' << e.data0 << ' ' << refStr(e.ref) << ' ' << ((e.t == EventType::NoteOn && e.stream == Stream::Chord) ? kNoTick : e.tick) << '\n';
  return o.str();
}

// G4 / CH1 (critic L3): the chord stage is timed by its INPUTS, not by the grid, so at every sample rate each chord-stream NoteOn and NoteOff must land at
// exactly the sample of what caused it: an input event, a settings / scale change (offset 0 of its block), a reset or prepare(), or the final reset. `s` is the
// session AS RUN (already scaled to its sample rate, so its inputs sit where the rate puts them). `sig` names every chord event by its CAUSE's rank among the
// session's cause samples, so it must be identical at every rate; `err` is non-empty when an event sits at a sample that is no cause's (a NoteOff may follow
// its own NoteOn by the one sample SPEC 1.1 demands). While the host transport plays, a chord NoteOn's tick must also be the floor of the host's EXACT position
// at its sample (a stopped transport runs the free clock, whose position this does not model). Plant chordOnLate lands every on one sample after its cause.
struct ChordCauses { std::string sig, err; int events = 0, tickChecked = 0; };
inline ChordCauses chordCauses(const Session& s, const Run& r) {
  ChordCauses out;
  std::vector<std::int64_t> cause = {0, s.total};
  for (const InEv& i : s.ins) cause.push_back(i.at);
  for (const Change& c : s.cfg) cause.push_back(c.at);
  for (const auto& sc : s.scaleAt) cause.push_back(sc.first);
  for (int x : s.resets) cause.push_back(x);
  for (int x : s.prepares) cause.push_back(x);
  std::sort(cause.begin(), cause.end());
  cause.erase(std::unique(cause.begin(), cause.end()), cause.end());
  auto rank = [&](std::int64_t a) { const auto it = std::lower_bound(cause.begin(), cause.end(), a); return it != cause.end() && *it == a ? static_cast<int>(it - cause.begin()) : -1; };
  const std::shared_ptr<SimHost> host = s.host ? s.host : std::make_shared<SimHost>(s.sr, s.bpm);
  std::vector<std::pair<std::string, std::int64_t>> onAt;   // epoch / ref -> the sample of its chord NoteOn
  std::ostringstream sig;
  auto fail = [&](const std::string& m) { if (out.err.empty()) out.err = m; };
  for (const Rec& e : r.evs) {
    const std::string k = std::to_string(e.epoch) + "/" + refStr(e.ref);
    if (e.t == EventType::NoteOn && e.stream == Stream::Chord) {
      ++out.events;
      onAt.emplace_back(k, e.a);
      const int rk = rank(e.a);
      if (rk < 0) fail("chord on of key " + std::to_string(e.key) + " at sample " + std::to_string(e.a) + " is at no input's sample");
      if (host->segAt(static_cast<int>(e.a)).playing) {
        ++out.tickChecked;
        const std::int64_t want = host->posAt(static_cast<int>(e.a)).floorDiv(host->D);
        if (e.tick != want) fail("chord on of key " + std::to_string(e.key) + " at sample " + std::to_string(e.a) + " has tick " + std::to_string(e.tick) + ", the host is at " + std::to_string(want));
      }
      sig << "on " << e.key << ' ' << k << " @" << rk << '\n';
    } else if (e.t == EventType::NoteOff) {
      const auto it = std::find_if(onAt.begin(), onAt.end(), [&](const auto& p) { return p.first == k; });
      if (it == onAt.end()) continue;   // an arp note's off
      ++out.events;
      int rk = rank(e.a);
      bool late = false;
      if (rk < 0 && e.a - 1 == it->second && rank(e.a - 1) >= 0) { rk = rank(e.a - 1); late = true; }   // released inside its own on's sample: SPEC 1.1 defers the off by one
      if (rk < 0) fail("chord off of key " + std::to_string(e.key) + " at sample " + std::to_string(e.a) + " is at no input's sample");
      sig << "off " << e.key << ' ' << k << " @" << rk << (late ? "+1" : "") << '\n';
    }
  }
  out.sig = sig.str();
  return out;
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
