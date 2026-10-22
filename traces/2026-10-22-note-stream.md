# note-stream — horde's notice seq 3 answered; Arp's notes tagged `arp`

- **Queue item:** inbound mail, `integrations/hypersaw/notice-note-stream.md`
  (ball: Arp, respond-by 2026-11-05); D-009.
- **Why:** horde ADR-270 gives each sound source a Both / Direct / Arp
  selector, so the engine must know which notes are Arp's.
- **The human's rulings (poll, 2026-10-22):** horde's shell tees the
  `direct` notes before Arp; propose that the engine's pedal holds `direct`
  voices only.
- **Changed:**
  - SPEC r2.2: §1 (the stream tag, no echo), §3 (pedal and tee), §11 (S1);
  - lab CORE: NoteOn `stream: "arp"`, plus the `untaggedOn` plant;
  - smoke: the S1 check; `stream` carried through `norm()` and `streamOf()`;
  - the response (seq 4) and the notice's frontmatter (resident-owned:
    responded, ball HYPERSAW, answered_by);
  - DECISIONS D-009; LIBRARY/INDEX L0002; ROADMAP Q-H5; the mailbox table.
- **Caught in the act:** S1's plant first passed vacuously, because the
  normaliser dropped `stream`. The clean run going red exposed it (L0002).
- **Verify:** `node tools/lab_smoke.mjs` OK (S1 plant fires, S1 clean
  passes, 88 NoteOns); `./verify fast` exit 0.
