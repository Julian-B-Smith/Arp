#!/usr/bin/env bash
# core_test.sh - build and run the C++ core suite (ROADMAP T-003, M1a).
#   bash tools/core_test.sh          configure + build + run every test binary
#   bash tools/core_test.sh build    configure + build only (tools/core_params_check.py uses this)
#   bash tools/core_test.sh copyin-table   print the file table of docs/horde-copy-in.md for the current core/ (paste it in when core/ changes on purpose)
# Besides the suite and the copy-in list it checks the benchmark stream digest (M3b, D-030: the shipped core's output over the benchmark's scenarios,
# against tests/core/stream_digest_bench.txt); the suite's own digest check (tests/core/stream_digest.txt) runs inside arp_tests. Regenerate both with
# tools/core_digest.sh, and only for a change that is meant to move the output.
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

# ---- the copy-in list (M3a, D-015): docs/horde-copy-in.md names exactly the files horde copies into its tree ----
# Rows look like:  | `core/include/arp/types.h` | <64 hex sha256> |   core/CMakeLists.txt is the one file under core/ that is NOT copied (horde brings its own build).
sha256_of() { if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1; else sha256sum "$1" | cut -d' ' -f1; fi; }
copyin_rows() { sed -n -E 's/^\| `(core\/[^`]+)` \| ([0-9a-f]{64}) \|.*$/\1 \2/p' "$1"; }
copyin_table() {
  (cd "$repo" && find core -type f ! -name CMakeLists.txt | LC_ALL=C sort) | while read -r f; do echo "| \`$f\` | $(sha256_of "$repo/$f") |"; done
}
# copyin_audit <doc> : 0 when every listed file exists with its listed hash and no file under core/ is missing from the list; prints one FAIL line per problem.
copyin_audit() {
  local doc="$1" bad=0 path want got listed
  listed="$(copyin_rows "$doc" | cut -d' ' -f1 | LC_ALL=C sort)"
  if [ -z "$listed" ]; then echo "FAIL  copy-in: no file rows found in $doc"; return 1; fi
  while read -r path want; do
    if [ ! -f "$repo/$path" ]; then echo "FAIL  copy-in: $path is listed but does not exist"; bad=1; continue; fi
    got="$(sha256_of "$repo/$path")"
    [ "$got" = "$want" ] || { echo "FAIL  copy-in: stale hash for $path (listed ${want:0:12}..., actual ${got:0:12}...)"; bad=1; }
  done < <(copyin_rows "$doc")
  while read -r path; do
    echo "$listed" | grep -qxF "$path" || { echo "FAIL  copy-in: $path is under core/ but missing from the list"; bad=1; }
  done < <(cd "$repo" && find core -type f ! -name CMakeLists.txt | LC_ALL=C sort)
  return "$bad"
}
# copyin_build <doc> [omit-path] : copy EXACTLY the listed files (minus the omitted one) into a scratch tree and build + run the shipped-library
# smoke from them alone. Nothing of the repo's core/ is on the include path, so a header the list forgot cannot be found by accident.
# No defines (ARP_PLANTS must not exist in horde's build), the C++ standard library only, no extra link flags. Output goes to $copy_log.
copyin_build() {
  local doc="$1" omit="${2:-}" path want opt="-O1"
  rm -rf "$copy_dir/tree" && mkdir -p "$copy_dir/tree"
  while read -r path want; do
    [ "$path" = "$omit" ] && continue
    mkdir -p "$copy_dir/tree/$(dirname "$path")" && cp "$repo/$path" "$copy_dir/tree/$path"
  done < <(copyin_rows "$doc")
  [ -n "$omit" ] && opt="-O0"
  "$cxx" -std=c++20 $opt -Wall -Wextra -Werror -I"$copy_dir/tree/core/include" "$copy_dir/tree/core/src/core.cpp" "$repo/tests/core/shipped_smoke.cpp" -o "$copy_dir/smoke" >"$copy_log" 2>&1 || return 1
  "$copy_dir/smoke" >>"$copy_log" 2>&1
}

[ "${1:-}" = "copyin-table" ] && { copyin_table; exit 0; }

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

# Must-fail builds (LOW-B): the core refuses to compile under -ffast-math / -ffinite-math-only, where the NaN / inf checks on
# the host's floats could be optimised away. The control compiles the same file WITHOUT the flag and must succeed, so a
# refusal is proven to come from the flag and not from some other breakage.
cxx="${CXX:-c++}"
guard_log="$(mktemp)"
copy_dir="$(mktemp -d)"
copy_log="$copy_dir/build.log"
trap 'rm -rf "$log" "$guard_log" "$copy_dir"' EXIT
guard_rc=0
if "$cxx" -std=c++20 -I"$repo/core/include" -fsyntax-only "$repo/core/src/core.cpp" >"$guard_log" 2>&1; then
  echo "PASS  guard control: core.cpp compiles without a math flag"
else
  cat "$guard_log"; echo "FAIL  guard control: core.cpp does not compile even without a math flag"; guard_rc=1
fi
for flag in -ffast-math -ffinite-math-only; do
  if "$cxx" -std=c++20 $flag -I"$repo/core/include" -fsyntax-only "$repo/core/src/core.cpp" >"$guard_log" 2>&1; then
    echo "FAIL  guard: core.cpp compiled under $flag (it must be refused)"; guard_rc=1
  elif grep -q "IEEE semantics" "$guard_log"; then
    echo "PASS  guard: core.cpp is refused under $flag (the #error fired)"
  else
    cat "$guard_log"; echo "FAIL  guard: core.cpp failed under $flag but not with the IEEE-semantics #error"; guard_rc=1
  fi
done
[ "$guard_rc" = 0 ] || exit 1
[ "${1:-}" = "build" ] && exit 0

