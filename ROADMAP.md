# ROADMAP — Arp

Single source of truth. Only the lead session (or the human) edits this file.
State lives here; conversations are ephemeral. The *why* behind ratified choices
lives in `DECISIONS.md`; horde's terms are in `intake/` and `SPINUP-BRIEF.md`.

## Status

- **Phase:** spun up 2026-10-21. Phase 0 (prior art) is next. Nothing is built.
- **Oracle:** `./verify fast` = kit integrity, leak gate, private-name gate,
  structure, manifest, intake pin. No note gates yet: they arrive with the lab
  (Phase A) and the C++ core (M1). `full` = `fast` for now.
- **Hard date:** horde's module cutoff **2026-12-11**. The 1.0 arpeggiator
  meets horde's MUST rows by then, or it ships after 1.0. Which rows bind Arp is
  not in the intake packet yet (Q-H1).
- **Last human ratification:** 2026-10-21 — manifest ratified by poll (D-003).

## Phases (each gate is a checkable condition)

### Phase 0 — Prior-art landscape (agent swarm)
Arpeggiator and step-sequencer design: published literature and the public
feature sets of hardware and software arps (mode sets, latch and hold, pedal
handling, restart and retrigger rules, swing, gate, velocity accent); known
failure modes (stuck notes on transport stop, double note-offs, block-boundary
jitter); **pattern banks** (order-style taxonomies and step-pattern banks,
D-004), including any screenshots the human shares as inspiration; and a first
name-collision check. "Arp" is generic and certainly
collides, so the check feeds horde's B433 clearance rather than clearing it.
Clean-room: public documentation and listening only; never borrow another arp's
mode names, presets or UI text.
- **Gate:** `docs/prior-art.md` exists, dated and cited; DECISIONS records what
  it changes (reuse / avoid / differentiate). Name findings go to the human.

### Phase A — Spec + lab twin
- `spec/`: the scope list (manifest `scope_1_0`) and the parameter manifest:
  every key, unit, range, default, and its morph behaviour (continuous blends;
  discrete switches at a threshold). Also:
  - **the terminal-path table**: every way an emitted note ends, one row per
    path, each row a test in M1;
  - **the held-set state machine**: keys, latch, the sustain pedal as input
    hold, and how they compose;
  - **the three restart rules** as one parameter (chord-from-silence / bar /
    free-running), each with its invariants;
  - **the morph rule for discrete settings**: where the threshold sits, when a
    switch lands relative to the grid, and that no switch strands a note
    without its terminal;
  - **the pattern bank format** (D-004): order styles as generic algorithms and
    step patterns as data (rest/tie, accent, gate, octave jump per step); how a
    step pattern combines with an order style; the starter bank's entries (Q-A3);
  - the random mode's seed and stream rules;
  - the quantize rule (nearest in-scale pitch, and the tie-break);
  - the transport rules (stop, jump, loop, tempo change).
- `lab/`: a single-file browser listening twin (not a parity target), seeded
  and wall-clock-free, with an on-screen keyboard or MIDI in. It runs headless
  under a Node smoke in `tools/`.
- **Gate:** the human ratifies `spec/` (it is then protected); `./verify fast`
  runs the lab smoke (seeded → identical event stream; every note on has
  exactly one terminal over a scripted session; output identical across block
  sizes), each detector with a planted control.

### M1 — C++ core: held set, modes, grid, terminals
- `core/`: a C++20 library. Buffers sized at prepare. The NoteRef-compatible
  identity type is a local placeholder behind the FOUNDATIONS surface until
  that brief resolves (survey Q6).
- Modes (up, down, up-down ± repeated ends, as played, chord, random/seeded),
  synced rate by division name, octave range, gate length, latch, the pedal as
  input hold.
- **Invariant suite**, each detector with a planted must-fail control:
  - one terminal per note across every row of the spec's terminal-path table;
  - the grid is sample-exact against the transport;
  - identical output at block sizes 1…4096 and at 44.1/48/96/192 kHz;
  - seed determinism;
  - no allocation in `process`, `setParam` or `reset`;
  - no output for silent input, no stuck note after any transport event.
- **Gate:** the suite runs in `./verify fast`, green; every planted control is
  proven to fire (recorded in a trace).

### M2 — Feel, scale, restart, morph
- Swing; velocity (as played / fixed / accented by step); quantize to the
  global scale; the restart parameter's three values; every setting a morph
  target, discrete ones switching per the Phase A rule.
