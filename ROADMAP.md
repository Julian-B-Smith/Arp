# ROADMAP — Arp

Single source of truth. Only the lead session (or the human) edits this file.
State lives here; conversations are ephemeral. The *why* behind ratified choices
lives in `DECISIONS.md`; horde's terms are in `intake/` and `SPINUP-BRIEF.md`.

## Status

- **Phase:** **Phase A DONE 2026-10-23**: the spec is RATIFIED (D-022) and protected, and functionality is frozen (D-018).
  M1a (T-003) merged; **M1b (T-004) gate met 2026-10-23, in its PR**; then M2, M3 (ROADMAP §Phases).
- **Oracle:** `./verify fast` = kit integrity, leak gate, private-name gate,
  structure, manifest, intake pin, spec pin, the **lab smoke** (SPEC §11, each
  check proven on a planted fault) and the **C++ core suite** on both int128 paths
  (189 checks each after M1b) plus the params and bank drift checks. `full` = `fast` for now.
- **Functionality freeze (the human, 2026-10-22, D-018): IN EFFECT** once the repeats and chord-stage PR merges. 1.0
  scope is frozen; any further feature needs an explicit unfreeze decision. Next: play the lab, ratify the spec, M1.
- **Hard date:** horde's module cutoff **2026-12-11**. The 1.0 arpeggiator
  meets horde's MUST rows by then, or it ships after 1.0. The rows are horde's
  `docs/proposals/note-processor-1.0-bar.md` (answered 2026-10-22, D-015).
- **Last human ratification:** 2026-10-21 — manifest ratified by poll (D-003);
  scope widened by poll twice the same day: the pattern bank (D-004), and
  four Phase 0 additions (D-006).
- **Cutoff risk:** the 1.0 surface has grown three times since ratification
  (D-004, D-006, D-007, D-011), then time mode and the pattern library (D-016),
  which are not yet placed in the yield order (ask at the next gate). If the cutoff tightens, **ratchets yield
  first, then the user pattern, then probability, then triggers**. All are
  pattern data that can ship after 1.0, because `spec/bank/` reserves their
  fields now.

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

### Phase A — Spec + lab twin — DONE 2026-10-23 (D-022)
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
  - **latch's two behaviours** (replace / append) in the held-set state machine;
  - **gate above 100 %** and the same-key overlap rule (off-then-on at one
    sample, forced terminal at the next step);
  - **the tick grid** (15,360 per quarter, the half-open block window, the
    block-start conversion rule, ratchet sub-steps, swing parity from the
    absolute step);
  - the random draw (seed, absolute step, lane), used by random modes and
    per-step probability;
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

### M1a — C++ core: the skeleton and the seam — gate met 2026-10-23 (T-003)
The C++20 core proves the hard parts first, on the smallest feature set. It is
the RATIFIED `spec/SPEC.md` made real, judged by its invariants; the lab is
not a parity target.
- **`core/`:** a CMake library, self-contained and framework-free, buildable
  without horde (horde copies it in at our V1, D-015). C++20, standard
  library only, no allocation after `prepare`. The NoteRef-compatible
  identity type is a local placeholder behind the FOUNDATIONS surface until
  that brief resolves (survey Q6).
- **Scope:**
  - the public API (`prepare` / `process` / `reset`, a settings struct
    mirroring `spec/params.json`, events with output refs and stream tags);
  - exact 128-bit positions, with a **portable** int128 (a fast path where
    the compiler has `__int128`, and a two-word fallback, because horde may
    build with MSVC);
  - the three-band host clock (§2.2), the free clock (§2.4), the once-guard
    and pending queue (§4.1, §5.1);
  - the single release path, event caps and the off-reserve (§1.3, §7);
  - the held set: latch replace/append, the pedal, gesture start (§3);
  - order `up` with the plain one-step pattern;
  - gate up to 200 %, T2 within a stream, and the §7 rows that apply.
