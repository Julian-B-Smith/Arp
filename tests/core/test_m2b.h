#pragma once
#include "harness.h"
namespace arp_test {
void runM2bTests();
Session m2bSession(const char* name);   // "restartrepeats", "morph", "chordstage": N1 / G1 / G4 / A1 sessions every M2b feature must pass through
}
