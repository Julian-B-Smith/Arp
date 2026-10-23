# Spec review r4: the independent critic's findings on r2.10 (repeats, the chord stage), and their disposition

- **Reviewer:** the `critic` agent (Opus, pinned), 2026-10-22. It was
  sourced separately from the author and was read-only.
- **Verdict on r2.10:** REWORK. §14 had three structural holes. The §4.8
  formula was correct: 0 mismatches against brute force in 2,000 random
  cases at prob 100.

All findings are folded into **SPEC r2.11** (D-019). The human answered the
critic's two questions by poll: **chord notes behave like held notes**, and
**any new press restarts the repeat count**.

| # | Sev | Finding (short) | r2.11 disposition |
|---|---|---|---|
| H1 | HIGH | the chord mapping was "a pure function", but the `chord_root` anchor and the tie's `lastQ` move with H, so a release could decrement the wrong key and strand a note (sim: `sounding {66: 1}`, count −1) | the mapping is **stored per (held key, slot)** when the key enters H, and release decrements the stored keys; re-map only at named points; chord-stage ties go lower and never touch `lastQ` |
| H2 | HIGH | T2 ignored the stream, so with both streams on every arp note cut the chord note on its key | T2 applies **within one stream**; the two streams may share a key, told apart by id |
| H3 | HIGH | "arp off, chord on" was impossible with one `enabled` | new **`arp_enabled`**; `enabled` is the whole module (covered by the human's poll answer) |
| H4 | HIGH | an exhausted arp stopped the once-guard, so the next gesture could burst overdue steps | spent steps still consume the once-guard as silent rests |
| M1 | MED | morphs could revive an exhausted arp; under latch append or the pedal the arp could stay silent while keys were added | an `exhausted` flag, cleared at a gesture start, **any key-adding press** (the human's ruling), T6, T7 or T10 |
| M2 | MED | dedupe by (channel, key) against H keyed by key; MPE would duplicate keys | dedupe by **key**; channel and velocity fixed at NoteOn; H′'s Pp and velocities defined |
| M3 | MED | the T6 restart retriggered held chords at every loop wrap; same-sample overlaps | chord notes ignore T6 (the held-notes ruling); 1→0→1 within one sample keeps the note |
| M4 | MED | when chord changes apply was inconsistent with T8 and §6; Q-A8 missing; enabling with keys held unspecified | chord and scale settings apply at offset 0 like `enabled`; enabling maps every held key; Q-A8 added to §12 |
| M5 | MED | "no scale active", the scale chord_quantize uses, whether `h` moves, and the anchor were ambiguous | the effective scale per §6 (zero mask is chromatic); the +0 slot is never moved; degrees start from h's nearest in-scale key; `chord_root` anchors on the played keys H |
| M6 | MED | the 224 cap could never overflow, while the shared 128-note table let arp notes evict chord notes | the chord stage has **its own** 224-entry sounding table |
| L1 | LOW | S1 and §1's block output were stale | §1 updated; S1 already checks both tags |
| L2 | LOW | chord ons versus arp ons at one offset were unordered | chord first |
| L3 | LOW | RP1's "N full cycles" is false under bar, prob or a changing H | reworded as "repeats·C evaluated steps" |
| L4 | LOW | P should come from H′ | done |

The lab build's SPEC-GAPs 1 (the arp toggle silenced held chords) and 4
(the chord's own `lastQ` chain) are superseded by the held-notes ruling and
by H1. Gaps 2, 3, 5 and 7 are adopted in §14. Gap 6 (latch-off at the next
arp step) is superseded by M4's offset-0 rule.
