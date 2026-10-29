# Arp core: the copy-in file list for horde (D-015)

> **Origin.** Arp project, M3a dispatch of ROADMAP T-007, 2026-10-24 (base commit `ff115db` plus the M3a working tree). horde's
> note-processor 1.0 bar says the core is copied into horde's tree at Arp's V1 by a horde PR that records Arp's commit (D-015,
> Q-H2 ANSWERED). This file is the contract for that copy: **exactly these files, nothing else.**
> Last verified: 2026-10-27 by `bash tools/core_test.sh` (the check described at the end; hashes re-cut for D-039, whose shipped output is unchanged). The CPU envelope below was measured on 2026-10-25 (M3c, D-032); **the quoted input limit is the idle re-measure of 2026-10-29 (D-042, "The quoted input limit" below), which supersedes the 1,300 of D-033.**

## What horde copies

Keep the relative layout: `core/include` is the include root (`#include "arp/core.h"`), `core/src/core.cpp` is the one translation unit.

| file | sha256 |
|---|---|
| `core/include/arp/arith.h` | 702d89b13848b554a6e32392f5f48a22bbdd157e8a531270f194e0a103f37162 |
| `core/include/arp/bank_data.h` | 87876a9f7681733500c17e65463f043b1be4f5bda8a77aff5949347c3a4b3723 |
| `core/include/arp/core.h` | 1dc40a197e02731afb39a83e674376b099f641656dddd8e64b25b3ee0bcb2459 |
| `core/include/arp/draw.h` | ed9060b7d13a854f3ed09d0011a0483136df387982d16efafbe65cd2e90c8985 |
| `core/include/arp/int128.h` | bb2c26a4cea9e04015c18957bf81a0d76b10f332ba72c86416fa41065112b433 |
| `core/include/arp/pattern.h` | 1ad8f47861d91587aa6df2b6180d85cfc7d611fc8001769d42547cf1714e3899 |
| `core/include/arp/types.h` | b45f811cdc241a5bf3ab6d76d4cc9fb8b168cb195870cbfdb452afab6879133b |
| `core/src/core.cpp` | f4bdb3409d35b0206796d46c03cbe47e13a1a4a177fa6d5246c55f2b1ada1f18 |

- **Pinned at commit:** the commit that contains this file. horde records `git log -1 --format=%h -- core/ docs/horde-copy-in.md`
  in its copy-in PR; the sha256 column above is the authority (it is what `tools/core_test.sh` checks), the commit is the label.
- **Not copied:** `core/CMakeLists.txt` (a convenience for building the library standalone or via `add_subdirectory`; horde brings its own
  build), everything under `tests/`, `tools/`, `lab/`, `spec/`, `intake/`, `docs/`.
