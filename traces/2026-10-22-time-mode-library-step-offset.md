# time-mode-library-step-offset — lab implements SPEC r2.8: time mode, per-step `step` offset, the pattern library

- **Queue item:** unqueued: lead dispatch of 2026-10-22 (D-016: the human, by poll at /wakeup: a time mode shown as ms and
  Hz and firing from the key press; a user-pattern library; per-step `step` times a pattern step size in place of `oct`).
  Branch `feature/time-mode-library-step-offset`; nothing committed or pushed from here.
- **Why:** the human wants rates that are not tied to the host tempo, patterns they can keep, and a step offset that can move
  by scale degrees as well as octaves.
- **Changed** (`lab/arp-lab.html`, `tools/lab_smoke.mjs`; nothing else):
  - **Embedded copy.** `ARP_PARAMS` and `ARP_BANK` regenerated from the spec files (`rate_mode`, `step_time_us`; `step`
    replaces `oct`; `pattern_defaults`). The lead had done the same in parallel; the result is identical.
  - **CORE time mode (§2.6).** `rate_mode = time` makes a tick one microsecond (`D = sr`, `inc = 10^6` per sample, `L =
    step_time_us`). The time clock is the free clock in those units: it starts at each gesture with position 0, re-bases on the
    next gesture (pending positions shift exactly, epoch++), and stops when idle. Host tempo, position and transport edges are
    ignored. `bar` counts from tick 0 like `free`. A `rate_mode` change is a T6 clock switch at the block's offset 0. A changed
    `step_time_us` re-grids at the next step (see SPEC-GAP 1).
  - **CORE step offset (§4.3, §4.7).** `oct` is gone; `step` (-8..8) is multiplied by the pattern's `step_size` in its
    `step_unit` (bank patterns default from `pattern_defaults`; user patterns carry their own, clamped by `setUserPattern`;
    `getUserPattern` returns them). "Applying offsets": the pass transposition and the step offset each add a semitone part or a
    degree part; `key + S`, drop, quantize, move `G` in-scale keys, drop.
  - **Plants (new):** `timeUsesTempo`, `timeAnchorsTransport`, `stepSizeIgnored`.
  - **Smoke.** New TM1 (ons at `cdiv(k·U·sr, 10^6)` after the press, at 60 and 174 bpm, through host seeks and a stop/start; block
    sizes 4096/512/64/7/1 identical; 44.1/48/96/192 kHz give the same microsecond ticks) and ST1 (octave_leap = 60 72 60 48; size
    +7 with step 2 is +14; +2 degrees on C in C major is E; mixed units sum by kind). N1 gains a `time` session (time mode with
    host jumps and a stop, a step-time change, sync <-> time switches, restart bar/free, a degrees user pattern); the `styles`
    session and the G1 tail use non-12 step sizes (7, 2 and 5; degrees) and the G1 tail ends in time mode; the coverage check has
    a window for each. `oct` is gone from the U1 clamp check and UP1.
  - **UI.**
    - *Rate.* `rate_mode` is a select. In time mode the division picker is hidden and one row shows `step_time_us` as a
      log-scaled slider plus "ms" and "Hz" boxes that edit each other.
    - *Build your own.* A `step` row replaces `oct`; pattern-level "step size" and "unit" controls; the step grid shows e.g.
      "+1×12" (a "°" after the size for degrees).
    - *Library.* A name field with Save, Export and Import. Two lists, "Saved in the repo" (from `patterns/index.json`, read-only,
      with a note when fetch fails, e.g. on file://) and "Saved in this browser" (localStorage `arp.userPatterns.v1`, every access
      guarded). Load fills the editor and publishes through `setUserPattern`; imports and loads are normalised by a scratch core
      (the core's own validation). "Broken chord stabs" loads with picks 1,3,2,chord,1,4,chord(+1),2.
    - *Preview and matrix.* They run in time mode in microseconds, the roll axis is labelled in ms, and the note says so.
- **Evidence consulted:** `spec/SPEC.md` r2.8 (§2.6, 4.3, 4.6, 4.7, 11 TM1/ST1), `spec/params.json`, `spec/bank/starter.json`,
  `lab/patterns/`, the lab and smoke.
- **Alternatives rejected:** a second, time-only clock object (the free clock in different units already has every behaviour the
  spec asks for); a UI-side pattern validator (a scratch core reuses the core's).
- **SPEC-GAPs (marked `// SPEC-GAP:` in code where they live):**
  1. A change of `step_time_us` mid-run: §2.6 says it is read at each step's evaluation but not how the grid follows. Taken as a
     re-grid at the first new step at or after the old step's tick (as for a rate change) with `m` and the pattern position KEPT,
     so dragging the control does not restart the pattern.
  2. `restart = bar` in time mode counts from tick 0, exactly like `free` (§2.6: both count from the gesture start).
  3. A `rate_mode` change takes effect at offset 0 of the block whose snapshot carries it (a clock switch), not at the next step.
  4. The `timeUsesTempo` plant scales the time step by host tempo with 120 bpm as true time.
- **Verify:** `node tools/lab_smoke.mjs` exit 0; `./verify fast` exit 0 (git `ca93d99`, tree `89685b63d5fb91d753165cc5ef3ebfba88483a96`,
  working tree dirty). The UI ran only under a stubbed DOM (init, frames, time mode on/off, the library incl. loading the repo's
  pattern, Save, Export, Import), never in a browser.
- **Open questions:** none.
