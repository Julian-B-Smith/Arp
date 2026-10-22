# lab-pattern-view — the lab shows the pattern: one full cycle, plus a live roll

- **Queue item:** unqueued, at the human's request ("visualize the pattern
  in the lab"). It serves Q-A3 (choosing the starter bank) directly.
- **Changed** (`lab/arp-lab.html`, UI section only; CORE untouched): a
  **Pattern** card with two piano rolls.
  - **One full cycle.** A separate `ArpCore` (the code the smoke checks)
    renders offline on a simulated playing host at 120 bpm, with the current
    (morph-effective) settings and the held keys, or a C3-minor example
    chord.
    - The cycle length is computed from SPEC §4.2: pattern length ×
      period / gcd(period, plays), capped at 64 steps or 64 beats.
    - The view shows notes (brightness = velocity), step numbers, shaded
      rests, dashed ties, trigger markers with their lanes, and beat lines.
    - It recomputes only when something changes, at most about 8 times a
      second.
  - **Live.** The last 4 s of what the scheduler sent (notes, steps,
    triggers), with a playhead.
- **Traps handled:**
  - The preview starts half a step before tick 0 and presses keys one sample
    apart, in press order. Step 0 then lands at tick 0 under every restart
    value, and `played` sees the real order.
  - The paint loop now starts at page load, so the preview shows before audio
    starts.
  - The canvas CSS height lives in `data-h`. Reading back the dpr-scaled
    `height` attribute grew the canvas on every redraw at dpr 2; this was
    seen in the browser and fixed.
- **Evidence (real browser, dpr 2):**
  - bounce, 2 octaves, `roll_end`, swing 600: a 40-step cycle (= 8 × 10 /
    gcd(10, 8)), the bounce zigzag across C3–G4, odd steps swung late, ×2
    and ×4 ratchets on steps 7 and 8;
  - converge, 4 held keys, 2 octaves, `three_three_two`: 56 steps, rests
    shaded, and the live roll scrolling with the playhead;
  - canvas heights stable at 190 px and 150 px;
  - no console errors.
- **Verify:** `node tools/lab_smoke.mjs` OK; `./verify fast` exit 0.