- **Tests:** a C++ test runner. Plants come from a **test-only** compile flag
  (`ARP_PLANTS`), so the shipped library contains none.
  - The checks: N1 over the applicable T-rows, G1 (block sizes 1…4096), G2,
    G4 (44.1/48/96/192 kHz), G5 (re-base jitter), D1, and A1 (no
    allocation in `process`, `reset` or settings changes).
  - The int128 fallback passes the same tests.
- **Gate:**
  - `./verify fast` builds and runs the suite (CI too), green;
  - every planted control is proven to fire, recorded in a trace;
  - an independent critic review has no open HIGH findings.

### M1b — C++ core: every order style, pattern field and clock (T-004) — gate met 2026-10-23
- **First, the M1a critic's two LOWs** (re-check, 2026-10-23):
  - **LOW-A:** after a `prepare()` that leaves owed offs, start the output id counter above the largest owed id,
    so an owed off and a new note never share a ref (`intake/note-vocabulary.md` §1).
  - **LOW-B:** widen the fast-math guard to `__FINITE_MATH_ONLY__`, or test exponent bits, so `-ffinite-math-only`
    can't optimize away the finiteness checks.
- **Scope:**
  - all 16 order styles (§4.2), including the chord + note styles and
    `shuffle` without back-to-back repeats;
  - every step-pattern field (§4.3): tie look-ahead, ratchets, probability,
    triggers, picks, and `step` × the pattern step size;
  - transposition steps and step offsets in **semitones** (§4.7, §4.3). The **degree** unit needs the effective
    scale and the quantize stage, so it moves to M2 (approved by the human, D-025);
  - the user pattern (§4.6), triple-buffered, storage from construction;
  - time mode (§2.6), with the smooth step-time sweep.
- **Gate:** R1, U1, X1 and ST1 (their semitone parts), TM1–TM3, K1, T1, P1, N2, S1 and the order-style tables,
  each with its plant; the M1a suite still green; a critic review with no open HIGH.

### M2 — C++ core: scale, feel, restart, morph, chord stage (T-005)
- **Scope:**
  - quantize: global or own scale, `scale` or `chord_root`, the `lastQ`
    tie chain and its resets (§6);
  - the **degree** unit for transposition and step offsets, and the full six-stage offset order (§4.7);
  - swing and velocity;
  - restart and repeats (§4.5, §4.8);
  - the morph timing of discrete and continuous settings (§9);
  - the chord stage with its own stream and sounding table (§14).
- **Gate:** Q1–Q5, G3, RP1, CH1, X1 and ST1 (their degree parts) and the remaining §11 rows, each with its
  plant; every M1 check still green; a critic review with no open HIGH.

### Cross-check (Layer-E, measured, never a gate) (T-006)
`./verify full` runs a shared set of scripted sessions through the lab's JS
core and the C++ core and reports any difference in the event streams. It
is measured, never gating: the lab is a listening twin, not a parity target.
A difference is investigated against the spec, which wins.

### M3 — horde admission
- The horde-facing surface: parameter table, presets, undo of every control,
  morph targets; horde's copy-in of the core at our V1, with a file list (D-015); the FOUNDATIONS
  note-vocabulary brief when the C++ first touches NoteRef/HeldStack; the
  rows of horde's note-processor 1.0 bar (D-015); CPU in Release, reported (never a CI gate), and
  evidenced against horde's ≤ 2 % budget.
- Goldens cut per pattern/preset (a human gate), then protected.
- **Gate:**
  - every MUST row of horde's `docs/proposals/note-processor-1.0-bar.md` is evidenced, CPU ≤ 2 % of a min-spec
    core included (D-015);
  - a file list for horde's copy-in is shipped;
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
- **`landed`** — replay the order AND timing of the presses (D-012, deferred by D-014); the full
  design and its lab lessons are in `docs/deferred/landed.md`.

## Queue

