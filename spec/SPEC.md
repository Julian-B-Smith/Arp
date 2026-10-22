# Arp — 1.0 specification (DRAFT r2.2, Phase A)

> **Status: DRAFT r2.1, 2026-10-21.** r1 was reviewed by an independent Opus
> critic, which found 13 HIGH, 9 MED and 4 LOW issues (verdict: rework). r2
> folds in every finding; the per-finding record is `docs/spec-review-r1.md`.
> r2.1 adds §13, which settles the 17 gaps the lab build surfaced. §13 is
> normative and outranks the section it clarifies. r2.2 adds the output
> stream tag (D-009, horde ADR-270).
>
> - This file becomes a protected path once the human ratifies it (the
>   ROADMAP Phase A gate).
> - Rules marked **PROPOSED** are the lead's answers to open questions (Q-A1,
>   Q-A2, Q-A3, Q-A5), settled by playing the lab.
> - **Normative companions:** `spec/params.json` and `spec/bank/starter.json`.
> - **Upstream terms:** `intake/`, horde ADR-267 A1 (triggers), DECISIONS
>   D-000…D-008. The prior-art basis is `docs/prior-art.md`.

Arp is a **pure note processor**. Each block it takes:
- input events,
- the transport,
- a settings snapshot,

and it returns output note events and trigger events, each at a sample offset
in the block. It reads no clock, allocates nothing after `prepare`, and never
assumes an engine downstream.

**Arithmetic conventions:**
- `fdiv(a,b) = ⌊a/b⌋` and `cdiv(a,b) = ⌈a/b⌉` are mathematical floor and
  ceiling for any sign of `a`, with `b > 0`.
- `mod(a,b) = a − b·fdiv(a,b)` is always in `[0, b)`.
- C++ `/` and `%` are **not** these when `a < 0`.
- All settings arrive as **integers** (§9). Every rounding below is stated.

---

## 1. Interface

```
prepare(sampleRate, maxBlock)   // sampleRate rounded to an integer, 8,000–768,000 (IF-11);
                                // maxBlock ≤ 4096; allocates every buffer; resets all state
process(block) -> outEvents     // real-time; no allocation, no locks, no syscalls
reset()                         // T10
```

### Block input
- `n`, with 1 ≤ n ≤ maxBlock;
- `transport` (§2.2);
- `settings`: integers per `params.json`, resolved by horde's morph (§9);
- `scale`: root 0–11 plus a 12-bit mask, read-only (horde B260);
- `in[]`: input events, sorted by offset, `0 ≤ offset < n`:

| Event | Meaning |
|---|---|
| `NoteOn{ref, key, vel}` | a key press |
| `NoteOff{ref}` | a key release |
| `Pedal{down}` | CC 64; down when the value is ≥ 64 |
| `Panic` | all-notes-off or all-sound-off |
| any other event | e.g. pitch bend, channel pressure, other CCs, per-note expressions |

### Block output
- `outEvents`: sorted by offset, `0 ≤ offset < n`. Capacity: §1.3.
  - `NoteOn{ref, key, vel, stream = arp}`;
  - `NoteOff{ref, releaseVel = 64}`;
  - `Trigger{lane}`;
  - `Panic` (forwarded, §7 T7);
  - pass-through events.
- **Pass-through.** Every input event that is not a NoteOn, NoteOff, Pedal or
  Panic is copied to the output unchanged, at its offset. That includes
  pitch bend, channel pressure, other CCs and per-note expressions. Per-note
  expressions addressed to input refs therefore reach no Arp note. Mapping
  them is after 1.0 (Q-H4).
- **While disabled** (§7 T4): Arp emits only its own releases and the
  pass-through. horde routes input notes **around** a disabled Arp; Arp never
  echoes them.
- **Streams (D-009, horde ADR-270).**
  - **Every** `NoteOn` Arp emits carries `stream = arp`, whatever produced it
    (steps, ratchets, chord steps).
  - The player's held notes reach horde's engine as the `direct` stream
    through **horde's own tee**, placed before Arp. **Arp never echoes input
    notes**, enabled or not.
  - `NoteOff`s and expressions address notes by id, as before.

### 1.1 Order of events at one offset
Events at the same offset are emitted in this order:
1. `NoteOff`s, by ascending output id;
2. `Panic`;
3. `NoteOn`s, in the step's order (§4.2), then by ascending key;
4. `Trigger`s, by ascending lane;
5. pass-through events, in input order.

