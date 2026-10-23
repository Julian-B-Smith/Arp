# m1b-styles-patterns — every order style, step-pattern field, the user pattern and time mode in the C++ core (M1b)

- **Queue item:** T-004 (ROADMAP M1b), "start M1b" from the human, 2026-10-23. Authored by an implementer dispatch from the
  lead session of this project; M1a merged as PR #18 (CI green on Linux g++). The degree unit moved to M2 in the same
  dispatch.
- **Why:** M1b makes the ratified spec's musical surface real in `core/` so the M1b gate (R1, U1, X1, ST1, TM1-TM3, K1,
  T1, P1, N2, S1, the order tables, each with its plant) can be judged.
- **Changed:**
  - **LOW-A / LOW-B first.** After a `prepare()` that leaves owed offs the id counter starts above the largest owed id
    (plant `owedIdCollision`). The guard now also refuses `-ffinite-math-only` (`__FINITE_MATH_ONLY__` is predefined as 0, so
    its VALUE is tested), finiteness is decided on the exponent bits, and `tools/core_test.sh` has a must-fail build check
    (control without the flag compiles; `-ffast-math` and `-ffinite-math-only` are refused with the IEEE-semantics message).
  - **Order styles (SPEC 4.2).** All 16, in `Core::notesFor`. `shuffle` is allocation-free and O(e) with no memo: for e >= 3
    the previous cycle's last note is the raw Fisher-Yates last (the swap never touches it); for e = 2 every cycle ends on the
    same key (derived in the code comment, and checked by the suite against a LITERAL replay of the spec's chain).
  - **Step fields (4.3, 5.1-5.2, 8).** `core/include/arp/pattern.h` (PatternStep / PatternDef / UserPattern); on, tie
    (look-ahead, cap length - 1), vel, gate (span based), prob (draw lane 2), ratchets (floor first, then gate, capped at the
    sub-step), triggers (in the pending queue, first on only, trigger-only steps, tie steps), picks, `step` x the pattern's
    step_size. The bank is embedded: `tools/gen_bank.py` writes `core/include/arp/bank_data.h` from `spec/bank/starter.json`.
  - **Transposition (4.7) in semitones**; a degree unit plays as semitones until M2 (SPEC-GAP marked in the code).
  - **User pattern (4.6).** Three slots sized at construction, a triple buffer with ONE `std::atomic<int>` exchange
    (acq_rel), taken once per `process()`; `setUserPattern` validates and clamps; `getUserPattern` returns the writer's copy;
    survives `reset()` / `prepare()`.
  - **Time mode (2.6, 13b.8-10).** D = sr, inc = 10^6 per sample, the time clock = the free clock in microseconds, the
    piecewise grid for the smooth sweep (`gtick`, `timeRegrid`), mode switch = a clock edge (T19), host ignored.
  - Rows T12, T17, T18, T19 are counted; `Counters::rows` is `kRowCount` (22) long.
- **Evidence consulted:** SPEC 2.6, 4.2, 4.3, 4.6, 4.7, 5.1, 5.2, 7, 8, 10, 11, 13/13b; `spec/bank/starter.json`; the lab CORE
  and `tools/lab_smoke.mjs` for how each check was built (read, not copied: the order tables in `tests/core/test_m1b.cpp` are
  written from the spec text as explicit lists, not from the core's index formulas).
- **Planted controls added** (each fires in the suite's own output, with its reason): `owedIdCollision`, `orderTables`
  (12 style/e cells red), `shuffleBoundaryRepeat`, `advanceOnRest`, `pickAdvancesM`, `ignorePick`, `prepareResetsUser`,
  `userFirstWins`, `swapMidStep` (driven deterministically by a `midBlockHook` standing in for a writer thread landing
  mid-block), `transposeMerged`, `stepsZeroShifts`, `stepSizeIgnored`, `stepOffsetReadsGlobal`, `tieAtTieStep`,
  `trigEveryRatchet`, `probLE`, `untaggedOn`, `timeUsesTempo`, `timeAnchorsTransport`, `regridAbsolute`, `regridKAbs`,
  `timeBarsCount`, `timeEntryNoStart`, `inactiveReanchors`; the bank drift check has its own planted control (one changed
  probability must turn both the compiled-bank check and the generated-header check red).
- **Failed first:** the new suite was built against the M1a core from git (plus a no-op `setUserPattern` shim so it
  compiles): 37 checks FAIL there (every style table, R1, U1, T1, K1, P1, X1, ST1, TM1-TM3, the fuzz-free N2/S1/LOW-A, the
  new known answers). The M1a checks themselves stayed green on the new core (the first 94 of the 169).
- **Mutation check (mine, not committed):** 46 hand-made faults in the new code were compiled against the suite; 33 were
  caught on the first run, 13 were not. 9 of those were closed with new known answers (T16 under transposition, pick 12 /
  length 0 validation, the prob stream against `arp::draw`, ratchet floor-first at 1/8T, the tie's own gate and trigger,
  velocity rounding, `changeTick` rounded up at 44.1 kHz) and now fail. 4 remain and are equivalent in scope: the tie
  look-ahead cap (a playing step never sees itself as a tie), `stepSeq` as a same-offset tiebreak, an edge not zeroing the
  time origin (a gesture always does), and `kAtOrAfter`'s `gK1` term (overwritten by the free-clock start that always follows).
- **Alternatives rejected:** a memo for shuffle e = 2 (the chain collapses, see the code); an `std::vector` in the core for
  the merged-octave plant (it uses one, in plants only, capped at 32); reading `swing` (still M2: `kStraightSwing`).
- **Verify:** see the lead report: `bash tools/core_test.sh` exit 0 (169 + 169 passed, smoke PASS, 3 guard lines),
  `python3 tools/core_params_check.py` exit 0 (settings, enum lists, rate ticks, embedded bank, plant fires),
  `--plant` exit 1 (red), `python3 tools/gen_bank.py --check` PASS, `./verify fast` exit 0 (git 1f8da59, working tree
  uncommitted), UBSan builds of the suite (native and fallback int128) 169 passed with no runtime error. ASan and TSan
  cannot start on this machine (a trivial program aborts / segfaults in the sanitizer's own init), so no result.
- **Open questions:** `trigger` pending entries are emitted at the step's first on, which equals the evaluation time while
  swing is straight, so "cancelled by release-all while pending" has no observable case until M2's swing; the suite pins
  that it is emitted once and in order, not the cancellation.

## Follow-up: the critic's LOWs (L1-L6), same day

No HIGH was open. Changed (tests unless noted):
- **L1, G4 for the M1b features.** The existing G4 now also runs `m1bSession("styles")` and `("patterns")` (sync mode) at 44.1 /
  48 / 96 / 192 kHz, positions scaled to the rate. The existing `srTickMath` plant fires on both ("ticks differ at 96000 Hz").
  `tickStreamOf` now includes the trigger lane.
- **L2, bank order vs the `Pattern` enum.** `bankOrderMatchesPatternEnum` (a constexpr function in `pattern.h`) is
  `static_assert`ed in `core.cpp` on the real arrays; the suite feeds it a bank with two patterns swapped and a bank one pattern
  off, and both read red.
- **L3, triple buffer.** `static_assert(std::atomic<int>::is_always_lock_free)` in `core.h`. The real-thread U1 test now rewrites
  two patterns that differ in every field (on, tie, vel, gate, step, prob, ratchet, trig, pick, step size, length; both
  semitone, since the degree unit is M2) and checks that every block is wholly one pattern and every note length (576 / 6278 /
  768 ticks) is one pattern's. A deterministic twin drives a write into the middle of a block through `midBlockHook`
  (`swapMidStep` plant: 1 of 3 blocks torn). My first version of the criterion was wrong, not the core: a ratchet sub-note frozen
  at an earlier step's evaluation is legitimately emitted in the NEXT block (SPEC 5.1), so a block is judged by the steps it
  evaluated (NoteOn `stepSeq` >= the steps evaluated before it). **Interface note: `Core` is now non-copyable and
  non-movable** (it owns a `std::atomic`); hold it by pointer or construct it in place.
- **L5.** A known answer for `played` with two keys pressed on one sample (60 before 67, the later 64 last; plant
  `playedTieByKeyDesc` fires), and a check that random, shuffle and prob draws change with the epoch under the stopped free
  clock (gesture 1 uses epoch 1, gesture 2 epoch 2, each equal to a reference built from `arp::draw`; plant
  `drawIgnoresEpoch` fires for all three).
- **L6.** The comment in `core.cpp` now points at `tests/core/test_m1b.cpp` (the e = 2 replay is `shuffleReference`).
- **L4, for the record.** TM3's "restart is inert in time mode" half only has teeth once M2 builds restart bar / free: today
  `bar` and `free` already play as `chord` in sync mode too. The `timeBarsCount` plant does fire (it implements the old bar
  counting), so the detector is proven, but the clean-side assertion is trivially true until then.
- Not touched, waiting on the human: M1 (a trigger-only step in time mode with H empty but a note ringing, SPEC 2.5 vs 2.4 idle),
  M2 (the LOW-A id counter vs SPEC 1.2: an erratum), M3 (the degree unit moving to M2).

## Follow-up: time mode stops evaluating steps at key release (critic M1, decided by the human's poll)

- **Rule (SPEC 2.5; 2.6 amended by the lead):** in time mode, while H is empty the time clock evaluates NO grid step: no notes, no
  triggers, trigger-only steps included, even while a note rings (long gate, tie) or a sub-note is pending. What an earlier step
  scheduled still sounds and ends on time (offs are never dropped, N1 holds). The next gesture start re-bases as before. Sync
  mode is unchanged: the stopped free clock keeps 2.4's idle (H empty AND nothing sounding AND nothing pending), the host clock
  runs regardless of H.
- **C++:** one condition in the process loop (`timeHeldOnly` in `core.cpp`). The clock itself still runs until idle; only
  step evaluation is gated. Plant `timeStepsWhileRinging` restores the old rule.
- **Checks:** time mode with `[note gate 200 %, rest + trigger 3]`, key released at 1500: the trigger fires once (1000) and not
  at 3000; the note's off stays at 4000. The same with a tied note and trigger-carrying tie steps (released at 500): no trigger
  after, the tied note ends at 2800. Both go red under the plant. A sync-mode known answer on a stopped transport still fires the
  trigger at 3000 (only time mode changed). G1 / A1 / N1 over the `timemode` session stay green.
- **Lab:** the same one-line rule in the lab CORE and a matching `lab_smoke` check (TM4) with the same-named plant; the lab's
  embedded params and bank are untouched and still byte-equal to the spec.
