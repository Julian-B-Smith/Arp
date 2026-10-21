# Arp — DECISIONS (append-only)

> The decision log. Append only: never edit or delete a prior decision;
> supersede it with a new one that cites it. ROADMAP.md holds task state,
> acceptance criteria and open questions; the *why* behind a ratified choice
> lives here. Status words: PROPOSED (lead's answer, awaiting the human) ·
> RATIFIED (human said yes, date) · SUPERSEDED (by which).

## Settled elsewhere — imported, not re-litigated (2026-10-21)

These were ruled in HYPERSAW (horde) before the spin-out and bind here.
Re-opening one is a brief to horde. Source: `SPINUP-BRIEF.md` and `intake/`.
- **Arp is its own project**, by the human's request in horde (2026-10-21):
  "the arpeggiator … should get its own repo and agent". Working name, public
  repository, and the scope "note generation home" were chosen by poll the
  same day.
- **horde ADR-190 A5:** a simple arpeggiator and the shared quantizer are in
  1.0; complex patterns, step sequencing and generators come after.
- **The note vocabulary** (`intake/note-vocabulary.md`):
  - identity is `NoteRef`-compatible, never a bare key;
  - exactly one terminal per identity;
  - pitch on the bus is canonical, so Arp emits base pitches.
- **The engine's terms** (`intake/engine-note-interface.md`): IF-1 (notes by
  id, sample-timed), IF-2 (pitch by id), IF-11 (host tempo and transport as
  input), VO-6 (mono/legato) and VO-12 (the pedal). Legato and the pedal's
  effect on voices are engine behaviour, not Arp's.
- **The scale** is horde's global setting (B260): root + 12-bit mask. Arp
  reads it and never owns it.
- **Tempo divisions** are horde's list, stored by name (`intake/tempo-divisions.md`).
- **Name:** user-visible names go through horde's B433 clearance before 1.0.

## D-000 — The spin-up survey (RATIFIED by poll, 2026-10-21 — see D-003)

Nine polls plus the brief's six open questions, 2026-10-21. The manifest is the
record. Rejected alternatives are kept here so they are not re-proposed.
- **Q1:** library + lab twin, no plugin. Rejected:
  - library only (no way to play patterns before horde integrates them);
  - + standalone plugin (shell, ids, host validation and name clearance, all
    earlier).
- **Q2:** rung 2 (thread + read-only verifier/critic). Rejected:
  - rung 1 (no independent eye on a core horde 1.0 depends on);
  - rung 3 (one small core, no seams, no bottleneck, about 15× the tokens).
- **Q3:** a pure note processor, as the brief defines it. Rejected: putting
  horde's adapter in the core (couples Arp to horde's engine, which conflicts
  with the FX-device use after 1.0).
- **Q4:** invariants first, goldens once the C++ exists. Rejected:
  - invariants only (an unintended pattern change that still keeps the
    invariants would pass);
  - goldens first (would enshrine defects; horde ADR-142's lesson).
- **Q5:** no port-pin; the C++ is the reference. Rejected: a lab pinned to the
  C++ (every change paid for twice, for a twin that is only for listening).
- **Q6:** provider to horde; consumer of FOUNDATIONS' note vocabulary when the
  C++ first touches it. Rejected: horde only, with a private NoteRef copy (a
  second vocabulary that drifts).
- **Q7:** no audit thread. Rejected: a one-off audit (there is no legacy arp to
  port).
- **Q8:** the brief's five tags: note-lifecycle, timing-transport, determinism,
  sr-blocksize-independence, held-note-logic. Rejected:
  - a sixth, `realtime-safety` (stays an invariant, not a knowledge category,
    until a lesson earns it);
  - three tags only.
- **Q9:** long-lived, medium autonomy: PR per batch, the human merges.
  Rejected:
  - low (every change live, too slow for the cutoff);
  - high (unattended batches).
- **Open 1, modes:** the brief's list, including seeded random. Rejected:
  - no random for 1.0;
  - more modes (converge/diverge, pinky): too wide for the cutoff.
- **Open 2, rate:** synced only for 1.0; free Hz after 1.0. Rejected: free Hz
  now (it needs a phase-anchor rule and a second timing invariant).
- **Open 3, pedal:** the pedal holds Arp's input, and Arp consumes CC 64.
  Rejected:
  - passing it through to the engine (its notes would pile up as VO-12 tails
    and smear);
  - ignoring it.
- **Open 4, restart: a PARAMETER with all three values** (chord from silence /
  on the bar / free-running). **The human chose this over the lead's single
  recommended rule** (chord from silence). Step timing stays on the transport
  grid in every value. Cost accepted: three invariant sets in M2.
- **Open 5, morph: ALL settings are morph targets.** **The human chose this
  over the lead's recommendation** (global, not morphed, in 1.0) and over
  "continuous only". Discrete settings switch at a threshold. The threshold,
  the grid alignment of a switch, and terminal safety under a switch are
  Phase A spec items (ROADMAP Q-A1, Q-A2). This adds "morph-driven discrete
  switch" to the terminal-path list (LIBRARY L0001).
- **Open 6, step sequencer:** decided at the post-1.0 gate. Rejected: pinning
  "extend the grid" or "its own lab" now.

## D-001 — Knowledge-loop forks (lead's default, 2026-10-21)

Candidate lessons go inline in LIBRARY (`tier: candidate`), not in a QUARANTINE
file. Reflection is enforced at session close by `/breakdown`. This matches
the fleet (Scape, Bulwark, Echo). Rejected: a quarantine file (no reviewer
cadence to drain it).

## D-002 — intake/ is pinned by hash (lead's default, 2026-10-21)

horde's `intake/MANIFEST` holds the sha256 of every handed-over file, and
`./verify`'s `intake_gate` goes red on any edit or unlisted addition. The
charter's "intake is read-only" is enforced, not just stated (the kit's gate
rule: assert the effective state). The gate reads horde's MANIFEST as written,
with repo-root paths, so it is an adaptation of Echo's gate, not a copy.
A re-pin is a human gate. Rejected: prose-only protection.

