# chord-modes-user-pattern — lab implements SPEC r2.6: 5 new orders, `pick`, the user pattern, `landed`, shuffle without repeats, lastQ rules

- **Queue item:** unqueued: lead briefs of 2026-10-22 (D-011 chord + note styles, `pick`
  and the user pattern; D-012 `landed`; D-012a shuffle without repeats; D-013 r2.6
  review folds), delivered to one implementer dispatch on branch
  `lab/chord-modes-user-pattern`.
- **Why:** the human polled for these features for 1.0, lab first. The lab is where
  they are decided by playing, and the smoke is what makes the listening twin
  honest about the new spec rows.
- **Changed** (`lab/arp-lab.html`, `tools/lab_smoke.mjs`; nothing else):
  - **Embedded copy.** `ARP_PARAMS` regenerated from `spec/params.json` (17 orders,
    `user` in the pattern text). `ARP_BANK` already matched.
  - **CORE order styles.** `chord_walk`, `stride`, `chord_between`, `pairs` per
    §4.2/r2.6; `landed` per §4.7; `shuffle` gains the cross-cycle swap
    (`_shuffleCycle`; for e = 2 the previous cycle's last note depends on its own swap, so
    the chain is replayed from cycle 0 and memoised in `_shLast`, a pure cache).
  - **CORE `pick`.** `order | 1-8 | low | high | chord`; only `order` advances `m`
    (and never under `landed`).
  - **CORE user pattern.** Slots allocated in the constructor (never touched by
    `prepare`/`reset`), triple buffer with an atomic-style index exchange, the
    "new" flag read once per `process`, `getUserPattern()`. Validation clamps every
    field; an unknown pick reads as `order`.
  - **CORE `landed`.** A 32-press phrase buffer preallocated at `prepare`. Capture is
    at the NoteOn (q from the exact position), the phrase dies when H empties, `len`
    rounds up to a whole beat. Preview/UI approximate by pressing keys a step apart.
  - **CORE play rule / quantize (r2.6).** Drop out-of-range keys, then quantize the kept
    keys in ascending pre-quantize order, chained through `lastQ`; collisions keep the
    earliest source in list order; `lastQ` resets at every release-all; chord_root
    always admits the anchor's pitch class.
  - **Plants (new, all inert by default):** `pickAdvancesM`, `ignorePick`,
    `swapMidStep`, `landedIgnoreTiming`, `shuffleBoundaryRepeat`, `lastQNoReset`,
    `collisionKeepsLast`, `prepareResetsUser`, `userFirstWins`.
  - **Smoke.** New or extended: order-styles table for the 4 new styles (independent
    construct-by-prose reference incl. source velocities), R1 extended, U1 (picks and
    rhythm; edits between blocks; two writes at one sample; an edit during a step's
    evaluation via a subclass hook, T17; clamping), L1 (time-stepped reference), N2,
    Q5 (up-tie, reset at release-all, collision), Q1 on the effective chord_root
    scale. N1 gains the `styles` and `landed` sessions; the G1 session gains the new
    styles, a user pattern edited mid-step, landed and quantize ties; a coverage
    check keeps those windows from being silently empty.
  - **UI.** A "Build your own" card (length, per-step pick / note-rest-tie / vel / gate /
    oct / prob / ratchet / trig; start-from-bank, clear, play it). Every edit calls
    `eng.core.setUserPattern`. `user` is in the pattern picker and the matrix;
    the step grid shows non-default picks. `orderPeriod` and the preview cycle math
    know the new styles, explicit picks and `landed`.
- **Evidence consulted:** `spec/SPEC.md` r2.6 (§4.2, 4.3, 4.6, 4.7, 6, 7, 10, 11, 13),
  `spec/params.json`, `spec/bank/starter.json`, the lab's CORE/UI sections,
  `tools/lab_smoke.mjs`, `docs/spec-review-r2.md` (existence only).
- **Alternatives rejected:**
  - `swapMidStep` as a between-block immediate swap: unobservable, because the pattern
    is only read inside step evaluation. It is a write-in-place into the reader's
    slot, and the detector injects the edit during `_play`.
  - A `lastQAtEmission` plant: in the lab, pending emissions and step evaluations
    interleave in time order whatever the block size, so G1 cannot diverge on it.
    Not built.
- **SPEC-GAPs (marked `// SPEC-GAP:` in code where they live):**
  1. `landed` at a transport edge: the phrase is kept and re-anchored to the first
     grid step of the new position (§4.7 silent).
  2. `landed` on a rate change: the phrase re-anchors at the new grid step; its q's are
     read as steps of the new rate. `len` is recomputed from the new `L`.
  3. (Superseded by the lead, SPEC §4.7/§13b.2.) `landed`'s first press snapped BACK to an evaluated step under
     nearest-step rounding. Now `kAbs0 = cdiv(t0, L)` (`_k0Of`; plant `landedSnapBack` keeps the old rounding); L1 has a
     playing-host case with a press 0.3 step late.
  4. A `landed` position with no press: nothing plays; the step's trigger still fires
     (same result as §4.3 step 4).
  5. `landed` with an empty phrase (keys held before the core saw them, i.e. after
     `prepare`/`reset`): all rests.
  6. Free-clock re-base while a phrase is live: `t0` shifts with the clock and `k0` is
     recomputed by the same rounding.
  7. `chord_root` always admits the anchor's pitch class only when the mask is non-zero
     (a zero mask still disables quantize).
  8. UI: "clear" restores the default (8 plain steps), not all rests.
- **Verify:** `node tools/lab_smoke.mjs` exit 0; `./verify fast` exit 0, git
  `0cdb3b5` (tree `1ba262d4a0c8620e36cfcf685df5b24736e7d26f`, dirty working tree).
  The UI was only exercised under a stubbed DOM (init, frames, every editor control,
  preview and matrix for `landed`/`stride`/`pairs`/`shuffle`), never in a browser.
- **Open questions:** the lab's UI needs the human's eyes in light and dark; the
  `landed` preview is an approximation by design.
