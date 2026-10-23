#pragma once
#include "harness.h"
namespace arp_test {
void runM2aTests();
void applyM2aDraws(Session& s, std::uint32_t seed);   // layer the M2a features (quantize, scales, degrees, swing, velocity) on a seeded session
Session m2aSession(const char* name);   // "scalefeel", "ties": N1 sessions every M2a feature (quantize, degrees, swing, velocity) must pass through
}