**Exception:** a note's `NoteOff` is never emitted at the **same sample** as
its own `NoteOn`. If its off falls on that sample, it is emitted one sample
later (in the next block if needed). Every note therefore lasts at least one
sample (H4).

### 1.2 Identity (`intake/note-vocabulary.md`)
- Input notes are matched by `ref`.
- Output notes get **Arp's own refs**: `(port 0, channel, key, id)`.
  - `channel` is that of the source held key.
  - `id` comes from a per-instance counter that starts at 0 at `prepare` and
    `reset`, which makes ids deterministic.
- A `NoteOff` carries exactly its `NoteOn`'s ref.
- Arp emits base keys only; it never bends.

### 1.3 Capacities (fixed at `prepare`; nothing grows)

| Thing | Capacity | On overflow |
|---|---|---|
| held keys | 32 | a new key is ignored, and so is its later NoteOff (counted) |
| live refs per held key | 4 | an extra ref is ignored, and so is its NoteOff (counted) |
| sounding output notes | 128 | T11 |
| pending ons (§5.1) | 512 | the step's remaining ons are refused (counted) |
| output events per block | `max(1024, 4·maxBlock)`, with **128 slots reserved for NoteOffs** | a NoteOn that would eat into the reserve is **refused**: it is never emitted and never enters the sounding table (counted, T13). A NoteOff always fits. |

- **Tempo** is clamped to **[20, 999] bpm**, after horde's rule that a tempo
  ≤ 1 means 120.
- So the steps per block are bounded: at most `cdiv(maxBlock · 999 · TPQ, 60 · sr · 960) + 1`.

---

## 2. Time

### 2.1 The tick grid
- **TPQ = 15,360 ticks per quarter note.** Every horde division is an exact
  integer step length `L` (`params.json` → `rate`). The smallest is 960.
- Step `kAbs` (an integer, negative allowed) has grid tick `kAbs·L`.
- Ticks are never accumulated.

### 2.2 Exact position
**Units.** The exact position lives in `posNum` units:
- `D = 60 · sr · 1000`;
- tick `t` corresponds to `posNum = t·D`;
- tempo in milli-bpm `Tm = round(bpm·1000)`;
- each sample advances posNum by `inc = Tm · TPQ`.

**Storage.** Positions **and** every pending event time are stored as signed
128-bit integers (`__int128`; `BigInt` in the lab). 64-bit storage overflows
in under two hours at 768 kHz.

**Transport snapshot per block** (the adapter fills it from CLAP/VST3/AU):
- `playing`;
- `hasBeats`, `beats` (the block-start position);
- `hasTempo`, `tempo`;
- `timeSigNum`, `timeSigDen` (default 4/4);
- `barStartBeats` (optional);
- loop flags.

**The position rule (three bands, H1):**
1. The prediction is `pred = prevStart + prevN · prevInc`.
2. The report is `rep = round(beats · TPQ · D)` (one rounding, half away
   from zero).
3. The band, with `Δ = rep − pred`:

| Band | Condition | Action |
|---|---|---|
| **continuous** | `Δ = 0` | `start = pred` |
| **re-base** | `0 < \|Δ\| ≤ 960·D` (1/16 beat) | `start = rep`, silently. No terminals. Pending events keep their tick positions. The once-guard (§4.1) means no grid step fires twice or is skipped. |
| **jump** | `\|Δ\| > 960·D` | `start = rep`; T6 |

Host rounding jitter, tempo ramps read once per block, and fractional tempos
all land in the re-base band. A simulated exact host is always continuous.

**More adapter rules:**
- **Tempo:** with `hasTempo` false, use the last valid tempo, else 120.
- **CLAP:** a mid-block transport event splits the block.
- **Loops:** a loop wrap inside a block splits it there, and the wrap is a
  jump.

### 2.3 Sample offset of a position
Inside a block that starts at `start`, an event at position `x` (in posNum)
has offset

    s = cdiv(x − start, inc)

It belongs to this block iff `0 ≤ s < n`. That makes the block a half-open
window. **Offsets are never clamped**; one outside the window is an assertion
failure.

**G1:** for an exact host, every event lands on the same absolute sample at
any block partition.

