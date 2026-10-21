# Arp

horde's source of generated notes. For horde 1.0 it is a simple arpeggiator: it takes the notes the player holds and
plays them as a pattern in time. After 1.0 it grows into step sequencing and generative MIDI. It is a C++ note-processor
library that horde runs ahead of its engine, plus a browser lab for playing it. "Arp" is a working name; the
user-facing name is cleared before release.

**Status: Phase 0 done 2026-10-21. Nothing is built yet.** Prior art is in [`docs/prior-art.md`](docs/prior-art.md). The
next step is the spec and the lab (Phase A). horde's module cutoff is **2026-12-11**. Phase state lives
in [`ROADMAP.md`](ROADMAP.md).

## What it will do (1.0)

- **Modes:** up, down, up-down (with and without repeated ends), as played, chord, and random (seeded, so it repeats).
- **Rate:** tempo-synced by horde's division names, with dotted and triplet values.
- **Shape:** octave range, gate length (past 100% for legato), swing, and velocity (as played, fixed, or accented by
  step).
- **Holding:** latch, either replacing or adding to the held notes. The sustain pedal holds the arp's input notes.
- **Scale:** optional quantize to horde's global scale.
- **Restart:** a setting with three values: on a new chord, on the bar, or free-running.
- **Pattern bank:** a starter set of extra note-order styles and preset step patterns (rhythm, rests, accents, gate,
  octave jumps), with per-step probability and ratchets. More entries after 1.0.
- **Morph:** every setting morphs with horde's corners.

The output is sample-accurate and identical at any block size or sample rate, and every note it plays ends exactly
once. **Status: planned.** Every item above is spec work in Phase A.

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
| `spec/`, `lab/`, `tools/`, `core/` | Spec, listening twin, oracles, C++ core | not yet built |
| `verify` | The oracle: `./verify fast` / `full` / `report` | live |

## Neighbours

- **horde (HYPERSAW)** consumes Arp. Arp files to horde's `integrations/arp/`; horde files to Arp's `integrations/hypersaw/`.
- **FOUNDATIONS** owns the note vocabulary (`NoteRef`, `HeldStack`). A brief is filed when the C++ first touches it.
- **autonomous** provides the harness kit and the ecosystem tracks.

## Run the oracle

```bash
./verify fast
```

_Last verified: 2026-10-21 (spin-up; `./verify fast` green)._
