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

## Follow-up — the independent critic review of M2b (D-027), 2026-10-23

- **Queue item:** T-005 (ROADMAP M2), the follow-up dispatch after the critic review of `b4fc852`; authored by an implementer dispatch from the lead
  session. Working tree uncommitted (the lead commits); the lead's own edits (DECISIONS, README, CODEMAP, docs/spec-errata, spec/) were not touched.
  No public interface change: `Plants` (test-only, compiled out of the shipped library) grew 9 members; the private `Core` gained `lastC_`, `hasLastC_`
  and `dropSpilledSubnotes()`.
- **Why:** the critic found no HIGH; the human decided E8, E9, E10 by poll (D-027) and the lead settled L1 and L3 as the literal reading. Per item:
  - **M1 (chord stage 1->0->1 at one sample).** `process()` skips the block-start `chordSync(0)` when the first valid input is at offset 0, so the
    re-map and the held-set change are ONE pass (the post-input sync). Known answers: hold C with one slot (+4), at 2000 the shift becomes +7 and E is
    pressed in the same sample -> 64 keeps sounding, no off64/on64 pair (60/67 end with C at 5000, 64/71 with E at 7000); the mirror (shift change plus
    C released at 2000) -> 60, 64 end at 2000 and 67 never starts. Plant `chordSyncBlockStartFirst` reads off64@2000+on64@2000, and on67@2000+off67@2001.
    Block sizes 4096 / 128 / 1.
  - **M5 (bar bookkeeping).** Two known answers on a 4-step pattern, each with its mutant as a plant: bar -> chord -> a NEW gesture -> bar again inside bar 0
    re-anchors at the bar start (notes 4000 | 6000 | 8000 12000; `adoptKeepsLastBar` reads ... 10000 14000); a jump back to beat 1.25 inside bar 0 re-anchors at
    the bar start (4000 | 9500 13500; `reanchorKeepsLastBar` reads 4000 6500 10500). The Q-A2 `intoBar` check now uses a 4-step one-note pattern so the ANCHOR is
    visible (3000:60 8000:60 12000:64; `restartBarAsChord` keeps the chord anchor and reads red).
  - **E9 (repeats run out after release).** `lastC_` / `hasLastC_`: C is worked out whenever H' is non-empty (whatever `repeats` is, so a later repeats morph
    never reads an older gesture's C); while H' is empty the last C is kept and the count keeps running. RP1 check (j) is replaced: restart free, every
    step fires a trigger, repeats 1, C E G pressed at 100 and released at 1500 -> triggers 0 1000 2000 3000 then none, equal to the held run; plant
    `repeatsPauseWhileEmpty` (the old behaviour) reads triggers without end. Kept as (j0) the pre-gesture known answer (13 triggers, no count); added
    "repeats 0 -> 1 after the release runs on the C worked out while held". Lab CORE: same rule, `lab_smoke` checks (j) and (j0) with the plant.
  - **E10 (spilled ratchets, time mode).** `dropSpilledSubnotes(on0)` at every evaluated step (a spent one too), time mode only: pending sub-notes (not triggers)
    with `pos >= on0` are removed, queue order kept. Known answer: time mode, 500 ms, ratchet 4, 200 ms at sample 800 -> ons 0 1000 | 1600 2000 2400 2800 3200
    3600, no two ons of a key at one sample, nothing of step 0 after step 1's on, N1, identical at 4096 / 64 / 1; plant `keepSpilledRatchets`.
    **Found while testing "at or after": a sub-note in the SAME SAMPLE as the next step.** SPEC 1.1's tie order (input, offs, pending ons, grid step) emits it
    BEFORE the step is evaluated, so the drop never saw it and the step's own on retriggered the key. With 250 ms the next step lands exactly on step 0's
    sub-note at 2000: fixed by a timeline tie-break (time mode only: a pending sub-note whose tick is at or after the step's tick yields to the step), known
    answer 0 1000 | 2000 2500 3000 3500, plant read red. Lab CORE: same drop, same tie-break, `lab_smoke` TM5 with both scenes and the plant.
  - **L1 (swing pair).** The latch moved after the anchor test in `visit()`: an unanchored step does not latch, a spent step does. SPEC-GAP 4 comment rewritten.
    Known answers: restart chord, swing 500 -> 750 at 700, key at 500 (anchor = odd step 1) -> ons 1500 2000 3500 4000 5500 (plant `swingLatchUnanchored`
    reads 1000 ...); a spent even step latches (repeats 1, a key-adding press revives: ons 1000 3000 4000; no separate plant - hand-mutated: clearing the pair on a
    spent step turns it red).
  - **L2 (press order).** A stored chord record's held key (press time, velocity, channel) is refreshed from H on every sync, stored keys untouched. Two refs on
    key 60, the earliest released: `played` order 64 67 60 with the stage on (all slots off) and off; plant `chordStalePressTime` reads 64 60 67.
  - **L3 (G4 not narrowed).** `chordCauses()` (harness.h): at each sample rate (44.1 / 48 / 96 / 192 kHz, inputs scaled per rate) every chord-stream NoteOn and NoteOff
    of the `chordstage` and `morph` sessions lands at exactly the sample of the input, settings / scale change, reset or prepare that caused it, with the same cause
    (rank) at every rate; an off may follow its own on by SPEC 1.1's one sample. While the transport plays each chord on's tick is also the floor of the host's exact
    position there. `tickStreamOf` still leaves chord-on ticks out of the cross-rate tick comparison (it cannot compare them) but they are now held to this exact check
    (592 chord events, 296 on ticks per run). Plants: `chordOnLate` (on lands one sample late at >= 96 kHz) and `chordOnTickSkew`, each read red by the chord check ALONE.
    Residual: a chord on that lands while the transport is STOPPED (free clock) has its sample checked, not its tick (none occurs in the two sessions).
  - **L4.** `MorphModel` (harness only) reads "exceeds by 0.05" as STRICTLY greater (`leadW - curW > 50`, thousandths); horde owns the rule. The pinned known answers
    (switch at wB = 0.526 and not 0.525; back at 0.474) are the harness's reading, to be re-pinned if horde reads it as >=.
- **Evidence consulted:** SPEC 1.1, 2.5, 2.6, 3, 4.5, 4.8, 5.1, 9, 14 as amended (E8-E10); DECISIONS D-027; docs/spec-errata.md; the M2a and M2b traces; core.cpp
  (process, visit, adopt, reanchor, evalStep, chordSync); the lab CORE (_evalStep, timeline) and lab_smoke RP1 / TM sections.
- **Alternatives rejected:** dropping spilled sub-notes in sync mode too (the swing latch makes it unreachable there; the spec names time mode); dropping triggers with the
  sub-notes (the spec says sub-notes; a trigger sits at its own step's on, before any later step); clearing `lastC_` at Panic (T7 is "restart the count", nothing says
  forget the chord; it would let triggers run without end after a Panic); a per-rate list of expected sample numbers for L3 (the cause set derives from the scaled session, so
  it cannot drift from the harness's own scaling).
- **Mutation check (mine, not committed):** clearing the swing pair on a spent step -> red (the L1 spent-step known answer); a first attempt at that mutant (`!exhausted_` in the
  latch test) survived because `exhausted_` is only set inside `evalStep`, i.e. the mutant was not a mutant. The E10 drop with `>` instead of `>=` is red through the
  250 ms scene (it was the scene that found the same-sample ordering).
- **Verify (base git `b4fc852`, tree uncommitted):** `bash tools/core_test.sh` exit 0 (398 passed per int128 path, was 376; smoke PASS; 3 guard lines),
  `python3 tools/core_params_check.py` exit 0, `--plant` exit 1, `node tools/lab_smoke.mjs` OK, `./verify fast` exit 0. UBSan (`-fsanitize=undefined
  -fno-sanitize-recover=undefined -O1`, native and `ARP_FORCE_INT128_FALLBACK`): 398 passed, 0 runtime errors. ASan / TSan not run (cannot start on this machine).
- **Open questions (SPEC-GAPs, none blocks):**
  1. SPEC 5.1 / 1.1: the same-sample tie-break above is not in the spec text (E10 says "at that step's evaluation ... at or after its on"; the 1.1 order evaluates the
     step after pending ons of the same sample). Implemented as the literal reading of "at or after"; core.cpp ~654-662. The lead may want a sentence in 5.1.
  2. SPEC 4.8 / E9: T6 and T7 keep the last non-empty C (only T10 / prepare forget it), so after a Panic with no keys the count runs again from 0 on the old C and exhausts;
     core.cpp ~1226-1228.
  3. The lab CORE does not carry M1 (block-start sync), M5 (bar bookkeeping), L1 (swing pair), L2 (press order) or L3 (a core-only harness check): the brief scoped the lab to
     items 3 and 4. The lab keeps the M2b order for those.
  4. L2's refresh also refreshes the record's velocity and channel; they only matter when a chord note starts from that record (a later joiner), where they are the earliest
     live ref's, as SPEC 3 says for H.
