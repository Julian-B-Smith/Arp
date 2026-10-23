#pragma once
#include "harness.h"
namespace arp_test {
void runM1bTests();
Session m1bFuzzSession(std::uint32_t seed, bool keyRefs);   // the seeded M1b session exactly as M1b drew it
Session m1bSession(const char* name);   // "styles", "patterns", "timemode": N1 sessions every M1b feature must pass through
}
