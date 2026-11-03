# Stochastic pattern blending under morph — design, revision 3

> **Origin.** Arp lead session, 2026-11-02, for T-007, D-050, D-052, D-053 and D-054, after
> a critic's two REWORKs and the human's contract: aside from brief crossfades and FX
> tails, every position on the morph grid can be saved as its own corner preset.
>
> **Status: DESIGN, reviewed by a critic (three rounds), answered by horde 2026-11-03 (seq 16,
> ADR-357; D-055).** Ships in 1.0 only if our A/B fits 2 % and horde's CPU pass and morph are done.
> Built behind a compile-time flag, off by default. Only §9's key rule is in `spec/`; the blend is not. horde's rows are read from its origin/main at `4b2fdae`. The scripts
> `docs/design/morph-blend-model.mjs` and `-state.mjs` are scratch evidence, not gates:
> they test which step is chosen and how big the state is, not the core, ties or audio.

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
- **Count** (Two or Four, MO-11, MO-17) needs no code here: Arp draws over the set that arrives.
- **Always a set (seq 16).** horde sends `arp/pattern` as a set under every law: Quantum, Exempt
  and morph off send one entry at weight 1 with the corner's id, so `pattern = user` always
  names a corner. A corner's blob is published off-thread before any set can name its edited value.
- **Arp's other discrete settings (D-054):** one value under every law, following horde's
  key-by-key pick, except that **related settings flip together** as groups, so a group
  never lands on a combination no corner holds: the scale, transposition, chord and rate
  groups (§15.6 already joins some of these). This replaces D-021's single leading-corner
  rule, by the human's ruling, and horde agreed, for Arp only (ADR-357). SPEC §6 and §9 are amended (D-055). `step_time_us` stays out of the rate group.
- **Continuous settings** follow horde's law too: blended under Blend, the pick's value
  under Quantum (MO-7). They are not a change on our side.

## 3. The flattened pattern (D-053, D-054)

- **One pattern to the player, layers inside:** each source with its share, bank patterns
  by id, user patterns by value. `pattern = user`; the blob is `arp-user-pattern/2`, in the
  existing `blobs["arp.userPattern"]`. horde stores it verbatim and never parses it (ADR-294).
- **Compact (D-054):** each step packs into 5 bytes, 40 bits in base64. Measured with a full
  horde corner record, 50 keys, `docs/design/morph-blend-state.mjs`:

| Saved blend | As JSON text | Compact | Whole record |
|---|---|---|---|
| No blob | | | 1,380 B |
| Two bank patterns | | 84 B | 1,494 B |
| 1 busy user pattern | 1,253 B | 166 B | 1,574 B |
| 2 busy, 15 + 16 | 2,392 B | 285 B | 1,695 B |
| 4 busy, distinct, the most four plain corners hold | 4,885 B | 549 B | 1,963 B |
| 8 busy, only by blending saved blends | 9,688 B | 1,061 B | 2,483 B |

- **Budget.** RS-6.12 gives 2,048 B per corner, provisional; plan on 4 KiB, ruled with B603. The
  table uses default values. horde counts the whole corner record and stores integers as integers
  (seq 16), so with every setting at its widest: no blob 1,374 B, four busy layers **1,957 B**, 8 layers
  2,477 B, 16 layers 3,501 B. A position between four plain corners fits even at 2 KiB. Today's `/1` text with one busy pattern is about 2,630 B
  and is already over; `/2` fixes that too.
- **Over the budget or the layer cap (D-054):** normally reachable only by blending saved blends.
  Capture takes a byte limit from horde, since it cannot see the rest of the record. It drops the
  smallest shares, rescales, and returns `approximate`, so the state can never exceed the budget
  at creation (ADR-351 ruling 2) and the player is told. A single layer always fits (166 B), so
  there is always something valid to return. A tight budget could trigger this on a plain position too.
- **Why not a step table:** its length is the lengths' least common multiple, about 1.2 MB
  in the worst case tried, and a table must be cut to fit where layers need not.
