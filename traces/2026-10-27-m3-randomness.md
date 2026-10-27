# m3-randomness — velocity and gate randomness with a shared curve (D-036, a recorded exception to the 1.0 freeze): core, lab, RN1 checks, CPU

- **Queue item:** T-007 (ROADMAP M3), decision D-036, 2026-10-27, on `m3/randomness` at HEAD `3f76354` (spec text and `spec/params.json` already in that commit). Working tree uncommitted (the lead commits;
  no git operation was made; `git show` read-only to build an old-core benchmark in a scratch directory). Authored by an implementer dispatch from the Arp lead session.
- **Why:** the human lifted the freeze for `vel_random`, `gate_random` and `random_curve` alone. The rule is SPEC 4.4 (velocity), 4.9 (the draw and the curve), 5.2 (length, ratchet cap, tie tail), 10 (lanes 3 and
  4), 11 row RN1. Defaults are 0, so the output is byte-identical to the core without the feature; the committed digests (cut from `0ef0903`) were NOT regenerated and still match.

## What changed

1. **Core** (`core/include/arp/types.h`, `core.h`, `core/src/core.cpp`). Public interface: three new `Settings` fields only (`velRandom`, `gateRandom`, `randomCurve`, int32, after `velFixed`). `Core::randomU(x, lane, curve)`
   (public, const, next to `draw`) is the spec's U: one draw split into bytes, U1 / U4, the curve blend, integer math with `fdiv`; at curve 0 and 100 it computes only the value that survives (the blend is exactly
   that value). `evalStep` reads the three settings at the step's evaluation (continuous, like `gate`), builds `x = kAbs * 1024 + sub * 256 + n` with `n` = the note's index in style order (kept as `Out::src`, before dedupe
   and drops), draws only when an amount is above 0, and applies: velocity after accent / step vel (`clamp(out + fdiv(out * amount * U + 50000, 100000), 1, 127)`), length from the gate-derived length BEFORE the
   ratchet cap (`max(1, len + fdiv(len * amount * U, 100000))`, then the cap), and the same factor on the tie's tail term for the last sub-note. Chord-stage notes are never touched. `adopt()` masks the three fields like
   `gate` (a change is not a discrete T8 change); the T9 bookkeeping counts them. Nothing allocates.
   New plants (test-only, under `ARP_PLANTS`): `randomFromBlock`, `randomIgnoresCurve`, `randomChordStage`.
