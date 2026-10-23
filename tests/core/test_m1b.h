#pragma once
#include "harness.h"
namespace arp_test {
void runM1bTests();
Session m1bSession(const char* name);   // "styles", "patterns", "timemode": N1 sessions every M1b feature must pass through
}