rc=0
for t in arp_tests arp_tests_fallback arp_shipped_smoke; do
  echo "== $t"
  if ! "$build/$t"; then echo "FAIL  core_test: $t exited nonzero"; rc=1; fi
done

# The benchmark stream digest (M3b, D-030): the SHIPPED core's whole output over the benchmark's scenarios x rates x block sizes, hashed (every event
# field, step record and counter), against tests/core/stream_digest_bench.txt, generated from the pre-optimisation core. Both int128 paths must match
# the one file. The plant comes first: the same run with ONE field of ONE event changed in what is hashed (never in the core) must differ.
bench_digest="$repo/tests/core/stream_digest_bench.txt"
digest_rc=0
grep -v '^#' "$bench_digest" >"$copy_dir/bench.want"
for bin in arp_bench arp_bench_soft; do
  "$build/$bin" --digest-perturb 2>/dev/null | grep -v '^#' >"$copy_dir/bench.got"
  if [ -s "$copy_dir/bench.got" ] && ! cmp -s "$copy_dir/bench.want" "$copy_dir/bench.got"; then
    echo "PASS  bench digest plant ($bin): one field of one event changed is caught ($(diff "$copy_dir/bench.want" "$copy_dir/bench.got" | grep -c '^>') of $(wc -l <"$copy_dir/bench.want" | tr -d ' ') configurations differ)"
  else
    echo "FAIL  bench digest plant ($bin): a perturbed event was NOT caught"; digest_rc=1
  fi
  "$build/$bin" --digest 2>/dev/null | grep -v '^#' >"$copy_dir/bench.got"
  if [ -s "$copy_dir/bench.got" ] && cmp -s "$copy_dir/bench.want" "$copy_dir/bench.got"; then
    echo "PASS  bench digest ($bin): $(wc -l <"$copy_dir/bench.want" | tr -d ' ') configurations (scenario x 4 rates x 5 block sizes, 2 s each) hash to the committed pre-optimisation digest"
  else
    diff "$copy_dir/bench.want" "$copy_dir/bench.got" | head -6 | cut -c1-220
    echo "FAIL  bench digest ($bin): the shipped core's output differs from tests/core/stream_digest_bench.txt"; digest_rc=1
  fi
done
[ "$digest_rc" = 0 ] || rc=1

# The copy-in list (docs/horde-copy-in.md): the file list audited, then the listed files alone built into the shipped-library smoke.
# Each detector is preceded by its must-fail plant (the project charter): a plant that does NOT fire is itself a FAIL.
copy_doc="$repo/docs/horde-copy-in.md"
copy_rc=0
mut="$copy_dir/mutated.md"
first_row="$(copyin_rows "$copy_doc" | head -1 | cut -d' ' -f1)"
grep -vF "\`$first_row\`" "$copy_doc" >"$mut"
if copyin_audit "$mut" >"$copy_log" 2>&1 && ! grep -q "missing from the list" "$copy_log"; then
  echo "FAIL  copy-in plant: a list without $first_row passed the audit"; copy_rc=1
else
  echo "PASS  copy-in plant: dropping $first_row from the list is caught ($(grep -m1 "missing from the list" "$copy_log" | sed 's/^FAIL  //'))"
fi
first_hash="$(copyin_rows "$copy_doc" | head -1 | cut -d' ' -f2)"
sed "s/$first_hash/$(printf '%s' "$first_hash" | tr '0123456789abcdef' '123456789abcdef0')/" "$copy_doc" >"$mut"
if copyin_audit "$mut" >"$copy_log" 2>&1 || ! grep -q "stale hash" "$copy_log"; then
  echo "FAIL  copy-in plant: a stale hash passed the audit"; copy_rc=1
else
  echo "PASS  copy-in plant: a stale hash is caught ($(grep -m1 "stale hash" "$copy_log" | sed 's/^FAIL  //'))"
fi
if copyin_audit "$copy_doc"; then
  echo "PASS  copy-in: $(copyin_rows "$copy_doc" | wc -l | tr -d ' ') listed files exist with their listed hashes and nothing under core/ (bar CMakeLists.txt) is missing from the list"
else
  copy_rc=1
fi
omitted=0; omit_fail=0; omit_example=""
while read -r path want; do
  omitted=$((omitted + 1))
  if copyin_build "$copy_doc" "$path"; then
    echo "FAIL  copy-in plant: the build still succeeded without $path"; copy_rc=1
  else
    omit_fail=$((omit_fail + 1))
    [ -n "$omit_example" ] || omit_example="without $path: $(grep -m1 -E "error|undefined|not found" "$copy_log" | sed "s|$copy_dir/tree/||g" | cut -c1-150)"
  fi
done < <(copyin_rows "$copy_doc")
if [ "$omitted" -gt 0 ] && [ "$omit_fail" = "$omitted" ]; then
  echo "PASS  copy-in plant: omitting any one of the $omitted listed files breaks the build (e.g. $omit_example)"
else
  echo "FAIL  copy-in plant: only $omit_fail of $omitted single-file omissions broke the build"; copy_rc=1
fi
if copyin_build "$copy_doc"; then
  echo "PASS  copy-in: the listed files alone (C++20, standard library only, no defines, -Wall -Wextra -Werror) build and run the shipped-library smoke: $(grep -m1 "shipped library" "$copy_log")"
else
  cat "$copy_log"; echo "FAIL  copy-in: the listed files do not build the shipped-library smoke on their own"; copy_rc=1
fi
[ "$copy_rc" = 0 ] || rc=1

[ "$rc" = 0 ] && echo "PASS  core_test: all binaries green" || echo "FAIL  core_test: see FAIL lines above"
exit "$rc"
