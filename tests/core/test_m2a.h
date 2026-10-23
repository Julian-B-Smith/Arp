#pragma once
#include "harness.h"
namespace arp_test {
void runM2aTests();
Session m2aSession(const char* name);   // "scalefeel", "ties": N1 sessions every M2a feature (quantize, degrees, swing, velocity) must pass through
}