2. **RN1 checks** (`tests/core/test_random.cpp`, new; registered in `CMakeLists.txt`, `test_core.cpp`, `harness.h` plant table and session fingerprint, and `tools/core_digest.sh`'s suite list). 59 checks per
   binary, 21 of them plants, on both int128 paths. Every session runs through `runOnce()`, never `run()`, so none is recorded in the committed digest. Byte-identity at 0 (the digests ARE the check; plus the
   digest header still names `0ef0903`, and the curve alone changes nothing); known answers against Python literals (a separate implementation) and against a reference written in the test from the spec text
   (clamps at 1 and 127, the 1-tick minimum at 192 kHz, the ratchet cap, a tied tail, a chord step's per-note index); bounds at amounts 10 / 40 / 100 and curves 0 / 100; distribution over 24,000 notes (flat at curve 0,
   variance 0.2506 of uniform's at curve 100); chord-stage notes untouched; N1 / G1 (blocks 4096-1) / G4 (44.1-192 kHz) / A1 over a session with all three settings non-zero and changing; a 40-session seeded fuzz of NEW sessions.
3. **Params check** (`tools/core_params_check.py`, `--dump-defaults` in `test_core.cpp`): 50 settings; the docstring says 50.
4. **Lab** (`lab/arp-lab.html`): the same rule in CORE (`_randomU`, the ratchet loop); `ARP_PARAMS` regenerated from `spec/params.json`; the three sliders come from params.json by the existing UI loop. One lab plant,
   `randomIgnoresCurve`. `tools/lab_smoke.mjs`: an RN1 block with the SAME Python literals as the C++ suite, the identity check, G1 and N1 over a randomness session, each with its plant.
5. **Bench** (`tests/core/bench.cpp`): two scenarios with all three at 100 (`WORST CASE B++ with randomness`, `WORST CASE for events per block with randomness`), marked `noDigest` with the reason, so
   `stream_digest_bench.txt` (837 configurations, from `0ef0903`) is unchanged and still matches on both int128 paths. They now appear in the bench report's "heaviest settings-only scenario" line.
6. **Copy-in table** (`docs/horde-copy-in.md`): regenerated (`core.h`, `types.h`, `core.cpp` changed).

## CPU (Release, this machine, which was NOT quiet: load average 5-23 throughout; ratios are the evidence, absolute figures are inflated by about 25 % over docs/horde-copy-in.md's)
Old core (`3f76354`, built in a scratch directory), new core with randomness at 0, new core with all three at 100, interleaved, best of 6 runs per configuration, max over block sizes, x4 after:
- B++ (heaviest settings-only): old 1.85-2.03 %, new at 0 1.80-2.03 % (zero path costs +0-4 %), at 100: 2.43-2.60 % (x1.35 median of per-configuration ratios, range 1.16-1.48). Raw 0.46-0.51 % -> 0.61-0.65 %.
- 112-key ratchet-4 chord (events): old 1.56-1.69 %, new at 0 1.59-1.71 %, at 100: 2.37-2.55 % (x1.52, range 1.41-1.58).
- Cost per randomised note: about 11 ns (two draws, four floor divisions), from the extra 0.15 % of a core over 132,000 notes/s.
- Reading: with all three at 100 the two heaviest loads are OVER the 2 % slice after x4 by about 25-30 % of the slice on this busy machine; scaled to the quiet-machine baseline of docs/horde-copy-in.md (B++ 1.45-1.69 %)
  the estimate is 1.96-2.28 %. Ordinary loads are far inside (the bench's heaviest non-worst-case load is 0.14-0.22 % after x4 without randomness, +11 ns per note with it). Not min-spec measured.

## Evidence consulted
`spec/SPEC.md` 4.4, 4.9, 5.2, 9, 10, 11; `DECISIONS.md` D-036; `spec/params.json`; `core/include/arp/*.h`, `core/src/core.cpp` (`evalStep`, `adopt`, `chordStart`); `tests/core/harness.h`, `test_digest.cpp`,
`digest_corpus.cpp`, `bench.cpp`; `tools/core_test.sh`, `core_digest.sh`, `core_params_check.py`, `lab_smoke.mjs`; `docs/horde-copy-in.md`. Python reference (scratch): D1's known answers reproduced first.

## Alternatives rejected
- Recording the RN1 sessions in the digest, or adding randomness draws to `digest_corpus.cpp`'s generator: either moves the committed digest. The new sessions use `runOnce()` and their own generator.
- Reading `n` from the key's rank among the held keys: the spec says index in the step's notes in style order (a single-note step is n = 0). `Out::src` carries it through dedupe.
- A shared `draw` for both lanes: SPEC 10 makes lane part of the hash; two draws per note-sub-note are the spec.

## Verify
`bash tools/core_test.sh` exit 0 (514 passed, 0 failed on each int128 path; stream digest, bench digest x2, copy-in all green); `python3 tools/core_params_check.py` exit 0 and `--plant` exit 1;
`node tools/lab_smoke.mjs` exit 0; `bash tools/core_sanitize.sh ubsan` exit 0; `./verify fast` exit 0; `./verify full` exit 0 (`.harness/last-verify.json`: git `3f76354`, tree uncommitted).

## Open questions
- SPEC 5.2 "the same factor applies to the tie's tail term": implemented as the note's own U (lane 4, sub-note r-1) applied to the tail term; the spec does not say which sub-note's U a tied ratchet uses. It is the last sub-note's,
  the only one the tail applies to. Worth one sentence in the spec if the lead agrees.
- SPEC 9's list of continuous settings (line 833) does not name the three new ones; 4.9 does. Left alone (spec is out of scope).
- The bench report's "heaviest scenario built from settings alone" now names the randomness scenarios and reads OVER the slice (they are deliberate worst cases). Whether the report should keep them in that line or in a
  separate one is the lead's call; the docs figures for horde are unchanged by this dispatch.
- With all three at 100 the heaviest loads exceed the 2 % slice after x4 (see CPU). horde's notice may want that stated, or the settings capped for those loads: not decided here.

## Follow-up (same day, on `ed943b2`): the randomness path made cheap, pinned by a digest; regeneration from 0ef0903 kept working; the CPU envelope updated

- **Queue item / why:** T-007, D-036 with D-031's rule that every settings-only load fits the 2 % slice. The first version cost x1.35-1.5 on the heaviest loads (B++ and the 112-note chord read 2.4-2.6 % after x4 against 1.9-2.0 % without). Authored by an implementer dispatch from the Arp lead session; working tree uncommitted, no git operation made (`git show` read-only, as the digest script does).

### 1. The pin (before any optimisation)
`tests/core/stream_digest_random.txt` (451 runs: every clean run of `tests/core/test_random.cpp`, numbered in the order made, hashed over every event field, step record and counter) and `tests/core/stream_digest_random_bench.txt`
(the bench scenarios that use randomness, `arp_bench --digest-random`: 56 configurations) were generated from the **core of `ed943b2`** (`bash tools/core_digest.sh regen-random ed943b2`, new mode) BEFORE the core was touched, and are
compared on every run: `arp_tests` / `arp_tests_fallback` (both int128 paths) and `core_test.sh` (both bench binaries). Each has its plant (a digest digit changed, a line missing; `--digest-random-perturb`: one field of one event). A new
`testSweep` adds 202 sessions (every `random_curve` 0..100, every amount 0..100, chord x2 / x3 ratchets with an accent) so the digest covers every curve and amount, not only the ones the checks name. The 26 bench lines that existed
when the file was first cut are unchanged in the final file (30 lines were added later for the new input families, again cut from `ed943b2`: `diff` shows 0 removed, 30 added; the RN1 file `cmp`-identical after the re-cut).
**After each optimisation step the 451 runs and 56 configurations matched; they match in the final tree on both int128 paths and under UBSan.**

### 2. What made it cheaper (core only; output byte-identical)
- `kU`: U1 (256 entries) and U4 (1,021) as `constexpr` tables built from the spec's own `fdiv` formulas; the blend divides unsigned by 100 after adding the offset (`(mix + 100000) / 100 - 1000`), exact for negatives; at curve 0 and 100 only one table is read.
- Velocity and length deltas: the floor division by 100000 done in unsigned arithmetic (`+13,000,000` offset for the velocity numerator, which is >= -12.65e6; sign split for the length delta). No `fdiv`, no remainder test.
- `draw.h`: `drawBase` / `drawWith` (the same three multipliers, the draw split into "constant for the step" and "per note"). `evalStep` draws a sub-note's lane 3 and lane 4 for all its notes in two plain loops over 32-bit integers (the compiler vectorises them);
  lane 3 only if `vel_random` > 0, lane 4 only if `gate_random` > 0 (kept from the first version). The plant `timeInDraw` keeps its stateful per-draw path.
- Hoisted per sub-note: the on position's `I128::mul`, and the off position's unless the length is randomised; a randomised length that the ratchet cap brings back to the plain one (often, at a long gate) reuses it. This helps the loads without randomness too (two-word path about 5 % cheaper).
- Rejected: a fast path in `I128Soft::mul` for small operands (no measurable gain on this machine, touches `int128.h` for every load: reverted); sharing one draw between the lanes (SPEC 4.9 and 10: lanes are separate draws); `I128` is not involved in the randomness arithmetic (all int64).

### 3. Regeneration from `0ef0903` works again (`bash tools/core_digest.sh regen 0ef0903`)
The M3a core cannot compile the tests that name the new `Settings` fields, so `core_digest.sh` now detects a core without `velRandom` and builds the suite and the bench with `-DARP_NO_RANDOM`, leaving out `test_random.cpp` and the few lines that name the fields
(`test_core.cpp`, `harness.h`'s session fingerprint, `bench.cpp`'s randomness scenarios). Proof, run in this tree: the two committed files copied aside, `regen 0ef0903` run (1 min 20 s, "wrote tests/core/stream_digest.txt (1320 chunks) and
tests/core/stream_digest_bench.txt (837 configurations)", header "generated from core/ at commit 0ef0903: ... core.cpp sha256 6bdfd594..."), then `cmp` on both: **identical**; `git status` shows no change to either file, so nothing needed restoring.

### 4. CPU (Release, `-O3 -DNDEBUG`, Apple M3; load average 5-55 throughout, so absolute readings run about 1.4x above the quiet ones of 2026-10-25: B+ read 2.25 % where it read 1.54 %)
Method: `ed943b2`'s core and the current one built from the same `bench.cpp`, native and two-word (`-DARP_FORCE_INT128_FALLBACK`), run **at the same moment** (two processes at once, so the contention is common), 4 passes (earlier trials with 3-6 passes agree), per configuration the least disturbed pass.
Also `arp_bench --cpu-time` (new: the thread's CPU time instead of wall time, for comparing builds on a busy machine). x4 after, the **maximum over blocks 16 / 32 / 64 / 128 / 512 per rate**, before (`ed943b2`) -> after:

| load | path | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| B++ + randomness 100 | native | 2.88 -> 2.36 | 2.89 -> 2.33 | 2.89 -> 2.41 | 2.97 -> 2.48 |
| | two-word | 3.25 -> 2.52 | 3.29 -> 2.53 | 3.25 -> 2.60 | 3.22 -> 2.53 |
| 112-note chord + randomness 100 | native | 3.24 -> 2.22 | 2.69 -> 2.14 | 3.06 -> 2.45 | 2.77 -> 2.38 |
| | two-word | 3.10 -> 2.36 | 2.94 -> 2.36 | 3.19 -> 2.57 | 3.27 -> 2.54 |
| A at ratchet 4 + randomness 100 | native | 2.79 -> 2.21 | 2.77 -> 2.25 | 2.77 -> 2.34 | 2.74 -> 2.26 |
| | two-word | 3.26 -> 2.48 | 3.36 -> 2.45 | 3.28 -> 2.55 | 3.31 -> 2.54 |
| B++ (no randomness, for the machine's state) | native | 2.18 -> 2.22 | 2.10 -> 2.11 | 2.33 -> 2.28 | 2.24 -> 2.31 |
| | two-word | 2.42 -> 2.29 | 2.42 -> 2.29 | 2.47 -> 2.34 | 2.57 -> 2.44 |
| 112-note chord (no randomness) | native | 1.85 -> 1.86 | 1.83 -> 1.85 | 1.89 -> 1.92 | 1.97 -> 1.99 |
| | two-word | 2.11 -> 1.99 | 2.11 -> 1.99 | 2.23 -> 2.11 | 2.27 -> 2.14 |
| A at ratchet 4 (no randomness) | native | 2.02 -> 2.13 | 2.06 -> 2.01 | 2.11 -> 2.27 | 2.18 -> 2.17 |
| | two-word | 2.34 -> 2.34 | 2.31 -> 2.19 | 2.39 -> 2.36 | 2.46 -> 2.37 |

Cost of randomness at 100 against the same load without it (per-configuration ratio, new core, same moment): B++ x1.08 native / x1.08 two-word (max 1.14); 112-note chord x1.20 / x1.20 (max 1.32 / 1.28); A at ratchet 4 x1.07 / x1.12 (max 1.15 / 1.23). The first version: x1.33 / 1.32, x1.46 / 1.42, x1.32 / 1.40.
The loads without randomness did not get slower beyond noise (per-rate maxima moved -5 % to +7 %; the two-word path is about 5 % cheaper from the hoisting).
**Quiet-machine estimate** (this session's reading x the load's documented quiet figure / this session's reading of the same load; the machine never read as quiet): B++ 1.69 % native / 1.81 % two-word at the worst configuration; the 112-note chord 1.80 % / 1.91 %;
A at ratchet 4 1.60 % / 1.77 %. **Everything inside the 2 % slice; the tightest is 1.91 % (the 112-note chord, two-word path): 4.5 % of the slice to spare there, 10 % on the native path (1.80 %).** Not inside the 10 % headroom (1.8 %) the notice's input limit keeps on the two-word path. Estimates, not measurements on a quiet machine; the ratios are measured. Not min-spec verified.

### 5. Input headroom (docs/horde-copy-in.md, "CPU envelope")
New bench families `B++ chromatic transposed + randomness 100` (re-struck chord, 4 keys, flood; in `stream_digest_random_bench.txt`). An incoming event costs the same with randomness on (slope 8.5e-5 against 9.4e-5 % per event/s, noise); the load under it grows
(B++ alone about 1.69 % native / 1.76-1.89 % two-word, quiet-equivalent). With the notice's per-event cost: **under the 1.8 % line, about 1,100 events/s native and between 450 and none two-word; the 2.0 % edge moves from 3,600 to about 3,100 (native) and 1,200-2,700 (two-word).** The quoted 1,300
(D-033) is therefore NOT kept with randomness at 100 on the heaviest load; stated in the docs, the quoted figure itself NOT changed (the lead's call).

### 6. Files / verify
Changed in the follow-up: `core/include/arp/draw.h`, `core/src/core.cpp`, `docs/horde-copy-in.md` (sha table, CPU envelope), `tests/core/{bench.cpp,test_random.cpp,test_core.cpp,harness.h,CMakeLists.txt}`, `tools/{core_digest.sh,core_test.sh}`; new `tests/core/stream_digest_random.txt`, `tests/core/stream_digest_random_bench.txt`.
`core_params_check.py`, `lab_smoke.mjs`, the lab and the spec are untouched in the follow-up. Verify: `bash tools/core_test.sh` exit 0 (517 passed, 0 failed per int128 path; the randomness digest, the main digests, both bench digests and the copy-in check green), `core_params_check.py` exit 0 / `--plant` exit 1,
`node tools/lab_smoke.mjs` exit 0, `bash tools/core_sanitize.sh ubsan` exit 0, `./verify full` exit 0 (see the report for the lines).

### 7. Open questions
- The two-word path is the one MSVC builds use; its tightest load is estimated at 1.91 %. Another 3-5 % would need per-note work in the pending queue (the off position is a 128-bit product per randomised note), not in the randomness arithmetic.
- `CODEMAP.md` / `README.md` do not yet list `stream_digest_random*.txt`, `regen-random` or `--cpu-time` (out of scope here).

## Lead integration and critic round (2026-10-27)

- **Lead checks:** regenerated all four committed digests into the tree (`regen 0ef0903`, `regen-random ed943b2`): each `cmp`-identical, restored. `./verify full` exit 0 at `ed943b2`+follow-up and again at `d91e80f`.
- **Merged `origin/main`** (#26 listening, #27 mailbox): DECISIONS conflict only (D-036 and D-037 both kept, in order).
- **Listening piece 17** (`tests/core/render.cpp`): plain, velocity 60, + gate 60, + curve 100; played at velocity 80. `render_listening.sh --check` byte-identical twice; the manifest shows velocity spread narrowing under the bell and every note start on the grid.
- **Independent critic (Opus, separate from the Sonnet author): APPROVE-WITH-NOTES, no HIGH.** Arithmetic and hoisting confirmed exact. M1 (the input caveat named B++ where the 112-note chord now binds) and L1/L2 fixed in `docs/horde-copy-in.md` and D-036's wording. M2 (n before the dedupe) and M3 (randomness across transport, time mode, latch, pedal, panic) closed by tests with plants (RN1 suite 527 per path; randomness digest 451 -> 760 runs, 0 old lines changed, cut from `ed943b2`). L3 (the lab mirrors one of three RN1 plants) and L4 (the private-name gate is skipped in a worktree) left as noted.
- **Human decision D-038:** re-measure on a quiet machine, then quote one input figure covering every setting, before the readiness notice.
- **Found by the edge fuzz, predates this branch:** a candidate G4 divergence at the free-clock -> host-clock hand-over (ROADMAP T-007 note), to diagnose before M3 closes.
