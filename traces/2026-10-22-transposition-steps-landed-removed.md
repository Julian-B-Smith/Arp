# transposition-steps-landed-removed — `landed` out, `octaves` replaced by Ableton-style transposition steps (SPEC r2.7)

- **Queue item:** unqueued: lead dispatch of 2026-10-22 (D-014: the human deferred `landed` past 1.0
  and asked for "steps" with a step size in place of "octaves"; poll: transpose-per-repeat, in
  semitones or scale degrees). Follows `2026-10-22-chord-modes-user-pattern.md`, which it supersedes
  where it describes `landed` and `octaves`.
- **Recorded hashes (for `docs/deferred/landed.md`):**
  - **Last commit with the working `landed` implementation: `625370f0e5fc5b89495fbd73898b897be4c9baa9`**
    (its `lab/arp-lab.html` has the `landed` capture/playback and its `tools/lab_smoke.mjs` has L1 with
    plants `landedIgnoreTiming` and `landedSnapBack`).
  - **`landed` was removed in the commit after `625370f`.** At the time of this change HEAD was `625370f`
    and the removal sits uncommitted on top of it.
- **Why:** the 1.0 range model is now "the whole pattern repeats, shifted", as in Ableton's arpeggiator,
  instead of one merged octave range; and `landed` is held for after 1.0.
- **Changed** (`lab/arp-lab.html`, `tools/lab_smoke.mjs`; nothing else):
  - **Embedded copy.** `ARP_PARAMS` regenerated from `spec/params.json` (no `octaves`; `steps`,
    `step_size`, `step_unit`; no `landed`).
  - **CORE removals.** `landed` entirely (the phrase buffer, capture, `_phraseLen`/`_k0Of`/`_phraseAnchor`, the
    edge / rate-change / free-clock hooks, the `phraseOverflow` counter) and every `octaves` expansion.
    E is the held keys in pitch order; `Pp` is the held keys in press order; `chord` has period 1 and
    `stride` period 2 with no octave cycling.
  - **CORE transposition (§4.7).** `t = mod(fdiv(m, P), steps + 1)` (`_pass`; `P` from the live H, `random`: `e`);
    explicit picks use the current pass. Semitones: `key + oct·12 + t·step_size`, then drop, then quantize.
    Degrees: quantize first, `_degreeMove` t·step_size in-scale keys (chromatic when quantize is off or the mask
    is zero), then `+ oct·12`, then drop; the result sets `lastQ`.
  - **Plants (new):** `transposeMerged` (the old merged-range behaviour, generalised to `step_size` copies),
    `degreesAsSemis`, `stepsZeroShifts`. Removed with `landed`: `landedIgnoreTiming`, `landedSnapBack`.
  - **Smoke.** L1 and the landed sessions/coverage bits deleted. Every `octaves` use moved to the new model.
    New X1: (a) the spec's example `C E G | G B D | D F# A | C E G`; (b) `steps = 0` never shifts; (c) degrees in
    C major (`+2` -> `C E G | E G B`, `-1` -> `C E G | B D F`, quantize off -> semitones); (d) a `low` pick follows
    its pass; (e) all 13 deterministic styles over 3 passes. The N1 `discrete` and `styles` sessions and the G1
    tail exercise `steps > 0` in both units (`step_size` 7 and 2).
  - **UI.** The settings rows for `steps`, `step_size` and `step_unit` come from params. `orderPeriod` drops
    octave copies and `landed`; the preview cycle is the order cycle x (`steps` + 1); the landed preview and
    matrix notes are gone.
- **Evidence consulted:** `spec/SPEC.md` r2.7 (§4.2, 4.3, 4.7, 11 X1, 12, 13b; `landed` is gone from §4/§11/§12/§13b),
  `spec/params.json`, `docs/deferred/landed.md`, the lab and smoke as above.
- **Alternatives rejected:** none considered beyond the plants above (a plant for "`steps = 0` shifts" was cheaper
  than a perturbed-expectation control for X1(b)).
- **SPEC-GAPs (marked `// SPEC-GAP:` where they live):**
  1. `lastQ` after a degree move is the final, octave-shifted output key; it is set only when quantize is on with a
     non-zero mask, and a key dropped as out of range leaves `lastQ` at its first quantize result.
  2. A degree move that runs off 0-127 drops the key (counted, T16).
  3. At pass 0 (`t = 0`) in degrees mode the ordinary path runs (no quantize-first): the same result.
  4. A `low`/`high`/`k` pick reads `t` from `m` at its own step, so the pick right after the last `order` play of a
     pass already belongs to the next pass (X1(d)). This follows from §4.7 literally.
- **Verify:** `node tools/lab_smoke.mjs` exit 0; `./verify fast` exit 0. The UI was exercised only under a stubbed
  DOM, never in a browser.
- **Open questions:** none.
