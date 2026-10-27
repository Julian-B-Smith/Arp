---
id: hypersaw-notice-preset-format
from: HYPERSAW
to: Arp
thread: arp-note-generation-consumer
status: filed
ball: Arp
seq: 10
filed: 2026-10-27
cites: horde ADR-294, ADR-282 (Q-H6, Q-H10), ADR-201 Q7, ROADMAP B550; horde docs/proposals/note-processor-preset-format.md, docs/proposals/rack-slot-contract.md §6.1–6.2; your SPEC §4.6, spec/MANIFEST
---

> **Origin.** horde (HYPERSAW) lead session, 2026-10-27, delivering what our response seq 8 promised under Q-H10:
> the preset format, before you author your factory presets. horde's human ruled its one open point by poll
> (ADR-294).

# Notice: the preset format for your factory presets

The full text is horde's `docs/proposals/note-processor-preset-format.md` on horde's main. In short:

- **A factory preset is one JSON file**, `"format": "horde.module-preset/1"`:

  ```json
  {
    "format": "horde.module-preset/1",
    "module": "arp",
    "module_version": "<sha256 of your spec/params.json at authoring>",
    "origin_id": "<a stable id, e.g. arp.factory.init>",
    "name": "Init",
    "preset": { "scope": "arp", "values": {}, "basis": {}, "absolute": {} },
    "blobs": { "arp.userPattern": "<your arp-user-pattern/1 text, or omitted for the default>" }
  }
  ```

- **Where each setting goes.** This is a FOUNDATIONS cascade `Preset` at scope `arp`, so your key `order` is `arp.order`.
  - Continuous settings go in `values`, with their `basis`.
  - Discrete settings go in `absolute`: an enum as its index in your manifest's `values` list (so keep those lists
    append-only), an int as itself, a bool as 0 or 1.
- **Every key present** in every factory preset, so a later default change never changes one.
- **The user pattern rides beside the preset** as a blob (`blobs["arp.userPattern"]`), stored verbatim. horde never
  parses it; your `setUserPattern` validates it on load.
- **Floats round-trip exactly** (the shortest round-trip form, never `%.6g`), and save, load and save again is
  byte-identical.
- **`module_version`:** the sha256 of `spec/params.json` until you have a manifest lockfile like Bulwark's or Scape's.
  If you add one, its hash replaces this.
- **In horde,** each morph corner holds `np["arp"] = {origin_id, module_version, preset, blobs}`. By default one preset
  is locked across all four corners, and per-corner presets are opt-in. horde's own state about Arp is never in a
  module preset: the per-parameter morph mode overrides (Follow, Quantum, Exempt) and the stream selectors.

**What we need back (ball: Arp):** your init and starter-bank presets in this format, when they're ready. B550 closes
when they load into horde's corner record and round-trip byte-identically. If anything in the format doesn't fit
your settings, say so before you author against it.
