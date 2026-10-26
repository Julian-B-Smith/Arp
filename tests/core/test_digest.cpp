// test_digest.cpp - the stream-digest check (D-030, T-007 M3b): "the core's output did not move".
//
// The reference is tests/core/stream_digest.txt, generated (tools/core_digest.sh regen) from the pre-optimisation core and committed BEFORE the
// first optimisation. Every run() the whole suite makes, and one wide replay of each distinct session (harness.h digestRecord), must hash to the
// same 64-bit value as the committed line, on both int128 paths. The hasher itself is proven first: each field of an event, a step record and the
// counters must move the hash, and a planted one-event change in a real session must read red.
#include "test_digest.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <tuple>
#include <string>
#include <vector>

#include "digest_corpus.h"
#include "harness.h"

#ifndef ARP_DIGEST_FILE
#error "ARP_DIGEST_FILE must name tests/core/stream_digest.txt (tests/core/CMakeLists.txt defines it)"
#endif

namespace arp_test {
namespace {

// The hash covers every field by name; these pins make a NEW field in types.h fail here until it is hashed (and perturbed below).
static_assert(sizeof(arp::Event) == 80, "arp::Event changed: hash the new field in stream_digest.h and add it to the perturbation list below");
static_assert(sizeof(arp::StepRecord) == 48, "arp::StepRecord changed: hash the new field in stream_digest.h and add it to the perturbation list below");
static_assert(sizeof(arp::Counters) == (10 + arp::kRowCount) * sizeof(std::int64_t), "arp::Counters changed: hash the new field in stream_digest.h");

std::string lineOf(const DigestEntry& e) {
  char b[160];
  std::snprintf(b, sizeof b, "%c %d %d %d %llu %s %s", e.kind, e.sr, e.block, e.total, static_cast<unsigned long long>(e.events), hex16(e.stream).c_str(), hex16(e.counters).c_str());
  return b;
}

// The committed file holds one hash per kChunk consecutive runs (about 800 lines for ~25,000 runs), not one line per run: small enough to read and
// review. The chunk hash covers every column of every run in it. For the exact run that moved, ARP_DIGEST_DUMP=<path> writes the full per-run
// list (kept out of the tree) and two dumps - the pre- and post-change core - diff.
constexpr std::size_t kChunk = 32;

// The runs are compared as a SET: sorted by every column before they are chunked. The order a suite runs its sessions in is not the core's
// behaviour, and the language leaves some of it to the compiler (`f(a()) != f(b())` calls a() and b() in an order clang and g++ choose
// differently), so an ordered list made the same suite hash differently on macOS and on the Linux CI. Which runs exist, and what each produced, is
// what is pinned.
std::vector<DigestEntry> canonical(std::vector<DigestEntry> v) {
  std::sort(v.begin(), v.end(), [](const DigestEntry& a, const DigestEntry& b) {
    return std::tie(a.kind, a.sr, a.block, a.total, a.events, a.stream, a.counters) < std::tie(b.kind, b.sr, b.block, b.total, b.events, b.stream, b.counters);
  });
  return v;
}

std::uint64_t chunkHash(const std::vector<DigestEntry>& v, std::size_t from, std::size_t to) {
  Fnv f;
  for (std::size_t i = from; i < to; ++i) {
    f.byte(static_cast<std::uint8_t>(v[i].kind));
    f.i64(v[i].sr);
    f.i64(v[i].block);
    f.i64(v[i].total);
    f.u64(v[i].events);
    f.u64(v[i].stream);
    f.u64(v[i].counters);
  }
  return f.h;
}

struct Chunk { std::size_t runs = 0; std::uint64_t hash = 0; };

std::vector<Chunk> chunksOf(const std::vector<DigestEntry>& v) {
  std::vector<Chunk> c;
  for (std::size_t i = 0; i < v.size(); i += kChunk) c.push_back({std::min(kChunk, v.size() - i), chunkHash(v, i, std::min(v.size(), i + kChunk))});
  return c;
}

std::vector<Chunk> readFile(const std::string& path, std::string& err) {
  std::vector<Chunk> v;
  std::ifstream in(path);
  if (!in) { err = "cannot open " + path; return v; }
  std::string l;
  while (std::getline(in, l)) {
    if (l.empty() || l[0] == '#') continue;
    unsigned long long runs = 0;
    char h[32] = {};
    if (std::sscanf(l.c_str(), "%llu %16s", &runs, h) != 2) { err = "unparsable line: " + l; return v; }
    v.push_back({static_cast<std::size_t>(runs), std::strtoull(h, nullptr, 16)});
  }
  return v;
}

// The comparison: chunk by chunk (the order of the suite's runs is fixed), so a mismatch names the runs. Returns one line per problem.
std::vector<std::string> compare(const std::vector<DigestEntry>& gotInRunOrder, const std::vector<Chunk>& want) {
  const std::vector<DigestEntry> got = canonical(gotInRunOrder);
  const std::vector<Chunk> have = chunksOf(got);
  std::vector<std::string> bad;
  if (have.size() != want.size()) bad.push_back("the suite made " + std::to_string(got.size()) + " recorded runs in " + std::to_string(have.size()) + " chunks, the digest has " + std::to_string(want.size()) + " chunks");
  for (std::size_t i = 0; i < have.size() && i < want.size(); ++i)
    if (have[i].runs != want[i].runs || have[i].hash != want[i].hash)
      bad.push_back("runs " + std::to_string(i * kChunk) + ".." + std::to_string(i * kChunk + have[i].runs - 1) + " differ from the digest (chunk " + std::to_string(i) + "; its first run is \"" + lineOf(got[i * kChunk]) + "\")");
  return bad;
}

arp::Event fullEvent() {
  arp::Event e;
  e.type = arp::EventType::NoteOn; e.offset = 17; e.ref = arp::NoteRef{1, 2, 3, 4}; e.key = 5; e.vel = 6; e.stream = arp::Stream::Chord;
  e.down = true; e.lane = 7; e.data[0] = 8; e.data[1] = 9; e.data[2] = 10; e.tick = 11; e.stepSeq = 12;
  return e;
}
using EventHasher = std::uint64_t (*)(const arp::Event&);
std::uint64_t evHash(const arp::Event& e) { Fnv f; hashEvent(f, 1000, 3, e); return f.h; }
// The PLANT for the field check: the real hash minus `data[2]` (copied by hand), a detector's blind spot made on purpose.
std::uint64_t evHashBlindToData2(const arp::Event& e) {
  arp::Event x = e;
  x.data[2] = 0;
  return evHash(x);
}

// Fields perturbed one at a time: the hash must move for every one. Returns the names of the fields that did NOT move it.
std::string unhashedEventField(EventHasher hash) {
  const arp::Event base = fullEvent();
  const std::uint64_t h0 = hash(base);
  struct P { const char* name; void (*f)(arp::Event&); };
  static const P perturb[] = {
      {"type", [](arp::Event& e) { e.type = arp::EventType::NoteOff; }},   {"offset", [](arp::Event& e) { e.offset += 1; }},
      {"ref.port", [](arp::Event& e) { e.ref.port += 1; }},                {"ref.channel", [](arp::Event& e) { e.ref.channel += 1; }},
      {"ref.key", [](arp::Event& e) { e.ref.key += 1; }},                  {"ref.id", [](arp::Event& e) { e.ref.id += 1; }},
      {"key", [](arp::Event& e) { e.key += 1; }},                          {"vel", [](arp::Event& e) { e.vel += 1; }},
      {"stream", [](arp::Event& e) { e.stream = arp::Stream::Arp; }},     {"down", [](arp::Event& e) { e.down = false; }},
      {"lane", [](arp::Event& e) { e.lane += 1; }},                        {"data[0]", [](arp::Event& e) { e.data[0] += 1; }},
      {"data[1]", [](arp::Event& e) { e.data[1] += 1; }},                  {"data[2]", [](arp::Event& e) { e.data[2] += 1; }},
      {"tick", [](arp::Event& e) { e.tick += 1; }},                        {"stepSeq", [](arp::Event& e) { e.stepSeq += 1; }},
  };
  std::string missed;
  for (const P& p : perturb) {
    arp::Event e = base;
    p.f(e);
    if (hash(e) == h0) missed += std::string(missed.empty() ? "" : ", ") + p.name;
  }
  return missed;
}

// A session fingerprint decides which sessions share a wide replay (harness.h digestRecord), so two sessions that DIFFER but share one lose a replay, silently.
// The check: runs with the same fingerprint at the same block size are the same run, so their output must be identical. A collision of differing sessions
// shows up as differing output under one fingerprint.
struct FpRun { std::uint64_t fp = 0; int block = 0; std::uint64_t events = 0, stream = 0, counters = 0; };

std::vector<std::string> fingerprintConflicts(std::vector<FpRun> runs) {
  std::sort(runs.begin(), runs.end(), [](const FpRun& a, const FpRun& b) { return std::tie(a.fp, a.block, a.events, a.stream, a.counters) < std::tie(b.fp, b.block, b.events, b.stream, b.counters); });
  std::vector<std::string> bad;
  for (std::size_t i = 1; i < runs.size(); ++i) {
    const FpRun& a = runs[i - 1];
    const FpRun& b = runs[i];
    if (a.fp == b.fp && a.block == b.block && (a.events != b.events || a.stream != b.stream || a.counters != b.counters))
      bad.push_back("fingerprint " + hex16(a.fp) + " at block " + std::to_string(a.block) + " gives two different outputs (" + hex16(a.stream) + " and " + hex16(b.stream) + ")");
  }
  return bad;
}

// The G5 band-edge sessions (test_core.cpp testG5): one jittered block mid-run, by 960, 961 and -961 ticks. They differ ONLY in that one value at position 10240.
Session bandEdgeSession(int ticks) {
  Session s;
  s.total = 20000;
  s.base.gate = 200;
  s.ins = {K(0, "a", 60)};
  s.jitter = [ticks](int pos) { return pos == 10240 ? ticks : 0; };
  return s;
}

}  // namespace

void runDigestTests() {
  std::puts("Stream digest: the output is byte-identical to the pre-optimisation core's");

  // -- the hasher: each field matters. Plant first (the charter): a hasher that ignores a field is a blind detector, shown on a copy that does.
  plant(unhashedEventField(evHashBlindToData2) == "data[2]", "a hasher blind to one field (data[2]) is named by the field-perturbation check");
  const std::string missed = unhashedEventField(evHash);
  check(missed.empty(), "every one of the 16 event fields moves the hash" + (missed.empty() ? std::string() : " - NOT hashed: " + missed));
  {
    arp::StepRecord s0;
    s0.offset = 3; s0.kAbs = 4; s0.k = 5; s0.mBefore = 6; s0.mAfter = 7; s0.played = true;
    auto h = [](const arp::StepRecord& s) { Fnv f; hashStep(f, 100, s); return f.h; };
    int moved = 0;
    arp::StepRecord s;
    s = s0; s.offset += 1; moved += h(s) != h(s0);
    s = s0; s.kAbs += 1; moved += h(s) != h(s0);
    s = s0; s.k += 1; moved += h(s) != h(s0);
    s = s0; s.mBefore += 1; moved += h(s) != h(s0);
    s = s0; s.mAfter += 1; moved += h(s) != h(s0);
    s = s0; s.played = false; moved += h(s) != h(s0);
    arp::Counters c0, c;
    auto hc = [](const arp::Counters& x) { Fnv f; hashCounters(f, x); return f.h; };
    int cmoved = 0;
    std::int64_t* const fields[] = {&c.heldOverflow, &c.refOverflow, &c.soundingOverflow, &c.pendingOverflow, &c.eventOverflow, &c.refused, &c.keysDropped, &c.badInput, &c.stepLogDropped, &c.offsLost};
    for (std::int64_t* f : fields) { c = c0; *f += 1; cmoved += hc(c) != hc(c0); }
    for (int i = 0; i < arp::kRowCount; ++i) { c = c0; c.rows[i] += 1; cmoved += hc(c) != hc(c0); }
    check(moved == 6 && cmoved == 10 + arp::kRowCount, "every step-record field (6) and every counter (" + std::to_string(10 + arp::kRowCount) + ") moves the hash");
  }

  // -- a planted one-event change in a real session reads red: `untaggedOn` re-tags the stream of SOME ons only (ref.id % 7 == 3), so one field of a few events differs.
  {
    DigestLog& d = digestLog();
    const bool was = d.enabled;
    d.enabled = false;   // these two sessions are the check's own: they are not part of the committed corpus
    Session s;
    s.sr = 8000; s.total = 16000;
    s.ins = {Kk(0, 60), Kk(0, 64), Kk(0, 67)};
    const Run clean = runOnce(s, 64);
    s.plants = {"untaggedOn"};
    const Run planted = runOnce(s, 64);
    s.plants = {};
    const Run again = runOnce(s, 7);
    d.enabled = was;
    plant(clean.digestStream != planted.digestStream, "one field of some events re-tagged (plant untaggedOn) changes the run's stream digest (" + hex16(clean.digestStream) + " -> " + hex16(planted.digestStream) + ")");
    check(clean.digestStream == again.digestStream && clean.digestCounters != 0, "known answer: the same session at block 64 and block 7 hashes identically (the stream digest is block-partition independent)");
  }

  recordDigestCorpus();   // the seeded random sessions (kind F), after everything the suite itself recorded

  // -- the session fingerprint tells differing sessions apart (D-032, critic MEDIUM-2). Plant first: the OLD fingerprint (the closures sampled at 7 positions) cannot
  //    tell the three band-edge sessions apart, and the conflict check reads red on it; the real one (every position) can, and is clean.
  {
    DigestLog& d = digestLog();
    const bool was = d.enabled;
    d.enabled = false;   // the check's own sessions are not part of the committed corpus
    std::vector<FpRun> blind, sharp;
    std::uint64_t firstStream = 0;
    bool outputsDiffer = false;
    for (int ticks : {960, 961, -961}) {
      const Session s = bandEdgeSession(ticks);
      const Run r = runOnce(s, 64, true);
      if (blind.empty()) firstStream = r.digestStream; else outputsDiffer = outputsDiffer || r.digestStream != firstStream;
      blind.push_back({sessionFingerprint(s, true), 64, r.nEvents, r.digestStream, r.digestCounters});
      sharp.push_back({sessionFingerprint(s), 64, r.nEvents, r.digestStream, r.digestCounters});
    }
    // the replay comparison has its own control: the two band-edge sessions the blind fingerprint merges, replayed at one common block size, differ
    const Run e0 = replayLean(bandEdgeSession(960), 64), e1 = replayLean(bandEdgeSession(961), 64), e0b = replayLean(bandEdgeSession(960), 64);
    plant(sessionFingerprint(bandEdgeSession(960), true) == sessionFingerprint(bandEdgeSession(961), true) && e0.digestStream != e1.digestStream && e0.digestStream == e0b.digestStream,
          "the repeat comparison (replay both at one block size) tells the band-edge sessions the blind fingerprint merges apart, and a session from itself");
    d.enabled = was;
    const std::vector<std::string> blindBad = fingerprintConflicts(blind);
    plant(outputsDiffer && !blindBad.empty(), "a fingerprint that samples the jitter at a few positions merges the band-edge sessions (960 / 961 / -961 ticks at position 10240) and the conflict check names it (" + (blindBad.empty() ? std::string("NOT NAMED") : blindBad.front()) + ")");
    check(sharp[0].fp != sharp[1].fp && sharp[1].fp != sharp[2].fp && sharp[0].fp != sharp[2].fp && fingerprintConflicts(sharp).empty(),
          "the session fingerprint calls the jitter at every position, so the three band-edge sessions are three sessions");
    std::vector<FpRun> all;
    std::size_t distinct = 0;
    std::vector<std::uint64_t> seen;
    for (const DigestEntry& e : digestLog().entries)
      if (e.kind == 'P' && e.fp != 0) {
        all.push_back({e.fp, e.block, e.events, e.stream, e.counters});
        if (std::find(seen.begin(), seen.end(), e.fp) == seen.end()) { seen.push_back(e.fp); ++distinct; }
      }
    const std::vector<std::string> bad = fingerprintConflicts(all);
    for (std::size_t i = 0; i < bad.size() && i < 5; ++i) std::printf("  fingerprint: %s\n", bad[i].c_str());
    check(bad.empty(), "no two runs with the same session fingerprint and block size differ in output: " + std::to_string(all.size()) + " runs of " + std::to_string(distinct) + " distinct sessions");
    // The same claim for repeats whose runs used DIFFERENT block sizes: every repeat of a fingerprint was replayed beside the first session under it at one common block size.
    for (std::size_t i = 0; i < digestLog().conflicts.size() && i < 5; ++i) std::printf("  fingerprint: %s\n", digestLog().conflicts[i].c_str());
    check(digestLog().conflicts.empty(), "every repeat of a session fingerprint replays identically to the first session under it at one common block size: " + std::to_string(digestLog().repeatsCompared) + " repeats compared");
  }

  // -- the comparison: a flipped hash and a missing chunk are caught, by name.
  std::string err;
  const std::vector<Chunk> want = readFile(ARP_DIGEST_FILE, err);
  const std::vector<DigestEntry>& got = digestLog().entries;
  if (const char* dump = std::getenv("ARP_DIGEST_DUMP")) {   // the full per-run list, for diffing two cores: a debugging aid, not part of the check
    std::ofstream out(dump, std::ios::trunc);
    const std::vector<DigestEntry> sorted = canonical(got);
    for (std::size_t i = 0; i < sorted.size(); ++i) out << i << ' ' << lineOf(sorted[i]) << "\n";
  }
  if (std::getenv("ARP_DIGEST_WRITE") != nullptr) {   // regeneration: ONLY from the core the digest is meant to describe (tools/core_digest.sh regen); the check then fails on purpose so it cannot stay switched on
    std::ofstream out(ARP_DIGEST_FILE, std::ios::trunc);
    out << "# arp stream digest v1 (tests/core/test_digest.cpp): one hash per " << kChunk << " consecutive runs (sorted by every column) of the invariant suite (kind P) and wide replays of its\n";
    out << "# distinct sessions (kind W). Each run is hashed over every event field, step record and counter (tests/core/stream_digest.h). Columns: runs-in-chunk chunk-hash.\n";
    if (const char* note = std::getenv("ARP_DIGEST_NOTE")) out << "# " << note << "\n";
    for (const Chunk& c : chunksOf(canonical(got))) out << c.runs << ' ' << hex16(c.hash) << "\n";
    check(false, "ARP_DIGEST_WRITE is set: wrote " + std::to_string(got.size()) + " runs to the digest file; rerun without it to check");
    return;
  }
  if (!err.empty()) { check(false, "stream digest: " + err); return; }
  if (want.size() > 2) {
    std::vector<Chunk> flipped = want;
    const std::size_t at = want.size() / 2;
    flipped[at].hash ^= 1;
    const auto p1 = compare(got, flipped);
    std::vector<Chunk> shorter = want;
    shorter.pop_back();
    const auto p2 = compare(got, shorter);
    const std::string named = "runs " + std::to_string(at * kChunk) + "..";
    const bool flippedNamed = std::any_of(p1.begin(), p1.end(), [&](const std::string& m) { return m.find(named) == 0; });   // (any: a core that ALSO differs elsewhere lists more)
    plant(flippedNamed && !p2.empty(), "a digest chunk with one hash bit flipped is named (runs " + std::to_string(at * kChunk) + "..), and a digest missing its last chunk is caught");
  }
  const std::vector<std::string> bad = compare(got, want);
  int shown = 0;
  for (const std::string& b : bad) if (shown++ < 5) std::printf("  digest: %s\n", b.c_str());
  if (bad.size() > 5) std::printf("  digest: ... and %zu more chunks\n", bad.size() - 5);
  std::size_t wide = 0, seeded = 0;
  for (const DigestEntry& e : got) { wide += e.kind == 'W'; seeded += e.kind == 'F'; }
  check(bad.empty(), "stream digest: all " + std::to_string(got.size()) + " recorded runs (" + std::to_string(got.size() - wide - seeded) + " the suite's own, " + std::to_string(wide) + " wide replays at block sizes 1/7/64/512/4096 and 44.1/48/96/192 kHz, " + std::to_string(seeded) + " seeded random sessions with tiny capacities) hash to the committed pre-optimisation digest - every event field, step record and counter");
}

}  // namespace arp_test
