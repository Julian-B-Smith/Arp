# d039-order-at-one-sample — inside one sample an input comes first (erratum E13, option C); G4 narrowed

- **Queue item:** T-007 (M3). Decision **D-039**, decided twice on 2026-10-27,
  both times by the human by poll, relayed by the Arp lead session (which
  writes D-039 and the ROADMAP lines):
  - first E13 (A), "Earlier in the music first";
  - then, after the critic round below, E13 (C), "Back to input first
    (Recommended)".

  Follows `traces/2026-10-27-g4-handover.md`, where the breach was found
  (critic H1 there).
- **The finding:** an input less than one sample after an off, pending on or
  grid step shares that event's sample at some rates and not at others. Offsets
  take the ceiling (SPEC §2.3). The core takes the input first at its sample. So
  near such a tie the result can depend on the rate. Example: 209 bpm, 1/32, a
  release 0.70 tick after step 11; step 11 plays 60 at 48 kHz and 67 at 96 kHz.
- **Why (C), not (A):**
  - (A) put events strictly earlier in the music first. It restored G4 for that
    rare case, but it needs the host to deliver an on-grid event at or before
    its exact position.
  - At 44.1 kHz half the 1/16 lines of 120 bpm are half samples. A host that
    rounds to the nearest or up delivers a chord change a fraction of a sample
    after its step's tick, and (A) then plays the step with the old chord. The
    old order plays it at every rounding.
  - How real hosts round is unknown (hypothesis). The human chose the order that
    is safe under every rounding, and narrowed G4.
- **What landed:**
  - **SPEC §1.1** gains "Inside one sample, an input comes first", with the
    reason. Row **G4** now covers "events whose causes lie more than one sample
    apart at every rate; an input within one sample after an off, pending on or
    grid step counts as on it". The status line names E13 (C) / D-039, and
    `spec/MANIFEST` is re-pinned (SPEC.md c203338c…).
  - **Core and lab:** the shipped behaviour is the pre-D-039 order. Option (A)
    survives only as the test plant `musicalOrderOnSample`, in the core's
    timeline loop and in `takeDueOffs(before)`, and the lab mirrors it. In a
    shipped build `ARP_PLANT` is the constant `false`, so the extra compares
    fold away. The plants `inputFirstOnSample` / `inputYieldsStepOnly` from the
    (A) attempt are gone.
  - **Known answers with must-fail controls** (`test_core.cpp` `testG4Order` and
    `lab_smoke.mjs` §7b; all run through `runOnce`, kept out of the main
    digest):
    - **Must play:** key 67 pressed exactly on step 3's tick (120 bpm, 1/16;
      16537.5 samples at 44.1 kHz) plays on step 3 at 48/96/192 kHz and at
      44.1 kHz rounded down (16537), to the nearest or up (16538). The plant
      (A) plays 60 at 16538: red.
    - **Documented near-ties outside G4,** each pinned and each erased by the
      plant:
      - step: the 209 bpm release, 60 at 48 kHz, 67 at 96 kHz;
      - pending: a Panic 0.38 tick after a ratchet sub-note clears it at 48 kHz
        (7 ons) and ends it at 96 kHz (8 ons);
      - off: a Panic 0.38 tick after a gate end ends the note itself at 48 kHz
        (T7) and finds it ended at 96 kHz (T1).
  - **Test host:** the PR #30 fix stays: held and set positions exact, op
    samples to the nearest. The (A) attempt's switch to rounding inputs down and
    starting transport ops on the next whole sample is reverted: it existed only
    to suit (A).
  - **The edge fuzz's G4 comment** now states the narrowed guarantee.
- **Digests: unchanged.** Regenerated from the restored working-tree core
  (`regen`, `regen-random`), all four were identical, line for line apart from
  the header, to the pre-D-039 files:
  - `stream_digest.txt`: 1320 lines;
  - `stream_digest_bench.txt`: 837;
  - `stream_digest_random.txt`: 766;
  - `stream_digest_random_bench.txt`: 56.

  The committed files were kept as they were, with their origin headers
  (0ef0903, ed943b2). The suite's digest checks pass against them.
- **E14 (still pending the human):** a deferred off reports `kNoTick`, dropping
  its scheduled tick. It was found under (A)'s order. Under (C) its known path is
  closed: the Panic goes first and clears the sub-note. A release inside a note's
  own on-sample by a same-sample retrigger or eviction may still reach it
  (hypothesis, unverified). The row in `docs/spec-errata.md` says so.
- **History (the (A) attempt, superseded on this branch):**
  - Commits 73ba26b and 61428ee implemented (A) for every event kind, with
    regenerated digests (main 83 runs moved, bench 51/837, random bench 12/56,
    every moved run with an input yielding its sample).
  - The Opus critic returned REWORK:
    - HIGH 1, the host-rounding premise above, which went to the human;
    - HIGH 2, a block-size-dependent "held-back off" in (A)'s rule, now moot
      in shipped code and confined to the plant.
  - Reverted by the commit that carries this trace.
- **Evidence consulted:** SPEC §1.1, §2.3, §11 G4; core.cpp (the timeline loop,
  `takeDueOffs`, `releaseSlot`); the lab's timeline loop and `_offTime`; the
  T-007 critic reports (both rounds); the attribution experiment of the (A)
  attempt (scratch, outside the repo).
- **Alternatives rejected:**
  - (A): misses rounded-up chord changes.
  - (B), step first always: a chord pressed on the beat would miss it at every
    rate.
  - Keeping the round-down test host under (C): it was made for (A) and
    exercises no rule of (C).
- **Verify:** `./verify full` exit 0 at fc57045 (tree 56e2adb,
  2026-10-27T23:08:50Z): 537 passed, 0 failed on both suite binaries;
  `lab_smoke: OK`. The re-run critic's follow-ups (below) bring it to 539.
- **Re-run critic (Opus, read-only, independent build): REWORK, on the spec
  text only.** It verified that the shipped core and the bench outputs are
  byte-identical to 909ba44's, that the plant is (A) with the gate inverted,
  that every known answer is order-sensitive and erased by the plant, and the
  copy-in and MANIFEST hashes.
  - **H1 (the human's call):** G4's "more than one sample apart" is too
    narrow for a host that rounds. Up to 1.5 samples (nearest) or 2 (down) can
    still share a sample: 97 bpm, a release 1.14 samples after step 7 plays
    differently at 44.1 kHz. Pinned as a fourth near-tie (rounded). The proposed
    wording correction ("more than two samples", "cause" defined, plus two §1.1
    clarifications) is in `docs/spec-errata.md`.
  - **M1:** the spec re-pin needs D-039 in DECISIONS.md in the same PR (the
    lead's).
  - **M2:** a settings change rounded onto a step's sample is plausibly a
    fifth near-tie class. It is not pinned (hypothesis); the proposed G4 text
    names settings, transport and scale changes as causes.
  - **Done here:**
    - L1: the `before` path is gated on the plant, and the duplicate
      `allKinds` is gone.
    - L2: the C++ test checks the pending case's on and off counts, and the
      lab its counts and terminal rows.
    - L4: the copy-in "Last verified" line.
  - **Left:**
    - L3 (§1.1 clarifications): in the proposed text.
    - L5: no exact-positional-tie known answer (an implausible fault).
- **Open questions:**
  1. E14.
  2. Critic M1 of the first round (`tickStreamOf` is order-sensitive within one
     sample: two offs one tick apart sharing a sample only at 44.1 kHz above
     about 172 bpm). Not reached by any seed; left noted.
  3. The G4 / §1.1 wording correction (H1 of the re-run critic), pending the
     human.
