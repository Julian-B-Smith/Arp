#!/usr/bin/env bash
# core_digest.sh - regenerate the committed stream digests (D-030, T-007 M3b). ONLY for a change that is MEANT to move the core's output
# (a ratified behaviour change with its decision recorded); a CPU optimisation must never need it - that is what the digests are for.
#   bash tools/core_digest.sh regen [REV]
# With no REV the core of the working tree produces the digests. With a git REV (a commit hash) the core files of THAT commit are read out with
# `git show` (read-only; the working tree is not touched) into a scratch directory and built from there, so a digest can be produced by a core other
# than the one being edited: that is how the M3b digests were made, from the M3a core (0ef0903), before the optimised one existed.
# The tests and the benchmark come from the working tree either way. Writes tests/core/stream_digest.txt (the invariant suite's runs: see
# tests/core/test_digest.cpp) and tests/core/stream_digest_bench.txt (the benchmark's scenarios, from `arp_bench --digest`).
# The checks themselves are not here: the suite compares its runs against stream_digest.txt, and tools/core_test.sh diffs `arp_bench --digest`
# against stream_digest_bench.txt, each with a perturbed-event plant.
#
# Builds with plain c++ (not cmake), absolute paths, -O1 / -O2: the digests are integer-exact and do not depend on the optimisation level.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cxx="${CXX:-c++}"
[ "${1:-}" = "regen" ] || { echo "usage: bash tools/core_digest.sh regen [REV]   (rewrites the committed digests from the core at REV, default the working tree)"; exit 2; }
rev="${2:-}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if [ -n "$rev" ]; then
  core="$work/core"
  while read -r f; do
    mkdir -p "$core/$(dirname "${f#core/}")"
    git -C "$repo" show "$rev:$f" >"$core/${f#core/}" || { echo "FAIL  core_digest: cannot read $f at $rev"; exit 1; }
  done < <(git -C "$repo" ls-tree -r --name-only "$rev" core | grep -E '^core/(include/.*\.h|src/core\.cpp)$')
  origin="core/ at commit $rev"
else
  core="$repo/core"
  origin="core/ of the working tree"
fi
[ -f "$core/src/core.cpp" ] && [ -f "$core/include/arp/core.h" ] || { echo "FAIL  core_digest: no core found in $core"; exit 1; }

sha() { if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1; else sha256sum "$1" | cut -d' ' -f1; fi; }
note="generated from $origin: core/src/core.cpp sha256 $(sha "$core/src/core.cpp"), core/include/arp/core.h sha256 $(sha "$core/include/arp/core.h")"

tests="$repo/tests/core"
digest="$tests/stream_digest.txt"
suite=("$tests"/test_core.cpp "$tests"/test_m1b.cpp "$tests"/test_m2a.cpp "$tests"/test_m2b.cpp "$tests"/test_m3.cpp "$tests"/test_digest.cpp "$tests"/test_int128.cpp "$tests"/alloc_hook.cpp)
"$cxx" -std=c++20 -O1 -DARP_PLANTS -DARP_DIGEST_FILE="\"$digest\"" -I"$core/include" -I"$tests" "${suite[@]}" "$core/src/core.cpp" -o "$work/arp_tests" -pthread || { echo "FAIL  core_digest: the suite does not build against $origin"; exit 1; }
"$cxx" -std=c++20 -O2 -DNDEBUG -I"$core/include" -I"$tests" "$tests/bench.cpp" "$tests/alloc_hook.cpp" "$core/src/core.cpp" -o "$work/arp_bench" || { echo "FAIL  core_digest: the benchmark does not build against $origin"; exit 1; }

ARP_DIGEST_WRITE=1 ARP_DIGEST_NOTE="$note" "$work/arp_tests" >/dev/null 2>&1   # the check fails on purpose in write mode; the file is what matters
[ "$(grep -vc '^#' "$digest")" -gt 0 ] || { echo "FAIL  core_digest: the suite wrote no digest"; exit 1; }
{
  echo "# arp bench stream digest (tools/core_digest.sh): scenario <TAB> rate block events stream-hash counters-hash, from the SHIPPED core over the benchmark's scenarios."
  echo "# $note"
  "$work/arp_bench" --digest | grep -v '^#'
} >"$tests/stream_digest_bench.txt"
echo "wrote tests/core/stream_digest.txt ($(grep -vc '^#' "$digest") chunks) and tests/core/stream_digest_bench.txt ($(grep -vc '^#' "$tests/stream_digest_bench.txt") configurations)"
echo "$note"