### 2.4 Two clocks
1. **The host clock** runs while `playing ∧ hasBeats`.
2. **The free clock** runs otherwise. It is live play with the transport
   stopped, a first-class case.
   - It counts samples at the current tempo (else 120).
   - It runs from a **gesture start** (§3) until Arp is **idle**: H is empty,
     nothing is sounding, and nothing is pending. Then it stops.
   - **Re-base at a gesture start** (H7a). The free clock sets `posNum = 0`
     at the gesture's sample, and **every pending event and sounding off
     shifts by the same Δ**. Their remaining durations are exact, and no
     terminal fires.
   - **Entry with keys held** (H7b). On a switch to the free clock (T6) with
     H non-empty, the free clock starts at the switch sample, with
     `posNum = 0` there.
   - **Epoch.** Every free-clock start or re-base increments
     `epoch = (epoch + 1) mod 2³²`. The epoch is mixed into every draw
     (§10), so `random` and `prob` do **not** replay the same sequence on each
     chord while the transport is stopped (H7c). Under the host clock,
     `epoch = 0`.
3. A switch between clocks is a transport edge (T6).

### 2.5 Steps run only when the clock runs (H11)
- A grid step is **evaluated** iff the active clock is running **and** an
  anchor exists (§4.5).
- The host clock runs whenever the transport plays, **including while H is
  empty**. Trigger-only steps and triggers on note steps then fire even with
  no keys held, under `restart` = `bar` or `free`.
- Under `restart = chord` there is no anchor before the first gesture.
- While stopped and idle, nothing runs, so no triggers fire.

---

## 3. The held set

Each input `ref` is in one state: **DOWN**, **PEDAL** (released while the
pedal is down), or **LATCHED**.

**The held keys.** `H` is the set of distinct keys over all live refs.
- Each held key carries the channel and velocity of its **earliest live** ref.
- Its press order is the press sample of its earliest live ref (ties broken
  by key).
- Refs on one key share that one held key, which stays while any ref is live.

**A gesture start** is a `NoteOn` that arrives when **no ref is DOWN or
PEDAL**. LATCHED refs don't count, so a latch-replace re-press is a gesture
start (H12).

**Events:**
- **NoteOn(ref):**
  - At a gesture start under latch **replace**: every LATCHED ref is dropped
    first.
  - Under latch **append**, if the key is held only by LATCHED refs: the
    press **toggles the key out**. Those refs are dropped, and this ref is
    recorded as **ignored**, so its NoteOff is ignored too.
  - Otherwise the ref becomes DOWN, subject to the capacities.
- **NoteOff(ref):**
  - A ref that is unknown or ignored: no effect.
  - DOWN → PEDAL if the pedal is down.
  - Otherwise, with latch off, the ref is removed; with latch on, it becomes
    LATCHED.
- **Pedal up:** each PEDAL ref is removed (latch off) or becomes LATCHED
  (latch on).
- **Pedal down:** no immediate effect.
  - The pedal is **global** in 1.0: one pedal for all input channels. It
    holds Arp's **input** (D-000 Open 3).
  - CC 64 is consumed, never forwarded.
  - Per-channel and MPE-zone pedals are after 1.0 (Q-H4).
  - The `direct` stream gets its pedal from horde's tee, not from Arp. We
    proposed to horde (Q-H5) that the engine's pedal holds `direct` voices
    only, never `arp` voices, so Arp's notes never pile up as tails.
- **Latch turned off:** every LATCHED ref is removed.
- **Panic:** all refs and the pedal state are cleared (T7).

**Sounding notes.** A held-set change never cuts a sounding note (T3). The
pattern reads `H` only when a step is evaluated.

**While disabled.** H keeps tracking input while `enabled` is off, so turning
Arp on with a chord already held plays it (H13).

---

## 4. Pattern generation

### 4.1 Counters and the once-guard

| Counter | Definition |
|---|---|
| `kAbs` | the grid step index on the active clock; used for **on-times, swing parity and draws** |
| `anchorK` | the anchor step (§4.5) |
| `k = kAbs − anchorK` | steps since the anchor |
| `j = mod(k, patternLength)` | step-pattern position |
| `m` | **played-note counter**; set to 0 at every anchor; +1 only when a step plays new notes (§4.3) |

