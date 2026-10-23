# hear-chord-toggle — the lab can mute the chord stage's sustained notes

- **Queue item:** unqueued, at the human's request ("the sustained chord is
  still playing through the arp. Can we at least add a toggle for it?").
- **Cause:** the lab's listen default ("arp + chord", D-020) voices the
  chord stream. The chord stage both feeds the arp and emits its own
  sustained chord (SPEC §14), by design, because horde routes the streams.
- **Changed** (`lab/arp-lab.html`, UI only; CORE untouched; no spec change,
  so the functionality freeze is unaffected):
  - a **"hear the sustained chord"** checkbox on the chord card, **off by
    default**;
  - chord-stream notes are voiced only when it is on (and the listen setting
    allows them);
  - "chord only" still voices the chord.
- **Evidence:** the real browser, with oscillator creation counted. With
  the chord stage on and one key held:
  - toggle off: 0 chord (triangle) voices and 14 arp (saw) voices;
  - toggle on: 3 chord voices (C E G) alongside the arp.
- **Verify:** `node tools/lab_smoke.mjs` OK; `./verify fast` exit 0.
