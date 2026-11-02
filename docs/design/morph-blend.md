# Stochastic pattern blending under morph — design, revision 2

> **Origin.** Arp lead session, 2026-11-02, for T-007, D-050, D-052 and D-053, after
> a critic's REWORK of revision 1 and the human's contract: aside from brief
> crossfades and FX tails, every position on the morph grid can be saved as its own
> corner preset.
>
> **Status: DRAFT, waiting on horde and a critic's re-check.** Nothing here is in
> `spec/`. horde's rows are read from its origin/main at `4b2fdae` (the local
> checkout was stale). The scratch model is `docs/design/morph-blend-model.mjs`:
> it tests which step is chosen, not the core, ties or audio.

## 1. What it is

**A step can hold alternatives with chances.** A plain pattern has one alternative
per step. Blending stacks the alternatives of the corners' patterns, weighted by the
morph. Flattening saves that stack as one pattern, which can blend again by the same
process. Only the step pattern blends (D-050).

## 2. The arp follows horde's global morph setting (D-053)

Arp's `pattern`, with its user pattern, joins horde's resolver as a one-key
structural group (MO-14 has no Arp entry yet; §7). Each law then has its own rule:

| `pattern` under | What horde sends | What Arp plays | "Save current sound" writes |
|---|---|---|---|
| **Blend** (Follow) | a `SettingSet` (MO-17): distinct settings, pooled weights, an id | one alternative per step, drawn | the set, flattened |
| **Stepped** (Follow) | the same, from the stepped position (MO-10) | the same; the mix changes at each morph step | the set at the stepped position, flattened |
| **Quantum**, the default law, or Quantum on the key's menu | one value: the key's pick, latched (MO-7, MO-8, MO-13) | that corner's whole pattern | that corner's pattern, verbatim |
| **Exempt**, or morph off | corner A's, or the audible corner's | that corner's whole pattern | that corner's pattern, verbatim |

Consequences:
- **Arp adds no switch** and no setting; `morph.law` and the existing menu decide.
- **A default patch flips,** because `morph.law` defaults to Quantum. The draw is heard
  under Blend or Stepped, which act on the whole patch and make other corner differences
  blend too (ADR-317, ADR-324). This is the human's choice over a per-key Blend entry.
- **A flattened pattern sounds stochastic under every law,** since it is one corner's
  pattern. A Quantum flip between two flattened patterns swaps one distribution for another.
