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

## D-009 — Note streams: Arp's notes tagged `arp`; horde tees direct notes; the pedal proposal (RATIFIED by poll, 2026-10-22)

horde filed `notice-note-stream` (seq 3, horde ADR-270) through our mailbox
(PR #6), from the human's wish "to send the arpeggiator to some oscillators
but not others". Each horde source gains a Both / Direct / Arp selector.

**By poll, the human ruled:**
- **Arp tags every NoteOn it emits `stream = arp`.** This is SPEC r2.2 §1,
  proven by the new lab check S1 with a planted control.
- **horde's shell tees the player's held notes to the engine as `direct`,
  before Arp.** Arp never echoes input notes.
  - Rejected: Arp passing the held notes through. That would make Arp a
    router, with echoed refs, their terminals, and switch-off rows, and it
    breaks the pure-note-processor core (Q3) that horde's FX device needs.
- **Proposed to horde (Q-H5): the engine's pedal (VO-12) sustains `direct`
  voices only.** Arp keeps consuming CC 64 as its input hold.
  - Rejected: no engine pedal while Arp is on (a pad cannot ring under the
    arp).
  - Rejected: the engine pedal holding everything (Arp's notes smear into
    tails, the very problem D-000 Open 3 rejected).

Our response, `integrations/hypersaw/response-note-stream.md` (seq 4), sits
beside the notice. Ball: HYPERSAW.

**A lesson from building it:** the smoke's event normaliser dropped the new
`stream` field. The S1 plant then "fired" only because every NoteOn looked
untagged, which is a vacuous pass. The clean run failing exposed it, and the
fix was to carry the field through the normaliser and the G1/D1 stream
comparisons. Recorded as LIBRARY L0002.

## D-010 — Scale quantize in horde's scale schema (RATIFIED by poll, 2026-10-22)

The human asked for "a scale quantize option that fits into horde's scale
quantization schema". The lead read horde's records rather than guess:
- B260 and B391: the global scale is `scaleRoot` plus `scaleDeg0..11` (legacy
  ids 116–128), lifted out of the bend quantizer;
- the ruling "the mask is the truth, the name is UI";
- `bend-lab.html`'s `quantise()`: a root-relative mask, ties toward the
  previous emitted step, and 18 named scales;
- ADR-112: movable-do "scale (offset)".

**By poll, the human chose:**
- **Scale source: global by default, own optional.** `scale_source` is
  `global` (horde's scale, read never owned) or `own` (Arp's `scale_root` and
  `scale_deg0..11`, in exactly horde's shape). Rejected: global only.
- **Modes: off / scale / chord_root.** `chord_root` is horde's movable-do for
  an arp: the scale shape is anchored on the lowest held key, and the root is
  ignored. Rejected: off and scale only.
- **Ties toward the previous quantized output**, as horde does, with the
  lower key when there is nothing before. This settles Q-A5's tie half and
  supersedes SPEC r2.1's "ties go down". Rejected: always down, which
  diverges from horde.

**What changed:**
- SPEC r2.3 §6, with new invariants Q2–Q4, each with a plant;
- `params.json`: `quantize` becomes an enum; `scale_source`, `scale_root`,
  `scale_deg0..11` and `ui_scale_names` (horde's 18) are added;
- the lab core and UI: an own-scale control, and horde's names on the
  simulated global scale.

The core's discrete-setting list is now derived from `params.json`, so a new
discrete setting cannot be missed. Our root-relative degree math already
matched horde's; the mode and tie rules are new.

**For horde:** an Arp preset may now carry its own `{root, mask}`. That fits
horde's own rule (consumers transmit `{root, mask}`). It goes in the next
notice as information, with no ask.

## D-011 — Chord + note order styles; per-step `pick`; the user pattern (RATIFIED by poll, 2026-10-22)

The human asked for "a couple modes that mix chords and notes".
- **They took all four offered styles:**
  - `chord_walk`: the chord, then a walk up;
  - `stride`: bass, then the rest as a chord, oom-pah;
  - `chord_between`: the chord between single notes;
  - `pairs`: dyads stepping up.
- **They added "build your own pattern"**, clarified by a second poll as:
  - **both notes and rhythm per step**;
  - **in 1.0, lab first**.

**The design (lead's):**
- **The four styles** are SPEC §4.2 rows with exact periods.
- **A per-step `pick` field** (`order` by default, or 1–8, low, high,
  chord) chooses which held note(s) a step plays. It exists in every
  pattern, not just the user's.
  - **Only `order` picks advance `m`**, so explicit picks thread through an
    order style without consuming it.
  - This makes "build your own" the general case: one step format serves
    the bank and the user.
- **The user pattern** is pattern id `user`, up to 16 steps.
  - It is **preset data**, not host parameters.
  - It is set off the audio thread into a preallocated, double-buffered
    slot, and swaps at the next step.
  - Its content does not morph; choosing it does.

**Rejected:**
- order-only or rhythm-only editors (the human chose both);
- after 1.0 or lab-only.

It is also the natural seed of the post-1.0 step sequencer (ROADMAP's
step-sequencer question).

**Cutoff:** this is the fourth widening of 1.0. The yield order becomes
ratchets → **user pattern** → probability → triggers. The human's poll put
the user pattern after ratchets and before probability.

**For horde (Q-H6):** how should a preset carry a module's small structured
blob? This goes in the next notice, together with D-010's own-scale note.

## D-012 — `landed`: the order and the timing of the presses (RATIFIED by poll, 2026-10-22)

The human asked for "as played (arp in the pattern of the notes as they
landed)". The existing `played` already steps in press order, so a poll
asked which was meant. The human chose **order and timing**, a new mode.
Rejected:
- order only (`played` already does it);
- order with repeats.

**The design** (SPEC §4.7; the lead's, with one rule PROPOSED):
- **Capture.** A gesture's presses are captured as `(key, vel, ch, q)`, where
  `q` is the nearest grid step from the first press, with ties rounding up.
  Repeats count, and presses that land on the same step form a chord.
- **Lifetime.** The phrase is a recording: it lives until H is empty, and a
  key released mid-gesture keeps its captured press.
- **Playback.** The phrase loops at `len`, anchored on its own first press,
  so `restart` does not apply. Step patterns overlay it, and `m` never
  advances.
- **PROPOSED (Q-A6):** `len` rounds up to a whole beat when the rate divides
  a quarter note.
- **Capacity:** 32 presses and 64 steps, preallocated.

This is the fifth widening of 1.0. It is an order style and data-light, so
it sits early in the yield order: it yields after ratchets, together with
the user pattern.

## D-012a — `shuffle` never repeats back-to-back (2026-10-22, at the human's request)

The human asked for "a random other (random without repeat notes)". The
`shuffle` order already plays every held note once per cycle in a seeded
random order (the shuffle-bag model from Phase 0). It allowed one repeat:
the last note of a cycle could equal the first of the next. SPEC §4.2 now
closes that with a deterministic rule: when a new cycle would start on the
note just played, its first two entries swap. So `shuffle` is "random
without repeats", and no back-to-back repeat ever occurs when 2 or more keys
are held. New invariant N2, with a plant. Taken as the plain reading of the
request; no poll was needed.

## D-013 — Spec r2.6 after the second independent review (recorded 2026-10-22; the `lastQ` reset PROPOSED)

The Opus critic, sourced separately from the author, rated the r2.3–r2.4
additions **REWORK** (5 HIGH, 8 MED, 4 LOW). All findings are folded into
r2.6; `docs/spec-review-r2.md` holds the per-finding disposition. The two
that change behaviour the human hears:
1. **The quantize tie memory resets at every release-all**, so every
   play-from-here sounds the same. This is PROPOSED, for the human to
   confirm at ratification.
2. **A scale morph switches root and degrees together**, as one group, with
   no hybrid scales mid-morph.

Also: horde ADR-169 must be read before Q-H6 is filed, because it may
already answer how presets carry module data.

## D-014 — `landed` deferred past 1.0; `octaves` becomes Ableton-style transposition steps (RATIFIED, 2026-10-22)

**`landed` is deferred.** The human said: "remove as landed and hold it for
after 1.0". This supersedes D-012 for 1.0. The design, the lab-build
lessons (the first-press snap-back fix) and the open loop-length question
are preserved in `docs/deferred/landed.md`. The last commit with the working
implementation is on branch `lab/chord-modes-user-pattern`, and is recorded
in that branch's trace. It is removed from the spec, params, lab core, smoke
and UI. D-012a (`shuffle` without back-to-back repeats) stays.

**Steps replace octaves.** The human said: "taking a cue from ableton …
instead of 'octaves' it should be 'steps' and there should be a step size
that defaults to 12 semis". By poll:
- **Transpose per repeat (Ableton-style)**: each full pass of the order
  style repeats shifted by `step_size`, for `steps` passes, then returns.
  Rejected: a merged range (r2.6's `octaves`, generalised).
- **The step size is in semitones or scale degrees.** `step_unit` is
  `semitones` or `degrees`; degrees count in-scale keys of the effective
  scale. Rejected: semitones only.

The settings are `steps` 0–8 (default 0), `step_size` −48…+48 (default +12)
and `step_unit`. This is SPEC r2.7 §4.7, with invariant X1. Order styles now
walk just the held keys; `chord` and `stride` lose their octave cycling,
because transposition supplies the range.

## D-015 — horde answered the spin-up and trigger notices (recorded 2026-10-22)

horde filed `response-spinup` (seq 2, 2026-10-21) and `response-triggers`
(seq 4, 2026-10-22) in its own tree (`integrations/arp/`). Both are
`ball: Arp`, "nothing owed". Read and recorded:
- **Q-H1 (the 1.0 bar) is answered.** It is horde's
  `docs/proposals/note-processor-1.0-bar.md`:
  - every note ends exactly once;
  - sample-exact against the transport, with time signatures as data;
  - no input gives no output;
  - the pedal and restart behaviour as ruled;
  - quantize stays in scale;
  - triggers never create history;
  - **CPU ≤ 2 % of a min-spec core**, within horde's modulation, morph and
    intent bus slice;
  - cutoff 2026-12-11.

  It becomes the M3 gate's checklist.
- **Q-H2 (consumption) is answered.** horde **copies our core into its own
  tree at our V1**, by a horde PR that records our commit hash. There is no
  submodule, package or lock file (horde ADR-269, after Sluice D-096). So:
  - the C++ core stays **self-contained and framework-free, buildable
    without horde**;
  - M3 ships **a file list** for horde to take.
- **Q-H3 (trigger lanes) is answered.** Our `Trigger{lane 1–8, sampleOffset}`
  is right.
  - horde shows the lanes as event sources "Arp · T1"–"T8".
  - horde stores any lane names in its own preset, as display text.
  - The lane→target routes are horde's patch data.
- **The routing request is acknowledged.** horde apologised, and everything
  now comes through our mailbox.
- **ARP name** noted for horde's B433.

Still open with horde: Q-H5 (the direct-only pedal) and Q-H6 (presets for
the user pattern and its library), which is ours to file after reading
ADR-169.

## D-016 — Time mode; the user-pattern library; step offset × pattern step size (RATIFIED by poll, 2026-10-22)

The human said: "we only created a sync mode and we need a time mode as
well", "save 'build your own' arp patterns as presets", and "replace the
static octave with step and step size in the build your own". By poll, at
`/wakeup`:
- **Time mode, in 1.0.** This supersedes D-000 Open 2's "free rate in Hz
  after 1.0".
  - `rate_mode` is `sync` or `time`.
  - In time mode, one continuous setting, `step_time_us` (10 ms to 2 s), is
    **shown both as ms per step and as Hz**.
  - It **fires from the key press** in either case, ignoring the host tempo
    and transport.
  - SPEC §2.6: a tick is one microsecond, so every timing formula applies
    unchanged and exact.
  - Rejected: transport-anchored time (the human chose the key press).
- **A user-pattern library.**
  - Named saved copies of user patterns. Recalling one loads it into the
    single user-pattern slot.
  - It is adapter and UI data, not core state.
  - The lab library lists patterns saved in the repo (`lab/patterns/`,
    format `arp-user-pattern/1`) and in the browser, with JSON
    export/import.
  - In horde, it lives in horde's presets (Q-H6).
  - Rejected: a bank-design-only save.
  - **First entry:** the human's "Broken chord stabs" (picks 1, 3, 2,
    chord, 1, 4, chord +1 step, 2), saved at their request.
- **The step offset.** The per-step `oct` becomes `step` (−8…+8), times a
  per-pattern `step_size` (−48…+48, default +12) in `semitones` or
  `degrees`.
  - With the defaults, `step ±1` is exactly the old octave, so the bank's
    "Octave leap" is unchanged.
  - The bank format gains `pattern_defaults`.
  - SPEC §4.7 now sums semitone and degree parts from the transposition
    and the step offset, in one stated order.
  - Rejected: per-step sizes, and a bare semitone offset.

**Scope:** this is the sixth and seventh widening of 1.0. Where time mode
and the library sit in the cutoff yield order is **not yet placed**: ask at
the next phase gate. The library is UI-side and cheap. Time mode is core
timing, with a new invariant TM1.

## D-017 — Spec r2.9 after the third independent review; a smooth step-time sweep (RATIFIED by poll, 2026-10-22)

The Opus critic, sourced separately from the author, rated r2.8 **REWORK**
(2 HIGH, 3 MED, 3 LOW). The per-finding disposition is in
`docs/spec-review-r3.md`.

The human's ruling on the critic's question: **sweeping the step time
speeds the arp up or slows it down smoothly from the step it is on**.
- The next step comes at `max(lastTick + L_new, changeTick)`, then every
  `L_new`: no gap beyond max(L), no burst, and the pattern position kept.
- Rejected: waiting and restarting the pattern, as a synced rate change
  does.

Also adopted:
- **restart is inert in time mode** (there are no bars in time);
- `rate_mode` applies at offset 0 of its block;
- the offset stages run per key;
- the user-pattern format rules;
- **one accepted behaviour change from r2.7:** with the global unit in
  degrees, a semitone step offset now applies before quantize.

The implementer's earlier reading of a step-time change (re-grid on
absolute multiples of the new length) is superseded.

## D-018 — Repeats; the chord stage with its own stream; then a functionality freeze (RATIFIED by poll, 2026-10-22)

The human said: "Two last things and then I think we're good to properly
freeze functionality". By poll:
- **Lock: no change.** The asked-for "lock" (keep playing the last chord
  until a new one) is the existing latch `replace`. The human chose to keep
  latch as it is.
- **Repeats.** `repeats` runs 0–16, and 0 means forever.
  - One repeat is **one full cycle of the whole pattern**: the step
    pattern, the order style through every held note, and the transposition
    passes, all coming round together.
  - The count runs from the gesture start. After the last repeat the arp is
    silent until the next chord.
  - Rejected: step-pattern passes; one pass through the held notes.
- **The chord stage.** Pre-arp intervals live in Arp, and **its output is
  its own stream**. The human's ruling: "I would like to be able to send the
  chord to oscillators independently as well (one gets the chord, the other
  gets the chord → the arp, etc.)". So:
  - SPEC §14 is a second processor before the arp: each held key plus up to
    6 shifts (−24…+24), in semitones (optionally quantized) or degrees;
  - its notes carry `stream = chord`, deduplicated with source counts;
  - the arp plays the chord set when the stage is on;
  - the stage works with the arp off.

  Rejected: only feeding the arp; and a separate module, which would lose
  "chord → arp" inside one stream graph.
- **Intervals:** up to 6 shifts, in semitones or degrees. Rejected: a shape
  list; both.

**For horde (Q-H7):** horde's per-source selector (ADR-270) must route a
third stream, `chord`, so that one oscillator can take the chord and another
the arp. A notice is drafted: `docs/filings/hypersaw-notice-chord-stream.md`.

**The freeze.** The human intends to **freeze functionality** once this
batch lands. From then on, 1.0 scope changes only by an explicit unfreeze
decision. Still to place at the next gate: where time mode, the library,
repeats and the chord stage sit in the cutoff yield order.

## D-019 — Spec r2.11 after the fourth independent review: chord notes behave like held notes; any press restarts repeats (RATIFIED by poll, 2026-10-22)

The Opus critic, sourced separately from the author, rated r2.10 **REWORK**
(4 HIGH, 6 MED, 4 LOW). The per-finding disposition is in
`docs/spec-review-r4.md`. The human answered its two questions:

1. **Chord-stage notes behave like the player's held notes.** They follow
   only the keys, the pedal and the latch, and ignore transport edges, the
   arp's on/off and the arp's notes.
   - This adds **`arp_enabled`**, a new setting, approved through this poll.
     `enabled` is now the whole module, and "chord on, arp off" is a chord
     generator.
   - Rejected: behaving like arp output (a held chord would retrigger at
     every loop and at play/stop).
2. **Any key-adding press restarts the repeat count**, under latch append
   and the pedal too. Rejected: only a fresh gesture.

**Structural fixes:**
- the chord mapping is stored per (held key, slot);
- T2 applies within one stream;
- spent repeat steps still consume the once-guard;
- an `exhausted` flag;
- dedupe by key;
- the chord stage has its own sounding table.

## D-020 — One quantize switch for the arp and the chord stage; the lab plays arp + chord by default (RATIFIED by poll, 2026-10-22)

The human reported "sustained notes layered over the arp" and "the scale
quantization doesn't really work on either the chord or the arp". The lead
reproduced both in the browser:
- **The sustained notes** were the lab's monitor of the raw held keys.
  "Listen to" defaulted to "all (direct + chord + arp)", and the direct
  stream is the untouched input, never quantized.
- **The arp's quantize worked**: C♯ and F♯ held in C major gave D, F, G.
- **The chord stage had a real bug.** Its `+0` slot was "h as played, never
  moved" (r2.11, a lead's choice adopted from a build gap), so C♯ sounded
  as C♯. Only the added intervals snapped.

**By poll:**
- **One quantize switch for everything Arp emits.** The arp's `quantize`
  governs the chord stage. While it is on, every chord key snaps, the
  played note included. `chord_quantize` is **removed**. Rejected: keeping
  separate switches.
- **The lab plays arp + chord by default.** Direct becomes opt-in, labelled
  "your raw keys, unquantized (horde's direct stream)". Rejected: arp only.

A bug fix under the freeze: it removes a setting and adds no new
functionality.

## D-021 — The listening questions settled; the whole bank ships (RATIFIED by poll, 2026-10-23)

Before ratifying the spec, the human accepted all seven proposed rules by
poll, each implemented and tested in the lab:
1. **Q-A1, morph:** a discrete setting takes the leading corner's value,
   switching only when it leads by 0.05, at the next step.
2. **Q-A2, restart under morph:** into `bar` or `free`, it re-aligns at the
   next step; into `chord`, it waits for the next gesture.
3. **Q-A5, pre-roll:** the arp plays during the host's count-in.
4. **r2 H2, tie memory:** `lastQ` resets at every release-all, so every
   play-from-here sounds the same.
5. **Q-A7, pick pass:** a pick takes the next order note's transposition
   pass.
6. **Q-A8, chord re-voice:** a shape change while holding swaps only the
   changed notes, at once.
7. **D-008, host position:** Arp follows the host silently within 1/16
   beat; beyond that it is a jump.

**Q-A3, the bank:** all 16 order styles and all 11 step patterns ship in
1.0. Factory presets that combine them are curated at M3 with horde's
preset work. Rejected: a curated subset now. The old "12–16 entries" target
predates the matrix and the user library.

## D-022 — The 1.0 spec is RATIFIED; Phase A closes (RATIFIED by poll, 2026-10-23)

The human ratified `spec/` (SPEC.md r2.12, params.json with 47 settings,
and the bank with 11 patterns) by poll, with no changes, after D-021
settled every open rule.
- **`spec/` is now a protected path.** Any change needs the human's gate
  and a decision record.
- **The lab stays editable** as the listening twin; its core must keep
  matching the spec, which the smoke enforces.
- **The functionality freeze (D-018) stands.**

Phase A's gate is met:
- the spec is ratified;
- the lab smoke is in `./verify fast`, with every detector proven on a
  plant;
- the listening questions are closed (D-021).

**Next: M1**, the self-contained C++20 core that horde copies in at our V1
(D-015).

## D-023 — The ratified spec is pinned by hash in ./verify (RATIFIED by poll, 2026-10-23)

The human approved, by poll, enforcing D-022's "spec/ is protected" rather
than only stating it:
- `spec/MANIFEST` holds the sha256 of `SPEC.md`, `params.json` and
  `bank/starter.json`;
- `./verify fast` runs `spec_gate`, which is red on any unpinned edit or new
  file under `spec/`;
- the intake pin and the spec pin now share one helper, `pinned_dir_gate`.

Both were proven on plants: an edit to SPEC.md, a new file, an edit to
params.json, and the intake pin after the refactor all go red; the clean
tree is green. A deliberate spec change is now three steps: the human's
gate, a decision record, and a re-pin of `spec/MANIFEST` in the same change.

## D-024 — Spec errata E1 and E3 approved; E2 fixed in code (RATIFIED by poll, 2026-10-23)

The M1a C++ build found three places where the ratified spec's text should
change (`docs/spec-errata.md`). The human approved two amendments by poll:
- **E1 (§2.3):** an input event or block outside the window is **refused
  and counted** (`badInput`), never an assertion abort. The audio thread
  must not stop.
- **E3 (§1.3):** the sounding table is 128 plus a **slack of 16** for notes
  released inside their own first sample, which the 1-sample minimum keeps
  alive. If the slack is full, the new on is refused and counted.

**E2 needs no amendment.** `prepare()` could not emit T10's offs; instead
the core keeps the owed offs in construction-time storage and delivers them
at offset 0 of the first `process()` after `prepare()`. The spec as written
(T10) holds, and no adapter call order can strand a note (critic M1a MED-2).

`spec/MANIFEST` is re-pinned in the same change, as D-023 requires.

## D-025 — M1b review: errata E4 and E5, the degree unit moves to M2, four core files join the structure list (RATIFIED by poll, 2026-10-23)

The independent critic review of M1b (T-004) found no HIGH and raised three
points that needed the human. Decided by poll:
- **E5 (§2.6), time mode stops at key release.** §2.5 said the time clock is
  idle while H is empty; §2.6 said it stops "when Arp is idle" (§2.4's idle:
  nothing held, sounding or pending), and the code and lab followed §2.6, so a
  trigger-only step still fired after the keys were released while a long-gate
  or tied note rang. Chosen: in time mode **no step is evaluated while H is
  empty** — no notes, no triggers — and notes already scheduled still end on
  time. Rejected: keeping the §2.4 idle (triggers would outlive the hands in a
  mode that is defined by the key press). Sync mode is unchanged.
- **E4 (§1.2), the id counter after `prepare`.** M1a's critic (LOW-A) asked
  that a new note never share a ref with a note still owed an off after
  `prepare`. M1b starts the counter above the largest owed id in that case;
  the spec said "starts at 0". Approved as an amendment (still deterministic).
- **The degree unit moves from M1b to M2.** Transposition steps and step
  offsets in scale degrees need the effective scale and the quantize stage,
  which M2 builds. M1b's gate covers X1 and ST1's semitone parts; M2's covers
  the degree parts. The lead made this move when starting M1b; the human
  approved it here, after the fact (critic M3: a gate changed without a record).
- **`./verify`'s structure list** gains `core/include/arp/pattern.h`,
  `core/include/arp/bank_data.h`, `tools/gen_bank.py` and
  `tests/core/test_m1b.cpp` (an edit to `./verify`, a human gate).

`spec/MANIFEST` is re-pinned in the same change, as D-023 requires.

## D-026 — M2a review: swing is latched per step pair (E6); TM2's gap rule counts grid positions (E7) (RATIFIED by poll, 2026-10-23)

The independent critic review of M2a found no HIGH. It found that a swing
change mid-run could let a ratcheted step's last sub-notes sound after the
next step's on (and retrigger it on the same key): each step's span is
frozen at its evaluation with the swing of that moment, while the next
step's on is computed later, possibly with a different swing. Decided by
poll:
- **E6 (§5.1, §9):** swing is **latched per step pair**. The even step reads
  it; its odd partner reuses it if the even step was the last one evaluated.
  The odd step's span ends at an even on, which swing never moves, so no
  step can spill into the next. Cost: a swing move applies up to one step
  later. Rejected: dropping a step's not-yet-started ratchets at the next
  step's on (immediate, but notes vanish when the knob moves); accepting the
  overlap (an audible retrigger at every swing automation point).
- **E7 (§2.6, row TM2):** "no gap longer than `max(L_old, L_new)`" is about
  grid positions before swing; with swing above 500 an on-to-on gap is
  longer by the swing delay. Wording only, no behaviour change.

`spec/MANIFEST` is re-pinned in the same change, as D-023 requires. The lab's
CORE follows E6 too (its smoke enforces it).

## D-027 — M2b review: chord dedupe by key (E8), repeats run out after release (E9), spilled ratchets dropped in time mode (E10) (RATIFIED by poll, 2026-10-23)

The independent critic review of M2b found no HIGH. Three points needed the
human; decided by poll:
- **E8, chord dedupe by key alone.** §14 (r4 M2) keys the chord set by key;
  the CH1 row said (channel, key). The code follows §14, and the row now
  matches. Rejected: (channel, key), which would sound two copies of one
  pitch from sources on different channels.
- **E9, repeats run out after release.** With no keys held the cycle length
  `C` had no period, and the code paused the count, so under restart bar or
  free an arp let go early fired its trigger-only steps until the next
  chord. Now the last non-empty `C` is kept and the count runs out, as §4.8's
  "then the arp stops until the next gesture start" intends. Rejected:
  pausing the count (triggers without end).
- **E10, spilled ratchets in time mode.** D-026's latch fixed swing, but a
  `step_time_us` change can still bring the next step's on before the
  previous step's pending sub-notes. Those are dropped at the next step's
  evaluation (none has started, so no terminal is owed). "At or after"
  includes a sub-note exactly at the on: in time mode it yields to the step
  at that sample, ahead of §1.1's pending-first order (found while building
  E10; it is the approved rule's literal reach, not a new choice). Rejected: accepting
  the retrigger; delaying the step-time change to the end of the current
  step (it would undo the immediate sweep from slow rates, D-016).

The lead also settled, as the literal reading needing no erratum: the swing
pair's "evaluated" is §2.5's (clock running and an anchor exists), so an
unanchored step does not latch swing, while a spent step (§4.8, a silent
rest with an anchor) does (critic L1). And G4's skip of
chord-stage NoteOn ticks (timed by input, not the grid) is replaced by an
exact check that each chord on lands on its input's sample, so no gate is
narrowed (critic L3).

`spec/MANIFEST` is re-pinned in the same change, as D-023 requires.

## D-028 — M2 re-check: a tapped chord sets its own repeat length (E11); a used-up arp stays silent through loops and Panic with no keys held (E12); the lab's CORE catches up (RATIFIED by poll, 2026-10-23)

A focused critic re-check of the M2 follow-ups (2b438f4) found no HIGH and
two repeat cases nobody had chosen, both about trigger-only steps:
- **E11:** a chord tapped and released before the next step never set the
  cycle length `C` (it was computed only at a step with keys held), so the
  first such tap fired trigger steps without end and later taps borrowed the
  previous chord's length. Chosen: `C` is set the moment H′ becomes
  non-empty, so any press, however short, has its own repeats. Rejected:
  ignoring a quick tap (before the first real chord, triggers would fire
  freely).
- **E12:** after the arp ran out with no keys held, every loop wrap, jump or
  Panic cleared `exhausted` and fired another full round. Chosen: T6 and T7
  clear it only while H′ is non-empty; with no keys held the arp stays silent
  until the player plays again. A loop or jump with keys held still starts a
  fresh round. Rejected: another round on every loop pass.
  Asked as a follow-up and chosen the same way: with no keys held, T6 and T7
  also leave `repeatSteps` alone, so a round in progress finishes and stops
  rather than restarting (rejected: one more full round).

The re-check also noted that the lab's CORE had not taken four smaller M2b
fixes, against §Domain ("its CORE must keep matching the spec"). The lead
ports them rather than recording an exception: no human exception is needed
to follow the charter.

`spec/MANIFEST` is re-pinned in the same change, as D-023 requires.
