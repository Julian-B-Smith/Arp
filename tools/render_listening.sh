#!/usr/bin/env bash
# render_listening.sh - the listening renders of the SHIPPED core (ROADMAP T-007, M3 gate: the human's listening sign-off).
# Builds tests/core/render.cpp (target arp_render, linked to the shipped arp_core, no ARP_PLANTS) in a Release tree of its own,
# runs it, and writes build/listen/ (gitignored): one WAV and one MIDI file per scenario plus manifest.json.
# Then open lab/listen.html from the lab preview server (.claude/launch.json, "lab") at /lab/listen.html.
#   bash tools/render_listening.sh            render
#   bash tools/render_listening.sh --check    render twice and compare byte for byte (the renders must be deterministic)
# Not part of ./verify. Exit 0 = every scenario kept the core's contracts (nothing allocated inside process(), every NoteOn got its
# NoteOff, no counter that means "refused or lost" moved). Command Line Tools only: Unix Makefiles; every path is ABSOLUTE because
# the shell's cwd is not reliable (a relative `cmake --build` builds nothing and exits 0).
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$repo/build/render"
out="$repo/build/listen"
jobs="$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null) || echo 2)"

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
if ! cmake -S "$repo/core" -B "$build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DARP_WERROR=ON >"$log" 2>&1 ||
   ! cmake --build "$build" --target arp_render -j"$jobs" >"$log" 2>&1; then
  cat "$log"; echo "FAIL  render_listening: the renderer did not build"; exit 1
fi

rm -rf "$out"
"$build/arp_render" "$out" || { echo "FAIL  render_listening: the renderer reported a broken contract"; exit 1; }

if [ "${1:-}" = "--check" ]; then
  again="$repo/build/listen-check"
  rm -rf "$again"
  "$build/arp_render" "$again" >/dev/null || { echo "FAIL  render_listening --check: the second render failed"; exit 1; }
  if diff -rq "$out" "$again" >/dev/null; then
    echo "PASS  determinism: two renders are byte-identical ($(ls "$out" | wc -l | tr -d ' ') files)"
    rm -rf "$again"
  else
    diff -rq "$out" "$again"; echo "FAIL  determinism: two renders differ"; exit 1
  fi
fi
echo "open /lab/listen.html on the lab preview server"
