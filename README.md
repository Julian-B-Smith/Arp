# Arp

horde's source of generated notes. For horde 1.0 it is a simple arpeggiator: it takes the notes the player holds and
plays them as a pattern in time. After 1.0 it grows into step sequencing and generative MIDI. It is a C++ note-processor
library that horde runs ahead of its engine, plus a browser lab for playing it. "Arp" is a working name; the
user-facing name is cleared before release.

**Status: the 1.0 spec is RATIFIED (2026-10-23) and functionality is frozen.** The spec was four times independently
reviewed, and the lab plays all of it. The C++ core is under way: M1a (the skeleton), M1b (every order style, step field, the user pattern and time mode) and M2 (scale, feel, restart, repeats, morph timing, the chord stage) are built; M3 (horde admission) is under way: the evidence for horde's 1.0 bar, row by row, is in `docs/horde-1.0-bar.md`.
To play it:

```bash
python3 -m http.server 59531 --bind 127.0.0.1
```

Run that from the repo root, then open http://localhost:59531 (it goes straight to the lab), or use the Browser pane's "lab" preview.
horde's module cutoff is **2026-12-11**. Phase state lives
in [`ROADMAP.md`](ROADMAP.md).

## What it will do (1.0)

- **Order styles (16):** up, down, two up-downs (ends once or twice), as played, chord, random, shuffle (random without
  back-to-back repeats), converge, diverge, two pedal-tone styles, and four that mix chords and notes (chord then walk,
  stride, chord between notes, pairs). Random draws are seeded, so they repeat.
- **Build your own:** up to 16 steps, each choosing which held note(s) play (the next of the order, 1st–8th, lowest,
  highest, or the chord) plus its rhythm, with a per-step offset of `step × the pattern's step size` (semitones or scale
  degrees). A **library** saves patterns by name: in this repo (`lab/patterns/`) or in your browser, with JSON
  export/import.
- **Rate:** tempo-synced by horde's division names (dotted and triplet included), or **time mode**: a free step length
  shown as both ms and Hz, starting from your key press. Sweeping it speeds the arp up or down smoothly.
- **Shape:** transposition steps (the pattern repeats shifted, by semitones or scale degrees, like Ableton's), gate length (past 100% for legato), swing, and velocity (as played, fixed, or accented by
  step), with optional velocity and gate randomness on a shared curve.
- **Holding:** latch, either replacing or adding to the held notes. The sustain pedal holds the arp's input notes.
- **Scale:** quantize to horde's global scale or Arp's own, in horde's scale format (a root plus twelve degrees): snap
  to the scale, or anchor its shape on the chord's lowest note. Ties follow the melody.
- **Restart:** a setting with three values: on a new chord, on the bar, or free-running.
- **Repeats:** play the whole pattern N times per chord (0 = forever, up to 16), then wait for the next chord.
- **Chord stage:** before the arp, each played note gains up to six intervals (semitones or scale degrees). One
  **quantize** switch snaps everything Arp emits, the chord and the note you played included. The chord goes out as its own `chord` stream, so horde can send the chord to one oscillator and the arp to
  another. Chord notes follow your keys like held notes. With "arp on" off, Arp is a plain chord generator.
- **Pattern bank:** a starter set of extra note-order styles and preset step patterns (rhythm, rests, accents, gate,
  octave jumps), with per-step probability, ratchets and trigger lanes for horde's performance events. More entries
  after 1.0. Every note Arp plays is tagged `arp` so horde can route it per oscillator.
- **Morph:** every setting morphs with horde's corners.

The output is sample-accurate and identical at any block size or sample rate, and every note it plays ends exactly
once. **Status:** every item above is specified (`spec/SPEC.md`) and playable in the lab; all of it is in C++ too (morph is resolved by horde; the core applies each change where §9 says it lands). "As
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
| `docs/horde-1.0-bar.md`, `docs/horde-copy-in.md` | horde's 1.0 checklist mapped to evidence; the files horde copies, with hashes, build notes and the CPU envelope | live |
| `docs/manual/` | Arp's section of horde's manual (`arp.md`, its settings table generated from `spec/params.json` and checked by `./verify fast`) and its figure list | written; files into horde's manual later |
| `docs/prior-art.md` | Phase 0: pattern-bank landscape, failure modes, grid math, name check (dated, cited) | done |
| `spec/` | The 1.0 spec (SPEC.md), every setting (params.json), the candidate step-pattern bank | RATIFIED 2026-10-23 (protected) |
| `lab/arp-lab.html` | The playable twin: Ableton-style computer keys, MIDI, transport, every setting, morph A/B, a small synth, a **pattern view** (one full cycle as the core plays it, plus a live roll), an **order × pattern** thumbnail matrix, and a **build-your-own** step editor | built; play it to settle the open listening questions in ROADMAP (how settings switch during a morph, restart under morph, which patterns ship, pre-roll, tie memory, which pass a pick takes) |
| `tools/lab_smoke.mjs` | Headless checks of the lab core against the spec's invariants, run by `./verify fast` | live |
| `docs/spec-review-r1.md` … `-r4.md` | The four independent critic reviews of the spec, and where each finding went | done |
| `docs/deferred/` | Designs held for after 1.0 (`landed.md`) | live |
| `lab/patterns/` | Your saved build-your-own patterns (`arp-user-pattern/1` JSON, listed in `index.json`) | live: "Broken chord stabs" |
| `index.html` | Root landing page: redirects to the lab | done |
| `core/` | The C++20 core (standard library only, self-contained; horde copies it in). `tests/core/` is its invariant suite; `tools/core_test.sh` builds and runs it | **M1a, M1b and M2 built**: clocks (sync, free, time), held set, all 16 order styles, every step-pattern field, transposition, the user pattern, scale quantize, swing, velocity, restart, repeats, the chord stage, note randomness; 539 checks on each int128 path, in `./verify fast`. M3 (horde admission) under way |
| `verify` | The oracle: `./verify fast` / `full` / `report` | live |

## Neighbours

- **horde (HYPERSAW)** consumes Arp. Arp files to horde's `integrations/arp/`; horde files to Arp's `integrations/hypersaw/`.
- **FOUNDATIONS** owns the note vocabulary (`NoteRef`, `HeldStack`). A brief is filed when the C++ first touches it.
- **autonomous** provides the harness kit and the ecosystem tracks.

## Run the oracle

```bash
./verify fast
```

_Last verified: 2026-10-29 (`./verify fast` green at 04b79f4; spot-checked against the tree: the check count from `.harness/core_test.out`, `lab/patterns/index.json`, `docs/manual/`, the M3 status in ROADMAP)._
