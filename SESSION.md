# SESSION — Arp (hot state; the only prior-session context the next session trusts)

_Closed 2026-10-22 by `/breakdown`. Written last, so it describes the tree as it ends._

## Where it stands
- **Phase A: built, awaiting the human** (ROADMAP "Phase A — Spec + lab twin ← current").
  - `spec/SPEC.md` is **DRAFT r2.7**, with `params.json` and `bank/starter.json`.
  - It has had two independent Opus reviews, both folded in (`docs/spec-review-r1.md`, `-r2.md`).
- **The lab is playable** (`lab/arp-lab.html`, served at the repo root, which redirects to it):
  - 16 order styles, the step-pattern bank plus a build-your-own user pattern;
  - horde-schema scale quantize (global or own; off / scale / chord_root; ties toward the previous note);
  - Ableton-style transposition steps (semitones or degrees);
  - morph A/B, accents, a pattern view, the order × pattern matrix, Ableton-style computer keys.
- **Deferred past 1.0:** `landed` (`docs/deferred/landed.md`, pinned to commit `625370f`).
- **Merged on main:** PRs #1–#11. Main is at `7b4d857` before this close's PR.
- **The C++ core:** not started (M1). horde's module cutoff is **2026-12-11**.

## Verify
`./verify fast` is **green** at this close: kit integrity, leak, private-name, structure, manifest and intake pin, plus the lab smoke (about 80 checks covering SPEC §11, every detector proven on a planted fault, about 1 s).

## Next first move (the human's pick)
**Play the lab, then ratify the spec by poll.** Open http://localhost:59531 (`python3 -m http.server 59531 --bind 127.0.0.1` from the repo root, or the Browser pane's "lab" preview). Settle by ear:
1. How discrete settings switch during a morph: largest weight with 0.05 hysteresis (proposed). "Load Q-A1 demo".
2. Restart under morph (proposed: bar/free re-align at once; chord waits for the next gesture). "Load Q-A2 demo".
3. Which ~12–16 orders and patterns make the starter bank. Use the order × pattern matrix.
4. Pre-roll plays? (proposed: yes)
5. Quantize-tie memory resets at every release-all, so every play-from-here sounds the same? (proposed: yes)
6. Which transposition pass an explicit pick takes (proposed: the next order note's pass).

Ratifying the spec makes `spec/` a protected path and opens M1 (the C++ core).

## Open threads
- **With horde (ball theirs):**
  - Q-H1, which 1.0 must-have rows bind Arp; Q-H2, how horde consumes the core (both in the spin-up notice, merged as horde #1179, respond-by 2026-11-13);
  - Q-H3, the trigger-lane handoff (notice seq 2, horde PR #1184, respond-by 2026-11-13);
  - Q-H5, a pedal that sustains `direct` voices only (our response seq 4, in `integrations/hypersaw/`).
- **Ours to file next notice:**
  - Q-H6, how presets carry the user pattern. **Read horde ADR-169 first**; it may already answer.
  - D-010's own-scale note (information only).
  - Check whether horde #1184 and autonomous #60 have merged.
- **autonomous:** brief `arp-001` (track registration), PR #60, respond-by 2026-11-04.
- **Cutoff:** 1.0 widened five times today; see REFLECTIONS for the yield order.

## This session's traces
`traces/2026-10-21-*` (spinup, pattern bank and filings, phase0, phase-a-spec, lab-twin) and `traces/2026-10-22-*`: lab-ableton-keys, lab-pattern-view, note-stream, lab-root-redirect, lab-combo-thumbs, quantize-accents, chord-modes-user-pattern, transposition-steps-landed-removed.
