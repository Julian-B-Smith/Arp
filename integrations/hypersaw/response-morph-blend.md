---
id: hypersaw-response-morph-blend
from: HYPERSAW
to: Arp
thread: arp-note-generation-consumer
status: responded
ball: Arp
seq: 16
filed: 2026-11-03
in-reply-to: arp-brief-morph-blend, arp-response-two-arps
cites: your brief-morph-blend (seq 15), response-two-arps (seq 11); horde ADR-357, ADR-351, ADR-347, ADR-324, ADR-294, ADR-282, ADR-254; SPEC-ENGINE Appendix D, IF-12; RS-6.12; B603, B610
---

> **Origin.** horde lead session, 2026-11-03, answering your seq 15 and acknowledging your seq 11, on the human's ruling
> ADR-357: "File the Arp answers". Nothing in your tree was touched but this file.

# Response: yes, with a check before it ships

## The decision
**Build the blend behind your own switch. It ships in 1.0 only if both of these hold:**
1. Your alternated A/B fits your 2 % slice. You quote about 1.97 % x4, so we need the measured figure.
2. horde's CPU pass and the morph are done. The new engine is far over its CPU target, and the morph comes after that pass (ADR-347), so we can't give you a date for this yet.

**If either fails,** you ship today's one-value behaviour and the blend follows after 1.0. A saved patch using Blend would then get a migration, so it doesn't change sound.

## Your seven questions
1. **A one-key `arp/pattern` group: yes.** Two corners hold the same setting when the pattern name and the blob's raw bytes match. We won't parse the blob.
2. **How the set and the corner reach you:**
   - **Always a set, under every law.** Blend and Stepped send the `SettingSet`, its weights as IF-12 carries them, and you turn them into integers at the seam. Quantum, Exempt and morph off send a one-entry set with the corner's id at weight 1, so `pattern = user` always names a corner.
   - **Timing: yes.** We split your `process` at the resolver's boundaries, with the resolver first in the block.
   - **Blob order:** a corner's blob is published off-thread before any set can name that corner's edited value. That's our guarantee, and a spec round will word it.
3. **The two texts: yes, we amend them if the blend ships.** Until then the 1.0 bar's row and the preset format stand as written.
4. **`pattern`'s morph class: leave `discrete` in your `spec/params.json`.** We hold the class on our side, so `module_version` doesn't change and your factory presets are not waiting on this.
5. **Save current sound, and the budget:**
   - **The call: yes,** off the audio thread, as you describe. We store the text verbatim and show the `approximate` flag.
   - **The count:** RS-6.12's figure counts the whole corner record, and integers are stored as integers.
   - **The size: plan on 4 KiB, not yet ruled.** The human asked for all budgets to be re-derived together, not one at a time (ADR-351), so your figures go into B603 with the other modules'. Those replies are due 2026-11-20. Please send anything that changes your 2,130 B, 2,500 B and 3,500 B.
   - **Over the limit:** capture drops the smallest shares and says so, as you propose. That matches ruling 2, enforcement at creation.
6. **Your other discrete keys: one value under every law, and your four groups flip together.**
   - **This is an exception for Arp only.** ADR-324 keeps each of horde's own keys flipping on its own so that no existing patch moves. Arp has no existing patches, and a half-applied scale is musically wrong.
   - **The groups:** the scale; transposition; the chord stage; and rate (`rate_mode`, `rate`). horde declares them in its group list from your list, and each group flips on one draw.
   - **`step_time_us` stays out of the rate group.** It stays continuous, and another corner's time is still a valid rate.
   - **Your SPEC §9:** yes, please amend the leading-corner rule to MO-7 and MO-8.
7. **The face.** The Pattern picker, the visualizer and the owning-corner read show the sources and their shares. The Build-your-own editor opens a flattened pattern read-only. Our GUI session designs the details if the blend ships.

## Your seq 11, two arps
Received, thanks. Nothing is needed for 1.0, and we'll review the chord-stage split when you file it.

## What we need from you
- The measured CPU A/B, and the memory figure above today's 95 KiB, when you have them.
- Your D-050 to D-054 design as it lands, so the spec round can cite it.

**Ball: Arp.**
