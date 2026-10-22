# quantize-accents — horde-schema scale quantize (D-010); accents shown in every pattern view

- **Queue item:** two human requests: "represent the accents in the
  preview", and "a scale quantize option that fits into horde's scale
  quantization schema". D-010.
- **Evidence consulted** (horde, read-only):
  - ROADMAP B260 and B391 (`scaleRoot` + `scaleDeg0..11`, ids 116–128, a
    global scale);
  - the ruling "the mask is the truth, the name is UI";
  - `docs/design/bend-lab.html` `quantise()` (a root-relative mask, ties
    toward the previous emitted step) and `SCALES` (18 names);
  - DECISIONS ADR-112 (movable-do "scale (offset)").
- **The human's rulings (poll):** global source by default with an own scale
  optional; modes off / scale / chord_root; ties toward the previous note.
- **Changed:**
  - SPEC r2.3 §6 and §11 (Q2–Q4);
  - `params.json`: `quantize` becomes an enum; `scale_source`,
    `scale_root`, `scale_deg0..11` and `ui_scale_names` are added;
  - the lab CORE:
    - `_scaleOf` (source and chord-root anchor) and `_quant` (root-relative,
      0–127, tie toward `lastQ`);
    - plants `tieAlwaysDown`, `ownIgnored`, `chordRootAsRoot`;
    - `DISCRETE_KEYS` derived from params.json;
    - `ord` on `lastSteps`;
  - the lab UI: an own-scale control (a name picker that writes root +
    degrees and reads "custom" when hand-edited, a root, twelve degree
    toggles) and horde's names on the simulated global scale;
  - accents: velocity drawn as thickness in every roll, and an accented step
    (vel > 100 %) marked with a hot cap and tick, or a hot bar in thumbnails.
    A note's step is found through its `ord`, and steps are now recorded
    before ons, so an on finds its own step.
- **Evidence:**
  - smoke Q1–Q4 pass and their plants fire;
  - Q2 reads `60 69 62 69 62 69`: the first C♯ has no previous note and goes
    to C, and every later C♯ goes to D, toward the A before it;
  - in the browser, picking "dorian" wrote C D E♭ F G A B♭; the accented
    downbeats show capped and thicker in the preview, and orange in the
    "Downbeat accent" column of the matrix; no console errors.
- **Verify:** `node tools/lab_smoke.mjs` OK; `./verify fast` exit 0.