**The once-guard (H1, H6).** Arp keeps `lastEvaluated`.
- A block evaluates every `kAbs > lastEvaluated` whose **grid tick**
  `kAbs·L` lies before the block's end, in ascending order.
- So a re-base backwards never re-evaluates a step.
- A re-base forwards evaluates any passed step **late**, at offset 0.
- On a jump, `lastEvaluated` is reset to the step before the new position.

### 4.2 Order styles
**The sorted list** (no duplicates):

    E = sorted(unique(H ∪ (H+12) ∪ … ∪ (H+12·(oct−1))))

`e = |E|`, and `oct` is the `octaves` setting (1–4).

**The press-order list:** `Pp` = the held keys in press order, followed by the
same list +12, +24 and so on, for `oct` blocks. Duplicates are kept.

**Order position:** `p = mod(m, period)`. With `e = 0`, no note plays (§4.3).

| id | Period | Note(s) at position p |
|---|---|---|
| `up` | e | E[p] |
| `down` | e | E[e−1−p] |
| `bounce` | e = 1: 1; else 2e−2 | p < e ? E[p] : E[2e−2−p] |
| `bounce2` | 2e | p < e ? E[p] : E[2e−1−p] |
| `played` | \|Pp\| | Pp[p] |
| `chord` | oct | every key of H, +12·p; collisions after quantize resolve per §6 |
| `random` | — | E[ draw(seed, kAbs, 0, epoch) mod e ] |
| `shuffle` | e | P_c[p], c = fdiv(m, e); P_c = Fisher–Yates of E using draw(seed, c·256 + i, 1, epoch) for i = e−1 … 1 |
| `converge` | e | E[0], E[e−1], E[1], E[e−2], … (q-th = q even ? E[q/2] : E[e−1−(q−1)/2]) |
| `diverge` | e | converge read backwards: q-th = converge[e−1−q] |
| `pedal_low` | e = 1: 1; else 2(e−1) | p even → E[0]; p odd → E[1 + (p−1)/2] |
| `pedal_high` | e = 1: 1; else 2(e−1) | p even → E[e−1]; p odd → E[e−2 − (p−1)/2] |

**Which key is "source"** for channel and velocity: for an octave copy, the
held key it was copied from.

**PROPOSED (Q-A3):** the lab exposes all twelve; the starter bank is chosen
by playing.

### 4.3 Step patterns (data)
A pattern is `length` steps (1–64). Each step carries:
- `on`;
- `tie`;
- `vel` (0–200 %);
- `gate` (1–200 %);
- `oct` (−2…+2);
- `prob` (0–100 %);
- `ratchet` (1–4);
- `trig` (0–8).

Pattern `none` is a single plain step.

**Evaluating step kAbs** happens once, in the block whose window holds its
grid tick, using **that block's settings snapshot** (H6). For step `s = steps[j]`:
1. `fires = (draw(seed, kAbs, 2, epoch) mod 100) < s.prob`.
2. **If `s.tie`**: no new note and no change to `m`. The tie was already
   honoured by look-ahead (§5). Fire the trigger if `fires ∧ s.trig ≠ 0`.
3. **Else if `s.on ∧ fires ∧ e > 0`**: **play**.
   - Take the note(s) at `p`.
   - Apply `s.oct·12`, then quantize (§6).
   - **Drop** any key outside 0–127 (counted; H10).
   - Schedule the ons (§5). `m += 1`, even if every key was dropped.
   - Fire the trigger if `s.trig ≠ 0`.
4. **Else if `fires ∧ s.trig ≠ 0`** (a rest, or `e = 0`): fire the trigger.
   This is the trigger-only case, consistent across patterns (H11).
5. Otherwise the step is a rest.

### 4.4 Velocity
- `base` is the source key's velocity (mode `played`) or `vel_fixed` (mode
  `fixed`).
- `out = clamp(fdiv(base · s.vel + 50, 100), 1, 127)`. That is
  round-half-up in integers.

### 4.5 Restart: the anchor
| `restart` | Anchor |
|---|---|
| `chord` | the first grid step at or after a **gesture start**; under latch **append**, a press that adds to a non-empty latched set does **not** re-anchor. Also re-anchors at: enable (T5), a jump (T6), a rate change (§9) — each to the first grid step at or after that point |
| `bar` | the first grid step at or after each **bar start**. Bars come from `barStartBeats` when given, else from tick 0 with `timeSigNum/Den` (default 4/4): bar length = `TPQ·4·num/den` ticks. 10 of 22 rates don't divide a bar; the "first step at or after" rule handles them (H9) |
| `free` | the first grid step at or after tick 0 of the active clock; re-anchors only on a jump or a clock switch |

