# 2026-10-29 — Bank-pattern goldens cut from the shipped core (D-046, T-007)

**What changed.**
- `tests/core/goldens.cpp`: a new generator, `arp_goldens`. It links the
  SHIPPED library (no `ARP_PLANTS`) and is built once per int128 path
  (`arp_goldens`, `arp_goldens_soft`; `tests/core/CMakeLists.txt`).
- `tests/core/goldens/<id>.txt`: one golden for each of the 11 patterns of
  `spec/bank/starter.json`. Cut from main `c5741de`; the full hash is in every
  file's header.
- `tools/core_golden.sh cut [REV]`: the only thing that writes the goldens.
- `tools/core_test.sh`: the check and its plants. `./verify fast` already runs
  this script, so `./verify` is unchanged.
- `CODEMAP.md`: two rows name the new files.

**Why.** The human's poll ruled D-046 (PR #38): cut goldens for the bank
patterns now, and protect them from then on. Preset goldens come later, once
factory presets exist. The charter says goldens are pinned only once the C++
exists, and it does.

## Design

**What a golden pins.** Every pattern gets the same scenario, so the files
differ only in what the pattern does:
- Keys 60/64/67/71 are pressed at sample 0, with velocities 100/90/80/70 and
  host ids 1-4.
- Tempo 120 bpm, rate 1/16 in sync mode, gate 80, seed 20261029. The seed is
  nonzero, so `random` and `sparse_chance`'s probability steps draw from a
  seed we chose.
- Three keys (60/64/67) are released at 6.94 s, inside step 55: index 7 of
  an 8- or 16-step pattern, 3 of a 4-step one, 1 of offbeat. So the release
  lands on:
  - `roll_end`'s ratchet-4 step: sub-notes already scheduled play past it;
  - a tie of `tied_legato`;
  - a probability step of `sparse_chance`;
  - `octave_leap`'s -1 step.
- Key 71 stays held, so the arp goes on over one key (a held-chord change).
- The run ends at 7.95 s, inside the gate of step 63's note. `reset()` then
  ends that note: 8 of 11 files show its off at sample 381600. The other 3
  have nothing sounding then (`sparse_chance` step 63 did not fire,
  `three_three_two` rests, `trigger_pulse` plays no notes).
- So one golden covers steady play (at least 3 cycles of a 16-step pattern),
  a release on each pattern's busiest step, the play after it, and the
  reset's terminal.
- The matrix: 3 orders (up, bounce, random) × 3 sample rates (44.1, 48 and
  96 kHz) × 2 block sizes (61 and 4096) at swing 500, plus `up 48000` at
  swing 600 at both block sizes: 20 runs per pattern. Nothing else pins swing
  with a bank pattern.

Each run gives one `run` line:
- event counts (all, on, off, trigger);
- three 64-bit FNV-1a hashes from the existing hasher,
  `tests/core/stream_digest.h`: every field of every event, every step
  record, and the counters.

Each file also has an `ev` excerpt so a reviewer can see what moved. It comes
from the run `up 48000 61` and holds the first bar (16 steps, a whole cycle of
the longest pattern) and every event from the release through `reset()`. The excerpt is
capped at 64 lines per part. If the cap is hit, a pinned `ev-cut` line says
so; none is hit today.

**The run's length is fixed in samples, not blocks.** So the two block sizes
of one order and rate must give identical lines, and every file shows that
they do. That is block-size independence, pinned per pattern.

**How the check runs.** In `tools/core_test.sh`, after the bench digests:
- Both binaries write the 11 files to a scratch directory.
- `golden_compare` then requires three things:
  - the set of files is exactly the ids in `spec/bank/starter.json`, read
    from the JSON itself;
  - every committed file records a 40-hex cut commit;
  - every file matches its generated copy outside `#` lines.
- Each problem is one FAIL line that names the pattern.

**Plants (must-fail, before the real check):**
1. `arp_goldens --perturb ID` adds 1 to the velocity of event #3 of ID's first
   run. It changes what is hashed and printed, never the core. This runs for
   each of the 11 patterns in turn. Each time, the check must name that
   pattern and only that one. The unperturbed run must also match itself.
2. One `run` line edited in a copy of the committed set must be named.
3. One file missing from a copy must be named.
4. One `ev` (excerpt) line edited in a copy must be named.
5. One provenance line missing from a copy must be named.

