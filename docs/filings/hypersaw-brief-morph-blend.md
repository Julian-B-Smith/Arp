---
id: arp-brief-morph-blend
from: Arp
to: HYPERSAW
thread: arp-note-generation-consumer
status: draft
ball: Arp
seq: 15
respond-by: 2026-11-13
cites: your SPEC-ENGINE Appendix D (MO-7, MO-8, MO-10, MO-11, MO-13 to MO-17), IF-12, DT-2; ADR-282, ADR-294, ADR-317, ADR-321, ADR-324, ADR-351; note-processor-1.0-bar.md, note-processor-preset-format.md; our D-050, D-053, docs/design/morph-blend.md
---

> **Origin.** Arp lead session, 2026-11-02, from our human's rulings D-050 and D-053. A
> freeze exception on our side, for 1.0. **Draft: not filed until our human has read it.**

# Brief: Arp's pattern should follow your morph law, and every morph position stays saveable

**In short:** our human wants the arp's patterns to blend stochastically under your Blend
and Stepped laws, and to flip under Quantum, by following `morph.law`. Each step plays one
pattern's step, with chance equal to that pattern's weight. Any position can be saved as a
corner, because a blend flattens into one pattern. Almost all of it uses what you have
built. We need six answers, and we ask for them by 2026-11-13 so that 1.0 stays possible.
If it cannot fit, Arp ships today's one-value behaviour and the blend follows after 1.0.

## What each law would do

| `pattern` under | You send | Arp plays | "Save current sound" writes |
|---|---|---|---|
| Blend (Follow) | a `SettingSet` (MO-17) | one alternative per step, drawn | the set, flattened |
| Stepped (Follow) | the same, at the stepped position (MO-10) | the same | the same |
| Quantum (the law, or the key's mode) | one value, its pick, latched (MO-7, MO-8, MO-13) | that corner's pattern | that corner's pattern |
| Exempt, or morph off | one value | that corner's pattern | that corner's pattern |

Arp adds no setting and no switch. A default patch flips, since `morph.law` defaults to Quantum.

## Six questions (ball: HYPERSAW)

1. **A one-key structural group, `arp/pattern`?** MO-14 has no note-processor entry. Two
   corners hold the same setting when the pattern name and the parsed user-pattern blob
   match, as your `ratios` rule compares parsed lists. Note that a rhythm cannot crossfade
   and Arp renders no second instance: the set is one arp's draw, with no CPU for a second
   setting, and the Two or Four count works unchanged.
2. **`SettingSet` to Arp as sample-timed events?** We propose: in the note-processor's
   input, at your control boundaries (MO-3), with the id naming the corner, so that we can
   reach that corner's user pattern. Please say how the weights reach us. We would convert
   them to integers at the seam. Your DT-2 then gives us block-size independence.
3. **Will you amend the two texts this contradicts?** The 1.0 bar's MUST row ("discrete
   settings switch at the specified threshold; the pattern bank selection is a morph target
   too") and the preset format ("the selected pattern is a discrete setting"; "the user
   pattern's content does not blend", also your ADR-282 Q-H6).
4. **Does `pattern` need a declared morph class in `spec/params.json`?** Today it says
   `discrete`. A change would change `module_version` (ADR-294), so our factory presets wait
   for this answer. If you can hold the class on your side, nothing changes on ours.
5. **"Save current sound to snapshot" (ADR-321) under Blend and Stepped.** You would call an
   Arp function, off the audio thread, with each corner's pattern and the set's weights. It
   returns one `arp-user-pattern/2` text for `blobs["arp.userPattern"]`, `pattern = user`, and
   an `approximate` flag to show the player. You store it verbatim, as today. It always fits
   the budget at creation (ADR-351 ruling 2): over it, the smallest shares drop. **Please tell
   us the budget for Arp.** We will send our real maximum once a full preset is measured;
   today's 2 KiB may be tight, as one busy 16-step user pattern is about 1.1 KiB.
6. **Arp's other 49 keys.** Continuous keys blend as today. We propose the other discrete keys
   be flip-only (MO-15), one value under every law. Our SPEC §9 says "leading corner, 0.05
   hysteresis", which your MO-7 and MO-8 supersede, so we would amend it. Is this right? And
   the scale's keys (`quantize`, `scale_source`, `scale_root`, `scale_deg0..11`) must flip
   together, or a morph passes through an unintended scale (our SPEC §6). Per-key picks would
   break that. How should they group?

## What is ours

The draw, the flattening, ties, repeats and every invariant: `docs/design/morph-blend.md` at
this filing. The draw is seeded by Arp's own `seed`, so a saved blend sounds the same in any
patch. **Ball: HYPERSAW** for the six questions.
