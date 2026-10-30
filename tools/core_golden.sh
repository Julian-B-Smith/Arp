#!/usr/bin/env bash
# core_golden.sh - cut the bank-pattern goldens (D-046, T-007): tests/core/goldens/<id>.txt, one per step pattern of spec/bank/starter.json, what the
# SHIPPED core (no ARP_PLANTS) plays for it (tests/core/goldens.cpp). ONLY for a change that is MEANT to move what a bank pattern plays (a ratified
# behaviour or bank change, with its decision recorded): the goldens are a protected path once cut, and a CPU optimisation must never need this.
#   bash tools/core_golden.sh cut [REV]
# With no REV the core is HEAD's, and core/ must have no uncommitted change, so the commit recorded in each file is the core that made it. With a git REV
# (a commit) the core files of THAT commit are read out with `git show` (read-only; the working tree is not touched) and built from a scratch directory,
# as tools/core_digest.sh does. The generator (tests/core/goldens.cpp, stream_digest.h) comes from the working tree either way.
# The check is not here: tools/core_test.sh runs arp_goldens on both int128 paths and diffs it against these files, after its must-fail plants.
#
# Builds with plain c++ (not cmake), absolute paths, -O2, no defines: the goldens are integer-exact and do not depend on the optimisation level.
# Rewrites every file it generates; a committed file whose pattern left the bank is reported, not deleted (deleting is the human's call).
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cxx="${CXX:-c++}"
[ "${1:-}" = "cut" ] || { echo "usage: bash tools/core_golden.sh cut [REV]   (rewrites tests/core/goldens/ from the core at REV, default HEAD with core/ clean)"; exit 2; }
rev="${2:-}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if [ -n "$rev" ]; then
  commit="$(git -C "$repo" rev-parse --verify "$rev^{commit}" 2>/dev/null)" || { echo "FAIL  core_golden: $rev is not a commit"; exit 1; }
  core="$work/core"
  while read -r f; do
    mkdir -p "$core/$(dirname "${f#core/}")"
    git -C "$repo" show "$commit:$f" >"$core/${f#core/}" || { echo "FAIL  core_golden: cannot read $f at $commit"; exit 1; }
  done < <(git -C "$repo" ls-tree -r --name-only "$commit" core | grep -E '^core/(include/.*\.h|src/core\.cpp)$')
else
  [ -z "$(git -C "$repo" status --porcelain -- core)" ] || { echo "FAIL  core_golden: core/ has uncommitted changes; commit them first, or name a REV, so the recorded commit is the core that cut the goldens"; exit 1; }
  commit="$(git -C "$repo" rev-parse HEAD)"
  core="$repo/core"
fi
[ -f "$core/src/core.cpp" ] && [ -f "$core/include/arp/bank_data.h" ] || { echo "FAIL  core_golden: no core with a bank found in $core"; exit 1; }
sha() { if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1; else sha256sum "$1" | cut -d' ' -f1; fi; }
note="# cut from core/ at commit $commit by tools/core_golden.sh cut: core/src/core.cpp sha256 $(sha "$core/src/core.cpp"), core/include/arp/bank_data.h sha256 $(sha "$core/include/arp/bank_data.h"); generator tests/core/goldens.cpp sha256 $(sha "$repo/tests/core/goldens.cpp"), tests/core/stream_digest.h sha256 $(sha "$repo/tests/core/stream_digest.h")"

tests="$repo/tests/core"
"$cxx" -std=c++20 -O2 -DNDEBUG -Wall -Wextra -I"$core/include" -I"$tests" "$tests/goldens.cpp" "$core/src/core.cpp" -o "$work/arp_goldens" || { echo "FAIL  core_golden: the generator does not build against core/ at $commit"; exit 1; }
mkdir -p "$work/out"
"$work/arp_goldens" "$work/out" || { echo "FAIL  core_golden: the generator failed"; exit 1; }

dest="$tests/goldens"
mkdir -p "$dest"
n=0
for f in "$work/out"/*.txt; do
  [ -f "$f" ] || continue
  awk -v note="$note" 'NR == 2 { print note } { print }' "$f" >"$dest/$(basename "$f")"
  n=$((n + 1))
done
for f in "$dest"/*.txt; do
  [ -f "$f" ] && [ ! -f "$work/out/$(basename "$f")" ] && echo "NOTE  core_golden: $(basename "$f") is committed but its pattern is no longer in the bank; left in place (tools/core_test.sh will name it)"
done
echo "wrote $n goldens to tests/core/goldens/"
echo "${note#\# }"
