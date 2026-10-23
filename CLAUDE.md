# Agent Charter — Arp

Everything above §Domain is the invariant harness layer. Do not edit it
per-project. Project-specific facts live in §Domain and in ROADMAP.md.
**The global doctrine (imported via `~/.claude/CLAUDE.md`) applies on top of
this charter and is not restated here** — this file carries only what doctrine
doesn't: the operational contract of THIS harness. (Context budget: slimmed
2026-07-16, Decision 28.)

## Truth contract

- **ROADMAP.md is the single source of truth.** Task state, acceptance
  criteria, invariants, and open questions live there and only there. If the
  conversation and ROADMAP.md disagree, ROADMAP.md wins; if ROADMAP.md is
  wrong, fixing it is the first task.
- **Passing ≠ done.** Done = `./verify full` green AND the ROADMAP acceptance
  criteria satisfied AND a trace entry written in `traces/`. Never collapse
  these into each other.
- **Grounded refusal is a success class.** "I cannot do this within the brief
  because X" with evidence is a correct output. Guessing to appear productive
  is a failure.

## Provenance

- Every nontrivial claim about the codebase must cite its evidence: a file
  path and line, a verify run, or a ROADMAP entry. No provenance → phrase it
  as a hypothesis, not a fact.
- Every merged change gets an entry in `traces/` (see the provenance skill):
  what changed, why, evidence consulted, verify result + git hash.

## Delegation policy (lead session)

- The lead plans, delegates, integrates, and is the **only** writer of
  ROADMAP.md. Subagents never touch it.
- Delegation briefs are self-contained: subagents start with zero conversation
  history. Every brief states (1) files in scope, (2) acceptance criteria
  copied verbatim from ROADMAP.md, (3) the verify target, (4) what is
  explicitly out of scope.
- Use built-in Explore for codebase reconnaissance. Use `implementer` for
  scoped changes, `verifier` for oracle runs, `critic` (Opus) for adversarial
  review of anything architectural, irreversible, or touching an invariant.
- One queue item per implementer dispatch. Parallel dispatches only for items
  with disjoint file scopes.
- Do not start work on an item whose acceptance criteria are missing or
  ambiguous. Surface the gap to the human; that is the deliverable.

## Oracle discipline

- Run `./verify fast` after any change set; `./verify full` before declaring
  a queue item done. Report oracle output verbatim — never summarize a failure
  into vagueness.
- A red oracle halts forward work. Fix or revert; do not stack changes on red.
- Never weaken a gate (skip a test, relax a threshold, mark xfail) without an
  explicit human decision recorded in ROADMAP.md.

## Human gates

Stop and ask before: deleting files, changing the public interface of
anything, editing `./verify` or the gates it runs, adding a dependency,
any git operation beyond add/commit on the working branch, and anything §Domain
lists as protected.

---


## §Domain — Arp

**What this is.** Arp, horde's source of generated notes: a C++ note-processor
library that HYPERSAW's horde runs ahead of its engine on the instrument's note
input, plus a browser lab twin for playing and listening (`lab/`, built). For horde 1.0 (module cutoff **2026-12-11**): a simple arpeggiator.
After 1.0: step sequencing and generative MIDI, here. Not a plugin of its own
(survey Q1). Provenance and horde's contract needs: `SPINUP-BRIEF.md`; the
terms: `intake/` (note vocabulary, engine rows IF-1/IF-2/IF-11/VO-6/VO-12, the
quantizer shape, the tempo-division names).

**Stack & entrypoints.** Not yet built (see ROADMAP). Planned: C++20 core in
`core/` (CMake `-G "Unix Makefiles"`, ABSOLUTE build paths, Release for any
perf number — global CLAUDE.md audio section); single-file lab in `lab/`, run
headless by plain Node oracles in `tools/`. Oracle: `./verify fast|full`.

**Domain invariants** (the critic checks against these):
- **One terminal per note.** Every note Arp emits ends exactly once on every
  path: step end (gate), pattern/mode change, held-chord change, latch or pedal
  release, switch-off, transport stop/jump/loop, panic, a morph-driven discrete
  switch, reset. Identity is `NoteRef`-compatible (host note id, else
  port/channel/key) — never a bare key (`intake/note-vocabulary.md`).
- **The grid comes from the transport**, never from block boundaries; every
  emitted event carries its sample offset. Output is identical across block
  sizes (1…4096) and sample rates. Restart rules choose which step plays next,
  never when.
