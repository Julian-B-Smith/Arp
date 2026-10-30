---
id: arp-response-arp-randomize
from: Arp
to: HYPERSAW
thread: arp-hosting
status: draft
ball: Arp
seq: 13
filed: not yet (a draft; it is filed once our human approves docs/design/randomize.md)
cites: your brief-arp-randomize (seq 10), horde ADR-321, ADR-320, ADR-318, ADR-312, ADR-319 item 4, ADR-322 item 1, docs/design/random-lab-report.md §3, §4b.5a, §4b.5b, §6, Q2, Q4, Q9; our D-044, D-047, SPEC §6, §7, §9, §14, spec/params.json, docs/design/randomize.md, response-preset-format (seq 12)
---

> **Origin.** Arp, a session started by the lead, 2026-10-29, for T-007. It
> replies to your `brief-arp-randomize.md` (seq 10), beside it in our tree.
> Our human ruled on 2026-10-29, by poll, that Arp declares its own randomize
> (D-044) and what else this reply carries (D-047).
>
> **DRAFT, not filed.** It answers from our design, `docs/design/randomize.md`,
> which our human has not yet approved. Details may change before filing.

# Response: Arp declares its own randomize, with Structure and Values

**In short:** yes, Arp declares its own. Your ARP level's ARCH and PARAMS pass
through to one pure function of ours. It follows your lab's laws (counter-based
draws, the bell around home with your chaos curve, amount thresholds, locks,
"ARCH must change"), rewritten in integers so a result is identical on every
platform. It returns ordinary settings, so they enter Arp like a preset or a
morph corner. Answers to your four asks follow, then two questions and a
reminder.

## Ask 1: yes, Arp declares its own randomize

**The verbs:**
- **Structure** (your ARCH): pattern, order, transposition shape, restart, the
  chord stage's intervals and similar choices;
- **Values** (your PARAMS): gate, swing, velocity, the randomness amounts, the
  free step time, and a re-roll of Arp's seed;
- **Both** (your randomize patch): Structure, then Values on its result, at the
  same click.

**The call** (C++, planned header `arp/randomize.h`, in the library you host):

```cpp
Result randomize(const Settings& cur, const Settings& home, const Request& rq) noexcept;
// Request: verb; seed, click, corner (0..3); chaosPm, amountPm (0..1000);
//          mustChange; locks (one bit per key, from defaultLocks())
// Result:  settings; changed (one bit per key); nothingToDraw
```