**How a deliberate re-cut is done.** Run `bash tools/core_golden.sh cut`,
which uses HEAD's core and refuses if `core/` has uncommitted changes. Or run
`bash tools/core_golden.sh cut REV`, which reads REV's core out with
`git show`, as `core_digest.sh` does. The tool records the full commit, plus
the sha256 of `core.cpp`, `bank_data.h` and the generator (`goldens.cpp`,
`stream_digest.h`). It is used only for a change meant
to move the output, with its decision recorded. The goldens are protected
from that moment on. The CLAUDE.md protected-path line is proposed in the PR
for the lead; this change does not edit CLAUDE.md.

## Evidence

**A finding during the cut.** The first version hashed each block's events
and its step records into ONE stream. `roll_end` then hashed differently at
block 61 and at block 4096, with the same event count.

A side-by-side dump (scratch program, both block sizes, 48 kHz) showed the
cause:
- the events were identical (0 lines differ);
- the step records were identical as a set;
- only the interleaving of a step record with the ratchet sub-notes of one
  block differed.

So this was an artifact of the hasher, not of the core. Events and step
records are now two hashes. Every pattern then reads identically at both
block sizes.

The bench digest hashes the same way. It compares only equal block sizes, so
it is unaffected.

**Portability.**
- The driver has no RNG of its own and calls nothing whose argument order
  matters.
- Hashes go over explicit little-endian bytes (`stream_digest.h`).
- Integers print with `%lld` casts. Times are integer-exact
  (`sr * 703 / 100`).
- Native and two-word int128 produce byte-identical files on this Mac
  (Apple clang).
- Linux g++ is proven by CI on the PR, not here.

**Cost.** About 0.7 s wall for the 13 generator runs: the plants and the two
paths.

**Critic (Opus, independent, model pinned), verdict ACCEPT, no HIGH.**
- M1, applied: the first cut released at 7.03 s, on a plain step of every
  pattern, and `reset()` ended nothing. So "pins the release and the end"
  was overstated. Fixed by the 6.94 s release, the held top key and the
  7.95 s end, all above.
- M2, applied: swing with bank patterns was pinned nowhere. Fixed by the
  swing-600 pair.
- M3, open for the lead: nothing enforces "protected once cut". A CLAUDE.md
  protected-path line and a sha256 pin like `spec_gate` are proposed in the
  PR; editing `./verify` is a human gate.
- L1, applied: the `edit-ev` plant.
- L3, applied: the generator's hashes are in the header.
- L2 (a control that each bank field reaches the output), not applied: the
  excerpts show ratchet, tie, probability, step and accent output directly.
- L4: D-046 is in PR #38, not yet on main; the PR notes the dependency.
- L5 (the goldens build by default, so sanitizer CI compiles them too):
  harmless, noted.
- The critic proved the macOS side itself: bash 3.2 runs the golden section
  green; a `-O0` build reproduces every file; no machine path or private
  name leaks.

## Verify

`bash tools/core_test.sh` (exit 0), these lines verbatim:

```
PASS  golden plant: one event of one pattern changed is named under that pattern alone, for each of the 11 bank patterns (and the unperturbed run matches itself)
PASS  golden plant (edit): golden offbeat: 1 line(s) differ from the committed golden; first: < run up swing=500 44100 61 events=64 on=32 off=32 trig=0 events-hash=xee9106cc23b3799 steps-
PASS  golden plant (edit-ev): golden offbeat: 1 line(s) differ from the committed golden; first: < ev 6000 on key=60 vel=999 lane=0 stream=arp ref=0 tick=3840 step=1 -> > ev 6000 on key=60 v
PASS  golden plant (missing): golden offbeat: no committed golden for this bank pattern
PASS  golden plant (header): golden offbeat: the header does not record the commit it was cut from
PASS  goldens (arp_goldens): 11 bank patterns x 20 runs (3 orders x 3 rates x 2 block sizes, and a swung pair) and an event excerpt each match tests/core/goldens/ (cut from c5741de5da)
PASS  goldens (arp_goldens_soft): 11 bank patterns x 20 runs (3 orders x 3 rates x 2 block sizes, and a swung pair) and an event excerpt each match tests/core/goldens/ (cut from c5741de5da)
PASS  core_test: all binaries green
```

The `./verify fast` result and the commit hash are in the PR.
