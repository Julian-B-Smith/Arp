# m3a-admission-evidence — the Arp-side evidence for horde's 1.0 bar: Release CPU and memory figures, four new check families, the copy-in file list

- **Queue item:** T-007 (ROADMAP M3, horde admission), dispatch M3a, 2026-10-24, on `m3/admission`. Base `ff115db`; working tree uncommitted (the lead
  commits). Authored by an implementer dispatch from the lead session. The lead's concurrent edits (ROADMAP, DECISIONS, `.github/workflows/ci.yml`,
  `integrations/README.md`, `docs/filings/*`, `tools/core_sanitize.sh`) were not touched.
- **Why:** the M3 gate is "every MUST row of horde's `docs/proposals/note-processor-1.0-bar.md` is evidenced, CPU <= 2 % of a min-spec core included
  (D-015)" plus "a file list for horde's copy-in is shipped". M3a closes what the Arp side can close without horde: the two "affordable" rows
  (measured CPU, memory figure), the four "correct / plugs in" rows M1/M2 left uncovered, and the copy-in contract. It adds no feature and changes no
  public interface.

## What changed

- **`tests/core/test_m3.cpp` / `.h` (new)**, wired into `tests/core/CMakeLists.txt` and `test_core.cpp`'s `main` (`runM3Tests()`): 21 new check lines per int128 path
  (409 -> 430), every detector preceded by its plant:
  1. *Sample-exact timing, time signatures as data.* `restart bar`, `up` over one key at 1/16, so every bar start is a grid step. 10 meter scenes: 3/4, 6/8, 7/8,
     5/4 by time signature alone (bars counted from tick 0); 3/4 with a one-beat pickup, 6/8, 7/8 starting 2.5 beats in, 5/4 with the host's `barStartBeats`
     (a phase); 4/4 -> 3/4 -> 5/4 by time signature alone (both changes on a bar line of both grids); 4/4 -> 7/8 -> 6/8 with `barStartBeats` (changes off the old
     grid). 4 tempo/rate rows (120 and 77.7 and 999 bpm at 8 kHz, 133.7 bpm at 44.1 kHz) x block sizes 1/7/64/512/4096 = 200 bar starts. The expected sample of each is
     `cdiv(tick * D, tempo_milli * TPQ)` in plain int64 (not the core's arith), the bar anchor is the step with `StepRecord::k = 0`, and its NoteOn must carry the bar-start
     tick; N1 and G1 hold. A mid-session change is a block cut (a no-op `Change` at that sample), as the adapter splits blocks at any host edge.
     **Plant** `meterAs44` (new, test-only): 10 of 10 scenes read red ("bar-start steps at 0,16000,32000,48000, expected 0,12000,24000,36000,48000").
  2. *No input, no output.* 16 orders x sync/time x restart chord/bar/free x chord stage on/off x transport playing/stopped/play-stop-play x plain/`bar_trigger`/
     `trigger_pulse` = 1728 sessions, nothing ever held, latch off, a pedal press and a Panic riding along: 0 NoteOns, 0 NoteOffs. Triggers counted separately:
     448 in all; asserted only the ratified SPEC 2.5/2.6 boundary (they fire with no keys iff the transport plays, sync mode, restart bar/free: 256 on the pure playing
     transport, 192 in the play-stop-play sessions), NOT zero. Plus a known answer that after the last key is released (latch off) nothing starts.
     **Plant** `emitOnEmptySet` (new, test-only): 2560 NoteOns in the same matrix.
  3. *Preset load as a terminal path.* One snapshot where all 47 settings change (`scramble`, proved field by field by `unchanged`, itself shown to name a setting left alone)
     plus a new 6-step user pattern published in the same gap, at 6 states (mid-note, mid-ratchet with a pending sub-note, during a tie, while latched twice (latch -> off,
     latch -> append), while exhausted), each state proved reached in a run without the load; 4 snapshots each (all but `enabled`/`arp_enabled`; also arp off; also module off;
     every setting incl. both) = 24 sessions at block sizes 4096/333/64/7/1: N1 0 violations, G1 0 divergences, 0 lost offs, 0 allocations inside `process()`, 0 loads that
     changed nothing. **Plants, each red on every session they can reach:** `dropRelease` 24/24, `doubleRelease` 24/24, `allocInSettings` 6/6 (only snapshots that leave the arp
     running reach `adopt()`), `clampOffset` 24/24, `floatPosition` 24/24.
  4. *Pattern-bank selection as a morph target.* 29 morph sessions (`pattern` across all 12 values - the 11 bank entries and `user` - one session each, `order` across all 16,
     and one chain through every value) via a harness copy of the pinned `MorphModel` rule: N1/G1 at 4096/333/64/1, >= 6 corner switches each. Known answers for where each
     switch lands: a hard switch at 4400 lands at 5000 for all 12 pattern values (earlier notes bit-identical; every later step plays as the new pattern's step data says) and
     all 16 orders (earlier notes identical; from 5000 on exactly what a session that began on that order plays, `m` carried; 15 of 16 switches change the notes).
     **Plants:** `morphSwitchCuts` read red by the landing check on a pattern and an order switch; `dropRelease` and `clampOffset` red on the chain session.
- **`core/include/arp/core.h`, `core/src/core.cpp` (test-only plants, no behaviour change):** `Plants::meterAs44` (the bar length ignores the meter) and `Plants::emitOnEmptySet`
  (a step that finds no key held plays key 60). Both are `#ifdef ARP_PLANTS`, so the shipped library is unchanged; the one non-plant-looking edit is the `num`/`den` line in
  `process()`, which keeps its exact old semantics (each non-positive part reads as 4) and adds only `&& !ARP_PLANT(meterAs44)`.
- **`tests/core/harness.h`:** the two plants in the name table; a stale size comment (~50 KB -> ~96 KiB). **`tests/core/alloc_hook.{h,cpp}`:** `allocBytes()` / `AllocScope::bytes()`
  (additive).
- **`tests/core/bench.cpp` (new), `tools/core_bench.sh` (new), `tests/core/CMakeLists.txt`:** the benchmark (below). Two targets, `arp_bench` (native `__int128`) and `arp_bench_soft`
  (core compiled with `ARP_FORCE_INT128_FALLBACK`, the MSVC path), both link the SHIPPED library (no `ARP_PLANTS`; `#error` otherwise), both compiled by `core_test.sh` so they cannot rot,
  neither run by it. The script builds them in `build/bench` (Release, absolute paths) and prints the machine first.
- **`docs/horde-copy-in.md` (new) and `tools/core_test.sh` (a GATE SCRIPT; check ADDED, nothing weakened):** the 8 files horde copies, each with its sha256, build notes, and the
  `copyin-table` argument that regenerates the table. The added check audits the list (a listed file missing or hash stale; any file under `core/` bar `CMakeLists.txt` missing from
  the list), then copies exactly the listed files to an empty scratch tree and builds the shipped-library smoke from them alone (C++20, `-Wall -Wextra -Werror`, no defines, only
  that tree on the include path) and runs it. Plants first: a list with a row dropped, a list with a wrong hash, and **for each of the 8 listed files in turn a copy that omits it**
  (the build must fail; 8 of 8 do). The only edit to existing lines is the `trap` (it also removes the scratch dir).

## Evidence: the bench (reported, never gated)

`bash tools/core_bench.sh` (full: 15 repetitions of 1 s of audio per configuration after 1 s warm-up; 31 scenarios x 48 / 44.1 / 96 kHz x blocks 32/64/128/512, both int128 paths).
Machine: Apple M3, 8 cores, Darwin 25.6.0 arm64, Apple clang 16.0.0, `-O3 -DNDEBUG`, no `-ffast-math`. **The machine was shared with other sessions' builds and test runs during the
measurement** (the spread column shows it: single repetitions hit 60 % and more on the tiniest costs; the headline is the median of repetitions, but re-run on a quiet machine for a
figure you publish). **Not min-spec; the x4 is horde's heuristic, not "min-spec verified".**

48 kHz, block 128, native `__int128` (mean ns per block / ns per sample / % of one core / x4 against the 2 % slice):

| scenario | ns/block | ns/sample | %core | x4 % |
|---|---|---|---|---|
| 1/64 @999, 16 keys, up | 407 | 3.18 | 0.0153 | 0.061 |
| 1/64 @120, 16 keys, up | 124 | 0.97 | 0.0047 | 0.019 |
| 1/64 @999, 16 keys, order chord (the heaviest order) | 2920 | 22.81 | 0.1095 | 0.438 |
| 1/64 @999, 16 keys, order stride / chord_between | 1583 / 1684 | 12.4 / 13.2 | 0.059 / 0.063 | 0.237 / 0.253 |
| other 12 order styles | 354-575 | 2.8-4.5 | 0.013-0.022 | 0.05-0.09 |
| ratchet 4 / ties / probability / triggers on 8 lanes | 408 / 371 / 354 / 410 | ~3 | ~0.014 | ~0.06 |
| transposition 8 steps / quantize + degrees / swing 750 | 402 / 431 / 442 | ~3.3 | ~0.016 | ~0.065 |
| chord stage with 6 shifts, 16 keys | 1549 | 12.11 | 0.0581 | 0.232 |
| time mode at 10 ms | 206 | 1.61 | 0.0077 | 0.031 |
| chord stage + key churn, 1/16 @120 | 794 | 6.21 | 0.0298 | 0.119 |
| heavy but playable: order chord + chord stage 6 shifts, 16 keys, 1/16 @120 | 1287 | 10.05 | 0.0483 | 0.193 |
| **WORST CASE: order chord over 112 distinct keys (16 keys x 6 shifts), ratchet 4, gate 200, 1/64 @999** | 76386 | 596.77 | **2.8645** | **11.458** |

Summary lines, verbatim (native path). The two-word fallback path (MSVC) lands in the same range, noisier: worst case 3.0750 % / x4 12.3002 % at 48 kHz / 128, heaviest non-worst 0.3085 % / x4 1.2339 % at 96 kHz / 32; individual scenarios differ by up to ~60 % from the native path (e.g. `chord_between` 21.0 against 13.2 ns/sample), so read both tables in the full output:

```
   48000 Hz  heaviest scenario overall: 3.1244 % of a core, x4 = 12.4977 %  [WORST CASE ... block 32]  -> OVER the 2 % slice
            heaviest excluding the deliberate worst case: 0.1588 %, x4 = 0.6353 %  [heavy but playable: order chord, chord stage with 6 shifts, 16 keys, 1/16 @120, block 32]  -> WITHIN
   44100 Hz  heaviest scenario overall: 3.0524 % of a core, x4 = 12.2095 %  [WORST CASE ... block 32]  -> OVER the 2 % slice
            heaviest excluding the deliberate worst case: 0.1468 %, x4 = 0.5873 %  [heavy but playable ...  block 32]  -> WITHIN
   96000 Hz  heaviest scenario overall: 3.3921 % of a core, x4 = 13.5684 %  [WORST CASE ... block 32]  -> OVER the 2 % slice
            heaviest excluding the deliberate worst case: 0.3042 %, x4 = 1.2170 %  [heavy but playable ...  block 32]  -> WITHIN
  events: the deliberate worst case costs 480 ns per output event at 48 kHz / 128; the 2 % slice at x4 is 0.50 % of this core = about 10416 output events per second at that cost
  per-block deadline: the largest p99 over any configuration is 32.700 % of its block period, the largest single call 68.675 % (OS preemption noise included)
  allocations inside process() / reset() over every timed loop: 0 (OK); NoteOffs lost: 0 (OK)
```

**The finding the lead must weigh:** every musically ordinary scenario is far inside the slice (<= 1.22 % after x4, even at 96 kHz / block 32); the deliberate worst case (112 simultaneous
chord-stage notes, each ratcheted 4x, 16 steps per beat at 999 bpm - about 60 000 output events per second) is 11-14 % after x4, i.e. over 2 %. Whether "CPU <= 2 % of a min-spec core" means
the heaviest *musical* load or the capacity-maximal one is a decision for the lead and horde; the numbers for both are above. A `sample(1)` profile of the worst case puts the time in
`process()` (inlined step work), `emitOn`, `release` and `sortEvents` - linear scans of the 368-entry sounding table and the event sort per event; no optimisation was attempted (not in scope, and
no feature may change).

## Evidence: the memory figure (worst case, all capacities full, shipped layout)

- `sizeof(arp::Core)` = **98 752 B (96.4 KiB)**: every fixed table is INSIDE the object, so it is the worst case already: user pattern slots 3 x 524 B, chord records 32 x 64, chord notes 128 x 48,
  chord set 128 x 32, refs 128 x 48, ignored ring 256 x 24, sounding table 368 x 80 (128 + 16 slack + 224 chord) = 29 440 B, pending 512 x 64 = 32 768 B, owed refs 368 x 24, other 1564 B.
  Put it on the heap or in a long-lived object, not on a small stack.
- Allocation points, counted by the A1 hook (operator new; a direct malloc/mmap would not show, the core includes none): `make_unique<Core>()` = 1 call, 98 752 B (the object itself, the constructor
  allocates nothing else); `prepare(sr, maxBlock)` = 2 calls = `max(1024, 4 * maxBlock)` events x 80 B + (steps bound + 8) step records x 48 B, matching the formula in every configuration
  tried (48 kHz / 32: 82 400 B; 48 kHz / 512: 164 416 B; 48 kHz / 4096: 1 312 256 B; **8 kHz / 4096, the largest: 1 317 728 B**); a second `prepare` at the same size: 0 calls;
  `setUserPattern` / `getUserPattern` / `heldKeyCount`: 0; `process()` and `reset()`: 0 over every timed loop of the whole bench (and in the suite's A1 checks).
- **Worst-case total: 1.35 MiB** (`sizeof(Core)` + the 8 kHz / 4096 prepare), allocated at construction and `prepare` only.

## Gate-script note

`tools/core_test.sh` is a gate script. This dispatch only ADDS the copy-in check (plus the `copyin-table` helper argument and the scratch-dir cleanup in the existing `trap`); no existing
check, threshold or exit path was changed or relaxed. `./verify` and `.kit/` were not touched.

## Evidence consulted

`CLAUDE.md` §Domain, ROADMAP §M3 / T-007, SPEC 1, 1.3, 2.2, 2.5, 2.6, 4.5, 4.6, 9, 11; horde's `docs/proposals/note-processor-1.0-bar.md` and
`docs/research/2026-08-22-two-osc-cpu-measurement.md` (read-only); `traces/2026-10-23-m2b-restart-morph-chord.md` (conventions); `core/`, `tests/core/` (harness, test_m2b's
`MorphModel` / `discreteTable` / `landingBad`), `tools/core_test.sh`, `tools/core_sanitize.sh` (the lead's, used for the UBSan run).

## Alternatives rejected

- A harness-side "plant" (feed the checker a 4/4 reading) instead of `meterAs44`: tests the test, not the core.
- Per-member sizes by adding a public memory-report API to `Core`: a public interface change. The bench instead sees the private tables with `#define private public` around the single include of `core.h`
  (measurement only; access control does not change layout on clang/gcc; the linked library is unmodified).
- Asserting zero Triggers in the no-input matrix: SPEC 2.5 fires them; the brief says report. Only the ratified boundary is asserted.
- Gating the CPU number: horde's convention and the brief say reported, never gated.
- Fixing the worst-case cost in the core: out of scope (no feature work; the bench is the deliverable).

## Verify (base git `ff115db`, tree uncommitted)

```
bash tools/core_test.sh            exit 0
  PASS  core_test: built (Release, -Wall -Wextra, warnings clean)
  PASS  guard control / -ffast-math / -ffinite-math-only (3 lines)
  OK: 430 passed, 0 failed         (arp_tests, native __int128)
  OK: 430 passed, 0 failed         (arp_tests_fallback, two-word int128)
  PASS  shipped library: 67 NoteOns, 67 NoteOffs, no plants compiled in
  PASS  copy-in plant: dropping core/include/arp/arith.h from the list is caught (copy-in: core/include/arp/arith.h is under core/ but missing from the list)
  PASS  copy-in plant: a stale hash is caught (copy-in: stale hash for core/include/arp/arith.h (listed 813e9ac24959..., actual 702d89b13848...))
  PASS  copy-in: 8 listed files exist with their listed hashes and nothing under core/ (bar CMakeLists.txt) is missing from the list
  PASS  copy-in plant: omitting any one of the 8 listed files breaks the build (e.g. without core/include/arp/arith.h: core/include/arp/core.h:16:10: fatal error: 'arp/arith.h' file not found)
  PASS  copy-in: the listed files alone (C++20, standard library only, no defines, -Wall -Wextra -Werror) build and run the shipped-library smoke: PASS  shipped library: 67 NoteOns, 67 NoteOffs, no plants compiled in
  PASS  core_test: all binaries green
python3 tools/core_params_check.py          exit 0   (4 PASS lines)
python3 tools/core_params_check.py --plant  exit 1   (red, as designed)
node tools/lab_smoke.mjs                    exit 0   lab_smoke: OK (2227 ms)
./verify fast                               exit 0   .harness/last-verify.json: {"target":"fast","exit":0,"git":"ff115db",...}
bash tools/core_sanitize.sh ubsan           exit 0   arp_tests OK: 430 passed, 0 failed; arp_tests_fallback OK: 430 passed, 0 failed; arp_shipped_smoke PASS; "PASS  core_sanitize(ubsan): every binary green, no sanitizer report"
bash tools/core_bench.sh                    exit 0   BENCH OK (reported, never gated), both int128 paths
```
ASan / TSan not run (they cannot start on this machine, M1b trace; the lead's CI job covers them on Linux, where the new `arp_bench*` targets are built too).

## Open questions

- **CPU scope of "<= 2 %" (decision, lead + horde):** musical load 0.6-1.2 % after x4 (within), capacity-maximal load 11-14 % (over). See above.
- **SPEC-GAP, meter change by time signature alone, off the shared bar grid:** with no `barStartBeats`, bars are counted from tick 0 with the NEW length (SPEC 4.5), so a 4/4 -> 3/4 change at
  tick 122880 (not a 3/4 bar line) re-anchors at the first step after the change to the new grid's bar start in the PAST (tick 92160): `kAbs 32, k 8, mBefore 0` (probed). Read literally; not tested
  as a rule. Hosts that report a meter change report `barStartBeats` too, where the new bar start is the phase and nothing in the past is chosen. The tested scenes change only on a common bar line or
  with `barStartBeats`.
- **Triggers with no keys (SPEC 2.5):** 448 fired in the matrix, all inside the ratified boundary. Whether horde's "no input, no output" row covers them is the open question already filed with horde.
- **`allocInSettings` reaches only sessions where the arp keeps running after the load** (`adopt()` runs at an evaluated step); the other 18 load sessions are covered by the N1/G1/A1 check, not by that plant.
- **`bench.cpp` uses `#define private public`** (measurement only, above) and is untested on g++ (this machine has Apple clang only); the CI Linux build compiles it with `-Werror`. If g++ objects, the fix is
  local to `tests/core/bench.cpp`.
- **Pinned commit in `docs/horde-copy-in.md`** is described, not stated: it cannot name its own commit. The lead records it (or amends the line) when committing; the sha256 column is what is checked.
