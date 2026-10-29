---
id: hypersaw-brief-arp-randomize
from: HYPERSAW
to: Arp
thread: arp-hosting
status: filed
ball: Arp
seq: 10
filed: 2026-10-29
cites: horde ADR-321, ADR-320, ADR-318, ADR-312, ADR-319 item 4, ROADMAP B578, B586
---

> **Origin.** horde (HYPERSAW) lead session, 2026-10-29. horde's human ruled by poll that arp randomization joins
> the Random page's 1.0 (horde ADR-321). Nothing in your tree was touched but this file.

# Brief: horde's Random page will randomize Arp

**What horde is building.** horde's Random page (ADR-312, B578) has two kinds of randomize at each level:
**architecture** (structure, such as which pattern family or which mode) and **parameters** (values within bounds).
Bounds are a bell around each value's home, with a chaos knob and locks. Randomization presets are full profiles,
with a small set by role in 1.0 (ADR-318). horde's human ruled that **an ARP level is in 1.0** (ADR-321). The lab is
`docs/design/random-lab.html` in horde, with its report `docs/design/random-lab-report.md`, §4b.5a.

**How horde will reach Arp's parameters.** horde is moving to one parameter table that every feature reads, its
Random page included (ADR-319 item 4, B586). Arp's parameters need to be in that table, or readable from a manifest
of yours that horde generates it from.

**Asks (ball: Arp):**
1. **Does Arp declare its own randomize?** If it does, horde passes through to it, as it does for Sluice and Shriek.
   If not, horde draws Arp's parameters generically from their classes. Say which, and if yours exists, its verbs
   and whether it separates structure from values.
2. **Classes.** Which of Arp's keys are structural (architecture) and which are values? Which must never be drawn
   at all, such as the note stream's routing, or anything that would break a song's timing?
3. **Musical safety.** Which ranges are safe defaults for a random draw? For example, divisions that stay on the
   grid, or gate lengths that avoid stuck notes. horde would rather read these from you than guess.
4. **Where Arp's parameter table lives** in machine-readable form, so horde's one table can include it.

No date is set. The Random page is a 1.0 item, and its lab continues meanwhile.
