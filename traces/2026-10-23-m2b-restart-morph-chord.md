# m2b-restart-morph-chord — restart, repeats, morph timing and the chord stage in the C++ core (M2b), plus swing latched per step pair (E6)

- **Queue item:** T-005 (ROADMAP M2), dispatch M2b of two, 2026-10-23, on `m2/core-scale-feel`. Base `6819232` (M2a committed); working tree
  uncommitted, the lead commits. Authored by an implementer dispatch from the lead session. Mid-dispatch the lead added (a) the M2a critic's
  fixes (chord_root anchor trap, the stopped-`prepare()` Q5 variant, the random-lastQ sweep, restoring the 30 M1b sessions) and (b) the swing
  decision (SPEC 5.1 amended, erratum E6/E7, D-026), which also touched `lab/arp-lab.html` (CORE only) and `tools/lab_smoke.mjs`.
- **Why:** M2b makes the rest of the ratified spec's musical surface real in `core/`: restart `chord` / `bar` / `free` (4.5), repeats (4.8),
  the timing of a changed setting under horde's morph (9), the chord stage with its own stream and table (14), and swing latched per step pair
  (5.1 E6). It closes the M2 gate rows RP1, CH1, TM3's restart half, S1 over two streams, and the T8 / Q-A2 landing rules.