- **Format rules, for the spec:**
  - a `/1` blob is one layer at 100 %, and loads unchanged;
  - `/2` is accepted beside `/1`, amending SPEC §4.6's "a `format` other than `/1` is rejected";
  - an unknown bank id is a refused load, never a silent default (as ADR-351's names);
  - shares must be positive and sum to one million; a layer cannot itself be layered;
  - layers are in canonical order, so save, load, save is byte-identical (RS-6.11);
  - a new writer and parser join the copy-in list, since the core has no text format today.
- **The face.** A flattened pattern shows its bank patterns by name, unnamed user layers by number,
  and each share; storing user-layer names would cost bytes the budget does not have. It is **not editable step
  by step in 1.0**; the public `UserPattern` struct gains a layered form, and the lab's
  editor shows such a pattern read-only.
- **Exact means:** puck held still, same seed and settings, same start: the saved corner plays
  what the live position plays. It does not reproduce a moving puck's path. A saved blend
  sounds the same in another patch unless `seed` is Exempt there.

## 4. Rules

**4.1 The operator.** One pure function, used live and by capture. Stack the settings'
layers, merge identical layers, and round shares to millionths **once**, by largest
remainder. At a step, take each layer's step at `k mod its own length`, canonicalise it with
its pattern's `step_size` and `step_unit`, merge identical contents and sum their shares
raw, with no per-step renormalising. So live equals saved by construction, and saving a saved
blend changes nothing (modelled, with awkward weights and nesting). **One alternative left: no
draw,** byte-identical to today. Content keys are precomputed off the audio thread.

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
alternative is a tie, capped at 63, since a flattened pattern has no source to read. The tail
gate comes from the drawn step at `kAbs + t`. This is the rule the human heard. **The note
keeps its promise (D-052):** when a step plays with `t > 0` and it or any counted step had two
or more alternatives, the core stores a deadline `holdUntilK`. Until then each step is a tie
whatever is drawn, reading the drawn alternative for `prob` and `trig`. Every anchor and
release clears the hold: the bar anchor, gesture start, free start, transport edge and
re-anchor, rate switch, restart adoption, arp off, panic, reset. A tie outside a hold acts as
a rest (§5.2). A plain single pattern never arms a hold, so it is unchanged. At a held puck a
hold changes nothing, because look-ahead and evaluation read the same draws.

**4.5 Shares (D-054).** A corner's chance equals its weight **before ties**. Ties skew the
sounding steps: 36 to 64 at 50/50 for `tied_legato` against `none`. Accepted by the human.

**4.6 Derived rules.**
- **Each layer keeps its own position,** since at 100 % a corner must equal that pattern alone.
- **One order stream (D-053):** `m` is shared. It stays one arp, not four.
- **A pattern's `step_size` and `step_unit` travel with its steps;** the global transposition
  settings do not blend.
- **Repeats (D-054):** a repeat lasts as long as the longest pattern in the blend: `C` is the
  largest of the layers' own `C`. With two or more layers **after merging**, whether live or saved,
  `C` keeps its high-water mark, so a saved blend counts as the live position does. The mark folds
  in at evaluated steps only, is tested as `max(highWater, lastC)`, and clears wherever
  `repeatSteps` resets, including a key-adding revive, so no stale count outlives a restart. One
  pattern counts exactly as today. `none` against `three_three_two` over 3 keys gives 8. So moving
  the puck between patterns cannot stop the arp early and a tiny touch of another corner cannot
  multiply the length. **Not covered:** corners holding different `repeats` values, since a flip of
  `repeats` itself can still stop it, as today.
- **Timing:** we ask horde to split Arp's `process` at the resolver's boundaries while its events
  flow, covering all 50 keys with no new event type, as the test harness does with `cuts`. Output
  is then block-size independent for the same event stream (horde's DT-2). Sample-rate identity
  would need the same stream, which horde's control grid does not give across rates: that
  invariant's precondition is stated, not assumed.

**4.7 User patterns.** One triple buffer of `array<UserPattern, 4>` (one exchange, atomic across
corners, one flag), not four buffers. `setUserPattern(p)` without a corner writes all four. Slots
are sized to what the budget can hold, in packed form, not to 16 full patterns (about 134 KB by the
critic's estimate). To be measured against today's 95 KiB (`docs/horde-copy-in.md`).

## 5. Invariants, each with a must-fail plant

Single setting byte-identical to the pre-blend commit (stream hash, counters excluded,
so that a new counter cannot mask a change); saved layers equal live through the real state
machine, with ties, holds, `prob` and repeats, on non-terminating weights; a save of a save;
weight 0 never wins; chance at extremes as well as 50 % (1 ‰ against 999 ‰); lane
isolation; each hold-clear point; §4.3's weak form with ties off; N1, G1, G4 and D1 under
a blend; capture never exceeds the budget; `/1` blobs load unchanged; the repeat count never
shrinks within a gesture; a related group never lands on a combination no corner holds.

## 6. Risks

- **What is not proven.** The model tests selection only. "Saved equals live" with ties, holds and
  repeats rests on reasoning (both run one state machine) until the §5 gate exists.
- **horde's date.** The second ship condition, horde's CPU pass and morph, has no date. The work here can be good and still miss 1.0.
- **CPU.** The tightest quoted case is about 1.97 % x4 against 2 % (D-042), inside the
  noise. Measure with D-039's alternated A/B and a blend bench family; include per-block
  cost, which dominates at block 16 and 192 kHz. The worst step is a capped look-ahead over many layers, so the peak matters as well as the average.
- **Cutoff, 2026-12-11.** This is a new format, a capture call, core work and a change to
  horde's resolver and 1.0 bar. **Fallback, losing nothing:** Arp ships the one-value
  behaviour, and the blend follows after 1.0 with `/2` blobs additive.
- **Presets wait** on §7 item 4 (D-051's order).

## 7. For horde: the brief (`docs/filings/hypersaw-brief-morph-blend.md`)

Filed as seq 15, answered as seq 16. Seven questions: the group, how the set and the corner reach Arp, the two texts to amend, a
declared morph class, capture and what the budget counts, the other keys and their groups, and the face.
