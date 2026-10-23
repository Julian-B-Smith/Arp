#!/usr/bin/env bash
# core_test.sh - build and run the C++ core suite (ROADMAP T-003, M1a).
#   bash tools/core_test.sh          configure + build + run every test binary
#   bash tools/core_test.sh build    configure + build only (tools/core_params_check.py uses this)
#
# Command Line Tools only on this Mac: Unix Makefiles, never Xcode/Ninja. Every path is ABSOLUTE
# because the shell's cwd is not reliable (a relative `cmake --build` builds nothing and exits 0).
# Release build, warnings are errors (-Wall -Wextra); ARP_WERROR=0 downgrades them while debugging.
# Prints the suite's own PASS/FAIL lines; exits nonzero if the build or any binary fails.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$repo/build/core"
werror="${ARP_WERROR:-1}"
jobs="$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null) || echo 2)"

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

if ! cmake -S "$repo/core" -B "$build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DARP_WERROR="$([ "$werror" = 1 ] && echo ON || echo OFF)" >"$log" 2>&1; then
  cat "$log"; echo "FAIL  core_test: cmake configure failed"; exit 1
fi
if ! cmake --build "$build" -j"$jobs" >"$log" 2>&1; then
  cat "$log"; echo "FAIL  core_test: build failed"; exit 1
fi
if grep -E "warning:" "$log" >/dev/null 2>&1; then
  cat "$log"; echo "FAIL  core_test: the build printed warnings"; exit 1
fi
echo "PASS  core_test: built (Release, -Wall -Wextra, warnings clean)"
[ "${1:-}" = "build" ] && exit 0

rc=0
for t in arp_tests arp_tests_fallback arp_shipped_smoke; do
  echo "== $t"
  if ! "$build/$t"; then echo "FAIL  core_test: $t exited nonzero"; rc=1; fi
done
[ "$rc" = 0 ] && echo "PASS  core_test: all binaries green" || echo "FAIL  core_test: see FAIL lines above"
exit "$rc"