### T-001 — Phase 0 prior-art swarm
- **Status:** done 2026-10-21 (`docs/prior-art.md`; D-005, D-006)
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

### T-003 — M1a: the C++ skeleton and the seam
- **Status:** gate met 2026-10-23: the suite is in `./verify fast`, every plant fires, and the critic re-check found **no open HIGH**. In the M1a PR; CI is the first g++ build.
- **Scope:** `core/`, `tests/core/` (the C++ test runner), the `./verify` wiring (approved by poll, 2026-10-23).
- **Acceptance criteria:** the M1a gate above.
- **Out of scope:** everything listed for M1b and M2.

### T-004 — M1b: every order style, pattern field and clock
- **Status:** gate met 2026-10-23: 189 checks per int128 path in `./verify fast`, every plant fires, and the independent critic review found **no HIGH**; its MEDs settled by the human (D-025: errata E4, E5, the degree unit to M2) and its LOWs fixed. Trace: `traces/2026-10-23-m1b-styles-patterns.md`. In the M1b PR.
- **Scope:** `core/`, `tests/core/`, the trace.
- **Acceptance criteria:** the M1b gate above.
- **Out of scope:** everything listed for M2.

## Open questions (blocking — ask the human or horde)

- **Q-H1 — which MUST rows bind Arp: ANSWERED by horde (D-015).** horde's `docs/proposals/note-processor-1.0-bar.md` (incl. CPU ≤ 2 % of a min-spec core); it is M3's checklist.
- **Q-H2 — how horde consumes the core: ANSWERED by horde (D-015).** copied into horde's tree at our V1 by a horde PR recording our commit; keep the core self-contained, ship a file list.
- **Q-H3 — the trigger-lane handoff: ANSWERED by horde (D-015).** `Trigger{lane 1–8, sampleOffset}` as proposed; horde shows "Arp · T1–T8" event sources.
- **Q-H6 — How a horde preset carries Arp's user pattern** (D-011): a small
  structured blob (up to 16 steps × 9 fields), not host parameters. **Read
  horde ADR-169 first**: module presets "embed by value", and preset identity
  per corner may already answer this (D-013). Then ask in the next notice.
  Blocks the M3 preset wiring only.
- **Q-H7 — A third stream, `chord`, in horde's per-source selector** (D-018). Filed 2026-10-22 as horde PR #1194
  (seq 5); ball HYPERSAW, respond-by 2026-11-13.
- **Q-H5 — Stream-scoped pedal** (D-009). We proposed that horde's VO-12
  sustain `direct` voices only. The ball is horde's, in our
  `integrations/hypersaw/response-note-stream.md`. It blocks nothing in the
  core: Arp's side (the `arp` tag, consuming CC 64) is specified and tested.
- **Q-A5 — Whether pre-roll plays: SETTLED 2026-10-23 by poll (D-021).**
- **Q-A1 — The morph threshold rule for discrete settings: SETTLED 2026-10-23 by poll (D-021).**
- **Q-A2 — Restart × morph: SETTLED 2026-10-23 by poll (D-021).**
- **Q-A3 — The starter bank's entries: SETTLED 2026-10-23 by poll (D-021).**
- **Q-A4 — Phase 0 extras:** CLOSED 2026-10-21 by poll (D-006). All four
  are in 1.0.
- **Q-P1 — Where the step sequencer begins** (an extension of the arp's grid,
  or its own lab/module here). Decided at the post-1.0 gate (survey Open 6).

## Deferred / demoted

- **Bank growth beyond the starter set:** after 1.0, as data on the bank
  format (D-004 supersedes the survey's "extra modes after 1.0").
- **Standalone plugin:** no (survey Q1); any change is a later, logged decision.

## Graduation criteria

Arp graduates from interactive design to autonomous queue work when the
remaining open questions are infrastructure, not playing. Still in the
judgment column: Q-A1, Q-A2, Q-A3 (the starter bank), the lab's feel.