At every anchor `m := 0` (H8).

---

## 5. Note timing

### 5.1 Ons, swing and the pending queue
- The **span** of step kAbs runs from its swung on to the next step's swung on:

      swingDelay(x) = (x odd) ? fdiv(L · (2·swing − 1000), 1000) : 0
      on(kAbs)      = kAbs·L + swingDelay(kAbs)
      span(kAbs)    = on(kAbs+1) − on(kAbs)

  `swing` is in permille, 500–750: 500 is straight, 667 is a triplet feel,
  750 the maximum. Parity comes from `kAbs`, so swing locks to the song grid
  whatever the restart.
- **H2:** the on-time is always anchored at `kAbs·L`, never at `k·L`.
- **Ratchets:** sub-note i (0 ≤ i < r) starts at `on + fdiv(i · span, r)`.
- All of a step's ons are **frozen at evaluation** into the pending queue,
  with their positions in posNum. Each is emitted in the block whose window
  holds it. A release-all **clears** the queue (H6).

### 5.2 Lengths and ties
- **Base length:**
  - without ratchet, `len = max(1, fdiv(span · gate · s.gate, 10000))`;
  - with ratchet r, `len = max(1, fdiv(fdiv(span, r) · gate · s.gate, 10000))`,
    capped at `fdiv(span, r)`. Floor first, then gate.
- **Ties by look-ahead (H3).** When step kAbs plays, let `t ≥ 0` be the
  number of consecutive tie steps that follow it in the pattern data, read
  cyclically, with no regard to `prob`. When t > 0, the off tick of the
  step's notes (the **last** sub-note only, if ratcheted) is:

      off = on(kAbs + t) + max(1, fdiv(span(kAbs + t) · gate · steps[j+t].gate, 10000))

  Ties therefore engage at any gate. Every note of a chord step is extended.
  A tie after a rest, or at pattern position 0 after a rest, acts as a rest.
- **Off position:** `off = on + len` in ticks, stored as posNum.
  - Offs are scheduled in **ticks**, so a tempo change moves them with the
    music.
  - The 1-sample minimum of §1.1 applies.
- **Same key still sounding (T2).** "Same" means the same **(channel, key)**.
  If a new on arrives while that key sounds, the old note's `NoteOff` and the
  new note's `NoteOn` go out at one sample, in that order.

---

## 6. Scale quantize
- When `quantize` is on and the mask is non-zero, each output key moves to
  the nearest in-scale key **within 0–127**. A tie goes to the **lower** key
  (**PROPOSED**, Q-A5).
- **Collisions.** When two keys of one step land on one key, it is emitted
  once, with the channel and velocity of the **earliest** source in the
  style's order.
- The scale is read at step evaluation. A scale change never moves a
  sounding note.

---

## 7. Terminals

**Invariant N1.** Every emitted `NoteOn` gets **exactly one** `NoteOff`:
- with the same ref,
- at least one sample later,
- on every path, at every block partition.

**The mechanism.** `release(ref)` is the **only** path to a `NoteOff`. It is
idempotent and clears the sounding entry. **Release-all** walks the sounding
table **and clears the pending queue**.

