// shipped_smoke.cpp - links the SHIPPED library (core/ target `arp_core`, built WITHOUT ARP_PLANTS) and
// proves it works as shipped: no plant switches exist in it, and a small session keeps N1.
// It must not include anything from the plant-enabled test build: `Core` has a different layout there.
#include <cstdio>
#include <vector>

#include "arp/core.h"

int main() {
  using namespace arp;
  if (plantsCompiledIntoLibrary()) { std::puts("FAIL  the shipped library was built with ARP_PLANTS"); return 1; }
  Core core;
  if (!core.prepare(48000, 256)) { std::puts("FAIL  prepare refused"); return 1; }
  int ons = 0, offs = 0;
  for (int b = 0; b < 2000; ++b) {
    Block blk;
    blk.n = 256;
    blk.settings.enabled = true;
    blk.settings.gate = 150;
    blk.transport.playing = true;
    blk.transport.hasBeats = true;
    blk.transport.hasRep = true;
    blk.transport.rep = I128::mul(static_cast<std::int64_t>(b) * 256 * 120000 * kTpq, 1);   // 120 bpm: inc = 120000 * TPQ posNum per sample (exact, D cancels)
    blk.transport.hasTempo = true;
    blk.transport.tempo = 120;
    std::vector<Event> in;
    if (b == 0 || b == 1200) {
      for (int k : {60, 64, 67}) {
        Event e; e.type = EventType::NoteOn; e.key = k; e.vel = 100; e.ref.id = k + (b == 0 ? 0 : 1000); e.offset = 3; in.push_back(e);
      }
    }
    if (b == 1000 || b == 1800) {
      for (int k : {60, 64, 67}) {
        Event e; e.type = EventType::NoteOff; e.ref.id = k + (b == 1000 ? 0 : 1000); e.offset = 5; in.push_back(e);
      }
    }
    blk.in = std::span<const Event>(in.data(), in.size());
    for (const Event& e : core.process(blk)) { ons += e.type == EventType::NoteOn; offs += e.type == EventType::NoteOff; }
  }
  for (const Event& e : core.reset()) offs += e.type == EventType::NoteOff;
  const bool ok = ons > 50 && ons == offs && core.counters().offsLost == 0;
  std::printf("%s  shipped library: %d NoteOns, %d NoteOffs, no plants compiled in\n", ok ? "PASS" : "FAIL", ons, offs);
  return ok ? 0 : 1;
}
