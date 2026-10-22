# latch-at-offset-0 — `latch` applies at offset 0 of its block (SPEC §13b item 12)

- **Queue item:** unqueued: lead fix of 2026-10-22. It **supersedes SPEC-GAP 5** of `2026-10-22-chord-stage-held-like-r2-11.md` (a latch change
  applied at the next arp step).
- **Why:** with `arp_enabled` off (chord-generator mode) there are no arp steps, so a latch turned off was never applied and the latched chord
  droned on. `latch` changes H, which the chord stage follows, so it applies at offset 0 of the block whose snapshot changes it, like `enabled`.
- **Changed** (`lab/arp-lab.html`, `tools/lab_smoke.mjs`): at the block start the core adopts a changed `latch` and, on `off`, drops the LATCHED
  refs (the chord stage's sync that follows ends their chord notes at that sample; the arp sees the new H from its next step). Plant
  `latchAtNextStep` keeps the old behaviour (`_adopt` at an arp step). New CH1 cases (p) arp off + chord on + latch replace: every chord note
  ends at the latch-off block's offset 0 (the plant never ends them); (q) arp on: the latched arp has no note from the latch-off block on,
  the chord ends there, N1 holds.
- **Verify:** `node tools/lab_smoke.mjs` exit 0; `./verify fast` exit 0.
- **Open questions:** none.