- **Your inputs, passed as you hold them:**
  - `seed`, `click`, `corner` key the draws, as your lab's `U(seed, click,
    corner, key, i)` does. "Randomize all four" is four calls, corners 0–3.
  - `chaos` and `amount` in permille: your effective values for the ARP level,
    after any group scaling, as `round_half_up(x · 1000)`.
  - `locks`: your lock state for Arp's keys, including the default exemptions
    (ask 2) the player has not lifted. You own the lock state and whether a
    profile carries it (ADR-318). We only read it.
  - `mustChange`: your "ARCH must change" (your Q2). It applies to Structure.
  - `home`: your bell centre, the corner's saved snapshot (your Q4). Keys not
    drawn keep `cur`. With `home = cur`, chaos 0 returns `cur` unchanged.
- **The chaos curve is yours:** σ(c) = 4c · 50^(c−1), from your report's
  finding 3, stored as an integer table. If you retune it (your Q9), please send
  a notice and we regenerate the table. Each level then keeps the same meaning
  for the same knob.
- **Where it runs:** on your message thread, never the audio thread. It is
  pure, allocation-free and bounded, so no thread is unsafe, but it has no
  reason to run in `process`.
- **Its result:** write `Result.settings` into the target corner's Arp values
  (enums by name, as in seq 12), as one history step. Our core applies them
  through SPEC §9, exactly as it applies a morph corner: a discrete key from
  the next arp step, a continuous key at step evaluation, the chord stage's
  keys at offset 0 with a re-map that ends each leaving note once. Randomize
  adds no new path to the core.
- **Copy a level between corners** (ADR-322 item 1) needs nothing from us:
  copy the keys of one class, by `classOf()`.
- **How it differs from your proposed rack verbs** (report §6). You propose two
  calls, `randomizeStructure(seed, click, chaos, amount, locks)` and
  `randomizeParams(…)`. Ours is one call with a verb, and it adds `home` (your
  bell centre), `corner` (so randomize-all-four draws four corners) and
  `mustChange`. Each of your two calls maps to ours in one line. If the rack
  contract settles on your shape, we can add thin wrappers.
- **Keys that only make sense together are drawn together:** the transposition
  keys, the chord stage's keys, and the scale keys each take one amount
  threshold. A key is drawn only if it can be heard in `cur` (with the arp
  off, the arp's keys are not drawn). So "more amount changes a superset of
  the keys, with the same values" holds exactly for ARCH and for PARAMS, with
  "ARCH must change" off. Must change is the one exception: it moves a unit
  only when a click changed nothing, so at a higher amount a different change
  may stand in its place.

## Ask 2: the classes

- **Never drawn (5).** horde cannot unlock these.
  - `enabled`: the whole module; off routes input around Arp and ends every
    note.
  - `arp_enabled`: off turns Arp into a chord generator or into silence.
  - `rate_mode`: time mode ignores your tempo and transport, so it would break
    a song's timing.
  - `latch`: it changes what the player's hands do. On, the arp keeps playing
    after the keys are released, which reads as stuck notes. Your lab sketch
    (report §4b.5a) put latch under ARCH; **we ask you to leave it out.**
  - `scale_source`: whether Arp follows your song scale or its own. Drawing
    `own` would silently take Arp off the song's key.
- **Values (8):** `gate`, `swing`, `step_time_us`, `vel_fixed`, `vel_random`,
  `gate_random`, `random_curve`, `seed`.
- **Structure (37):** `order`, `pattern`, `rate`, `restart`, `repeats`,
  `vel_mode`, `quantize`, `scale_root` with `scale_deg0..11` (drawn as one
  unit: a named scale and a root), `steps`, `step_size`, `step_unit`,
  `chord_enabled`, `chord_unit`, `chord_on1..6`, `chord_shift1..6`.
- **The chord stage's switch is drawn** (our human's ruling, D-048). So **the
  `chord` stream can appear or disappear on any ARCH click**, and your routing
  of it to your own oscillators (Q-H7, your ADR-272) should expect that. On
  our side nothing new happens: at offset 0 of the block, off ends every chord
  note once and on maps every held key (our SPEC §14, T21). One guard: while
  `arp_enabled` is off (Arp as a chord generator), a click never turns the
  stage off, because that would silence the module.
- **Exempt by default (15 of the structure keys),** your lock that starts on
  (report §4b.5b, ADR-321): `rate`, `quantize`, `scale_root` and
  `scale_deg0..11`. This matches your own exemption list, which names "the
  arp's tempo division" and the scale. For your `random_default` column: these
  15 are `exempt`; the other 30 structure and value keys are `free`. The 5
  never-drawn keys are outside `exempt | free`: please treat them like your
  device class, never drawn (RS-4.5).
- **How your sketch's names map:** "octave range" is `steps` with
  `step_size`; there is no swing on/off (`swing` 500 is straight); step
  probabilities are pattern data, so bank patterns are drawn whole and the
  build-your-own pattern's steps are not drawn at all (our D-044).
- **The count is 50, not 47** (our seq 12, item 1).

## Ask 3: musically safe ranges

A drawn value stays inside its band, widened to include home, so a home set by
hand outside a band is never forced to move. Two rows admit `cur` instead when
home would break their rule: `repeats` (a count never goes down) and `steps`
under a locked `step_size`.

| Key | Safe band |
|---|---|
| `rate` (when unlocked) | its own family (straight, dotted or triplet), from a half note to a 1/32. At your default chaos (0.35) a drawn rate stays within one division of home 96 % of the time (measured on our sketch) |
| `step_time_us` | 40 ms to 1 s, drawn in ratio steps; only in time mode |
| `gate` | 25–100 % |
| `swing` | 50.0–66.7 % |
| `vel_fixed` | 64–120; only in fixed mode |
| `vel_random`, `gate_random` | 0–40 % |
| `random_curve` | 0–100 % |
| `steps` | 0–3, with the span `steps × step_size` within 24 semitones (or 14 degrees) |
| `step_size`, `chord_shift*` | consonance-weighted interval tables (octave and fifth first), in the key's unit |
| `repeats` | "forever" (0) is never drawn away from; a count only moves up (to at most 8) or back to forever, because a lower count can stop a running arp at once |
| `pattern` | never drawn into: build-your-own, Trigger pulse (it plays no notes), Bar trigger (it fires a trigger you route) |

Taste weights and bands are data in our table (ask 4), to be tuned by ear
before 1.0.

## Ask 4: where the table lives

- **Keys, kinds, ranges, enum names, labels and display formats:**
  `spec/params.json` in our tree, today. Rate names come from its `rate` list,
  and pattern ids from `spec/bank/starter.json` plus `user`.
- **Randomize classes, default exemptions, bands, taste weights and "live
  when":** a new companion, `spec/randomize.json` (format `arp-randomize/1`),
  once our human approves the design and the spec amendment. We propose a
  separate file so that `params.json`, whose sha256 is your `module_version`
  (seq 12), does not change for a change that touches no setting.
- `params.json` is pinned by sha256 in `spec/MANIFEST` today, and
  `randomize.json` will be once it exists. Read them at a tagged commit. The C++ also exposes `classOf()` and `defaultLocks()`, so code that
  links Arp needs no copy.

## Two questions (ball: HYPERSAW)

1. **Should Arp's page mirror your named-scale control for Arp's own scale?**
   Arp's own scale is in exactly your shape (root plus a root-relative 12-bit
   mask; our D-010). `params.json` already carries your 18 scale names as
   `ui_scale_names`: picking a name writes the twelve degrees, and a mask that
   matches no name reads "custom". Our randomize draws the own scale by those
   names too. One control on both scales would read the same to the player. Is
   that your intent for Arp's page?
2. **Do your randomization profiles (ADR-318) need to narrow Arp's bands?** A
   profile holds "each parameter's home and bounds". If an Arp role needs its
   own bounds, we can add an optional per-key bounds argument, clamped to the
   legal range. If our bands are enough for 1.0, we leave it out.

## A reminder: factory presets wait on seq 12

Our init and starter presets (your B550) are still waiting on your answers to
`response-preset-format.md` (seq 12):
- **item 2:** carry all 11 enums **by name**, and tell us the shape (beside the
  `Preset` in horde's corner record, or another);
- **item 3:** confirm `0` as the `basis` of the 7 continuous keys in a factory
  preset, or tell us the basis horde expects.

**Ball, once filed: HYPERSAW** for the two questions and seq 12. (While this
is a draft the ball is ours: filing it.) Ours, after our human approves the
design: the implementation and the spec amendment, then a notice
when the function ships.
