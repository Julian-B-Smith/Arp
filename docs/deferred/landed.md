# Deferred past 1.0: `landed`, the order and timing of the presses

> **Status: DEFERRED to after 1.0** (D-014, 2026-10-22, the human: "remove as landed and hold it
> for after 1.0"). This file preserves the design so nothing has to be re-derived. It was specified
> as SPEC r2.5–r2.6 §4.7, built and tested in the lab on branch `lab/chord-modes-user-pattern`
> (its smoke check L1, with plants `landedIgnoreTiming` and `landedSnapBack`, passed), then removed
> from the 1.0 lab. **The working implementation is in commit `625370f`** (pushed, branch
> `lab/chord-modes-user-pattern`, PR #11): `lab/arp-lab.html` (the `landed` capture and playback
> in CORE) and `tools/lab_smoke.mjs` (check L1). Restore it from there after 1.0.
>
> Lessons carried with it:
> - A slightly late first press must anchor at the NEXT grid step (`kAbs0 = cdiv(t0, L)`). The
>   literal "nearest step" reading silenced the first note for a whole loop.
> - The open listening question was whether the loop length rounds up to a whole beat (Q-A6;
>   proposed yes).

## The design, as specified (SPEC r2.6 §4.7)

### 4.7 `landed`: the order and the timing of the presses (D-012)
`landed` replays **the gesture as it landed**: which keys were pressed, in
what order, and **when**, each snapped to the grid, looped while the gesture
lives.

- **Capture.** At a gesture start (§3), the **phrase** is cleared and
  anchored:
  - the first `NoteOn` sets `t0`, its exact position on the active clock;
  - every `NoteOn` of the gesture, the first included, is captured as
    `(key, vel, ch, q)`;
  - `q = fdiv(2·(t − t0) + L, 2·L)` is its nearest grid step from `t0`, with
    ties rounding up;
  - **repeated presses count each time**, including a re-press of a key;
  - presses that snap to the same `q` form a chord at that position;
  - capacity is **32 captured presses** per phrase (later presses are
    ignored, counted).
- **Lifetime.**
  - The phrase lives until H becomes empty, i.e. until every key, pedal and
    latch hold is released. Releasing one key mid-gesture **keeps** its
    captured press: the phrase is a recording, not the live held set.
  - Under latch, the phrase lives as long as the latch holds.
  - The next gesture start clears it and records anew.
- **Phrase length** `len`, PROPOSED (Q-A6, decided by playing):
  - start from `qmax + 1`, where `qmax` is the largest captured `q`;
  - if `mod(TPQ, L) = 0`, round **up** to a whole beat, a multiple of
    `TPQ / L` steps;
  - otherwise keep it exact;
  - the result never exceeds 64 steps (presses beyond step 63 are ignored,
    counted).
- **Playback.**
  - Phrase step 0 is the **first grid step at or after `t0`** (`kAbs0 = cdiv(t0, L)`), as a `chord`
    restart anchors. So the first press always sounds at once, at most one step late, and never
    snaps back to a step already passed. Later presses keep their `q` relative to `t0`.
  - At step kAbs, the phrase position is `q = mod(kAbs − kAbs0, len)`.
  - **`landed` ignores `restart`**: the phrase is anchored by its own first
    press.
  - If the phrase holds presses at `q`, they are the step's keys, each with
    its captured `vel` and `ch`, then octave copies per `octaves` (+12·i for
    i < oct, all at once, as a chord).
  - Positions with no press are **rests from the order**: no note, and the
    step pattern's fields cannot create one.
  - `landed` never advances `m`; it is timed, not counted.
  - With `pick ≠ order`, the explicit pick reads E as usual (§4.3).
- **Step patterns** overlay as for any order:
  - `on`/`tie`/`prob` gate the step;
  - `vel`/`gate`/`oct`/`ratchet`/`trig` shape it.
  - A step-pattern rest silences a captured press at that step.
- **Velocity.** The captured `vel` is the `played` base (§4.4).


## Edge cases settled by the lab build (were SPEC §13b items 1–5)

1. **`landed` at a transport edge or a rate change:** the phrase is kept. It
   re-anchors at the first grid step of the new position or rate, its `q`
   values are read as steps of the current rate, and `len` is recomputed.
2. **`landed`'s first press never snaps back** to an already-evaluated step:
   see §4.7, where `kAbs0 = cdiv(t0, L)`. The lab build had found that the
   literal "rounded" reading silenced the first note for a whole loop.
3. **A `landed` position with no press** plays no note, but the step's
   trigger still fires (§4.3 step 4).
4. **`landed` with an empty phrase** (keys held before the core saw them) is
   all rests until the next gesture.
5. **A free-clock re-base while a phrase is live** shifts `t0` with the
   clock and recomputes `kAbs0` by the same rule.

## Its invariant (was SPEC §11 L1)

`landed` replays the captured presses at their snapped steps, in order, with the captured
velocity, looping at `len`; repeated presses count. Planted controls: ignore the captured timing
(play in press order, one per step); snap the first press back to an already-evaluated step.
