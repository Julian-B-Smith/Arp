# m2a-scale-feel — scale quantize, the degree unit, swing and velocity in the C++ core (M2a)

- **Queue item:** T-005 (ROADMAP M2), dispatch M2a of two, 2026-10-23, on `m2/core-scale-feel` (base `a683e57`, M1b's commit;
  working tree uncommitted, the lead commits). Authored by an implementer dispatch from the lead session. The degree unit
  moved here from M1b by D-025.
- **Why:** M2a makes the ratified spec's pitch and feel surface real in `core/`: the nearest-in-scale rule with the `lastQ` tie
  chain (SPEC 6), degrees as a transposition and step-offset unit with the six-stage order (4.7), swing through the pending queue
  (5.1) and the velocity modes (4.4), so Q1-Q5, G3, the degree parts of X1/ST1 and TM1's swing term can be judged. M2b
  (restart and repeats, morph, the chord stage) is untouched: those settings are still accepted and ignored.
- **Changed (`core/`):**
  - **Scale quantize (SPEC 6).** `quantizeKey` (nearest in-scale key within 0-127, a tie toward `lastQ`, lower on none or on
    equal distance), `inScale` (root-relative mask, `mod`). Effective scale per step: horde's `Block::scale` (read each block,
    never owned) or Arp's own `scale_root` + `scale_deg0..11`; `chord_root` re-roots the shape on the lowest held key (`E[0]`)
    and forces degree 0 on, only when the mask is non-zero (13b.1); a zero mask disables quantize. `lastQ` is `lastQ_` /
    `hasLastQ_`: set at step evaluation per key, cleared in `releaseAll()` (T4, T6, T7, T10 all pass through it) and in
    `resetState()`; not touched while quantize is off or the mask is zero.
  - **Degrees and the six-stage order (4.7, 4.3).** `evalStep` splits the pass shift (global unit) and the step offset (the
    pattern's unit) into `S` semitones and `G` degrees; per key, ascending pre-quantize order: `key + S`, drop, quantize,
    `degreeMove(G)` (walks in-scale keys; chromatic when quantize is off or the mask is zero), drop, then `lastQ` := the final
    key only if it survived. Collisions keep the earliest source of the style's order (list order).
  - **Swing (5.1).** The constant `kStraightSwing` is gone: `swing` is read (clamped 500..750) at each step's evaluation; the
    on-time, span, ratchet starts and the tie look-ahead already went through `onTick`. Ons and triggers were already in the
    pending queue, so a swung step is cancelled by release-all like any other pending entry.
  - **Velocity (4.4):** no code change was needed; M1b already had played / fixed / step accent. Only known answers were added.
  - **Public interface of `Core`: unchanged.** `Settings` and `Block::scale` already carried every field. `Plants` (test-only) grew
    17 members; `Pending` has one `#ifdef ARP_PLANTS` bool.
- **Changed (`tests/core/`):** `test_m2a.cpp` / `.h` (new, 80 of the new checks), `harness.h` (plant table; `Session::scaleAt`
  for horde's scale changing at a sample; `Session::prepares` for a second `prepare()`), `test_core.cpp` (the sessions
  `scalefeel` and `ties` join N1 and, for `scalefeel`, G4; `atRate` scales `scaleAt`), `test_m1b.cpp` (the 30 seeded sessions
  also draw quantize, own and global scales, degrees, swing, velocity modes, scale changes; drawn LAST so the earlier structure
  of every session is unchanged), `CMakeLists.txt` (one source).
- **Planted controls added (17, each fires in the suite's own output with its reason):** `skipQuantizeChord` (Q1),
  `tieAlwaysDown` (Q2/Q5), `ownIgnored` (Q3), `chordRootAsRoot` (Q4), `lastQAtEmission`, `lastQNoReset` (Q5),
  `collisionKeepsLast` (collisions, two cases), `degreesAsSemitones` (X1, also across 14 styles), `stepDegreesAsSemitones`
  (ST1), `semitoneAfterQuantize` (stage 1, and the mixed-kinds case), `degreeBeforeQuantize` (stage 4), `lastQPreMove`,
  `droppedKeySetsLastQ` (stage 6), `noStage2Drop` (stage 2), `swingParityK` (G3), `timeIgnoresSwing` (TM1's swing term),
  `releaseKeepsPending` (a pending swung on / trigger survives a Panic). `stepSizeIgnored` and `stepOffsetReadsGlobal` now also
  override the unit, so ST1's degree case fires them too.
- **Known answers (no plant, by M1b's convention):** the nearest rule at every key 0-127 in six scales against a brute-force
  oracle (two scales have a candidate off the end of 0-127), tie up / down / equal-distance, lastQ untouched while quantize is
  off, lastQ reset by Panic / transport stop / enable off-on / `reset()` / `prepare()`, lastQ follows evaluation (pending cap 1),
  zero mask, chord_root anchor moving with the lowest key and always admitting it, both collision directions, degrees at both
  ends of 0-127, quantize/scale/swing/vel changes taking effect at the next evaluated step with a sounding note's off unmoved,
  swing ticks (667, 750, clamp, mid-run change, pre-roll negative parity), ratchets and ties under swing, a pending on and
  trigger cancelled by a Panic while an already-emitted trigger is not, velocity (accent over played and fixed, rounding, fixed
  clamp before the accent, per-key chord velocity, continuous vs discrete change).
- **Evidence consulted:** SPEC 4.3, 4.4, 4.7, 5.1, 5.2, 6, 7, 8, 9, 11 (G3, Q1-Q5, X1, ST1, TM1), 12, 13, 13b.1, 13b.3, 13b.4,
  13b.6, 13b.11; ROADMAP M2 / T-005; `traces/2026-10-23-m1b-styles-patterns.md`; the lab CORE (`_play`, `_quant`,
  `_scaleOf`, `_degreeMove`) for intent only. Expected values are written from the spec text: the quantize oracle collects every
  in-scale key and picks by distance, degree moves are an index walk over the C-major key list, swing and tie ticks are numbers.
- **Mutation check (mine, not committed):** 39 hand-made faults in the new code were compiled against the suite
  (`-O1`, native int128; the runner lives in the scratchpad, not the repo). 33 were caught on the first run, 6 survived. 4 of
  those were closed with new known answers and now fail (stage 5 not dropping above 127; a swing parity that skips negative
  steps, also covers C++ `%`; `vel_fixed` clamped after instead of before the accent; lastQ touched while quantize is off). 2
  remain and are equivalent: processing keys in list order instead of ascending (every style already emits ascending keys, so
  SPEC 4.7's "ascending pre-quantize order" is unobservable until a style emits otherwise), and clearing `lastQ` in `resetState()`
  (redundant: the first `process()` after `prepare()` / `reset()` is a clock edge, which runs `releaseAll()`). Kept as explicit
  defence in depth. Final run: 37 of 39 caught.
- **Alternatives rejected:** a closed-form nearest-key search (the 127-step walk is cheap and mirrors the spec's wording;
  nothing here is hot); caching the effective scale per block (it depends on the held set for `chord_root`, so it is recomputed
  per step); a lastQ-at-emission design (13b.3 explains why evaluation is the rule, and the pending-cap case proves it).
- **Verify (base git `a683e57`, tree uncommitted):** `bash tools/core_test.sh` exit 0 (269 passed per int128 path, was 189;
  smoke PASS; 3 guard lines), `python3 tools/core_params_check.py` exit 0, `--plant` exit 1 (red), `./verify fast` exit 0
  (`.harness/last-verify.json`: target fast, exit 0). UBSan builds of the suite
  (`-fsanitize=undefined -fno-sanitize-recover=undefined`, native and `ARP_FORCE_INT128_FALLBACK`): 269 passed, no runtime error.
  ASan / TSan not run (they cannot start on this machine, per the M1b trace).
- **Open questions (for the lead / the human), none blocks the code:**
  1. SPEC 5.1 swing is continuous and each step is frozen at its own evaluation, so a swing change between two steps leaves the
     earlier step's span and off computed with the old value while the later step's on uses the new one. Implemented literally
     and marked `SPEC-GAP` at `core/src/core.cpp` (the `swing` constant in `evalStep`). It is an observation, not a conflict.
  2. SPEC 6 lists "T4 ... switch-off" among the `lastQ` resets; `arp_enabled` off (the arp's half of T4, SPEC 14 note 14) also
     goes through `releaseAll()` and so resets `lastQ`. That is the literal reading; M2b's chord stage never reads `lastQ`.
  3. The global scale is a per-block input, not a setting: it is read at step evaluation, so an adapter that changes it
     mid-block is block-size dependent exactly as it would be for a settings change. The harness cuts blocks at a scale change.
  4. `CODEMAP.md` and README status lines are out of this dispatch's scope and not updated.

## Correction (M2b dispatch, 2026-10-23, after the M2a critic review)

- **The mutation note above was wrong about `resetState()`.** It called clearing `lastQ` in `resetState()` "redundant: the first `process()`
  after `prepare()` / `reset()` is a clock edge, which runs `releaseAll()`". With a STOPPED transport the first `process()` runs the free
  clock with no clock edge, so no `releaseAll()` runs and only the `resetState()` clear keeps SPEC 6's T10 reset (critic M1). M2b added the
  stopped-host `prepare()` variant to the Q5 known answers and to the `lastQNoReset` plant's must-fire set (`tests/core/test_m2a.cpp`,
  `afterPrepareStopped`), and the mutation (drop the `resetState()` clear) is now caught.
- The 30 seeded M1b sessions were restored exactly as M1b drew them (critic L2); the M2a draws moved into a NEW set of 30 (`test_m2b.cpp`,
  `featureFuzz`). Critic L1: a seeded brute-force sweep with a random `lastQ` chain and `degShift` over irregular masks were added.
