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
