// goldens.cpp - the bank-pattern goldens (D-046, T-007): what the SHIPPED core plays for each step pattern of spec/bank/starter.json, pinned.
//
// Why it exists: the stream digests (stream_digest*.txt) pin the suite's and the benchmark's sessions, which use only a few of the bank's
// patterns. These pin every bank pattern by name, one file each (tests/core/goldens/<id>.txt), so a change that moves what one pattern plays
// reads red under that pattern's name, with a human-readable excerpt that shows what moved.
//
// `arp_goldens OUTDIR` writes the files' bodies (no provenance line); tools/core_test.sh diffs them against the committed files, '#' lines
// ignored, on both int128 paths. tools/core_golden.sh cut is the only thing that rewrites the committed files (a deliberate re-cut, recorded).
// `--perturb ID` is the must-fail control: it changes one field of one event of pattern ID in what is HASHED AND PRINTED (never in the core),
// and the check must name that file and only that file.
//
// The scenario (the same for every pattern, so the files differ only by what the pattern does): a held 4-note chord, 120 bpm, rate 1/16 in sync
// mode, gate 80, seed kSeed, swing 500 (each of kOrders, three sample rates, two block sizes) and swing kSwing (order up, 48 kHz). The chord is
// pressed at sample 0; three of its keys are released at 6.94 s, inside step 55 (index 7 of an 8- or 16-step pattern, 3 of a 4-step one, 1 of offbeat): roll_end's
// ratchet-4 step, a tie of tied_legato, a probability step of sparse_chance, octave_leap's -1 step. The top key stays held, so the arp goes on
// over one key (a held-chord change) until 7.95 s, inside the gate of step 63's note, so reset() has a sounding note to end. So it pins steady play (3+ cycles of a 16-step pattern),
// a release that lands on each pattern's busiest step, the play after it, and the reset's terminals.
// The run's length is fixed in samples, not blocks, so the two block sizes of one (order, rate) must give the same line but for the block column.
// Links the shipped library (no ARP_PLANTS): there is no fault switch in it to leave on by accident.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "stream_digest.h"
#include "arp/bank_data.h"
#include "arp/core.h"

#ifdef ARP_PLANTS
#error "the goldens pin the shipped library: build without ARP_PLANTS"
#endif

using namespace arp;
using arp_test::Fnv;
using arp_test::hashCounters;
using arp_test::hashEvent;
using arp_test::hashStep;
using arp_test::hex16;

