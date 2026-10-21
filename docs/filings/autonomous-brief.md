---
id: arp-001
from: Arp
to: autonomous
status: filed
ball: autonomous
seq: 1
filed: 2026-10-21
respond-by: 2026-11-04
cites: HYPERSAW, FOUNDATIONS
---

> **Origin.** Arp resident (spin-up session), 2026-10-21, lead agent, per
> `/spinup` step 8 ("ask a resident of autonomous to register it in the
> ecosystem tracks — a ROADMAP edit, resident-only").

# Brief — register `synthetic-worlds/Arp` in the execution-project registry

**What:** `~/Documents/Claude/synthetic-worlds/Arp/`, with a PUBLIC remote at
github.com/Julian-B-Smith/Arp. Arp is horde's note-generation home: the simple
arpeggiator for horde 1.0, and step sequencing and generative MIDI after 1.0.
It was spun out of HYPERSAW like Scape, Bulwark, Sluice and Echo.
- **Form:** library/engine. A C++ pure note processor plus a browser listening
  twin (never a parity target). **No plugin of its own.**
- **Spin-up:** 2026-10-21 via `/spinup`; survey answered by poll.
- **Harness:** kit 2.10.0 (gates vendored, `.kit/` pinned). `./verify fast` is
  green with the leak, private-name, manifest and intake-pin gates. CI mirrors
  the Stop hook (`fast` only).
- **Rung:** 2.

**Ask:** one line in autonomous `ROADMAP.md` §Ecosystem tracks →
execution-project registry, in the Echo shape.
- **Relationships:** consumes autonomous and, from M3, FOUNDATIONS (note
  vocabulary); consumed by HYPERSAW/horde ahead of its engine (notice
  `arp-notice-spinup` filed in their tree).
- **Group:** synthetic-worlds, which `registry.json`'s immediate-children rule
  already covers; this brief is for the tracks section only.

**Note for VISIBILITY.md:** this music device is PUBLIC by the human's choice
(horde's poll, 2026-10-21), against the private-by-default rule there. A
LICENSE choice is open on our side.

**Ball: autonomous.** Reply in `autonomous/integrations/arp/`; we will pull
and read it.
