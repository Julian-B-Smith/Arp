# repeats-chord-stage — lab implements SPEC r2.10: repeats and the chord stage with its own `chord` stream

- **Queue item:** unqueued: lead dispatch of 2026-10-22 (D-018, the human, by poll: `repeats` 0-16 = full cycles of the whole
  pattern per chord, 0 = forever; a chord stage before the arp with its own stream so horde can route chord and arp to different
  oscillators). Branch `feature/repeats-chord-stage`; nothing committed or pushed from here.
- **Changed** (`lab/arp-lab.html`, `tools/lab_smoke.mjs`; nothing else):
  - **Embedded copy.** `ARP_PARAMS` regenerated (47 params: `repeats`, `chord_enabled`, `chord_unit`, `chord_quantize`, `chord_on1..6`,
    `chord_shift1..6`).
  - **CORE repeats (§4.8).** `repeatSteps` counts evaluated steps from the gesture's first step; it resets at a gesture start and at
    T6/T7/T10. `C = plays > 0 ? Ls·(P·(steps+1)/gcd(P·(steps+1), plays)) : Ls`, recomputed from the live H' each step. At
    `repeatSteps >= repeats·C` the step is not evaluated (no notes, no triggers); sounding notes end by their gates (T20).
    Plants `repeatsCountPatternPasses` (C = Ls) and `repeatsNeverStop`.
  - **CORE chord stage (§14).** `_chordSet(S)` is a pure function of H, the settings and the scale: each held key plus each enabled
    slot (semitones, optionally quantized; degrees: quantize then move in-scale keys), out-of-range keys dropped, deduplicated by
    (channel, key) with a source count and the earliest source's velocity/channel/press order. `_chordSync(s)` runs at offset 0 of every
    block and after every input event: leavers end, joiners start, unchanged keys keep sounding. Chord notes are `stream: "chord"`
    NoteOns with their own refs, their own sounding entries (never scheduled, never touched by T2/T11), and exactly one NoteOff via
    the one `_release` path. Release-all (T4/T6/T7/T10) ends them; at T6 they restart at once if their sources are still held. The
    arp's H' is the chord set when the stage is on (`_heldArp`); the arp's `enabled` is independent (arp off + chord on = chord
    generator). Plants `chordNoDedupe`, `chordEndsOnFirstSource`, `arpIgnoresChord`, `chordRestartAll` (a shape change restarts
    every chord note).
  - **Smoke.** RP1 (2 repeats of up over C E G = C E G C E G then silence and no triggers; gallop + 1 step of +12 is 8 steps per
    cycle; 0 never stops; latch + repeats stops and a new gesture restarts; bar/free count from the gesture start) and CH1 (C + {+4,
    +7} = C E G as stream chord with the arp off; a shared key is emitted once and ends with its last source; degrees on D in C major
    = D F A; semitones with chord_quantize in C minor snaps to Eb; the arp plays the chord set; a shape change moves only leavers and
    joiners; stage off ends all; the pedal holds the chord), each with its plants firing first. S1 now requires `arp` on arp notes and
    `chord` on chord notes. A new N1 session `chordstage` (pedal, latch, panic, T6 stop/start, arp off/on, shape/unit change, stage
    off/on, repeats) and the G1 tail (chord stage on/off, a degrees shape, repeats) cover both; the coverage check has windows.
  - **UI.** A "Chord" card above Settings (enable, unit, quantize, six slots of on + shift -24..24). `repeats` shows "∞" at 0. A
    "listen to" selector (all / chord only / arp only / chord + arp) mutes voices in the page's synth only; "all" also voices the
    player's keys as a quiet `direct` sine. Chord notes use a triangle voice, a purple roll colour, a "[chord]" tag in the log and a
    legend entry. The preview feeds the example chord through the stage (the order cycle runs over the chord set; chord notes are
    drawn, thumbnails show the arp only).
- **Evidence consulted:** `spec/SPEC.md` r2.10 (§1 streams, §4.8, §14, §7 T20/T21, §11 RP1/CH1), `spec/params.json`, the lab and smoke.
- **SPEC-GAPs (marked `// SPEC-GAP:` where they live):**
  1. T4 (the arp's `enabled` → off) is a release-all, so it ends chord notes too, and they do NOT restart (only T6 restarts them): with
     the stage on and keys held, toggling the arp off silences the held chord until the keys are pressed again.
  2. The chord stage has no mode of its own: its effective scale is read from the arp's `scale_source`/`scale_root`/`scale_deg*`, with
     `quantize = off` read as `scale` (and `chord_root` anchoring on the lowest held key). `chord_quantize` and degrees need only a
     non-zero mask.
  3. `chord_quantize` quantizes the SHIFTED keys; the held key itself (the "+0" note) is the player's note and is not moved.
  4. §6's tie rule needs `lastQ`; the set is a pure function, so the stage runs its own chain, starting from "none" at each recompute
     and processing held keys ascending, then slots in order.
  5. A chord note refused at the event cap stays silent and counted (it has no sounding entry, so no off is owed).
  6. A chord set change caused by the latch turning off is applied at the next arp step, with the latch change (not at offset 0 of the block).
  7. Chord NoteOns sort before the arp's at one offset (order key -1).
- **Verify:** `node tools/lab_smoke.mjs` exit 0; `./verify fast` exit 0. UI under a stubbed DOM only (init with the stage on, time mode
  on, the library); the audio routing was not run.
- **Open questions:** gap 1 (is silencing held chords on an arp toggle intended?).
