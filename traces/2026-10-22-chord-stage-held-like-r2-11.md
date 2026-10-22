# chord-stage-held-like-r2-11 — chord notes behave like held notes; `arp_enabled`; latched `exhausted`

- **Queue item:** unqueued: lead dispatch of 2026-10-22 (D-019, the fourth independent review, `docs/spec-review-r4.md`; the human
  ruled by poll that chord notes behave like held notes and that any key-adding press restarts the repeat count). It follows
  `2026-10-22-repeats-chord-stage.md` and **supersedes its SPEC-GAPs 1 (T4/arp toggle) and 4 (the chord stage's own lastQ chain)**.
- **Changed** (`lab/arp-lab.html`, `tools/lab_smoke.mjs`; nothing else):
  - **Embedded copy.** `ARP_PARAMS` regenerated (48 params, `arp_enabled`).
  - **Module vs arp.** `enabled` is the whole module (off: T4, every chord and arp note ends, the stage is inactive). `arp_enabled` is
    the arp alone, applied at offset 0: switching it off ends the ARP's notes (`_releaseArp`) and never touches chord notes. `this.enabled`
    now means "the arp is running" (module on and `arp_enabled`).
  - **Chord notes like held notes.** Transport edges (T6) no longer end or restart them (the T6 restart is gone); they end only when
    their stored source count reaches 0, at a re-map that drops them, at `chord_enabled` off, at the module's `enabled` off (T4), at
    panic (T7) and at T10.
  - **Stored mapping.** A key entering H gets its chord keys computed ONCE and stored per (held key, slot); a leaver just drops its stored
    keys. A re-map (a chord_* setting, a scale setting, the arp's quantize mode, the global scale when it is the source, or `chord_enabled`
    turning on) recomputes every held key at offset 0 of its block: leavers end, joiners start, unchanged keys keep sounding. The `+0` slot
    is the played key. Semitones: `h + shift`, quantized with ties ALWAYS lower; degrees: from the nearest in-scale key (ties lower), move in
    the scale. The stage never reads or writes `lastQ` (`_quant` takes the tie target as a parameter).
  - **Dedupe by key.** Source counts per key; channel and velocity fixed from the earliest source at the NoteOn. The sync runs after the LAST
    input event of a sample, so a count going 1→0→1 within one sample keeps its note.
  - **Own sounding table.** Arp T11 eviction counts and evicts only arp entries; T2 retrigger acts within one stream, so an arp note and a
    chord note may sound on one key.
  - **Repeats.** `exhausted` is a latched flag, cleared at a gesture start, ANY NoteOn that adds a key to H (latch append and the pedal too;
    it also restarts `repeatSteps` at 0 but re-anchors only at a gesture start), T6, T7, T10. Spent steps still consume the once-guard as silent
    rests, so no burst follows. Morphs of `repeats`/`steps`/`pattern` never revive it.
  - **Plants (new):** `chordRecomputeAtRelease`, `t2AcrossStreams`, `arpToggleEndsChord`, `chordT6Restart`, `sharedSoundingTable`,
    `exhaustedStopsGuard`, `onlyGestureResets`, `morphRevives`, `chordSyncPerEvent`, `chordDedupeByChannelKey`.
  - **Smoke.** CH1 now (i) the strand case, (j) both streams on one key, (k) the arp toggle, (l) a transport stop/start, (m) arp eviction,
    (n) a 1→0→1 count inside one sample, (o) dedupe by key; RP1 (f) no burst after exhaustion, (g) a key-adding latch-append press restarts
    the count, (h) a repeats/steps/pattern morph never revives. CH1's "arp off" cases use `arp_enabled: false` with the module on. The `chordstage`
    N1 session and the G1 tail now toggle `arp_enabled`.
  - **UI.** An "arp on" toggle next to the chord stage's enable; the master `enabled` row reads "enabled (whole module)". Chord notes keep
    drawing on the roll while the arp is toggled.
- **Evidence consulted:** `spec/SPEC.md` r2.11 (§1, §4.8, §5.2 T2, §7 T4/T21, §11 RP1/CH1, §12 Q-A8, §14), `spec/params.json`, the lab and smoke.
- **SPEC-GAPs (marked `// SPEC-GAP:` where they live):**
  1. The chord stage's effective scale is the arp's scale source / root / mask with the chord_root anchor on the played keys H and
     `quantize = off` read as `scale` (the stage has no mode of its own); `chord_quantize` and degrees need only a non-zero mask.
  2. §14 says nothing about the arp's OWN notes when `arp_enabled` goes off; they end there (the arp's half of T4) and its pending queue clears.
  3. A re-map is triggered by a change of the signature (chord_* settings, scale settings, the arp's `quantize`, and the global scale root/mask
     when the source is global); the spec lists "a chord_* setting change, a scale-setting change".
  4. With the module off the stage is inactive and forgets its held keys; turning the module on with keys held maps them afresh.
  5. A chord set change caused by the latch turning off is applied at the next arp step (with the latch change), not at offset 0.
  6. A chord note refused at the event cap stays silent, counted, and is not retried while its count stays above 0.
  7. Spent (exhausted) steps leave no step record for the UI's step highlight.
- **Verify:** `node tools/lab_smoke.mjs` exit 0; `./verify fast` exit 0. UI under a stubbed DOM only.
- **Open questions:** none.