| # | Path | Effect |
|---|---|---|
| T1 | gate end (after look-ahead ties) | `NoteOff` at the off position |
| T2 | the same (channel, key) retriggered while sounding | old off, then the new on, at one sample |
| T3 | held-set change | none; notes finish their gate |
| T4 | `enabled` → off (setting or bypass), applied at **offset 0 of the block whose snapshot changes it** | release-all at that offset; no new steps |
| T5 | `enabled` → on, same timing | nothing sounding; anchor per §4.5 |
| T6 | transport edge: start, stop, jump (§2.2 band 3), loop wrap, clock switch | release-all at the edge's offset; re-anchor; `lastEvaluated` reset |
| T7 | `Panic` in | release-all at that offset; clear H, latch state and pedal; **forward `Panic`** after the offs, so the engine still hard-cuts (IF-1 all-sound-off) |
| T8 | a discrete setting changes | none to sounding notes; it applies from the next evaluated step |
| T9 | a continuous setting changes | none; read at step evaluation |
| T10 | `reset()`, `prepare()`, deactivate | release-all; the adapter delivers the offs to the engine directly |
| T11 | the sounding table is full | the oldest sounding note is released at the new on's offset, before it (counted) |
| T12 | a look-ahead tie | none; one note with one later off |
| T13 | the event cap's off-reserve would be breached | the new NoteOn is refused: never emitted, never sounding (counted) |
| T14 | a free-clock re-base | none; pending positions shift exactly |
| T15 | a rate change | none; offs keep their ticks; re-anchor per §4.5 |
| T16 | a key dropped outside 0–127 | none emitted, so nothing to end |

T3, T8, T9, T12, T14, T15 and T16 are rows on purpose: each must be shown to
produce no extra and no missing terminal (LIBRARY L0001).

---

## 8. Triggers (horde ADR-267 A1; D-007)
- A step fires `Trigger{lane = s.trig}` at its **first** on position (after
  swing; never per ratchet) when §4.3 says it does.
- A trigger is a performance event: no state, no terminal, no history.
- Lanes 1–8 are Arp's outputs. Routing them is horde's (**Q-H3**).
- Release-all does **not** cancel a trigger that has already been emitted.
  It does cancel a pending one: triggers wait in the pending queue like ons.

---

## 9. Settings, morph and timing of changes
- All settings arrive as **integers** (`params.json`). horde rounds morph
  blends to the nearest integer before the snapshot.
- **Continuous settings:** `gate` (1–200 %), `swing` (500–750 ‰), `vel_fixed`.
- **Discrete settings:** all the rest. All of them are morph targets
  (D-000 Open 5).
- **Q-A1, PROPOSED.** horde resolves a discrete setting to the value of the
  corner with the largest weight, **with hysteresis**: it switches only when
  the leading corner's weight exceeds the current value's corner weight by
  0.05. Ties go to the lower corner index. Arp applies any change from the
  next evaluated step. A change of `rate` applies at the next step of the
  **old** rate. With a long rate (2/1) that can lag up to 8 beats. That lag
  is accepted and audible in the lab.
- **Q-A2, PROPOSED.** A change of `restart` takes effect from the next
  evaluated step:
  - into `bar` or `free`: the anchor is recomputed per §4.5, and `m := 0`;
  - into `chord`: the anchor is kept until the next gesture start.
- **`enabled`** is the single source for on/off. It applies at offset 0 of
  its block, not at the next step (T4, T5).

---

## 10. Determinism

    draw(seed, x, lane, epoch) = mulberry32( seed ⊕ imul(x, 0x9E3779B1) ⊕ imul(lane, 0x85EBCA77) ⊕ imul(epoch, 0xC2B2AE3D) )

- It takes the first output, in uint32 arithmetic, with `x` taken mod 2³².
- **Lanes:** 0 is `random`, 1 is `shuffle`, 2 is `prob`.
- No RNG state is carried between steps.

**D1.** These must all match to give an identical output stream:
- the same input events at the same absolute samples;
- the same transport;
- the same settings;
- the same seed.

---

## 11. Invariants (each with a planted must-fail control)

| id | Invariant | Planted control |
|---|---|---|
| N1 | exactly one NoteOff per NoteOn, same ref, ≥ 1 sample later, over sessions exercising T1–T16 | drop one release; double one release; emit a 1-tick note's off at its on sample |
| G1 | event absolute samples identical at block sizes 1, 7, 64, 512, 4096 | clamp offsets; accumulate the position in float |
| G2 | ons land at `cdiv` of their exact position | round instead of ceil |
| G3 | swing parity follows `kAbs`, not `k` | use `k` under `restart = chord` |
| G4 | the **tick** of every event is identical at 44.1, 48, 96 and 192 kHz (samples differ by rate) | read a sample-rate-dependent value into tick math |
| G5 | under re-base jitter (the host position perturbed by ±1/32 beat), every grid step is evaluated exactly once | drop the once-guard |
| D1 | same seed → same stream; a different seed changes `random` / `shuffle` / `prob` output | put a time-varying value in the draw |
| Q1 | every quantized key is in the mask and in 0–127 | skip quantize on chord steps |
| R1 | rest, miss, tie and trigger-only steps don't advance `m` | advance `m` on rests |
| P1 | `prob` 0 never fires and 100 always fires (exact); 30 and 50 are within ±2 % over 10,000 steps | use `<=` (caught at the exact endpoints, since a ±2 % band cannot see it) |
| T1 | triggers fire once per firing step, at its first on | fire on every ratchet |
| S1 | every emitted NoteOn carries `stream = arp` | drop the tag on some ons |
| K1 | ties engage at gate 80 (`tied_legato` ≠ `none`) | evaluate ties at the tie step instead of by look-ahead |
| C1 | at the event cap, no NoteOn is emitted without its NoteOff | drop the off-reserve |
| A1 | `process` never allocates (C++ only: an allocation hook) | allocate in `process` |

