# Arp core: the copy-in file list for horde (D-015)

> **Origin.** Arp project, M3a dispatch of ROADMAP T-007, 2026-10-24 (base commit `ff115db` plus the M3a working tree). horde's
> note-processor 1.0 bar says the core is copied into horde's tree at Arp's V1 by a horde PR that records Arp's commit (D-015,
> Q-H2 ANSWERED). This file is the contract for that copy: **exactly these files, nothing else.**
> Last verified: 2026-10-25 by `bash tools/core_test.sh` (the check described at the end). The CPU envelope below was measured on 2026-10-25 (M3c, D-032).

## What horde copies

Keep the relative layout: `core/include` is the include root (`#include "arp/core.h"`), `core/src/core.cpp` is the one translation unit.

| file | sha256 |
|---|---|
| `core/include/arp/arith.h` | 702d89b13848b554a6e32392f5f48a22bbdd157e8a531270f194e0a103f37162 |
| `core/include/arp/bank_data.h` | 87876a9f7681733500c17e65463f043b1be4f5bda8a77aff5949347c3a4b3723 |
| `core/include/arp/core.h` | 305a41e73dcd76790ecd183b4c5b55d5d68bb93eb0eddad8adcbf075d5da7ea0 |
| `core/include/arp/draw.h` | 8ea2c17c6eb55baa621fd37659c7f19bcb6c46b0304d59870d371eca0ca8dc92 |
| `core/include/arp/int128.h` | bb2c26a4cea9e04015c18957bf81a0d76b10f332ba72c86416fa41065112b433 |
| `core/include/arp/pattern.h` | 1ad8f47861d91587aa6df2b6180d85cfc7d611fc8001769d42547cf1714e3899 |
| `core/include/arp/types.h` | b45f811cdc241a5bf3ab6d76d4cc9fb8b168cb195870cbfdb452afab6879133b |
| `core/src/core.cpp` | a42b6b8cccb78f122bec15e3cbdb51efbefbb5e722d47fa91049288ef41eab17 |

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
machine), both int128 paths (native `__int128` and the two-word fallback), 44.1 / 48 / 96 / 192 kHz, blocks of 16 / 32 / 64 / 128 / 512 samples, measured 2026-10-25 (M3c, D-032).
"Raw" is the percentage of one core in real time; "x4" is raw times 4. Each configuration is the median of 15 repetitions of 1 s of audio; the table below takes, per configuration, the least disturbed
of three interleaved passes (other work ran on the machine; a single pass read up to 1.7x higher). Reproduce: `bash tools/core_bench.sh --reps 15 --input-seconds 1` (the default run, which
`./verify full` prints, takes fewer repetitions for the input scenarios and is noisier).

**Settings alone (nothing arrives but the held keys): every load fits.** 720 configurations per int128 path, none over the slice. **The heaviest is B with ratchet 4, a trigger, quantize in a chromatic own scale
(all 12 degrees on) and the chord stage in degrees** (32 keys on one channel, 128 chord notes, 1/64 at 999 bpm, gate 200; `WORST CASE B+` in the bench), because a chromatic scale snaps nothing and
the 128 chord keys stay 128 distinct keys (a major scale folds them onto about 75). Measured edge, x4 at 192 kHz in blocks of 16, native / two-word: **B+ 1.54 % / 1.68 %; with six chord shifts 1.53 % / 1.68 %; with
4 transposition steps (B++) 1.53 % / 1.69 %** (the single passes of the critic's re-measurement read 1.63 % / 1.73 %). Then the 112-note, ratchet-4 case 1.46 % / 1.59 %; D-031's load A (32 keys on 16 channels, 128 chord notes,
8 transposition steps) 1.20 % / 1.27 %; load B 0.85 % / 0.91 %.

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

The reading for the notice (the human's choice, D-033): **over the heaviest load measured (B+ / B++), any of these shapes stays inside the 2 % slice with 10 % headroom (x4 ≤ 1.8 %) up to 1,300
incoming note events per second (x4, not min-spec verified).** That is the quoted limit. The measured edge of the slice itself is 3,600 events/s (4 keys re-struck, two-word path, 192 kHz, block 16); at
2,500 that worst combination reads about 1.90 % after x4 (the load alone is 1.68 %), only 5 % of the slice below the limit, and the figure moves by 0.05-0.1 % with the state of the machine, which is why
the quote keeps 10 % headroom instead. Over load A the same shapes hold to 9,000. Over a lighter load the room grows: for a load that costs `L` % after x4 alone, the rate is about
`(2 - L) / (c * 4e-7)` events per second, with `c` the cost per event in ns from the first table. A typical 16-key chord load costs well under 0.5 % after x4 (the bench's "heavy but playable" load: 0.13 %), which leaves
1.5 %: about 60,000 events/s at 60 ns an event, 15,000 at 250 ns. These are stress rates, not playing rates (a chord re-struck at every 1/64 step at 999 bpm is about 17,000 events/s). The core stays correct at any rate;
only the CPU grows.

## How this list is kept honest

`bash tools/core_test.sh` (and so `./verify fast`) does, every run:

1. fails if any listed file is missing or its sha256 is stale, or if any file under `core/` (other than `CMakeLists.txt`) is missing from the list;
2. copies exactly the listed files into an empty scratch directory and builds `tests/core/shipped_smoke.cpp` from them alone (C++20,
   `-Wall -Wextra -Werror`, no defines, nothing of the repository's `core/` on the include path) and runs it;
3. proves each of those detectors with a planted fault first: a list with a row removed, a list with a wrong hash, and, for every listed file in
   turn, a copy that omits it (the build must fail).

When `core/` changes on purpose, regenerate the table with `bash tools/core_test.sh copyin-table` and paste it above.
