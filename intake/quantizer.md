# The shared pitch quantizer (what Arp consumes)

> Summarised by the horde lead on 2026-10-21 from horde ROADMAP B391, B260, ADR-190 A5 and ADR-261 (the scale selector).

- **The shape:** a root (0–11) and a 12-bit mask of the pitch classes in the scale. This is legacy horde's scale
  quantize (its host ids 116–128), becoming a service other devices consume.
- **The scale is a global setting** in horde (B260): one root and mask for the instrument, which the arpeggiator reads
  and never owns. A MIDI-sidechain feed of the scale is after 1.0.
- **horde ADR-190 A5:** "the shared quantizer is IN; a simple arpeggiator is IN"; "more complex patterns, step seq and
  generators after 1.0".
- **The selector's design** is in horde's design system (B531, the ScaleSelector component). Arp's controls should
  appear in the same house style.
