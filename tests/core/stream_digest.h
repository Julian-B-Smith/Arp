// stream_digest.h - an independent reference for "the output did not change" (D-030, T-007 M3b).
//
// Why it exists: the CPU optimisation of core.cpp must leave the event stream BYTE-IDENTICAL. The invariant suite proves the rules still
// hold; this proves nothing moved at all. Every event the core emits (every field, raw, before any test normaliser can drop one: L0002),
// every step record and the final counters go into a 64-bit FNV-1a hash, one hash per run. The hashes are committed in
// tests/core/stream_digest.txt (the suite's sessions) and tests/core/stream_digest_bench.txt (the benchmark's scenarios), generated from the
// core.cpp recorded in each file's header BEFORE it was optimised, so a later core is held to the pre-optimisation core, not to itself.
//
// Header-only and core-free (it takes arp::Event & co. as plain data) so tests/core/bench.cpp, which links the SHIPPED library, shares the
// exact same hasher as the plant-enabled suite.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "arp/types.h"

namespace arp_test {

// FNV-1a 64 over explicit little-endian bytes: never over a struct's memory (padding bytes are indeterminate), so the digest is the same on
// every compiler, on both int128 paths and across -O levels.
struct Fnv {
  std::uint64_t h = 14695981039346656037ull;
  void byte(std::uint8_t b) { h = (h ^ b) * 1099511628211ull; }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) byte(static_cast<std::uint8_t>(v >> (8 * i))); }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
};

// Every field of arp::Event (types.h). A field added to Event but not here would escape the digest: tests/core/test_digest.cpp perturbs each
// one in turn and requires the hash to move, and sizeof(Event) is pinned there too, so adding a field fails that check until it is hashed.
inline void hashEvent(Fnv& f, std::int64_t absBase, int epoch, const arp::Event& e) {
  f.i64(absBase + e.offset);   // the absolute sample (block start + offset): where the event lands, whatever the block partition
  f.i64(epoch);
  f.u64(static_cast<std::uint64_t>(e.type));
  f.i64(e.ref.port);
  f.i64(e.ref.channel);
  f.i64(e.ref.key);
  f.i64(e.ref.id);
  f.i64(e.key);
  f.i64(e.vel);
  f.u64(static_cast<std::uint64_t>(e.stream));
  f.u64(e.down ? 1 : 0);
  f.i64(e.lane);
  for (int i = 0; i < 3; ++i) f.i64(e.data[i]);
  f.i64(e.tick);
  f.i64(e.stepSeq);
}

inline void hashStep(Fnv& f, std::int64_t absBase, const arp::StepRecord& s) {
  f.i64(absBase + s.offset);
  f.i64(s.kAbs);
  f.i64(s.k);
  f.i64(s.mBefore);
  f.i64(s.mAfter);
  f.u64(s.played ? 1 : 0);
}

inline void hashCounters(Fnv& f, const arp::Counters& c) {
  for (std::int64_t v : {c.heldOverflow, c.refOverflow, c.soundingOverflow, c.pendingOverflow, c.eventOverflow, c.refused, c.keysDropped, c.badInput, c.stepLogDropped, c.offsLost}) f.i64(v);
  for (int i = 0; i < arp::kRowCount; ++i) f.i64(c.rows[i]);
}

inline std::string hex16(std::uint64_t v) {
  char b[17];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
  return b;
}

}  // namespace arp_test