- **Deterministic:** seeded mulberry32 streams only; no `Math.random`, no
  wall-clock read. Tempo and transport arrive as data. Same input + params +
  seed → identical event stream.
- **Real time:** nothing allocates, locks or makes a system call on the audio
  thread; held-note and output buffers are sized at prepare.
- **Pure note processor:** no engine assumed downstream (horde's FX device may
  run Arp on MIDI-sidechain notes after 1.0). Arp emits base pitches only, never
  a baked bend. The sustain pedal holds Arp's INPUT; Arp consumes CC 64 rather
  than forwarding it (D-000 Open 3).
- **Scale (D-010):** horde's global scale is read, never owned. Arp may also
  hold its **own** scale in exactly horde's shape (root + root-relative 12-bit
  mask). Quantized output is always in the **effective** scale (source, mode,
  anchor).
- **The pattern bank is data** (D-004): order styles and step patterns load
  from the bank format, and every entry passes the full invariant suite.
  Clean-room: generic algorithms under our own names; nothing copied from
  another arp's names, presets or UI text.
- Tempo divisions are stored **by name** from horde's list
  (`intake/tempo-divisions.md`); nearest division by ratio, not by difference.
- Every detector has a must-fail planted control before it gates anything.
  Goldens are pinned only once the C++ exists. The lab is a listening twin,
  never a parity target. CPU is measured in Release, never gated.

**Alias note.** This repo is PUBLIC. Private siblings are named by alias in
every tracked file; the map lives in untracked `PRIVATE-NOTES.md`, enforced by
`private_name_gate` from untracked `.leakcheck-names`.

**Protected paths** (human gate to modify): `./verify` and `.kit/`;
`project.manifest.json`; `intake/` (horde's record, sha256-pinned by
`intake_gate` — annotate in docs/, never edit); **`spec/` (RATIFIED 2026-10-23, D-022)**;
goldens once cut. `lab/` stays editable, but its CORE must keep matching the spec (the smoke enforces it).

**Verify targets.** `fast` (~1 s): kit integrity, leak gate, private-name gate,
structure, manifest, intake pin. `full`: = fast until the C++ core lands; then
invariant tests, goldens, sweeps. CI runs `fast` on Linux.

---

<!-- KNOWLEDGE-LOOP:START -->
## Self-Improving Knowledge Loop

Each session: read accumulated knowledge before acting, write distilled knowledge
after. This meta-layer sits on top of my primary role and never overrides it.

### Every session
1. **ORIENT** — Read INDEX.md in full (kept small on purpose). Pull ONLY the matching
   entries from LIBRARY.md into context. Never load all of LIBRARY by default.
2. **ACT** — Do the work, applying retrieved lessons. If a lesson proves wrong,
   correcting it outranks adding a new one.
3. **REFLECT** — Ask: "What did I learn that a future session needs and could not
   cheaply re-derive?" A lesson qualifies only if durable, evidenced (tied to a
   concrete trigger), and non-obvious. If nothing qualifies, write nothing.
4. **WRITE (atomic)** — Append the lesson to LIBRARY.md and a one-line pointer to
   INDEX.md in the same change. New lessons enter as `tier: candidate`; promote to
   `canonical` only when a second occurrence is SHOWN independent — a different
   root cause, not the same shared file, prompt, or tool seen twice; say why in
   the promotion note — or on human review. Recurrence alone never promotes.

### Write gate (anti-poisoning)
This loop feeds its own output back as input, so a wrong lesson, written once, is
retrieved and reinforced forever. Therefore: prefer not writing over writing
unverified; every lesson states what would falsify it; if a retrieved lesson
contradicts present evidence, trust the evidence and demote the lesson.

### Consolidation (periodic)
When LIBRARY exceeds ~30 entries, merge duplicates, delete superseded entries,
promote recurring candidates, tighten tags. Refactor it like code; don't grow it
like a log.

### LIBRARY entry template
`[Lxxxx] <title> | tier | added: YYYY-MM-DD | tags: … | lesson: … | evidence: … | falsifier: … | supersedes: …`
<!-- KNOWLEDGE-LOOP:END -->

**Tags here:** note-lifecycle · timing-transport · determinism ·
sr-blocksize-independence · held-note-logic (survey Q8). Candidates inline;
reflection enforced at session close by `/breakdown` (D-001).
