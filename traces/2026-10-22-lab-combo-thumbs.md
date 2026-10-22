# lab-combo-thumbs — a thumbnail for every order × pattern combination

- **Queue item:** unqueued, at the human's request ("each pattern and order
  combo should show a little mock preview of the shape"). It serves the
  starter-bank choice directly.
- **Changed** (`lab/arp-lab.html`, UI section only; CORE untouched):
  - an **Order × pattern** card with a 12 × 11 matrix of thumbnails;
  - each thumbnail is `computePreview()`, the real core rendered offline,
    for that order and pattern, with every other setting as it is now and
    the held keys (or the example chord). They therefore change with
    octaves, gate, swing, rate and the keys you hold;
  - a thumbnail shows the shape only: pitch normalised per cell, bars by
    velocity, rests shaded, trigger ticks;
  - clicking (or Enter/Space on a focused cell) selects that order and
    pattern, and the selection outline follows the effective (morphed)
    settings;
  - `computePreview` gained a step cap (thumbnails show at most 32 steps).
- **Performance:** the 132 renders are spread across frames, about 6 ms per
  frame, and restart only when their inputs change.
- **Evidence** (real browser): all 132 cells rendered within 4 s with no
  console errors. Clicking bounce × gallop set the order and pattern
  dropdowns, moved the outline, and showed gallop's step grid.
- **Verify:** `node tools/lab_smoke.mjs` OK; `./verify fast` exit 0.
