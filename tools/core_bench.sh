#!/usr/bin/env bash
# core_bench.sh - the Release CPU and memory benchmark of the shipped core (ROADMAP T-007, M3a). REPORTED, never gated:
# it is not part of ./verify or tools/core_test.sh, and its exit code says only that the real-time contract held
# (no allocation inside process() / reset(), no NoteOff lost) and the memory figure matched its formula.
#   bash tools/core_bench.sh                 the full report, both int128 paths (about a minute each)
#   bash tools/core_bench.sh --quick         a fast, noisier pass
#   bash tools/core_bench.sh --only TEXT     only the scenarios whose name contains TEXT
# Any other argument goes to the benchmark binaries unchanged (see tests/core/bench.cpp).
#
# Release only (-O3 -DNDEBUG, never -ffast-math: the core refuses it), in a build tree of its own, with ABSOLUTE paths because
# the shell's cwd is not reliable (a relative `cmake --build` builds nothing and exits 0). Command Line Tools only: Unix Makefiles.
# The report starts with the machine, because a CPU figure without one is not a figure.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$repo/build/bench"
jobs="$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null) || echo 2)"
cxx="${CXX:-c++}"

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
if ! cmake -S "$repo/core" -B "$build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DARP_WERROR=ON >"$log" 2>&1 ||
   ! cmake --build "$build" --target arp_bench arp_bench_soft -j"$jobs" >"$log" 2>&1; then
  cat "$log"; echo "FAIL  core_bench: the benchmark did not build"; exit 1
fi

echo "machine: $(sysctl -n machdep.cpu.brand_string 2>/dev/null || grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //' || echo unknown)"
echo "cores:   $(sysctl -n hw.physicalcpu 2>/dev/null || nproc 2>/dev/null || echo ?) physical, $(sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo ?) logical"
echo "os:      $(uname -sr) $(uname -m)"
echo "compiler: $("$cxx" --version 2>&1 | head -1)"
echo "build:   CMAKE_BUILD_TYPE=Release ($(grep -E '^CMAKE_CXX_FLAGS_RELEASE:' "$build/CMakeCache.txt" | cut -d= -f2-)), shipped core (no ARP_PLANTS), $(date +%Y-%m-%d)"
echo "load:    the machine is not idle-guaranteed; close other work for a clean figure and read the spread column"

rc=0
for bin in arp_bench arp_bench_soft; do
  echo
  echo "################ $bin"
  "$build/$bin" "$@" || { echo "FAIL  core_bench: $bin exited nonzero"; rc=1; }
done
exit "$rc"
