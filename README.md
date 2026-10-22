# Arp

horde's source of generated notes. For horde 1.0 it is a simple arpeggiator: it takes the notes the player holds and
plays them as a pattern in time. After 1.0 it grows into step sequencing and generative MIDI. It is a C++ note-processor
library that horde runs ahead of its engine, plus a browser lab for playing it. "Arp" is a working name; the
user-facing name is cleared before release.

**Status: Phase A built (2026-10-21/22), awaiting your playing and ratification.** The spec (draft r2.7) is written and
twice independently reviewed, and the lab plays all of it.
To play it:

```bash
python3 -m http.server 59531 --bind 127.0.0.1
```

Run that from the repo root, then open http://localhost:59531 (it goes straight to the lab), or use the Browser pane's "lab" preview.
The C++ core is next (M1). horde's module cutoff is **2026-12-11**. Phase state lives
in [`ROADMAP.md`](ROADMAP.md).

## What it will do (1.0)

- **Order styles (16):** up, down, two up-downs (ends once or twice), as played, chord, random, shuffle (random without
  back-to-back repeats), converge, diverge, two pedal-tone styles, and four that mix chords and notes (chord then walk,
  stride, chord between notes, pairs). Random draws are seeded, so they repeat.
- **Build your own:** up to 16 steps, each choosing which held note(s) play (the next of the order, 1st–8th, lowest,
  highest, or the chord) plus its rhythm. Saved with the preset.
- **Rate:** tempo-synced by horde's division names, with dotted and triplet values.
- **Shape:** transposition steps (the pattern repeats shifted, by semitones or scale degrees, like Ableton's), gate length (past 100% for legato), swing, and velocity (as played, fixed, or accented by
  step).
- **Holding:** latch, either replacing or adding to the held notes. The sustain pedal holds the arp's input notes.
- **Scale:** quantize to horde's global scale or Arp's own, in horde's scale format (a root plus twelve degrees): snap
  to the scale, or anchor its shape on the chord's lowest note. Ties follow the melody.
- **Restart:** a setting with three values: on a new chord, on the bar, or free-running.
- **Pattern bank:** a starter set of extra note-order styles and preset step patterns (rhythm, rests, accents, gate,
  octave jumps), with per-step probability, ratchets and trigger lanes for horde's performance events. More entries
  after 1.0. Every note Arp plays is tagged `arp` so horde can route it per oscillator.
- **Morph:** every setting morphs with horde's corners.

The output is sample-accurate and identical at any block size or sample rate, and every note it plays ends exactly
once. **Status:** every item above is specified (`spec/SPEC.md`) and playable in the lab; none is in C++ yet (M1). "As
landed" (replaying the timing of your presses) is held for after 1.0: `docs/deferred/landed.md`.

## The map

| Path | What it holds | Status |
|---|---|---|
| `SPINUP-BRIEF.md` | Provenance, scope, horde's contract needs, the survey's proposed answers | done |
| `project.manifest.json` | The survey answers (the machine-readable record) | ratified 2026-10-21 |
| `ROADMAP.md` | Phases, gates, queue, open questions: the single source of truth | live |
| `DECISIONS.md` | Why each settled choice was made (append-only) | live |
| `CLAUDE.md` | The agent charter: harness contract + Arp's domain invariants + knowledge loop | live |
| `INDEX.md`, `LIBRARY.md` | The knowledge loop's map and lessons | live |
| `intake/` | horde's packet: note vocabulary, engine note interface, quantizer, tempo divisions. Read-only, hash-pinned | done |
| `integrations/` | Inbound mailbox (`integrations/<sender>/`) and the outbound thread table | live |
| `docs/filings/` | Working copies of what Arp filed in other repos | live |
| `docs/prior-art.md` | Phase 0: pattern-bank landscape, failure modes, grid math, name check (dated, cited) | done |
| `spec/` | The 1.0 spec (SPEC.md), every setting (params.json), the candidate step-pattern bank | draft r2.7, awaiting ratification |
| `lab/arp-lab.html` | The playable twin: Ableton-style computer keys, MIDI, transport, every setting, morph A/B, a small synth, a **pattern view** (one full cycle as the core plays it, plus a live roll), an **order × pattern** thumbnail matrix, and a **build-your-own** step editor | built; play it to settle the open listening questions in ROADMAP (how settings switch during a morph, restart under morph, which patterns ship, pre-roll, tie memory, which pass a pick takes) |
| `tools/lab_smoke.mjs` | Headless checks of the lab core against the spec's invariants, run by `./verify fast` | live |
| `docs/spec-review-r1.md`, `docs/spec-review-r2.md` | The two independent critic reviews of the spec, and where each finding went | done |
| `docs/deferred/` | Designs held for after 1.0 (`landed.md`) | live |
| `index.html` | Root landing page: redirects to the lab | done |
| `core/` | C++ core | not yet built (M1) |
| `verify` | The oracle: `./verify fast` / `full` / `report` | live |

## Neighbours

- **horde (HYPERSAW)** consumes Arp. Arp files to horde's `integrations/arp/`; horde files to Arp's `integrations/hypersaw/`.
- **FOUNDATIONS** owns the note vocabulary (`NoteRef`, `HeldStack`). A brief is filed when the C++ first touches it.
- **autonomous** provides the harness kit and the ecosystem tracks.

## Run the oracle

```bash
./verify fast
```

_Last verified: 2026-10-22 (session close: `./verify fast` green on main 7b4d857; this README's map, statuses and counts checked against the tree)._
