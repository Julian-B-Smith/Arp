# Spin-up brief: Arp (note generation), a new project in synthetic-worlds

**Provenance.** Written by the horde (HYPERSAW) lead on 2026-10-21, at the human's request: "I would like to start
developing the arpeggiator so it's ready for 1.0. Maybe that should get its own repo and agent." By poll the same day
the human chose the working name **Arp**, a public repository, and the scope **"Note generation home"**: the simple
arpeggiator for horde 1.0, then the step sequencer and generators after 1.0, in one project.

Motivating records in horde: ADR-190 A5 (a simple arpeggiator and the shared quantizer are in 1.0; complex patterns,
step sequencers and generators after); ROADMAP B391 (the quantizer and note generation), B431 (musician basics: the
sustain pedal), B92 (the note transport), B260 (the global scale), ADR-254 (the FX device after 1.0), ADR-257 A1 (the
note context). horde was at commit 900963c when this directory was made.

## What Arp is for

horde's source of generated notes. For 1.0, a **simple arpeggiator**: it takes the notes the player holds and plays them
as a pattern in time. After 1.0 the same project grows into step sequencing and generative MIDI (horde's `seqgen`).

**Read as a first scope list for the survey to confirm (the 1.0 arpeggiator):**
- **Modes:** up, down, up-down (with and without repeated ends), as played, chord, and random (seeded, so it repeats).
- **Rate:** tempo-synced divisions by horde's names (`intake/tempo-divisions.md`), with dotted and triplet values; a
  free rate in Hz is a survey question.
- **Octave range** (1–4 or more), **gate length**, **swing**, and **latch / hold**.
- **Velocity:** as played, fixed, or accented by step.
- **The scale:** optional quantize of the output to horde's global scale (`intake/quantizer.md`).
- **Sync:** to host tempo and transport; what happens when the transport stops or jumps, and on loop.

## What horde needs from Arp (the contract)

- **The note vocabulary** (`intake/note-vocabulary.md`): `NoteRef` identity, exactly one terminal per note, and
  canonical base pitch. Every emitted note ends exactly once on every path: pattern change, held-chord change, switch
  off, transport stop, panic.
- **Determinism:** the same input notes, tempo and seed give the same output. Seeded mulberry32 streams only; no wall
  clock. Tempo and transport arrive as data.
- **Real time:** nothing allocates, locks or makes a system call on the audio thread; buffers are sized at prepare.
- **Sample-accurate output:** each emitted event carries its sample offset in the block; the arp's grid is computed
  from the transport, never from block boundaries.
- **The engine downstream:** what horde's engine accepts is in `intake/engine-note-interface.md` (IF-1, IF-2, IF-11,
  VO-6, VO-12: notes by id, the sustain pedal). The sustain pedal and legato are engine behaviour; Arp decides what the
  pedal means for a held pattern (a survey question).
- **No engine assumed:** in horde's FX device after 1.0, Arp may run on the MIDI sidechain's notes, so it is a pure
  note processor with no dependency on horde's engine.
- **Undo, presets and macros:** Arp's settings are part of a horde preset, every control undoable; whether Arp is a
  morph target is a survey question.
- **Schedule:** horde's module cutoff is **2026-12-11**. Arp's 1.0 arpeggiator meets its MUST rows by then to ship in 1.0.
- **The name:** "Arp" is a working name; any user-visible name goes through horde's B433 clearance before 1.0.

## Your first act: the spin-up survey, not code

Run the autonomous kit's spin-up survey (`/spinup`; ONBOARDING.md Part 2), commit `project.manifest.json`, then scaffold
from it. The nine questions, with the lead's PROPOSED answers for the human to keep or strike:

1. **What is it?** A note-generation library (the arpeggiator first) that horde consumes, with a lab twin for playing it.
2. **Architecture rung:** one thread plus a read-only verifier. A fleet is not earned.
3. **Domain core:** a pure note processor (notes in, notes out, transport and tempo as data, a seeded RNG), framework-free,
   with the UI and host in thin adapters.
4. **Oracle shape:** invariants first:
   - every note on gets exactly one terminal on every path;
   - the timing grid is sample-exact against the transport;
   - the output is identical across block sizes and sample rates;
   - the same seed gives the same output;
   - quantize keeps every note in the scale;
   - must-fail controls throughout.
   Goldens are pinned per pattern once the C++ exists.
5. **A second implementation?** The lab is a listening twin, not a parity target. The C++ core is the reference.
6. **Consumers:**
   - **Provider to HYPERSAW (horde).** Inbound mailbox `integrations/<sender>/` here; filings to horde go in horde's tree
     under `integrations/arp/`, by a PR touching only that path.
   - **Consumer of FOUNDATIONS**' note vocabulary (`NoteRef`, `HeldStack`) if it fits.
7. **A parallel audit thread?** No.
8. **Knowledge-loop tags:** note lifecycle, timing and transport, determinism, sample-rate and block-size independence,
   held-note logic.
9. **Lifespan and autonomy:** long-lived (it grows into sequencing and generators after 1.0). Medium autonomy:
   PR-per-batch, the human merges.

## Open for the human, at the survey

- The 1.0 mode list, and whether "random" is in.
- A free rate in Hz as well as synced divisions.
- What the sustain pedal does to a latched or held pattern.
- Restarting the pattern: on each new chord, on the bar, or free-running.
- Whether Arp's settings morph with horde's corners or stay global.
- Where the step sequencer begins after 1.0: its own lab, or an extension of the arp's pattern grid.
