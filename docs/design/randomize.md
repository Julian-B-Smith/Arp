# Arp's own randomize — design (phase 1, for review)

> **Origin.** Arp, a session started by the lead, 2026-10-29, for T-007. It
> follows the human's polls D-044 and D-047 (recorded in PR #38, not yet merged)
> and answers horde's brief seq 10 (`integrations/hypersaw/brief-arp-randomize.md`,
> horde ADR-321).
>
> **Status: DRAFT DESIGN, revision 3.** Nothing here is implemented, and
> `spec/` is not amended. Both wait for the human's approval of this page
> (phase 2). Every number in §9 comes from a Python sketch of these rules, not
> from the C++. Revision 2 folds in an independent critic's review (Opus); the
> per-finding record is in §12. Revision 3 folds in the critic's second
> round (also §12).

## 0. In short

- **Arp declares its own randomize** (D-044). horde's Random panel passes its
  buttons through to it, as it does for Sluice and Shriek.
- **Two verbs, plus both together:**
  - **Structure** (horde's ARCH): the order, the pattern, the transposition
    shape, the chord stage's intervals and similar choices;
  - **Values** (horde's PARAMS): gate, swing, velocity, the randomness amounts,
    the free step time, the seed;
  - **Both**: Structure, then Values on the result, at the same click.
- **Settings only.** The build-your-own pattern's steps are never drawn (D-044).
- **One pure function, integer only.** It takes the current settings, a home,
  and horde's seed, click, corner, chaos, amount and locks. It returns new
  settings and nothing else. The same inputs give the same result on every
  platform. It never runs on the audio thread.
- **Every key has a class:** 36 structure keys, 8 value keys, 6 never drawn.
  15 of the structure keys are **exempt by default**: horde's lock that starts
  on (horde ADR-321), so a random click keeps the song's key and division
  unless the player unlocks them.
- **Keys that only make sense together are drawn together.** The transposition
  keys, the chord stage's keys and the scale keys each form a **group**, with
  one amount threshold. Everything else is decided independently.
- **The result is an ordinary settings change.** It reaches the core like a
  preset load or a morph corner, so SPEC §9's timing and §7's terminals apply
  unchanged. Randomize adds no new path to the core.
- **The laws are horde's,** taken from its Random lab (`docs/design/random-lab.html`
  and its report §3, in horde's tree), rewritten in integers so they are exact
  on every platform. Three deliberate departures are marked "**departs from
  horde**".

## 1. What was asked and what was ruled

**horde's four asks** (seq 10):
1. Does Arp declare its own randomize, and with which verbs?
2. Which keys are structure, which are values, and which are never drawn?
3. Which ranges are safe defaults for a random draw?
4. Where does Arp's parameter table live, in machine-readable form?

**The human's rulings** (2026-10-29, by poll):
- **D-044.** Arp declares its own randomize, as a recorded exception to the
  feature freeze. Its scope:
  - settings only;
  - two verbs, structure and values;
  - seeded and deterministic (mulberry32 only, no wall clock);
  - it honours horde's chaos and locks;
  - it runs off the audio thread and only produces new settings.
- **D-047.** The reply to horde also asks whether Arp's page should mirror
  horde's named-scale control for Arp's own scale. It reminds horde that the
  factory presets wait on horde's answers to our seq 12.

**What horde's lab already settled, and this design adopts** (report §3, §3a,
ADR-318):
- draws are **counter-based**, so a key's draw never depends on how many keys
  exist or which are locked;
- numbers draw from a **bell around home**, cut to bounds. Its width follows
  CHAOS;
- **AMOUNT** gives each key a threshold, so a higher amount changes a superset
  of the keys, with the same values (§2.3 says exactly when this holds);
- an **unordered choice** keeps home with probability 1 − chaos, and otherwise
  draws by taste weight;
- an **ordered choice** is a bell in index space;
- a value nobody can hear is **not drawn** (the dependency rule);
- chaos and amount are **bounds for the next draw only**. They never change the
  current sound (ADR-318 item 1). Arp follows this for free, because it only
  runs when horde calls it.

## 2. The rules

### 2.1 Inputs

| Input | What it is | Why it is there |
|---|---|---|
| `cur` | the settings now held at the target corner | keys that are not drawn keep these values, and `cur` decides what is live (§2.3) |
| `home` | the bell's centre | horde's home is the corner's saved snapshot (report Q4). With `home = cur`, the bell centres on what is playing |
| `verb` | Structure, Values or Both | horde's ARCH, PARAMS, or randomize patch |
| `seed`, `click`, `corner` | horde's randomize seed, its click counter, and the target corner (0–3) | the draw key: randomize-all-four gets four different results from one click |
| `chaos` | 0–1000 (horde's 0–1, in permille) | the bell's width, and the chance an unordered choice moves |
| `amount` | 0–1000 | the share of keys (and groups) that may change (report §3) |
| `locks` | one bit per key | horde's lock state for Arp's keys, including the default exemptions the player has not lifted |
| `mustChange` | on or off | horde's "ARCH must change" (report finding 1, Q2). Applies to Structure only |

**Why integers, not floats.** The lead's task sketch used `float chaos`. A
float would have to be turned into the core's integer rules somewhere, so the
API takes permille and horde's adapter converts once, as
`clamp(round_half_up(chaos · 1000), 0, 1000)`. Arp's function also clamps
`chaosPm` and `amountPm` to 1000 on entry, so the σ table is never indexed out
of bounds. Whatever float arithmetic horde does before the conversion is
horde's. From the permille integers on, everything is exact. A step of 1/1000
of the knob is far below anything audible.

### 2.2 The draw

    rdraw(seed, click, corner, keyId, idx) =
      mulberry32( seed ⊕ SALT ⊕ imul(click, K1) ⊕ imul(corner, K2) ⊕ imul(keyId, K3) ⊕ imul(idx + 1, K4) )

- This is SPEC §10's construction: one mulberry32 output from a mixed input,
  uint32 arithmetic, no state carried. It is a **separate function** with its
  own salt and constants. The randomize stream is independent of the note
  stream's lanes 0–4. It is also keyed by horde's seed, not by Arp's `seed`
  setting. The constants are fixed when the spec section is written.
- **`keyId` is the FNV-1a 32-bit hash of a name,** computed at compile time:
  the key's name in `params.json` (such as `"gate"`), or a group's name
  (`"transposition"`, `"chord"`, `"scale"`), or `"scale_mask"` for the named
  scale pick. So a key's draws do not depend on its position in any list. A key
  appended later moves nobody's draws. The 50 key names plus the 4 extra names
  must hash without collision. A test pins it, and the sketch checked the 51
  names it used.
- **`idx`** names the draw's purpose: 0 the amount threshold, 1 the keep test,
  2 the value pick, 3 the must-change pick.
- **A threshold test** `below(r, pm)` is `r < ⌊pm · 2³² / 1000⌋` in 64-bit
  integers.

### 2.3 Which keys a click considers, and amount nesting

A **unit** is either a single key or a group (§2.6). A unit is **drawn** only
if all of these hold:
1. its class matches the verb (§3);
2. it is not never-draw and not locked (a group draws its unlocked members);
3. it is **live on `cur`** (§3's "live when" column): a value no one can hear
   is not drawn;
4. `below(rdraw(…, unit's keyId, idx 0), amount)`: the amount threshold.

A key that is not drawn keeps its `cur` value, exactly.

**Liveness and joint rules read `cur` and the unit's own draws, never another
unit's draw from the same click.**
- `cur` decides what is live, because `cur` is what plays now. `home` decides
  where the bell sits.
- Inside a group, the members' joint rules read the group's own proposals
  (drawn in a fixed order) and `cur` for locked members. A group's proposal
  depends only on its draws, `cur`, `home` and chaos, never on amount. The
  amount threshold then decides whether the whole proposal is applied.
- **Consequence, the nesting law:** for one verb (Structure or Values), **with
  must change off**, the keys changed at amount a are a subset of those changed
  at any b > a, with the same values. This is exact and gated (RZ7).
- **Must change is the one exception.** It fires only when a click changed
  nothing, and then moves a unit by its own rule (§2.8). At a higher amount the
  click may change something else, so must change does not fire there, and the
  unit it moved at the lower amount may keep its value. horde's lab has the
  same exception: its C3 tests PARAMS, which never use must change. The sketch measured 0 violations
  in 16,000 pairs with the controlling keys on. The planted control (liveness
  read from the keys just drawn, revision 1's rule) shows 12 (§9).
- **What it costs.** A key that only becomes live this click is not drawn
  until the next click. For example, when Values draws `vel_random` from 0 to
  20, `random_curve` waits a click. horde's lab has the same property for
  PARAMS keys after an ARCH click.
- **Both** is defined as Values run on Structure's result, so its Values part
  reads liveness from that result. Nesting then holds for Both's Structure keys
  and for its Values keys whose "live when" reads no structure key. It does not
  hold for `vel_fixed` (reads `vel_mode`) or `seed` (reads `order` and
  `pattern`). Q11 asks the human to accept that.
- horde gates liveness for PARAMS keys only, never ARCH (`applyRoll`'s
  `if (!s.arch && !active(s, out))`). Arp gates both, because Arp's structure
  keys do depend on each other (the chord stage's keys on `chord_enabled`). An
  arp-off module must not report a structure change no one can hear (report
  finding 2). **Departs from horde.**

### 2.4 Unordered choices (and bools)

- If `chaos = 0`, or `¬below(rdraw(…, idx 1), chaos)`: the result is **home**.
- Otherwise pick among the **lit** values by taste weight, using
  `rdraw(…, idx 2)` (the pick in §2.5). This pick may land on home again, as in
  horde's lab.
- A value is **lit** when its taste weight is above 0. An unlit value is never
  drawn into.
- **An unlit home is kept** when the keep test keeps it. horde's `drawEnum`
  keeps home only when it is lit (`u1 >= c && L[home]`). Arp keeps it, so a
  player's build-your-own pattern or trigger pattern survives a low-chaos click.
  **Departs from horde.**
- Taste weights are data (§8, `spec/randomize.json`), never code. They are
  starting values, to be tuned by ear (Q10).

### 2.5 Ordered choices and numbers: one discrete bell

Every number Arp has is an integer (SPEC §9). So every drawn number, every
ordered enum and every interval is drawn over a finite **ladder** of candidate
values. The same integer bell serves them all.

- **The ladder:**
  - for `gate`, `swing`, `vel_fixed` and the percent keys, every integer in the
    legal range;
  - for `step_time_us`, a geometric ladder of 1/12-octave steps,
    `10,000 · 2^(i/12)` rounded, for i = 0…91 (up to 1,917,833 µs), then
    2,000,000 as the last rung: 93 rungs, all inside the legal range;
  - for `rate`, home's family in length order (§3);
  - for `steps` and `repeats`, their integers;
  - for `scale_root`, the circle of fifths (C G D A E B F♯ C♯ G♯ D♯ A♯ F),
    **circular**: the distance is `min(|d|, 12 − |d|)`.
- **Home's index** `h` is home's place on the ladder. A home that is not on a
  ladder (a `step_time_us` set by hand) uses the nearest rung by ratio. If the
  pick lands on that rung, the result is **home's exact value**.
- **The band:** the key's musically safe band (§3), **widened to include
  home**. A home outside the band is never forced to move: the band only limits
  new values.
- **The centre.** The bell is centred on home. Two rules can make home
  inadmissible: `repeats`, which never goes down from `cur` (§3, row 20), and
  `steps` under a locked `step_size` (§2.6). There the centre is `cur`'s value,
  which those rules always admit. **The centre is always a candidate,** for
  every bell, at every chaos, under every lock set.
- **The width:** `σ = σ(chaos) · n`, in ladder steps, where `n` is the full
  ladder's length (horde's lab scales an ordered choice by its list length the
  same way). `σ(c) = 4c · 50^(c−1)` is horde's chaos law (report finding 3). It
  is stored as a **1001-entry integer table** in units of 2⁻¹⁶, generated once
  by a script and committed. The C++ never computes a power or a logarithm.
- **The weights:** `w(i) = taste(i) · E(i)`, where
  `E(i) ≈ 2¹⁶ · exp(−d² / 2σ²)` for the distance `d` from the centre. E comes from
  an integer table of `exp(−x)` at 1/256 steps for `x < 16` (0 beyond). Every
  product is a 64-bit unsigned integer, with bounds checked by `static_assert`.
- **The pick:** `t = ⌊r · Σw / 2³²⌋` with `r = rdraw(…, idx 2)`; the result is
  the first candidate whose running sum exceeds `t`. The centre is always a
  candidate and carries the largest bell weight (`E = 2¹⁶`, its taste counted
  as at least 1), so `Σw > 0` for every bell pick. RZ3 asserts it at every
  chaos from 1 to 1000. Must change does not use the bell (§2.8).
- **At chaos 0** the result is the centre (home, except as above), with no
  draw.
- **At chaos 1000** `σ = 4n`, so the bell is flat across the band to within a
  few percent (horde's C4 measured the same law at 0.014 from flat).

This is horde's truncated normal, sampled at the integers. horde's lab draws
the continuous bell by inverse CDF in floating point (`Phi`, `PhiInv`, `exp`,
`pow`). Those differ in their last bits between libm builds, as Sluice's
randomiser notice measured (≤ 4 ulps). Integer tables remove that.

### 2.6 Groups

Three groups are drawn as one unit each: one amount threshold, a fixed internal
order, and joint rules that read only the group's own proposals and `cur` for
its locked members.

**The transposition group** (`steps`, `step_unit`, `step_size`; keyId
`"transposition"`). Live when the arp is live (§3). **The span bound** is 24
semitones, or 14 degrees. *Why:* past two octaves most of a chord's passes
leave 0–127 and are dropped silently (T16). In order:
1. **`steps`**: the bell over 0–3 ∪ {home}, unless locked.
   - If `step_size` is locked, a value is admitted only if
     `|steps · cur.step_size|` is within the bound (in `cur.step_unit`, which
     the lock pins), **or it is `cur.steps`** (the player's own shape). If home
     is not admitted, the centre is `cur.steps` (§2.5). The set always holds 0
     and `cur.steps`, and the centre is in it.
2. **`step_unit`**, only if `steps > 0` after step 1: an unordered choice. It
   is kept at `cur` if it or `step_size` is locked (a lock on the size pins its
   unit). `degrees` is lit only while `cur.quantize ≠ off`: with quantize off a
   degree is one semitone (SPEC §4.7, §13 item 13), so a flip to degrees would
   only re-weight the table. Drawing the unit only when there are passes means
   a flip never leaves an old interval silently waiting in the new unit.
3. **`step_size`**, only if `steps > 0` after step 1, unless locked. Drawn from
   the unit's interval table (§3, row 9), keeping only intervals with
   `|steps · s|` within the bound.
   - **Forced** (no keep test) if the unit flipped (home's interval belongs to
     the other unit), or if keeping home's size would break the span bound.
   - Otherwise an ordinary unordered choice.
   - **Never empty:** for steps 1–3 the tables always hold an interval inside
     the bound (±5 and ±7 semitones; ±3 and ±4 degrees). A `steps` above 3 can
     only be home's or `cur`'s own value, kept beyond the band. It brings its
     own shape: the span filter does not apply, and the size is kept, or drawn
     unfiltered after a flip. The sketch swept home `steps` 0–8 in both units:
     no empty set (§9).

**The chord group** (`chord_on1..6`, `chord_unit`, `chord_shift1..6`; keyId
`"chord"`). Live when `cur.enabled` and `cur.chord_enabled`. In order:
1. **`chord_on1..6`**: unordered bools. A locked slot keeps `cur`.
2. **`chord_unit`**: an unordered choice. It is kept at `cur` if it, or **any**
   `chord_shift`, is locked, enabled or not. `degrees` is lit only while
   `cur.quantize ≠ off`, for the same reason as above.
3. **`chord_shift_i`** for each slot that is on after step 1, unless locked:
   - **forced** if the slot just turned on, or the unit flipped;
   - otherwise an ordinary unordered choice;
   - drawn from the unit's consonance table. 0 is never drawn: it only doubles
     the played note. So a slot that turns on always gets an interval you can
     hear. A slot turned on while its shift is locked keeps the locked shift,
     which is the player's choice.

**The scale group** (`quantize`, `scale_root`, `scale_deg0..11`; keyId
`"scale"`). All 15 keys are exempt by default (§2.7). Live when `cur.enabled`
and the arp or the chord stage is live. In order:
1. **`quantize`**: an unordered choice. A lock keeps `cur`.
2. **Arp's own scale**, only if `cur.scale_source = own` and quantize is not
   off after step 1. `scale_root` and `scale_deg0..11` are one morph group
   (SPEC §6), and twelve independent bits would make arbitrary scales:
   - **the mask** is a **named scale** from `params.json`'s `ui_scale_names`,
     by taste (an unordered choice, keyId `"scale_mask"`). The draw picks a
     name; only `{root, mask}` is stored, as §6 says ("the mask is the truth,
     the name is UI"). A home mask that matches no name is kept when the keep
     test keeps it;
   - **the root** is the circular bell on the circle of fifths, so low chaos
     moves to a neighbouring key;
   - a lock on any of `scale_root` or `scale_deg0..11` keeps all thirteen at
     `cur`.

### 2.7 Locks, exemptions and never-draw

- **Never-draw** keys (6, §3) are copied from `cur` on every verb, whatever the
  locks, chaos or amount say. horde cannot unlock them.
- **Locked** keys are copied from `cur`. A locked key consumes no draws that
  matter to anyone else: draws are counter-based (§2.2), so locking one key
  never changes another unit's result. Inside a group a lock can narrow a
  member's choices (§2.6), which is the lock's purpose.
- **Exempt by default** (15 keys, §3) is horde's lock that defaults on
  (report §4b.5b, ADR-321). Arp publishes the set (`defaultLocks()`, and the
  table in §8). horde owns the lock state: what the player has unlocked, and
  whether a profile carries it (ADR-318). Arp only reads the effective locks it
  is given.

### 2.8 Order of units, and "must change"

**Fixed order.** Structure runs in this order: `order`, `pattern`, `rate`,
`restart`, `repeats`, `vel_mode`, then the scale group, the transposition
group, the chord group. Values runs in this order: `gate`, `swing`,
`step_time_us`, `vel_fixed`, `vel_random`, `gate_random`, `random_curve`,
`seed`. Since liveness reads `cur` (§2.3), the order only fixes which draws
belong to which unit. It never changes a result. Both runs Structure, then
Values on its result.

**Must change** (horde's Q2, Structure only, `chaos > 0`). If Structure changed
no key:
1. Take the **movable** units: drawn units (classes, locks and liveness as
   §2.3, ignoring the amount threshold) for which **one of the moves in step 3
   can change a key**. A group counts as movable only through a move its
   unlocked members can make.
2. Of these, take the one whose amount draw (`idx 0`) is lowest, ties by the
   fixed order.
3. Move it with `r = rdraw(…, idx 3)`, trying its moves in this order and
   making the first that changes a key:
   - **an unordered key:** pick by taste among its lit values other than
     `cur`'s;
   - **an ordered key** (`rate`, `repeats`): the nearest admitted rung above or
     below `cur`, the side chosen by `r`'s top bit; if only one side exists,
     that side. No bell, so no zero-weight pick (the bell's weights all vanish
     once σ is under 0.18 rungs);
   - **the transposition group:** move `steps` as an ordered key, then re-apply
     the span rule to `step_size` (§2.6); else, with `steps > 0`, a different
     `step_size` from the table within the bound, by taste; else flip
     `step_unit` (degrees only while quantize is on), with a forced size;
   - **the chord group:** redraw the first on, unlocked shift, by taste,
     excluding its `cur` value; else turn on the first off, unlocked slot (its
     shift forced unless locked); else turn off the last on, unlocked slot;
     else flip `chord_unit`, if no shift is locked;
   - **the scale group:** `quantize`, by taste, excluding its `cur` value; else
     (own scale live) a different named scale; else the root, one fifth either
     way as an ordered key.
4. If no unit is movable, return unchanged with `nothingToDraw = true`, so
   horde can say why (report finding 2).

At chaos 0 must change does nothing, so chaos 0 stays exact (§2.9). horde's
must-change takes unordered choices only. Arp extends it to its ordered keys
and groups, because otherwise most of Arp's structure could never be the one
that moves. **Departs from horde.**

### 2.9 What chaos 0 means

- **With `home = cur`, chaos 0 returns `cur` exactly,** for every verb, amount
  and lock set, with must change on or off. This is the identity the lead asked
  for, and a test.
- **With a separate home,** chaos 0 returns every drawn member **to its
  centre**, which is home except in §2.5's two cases. A member a group does not
  draw (a `step_size` when steps is 0, the shift of a slot that is off) keeps
  `cur`. The bell has zero width, so its only value is the centre, and an
  unordered choice keeps home. That is horde's law: its lab's `drawNum` returns home at
  chaos 0. Inside a group, the forced redraws (§2.6) **do not apply at chaos
  0**: a group at chaos 0 takes home's members whole, a combination home
  already holds. It keeps the knob continuous: at a chaos just above 0 the
  draws land next to home. Jumping to `cur` exactly at 0 would be a
  discontinuity. Q5 asks the human to confirm this reading.

## 3. The 50 keys

**Classes:** S = structure (horde's ARCH), V = value (PARAMS), N = never drawn.
**Exempt** = locked by default (horde's lock that starts on).
"Unord." = unordered choice (§2.4); "bell" = the discrete bell (§2.5).
**"Arp live"** means `cur.enabled` and `cur.arp_enabled`: with the module or
the arp off, nothing the arp plays can be heard, so its keys are not drawn.
"Live when" always reads `cur` (§2.3).

| # | Key | Class | How drawn | Safe band or lit values | Live when | Reason |
|---|---|---|---|---|---|---|
| 1 | `enabled` | N | — | — | — | The whole module. Off routes input around Arp and ends every note (T4). Routing, not music. |
| 2 | `arp_enabled` | N | — | — | — | Off turns Arp into a chord generator or into silence (§14), at offset 0. A click that silences the arp reads as broken. |
| 3 | `order` | S | unord. | all 16 | arp live | The arp's shape. Applies from the next step (T8). |
| 4 | `pattern` | S | unord. | the 9 other bank patterns. Unlit: `user`, `trigger_pulse`, `bar_trigger` | arp live | The rhythm. `user`: its steps are never drawn (D-044), and drawing into it would play whatever was last built. `trigger_pulse` plays no notes, so the arp would fall silent. `bar_trigger` fires trigger lane 1, which horde routes (Q-H3). A home on any of them is kept by the keep test (§2.4). |
| 5 | `rate` | S, **exempt** | bell, in home's family | straight 1/2…1/32; dotted 3/8…3/64; triplet 1/2T…1/32T | arp live, `rate_mode = sync` | The division usually belongs to the song's part, and horde's own exemption list names it (report §4b.5b). Every division is on the transport grid, so even a drawn rate stays in time. Keeping the family means a straight part never turns triplet. Each band runs from at most a half note down to a 1/32: a 2/1 rate's change can lag 8 beats (§9). |
| 6 | `rate_mode` | N | — | — | — | Time mode ignores the host tempo and transport (§2.6). It would break a song's timing, and switching is a clock switch with release-all (T19). |
| 7 | `step_time_us` | V | bell, 1/12-octave ladder | 40,000–1,000,000 µs (25 Hz to 1 Hz) | arp live, `rate_mode = time` | Only heard in time mode. Faster than 40 ms reads as a buzz; slower than 1 s no longer reads as an arp. |
| 8 | `steps` | S | transposition group: bell | 0–3 (with `step_size` locked: only values inside the span bound, plus `cur.steps`) | arp live | The transposition passes. More than 3 at +12 pushes most passes off the keyboard (T16). Span rule (§2.6). |
| 9 | `step_size` | S | transposition group: unord., interval table | semitones: +12 6, −12 3, +7 3, +5 2, and −5, −7, +24, −24 at 1. Degrees: +7 6, −7 3, +4 3, +3 2, and −3, −4, +14, −14 at 1 | in the group, `steps > 0` | An interval, not a magnitude: +6 is not "near" +7 musically. Weighted toward the octave and the fifth (horde ADR-321's minimal pitch ruleset). Span rule. |
| 10 | `step_unit` | S | transposition group: unord. | semitones 3, degrees 1 (degrees lit only while `cur.quantize ≠ off`) | in the group, `steps > 0`, `step_size` not locked | Gives `step_size` its meaning. A flip forces the size (§2.6). |
| 11 | `gate` | V | bell | 25–100 % | arp live | Below about 25 % fast rates turn to clicks (a 1/32 step at 120 bpm is 62.5 ms, so 10 % is 6 ms). Above 100 % notes overlap into legato, a deliberate choice kept only when home is there. |
| 12 | `swing` | V | bell | 500–667 ‰ | arp live | Past the triplet feel the pairs lurch. Applies from the next even step (E6). |
| 13 | `latch` | N | — | — | — | It changes what the player's hands do. On, the arp keeps playing after the keys are released, which reads as stuck notes. Off, it cuts a latched performance at offset 0 (§13 item 12). horde's lab sketch (§4b.5a) listed latch under ARCH; we ask to keep it out. |
| 14 | `vel_mode` | S | unord. | played 3, fixed 1 | arp live | Whether the player's dynamics pass through. |
| 15 | `vel_fixed` | V | bell | 64–120 | arp live, `vel_mode = fixed` | Only heard in fixed mode. Below 64 most patches get quiet. |
| 16 | `vel_random` | V | bell | 0–40 % | arp live | Past about 40 % notes jump between very loud and very quiet. |
| 17 | `gate_random` | V | bell | 0–40 % | arp live | High amounts shrink some notes to almost nothing (the length floor is one tick, §5.2). |
| 18 | `random_curve` | V | bell | 0–100 % | arp live, `vel_random > 0` or `gate_random > 0` | The shape of both randomnesses; inaudible when both are 0. |
| 19 | `restart` | S | unord. | chord 2, bar 1, free 1 | arp live, `rate_mode = sync` | Chooses which step plays next, never when (domain invariant). Inert in time mode. |
| 20 | `repeats` | S | bell, centred on home if admitted, else on `cur.repeats` | admitted: `cur.repeats`; every count above it up to 8; home, if home is 0 (forever) or above `cur.repeats` | arp live, `cur.repeats ≠ 0` | "Forever" (0) is never drawn away from: a click must not make a held arp stop after a few cycles. A count only moves **up**, or back to forever, because a lower count can exhaust a running arp at once, and a morph never revives it (§4.8). |
| 21 | `quantize` | S, **exempt** | scale group: unord. | off 1, scale 2, chord_root 1 | arp or chord stage live | `chord_root` re-anchors the scale on the lowest held key, off the song's key. horde exempts key by default (ADR-321). |
| 22 | `scale_source` | N | — | — | — | Whose scale Arp follows: horde's song scale, read never owned, or its own (D-010). A routing choice. Drawing `own` would silently take Arp off the song's key. |
| 23 | `scale_root` | S, **exempt** | scale group: circular bell on the circle of fifths | all 12 | in the group, `cur.scale_source = own`, quantize on | The key. Drawn with the mask as one unit (§2.6). |
| 24–35 | `scale_deg0` … `scale_deg11` | S, **exempt** | scale group: a named scale, unord. | 17 of horde's 18 names (chromatic unlit: it quantizes nothing) | as `scale_root` | Bits drawn one by one make arbitrary scales. |
| 36 | `seed` | V | re-roll: keep with 1 − chaos, else a fresh 32-bit draw (`idx 2`) | the whole range | arp live, and something reads it: `order` is `random` or `shuffle`, `vel_random > 0`, `gate_random > 0`, or the pattern has a step with prob < 100 (`sparse_chance`, `user`) | A new take of the same settings. A seed has no "near", so there is no bell. One shuffle caveat (§5, Q12). |
| 37 | `chord_enabled` | N | — | — | — | Adds or removes the whole `chord` stream, which horde routes to its own oscillators (Q-H7). Routing. |
| 38 | `chord_unit` | S | chord group: unord. | semitones 3, degrees 1 (degrees lit only while `cur.quantize ≠ off`) | in the group; no `chord_shift` locked | Gives the shifts their meaning. A flip forces every on shift (§2.6). |
| 39–44 | `chord_on1` … `chord_on6` | S | chord group: unord. bool | on 1, off 2 | `cur.enabled`, `cur.chord_enabled` | The chord's size. The `+0` slot always sounds, so all off is still the played note. Applies at offset 0 with a re-map (§14). |
| 45–50 | `chord_shift1` … `chord_shift6` | S | chord group: unord., interval table | semitones: +7 4, +12 3, +4 3, +3 3, +5 2, +9 2, −12 2, and +2, +10, +11, +14, +16, +19, +24, −5, −7 at 1. Degrees: +2 4, +4 4, +7 3, +5 2, −7 2, and +1, −3, +6, +9, +11 at 1 | in the group, that slot on after the group's step 1 | Intervals, weighted by consonance (ADR-321's minimal pitch ruleset applied to Arp's own intervals). Never 0, which only doubles the played note. |

**Counts.** Never drawn: 6. Value: 8. Structure: 36, of which 15 are exempt
by default (`rate`, `quantize`, `scale_root`, `scale_deg0..11`). The other 29
structure and value keys are drawable by default.

**What horde's lab sketch named that maps differently** (report §4b.5a):
- "octave range" is `steps` with `step_size` (structure);
- "the swing on/off" does not exist: `swing` is one value, and 500 is straight;
- "any step probabilities" are pattern data. Bank patterns are drawn whole, and
  the build-your-own pattern's steps are not drawn at all (D-044);
- "latch" is never drawn (row 13).

## 4. The API (planned; nothing written yet)

```cpp
// arp/randomize.h — planned. A pure, integer-only function: no float, no libm, no allocation,
// no locks, no statics with dynamic initialisation, bounded work. The core never calls it.
namespace arp::rnd {

enum class Verb : std::uint8_t { Structure, Values, Both };
enum class Class : std::uint8_t { Structure, Value, Never };

// One id per params.json key, in params.json order, append-only. Locks and the changed mask index it.
enum class ParamId : std::uint8_t { Enabled, ArpEnabled, Order, /* … */ ChordShift6, Count };
struct LockSet { std::uint64_t bits = 0; /* set, test */ };

struct Request {
  Verb verb = Verb::Both;
  std::uint32_t seed = 0, click = 0, corner = 0;   // horde's draw key (corner: 0..3)
  std::uint16_t chaosPm = 0;                       // 0..1000; larger values clamp to 1000
  std::uint16_t amountPm = 1000;                   // 0..1000; larger values clamp to 1000
  bool mustChange = true;                          // horde's "ARCH must change"; Structure only
  LockSet locks = defaultLocks();                  // horde's effective locks
};

struct Result {
  Settings settings;              // valid whenever cur is valid (validSettings)
  std::uint64_t changed = 0;      // bit per ParamId: what differs from cur
  bool nothingToDraw = false;     // must change found no movable unit
};

Result randomize(const Settings& cur, const Settings& home, const Request& rq) noexcept;
inline Result randomize(const Settings& cur, const Request& rq) noexcept { return randomize(cur, cur, rq); }

LockSet defaultLocks() noexcept;      // the 15 exempt-by-default keys
Class classOf(ParamId) noexcept;
bool validSettings(const Settings&) noexcept;   // every key inside params.json's range

}  // namespace arp::rnd
```

**Why it differs from the lead's task sketch**
(`Settings randomize(cur, verb, seed, float chaos, locks)`):
- `home` separate from `cur`, because horde's bell centres on the corner's
  snapshot (report Q4). The two-argument form keeps the simple case simple.
- `click` and `corner`, because horde keys its draws by them (report §3). One
  seed then serves a whole session, and "randomize all four" draws four
  different corners.
- `amountPm`, because AMOUNT is in horde's 1.0 set (report §5 item 4).
- `mustChange`, because horde recommends it on (Q2) and only Arp knows which of
  its units can still move.
- Permille integers instead of a float (§2.1).
- A `Result` instead of bare `Settings`, so horde can show what changed and say
  "nothing to draw".

**And from horde's proposed rack verbs** (report §6): horde proposes two
calls, `randomizeStructure(seed, click, chaos, amount, locks)` and
`randomizeParams(…)`. Ours is one call with a verb, and it adds `home`,
`corner` and `mustChange`. The mapping is one line each (§5), and the reply
says so.

**Validity.** No settings validator exists in the core today. `validated()` in
`core/src/core.cpp` covers the user pattern only. Phase 2 adds
`validSettings()`, generated from `params.json`'s ranges and enum lists and
guarded by the existing drift check (`tools/core_params_check.py`). By
construction every drawn value is a ladder or table candidate inside its legal
range, and every other key is copied. So `validSettings(cur)` implies
`validSettings(result)`. An out-of-range `home` is clamped into range before
it is used as a centre. An out-of-range `cur` value is copied as it is, because
randomize never edits a key it does not draw.

**Determinism across platforms.**
- integers only: the σ, `exp` and ladder tables are committed integer data,
  generated offline;
- unsigned 32-bit wrap for the draw, which is well defined in C++20;
- the fixed unit order of §2.8, with no hashing containers;
- one draw per full expression, so argument evaluation order cannot reorder
  draws;
- `fdiv` and `mod` from `arith.h` wherever a signed value meets a division.

## 5. How horde passes through, and how the result reaches the core

**horde's side.**

| horde's control | Arp's call |
|---|---|
| ARP level, ARCH | `randomize(cur, home, {Structure, …})` |
| ARP level, PARAMS | `randomize(cur, home, {Values, …})` |
| randomize patch (global) | `randomize(cur, home, {Both, …})` |
| randomize all four | four calls, `corner = 0..3` |
| CHAOS, AMOUNT, group scales | horde's effective chaos and amount for the ARP level, in permille |
| locks and exemptions | horde's lock state for Arp's keys, starting from `defaultLocks()` |
| ARCH must change | `mustChange` |

- horde calls on its message thread, never the audio thread. The function is
  pure and bounded, so any thread is safe, but it has no reason to run in
  `process`.
- horde writes `Result.settings` into the target corner's Arp values, enums by
  name in its records (our seq 12), as one history step.
- **Copy a level between corners** (ADR-322 item 1) needs nothing from Arp:
  horde copies the Structure or Value keys by `classOf`.

**The core's side: nothing new.**
- The core takes a whole new snapshot in one step (`Core::adopt`,
  `core/src/core.cpp:1229–1263`, with the offset-0 sync at `:624–673`). So
  many keys changing in one block is exactly what a morph corner flip already
  does.
- With morph off and that corner audible, the next block's snapshot carries the
  new settings. With morph on, horde's morph blends the continuous keys and
  flips each discrete key at the leading corner with 0.05 hysteresis (§9).
- Arp then applies them as it applies any settings change:
  - a discrete key from the next evaluated step (T8); `rate` at the next step
    of the old rate, re-anchoring (T15); `restart` per Q-A2; a `repeats` change
    under §4.8 (T20: a count reached ends no note early);
  - a continuous key, read at step evaluation (T9); `step_time_us` by the
    piecewise grid (T18);
  - `chord_unit`, `chord_on*`, `chord_shift*`, `quantize` and the scale keys
    re-map the chord stage at offset 0 (§14, §13 item 15). Each leaving key ends
    once, each joining key starts once, and unchanged keys keep sounding (T21).
- **Under morph, blends stay safe.** Each safe band is an interval with integer
  ends, so a rounded blend of two in-band values is in band. A discrete key
  takes one corner's value whole.
- **Two caveats, stated plainly:**
  - **The span rule is joint.** horde flips each discrete key at its own
    boundary, so mid-morph `steps` can come from one corner and `step_size`
    from another, and the span can pass 24 semitones. Every key stays valid and
    every terminal still holds. Only the "musically safe" span can be exceeded
    there. The scale needs no such caveat, because its keys flip as one morph
    group (§6).
  - **A seed change can break shuffle's no-repeat rule once.** `shuffle`
    derives each cycle, and the previous cycle's last key, from the live seed
    (`Core::shuffleAt`, `core/src/core.cpp:1345–1362`). A seed change mid-cycle
    can therefore skip or repeat one key, and one at a cycle boundary can play
    the same key twice in a row. N2 (SPEC §11) is stated for a fixed seed. This
    already happens with a morph or automation of `seed` today. A Values click
    that re-rolls the seed makes it routine. Q12 asks the human how to handle it.

## 6. Why the domain invariants hold

| Invariant (CLAUDE.md §Domain) | Why randomize keeps it |
|---|---|
| **Deterministic** | A pure function of its inputs. mulberry32 counter-based draws only. No wall clock, no `Math.random`, no float. Same inputs give the same `Result` on every platform (§4). |
| **One terminal per note** | Randomize emits no notes. Its output enters as a settings change, adopted as one snapshot (§5), and §7 already gives every settings change exactly one terminal per note (T4–T21). The never-draw set keeps out the offset-0 switches that release everything (`enabled` T4, `rate_mode` T19, `arp_enabled`) and the one that changes the held set (`latch`). The drawn offset-0 keys are the chord stage's and the scale's, whose re-map ends each leaving note exactly once (§14). RZ14 checks it under random jumps. |
| **Real time** | Never on the audio thread. Even so it allocates nothing and does bounded work: at most 50 keys, each over at most 251 candidates. |
| **The grid comes from the transport** | Untouched. `rate_mode` is never drawn, and a drawn `rate` is a division name from horde's list (stored by name), applied by §9. |
| **Morph timing** | Unchanged. §9 applies because the result is ordinary settings (§5). |
| **Scale (D-010)** | horde's global scale is not an input and cannot be written. `scale_source` is never drawn. Arp's own scale is drawn only in horde's shape (root + root-relative mask), as one group. |
| **Pure note processor** | Nothing assumes an engine. The result is settings. |
| **The pattern bank is data** | Taste weights and lit flags come from data (§8), and every bank pattern already passes the invariant suite. |
| **Detectors have plants** | Every RZ test below has a must-fail control that the test can see. |

## 7. Tests and must-fail plants (phase 2)

All Layer-0: deterministic, no model calls, in `tests/core/test_randomize.cpp`
and two tools, run by `./verify full`. **The oracle for classes, bands, lit
values and groups is `spec/randomize.json` (or a literal list in the test),
never the function's own `classOf()`,** so a plant that moves a key cannot move
the oracle with it.

| id | Test | Must-fail plant |
|---|---|---|
| RZ1 | **Determinism.** 10,000 seeded calls (random `cur`, `home` and requests) hash to a pinned digest, identical on macOS and Linux. | Carry one mulberry32 stream across keys; mix a pointer value into the seed. |
| RZ2 | **Position-free.** Locking key A never changes another unit's result. | The carried stream (Sluice-style): locking one key shifts the next key's draw. |
| RZ3 | **Validity.** 100,000 calls, including extreme `cur` and `home` (`repeats` 9–16 against a lower home; home `steps` 0–8 in both units), locks swept inside every group, and every chaos from 1 to 1000: `validSettings(result)` holds whenever `validSettings(cur)` does, and no pick ever meets an empty candidate set or a zero total weight (assertions count both). | Let a chord shift draw ±25; drop the "steps above 3 keep their own shape" rule, so the span filter empties the size table; drop "the centre is a candidate", so `steps` with a locked +12 size and home 3 finds zero weight at chaos 100. |
| RZ4 | **Locks.** Every locked key equals `cur` bit for bit, on every verb. | Honour locks in Values only. |
| RZ5 | **Never-draw.** The 6 keys equal `cur` on every verb, chaos, amount and lock set, unlocked included. | Let `latch` be drawn. |
| RZ6 | **Chaos 0.** With `home = cur`: `result = cur` for every verb and amount, must change on and off. With a separate home (including home ≠ cur in every group, and a unit flip between them): every drawn member equals its centre (§2.5), and every other key, including group members not drawn, equals `cur`. | Let must change fire at chaos 0; let a group's forced redraw run at chaos 0. |
| RZ7 | **Amount nesting**, per verb, with must change off (§2.3). At one click, the keys changed at amount a are a subset of those changed at b > a, with the same values. Run with the controlling keys on (`steps` 1, `vel_random` 20, `vel_mode` fixed, chord stage on with slots off) and with home ≠ cur. | Read liveness from the keys just drawn (revision 1's rule; the sketch shows 12 violations in 16,000 pairs); let the value pick depend on amount. |
| RZ8 | **Verbs.** Structure changes only S keys and Values only V keys, against the literal class list; Both equals Values after Structure at the same click. | Move one key to the wrong class in the function's table. |
| RZ9 | **Live when, read on `cur`.** A key that is not live on `cur` is never drawn (`step_time_us` in sync, `vel_fixed` in played, the arp's keys with `arp_enabled` off, the chord group with the stage off, the own scale with source `global`, everything with `enabled` off). | Draw `step_time_us` in sync mode; ignore `arp_enabled`. |
| RZ10 | **Distribution sanity** (exact on fixed seeds, so it can gate). Mean move rises with chaos; at chaos 1000 band occupancy is within 0.05 of flat (KS); at chaos 350 `rate` stays within one rung of home at least 90 % of the time; at chaos 100 `gate` moves by under 3 % on average; `scale_root` moves both ways round the circle from any home. | Ignore chaos (flat at every chaos); reverse the σ table; make the root's distance linear. |
| RZ11 | **Must change.** With must change on, chaos > 0 and a movable unit, Structure changes at least one key, every time, at every chaos from 1 to 1000 (the low end is where a bell pick would find only zero weights). Includes groups with their usual first move locked (`steps` locked, `quantize` exempt with the own scale unlocked, every shift locked). | Turn must change off inside the function; use the bell for an ordered key's move; give each group only its first move. |
| RZ12 | **Musical safety.** A newly drawn `steps` of 1–3 keeps the span within 24 semitones or 14 degrees; a `repeats` of 0 in `cur` stays 0, and a count never goes down; `rate` stays in its family and band; `pattern` never lands on `user`, `trigger_pulse` or `bar_trigger` unless home is that pattern; degrees are never drawn into while `cur.quantize` is off (a home or `cur` already in degrees is kept); every value lies in band ∪ {home}, except where a row admits only `cur` (repeats, steps under a locked size). Includes home ≠ cur. | Drop the span filter; let `repeats` draw below `cur`; light `trigger_pulse`. |
| RZ13 | **No allocation.** The allocation hook (A1's) sees none during 10,000 calls. | Allocate a scratch vector. |
| RZ14 | **Settings jumps in the core.** Sessions where a Both result at chaos 1000 replaces the snapshot every few blocks: N1 (one NoteOff per NoteOn), CH1, RP1, G1 (identical at block sizes 1, 7, 64, 512 and 4096) and `offsLost = 0` all hold. The jumps fall on multiples of 28,672 samples (the least common multiple of those block sizes), so each lands on a block start at every size. N2 is checked with `seed` locked (§5's caveat). | The existing N1 plant (drop one release) must still go red under this fuzz. |
| RZ15 | **Tables.** The generated header matches `spec/randomize.json` (drift guard, like `core_params_check.py`); the σ table regenerates bit for bit; the 54 key and group ids are collision-free. | Perturb one taste weight and one band in the parsed table. |

**Layer-E (measured, not gating):** keys changed per click and the no-change
rate, as horde's report §4 tabulates them; a listening pass in the lab (Q10).

**One gap to say plainly.** CI runs only `./verify fast`, on Linux. RZ1's
cross-platform claim needs the randomize digest in a Linux run too. That means
adding it to CI, which changes `./verify` or the workflow, and both are behind
the human's gate. It is part of what phase 2 asks for.

## 8. The spec amendment it will need (described, not applied)

`spec/` is protected (D-022, D-023). Phase 2 asks the human to approve:

1. **A new §15, "Randomize (D-044)"**, normative: the verbs and inputs (§2.1);
   `rdraw` with its constants (§2.2); the consideration rule and liveness on
   `cur` (§2.3); the unordered rule (§2.4); the integer bell with its σ and
   `exp` tables (§2.5); the three groups (§2.6); locks, exemptions and
   never-draw (§2.7); the unit order and must change (§2.8); chaos 0 (§2.9);
   and "the result is an ordinary settings change; §9 and §7 apply".
2. **A new normative companion, `spec/randomize.json`** (format
   `arp-randomize/1`), pinned in `spec/MANIFEST`. Per key: its class and group,
   exempt by default or not, how it is drawn, the band or lit values with taste
   weights, the interval tables, and "live when". `tools/gen_randomize.py`
   generates the C++ tables from it, as `gen_bank.py` does for the bank.
   `params.json` is untouched (Q6).
3. **§11 rows RZ1–RZ15** with their plants.
4. **§7: a T22 row,** "a randomize result arrives: a settings change, adopted
   as one snapshot. No terminal beyond the rows its keys already have (T8, T9,
   T15, T18, T20 and §14's re-map, T21)", on purpose, as T3 and T8 are
   (LIBRARY L0001: a row that produces no terminal must be shown to produce no
   extra and no missing one).
5. **§10:** the randomize draw is a separate function, an independent stream
   from the note lanes 0–4.
6. **§11 N2:** the invariant is stated for a fixed seed, or §4.2's shuffle is
   changed, as Q12 decides.
7. **The header:** "randomize added 2026-10-29 (D-044, a recorded exception to
   the freeze)".

## 9. Measured on a sketch (Layer-E, not the C++)

Two Python sketches of §2's integer rules (scratch files, not committed), over
2,000 seeds per row unless stated:

- **σ table (units of 2⁻¹⁶)** at chaos 100 / 250 / 350 / 500 / 750 / 1000: 775,
  3,485, 7,216, 18,536, 73,936, 262,144 (horde's 0.012, 0.053, 0.11, 0.28,
  1.13, 4.0). The critic recomputed these independently.
- **Chaos 0 is the identity**, on 500 seeds.
- **Amount nesting with the controlling keys on** (revision 2's rule: liveness
  on `cur`, the transposition group as one unit): `vel_random` 20,
  `gate_random` 0 and locked, `steps` 1 at +12. Amount pairs (300, 800) and
  (100, 1000), at chaos 350 and 1000, Values and Structure: **0 violations in
  16,000 pairs.** The planted control, revision 1's rule (liveness read from the
  keys just drawn), gives **12 violations**, so the check can see the bug.
  Revision 1's own sketch had used home at the defaults, where every
  controlling key is off, and could not find it.
- **No empty candidate set:** home `steps` 0–8 × both units × 300 seeds at
  chaos 1000, no locks. That sweep could not see revision 2's zero-weight
  cases (low chaos, a locked size, `repeats` below `cur`); revision 3 removes
  them by construction (the centre is always a candidate), and RZ3 asserts it
  at every chaos with the locks swept. **The span rule held:** 0 drawn spans beyond the bound (steps
  1–3), in 5,000 seeds.
- **`rate` from 1/16 (when unlocked),** share by rungs moved in its family:

  | Chaos | −3 | −2 | −1 | 0 | +1 |
  |---|---|---|---|---|---|
  | 100 | | | | 100 % | |
  | 250 | | | 5 % | 90 % | 5 % |
  | 350 | 0.2 % | 3 % | 25 % | 47 % | 24 % |
  | 500 | 11 % | 17 % | 23 % | 26 % | 23 % |
  | 1000 | 20 % | 19 % | 21 % | 20 % | 20 % |

  So at horde's default chaos (0.35) a drawn rate stays within one rung 96 % of
  the time, as horde's sketch asked ("within one step of home unless CHAOS is
  high"). The band stops it at 1/32 (+1 rung from 1/16).
- **`gate` from 80** (band 25–100): mean move 1.9 at chaos 100, 14.1 at 350,
  22.7 at 750; never outside 25–100.
- **A Structure click with no must change** (revision 1's sketch: rate exempt;
  `order`, `pattern`, `restart`, `vel_mode` and `steps` live at home):

  | Chaos / amount | Keys changed (mean) | No change |
  |---|---|---|
  | 350 / 600 | 0.76 | 44 % |
  | 600 / 800 | 1.76 | 10 % |
  | 1000 / 1000 | 3.15 | 0.2 % |

  At horde's defaults almost half the clicks would change nothing, the same
  finding as horde's small modules (report finding 1). So must change matters
  for Arp too.

## 10. Open questions for the human

Each has a recommendation, first.

1. **Should a random click leave the arp's rate alone unless the player
   unlocks it?** *Recommendation: yes.* It is locked by default, as horde's own
   exemption list proposes. When unlocked it stays in its family (straight,
   dotted or triplet) and between about a half note and a 1/32, so it stays in
   time with the song.
2. **Should it leave the scale settings alone by default** (the quantize mode
   and Arp's own scale)? *Recommendation: yes,* as horde's human ruled for
   horde's own scale (ADR-321). When unlocked, Arp's own scale is drawn as a
   named scale plus a root that moves round the circle of fifths, never as
   twelve random notes.
3. **Should latch never be randomized?** *Recommendation: never.* A click that
   turns latch on makes the arp keep playing after the keys are released, and
   one that turns it off cuts a latched performance. horde's lab sketch had it
   under architecture; the reply asks horde to drop it.
4. **Should switching the chord stage on or off never be randomized?**
   *Recommendation: never.* It adds or removes a whole note stream that horde
   routes to its own oscillators. When the stage is already on, its intervals
   and slots are drawn.
5. **What should chaos 0 mean when horde's home is not the current sound?**
   *Recommendation: follow horde's law.* Chaos 0 puts the drawn settings back
   to home, and returns the current settings unchanged when home is the current
   sound. The knob then has no jump just above 0.
6. **Where should the randomize table live?** *Recommendation: a new file,
   `spec/randomize.json`, beside `params.json`.* Adding fields to `params.json`
   would change its sha256, which horde uses as every preset's
   `module_version` (our seq 12), for no change to any setting.
7. **Should Values also re-roll the seed** (a new take on the same settings),
   when something uses it? *Recommendation: yes.* It keeps the seed with
   probability 1 − chaos, so it is rare at low chaos. See question 12 for one
   side effect.
8. **Should "repeat forever" never be randomized into a count, and a count
   only move up?** *Recommendation: yes.* Otherwise a click can make a held arp
   stop after a few cycles, or stop it at once, which reads as broken.
9. **Should three patterns never be drawn into: build-your-own, Trigger pulse
   (it plays no notes) and Bar trigger (it fires a trigger horde routes)?**
   *Recommendation: yes.* A home on any of them is still kept.
10. **Should phase 2 add a Randomize panel to the lab,** so the taste weights
    and bands can be tuned by ear? *Recommendation: yes.* horde's report says
    its chaos curve has to be judged by listening (its Q9). Arp's weights are
    the same kind of thing. They live in `spec/randomize.json`, so retuning them
    never touches code, but it does pass the spec's gate.
11. **Is it enough that "more amount changes more keys, with the same values"
    holds exactly for each verb on its own,** but not fully for Both (randomize
    patch)? *Recommendation: yes.* Making Both nest too would mean Values
    ignores what Structure just chose, so a fresh "fixed velocity" would get no
    velocity drawn until the next click. Only two keys are affected: the fixed
    velocity and the seed.
12. **A seed re-roll can make shuffle repeat or skip one note, once.**
    Shuffle's "never the same note twice in a row, every note once per cycle"
    is computed from the seed, so changing the seed mid-cycle breaks it for that
    cycle only. A morph of the seed does the same today. *Recommendation:*
    accept it, write the rule in the spec as holding for a fixed seed, and
    listen for it in the lab. The alternatives are not re-rolling the seed while
    the order is shuffle, or changing the core so a new seed takes effect only
    at the next cycle (a core change behind the freeze).

## 11. Out of scope, and after 1.0

- Drawing the build-your-own pattern's steps (D-044: settings only).
- horde's per-value dice and per-key bounds from a randomization profile
  (ADR-318). If horde needs profiles to narrow Arp's bands, an optional
  per-key bounds argument can be added; the reply asks.
- Joint voicing rules for the chord stage, such as never two shifts a semitone
  apart. Per-slot draws are independent in 1.0.
- Changing a rate's family (straight, dotted, triplet) at high chaos.
- Live re-application of the last draw when a bound moves: horde removed it by
  ruling (ADR-318), so Arp does not plan it.

## 12. Revision 2: the critic's findings and what changed

The independent critic (Opus) reviewed revision 1. Its verdict was **REWORK**:
2 HIGH, 7 MED, 14 LOW. It found no path that breaks a terminal invariant: the
core adopts a whole snapshot at once. All findings are applied:

| Finding | What changed |
|---|---|
| **Round 1** | |
| **H1** Liveness and joint rules read keys drawn earlier in the same click, so the amount-nesting law failed (counterexamples: `steps` killing `step_size`, `vel_random` killing `random_curve`) | Liveness reads `cur`; coupled keys became three groups with one amount threshold each (§2.3, §2.6). Measured: 0 violations, and the old rule as a plant gives 12 (§9). Both's partial nesting is Q11. |
| **H2** Empty or zero-weight candidate sets: `step_size` under the span rule with a home `steps` of 5–8; must change's bell at low chaos | A home `steps` above 3 keeps its own shape; the tables always hold an interval for steps 1–3 (sketch-swept); must change moves an ordered key to the nearest rung, never by bell (§2.6, §2.8). RZ3 and RZ11 gain cases and plants. |
| **M1** Home and cur mixed in joint rules (`repeats` live on home; the span limited by home's size; unit flips undefined at chaos 0) | One rule: `cur` decides liveness and locks; home only centres the bell; forced redraws do not run at chaos 0 (§2.3, §2.6, §2.9). |
| **M2** `chord_unit` decided before `chord_on`; a newly enabled slot could keep a shift of 0 | Order is slots, unit, shifts; any locked shift pins the unit; a slot that turns on gets a forced shift (§2.6). |
| **M3** The arp's keys ignored `arp_enabled` and `enabled` | "Arp live" added to every arp key's "live when" (§3). |
| **M4** Three plants could not fail (RZ7, RZ3, RZ8) | RZ7's plant is now revision 1's liveness rule and an amount-dependent pick; `trigger_pulse` moved to RZ12; RZ8's oracle is the literal class list (§7). |
| **M5** The reply's count was wrong ("44 others") | 15 exempt, 29 drawable by default, 6 never drawn (horde's device class). |
| **M6** A seed change can break shuffle's N2 | Stated in §5, checked with `seed` locked in RZ14, put to the human as Q12. |
| **M7** The circle-of-fifths ladder was not circular | Distance `min(|d|, 12 − |d|)` (§2.5). |
| **L1** The step-time ladder's top rung exceeded the maximum | 92 geometric rungs, then 2,000,000 (§2.5). |
| **L2** T22 understated; the re-map list missed `quantize` | Both fixed (§5, §8). |
| **L3** "Q16" read as a question; §8 said RZ1–RZ12 | "Units of 2⁻¹⁶"; RZ1–RZ15. |
| **L4** The `float chaos` sketch was attributed to horde's brief | It was the lead's task sketch; the departure from horde's two-verb proposal is now stated (§4) and in the reply. |
| **L5** Keeping an unlit home departs from horde unflagged | Flagged (§2.4). |
| **L6** The scale group's threshold and key id were unspecified | The scale group, keyId `"scale"` (§2.6). |
| **L7** `chaosPm` and `amountPm` not clamped | Clamped on entry (§2.1, §4). |
| **L8** "A single correctly rounded operation" overstated | Reworded: horde's float arithmetic is horde's; exact from the permille integers on (§2.1). |
| **L9** "Never coincide" with the note lanes | "An independent stream" (§2.2, §8). |
| **L10** Degrees with quantize off make whole-step clusters | Degrees lit only while `cur.quantize ≠ off` (§2.6, §3). |
| **L11** The dotted band started at a dotted half | Now 3/8 (a dotted quarter) to 3/64 (§3). |
| **L12** Lowering `repeats` can exhaust a running arp | A count only moves up, or back to forever (§3, row 20). |
| **L13** RZ14's jump samples unspecified | Multiples of 28,672 (§7). |
| **L14** Process: no ROADMAP criteria in this tree; the reply said `randomize.json` "is pinned" | The trace notes the first (the lead owns ROADMAP); the reply says "will be pinned". |
| **Round 2** (verdict REWORK: 2 HIGH, 1 MED, 3 LOW; H1 and H2 confirmed closed for the cases reported) | |
| **N1** Home was no longer always a candidate: `repeats` below `cur` (empty set at cur 12 / home 5; zero weights at low chaos; a count going down at chaos 0), `steps` under a locked size | The bell's centre is home if admitted, else `cur`'s value, and is always a candidate (§2.5); `steps` under a locked size always admits `cur.steps` (§2.6); `repeats` row 20 redefined; RZ3 asserts no zero total weight at every chaos with locks swept, with a plant. |
| **N2** Must change breaks the "exact" nesting law, and the reply claimed it | The law is stated with must change off, and must change is named as the one exception (§2.3); RZ7 runs with must change off; the reply corrected. |
| **M1** Must change judged a group movable, then moved a fixed member that could be locked | Movability is judged by the moves; each group's moves fall through its members in a fixed order (§2.8); RZ11 covers the locked first moves. |
| **L1** RZ6 asked too much; a unit flip with no passes left an old interval waiting | RZ6 narrowed to drawn members; `step_unit` is drawn only when `steps > 0` (§2.6). |
| **L2** The step-time-rung plant could not fire | Dropped; replaced by the zero-weight plant. |
| **L3** Wording in RZ12 and the reply | RZ12 keeps a home or `cur` already in degrees; the reply's band sentence names the exceptions. |
| **Round 3** (verdict **PASS**: N1, N2 and M1 closed, nothing new at HIGH or MED; one LOW) | |
| **LOW** The bell's distance and chaos 0 were still worded "from home" | "From the centre" (§2.5, §2.9). |