- `core/include/arp/bank_data.h` is GENERATED from `spec/bank/starter.json` by `tools/gen_bank.py`; horde copies the generated header and never
  regenerates it (Arp's `tools/core_params_check.py` guards the drift on this side).

## Build notes

- **C++20**, standard library only: `<algorithm> <array> <atomic> <bit> <cassert> <cmath> <cstdint> <cstring> <functional> <iterator> <numeric> <span> <tuple> <vector>` (`<cassert>` only for debug asserts; `NDEBUG` removes them; `<bit>` for `std::countr_zero`).
  No third-party dependency, no thread library (the user-pattern hand-over is a lock-free `std::atomic<int>`, asserted at compile time).
- **No defines.** In particular **`ARP_PLANTS` must NOT be defined** in horde's build: it compiles in the test-only fault switches of the suite
  (`arp::Plants`) and the shipped library must not contain them. `arp::plantsCompiledIntoLibrary()` returns `false` in a correct build.
- `ARP_FORCE_INT128_FALLBACK` is optional and only for testing: the core uses `__int128` where the compiler has it and a two-word fallback
  otherwise (MSVC), selected automatically, same results on both paths (the suite runs the whole core on each).
- **Never `-ffast-math` / `-ffinite-math-only`**: `core.cpp` refuses to compile under them (`#error`, IEEE semantics for the host's float inputs).
- Warning-clean under `-Wall -Wextra` (`/W4` on MSVC is the project's own setting, untested by CI).
- **Memory:** `sizeof(arp::Core)` is about 95 KiB of fixed tables; put it on the heap or as a member of a long-lived object, not on a small stack.
  `prepare(sampleRate, maxBlock)` allocates the output buffer (`max(1024, 4 * maxBlock)` events) and the step log; nothing else allocates after
  construction. The full figure, worst case included, is printed by `bash tools/core_bench.sh`.
- **Stack on the audio thread (measured, arm64 clang `-O2`; other compilers differ):** `process()` runs on the caller's stack and its deepest chain of frames is about 16 KB (frame sizes from clang `-Wframe-larger-than`, summed along one call chain):
  `evalStep` 9.8 KB + `notesFor` 4.2 KB + `process` 1.8 KB, with the chord stage's `chordRebuild` / `chordIncremental` about 5 KB each, one at a time. The largest single
  frame is `sortSegment`'s 12 KB (`Key keys[512]`, a table of 24-byte sort keys for a run of up to 512 events at one offset). Run the audio thread on a stack of 64 KB or
  more (every ordinary audio thread has megabytes); do not call `process()` from a thread with a tiny fixed stack. Nothing allocates on it: the figures are frames, not heap.
- **Threads:** `process()` / `reset()` on the audio thread; `prepare()` off it; `setUserPattern()` / `getUserPattern()` off the audio thread,
  one writer at a time (SPEC 4.6).

## CPU envelope

What the readiness notice quotes. horde's slice for Arp is 2 % of a min-spec core. **The x4 figures are horde's min-spec heuristic (this machine's cost times 4), not min-spec verified:
no min-spec hardware was measured.** Everything here is a Release build (`-O3 -DNDEBUG`, never `-ffast-math`) of the shipped core (no `ARP_PLANTS`) on an **Apple M3** (macOS, Apple clang 16, a shared
machine), both int128 paths (native `__int128` and the two-word fallback), 44.1 / 48 / 96 / 192 kHz, blocks of 16 / 32 / 64 / 128 / 512 samples, measured 2026-10-25 (M3c, D-032); the note-randomness rows were measured 2026-10-27 (D-036) and are labelled as such.
"Raw" is the percentage of one core in real time; "x4" is raw times 4. Each configuration is the median of 15 repetitions of 1 s of audio; the table below takes, per configuration, the least disturbed
of three interleaved passes (other work ran on the machine; a single pass read up to 1.7x higher). Reproduce: `bash tools/core_bench.sh --reps 15 --input-seconds 1` (the default run, which
`./verify full` prints, takes fewer repetitions for the input scenarios and is noisier).

**Settings alone (nothing arrives but the held keys): every load fits.** 720 configurations per int128 path, none over the slice. **The heaviest is B with ratchet 4, a trigger, quantize in a chromatic own scale
(all 12 degrees on) and the chord stage in degrees** (32 keys on one channel, 128 chord notes, 1/64 at 999 bpm, gate 200; `WORST CASE B+` in the bench), because a chromatic scale snaps nothing and
the 128 chord keys stay 128 distinct keys (a major scale folds them onto about 75). Measured edge, x4 at 192 kHz in blocks of 16, native / two-word: **B+ 1.54 % / 1.68 %; with six chord shifts 1.53 % / 1.68 %; with
4 transposition steps (B++) 1.53 % / 1.69 %** (the single passes of the critic's re-measurement read 1.63 % / 1.73 %). Then the 112-note, ratchet-4 case 1.46 % / 1.59 %; D-031's load A (32 keys on 16 channels, 128 chord notes,
8 transposition steps) 1.20 % / 1.27 %; load B 0.85 % / 0.91 %.

**Note randomness at 100 (D-036): every settings-only load still fits.** `vel_random`, `gate_random` and `random_curve` at 100 make every sub-note of every note draw twice (velocity and length). It is the one
feature that adds cost per note, so the bench runs the three heaviest loads with all three at 100 (`... with randomness` in `bash tools/core_bench.sh`; they are the only bench scenarios outside
`tests/core/stream_digest_bench.txt`, they have `tests/core/stream_digest_random_bench.txt`). First version of the feature (`ed943b2`) cost x1.35-1.5 on those loads and read over the slice; the shipped one costs
**x1.08 on B++ (up to x1.14 in one configuration), x1.20 on the 112-note chord (up to x1.32), x1.07-1.12 on A at ratchet 4 (up to x1.23)**, output byte-identical to the first version
(`tests/core/stream_digest_random*.txt` pin it). At 0 the cost is nil (no draw is made) and the loads above are unchanged.

| load, all three randomness settings at 100 | x4 at 192 kHz, block 16, native / two-word | worst of 4 rates x 5 blocks, native / two-word | cost against the same load without it |
|---|---|---|---|
| B++ | 1.69 % / 1.76 % | 1.69 % / 1.81 % | x1.08 (up to x1.14) |
| the 112-note, ratchet-4 chord | 1.75 % / 1.86 % | 1.80 % / 1.91 % | x1.20 (up to x1.32) |
| A at ratchet 4 | 1.54 % / 1.73 % | 1.60 % / 1.77 % | x1.07 native, x1.12 two-word (up to x1.23) |

**Superseded by the idle re-measure (D-042, below).** These are **estimates, not quiet-machine readings**: the machine never read as quiet on 2026-10-27 (the same B+ read 2.25 % where 2026-10-25 read 1.54 %), so each figure is this session's
reading (least disturbed of 4 passes, the old and new builds run at the same moment, native and two-word, 44.1-192 kHz, blocks of 16 / 32 / 64 / 128 / 512) scaled by the same load's quiet figure above divided by
this session's reading of it (B++ x0.68 / x0.69, the 112-note chord x0.73 / x0.74; A at ratchet 4 uses B++'s). The ratio column is measured and needs no scaling. **The worst is 1.91 % after x4 (the 112-note chord, two-word
path); 1.80 % is the worst on the native path (the same load).** That is inside the slice with 4.5 % of it to spare on the two-word path and 10 % on the native one; it is not inside the 10 % headroom the notice's limit keeps. Not min-spec verified.

**Dense incoming notes (D-032).** The cost of input is linear in its rate, so the number to quote is the cost of one incoming note event (a NoteOn or a NoteOff) and the room that is left. After M3c, per
incoming event, **for the shapes measured** (the bench's input scenarios; before M3c in brackets). The cost depends on the shape and the load, not on one constant: the ranges cover every load measured, and a shape not
in the table can fall outside them.

| what the host sends | cost per incoming event |
|---|---|
| a held 32-key chord re-struck whole (every key off, then on, in one sample) | 46-65 ns on the heavy loads, about 0 on load A  (was 93-105 / 29-41 ns) |
| a few keys re-struck over and over (the off and the on in different samples: two held-set changes) | 150-360 ns  (was 530-700 ns) |
| one ref of a key held by four refs re-struck over and over, six chord shifts | 200-225 ns |
| presses on keys that are not held, all refused (32 keys x 4 refs held), chord stage off | 58-80 ns  (was 2,480-2,690 ns) |
| the same, chord stage on | 60-100 ns  (was 2,730-2,850 ns) |

The largest incoming rate that keeps the whole module inside the slice (x4 <= 2 %), as the worst over the four sample rates, five block sizes and both int128 paths. It depends on what the rest of
Arp costs, so it is given for three loads: **A** (D-031's, x4 about 1.1-1.3 % alone), **A at ratchet 4** (x4 about 1.5-1.8 % alone) and **B+ / B++**, the heaviest settings-only loads above (x4 1.45-1.69 % alone, so
0.3-0.5 % is left for input). `~` marks a rate beyond the highest one measured (read off the measured slope); the others lie between measured rates.

| host input | on load A | on A at ratchet 4 | on B+ / B++ (heaviest) |
|---|---|---|---|
| a whole 32-key chord re-struck (the realistic shape) | no limit found (the cost is below the noise up to 17,000 events/s measured) | 11,600 events/s (measured to 34,000) | 16,500 events/s (measured to 17,000) |
| a few keys re-struck over and over | ~9,700 events/s (measured to 8,500) | 3,100 events/s | 3,600 events/s |
| one ref of a 4-ref key re-struck over and over, six shifts | | 4,300 events/s | |
| a flood of refused presses on new keys, chord stage off | ~66,000 events/s (measured to 8,000) | ~53,000 events/s (measured to 17,000) | ~44,000 events/s (measured to 17,000) |
| the same, chord stage on | ~24,000 events/s (measured to 8,000) | 9,000 events/s (measured to 17,000) | 9,300 events/s |

**Superseded by D-042 (below).** The reading for the notice (the human's choice, D-033): **over the heaviest load measured (B+ / B++), any of these shapes stays inside the 2 % slice with 10 % headroom (x4 ≤ 1.8 %) up to 1,300
incoming note events per second (x4, not min-spec verified).** That is the quoted limit. The measured edge of the slice itself is 3,600 events/s (4 keys re-struck, two-word path, 192 kHz, block 16); at
2,500 that worst combination reads about 1.90 % after x4 (the load alone is 1.68 %), only 5 % of the slice below the limit, and the figure moves by 0.05-0.1 % with the state of the machine, which is why
the quote keeps 10 % headroom instead. Over load A the same shapes hold to 9,000. Over a lighter load the room grows: for a load that costs `L` % after x4 alone, the rate is about
`(2 - L) / (c * 4e-7)` events per second, with `c` the cost per event in ns from the first table. A typical 16-key chord load costs well under 0.5 % after x4 (the bench's "heavy but playable" load: 0.13 %), which leaves
1.5 %: about 60,000 events/s at 60 ns an event, 15,000 at 250 ns. These are stress rates, not playing rates (a chord re-struck at every 1/64 step at 999 bpm is about 17,000 events/s). The core stays correct at any rate;
only the CPU grows.

**Superseded by D-042 (below).** **With note randomness at 100 (D-036), the quoted 1,300 does not hold on the heaviest loads, and the figure to quote is re-measured before the readiness notice (D-038).** An incoming event costs the same with
randomness on (the 4-keys-re-struck slope on B++ is 8.5e-5 against 9.4e-5 % per event/s, two-word, 192 kHz, block 16, this session: noise); what moves is the load under it. **With randomness at 100 the heaviest
settings-only load is no longer B++ but the 112-note, ratchet-4 chord** (1.80 % / 1.91 % in the table above, native / two-word), and on it **no room is left under the 1.8 % line (10 % headroom) on either path**;
the 2.0 % edge itself is at roughly 2,000 events/s (native) and 1,000 (two-word), if its per-event slope is B++'s (that load has no measured input family). On B++ with randomness (1.69 % / 1.76-1.89 %; the two-word range is
the two ways it was measured, x1.04 in the settings-only passes and x1.12 in the 4-keys family) the room under 1.8 % is roughly 1,100-1,200 events/s native and 0-450 two-word, at a slope of 9-10e-5 % per event/s.
With randomness at 0, or on lighter loads (A at ratchet 4 with randomness: 1.54-1.77 %), the 1,300 stands. All of this is the scaled estimate described above, not a quiet-machine reading; the human's
choice (D-038) is to re-measure on a quiet machine and then quote one figure that covers every setting. The bench families `... + randomness 100` measure it (`bash tools/core_bench.sh --only "randomness 100"`).

### The quoted input limit (D-042, idle re-measure 2026-10-29)

**The figure for the readiness notice: up to 1,000 incoming note events per second (NoteOn plus NoteOff, host side), with every setting, randomness at 100 included, on both int128 paths, at 44.1-192 kHz and blocks of 16-512, inside the 2 % slice (x4, not min-spec verified).** D-033's separate 10 % CPU margin is dropped.

**Read this with the margin, which is thin (the human's choice: state it plainly).**
- **The tightest case:** the two-word path at 192 kHz, randomness at 100, on the heaviest loads.
- **Before any input arrives,** the settings alone read 1.91 % x4 by wall clock, and up to 1.97 % in a noisier CPU-time pass.
- **At 1,000 events/s,** the binding shape below reads about 1.97 %, interpolated between its measured 0 and 2,131 events/s points (1.909 % and 2.042 %). That leaves about 1.5 % of the slice, which is less than the roughly 3 % a figure moves between runs.
- **So in that corner, Arp sits within measurement noise of its slice.** Lowering the input figure would barely help, because the settings alone leave only about 4.5 %.
- **Everywhere else** the room is larger:
  - the native path's worst is 1.79 % alone;
  - with randomness off the two-word worst is 1.72 %;
  - lighter loads leave much more.
- For scale, a fast player sends about 40 events/s.

**Conditions.** The run took place at 12:00 on 2026-10-29, after a fresh restart, with the battery charged, OneDrive paused and nothing else running.
- Machine: the same Apple M3, Release build, main `a90f03c`'s core.
- **Quiet rule:** the run started when the 1-minute load was below 1.5 and the 5-minute below 2.0 (Maw's rule; this Mac idles near 2 with the desktop and the Claude app open). Load rose to 2.2-2.5 during the run, the bench's own core included.
- **Timing:** `bash tools/core_bench.sh` measured twice, once by wall clock and once with `--cpu-time` (this thread's CPU time, which other processes cannot inflate, though they can still disturb caches and clocks).

**Settings alone: every load fits on both paths** (0 of 195 configurations over, both timings). The worst x4 over 4 rates and 5 blocks, by wall clock (spread 1-4 %):

| load | native | two-word |
|---|---|---|
| B++ with randomness 100 | 1.79 % | 1.91 % |
| the 112-note, ratchet-4 chord with randomness 100 | 1.72 % | 1.91 % (1.97 % in the CPU-time pass, spread 9.6 %; 1.94 % at 44.1 kHz with spread 37 %) |
| A at ratchet 4 with randomness 100 | 1.75 % | 1.87 % |
| B+ / B+6 / B++ (no randomness) | 1.62 % | 1.72 % |
| the 112-note chord (no randomness) | 1.43 % | 1.52 % |

The worst is at 192 kHz, block 16. Against 2026-10-25 on the same loads, B+ / B+6 / B++ read up to 6 % higher, and the 112-note chord, A and B read 0-6 % lower. D-039 accounts for x1.004-1.02, measured by building the pre-D-039 core (`e1dbd3c`) and alternating it with the current one, three runs each, `--cpu-time`. The ratio is x1.015-1.02 at 48, 96 and 192 kHz and x1.004-1.013 at 44.1 kHz.

**The binding input shape:** 4 keys re-struck over and over, on B++ with randomness 100, on the two-word path at 192 kHz, block 16. Its edge is **1,461** (wall clock), **1,416** and **1,415** events/s (`--cpu-time`, the second with 9 repetitions). The lowest edge of every other input family, over both runs:
- the same shape native: 2,507;
- 4 keys re-struck, two-word, 192 kHz: on B+ 3,280, on B++ 3,390, on A at ratchet 4 3,507;
- the new family on the 112-note randomness chord: 2,782-2,845 two-word and 3,857-4,142 native at 192 kHz (its first, short wall-clock pass also read 2,112 native at 44.1 kHz);
- floods of refused presses with the chord stage on: 2,561 and up (the lowest on B++ with randomness, two-word, 192 kHz);
- one ref of a 4-ref key re-struck: 3,804 and up;
- whole-chord re-strikes: 4,145 and up;
- floods with the chord stage off: no limit found up to the 17,000 measured.

**Readings below the binding figure, and why they are not taken as cost:**
- 456 events/s on the chord-stage-on flood over B++, two-word, 44.1 kHz. Four re-runs read 11,300-12,700.
- 1,532 on B++'s 4-keys family, native, 48 kHz. In four re-runs that configuration read 4,124 once and reached no limit up to 4,263 three times. The family's 96 and 192 kHz configurations read 3,930-4,252.
  - One re-run read 1,976 in another configuration of the same family (96 kHz, block 512).
  - So a sub-2,000 reading did recur in this family, in a different place each time. That pattern looks like scheduling, not cost, but it is a judgement, not a proof.
- Three passes read a family's load alone as over the slice in one configuration each: two `--cpu-time` passes and the new family's first wall-clock pass (48 kHz, block 128). The other configurations of the same loads read 1.4-1.8 %.

**What is measured and what is inferred.**
- **Measured:** each load's cost at each rate, rate of input, block and path above.
- **Inferred:**
  - **Each input edge** is a straight line drawn between two measured input rates.
  - **The x4 min-spec factor** is horde's heuristic.
  - **"Every setting" assumes** input cost adds to a load the same way on loads that have no input family of their own. A at ratchet 4 with randomness (1.87 % alone, two-word) has none. Among the randomness loads, only B++ has flood families, and none has a one-ref family (the only one-ref family is on A at ratchet 4 without randomness).
  - **Blocks under 16 samples** were not measured, although Arp's output is defined for blocks of 1-4096.
  - **Block-512 figures on the heaviest loads may under-read.** At 512 samples their output overflows the buffer, so less work is done. For example, the re-struck chord on A at ratchet 4 emits about 165,000 events/s instead of 264,000. The binding case is at block 16, where nothing overflows.
- **Nothing gates this figure:** CPU is measured, never gated (the charter), so a later cost increase would make it stale silently. Re-measure before any release that touches the core.

Reproduce:
- `bash tools/core_bench.sh` and `bash tools/core_bench.sh --cpu-time`;
- the binding family: `build/bench/arp_bench_soft --cpu-time --reps 9 --input-seconds 0.5 --only "4 keys re-struck: B++ chromatic transposed + randomness"`.

## How this list is kept honest

`bash tools/core_test.sh` (and so `./verify fast`) does, every run:

1. fails if any listed file is missing or its sha256 is stale, or if any file under `core/` (other than `CMakeLists.txt`) is missing from the list;
2. copies exactly the listed files into an empty scratch directory and builds `tests/core/shipped_smoke.cpp` from them alone (C++20,
   `-Wall -Wextra -Werror`, no defines, nothing of the repository's `core/` on the include path) and runs it;
3. proves each of those detectors with a planted fault first: a list with a row removed, a list with a wrong hash, and, for every listed file in
   turn, a copy that omits it (the build must fail).

When `core/` changes on purpose, regenerate the table with `bash tools/core_test.sh copyin-table` and paste it above.
