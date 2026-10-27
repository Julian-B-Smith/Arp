# d039-order-at-one-sample — an input yields its sample to what is earlier in the music (erratum E13, option A)

- **Queue item:** T-007 (M3). Decision **D-039**: the human chose E13 (A), "Earlier
  in the music first", by poll. The decision was relayed by the Arp lead session,
  which records D-039 and the ROADMAP lines. Follows
  `traces/2026-10-27-g4-handover.md`, where the critic found the breach (H1).
- **Why:** an input and an earlier grid step, off or pending on share one sample
  only because offsets take the ceiling (SPEC §2.3), and whether they share it
  depends on the sample rate. The core used to take the input first on such a
  tie (core.cpp timeline: "input, then offs, then pending ons, then the grid
  step"). So the same musical input played differently at 48 kHz than at
  96 kHz: a G4 breach. Musical order is the only order no sample rate can change.
- **What changed:**
  - **SPEC §1.1** gains "What happens first inside one sample":
    - an off, pending on or grid step strictly before the input's position
      goes first;
    - what is exactly on the sample follows the input (a chord pressed on the
      beat plays it);
    - an off held back by the 1-sample minimum counts as on the sample.

    The status line names E13 / D-039, and `spec/MANIFEST` is re-pinned
    (SPEC.md 21baff09…).
  - **Core:**
    - `Core::process`: each pass decides, per kind, whether the event is
      eligible ahead of the input; the input is taken only when nothing earlier
      in the music is left on its sample.
    - `takeDueOffs(t, out, before)`: at the input's sample, only the scheduled
      offs strictly before it are taken (quick offs and offs exactly on the
      sample follow the input).
    - Two plants: `inputFirstOnSample` (the old order) and `inputYieldsStepOnly`
      (the rule for the grid step alone).
  - **Lab core:** the same eligibility in its one-off-per-pass loop. A
    1-sample-minimum off is the one whose `_offTime` was raised to `min`, which
    is the core's `quick_`.
  - **The hypothesis (other event kinds) holds, with evidence.** Each class has a
    known answer and a plant, in `test_core.cpp` `testG4Order` and
    `lab_smoke.mjs` §7b:
    - **Pending:** a Panic 0.38 tick after a ratchet sub-note. Under input-first
      (and under step-only), the Panic cleared the sub-note at 44.1 and 48 kHz
      only (7 ons, against 8 at 96 and 192 kHz). Now it plays everywhere.
    - **Off:** a Panic 0.38 tick after a gate end. The events are the same
      either way, but at 44.1 and 48 kHz the note's terminal was the Panic (T7)
      instead of the gate end (T1). Now it is T1 everywhere. This class is
      visible only in the terminal rows, which are in the digests' counters.
    - **Step:** the critic's case: 209 bpm, 1/32, a release 0.70 tick after
      step 11. Step 11 plays 67 everywhere (before: 60 at 44.1 and 48 kHz).
  - **Test host (44.1 kHz):**
    - Inputs and settings round DOWN, so a script event written exactly on a
      tick stays at or before it. Rounding to the nearest pushed half of them
      past the tick, which the old input-first order happened to hide.
    - Transport ops take effect at the first whole sample at or after their
      time, with their exact positions (a host starts ON a sample, at its
      located position).
    - Evidence: rounding the inputs down alone left one session red,
      `chordstage`. Its host restarts at 8 kHz 11000, which is 60637.5 at
      44.1 kHz. Rounded down to 60637, every later position was half a sample
      late, and the enable at 88200 re-anchored to step 6 instead of step 5.
      Modelling a start between samples was worse (the hand-over check went
      red). Ceil plus an exact position fixes both.
- **Digests.** All four were regenerated from **73ba26b**, this change's own
  core, as D-039 directs (`regen 73ba26b`, `regen-random 73ba26b`). The new
  origin is in `tools/core_digest.sh`, the bench / random texts and the suite's
  origin check.

  Attribution was done with a scratch copy (outside the repo) of the core and
  tests, built twice:
  - **NEW:** the D-039 core with a counter of "an event handled before an input
    on that input's own sample".
  - **OLD:** the same code with the yield switched off.

  The tests were the same in both builds, and runs were aligned by descriptor.
  Results:
  - **Main digest:** 83 runs moved, every one with at least one yield. By kind
    (overlapping): 54 off, 28 pending, 16 step. 55 of them are counters only:
    the terminal row, events identical. No run moved without a yield; 16 runs
    yielded with unchanged output.
  - **Bench digest:** 51 of 837 configurations moved, all with yields (17
    counters only).
  - **Bench randomness digest:** 12 of 56 moved, all with yields (4 counters
    only).

    Every moved bench configuration is an "INPUT re-struck chord" stress
    scenario: 32 keys re-struck in the sample of a step's off and on.
  - **Randomness digest:** 0 moved by the rule.
  - **The test-input change** (inputs down, ops up), measured with the OLD
    build on HEAD's tests against the new tests:
    - randomness digest: 22 runs, all at 44.1 kHz;
    - main digest: 36 runs at 44.1 kHz, plus 13 wide-matrix replays (at
      48/96/192 kHz) of sessions whose base rate is 44.1 kHz, plus 3 blocks of
      44.1 kHz sessions whose total rounds differently (17 runs).

    Every input-only move belongs to a session written at 44.1 kHz.
  - **Control:** 73ba26b with the yield switched off, on HEAD's tests, passed
    the PREVIOUSLY committed digests (main 42226 runs, randomness 766) and
    reproduced both committed bench files. So the D-039 core differs from the
    reference only through the decided rule.
- **Critic M1 (`tickStreamOf` is order-sensitive within one sample):** not made
  moot. (A) orders an input against Arp's own events; two offs one tick apart
  that share a sample only at 44.1 kHz above about 172 bpm are still emitted by
  ascending id (§1.1). Left noted.
- **E14, proposed (pending the human):** an off deferred by the 1-sample minimum
  reports `kNoTick`, dropping the tick it was scheduled at (core.cpp
  `releaseSlot`, `x.hasOff = false`). Whether a release lands on its note's own
  on-sample is rate-dependent, so the same musical Panic reports 14016 at 96 kHz
  and `kNoTick` at 48 kHz. The pending known answer compares ons and the number
  of offs for that reason. Written up in `docs/spec-errata.md`; not implemented.
- **Evidence consulted:**
  - SPEC §1.1, §2.3–2.5, §4.5;
  - core.cpp: the timeline loop, `takeDueOffs`, `releaseSlot`, `emitOn` (the
    quick / sched filing);
  - the lab: the timeline loop and `_offTime`;
  - `test_core.cpp` `atRate` / `g4M1bDiverges` and `test_m2b.cpp` `chordstage`;
  - `digest_corpus.cpp` (its `F` sessions bypass `runOnce`, so its yields had
    to be counted there);
  - `bench.cpp` digest output.
- **Alternatives rejected:**
  - Yielding to the grid step only. It is the plant `inputYieldsStepOnly`, and
    its known answers fail.
  - Taking every off due at the input's sample when yielding. That puts offs
    exactly on the sample before the input, contrary to the rule.
  - Rounding host ops to the nearest, or to floor. Each moves the transport's
    phase by half a sample at 44.1 kHz, and one of the M1b sessions goes red
    either way.
  - Implementing E14. It is an undecided output change.
- **Verify:** `./verify full` exit 0 on 73ba26b + the regenerated digests
  (tree 1a3924c, 2026-10-27T22:38:25Z): 536 passed, 0 failed on both suite
  binaries; `lab_smoke: OK`. `./verify fast` exit 0 with this trace added.
- **Critic round (Opus, read-only, independent probes): REWORK. No PR opened.**
  - **HIGH 1, for the human: (A)'s "a chord pressed on the beat plays that beat"
    holds only if the host delivers an on-grid event at or before its exact
    position.**
    - At 44.1 kHz, a 1/16 at 120 bpm is 5512.5 samples. A host that rounds that
      half sample to the NEAREST or UP puts the input on the step's sample,
      strictly after its tick. The step then goes first and misses the chord
      change, every other 16th.
    - The old order caught it under every rounding. The critic's probe: key 67
      on step 3's tick misses at 44.1 kHz nearest and ceil under D-039, and
      plays under the old order.
    - Our test host was switched to round inputs down, which is exactly this
      convention. With those test changes the old order passes G4 too.
    - How real DAWs round clip events: unknown (hypothesis).
  - **HIGH 2 (verified, not yet fixed):** whether a held-back off "counts as on
    the sample" depends on block size.
    - A note born on a block's last sample has its quick off moved into
      `sched_` by `startBlockOffs` (core.cpp ~1884), where it counts as
      positioned. A Panic at the next sample then goes after it.
    - The terminal rows differ by block size (T1=2/T7=0 at blocks 1/2/32,
      T1=1/T7=1 at blocks 7/31/4096). The events are identical.
    - The lab has the same flaw.
    - Fix: keep held-back entries in `quick_` (core), and judge `positioned`
      on the unclamped offset (lab), plus a G1 check of the terminal rows.
      Held until HIGH 1 is decided, since option C would revert the order
      change.
  - **MED:**
    1. E14 is reachable on D-039's own common path, so "no other order keeps
       G4" overclaims until E14 is decided.
    2. The "73ba26b with the order off reproduces the old digests" claim holds
       with 909ba44's tests only (the critic re-checked it). The recipe should
       be committed.
  - **LOW:** the atRate comment's window can be about 2 samples next to a
    fractional transport op; "musical order" in §1.1 reads as a total order;
    one extra I128 multiply per pass.
  - **Checked, no finding:** the selection logic (late events, several inputs
    on one sample, the free-clock re-base, E10 dropsTie); N1; the exact-tie
    clause (a `<=` mutant fails about 20 checks); the main-digest attribution
    (83 moved, 55 counters only, 0 in the randomness digest by the rule).
- **Open questions:**
  0. HIGH 1: keep (A) with the host convention written into SPEC §1.1 / G4
     ("G4 assumes the host places an event at or before its exact position"),
     or move to (C) (narrow G4 to causes more than one sample apart) and revert
     the order change. For the human.
  1. E14.
  2. Critic M1 (comparator order within one sample).
  3. Intermediate commit 73ba26b carries stale digests by construction (they are
     regenerated from it in the next commit). `./verify` is green only at the
     branch tip.
