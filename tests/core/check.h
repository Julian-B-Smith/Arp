// check.h - the suite's PASS/FAIL reporting, in the style of tools/lab_smoke.mjs: one line per
// check, a plant line for every must-fail control, and a nonzero exit if anything failed.
// A planted control that does NOT fire is a blind detector and is itself a FAIL.
#pragma once

#include <cstdio>
#include <string>

namespace arp_test {

struct Tally {
  int pass = 0;
  int fail = 0;
};
inline Tally& tally() {
  static Tally t;
  return t;
}

inline void check(bool ok, const std::string& msg) {
  std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", msg.c_str());
  ok ? ++tally().pass : ++tally().fail;
}
// `fired` is "the detector read red on the planted fault".
inline void plant(bool fired, const std::string& msg) { check(fired, "plant fires: " + msg); }

}  // namespace arp_test
