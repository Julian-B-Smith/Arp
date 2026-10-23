# one-quantize-switch — the arp's `quantize` governs the chord stage too; lab default listens to arp + chord (SPEC r2.12, D-020)

- **Queue item:** unqueued: lead bug-fix dispatch of 2026-10-22. The human reported "sustained notes layered over the arp" and "scale
  quantization doesn't really work on either the chord or the arp"; the lead reproduced: the lab's `listen = all` voiced the raw held keys (horde's
  `direct` stream, never quantized) and the chord stage's +0 slot emitted the played key unquantized (C# in C major sounded as 61 [chord]).
  The human ruled by poll: ONE quantize switch; the lab default listen is arp + chord.
- **Changed** (`lab/arp-lab.html`, `tools/lab_smoke.mjs`; nothing else):
  - `ARP_PARAMS`: `chord_quantize` removed (47 params; the lead had regenerated it in parallel, identical).
  - **CORE.** `_chordMap` snaps EVERY chord key, the +0 slot included, iff `quantize` is not `off` and the effective scale has a non-zero mask
    (§6: source / root / mask, the `chord_root` anchor on the played keys H); ties always lower, never touching `lastQ`. Degrees with quantize off or a
    zero mask: a degree is one semitone. All `chord_quantize` reads are gone; the re-map signature still includes the arp's `quantize`.
    Plants: `chordPlusZeroUnquantized` (the old +0 slot), `chordSkipQuantize` (no snapping on the chord stream).
  - **Smoke.** The CH1 cases that used `chord_quantize` now use `quantize`. New CH1 (r): C# held in C major, quantize on, +4 +7 → {60, 65, 67} (the played note
    snapped, tie lower); quantize off → {61, 65, 68}. (s): a session of out-of-scale presses with arp + chord both on: every arp AND chord NoteOn is in
    the mask. Both go red under their plants. Q1 for the arp is unchanged.
  - **UI.** The "listen to" selector defaults to "arp + chord"; the raw-keys (direct) voice exists only in the explicit option "arp + chord + your raw keys
    (horde's direct stream, unquantized)". The chord-quantize checkbox is gone from the chord card, with the note "Quantize (in Settings) snaps the chord
    too, including the note you play."
- **SPEC-GAPs:** none new. (The chord stage's effective scale still reads the arp's scale source / root / mask; with quantize `off` nothing is snapped.)
- **Verify:** `node tools/lab_smoke.mjs` exit 0; `./verify fast` exit 0. UI under a stubbed DOM only.
- **Open questions:** none.