namespace {

constexpr std::uint32_t kSeed = 20261029;   // nonzero, so `random` and the probability steps of sparse_chance draw from a seed of our choosing
constexpr Order kOrders[] = {Order::Up, Order::Bounce, Order::Random};
constexpr int kSwing = 600;   // a swung run per pattern: off-beat steps and ratchet sub-notes are placed by swing (bank patterns have no other swing pin)
constexpr int kRates[] = {44100, 48000, 96000};
constexpr int kBlocks[] = {61, 4096};   // an odd size (offsets never land on a power-of-two boundary) and the largest
constexpr int kKeys[] = {60, 64, 67, 71};
constexpr int kVels[] = {100, 90, 80, 70};   // distinct, so a velocity-shaping pattern (accent_one) shows on each key
constexpr int kBpm = 120;
constexpr int kExcerptMax = 64;   // excerpt lines per part, a bound for the reviewer, not a rule of the core (a cut part says so)

const char* typeName(EventType t) {
  switch (t) {
    case EventType::NoteOn: return "on";
    case EventType::NoteOff: return "off";
    case EventType::Pedal: return "pedal";
    case EventType::Panic: return "panic";
    case EventType::Trigger: return "trig";
    case EventType::Other: return "other";
  }
  return "?";
}

std::string eventLine(std::int64_t abs, const Event& e) {
  char b[256];
  const long long tick = e.tick == kNoTick ? -1 : static_cast<long long>(e.tick);
  std::snprintf(b, sizeof b, "ev %lld %s key=%d vel=%d lane=%d stream=%s ref=%lld tick=%lld step=%lld", static_cast<long long>(abs), typeName(e.type), e.key, e.vel, e.lane,
                e.stream == Stream::Chord ? "chord" : "arp", static_cast<long long>(e.ref.id), tick, static_cast<long long>(e.stepSeq));
  return b;
}

struct RunOut {
  std::string line;                    // the run's summary line
  std::vector<std::string> head, tail;  // the excerpt: the first bar, and everything from the release on
  bool headCut = false, tailCut = false;
};

// One run. `perturb`: add 1 to the velocity of output event #3 in what is hashed and printed (the plant; the core is untouched).
RunOut runOne(int pattern, Order order, int swing, int sr, int block, bool perturb) {
  auto core = std::make_unique<Core>();
  if (!core->prepare(sr, block)) { std::fprintf(stderr, "prepare(%d, %d) refused\n", sr, block); std::exit(2); }
  Settings st;
  st.enabled = true;
  st.pattern = static_cast<Pattern>(pattern);
  st.order = order;
  st.rate = Rate::R1_16;
  st.rateMode = RateMode::Sync;
  st.gate = 80;
  st.swing = swing;
  st.seed = kSeed;
  const std::int64_t total = static_cast<std::int64_t>(sr) * 795 / 100;   // 7.95 s: mid-note (step 63 starts at 7.875 s, its gate ends at 7.975 s)
  const std::int64_t release = static_cast<std::int64_t>(sr) * 694 / 100;   // 6.94 s, inside step 55: integer-exact at every rate
  const std::int64_t bar = static_cast<std::int64_t>(sr) * 2;   // 16 steps of 1/16 at 120 bpm: a whole cycle of the longest bank pattern
  const std::int64_t inc = static_cast<std::int64_t>(kBpm) * 1000 * kTpq;   // posNum per sample (exact, D cancels: tests/core/shipped_smoke.cpp)
  Fnv fs, fsteps;   // events and step records in two hashes: one hash over both would depend on how a block interleaves them, i.e. on the block size
  std::int64_t nEv = 0, ons = 0, offs = 0, trigs = 0;
  RunOut out;
  auto take = [&](std::int64_t base, Event e) {
    if (perturb && nEv == 3) e.vel += 1;
    hashEvent(fs, base, 0, e);
    ++nEv;
    ons += e.type == EventType::NoteOn;
    offs += e.type == EventType::NoteOff;
    trigs += e.type == EventType::Trigger;
    const std::int64_t abs = base + e.offset;
    if (abs < bar) { if (out.head.size() < kExcerptMax) out.head.push_back(eventLine(abs, e)); else out.headCut = true; }
    if (abs >= release) { if (out.tail.size() < kExcerptMax) out.tail.push_back(eventLine(abs, e)); else out.tailCut = true; }
  };
  std::vector<Event> in;
  for (std::int64_t base = 0; base < total; base += block) {
    const int n = static_cast<int>(std::min<std::int64_t>(block, total - base));
    in.clear();
    for (int i = 0; i < 4; ++i) {
      if (base == 0) { Event e; e.type = EventType::NoteOn; e.ref.id = i + 1; e.key = kKeys[i]; e.vel = kVels[i]; in.push_back(e); }
      if (i < 3 && release >= base && release < base + n) { Event e; e.type = EventType::NoteOff; e.ref.id = i + 1; e.offset = static_cast<std::int32_t>(release - base); in.push_back(e); }
    }
    Block b;
    b.n = n;
    b.settings = st;
    b.scale = Scale{0, 0xAB5};   // horde's global scale, C major (read only when quantizing; the bank patterns do not)
    b.transport.playing = true;
    b.transport.hasBeats = true;
    b.transport.hasRep = true;
    b.transport.rep = I128::mul(base, inc);
    b.transport.hasTempo = true;
    b.transport.tempo = kBpm;
    b.in = std::span<const Event>(in.data(), in.size());
    for (const Event& e : core->process(b)) take(base, e);
    for (const StepRecord& r : core->lastSteps()) hashStep(fsteps, base, r);
  }
  for (const Event& e : core->reset()) take(total, e);   // the end: what the held top key still sounds is ended here
  Fnv fc;
  hashCounters(fc, core->counters());
  char b[256];
  std::snprintf(b, sizeof b, "run %s swing=%d %d %d events=%lld on=%lld off=%lld trig=%lld events-hash=%s steps-hash=%s counters-hash=%s", kOrderNames[static_cast<int>(order)], swing, sr, block, static_cast<long long>(nEv),
                static_cast<long long>(ons), static_cast<long long>(offs), static_cast<long long>(trigs), hex16(fs.h).c_str(), hex16(fsteps.h).c_str(), hex16(fc.h).c_str());
  out.line = b;
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const char* outDir = nullptr;
  const char* perturbId = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--perturb") == 0 && i + 1 < argc) perturbId = argv[++i];
    else if (argv[i][0] != '-' && outDir == nullptr) outDir = argv[i];
    else { std::fprintf(stderr, "usage: arp_goldens [--perturb PATTERN_ID] OUTDIR\n"); return 2; }
  }
  if (outDir == nullptr) { std::fprintf(stderr, "usage: arp_goldens [--perturb PATTERN_ID] OUTDIR\n"); return 2; }
  if (perturbId != nullptr) {
    bool known = false;
    for (int p = 0; p < kBankCount; ++p) known = known || std::strcmp(kBankIds[p], perturbId) == 0;
    if (!known) { std::fprintf(stderr, "arp_goldens: --perturb %s is not a bank pattern id\n", perturbId); return 2; }
  }
  for (int p = 0; p < kBankCount; ++p) {
    // the bank is indexed by the `pattern` enum (core.cpp static_asserts it); checked here too, since the file is named by the id and run by the enum
    if (std::strcmp(kBankIds[p], kPatternNames[p]) != 0) { std::fprintf(stderr, "arp_goldens: bank id %s is not pattern enum name %s\n", kBankIds[p], kPatternNames[p]); return 2; }
    const std::string path = std::string(outDir) + "/" + kBankIds[p] + ".txt";
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) { std::fprintf(stderr, "arp_goldens: cannot write %s\n", path.c_str()); return 2; }
    std::fprintf(f, "# arp bank-pattern golden v1 (tests/core/goldens.cpp, D-046): step pattern `%s` of spec/bank/starter.json, played by the SHIPPED core (no ARP_PLANTS).\n", kBankIds[p]);
    std::fprintf(f, "# scenario: keys 60/64/67/71 (velocities 100/90/80/70, host ids 1-4) pressed at sample 0, keys 1-3 released at 6.94 s, key 71 held; 120 bpm, rate 1/16 sync, gate 80, seed %u; 7.95 s, then reset() mid-note.\n", static_cast<unsigned>(kSeed));
    std::fprintf(f, "# run lines: order, swing, sample rate, block size, event counts, then 64-bit FNV-1a hashes of every field of every event, of every step record, and of the counters\n");
    std::fprintf(f, "#   (tests/core/stream_digest.h). The run's length is fixed in samples, so the two block sizes of one order and rate must read the same.\n");
    std::fprintf(f, "# ev lines: an excerpt of the run `up 48000 61` - the first bar (16 steps; this pattern is %d long), then every event from the release on (sample 333120) through reset().\n", kBank[p].length);
    std::fprintf(f, "#   Columns: sample, type, key, velocity, trigger lane, stream, output ref id (an off names its on by it), tick (-1: none), the step that produced it.\n");
    std::fprintf(f, "# Changing ANY line here needs a deliberate re-cut (tools/core_golden.sh cut), recorded with its decision.\n");
    const bool perturb = perturbId != nullptr && std::strcmp(perturbId, kBankIds[p]) == 0;
    RunOut excerpt;
    bool first = true;
    for (Order o : kOrders)
      for (int sr : kRates)
        for (int block : kBlocks) {
          RunOut r = runOne(p, o, 500, sr, block, perturb && first);
          first = false;
          std::fprintf(f, "%s\n", r.line.c_str());
          if (o == Order::Up && sr == 48000 && block == 61) excerpt = std::move(r);
        }
    for (int block : kBlocks) std::fprintf(f, "%s\n", runOne(p, Order::Up, kSwing, 48000, block, false).line.c_str());
    std::fprintf(f, "# the first bar\n");
    for (const std::string& l : excerpt.head) std::fprintf(f, "%s\n", l.c_str());
    if (excerpt.headCut) std::fprintf(f, "ev-cut the first bar has more than %d events\n", kExcerptMax);
    std::fprintf(f, "# from the release on\n");
    for (const std::string& l : excerpt.tail) std::fprintf(f, "%s\n", l.c_str());
    if (excerpt.tailCut) std::fprintf(f, "ev-cut more than %d events from the release on\n", kExcerptMax);
    if (std::ferror(f) != 0 || std::fclose(f) != 0) { std::fprintf(stderr, "arp_goldens: writing %s failed\n", path.c_str()); return 2; }
  }
  return 0;
}
