#pragma once
#include "harness.h"
namespace arp_test {
// Plays kDigestCorpusSessions seeded random sessions straight through the core and records one digest entry (kind 'F') for each.
inline constexpr int kDigestCorpusSessions = 2000;
void recordDigestCorpus();
}
