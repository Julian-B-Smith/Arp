# Stochastic pattern blending under morph — design (draft, for the human's answers)

> **Origin.** Arp lead session, 2026-11-02, for T-007 and D-050 (the human's
> freeze exception, 2026-10-30), after the human heard the lab prototype and
> chose to keep it (D-051). Read alongside SPEC §4.3, §5.2, §9, §10 and horde's
> seq 14 (`integrations/hypersaw/response-preset-format-and-randomize.md`).
>
> **Status: ANSWERED DRAFT.** The human answered §5 by poll on 2026-11-02 (D-052).
> Nothing here is in `spec/`. A critic reviews this version before the spec
> amendment; §2 and §6 still need horde.

## 1. What it is

Today horde resolves `pattern` to one value and Arp plays it. With a blend, each
step plays one corner's pattern step, and a corner's chance is its morph weight.
Only the step pattern blends (D-050). The order style, rate and every other
discrete setting still arrive as one value.

## 2. How it fits horde's morph laws (a finding, not yet agreed with horde)

SPEC §9 records one rule for discrete settings: the leading corner, with 0.05
hysteresis (D-021). horde's morph has moved on since:

- **Three laws and a menu.** horde has Blend, Quantum and a stepped hybrid, and
  each parameter has a menu of Follow, Quantum or Exempt (horde ADR-291 item 3,
  B557, whose text expects it to matter most for Arp's settings).
- **Architecture follows the law** (horde ADR-317): under Blend, a structural
  difference crossfades; under Quantum it flips at the parameter's own seeded
  boundary (ADR-104, ADR-325 X32); under the stepped hybrid it steps.
- **The weights** are the whole pad's bilinear weights with no band (ADR-324 H4),
  and may be limited to two settings or four (H1).
- **The latch** is now a 0.1 band (ADR-325 X33), not §9's 0.05.

Nothing in horde's text says what these laws do to a hosted note processor's
discrete settings. A rhythm cannot crossfade the way a timbre can, and running
two arps would double the notes. So the proposal:

> **The per-step draw is what the Blend law means for Arp's `pattern`.**

| `pattern`'s law in horde | What horde sends | What Arp does |
|---|---|---|
| Blend | each corner's pattern and weight | draws per step (this design) |
| Stepped hybrid | the same, with stepped weights | draws per step; the mix steps |
| Quantum | one pattern, weight 100 % | plays it (today's behaviour) |
| Exempt | one pattern, weight 100 % | plays it (today's behaviour) |

**What this buys:** horde's existing menu is the on/off switch, so Arp needs no
new setting. `spec/params.json` does not change, so `module_version` does not
change, and the factory presets are not blocked by this work.

**What it costs:** two facts only horde can confirm (§6). Also, horde locks one
preset across all four corners by default (RS-6.3, seq 10), so the blend has
nothing to do until someone gives corners different patterns.

## 3. Forced rules (no choice, given "at 100 % a corner sounds exactly like that pattern alone")

- **Each pattern keeps its own position:** `j = mod(k, that pattern's length)`.
  A shared position could not match the pattern alone at the corner.
- **One order stream:** `m` is shared, so the order style carries on whichever
  corner plays the step. It is one arp, not four.
- **A pattern's `step_size` and `step_unit` travel with it** (SPEC §4.3), because
  they are pattern data. The global transposition settings do not blend.
- **A single non-zero weight makes no draw,** so the output is byte-identical to
  today's. This is a gate (criterion 3 in the ROADMAP).
- **The weights are read like any setting:** from the snapshot of the block whose
  window holds the step (H6). horde's glide happens before Arp sees them (X31, X35).
- **A corner with weight 0 can never win.**
- **Repeats (§4.8):** `C` comes from the leading corner's pattern, recomputed at
  each step as it already is. Under a blend, "one repeat" is therefore approximate.

## 4. Rules, each put to the human (§5)

**4.1 Ties.** A tie is honoured by look-ahead (SPEC §5.2), and the draw is a pure
function of the step, so look-ahead can ask which corner will win each later step.
The prototype looks ahead with today's weight. If the weight then moves, a step
the note was tied across can play a new note, or a step can fall silent.
**Chosen: the note keeps its promise.** The playing step counts `t` tie steps
and the core holds them: the next `t` steps are ties whatever is drawn. A tie
step reached outside a hold acts as a rest, as a tie after a rest already does
(§5.2). The hold is plain state, so it stays deterministic and block-independent.

**4.2 The draw, for up to four corners.** The prototype's one draw against
thresholds works for two. With four, a step's corner can jump between two corners
whose weights did not rise. **Chosen: a race.** Each corner gets its own draw on
lane 5, turned into an exponential time `E` by a fixed integer table, and the
lowest `E / weight` wins, compared by cross-multiplying, so no float and no
division. Ties go to the lower corner. Raising a corner's weight can then only
move steps **to** that corner, so moving the puck evolves the rhythm instead of
reshuffling it. It is the same law as horde's Quantum draw (ADR-104) at
temperature 1, applied per step. Cost: four draws per step instead of one.

**4.3 User patterns.** The core holds one active user pattern (§4.6), but horde
lets each corner carry its own (seq 8, ADR-169). A blend can need them all at
once. **Chosen: four preallocated slots,** `setUserPattern` gaining a corner.
That changes a public interface.

**4.4 The curve.** A corner's chance equals its weight, a straight line. A curve
would need a new setting, which §2 avoids. Provisional unless listening says
otherwise.

## 5. The human's answers (D-052, each as proposed)

1. **On/off:** horde's per-parameter menu is the switch (§2); Arp adds no setting.
   If horde disagrees, this returns to the human.
2. **Ties:** the note keeps its promise (§4.1).
3. **The draw:** the race (§4.2).
4. **User patterns:** four slots, one per corner (§4.3).

## 6. For horde (a brief, shown to the human before it is filed)

1. Does §2's table match your intent for `pattern` under each law?
2. **Interface:** each block, alongside the snapshot, up to four (pattern, weight
   in permille) pairs. A single pair means today's behaviour.
3. How do Arp's **other** discrete settings resolve under Blend? Our reading is one
   value per IF-4, with your 0.1 latch rather than §9's 0.05. We would update §9.
4. Each corner's user pattern reaches Arp under a blend (§4.3), amending "flipping
   at the threshold" for a blended `pattern` (ADR-282 Q-H6).

## 7. Risks

- **CPU.** The tightest quoted case has a thin margin (about 1.97 % x4 against
  2 %, D-042). A race adds four draws per step, and a long tie look-ahead up to 63
  steps more. It must be measured idle in Release before the 1,000 events/s quote
  stands, and the look-ahead may need caching.
- **The cutoff, 2026-12-11.** horde's answer sets the interface. If it is late,
  Arp can ship the blend behind a single pair (today's behaviour) with nothing lost.