- **Same pattern on every corner** (horde's default, RS-6.3) gives one setting, so no draw.
- **Count** (Two or Four, MO-11, MO-17) is honoured without code here: Arp draws over
  whatever set arrives.
- **Arp's other discrete settings** stay one value under every law (D-050). We propose
  they be flip-only keys (MO-15). `order`, `rate` and the rest therefore flip per key,
  not at SPEC §9's leading corner with 0.05. SPEC §9 and §6's scale group need amending (§7).

## 3. The flattened pattern (D-053)

- **One pattern to the player, layers inside:** each source with its share, bank
  patterns by id, user patterns by value. `pattern = user`; the blob is
  `arp-user-pattern/2`, in the existing `blobs["arp.userPattern"]`. A `/1` blob is one
  layer at 100 %. horde stores it verbatim.
- **Why layers, not a step table:** a table's length is the lengths' least common
  multiple. Measured as compact text (`docs/design/morph-blend-sizes.mjs`):

| Blend | Combined length | Table | Layers |
|---|---|---|---|
| Simple user 7 + 16 | 112 | 1,823 B | 337 B |
| Busy user 15 + 16 | 240 | 5,932 B | 2,123 B |
| Busy user 16, 15, 13, 11 | 34,320 | about 1.2 MB | 3,759 B |

- **Budget (ADR-351 ruling 2):** the state may never exceed the budget at creation.
  Capture fits it: over the budget, or over 16 layers, the smallest shares drop, the rest
  rescale, and the result says `approximate` so the player is told (the human's answer).
  The budget is 2 KiB per corner today, provisional. One busy 16-step user pattern is
  about 1.1 KiB. Whether a full preset with one fits is **not measured yet**.
- **Capture** is a pure function off the audio thread: (each corner's pattern, the set's
  weights) → blob and `approximate`. **Not editable step by step in 1.0.**
- **Exact means:** puck held still, same seed, same settings, same start: the saved
  corner plays what the live position plays. A saved corner does not reproduce a moving
  puck's path. Past a limit, the save is approximate and says so.

## 4. Rules

**4.1 The operator.** One pure function, used live and by capture. At step position `k`,
for each setting with non-zero weight, take each layer's step at `k mod that layer's
length`. Canonicalise each step to its content, with its pattern's `step_size` and
`step_unit`. Merge identical contents, summing shares. Normalise to millionths by largest
remainder, so it is idempotent. **One alternative left: no draw,** byte-identical to today.

**4.2 The race (D-053, amending D-052).** Each alternative draws on lane 5, keyed by its
**content**, not by corner: `x` mixes `kAbs` with a fold of the content (exact layout in
the spec). A 4096-entry integer table of bin centres gives `E` (never 0). The lowest
`E / weight` wins, compared by cross-multiplying in 64 bits; a tie goes to the lower
content key; weight 0 is skipped. The seed is Arp's own `seed` (D-053), so a saved blend
sounds the same in any patch. Modelled result: a corner-keyed draw differs from the saved
pattern on 11,182 of 20,000 steps; content keys match on all.

**4.3 What moving the puck does.** With the seed fixed, no hold in play and the other
weights fixed, raising one weight moves steps only to that setting. On the pad, a step
moves only to an alternative whose weight ratio rose, so a diagonal move can shift steps
between two corners that are both falling. If corners hold different `seed` values,
crossing that key's flip redraws every step once (accepted, D-053).

**4.4 Ties.** Look-ahead follows the draw: `t` counts the following steps whose drawn
alternative is a tie, since a flattened pattern has no source to read. This is the rule
the human heard. **The note keeps its promise (D-052):** when a step evaluated from two
or more alternatives plays with `t > 0`, the core stores a deadline `holdUntilK`. Until
then each step is a tie whatever is drawn, reading the drawn alternative for `prob` and
`trig` (open to the critic). Every anchor and release clears the hold: the bar anchor,
gesture start, free start, transport edge and re-anchor, rate switch, restart adoption,
arp off, panic, reset. A tie outside a hold acts as a rest (§5.2). A hold is never armed
with one alternative, so a session that never blends is unchanged.

**4.5 Shares.** Before ties, an alternative's chance is its share. Ties skew the sounding
steps: 36 to 64 at 50/50 for `tied_legato` against `none`, in the critic's simulation.

**4.6 Derived rules.**
- **Each layer keeps its own position,** since at 100 % a corner must equal that pattern alone.
- **One order stream (D-053):** `m` is shared. It stays one arp, not four.
- **A pattern's `step_size` and `step_unit` travel with its steps;** the global
  transposition settings do not blend.
- **Repeats (D-053):** `C` uses the combined length and counts a step as playing if any
  alternative plays. It changes only when a setting's weight crosses zero, and equals
  today's for one pattern. `none` against `three_three_two` over 3 keys gives 24.
- **Weights are sample-timed:** they arrive as events at horde's control boundaries
  (MO-3, IF-12), so output is block-size independent for the same stream (horde's DT-2).
  Sample-rate identity would need the same weight stream, which horde's control grid does
  not give across rates: this invariant's precondition is stated, not assumed.

**4.7 User patterns.** One triple buffer of `array<UserPattern, 4>` (one exchange, atomic
across corners, one flag), not four buffers. `setUserPattern(p)` without a corner writes
all four. The memory grows with layers and is reserved at construction: to be measured
against today's 95 KiB (`docs/horde-copy-in.md`).

## 5. Invariants, each with a must-fail plant

Single setting byte-identical to the pre-blend commit (stream hash, counters excluded,
so that a new counter cannot mask a change); flatten equals live; flatten of a flatten;
weight 0 never wins; chance at extremes as well as 50 % (1 ‰ against 999 ‰); lane
isolation; each hold-clear point; §4.3's weak form with ties off; N1, G1, G4 and D1 under
a blend; capture never exceeds the budget; `/1` blobs load unchanged.

## 6. Risks

- **CPU.** The tightest quoted case is about 1.97 % x4 against 2 % (D-042), inside the
  noise. Measure with D-039's alternated A/B and a blend bench family; include per-block
  cost, which dominates at block 16 and 192 kHz. Worst case is 64 alternatives per step.
- **Cutoff, 2026-12-11.** This is a new format, a capture call, core work and a change to
  horde's resolver and 1.0 bar. **Fallback, losing nothing:** Arp ships the one-value
  behaviour, and the blend follows after 1.0 with `/2` blobs additive.
- **Presets wait** on §7 item 4 (D-051's order).

## 7. For horde: the brief (`docs/filings/hypersaw-brief-morph-blend.md`)

1. `arp/pattern` as a one-key structural group, the setting test over (pattern, parsed blob).
2. `SettingSet` reaching Arp as sample-timed events, id naming the corner.
3. The bar's MUST row and the format's "the selected pattern is a discrete setting".
4. Whether `spec/params.json` needs a declared morph class: that would change `module_version`.
5. "Save current sound" calling capture, and Arp's real state maximum (ADR-351).
6. Arp's other discrete keys as flip-only, and how the scale's keys flip together.
