// test_evict.cpp - T11's eviction boundary, as a known answer (M3b rework, D-030 critic MEDIUM-1).
//
// A full sounding table evicts its oldest note before a new on, but never a note born in the SAME sample (SPEC 1.1: no off at its own on's sample).
// A note born one sample EARLIER is evictable: its off goes out at the new on's sample, which is its on + 1. A mutant that read "the victim must be two
// samples old" passed the whole suite and the digest, because no session puts two arp ons exactly one sample apart (the finest grid step is 30 samples at
// 8 kHz and 999 bpm). This drives emitOn directly, at chosen samples, to pin that boundary: 0 samples apart (refused), 1 apart (evicted), 2 apart (evicted).
//
// emitOn is private and takes a queued Pending, which no public input can place one sample after another: the bench does the same access trick
// (`private` made `public` around the one include of core.h, measurement and test only; the layout does not change on the compilers we use).
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
#include "test_evict.h"

namespace arp_test {
namespace {

using namespace arp;

struct Outcome { std::string events; int evictions = 0, refused = 0; };

// A table of ONE arp note. A on key 60 at abs sample 105 (block offset 5 of a block that starts at 100); B on key 62 `gap` samples later.
template <class P>
Outcome evictAfterGap(int gap, bool mutant) {
  Caps caps;
  caps.sounding = 1;
  auto core = std::make_unique<Core>(caps);
  bool known = true;
  P& pl = core->plants;   // through P so the member is a dependent name: a core without the plant still compiles this
  if constexpr (requires { pl.evictBornPrevSample; }) pl.evictBornPrevSample = mutant; else known = !mutant;
  Outcome o;
  if (!known) { o.events = "plant not in this core"; return o; }
  core->prepare(8000, 64);
  core->absBase_ = 100;
  core->n_ = 64;
  core->inc_ = 1;   // ofs() divides by it; the offs below are far in the future either way
  core->outN_ = 0;
  Core::Pending a, b;
  a.key = 60; b.key = 62; a.vel = b.vel = 100;
  a.pos = b.pos = I128::fromInt(0);
  a.offPos = b.offPos = I128::fromInt(1000000);
  a.ord = 0; b.ord = 1;
  core->emitOn(a, 5);
  core->emitOn(b, static_cast<std::int32_t>(5 + gap));
  for (int i = 0; i < core->outN_; ++i) {
    const Event& e = core->out_[static_cast<std::size_t>(i)];
    o.events += std::string(e.type == EventType::NoteOn ? "on" : "off") + std::to_string(e.type == EventType::NoteOn ? e.key : e.ref.key) + "@" + std::to_string(e.offset) + " ";
  }
  o.evictions = static_cast<int>(core->counters_.rows[11]);
  o.refused = static_cast<int>(core->counters_.refused);
  return o;
}

}  // namespace

void runEvictTests() {
  std::puts("T11 eviction boundary: a note born one sample earlier is evictable, one born this sample is not");
  const Outcome same = evictAfterGap<Plants>(0, false), one = evictAfterGap<Plants>(1, false), two = evictAfterGap<Plants>(2, false);
  check(same.events == "on60@5 " && same.refused == 1 && same.evictions == 0,
        "known answer (SPEC 1.1, T11): a table of one, the new on in the SAME sample as the old on: the old note cannot go, the new on is refused (read \"" + same.events + "\")");
  check(one.events == "on60@5 off60@6 on62@6 " && one.evictions == 1 && one.refused == 0,
        "known answer (SPEC 1.1, T11): the old note's on one sample earlier: it is evicted, its NoteOff at the new on's sample, before it (read \"" + one.events + "\")");
  check(two.events == "on60@5 off60@7 on62@7 " && two.evictions == 1 && two.refused == 0,
        "known answer (T11): two samples earlier: evicted (read \"" + two.events + "\")");
  const Outcome mutant = evictAfterGap<Plants>(1, true);
  if (mutant.events == "plant not in this core") return;   // the pre-optimisation core the digests are generated from (tools/core_digest.sh) has no plants of M3b
  plant(mutant.events != one.events && mutant.refused == 1, "T11 detects an eviction test that wants the victim two samples old (the one-sample-old note survives and the new on is refused; read \"" + mutant.events + "\")");
}

}  // namespace arp_test