## D-003 — Manifest ratified (RATIFIED by poll, 2026-10-21)

The human ratified `project.manifest.json` as surveyed, by poll, with no
changes; it is now a protected path. In the same poll batch:
- The initial commit is pushed straight to `main` on the empty remote. There is
  no base branch to open a PR against; every later change is PR-per-batch
  (Echo's D-003 precedent). Rejected: commit locally only.
- The spin-up filings stay drafts until ROADMAP T-002. Rejected: filing both
  now from this session.

## D-004 — A pattern bank joins 1.0 (RATIFIED by poll, 2026-10-21)

The human asked for "a bank of patterns for the arpeggiation, like Ableton's
arpeggiator". This partly supersedes D-000 Open 1, which kept the brief's six
modes and rejected extra modes for the cutoff. Settled by poll the same day:
- **Contents: both kinds.** (a) Note-ORDER styles beyond the six modes, and
  (b) preset STEP patterns (per-step rhythm, rests, ties, accent, gate, octave
  jump) that play over an order style. Rejected:
  - order styles only (Ableton's shape, which has no step bank);
  - step patterns only.
- **Timing: a starter bank in 1.0, more after.** About 12–16 curated entries
  on a DATA-DRIVEN bank format, so growth after 1.0 needs no core change.
  Rejected:
  - the full bank in 1.0 (the biggest cutoff risk);
  - after 1.0 only.
- **Consequences:**
  - Every bank entry passes the same invariant suite (one terminal per note,
    grid, determinism).
  - Bank selection is a discrete setting, so it morphs under the Q-A1 rule.
  - Phase 0 now also surveys pattern banks.
  - Which entries ship is ROADMAP Q-A3.
- **Clean-room:** styles are specified as generic algorithms under our own
  names. Screenshots of other arps that the human shares are inspiration only:
  no names, preset names or UI text copied.
- The manifest's `scope_1_0.pattern_bank` records this. Editing a protected
  path is covered by this poll.
- Same poll: the spin-up filings are filed now (T-002), superseding D-003's
  "drafts until T-002". The horde notice states the bank.

## D-005 — What Phase 0 changes (recorded 2026-10-21; design rules PROPOSED for the Phase A spec)

The findings are in `docs/prior-art.md`, dated and cited.

**Reuse:**
- **Order and rhythm are orthogonal.** The order style yields a note stream,
  and the step pattern gates it. A rest does not consume a note. Pattern
  length is independent of the held-note count.
- **The two bounce variants are defined by period:** 2n−2 for ends once, 2n
  for ends twice.
- **Starter-bank candidates for Q-A3:**
  - order styles: ascend, descend, both bounces, as played, chord, random
    free, random no-repeat, converge, diverge;
  - about 8–10 step patterns that differ in kind (straight, off-beat, gallop,
    syncopated, accent-on-one, tie-legato, octave-jump, ratchet roll, sparse)
    rather than escalating one idea.
- **The integer-tick grid:**
  - 15,360 ticks per quarter makes every horde division exact (checked by the
    lead; 96 is the minimum);
  - the step tick comes from the absolute step index, never accumulated;
  - the block is the half-open window [start, end), every boundary in it
    fires, and offsets are never clamped.
- **One emitted-note table and one idempotent `release(NoteRef)`** as the only
  terminal path. Same-key overlap is ordered off-then-on at the same sample.

**Avoid:**
- running float position across blocks;
- frame-scheduled note-offs (the LMMS tempo bug);
- a wall-clock fallback when the transport is absent (use a deterministic
  sample-counter clock);
- unseeded or stateful-per-call RNG (draw from (seed, absolute step, lane));
- trusting a CLAP beat position without its beats and tempo flags (the
  clap-wrapper gaps);
- C `%` on negative ticks;
- ambiguous end-repeat definitions;
- copying any product's style names, preset names or UI text.

**Differentiate:**
- per-step probability (no arp we read documents it);
- swing defined unambiguously: the odd step on the absolute grid is
  displaced by a stated fraction of the step pair.

**Corrections the lead made to agent claims:**
- 15,360 is not divisible by 7;
- CLAP's 2³¹ beat factor does not convert exactly to 15,360 ticks, so it
  rounds once per block by a documented rule.

**Name:** high collision risk for "Arp" as a product name ("ARP" is a live
mark, almost certainly Korg's); low as an in-UI feature label. Keep it as the
codename; horde's B433 clears the public name. Directions are in the doc.

**For M3:** a logging probe in each target host, to settle what public
sources cannot (equal-timestamp event order, loop-point block splits, which
transport fields each DAW fills).

## D-006 — Four more features join 1.0 (RATIFIED by poll, 2026-10-21; ROADMAP Q-A4)

From the Phase 0 landscape, by one multi-select poll, the human took all four:
1. **Latch with two behaviours.** Replace: a new chord after a full release
   replaces the held set. Append: keys added while latched join it.
2. **Gate above 100 % (legato overlap).** A repeated key under overlap is a
   forced terminal at the next step (off-then-on at one sample). What the
   voice does is horde's engine (VO-3/VO-6).
3. **Per-step probability** in step patterns, drawn deterministically from
   (seed, absolute step, lane).
4. **Ratchets:** a step repeats 2–4 times within its length, and sub-steps
   are placed exactly on the tick grid.

Each adds spec rows, invariant tests with planted controls, and a morph rule.
The manifest's `scope_1_0` records them (a protected path, covered by this
poll). **Cutoff risk:** with D-004, the 1.0 surface has grown twice since
ratification. ROADMAP now says which of them yields first if the cutoff
tightens. Proposed: ratchets, then probability, since both live in bank data,
can ship as entries after 1.0, and need no core-format change if the format
reserves their fields now.

## D-007 — horde's trigger amendment kept; trigger output in 1.0 (RATIFIED by poll, 2026-10-21)

- **Provenance.** At 19:06 on 2026-10-21, horde's lead wrote a new bullet
  straight into this repo's uncommitted `SPINUP-BRIEF.md`, outside our
  mailbox (`integrations/hypersaw/`). The bullet is trigger output, per horde
  ADR-267 A1 (on horde's `lead-records-311`; read there, not copied). The
  human chose to **keep it**. It is committed here as horde's amendment to
  its own brief, attributed in this entry. The next notice to horde asks that
  later requests come through the mailbox (INTEGRATIONS §3, rule zero).
  Rejected: reverting and asking horde to re-file (the content is real, so
  only the route was wrong).
- **Trigger output joins 1.0** (the human chose this over the lead's
  "reserve the field, build after 1.0").
  - Step patterns carry a `trig` lane, 0–8. A firing step emits
    `Trigger{lane}` at its on-time, and trigger-only steps exist (SPEC §4.3,
    §8).
  - A trigger is a performance event: it has no history and no terminal.
  - Lane → action/impulse routing is horde's. The handoff shape is asked as
    **Q-H3**, the third question to horde.
- **Cutoff:** the third widening of 1.0 today. The ROADMAP's yield order
  becomes **ratchets, then probability, then triggers**. Triggers are last
  because horde's lead asked for them, so they serve the consumer directly.
- The manifest's `scope_1_0.trigger_output` records this (a protected path,
  covered by this poll).

## D-008 — Spec r2 after the independent critic review (recorded 2026-10-21; the position policy PROPOSED)

The Opus critic, sourced separately from the author, rated draft r1 **REWORK**
(13 HIGH). All findings are folded into DRAFT r2. The disposition of each is
in `docs/spec-review-r1.md`.

**Position policy (the critic's open question, answered by the lead,
PROPOSED).** When the host's reported position differs from Arp's prediction:
- within 1/16 beat, Arp **silently follows the host**. No notes are cut, and
  a once-guard keeps every grid step exactly-once.
- beyond that, it is a jump: release-all and re-anchor.

Rejected:
- r1's "keep our own count" (it drifts and causes false jumps about every
  12 s at a fractional tempo);
- "always follow, never jump" (a seek would double or skip steps
  unboundedly).

The human confirms this at spec ratification.
