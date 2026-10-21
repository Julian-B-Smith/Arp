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
