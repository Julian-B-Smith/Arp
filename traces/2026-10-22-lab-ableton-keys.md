# lab-ableton-keys — the lab's computer keyboard follows Ableton Live's layout

- **Queue item:** unqueued, at the human's request ("edit the lab so the
  keyboard is ableton-style").
- **Changed** (`lab/arp-lab.html`, UI section only; CORE untouched, so the
  smoke is unaffected):
  - the home row `A S D F G H J K L ; '` plays the white keys and
    `W E T Y U O P` the black keys;
  - `Z`/`X` shift the octave and `C`/`V` step the velocity
    (1, 20, 40, 60, 80, 100, 120, 127), as in Live;
  - `Space` plays and stops;
  - keys match on physical position (`ev.code`), so AZERTY, QWERTZ and Caps
    Lock all work;
  - the on-screen keyboard follows the octave, labels each key with its
    computer key, and marks the mapped span;
  - note names follow Live (MIDI 60 = C3).
- **A trap avoided:** a release ends the note its **press** sounded (held in
  a code → key map). Recomputing it from the current octave after a
  mid-hold `X` would release the wrong note and strand the first. Window
  blur releases every held key, since its keyup never arrives.
- **Evidence:**
  - real browser (Browser pane): A, D, G played 60, 64, 67;
  - `X` mid-hold moved the range to C4–F5 while the chord kept its notes and
    released cleanly (0 sounding afterwards);
  - `V` gave velocity 120, and `K` played 84 at velocity 120;
  - a screenshot shows the labels.
- **Verify:** `node tools/lab_smoke.mjs` OK; `./verify fast` exit 0.
