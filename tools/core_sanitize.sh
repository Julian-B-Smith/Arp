#!/usr/bin/env bash
# core_sanitize.sh - build and run the C++ core suite under one sanitizer (M3, D-029).
#   bash tools/core_sanitize.sh asan|tsan|ubsan
#
# Evidence for horde's note-processor bar row "real-time safe ... a sanitizer run". Runs in CI on Linux
# (.github/workflows/ci.yml, job `sanitize`), because ASan and TSan cannot start on the dev Mac (the M1b
# trace). Each sanitizer gets its own build dir: their runtimes cannot be mixed in one binary.
# -O1 keeps the instrumented build quick while still exercising real code paths; the suite's checks
# (including the real-thread U1 test, which is what TSan is for) run unchanged. Any sanitizer report
# aborts the binary (halt_on_error / -fno-sanitize-recover), so a report is a nonzero exit, never noise.
# Paths are ABSOLUTE: the shell's cwd is not reliable (global CLAUDE.md, sandbox gotchas).
set -uo pipefail

kind="${1:-}"
case "$kind" in
  asan)  flags="-fsanitize=address -fno-omit-frame-pointer"
         export ASAN_OPTIONS="halt_on_error=1:detect_leaks=1:abort_on_error=1" ;;
  tsan)  flags="-fsanitize=thread"
         export TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1" ;;
  ubsan) flags="-fsanitize=undefined -fno-sanitize-recover=undefined"
         export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" ;;
  *)     echo "usage: tools/core_sanitize.sh asan|tsan|ubsan" >&2; exit 64 ;;
esac

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$repo/build/san-$kind"
jobs="$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null) || echo 2)"

if ! cmake -S "$repo/core" -B "$build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="$flags -O1 -g" -DCMAKE_EXE_LINKER_FLAGS="$flags"; then
  echo "FAIL  core_sanitize($kind): cmake configure failed"; exit 1
fi
if ! cmake --build "$build" -j"$jobs"; then
  echo "FAIL  core_sanitize($kind): build failed"; exit 1
fi

rc=0
for t in arp_tests arp_tests_fallback arp_shipped_smoke; do
  echo "== $t ($kind)"
  if ! "$build/$t"; then echo "FAIL  core_sanitize($kind): $t exited nonzero (a check failed or the sanitizer reported)"; rc=1; fi
done
[ "$rc" = 0 ] && echo "PASS  core_sanitize($kind): every binary green, no sanitizer report"
exit $rc