---

## 12. Open, decided by playing the lab and then ratified
- **Q-A1:** the discrete morph rule, with hysteresis (§9).
- **Q-A2:** restart under morph (§9).
- **Q-A3:** the starter bank (§4.2 orders; `bank/starter.json` patterns).
- **Q-A5:** the quantize tie direction (§6), and pre-roll (**PROPOSED:**
  played; the grid extends to negative ticks).
- **Q-H3** (horde): the trigger-lane handoff.
- **Q-H4** (horde, after 1.0): per-channel and MPE pedals; mapping per-note
  expressions onto Arp's output notes.

---

## 13. Clarifications (r2.1, from the lab build; normative)

The implementer marked 17 places as `SPEC-GAP`, each taking the simplest
reading. The lead adopted each reading below as the rule, unless the entry
says otherwise.

1. **Late events.** An event or step whose position is before the block's
   start (only after a forward re-base) is emitted at offset 0. The §2.3
   assertion applies only to positions at or beyond the block's end.
2. **The evaluation window** is §2.3's: a grid tick belongs to the block iff
   `cdiv(x − start, inc) < n`. §4.1's "before the block's end" means exactly
   this.
3. **The event cap.** A NoteOn is refused when
   `events + sounding + 2 > cap`, which guarantees every owed NoteOff fits.
   Triggers, a forwarded Panic and pass-through events use the same room
   test, and are dropped and counted when it fails.
4. **The chord anchor** is `lastEvaluated + 1`: the first step not yet
   evaluated at the gesture's sample.
5. **Re-anchoring needs a reason.** Under `chord`, enable, an edge or a rate
   change re-anchors only if an anchor already exists or H is non-empty (§2.5
   wins over §4.5's list).
6. **Latch append:** a press that joins a non-empty latched set is not a
   gesture start. It neither re-anchors nor re-bases the free clock.
7. **A free-clock start or re-base anchors:**
   - `lastEvaluated := −1`, `m := 0`;
   - `anchorK := 0` under `chord` and `free`;
   - bars are recomputed under `bar`.
8. **Rate change:**
   - the new grid anchors at the first new-grid step at or after the old
     step's grid tick;
   - under `chord`, `m := 0`;
   - under `free`, `anchorK := 0` and `m` is kept;
   - under `bar`, the anchor is re-expressed in the new rate and `m` is kept;
   - at an edge or at enable, the new rate is adopted immediately.
9. **A Panic while disabled is still forwarded.** That is an exception to
   §1's "only releases and pass-through".
10. **Ties:**
    - the look-ahead counts at most `length − 1` consecutive ties;
    - a tie step never plays a note, even after a rest (§5.2's "acts as a
      rest");
    - if the pattern changes mid-tie, the note keeps its scheduled off.
11. **Out-of-range keys stay in E**, so they count toward `e` and the period.
    They are dropped only at output (T16), and `m` advances anyway.
12. **`barStartBeats` is a phase:** bars fall at `barStart + i·barLength`.
13. **The adapter may pass `rep` exactly** (a BigInt or `__int128`). Only a
    floating `beats` fallback rounds, half up.
14. **Steps passed with no anchor** consume the once-guard. They are skipped,
    not queued.
15. **Idle** (§2.4) counts pending triggers as pending work.
16. **The step bound** (§1.3) is asserted in C++ only (M1).
17. **Enable with keys held under the free clock** starts the clock at offset
    0 of that block, as a clock switch would (§2.4 H7b).
