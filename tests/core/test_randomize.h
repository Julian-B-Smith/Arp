#pragma once
#include "harness.h"
namespace arp_test {
void runRandomizeTests();
void dumpRandomize();
void dumpRandomizeCases();   // --dump-randomize-cases: tests/core/randomize_cases.json, the known answers the lab twin must reproduce   // --dump-randomize: one `rnd.<key>=<class> <exempt>` line per setting, for tools/core_params_check.py (RZ15)
}
