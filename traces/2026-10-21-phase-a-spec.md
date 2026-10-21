# phase-a-spec — spec r1 → critic → r2 → lab gaps → r2.1; lab gate wired

- **Queue item:** ROADMAP Phase A, approved by poll ("Proceed, including the
  ./verify edit"); D-007 and D-008.
- **Why:** the Phase A gate is a ratifiable spec plus a playable, checked lab.
- **The path:**
  - the lead wrote r1;
  - an independent critic (Opus, read-only, sourced separately) found 13
    HIGH, 9 MED and 4 LOW issues. All were folded into r2, with each
    disposition in `docs/spec-review-r1.md`;
  - the implementer (Sonnet) built the lab and smoke against r2, mid-course
    by message, and marked 17 SPEC-GAPs;
  - the lead adopted their readings as §13 (r2.1). P1 now uses exact
    endpoints, because a ±2 % band cannot see a `<=` bug.
- **The lab in a real browser** (Browser pane, localhost):
  - it rendered with no console errors and audio ran at 48 kHz;
  - one held key on the free clock gave ons every 0.125 s at 1/16 and
    120 bpm, with offs 0.100 s later (gate 80), and ids consecutive;
  - on release the clock went idle with 0 sounding.
- **`./verify` wiring:** `need_node` (absent means RED, never a skip),
  `lab_smoke_gate`, and structure entries. Proven to fire in scratch copies:
  - a real core bug (an unconditional dropped release) went red, and N1
    named the stuck note;
  - a missing lab went red at the structure gate;
  - with no node on PATH it went red.
- **Verify:** `./verify fast` exit 0 (`lab_smoke: OK`, about 0.3 s).
- **Open:** Q-A1, Q-A2, Q-A3, Q-A5 by playing; spec ratification; the horde
  notice (seq 2) awaits a go-ahead; Q-H1, Q-H2, Q-H3.