- **Changed (`core/`, no public interface change):**
  - **Restart.** `restartMode()` (inert in time mode; plants aside). `bar`: the first grid step at or after each bar start (`barTicks_` from the
    transport's time signature, a non-positive part reads 4/4; `barPhase_` from `barStartBeats` as a PHASE, 13.12; free clock: phase 0), `m := 0`
    there; `free`: anchor 0; `chord`: as before. Rate change re-expresses the bar anchor (13.8). A morph of `restart` lands at the next evaluated
    step (Q-A2): into bar / free zero `m` and re-anchor, into chord keep the anchor.
  - **Repeats.** `repeatSteps_`, latched `exhausted_`, `C = Ls * P*(steps+1) / gcd(.., plays)` from the live H', spent steps consumed by the
    once-guard (no step record, no trigger, no stepSeq), cleared by a gesture start / a key-adding NoteOn / T6 / T7 / T10 (`noteOn`, `edge`,
    Panic, `resetState`). T20 counted.
  - **Chord stage.** `chordSync()` (offset 0 of every block, after the last input event of a sample, and after `adopt()`): stored mapping per held
    key (`ChordRec`, up to 7 slots), re-map only on a changed signature, dedupe by KEY with a source count (`ChordNote[128]`), notes start with the
    earliest source's channel / velocity and end exactly once through `release()`; its own 224-entry share of the sounding table (T11 counts only
    arp entries); `H'` (`chordSet_`, ordered by first source press, slot, key) feeds the arp; `stream = Chord`, `stepSeq = -1` (sorts before
    the arp's); `releaseAll(s, includeChord)` / `releaseArp` split the arp's release-all (T6, arp off alone) from the module's (T4, T7, T10).
    `chord_root` for the ARP anchors on `lowestHeldKey()` (the lowest PLAYED key), not on `E[0]` of H' (critic L3).
  - **Swing (E6).** `visit()` latches `swing` per pair: the even step reads it, the odd step reuses it when the even step was the last one
    visited, else reads the current value (`swingUsed_`, `swingPair_`, `swingPairK_`, invalidated at an edge, a free-clock start and a rate switch).
  - **Public interface:** none. `types.h` and the public part of `Core` are unchanged (`Stream::Chord`, `arpEnabled`, the chord_* settings
    already existed). `Plants` (test-only, compiled out of the shipped library) grew 29 members; `HeldKey` (private) gained `slot`.
- **Changed (`tests/core/`):** `test_m2b.cpp` / `.h` (new), `harness.h` (plant table, `Rec::ord`, `SimHost` replay at another rate,
  `tickStreamOf` leaves a chord ON's tick out: it is the floor of the position at the input's sample, which another sample rate rounds),
  `test_core.cpp` (three sessions join N1 / G1 / G4 / A1; rows T20 / T21 required; `atRate` also rescales the host and `prepares`),
  `test_m1b.cpp` (the 30 M1b sessions restored exactly; `m1bFuzzSession` exported), `test_m2a.cpp` (critic items 2 and 3, the swing checks,
  `applyM2aDraws`), `CMakeLists.txt` (one source).
- **Planted controls added (each fires in the suite's own output with its reason):** RP1 `repeatsCountPatternPasses`, `repeatsNeverStop`,
  `exhaustedStopsGuard`, `onlyGestureResets`, `morphRevives`; restart `restartBarAsChord`, `restartFreeAsChord`, `restartMorphKeepsM`,
  `restartMorphReanchors`, TM3 `timeBarsCount` (now the real bar logic applied in time mode); T8 `morphSwitchCuts`, `morphSwitchDropsPending`
  (all 42 non-offset-0 discrete settings read red under each); CH1 `chordNoDedupe`, `chordEndsOnFirstSource`, `arpIgnoresChord` (the spec's three)
  plus `chordRestartAll`, `chordRecomputeAtRelease`, `t2AcrossStreams`, `arpToggleEndsChord`, `chordEndsAtEdge`, `sharedSoundingTable`,
  `chordSyncPerEvent`, `chordDedupeByChannelKey`, `chordPlusZeroUnquantized`, `chordSkipQuantize`, `chordReadsLastQ`, `chordWritesLastQ`,
  `chordRootFromChordSet`, `latchAtNextStep` (existing, now with a CH1 scene); S1 `untaggedOn` / `chordTaggedArp` (some ons only); E6 `swingPerStep`.
- **Morph.** horde RESOLVES a morph (SPEC 9) and gives Arp integer snapshots; the spec defines no morph interface, so the core has no morph code
  (and none was invented). The harness has a `MorphModel` of horde's rule (leading corner by MORE than 0.05, ties to the lower corner,
  continuous settings blended and rounded half up) pinned by known answers, used to generate 47 morph-driven N1 / G1 sessions (each of the 45
  discrete settings alone, plus every setting at once, with and without the three offset-0 ones) and the continuous-blend known answer.
- **Known answers worth naming:** 2 repeats of `up` over C E G = `C E G C E G` then silence; gallop + 1 step of +12 is 8 steps per cycle;
  tied_legato under bounce2 over 4 keys is one 8-note cycle (the gcd bites); a 3/16 rate under bar re-anchors at steps 0, 6, 11, 16; 3/4 bars;
  `barStartBeats` as a phase; a morph chord -> free at 6500 lands at step 7000 with `m := 0`; free -> chord keeps the anchor; a 2/1 -> 1/16 rate
  morph waits for the old step; the stage: C + {+4, +7}, shared keys end with the LAST source, a stored mapping survives an anchor change,
  a C# in C major is C F G, degrees from the SNAPPED key, 128 chord + 128 arp notes sound together, the arp's chord_root anchors on the
  played key (58 61 68), the stage neither reads nor writes `lastQ` (scene found by search, pinned: press 61 at 2000 / 4000).
- **Evidence consulted:** SPEC 1, 3, 4.5, 4.8, 5.1 (as amended), 7, 9, 11, 12, 13, 13b, 14; ROADMAP M2 / T-005; the two prior traces; the lab
  CORE (`_chordSync`, `_chordMap`, `_evalStep`'s repeats, `_adopt`) for intent only (expected values are written from the spec text).
- **Mutation check (mine, not committed):** 46 hand-made faults in the new core code compiled against the suite (`-O1`, native int128; the
  runner lives in the scratchpad). 41 caught; 5 survive and are equivalent in scope: Panic not zeroing the repeat count (the next press is a
  gesture start that zeroes it), the `free` branch in `evalStep` forcing `anchorK_ = 0` (every other path already does), the arp reading the chord
  set without the module check (the arp is not running with the module off), `++repeatSteps_` before the exhausted return (the count is irrelevant
  while latched), and not invalidating the swing pair at a free-clock start (step 0 is even and always re-latches). 9 more were first
  survivors and closed with new known answers (static play count vs ties and picks, T6 clearing `exhausted`, a bar-mode gesture not
  re-anchoring, a rate change under bar, the global scale in the re-map signature, degrees from the snapped base, the slot order of `Pp`,
  the table split at 128 + 128, the arp-off pending clear, trigger-only steps with no keys under repeats, and two stale-swing-pair cases).
- **Alternatives rejected:** a morph resolver in the core (the spec defines no interface; horde resolves it); a separate sounding array for chord
  notes (one table with a flag keeps `release()` the ONE path to a NoteOff and the 1-sample deferral); recomputing counts incrementally (a
  from-scratch recount per sync is 224 steps and obviously equal to the spec's stored-mapping rule).
- **Verify (base git `6819232`, tree uncommitted):** `bash tools/core_test.sh` exit 0 (376 passed per int128 path, was 269; smoke PASS; 3 guard
  lines), `python3 tools/core_params_check.py` exit 0, `--plant` exit 1 (red), `node tools/lab_smoke.mjs` OK, `./verify fast` exit 0
  (`.harness/last-verify.json`: target fast, exit 0). UBSan builds of the suite (`-fsanitize=undefined -fno-sanitize-recover=undefined`,
  native and `ARP_FORCE_INT128_FALLBACK`): 376 passed, no runtime error. ASan / TSan not run (they cannot start on this machine).
- **Open questions (SPEC-GAPs, none blocks):** see the report; chiefly (1) the bar phase on the free clock is 0, (2) a non-positive time
  signature reads 4/4, (3) an input key outside 0-127 is dropped before the chord stage snaps it, and in degrees mode all its slots drop,
  (4) a step consumed without an anchor or as spent counts as "evaluated" for the swing pair, (5) the stage's earliest-source tie-break is
  (press sample, slot, source key), (6) a chord NoteOn's diagnostic `tick` is the floor of the position at its sample.