- **The starter pattern bank** (D-004): the order styles and step patterns that
  Q-A3 chose, loaded from the bank format.
- **Gate:** each item's invariants are in the suite with planted controls:
  - quantize keeps every note in the scale;
  - swing displaces only the off-steps, by the spec'd fraction, sample-exact;
  - each restart value meets its own rule;
  - a full-range morph sweep in mid-pattern leaves no note without exactly one
    terminal;
  - every starter-bank entry passes the whole M1 suite, and a bank switch
    mid-note is a terminal-path row.

### M3 — horde admission
- The horde-facing surface: parameter table, presets, undo of every control,
  morph targets; the integration route horde chooses (Q-H2); the FOUNDATIONS
  note-vocabulary brief when the C++ first touches NoteRef/HeldStack; the
  MUST rows horde names for Arp (Q-H1); CPU in Release, reported, never gated.
- Goldens cut per pattern/preset (a human gate), then protected.
- **Gate:**
  - every MUST row horde names is evidenced;
  - an independent critic review (sourced separately from the author) has no
    open HIGH findings;
  - the human's playing sign-off is recorded here;
  - the readiness notice is filed in horde's `integrations/arp/`.

  **Due 2026-12-11.**

### Phase N (pre-ship) — Prior-art & IP re-scan + name clearance
- Re-run the swarm; a patent/IP landscape pass; horde's B433 knockout screen
  for the user-visible name.
- **Gate:** dated re-scan appended to `docs/prior-art.md`; conflicts surfaced to
  the human with a go/no-go; name cleared before any public release.

### After 1.0 — Step sequencing and generators
Scoped at the post-1.0 gate with the human: where the step sequencer begins
(Q-P1), the free rate in Hz, horde's FX-device use on MIDI-sidechain notes
(ADR-254), and the generators (`seqgen`).

## Queue

### T-001 — Phase 0 prior-art swarm
- **Status:** open
- **Scope:** `docs/prior-art.md`, DECISIONS
- **Acceptance criteria:** the Phase 0 gate above.
- **Out of scope:** any spec or code.

### T-002 — Spin-up filings
- **Status:** filed 2026-10-21 (horde PR #1179, autonomous PR #60), awaiting
  their residents' merges; then the balls are theirs (respond-by 11-13 / 11-04)
- **Scope:** horde's `integrations/arp/notice-spinup.md` (carries Q-H1, Q-H2);
  autonomous's `integrations/arp/brief.md` (ecosystem-track registration).
- **Acceptance criteria:** each is filed by a PR in the provider's repo,
  touching only that path, with the human's go-ahead; the outbound table in
  `integrations/README.md` is updated.
- **Out of scope:** the FOUNDATIONS brief (M3, when the C++ first touches
  NoteRef).

## Open questions (blocking — ask the human or horde)

- **Q-H1 — Which of horde's MUST rows bind Arp?** The intake packet carries
  Arp's note interface but no admission bar for note processors (Echo's came
  with `module-1.0-bar.md`). Asked of horde in the spin-up notice. Blocks the
  M3 gate.
- **Q-H2 — How horde consumes the core.** Vendored source pinned by hash, a
  submodule, or a package? Asked of horde in the spin-up notice. Blocks M3.
- **Q-A1 — The morph threshold rule for discrete settings.** Decided in
  Phase A by playing in the lab, then an ADR (survey Open 5).
- **Q-A2 — Restart × morph.** Does a morph that changes the restart value act
  at once or at the next restart point? Same venue as Q-A1.
- **Q-A3 — The starter bank's entries** (about 12–16 order styles and step
  patterns). Decided in Phase A from the Phase 0 landscape and by playing in
  the lab (D-004).
- **Q-P1 — Where the step sequencer begins** (an extension of the arp's grid,
  or its own lab/module here). Decided at the post-1.0 gate (survey Open 6).

## Deferred / demoted

- **Free rate in Hz:** after 1.0 (survey Open 2).
- **Bank growth beyond the starter set:** after 1.0, as data on the bank
  format (D-004 supersedes the survey's "extra modes after 1.0").
- **Standalone plugin:** no (survey Q1); any change is a later, logged decision.

## Graduation criteria

Arp graduates from interactive design to autonomous queue work when the
remaining open questions are infrastructure, not playing. Still in the
judgment column: Q-A1, Q-A2, Q-A3 (the starter bank), the lab's feel.
