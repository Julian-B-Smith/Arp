# preview-chord — the pattern preview read as "too many notes" with the chord stage on

- **Queue item:** unqueued, at the human's report ("the preview is
  inaccurate when chord is on; it shows too many notes").
- **Cause** (reproduced headless with the lab's own core, so not guessed):
  - With nothing held, the preview fed the example **chord** C E♭ G into
    the chord stage.
  - The default shape (+4, +7) stacked a triad on each note, giving 7 keys
    (60 63 64 67 70 71 74). The arp cycled over all 7.
  - The preview also drew the chord stream's 7 sustained notes as solid
    bars.
  - The core was correct; the preview's input and drawing misled.
- **Changed** (`lab/arp-lab.html`, UI only; CORE untouched):
  - with the chord stage on and nothing held, the preview's example is a
    **single note** (C3), because the stage makes the chord;
  - chord-stream notes are drawn as a faint full-row band **behind** the
    arp's notes, in the preview and the live roll;
  - the notes say "Example note C3 through the chord stage".
- **Evidence:** in the browser, chord on with nothing held gives "3 steps
  per cycle", the arp climbs C E G, and the chord shows as faint bands. No
  console errors.
- **Verify:** `node tools/lab_smoke.mjs` OK; `./verify fast` exit 0.
- **Note:** holding a triad with the chord stage on is still a 7-key
  cluster. That is the spec working as designed (§14), not a preview bug.
