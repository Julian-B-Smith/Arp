# Spec review r1 — the independent critic's findings and their disposition

- **Reviewer:** the `critic` agent (Opus, pinned), 2026-10-21. It was sourced
  separately from the author (the lead) and was read-only.
- **Verdict on r1:** REWORK. "Not ratifiable yet … all fixes are small and
  local."
- **Arithmetic it ran:** `ceil_div` partition invariance, held at 44.1, 48
  and 192 kHz, at 120, 123.457 and 61 bpm, under random block mixes; and the
  drift and overflow figures quoted below.

Every finding is folded into `spec/SPEC.md` **DRAFT r2**. The disposition of
each is listed here so a later reader can check that none was dropped.

| # | Sev | Finding (short) | r2 disposition |
|---|---|---|---|
| H1 | HIGH | "use pred" never re-syncs; milli-bpm error drifts past 2·inc in about 12.5 s, causing false jumps (19 on a tempo ramp; Logic rounding at 192 kHz) | §2.2 three bands: Δ=0 continuous / ≤ 1/16 beat silent re-base / else jump; the §4.1 once-guard keeps steps exactly-once; G5 test |
| H2 | HIGH | on-tick anchored at k·L (in the past under restart=chord) | §5.1: on = kAbs·L + swing |
| H3 | HIGH | ties never engage at gate < 100, and at 100 offs-first kills them; tie on chord or ratchet undefined | §5.2 look-ahead ties (prob-independent); extends all chord notes and the last ratchet sub-note; K1 test |
| H4 | HIGH | 1-tick notes put the off before their own on at one offset | §1.1: an off is never at its own on's sample; deferred 1 sample; N1 plant |
| H5 | HIGH | the 1024 cap breaks N1 (a 32-key chord at 1/64 ×4 can exceed it); tempo unbounded | §1.3: cap max(1024, 4·maxBlock) with 128 reserved for offs; refuse ons (T13); tempo [20, 999]; maxBlock ≤ 4096; C1 test |
| H6 | HIGH | swung or ratcheted ons in later blocks; settings snapshot ambiguous; release-all vs pending | §4.3/§5.1: evaluate once in the block holding the grid tick, freeze into a pending queue (512); release-all clears it |
| H7 | HIGH | free clock: re-anchoring breaks sounding offs; no anchor when switching with keys held; draws repeat each chord | §2.4: re-base shifts pending positions exactly (T14); start at the switch sample; `epoch` in the draw |
| H8 | HIGH | "recompute m from grid" undefined | §4.5: m := 0 at every anchor |
| H9 | HIGH | bar restart: 10 of 22 rates don't divide a bar; no time signature | §4.5: first grid step at or after a bar start; timeSig in the snapshot (default 4/4) |
| H10 | HIGH | keys outside 0–127 (octaves, oct offset, quantize) | §4.3 drop (counted, T16); quantize searches 0–127 |
| H11 | HIGH | triggers and pattern clock with H empty undefined; bar_trigger vs trigger_pulse inconsistent | §2.5 and §4.3 step 4: steps evaluate when the clock runs and an anchor exists; triggers fire with e = 0 |
| H12 | HIGH | does a latch-replace re-press count as a new gesture? | §3: a gesture start is a NoteOn with no DOWN/PEDAL ref, so yes |
| H13 | HIGH | `enabled` defined twice; timing; does H track while disabled? | `enabled` setting only, applied at offset 0 (T4/T5); H tracks while disabled |
| M | MED | shuffle key collision (c·64) | c·256 + i |
| M | MED | unbounded refs per key; unknown-ref NoteOffs | ≤ 4 refs per key; unknown/ignored refs' offs ignored |
| M | MED | ratchet sub-length rounding; swing + ratchet overlap | floor first; sub-steps divide the swung **span** |
| M | MED | number formats, velocity rounding | integer settings (swing in ‰); fdiv(base·vel + 50, 100) |
| M | MED | 128-bit storage, integer sample rate | stored as 128-bit; sample rate rounded at prepare |
| M | MED | rate change under chord; long-rate lag; no hysteresis | re-anchor on rate change (T15); lag accepted and audible; Q-A1 hysteresis 0.05 |
| M | MED | E unsorted and duplicated for octaves > 1 | E = sorted unique; `played` keeps press-order blocks |
| M | MED | T2 key vs (channel, key); collision source | (channel, key); earliest source wins |
| M | MED | contract: global pedal vs VO-12 per channel/zone; panic swallowed; expressions; release velocity | panic forwarded after offs (T7); pass-through of non-note events; releaseVel 64; per-channel/MPE pedal and expression mapping → Q-H4 (after 1.0) |
| M | MED | no sample-rate invariant despite the charter | G4: event ticks identical across 44.1, 48, 96 and 192 kHz |
| L | LOW | negative `/`, `%` | conventions block: fdiv/cdiv/mod defined mathematically |
| L | LOW | order within one offset | §1.1 full order |
| L | LOW | first-press order after the earliest ref is released | §3: press sample of the earliest **live** ref |
| L | LOW | D1 control needs a seeded pattern | D1 now names random/shuffle/prob |

**The critic's question for the human** ("follow the host or keep our own
count?") is answered by H1's three bands: follow the host silently within
1/16 beat; beyond that, it is a jump. Recorded as D-008, PROPOSED, for the
human to confirm at spec ratification.
