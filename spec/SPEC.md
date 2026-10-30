# Arp — 1.0 specification (RATIFIED 2026-10-23, r2.12)

> **Status: RATIFIED by the human, by poll, 2026-10-23 (D-022); errata E1 and E3 approved 2026-10-23 (D-024); E4 and E5 approved 2026-10-23 (D-025); E6 and E7 approved 2026-10-23 (D-026); E8, E9 and E10 approved 2026-10-23 (D-027); E11 and E12 approved 2026-10-23 (D-028).; velocity and length randomness added 2026-10-27 (D-036, a recorded exception to the freeze); erratum E13 approved 2026-10-27 as option C (D-039); Arp's own randomize added 2026-10-29 as §15 with its companion `spec/randomize.json` (D-044, D-048, a recorded exception to the freeze).** This is a protected path: any change needs the human's
> gate and a decision record. Revision history: r1 (2026-10-21) to r2.12 (2026-10-22); four independent reviews
> (`docs/spec-review-r1..r4.md`).
> critic, which found 13 HIGH, 9 MED and 4 LOW issues (verdict: rework). r2
> folds in every finding; the per-finding record is `docs/spec-review-r1.md`.
> r2.1 adds §13, which settles the 17 gaps the lab build surfaced. §13 is
> normative and outranks the section it clarifies. r2.2 adds the output
> stream tag (D-009, horde ADR-270). r2.3 rewrites §6 in horde's scale schema
> (D-010). r2.4 adds four chord + note order styles, the per-step `pick`, and
> the user pattern (§4.6) (D-011). r2.5 added `landed` (D-012); r2.7 defers it past 1.0
> (`docs/deferred/landed.md`) and replaces `octaves` with Ableton-style **steps** (§4.7, D-014).
> r2.8 adds **time mode** (§2.6), the per-step **step offset** (§4.3) and the **pattern library** (§4.6) (D-016).
> r2.12: one `quantize` switch governs the arp AND the chord stage; `chord_quantize` removed; the played note snaps too (D-020).
> r2.11 folds in the fourth independent review (4 HIGH; `docs/spec-review-r4.md`, D-019). Chord notes behave like held notes.
> r2.10 adds **repeats** (§4.8) and the **chord stage** (§14), a second processor with its own `chord` stream (D-018).
> r2.9 folds in the third independent review (2 HIGH, 3 MED, 3 LOW; `docs/spec-review-r3.md`, D-017).
> r2.6 folded in the second independent review
> (5 HIGH, 8 MED, 4 LOW; `docs/spec-review-r2.md`, D-013).
>
> - This file becomes a protected path once the human ratifies it (the
>   ROADMAP Phase A gate).
> - Every rule once marked PROPOSED was settled by the human at ratification
>   (§12, D-021).
> - **Normative companions:** `spec/params.json`, `spec/bank/starter.json` and `spec/randomize.json` (§15).
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
  - `NoteOn{ref, key, vel, stream}`, where `stream` is `arp` or `chord` (§14);
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
  - **Every** `NoteOn` the **arp** emits carries `stream = arp`, whatever produced it
    (steps, ratchets, chord steps).
  - **Every** `NoteOn` the **chord stage** (§14) emits carries `stream = chord`.
    horde's per-source selector routes each stream on its own, so one oscillator
    can take the chord, another the arp (D-018; Q-H7 to horde).
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

**Inside one sample, an input comes first (erratum E13, option C, D-039).**
Arp handles its input events at a sample before its own due offs, pending
ons (ratchet sub-notes, swung and straight ons, triggers) and grid steps at
that sample. So an input counts as **on** any such event of its sample, even
one a fraction of a sample earlier in the music. The reason is that a host
delivers an event written on a grid line at a whole sample. At 44.1 kHz half
the 1/16 lines of 120 bpm fall on half samples, and a host that rounds to the
nearest sample or up delivers a chord change a fraction of a sample after its
step's tick. This order still plays it on that step. The price is stated in
G4 (§11): an input up to two samples after an event can land on either side
of it, depending on the sample rate and the host's rounding.
- This is the order in which Arp *handles* events. The events it *emits* at
  one offset keep the order listed above.
- Among its own events at one sample, Arp handles offs, then pending ons,
  then the grid step (§5.1 E10 excepted).

### 1.2 Identity (`intake/note-vocabulary.md`)
- Input notes are matched by `ref`.
- Output notes get **Arp's own refs**: `(port 0, channel, key, id)`.
  - `channel` is that of the source held key.
  - `id` comes from a per-instance counter that starts at 0 at `prepare` and
    `reset`, which makes ids deterministic. When `prepare` leaves notes still
    owed a NoteOff (T10), the counter starts above the largest owed id instead,
    so a new note never shares a ref with an owed off (E4, D-025).
- A `NoteOff` carries exactly its `NoteOn`'s ref.
- Arp emits base keys only; it never bends.

### 1.3 Capacities (fixed at `prepare`; nothing grows)

| Thing | Capacity | On overflow |
|---|---|---|
| held keys | 32 | a new key is ignored, and so is its later NoteOff (counted) |
| live refs per held key | 4 | an extra ref is ignored, and so is its NoteOff (counted) |
| sounding output notes | 128, plus a small slack (16) for notes released inside their own first sample and held by the 1-sample minimum (§1.1; erratum E3, D-024) | T11; if the slack is full, the new on is refused (counted) |
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
window. **Offsets are never clamped.** An input event, or a block, whose offset or `n` falls outside the
window is **refused and counted** (`badInput`), never an abort: the audio thread must not stop (erratum E1, D-024).

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
- **In time mode** (§2.6) the time clock is idle while H is empty, so trigger-only
  steps do not fire with no keys held.

### 2.6 Time mode: a free rate, from the key press (D-016)
`rate_mode` is `sync` (the default: the grid is musical, §2.1–2.5) or `time`.

**In `time` mode a tick is one microsecond.**
- The step length is `L = step_time_us`, from 10,000 to 2,000,000 µs.
- The display shows it **both ways at once**, as ms per step and as Hz.
  10 ms is 100 Hz; 2 s is 0.5 Hz; the default 125,000 µs is 125 ms, or
  8 Hz.
- Positions use `D = sr` and `inc = 1,000,000` per sample. So every §2.3
  and §5 formula (offsets, swing, gate, ratchets, ties, the 1-sample
  minimum) applies **unchanged**, exactly, in integer arithmetic.

**The time clock fires from the key press, whatever the transport is doing.**
It behaves like the free clock (§2.4):
- it starts at a gesture start with position 0, so step 0 lands on the key
  press;
- it re-bases at every gesture start, shifting pending positions exactly
  (T14), with the epoch incremented;
- it evaluates **no step while H is empty** (§2.5), even while a note still
  rings or a ratchet sub-note is pending: no notes and no triggers. Notes
  already scheduled still sound and end on time (E5, D-025);
- it stops when Arp is idle.

The host's tempo, position and transport edges are **ignored** in time mode
(no T6 from them). Restart has nothing musical to count against:
- `chord` anchors at the gesture start, as always;
- `bar` and `free` both count from the time clock's tick 0, the gesture
  start.

**Changing the step time mid-pattern is smooth (the human, by poll; r3 HIGH-1).**
`step_time_us` is a **continuous** setting, read at each step's evaluation.
When the value read differs from the `L` in force:
- the next unevaluated step `k₁ = lastEvaluated + 1` lands at
  `max(lastTick + L_new, changeTick)`;
  - `lastTick` is the tick of the last evaluated step;
  - `changeTick` is the position where the change is applied (the block
    start);
- every later step is spaced by `L_new`: `tick(k) = originTick + (k − k₁)·L_new`.

This is exact integer arithmetic, a piecewise grid; §2.1's "never
accumulated" is about float drift. In practice:
- the arp accelerates or decelerates from the step it is on, with no gap
  between grid positions (before swing) longer than `max(L_old, L_new)` and
  **no burst** of overdue steps (E7, D-026);
- `m`, the pattern position and swing parity (`kAbs`) carry on;
- sounding notes keep their offs (T18); a previous step's sub-notes not yet
  started at the next step's on are dropped (§5.1, E10).

**Restart is inert in time mode (r3 HIGH-2).** There are no bars in time.
`bar` and `free` behave as `chord`: the pattern anchors at each gesture
start. The UI greys `restart` out in time mode.

**Switching `rate_mode` (r3 MED-4).**
- `rate_mode` applies at **offset 0 of the block whose snapshot changes it**,
  as `enabled` does. It is a clock switch (T6): release-all, then re-anchor.
- **Entering time mode with H non-empty** starts the time clock at that
  offset, as §2.4 H7b does.
- `rate` is ignored in time mode, and `step_time_us` is ignored in sync mode.
  A morph of the inactive one does nothing (no T15).

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
**The sorted list:** `E` = the held keys in pitch order (H, §3); `e = |E|`.
There are no octave copies. Range comes from **transposition steps**
(§4.7), which repeat the whole pattern shifted.

**The press-order list:** `Pp` = the held keys in press order.

**Order position:** `p = mod(m, period)`. With `e = 0`, no note plays (§4.3).

| id | Period | Note(s) at position p |
|---|---|---|
| `up` | e | E[p] |
| `down` | e | E[e−1−p] |
| `bounce` | e = 1: 1; else 2e−2 | p < e ? E[p] : E[2e−2−p] |
| `bounce2` | 2e | p < e ? E[p] : E[2e−1−p] |
| `played` | \|Pp\| | Pp[p] |
| `chord` | 1 | every key of H; collisions after quantize resolve per §6 |
| `random` | — | E[ draw(seed, kAbs, 0, epoch) mod e ] |
| `shuffle` | e | P_c[p], c = fdiv(m, e); P_c = Fisher–Yates of E using draw(seed, c·256 + i, 1, epoch) for i = e−1 … 1; **no back-to-back repeat**: for c > 0, e ≥ 2, if P_c[0] = P_{c−1}[e−1], swap P_c[0] and P_c[1] (deterministic). This is "random without repeats" (D-012a) |
| `converge` | e | E[0], E[e−1], E[1], E[e−2], … (q-th = q even ? E[q/2] : E[e−1−(q−1)/2]) |
| `diverge` | e | converge read backwards: q-th = converge[e−1−q] |
| `pedal_low` | e = 1: 1; else 2(e−1) | p even → E[0]; p odd → E[1 + (p−1)/2] |
| `pedal_high` | e = 1: 1; else 2(e−1) | p even → E[e−1]; p odd → E[e−2 − (p−1)/2] |
| `chord_walk` | e + 1 | p = 0 → every key of H (the chord); p ≥ 1 → E[p−1] |
| `stride` | 2 | p = 0 → H[0] (the bass, lowest held); p = 1 → the **rest** of H, as a chord. With one key held, the rest is that key +12. A step's `step` offset and the transposition (§4.7) apply to every key of the step, the bass included |
| `chord_between` | 2e | p even → every key of H; p odd → E[(p−1)/2] |
| `pairs` | e = 1: 1; else e − 1 | e = 1: {E[0]}; else {E[p], E[p+1]}, a dyad |

**Chord + note styles (D-011).** These four mix chords and single notes. In
each of them, a step that plays several keys behaves as a `chord` step does:
- every key gets its own note;
- collisions resolve after quantize (§6);
- `m` advances by 1 per step, whatever number of keys it plays.

`H` is in **pitch order** (ascending key) wherever §4 uses it, including the `chord` style and the `chord` pick, so `H[0]` is the lowest. A multi-key step's keys are processed in ascending pre-quantize key order (§6).

**Which key is "source"** for channel and velocity: for an octave copy, the
held key it was copied from.

**SETTLED (Q-A3, D-021):** all sixteen styles ship in 1.0 (`landed` is deferred); the starter bank is chosen
by playing.

### 4.3 Step patterns (data)
A pattern is `length` steps (1–64). Each step carries:
- `on`;
- `tie`;
- `vel` (0–200 %);
- `gate` (1–200 %);
- `step` (−8…+8): a pitch offset of `step × the pattern's step size` (D-016). It replaced r2.7's
  `oct`. Each pattern carries `step_size` (−48…+48, default **+12**) and `step_unit`
  (`semitones` or `degrees`), so `step = ±1` at the default is exactly the old octave;
- `prob` (0–100 %);
- `ratchet` (1–4);
- `trig` (0–8);
- `pick` (D-011): **which held note(s)** the step plays:
  - `order` (the default): the next note(s) of the order style, at
    `p = mod(m, period)`;
  - `1`…`8`: the k-th lowest key of E, cyclic: `E[mod(k−1, e)]`;
  - `low` or `high`: E[0] or E[e−1];
  - `chord`: every key of H.

  **Only `order` picks advance `m`.** An explicit pick plays its note without
  consuming the order stream, so the order style resumes where it left off.

Pattern `none` is a single plain step.

**Evaluating step kAbs** happens once, in the block whose window holds its
grid tick, using **that block's settings snapshot** (H6). For step `s = steps[j]`:
1. `fires = (draw(seed, kAbs, 2, epoch) mod 100) < s.prob`.
2. **If `s.tie`**: no new note and no change to `m`. The tie was already
   honoured by look-ahead (§5). Fire the trigger if `fires ∧ s.trig ≠ 0`.
3. **Else if `s.on ∧ fires ∧ e > 0`**: **play**.
   - Take the note(s) at `p` (`pick = order`), or the step's explicit pick.
   - Apply the step offset (`s.step × pattern step_size`) and the pass transposition `t`
     with §4.7's "Applying offsets" rule. That rule includes the drop and the quantize; they are not
     applied a second time.
   - Schedule the ons (§5). If `pick = order`, `m += 1`, even if every key
     was dropped. An explicit pick leaves `m` unchanged.
   - Fire the trigger if `s.trig ≠ 0`.
4. **Else if `fires ∧ s.trig ≠ 0`** (a rest, or `e = 0`): fire the trigger.
   This is the trigger-only case, consistent across patterns (H11).
5. Otherwise the step is a rest.

### 4.4 Velocity
- `base` is the source key's velocity (mode `played`) or `vel_fixed` (mode
  `fixed`).
- `out = clamp(fdiv(base · s.vel + 50, 100), 1, 127)`. That is
  round-half-up in integers.
- **Velocity randomness (D-036).** With `vel_random` > 0, each arp note's
  velocity then moves by up to that share of itself, up or down:

      out' = clamp(out + fdiv(out · vel_random · U + 50000, 100000), 1, 127)

  `U` is the note's random value (§4.9, lane 3). At `vel_random` = 0 nothing
  changes. Chord-stage notes are never randomised: they follow the hands.

### 4.5 Restart: the anchor
| `restart` | Anchor |
|---|---|
| `chord` | the first grid step at or after a **gesture start**; under latch **append**, a press that adds to a non-empty latched set does **not** re-anchor. Also re-anchors at: enable (T5), a jump (T6), a rate change (§9) — each to the first grid step at or after that point |
| `bar` | the first grid step at or after each **bar start**. Bars come from `barStartBeats` when given, else from tick 0 with `timeSigNum/Den` (default 4/4): bar length = `TPQ·4·num/den` ticks. 10 of 22 rates don't divide a bar; the "first step at or after" rule handles them (H9) |
| `free` | the first grid step at or after tick 0 of the active clock; re-anchors only on a jump or a clock switch |

At every anchor `m := 0` (H8).

---

### 4.6 The user pattern: build your own (D-011)
- **Shape.** The pattern id `user` names the player's own step pattern:
  - `length` 1–16;
  - each step has every §4.3 field, `pick` included;
  - it is selected like any bank pattern (`pattern = user`).
- **The default** is 8 plain steps with `pick = order`.
- **Storage.** It is **preset data**: a small structured blob saved with the
  preset. It is not host-automatable parameters, because 16 steps × 9 fields
  is no parameter surface. horde is asked how presets carry it (Q-H6).
- **Storage lives with the instance, not with `prepare`** (r2 H3). The
  pattern's slots are fixed members sized at **construction**. `prepare` and
  `reset` never touch them, because the pattern is preset data, not runtime
  state. Hosts load state before `prepare` and re-`prepare` on a rate change.
- **Setting it** (r2 H4):
  - `setUserPattern(p)` is called **off the audio thread** (UI edit or preset
    load). Calls are serialised on that side.
  - It validates `p`. Numeric fields clamp to their ranges; an invalid `pick`
    or field becomes its default.
  - It writes into a **triple buffer**: one writer, one reader, latest write
    wins, published by an atomic index exchange. So it never blocks and
    never tears, however many calls land between steps.
  - The audio thread reads the "new" flag **once per `process`**. A new
    pattern applies from the first step evaluated in that block, never
    mid-step.
  - `getUserPattern()` returns the writer side's latest copy, which is what a
    preset saves.
  - `process` never allocates or locks for it.
- **Morph.** Choosing `pattern = user` morphs like any discrete setting. The
  user pattern's content does not morph; it is one pattern per preset.
- **Ties and look-ahead (§5.2)** read the user pattern exactly as they read a
  bank pattern.
- **Pattern-level fields:** `step_size` (−48…+48, default +12) and
  `step_unit` (`semitones` or `degrees`) scale every step's `step` (§4.3).
  Bank patterns carry them too (`spec/bank` `pattern_defaults`).
- **The pattern library (D-016)** belongs to the adapter and UI, not the
  core. It holds **named saved copies** of user patterns. Saving stores the
  editor's pattern under a name; recalling one calls `setUserPattern` with
  it. The core only ever holds the one active pattern.
  - **Format rules (r3 LOW-6):**
    - **Unknown fields are ignored.** A `format` other than `arp-user-pattern/1` is
      rejected.
    - **Steps are padded** (with `step_defaults`) **or truncated to `length`**, which
      clamps to 1–16.
    - **Every field is validated** by `setUserPattern`'s rules.
    - **Repo patterns are read-only** and listed with their source. A browser save that
      shares a repo pattern's name is a separate entry, marked "(browser)", and never
      overwrites the repo file.
    - **Bank patterns may override** `pattern_defaults` with their own `step_size`
      and `step_unit`.
  - **Storage format:** `arp-user-pattern/1`, holding `name`, `length`,
    `step_size`, `step_unit` and `steps[]` (omitted step fields take
    `step_defaults`). Files of this format in `lab/patterns/`, listed in
    `lab/patterns/index.json`, are patterns **saved in the repo**.
  - **In the lab:** the library lists the repo's patterns plus any saved in
    the browser. Export and import use the same JSON.
  - **In horde:** the library lives in horde's preset system (Q-H6). Read
    horde ADR-169 first: module presets "embed by value".
- **The `pick` encoding:** the JSON string `"order"`, `"low"`, `"high"` or
  `"chord"`, or an integer 1–8. Anything else is read as `"order"`.

### 4.7 Transposition steps (Ableton-style; D-014)
These replace r2.6's `octaves`. They follow Ableton's arpeggiator
Transposition: the **whole pattern repeats, shifted**, rather than one merged
range.

**Settings:**
- `steps`: 0–8, default 0 (no transposition);
- `step_size`: −48…+48, default **+12**;
- `step_unit`: `semitones` (the default) or `degrees`.

**The pass index.** A **pass** is one full cycle of the order style:
`period` positions, or `e` for `random`, which has no period of its own. The
transposition of a step is

    t = mod(fdiv(m, P), steps + 1)

- `P` is the order style's period, recomputed from the live H at each step,
  as `p` is.
- Pass 0 is untransposed. Pass t shifts by `t · step_size` units. After
  `steps` shifted passes, it returns to pass 0.
- So `up` over C E G with 2 steps of +7 semitones plays
  `C E G | G B D | D F♯ A | C E G …`.
- With `steps = 0` the pattern never transposes, and `t` is always 0.
- An explicit `pick` (§4.3) takes the **current** pass's `t`, so a picked
  bass note shifts with its pass.
- Because `t` derives from `m`, it resets wherever `m` does (every anchor,
  §4.5).

**Applying offsets**, in the play rule (§4.3), to every key of the step.
Two offsets can apply:
- the pass transposition, `t × step_size` in the global `step_unit`;
- the step offset, `s.step × the pattern's step_size` in the pattern's
  `step_unit`.

Each offset is either a **semitone** part or a **degree** part, and the parts
are summed by kind: `S` semitones, `G` degrees. Then, **per key**, in ascending
pre-quantize key order, each key running through all six stages before the
next key starts (r3 MED-3); a tie in stage 3 reads `lastQ` as the previous key left it:
1. `key + S`.
2. **Drop** any key outside 0–127 (T16).
3. **Quantize** (§6), if it is on.
4. **Move `G` in-scale keys** up or down the effective scale, counting only
   keys in the scale.
   - With quantize `off`, or a zero mask, a degree is one semitone (the
     chromatic scale).
   - A degree-moved key is in the scale by construction.
5. **Drop** any key now outside 0–127 (T16).
6. **`lastQ`** is set to the key's final value only if the key **survives
   stage 5**. A dropped key never sets it.

With every part in semitones this is r2.7's rule; with `G = 0`, stages 4–5
change nothing. **One behaviour change from r2.7 (r3 LOW-7, recorded in D-017):** with the GLOBAL unit in
degrees, a semitone step offset now applies before quantize and the degree move, so a key near the top or
bottom can now be dropped where r2.7 kept it, and a tie can resolve differently.

**Morph:** `steps`, `step_size` and `step_unit` are discrete settings (§9),
applied from the next evaluated step.

### 4.8 Repeats (D-018)
`repeats` runs 0–16 (default **0 = forever**). It sets how many times the
pattern plays **per chord**, then the arp stops until the next gesture start.

- **One repeat is one full cycle of the whole pattern.** That is the number
  of grid steps `C` before the step pattern, the order style (through every
  held note) and the transposition passes all come round together:

      C = plays > 0 ? Ls · (P·(steps+1) / gcd(P·(steps+1), plays)) : Ls

  - `Ls` is the step-pattern length.
  - `plays` is its number of `on`, non-tie steps with `pick = order` (prob is
    ignored; this is a static count).
  - `P` is the order style's period from the live H (`random`: e).
  - Example: 2 repeats of `up` over C E G with pattern `none` is
    `C E G C E G`, then silence.
- **The count runs from the gesture start**, whatever `restart` does:
  `repeatSteps` counts grid steps evaluated since the gesture's first step,
  and resets only at a gesture start (and at T6/T7 while H′ is non-empty,
  and T10; E12).
  - `C` is recomputed from the live H at each step. **While H′ is empty, the
    last non-empty `C` is kept**, so the count keeps running after the keys
    are released and an arp let go before its repeats are done still
    exhausts (E9, D-027). Before the first gesture there is no count.
    `C` is also set **the moment H′ becomes non-empty**, so a chord tapped
    and released before the next step has its own cycle length and runs
    out like a held one (E11, D-028).
  - When `repeatSteps ≥ repeats · C`, the arp is **exhausted**. Its steps are
    still **consumed by the once-guard** as silent rests, with no notes and no
    triggers, so no burst of overdue steps follows (r4 H4). Sounding notes end
    by their gates (T20).
  - **`exhausted` is a latched flag** (r4 M1). It clears only at:
    - a gesture start;
    - **any NoteOn that adds a key to H**, under latch append or the pedal
      too (the human, by poll). That press also restarts `repeatSteps` at 0,
      without re-anchoring the pattern unless it is a gesture start;
    - T6, T7 or T10. **T6 and T7 clear it only while H′ is non-empty**: with
      no keys held, a loop, jump or Panic leaves an exhausted arp silent
      until a key-adding press or a gesture start (E12, D-028). Likewise T6
      and T7 reset `repeatSteps` only while H′ is non-empty: with no keys
      held, a round in progress finishes and stops, never restarts (E12).

    Morphs of `repeats`, `steps` or `pattern` never revive an exhausted arp.
  - `C` uses `P` from the arp's held set **H′** (§14), not H (r4 L4).
- Time mode counts the same way, in time steps.

### 4.9 Note randomness: the draw and the curve (D-036)
Each arp note gets its own random value `U` in permille, −1000…1000, one per
lane: lane 3 for velocity, lane 4 for length.
- The note's **draw position** is `x = kAbs · 1024 + sub · 256 + n`, where
  `sub` is the ratchet sub-note (0–3) and `n` is the note's index in the
  step's notes in style order, before any dedupe (0–255). So `x` depends only
  on the grid, never on blocks or the sample rate.
- `r = draw(seed, x, lane, epoch)` (§10), split into its four bytes `b0…b3`
  (low byte first).
- `U₁ = fdiv((2·b0 − 255) · 1000, 255)` is uniform; `U₄ = fdiv((2·(b0+b1+b2+b3)
  − 1020) · 1000, 1020)` is the mean of four, a bell around 0.
- **The curve** `random_curve` (0–100 %) blends them:
  `U = fdiv(U₁ · (100 − random_curve) + U₄ · random_curve, 100)`. At 0 every
  value up to the amount is equally likely; toward 100 most land near the
  setting, with the occasional big one. The range never exceeds ±1000.
- `vel_random`, `gate_random` and `random_curve` are continuous settings
  (§9): they blend under morph and apply from the next evaluated step.

## 5. Note timing

### 5.1 Ons, swing and the pending queue
- The **span** of step kAbs runs from its swung on to the next step's swung on:

      swingDelay(x) = (x odd) ? fdiv(L · (2·swing − 1000), 1000) : 0
      on(kAbs)      = kAbs·L + swingDelay(kAbs)
      span(kAbs)    = on(kAbs+1) − on(kAbs)

  `swing` is in permille, 500–750: 500 is straight, 667 is a triplet feel,
  750 the maximum. Parity comes from `kAbs`, so swing locks to the song grid
  whatever the restart.
- **Swing is latched per pair (E6, D-026).** The even step `2j` reads `swing`
  at its evaluation; the odd step `2j+1` uses that same value if `2j` was the
  last step evaluated, and otherwise reads the current value. So `span(2j)`
  and `on(2j+1)` always use one swing, and the odd step's span ends at an even
  on, which swing never moves: a step's ratchets never sound after the next
  step's on. A swing change therefore applies from the next even step.
- **Spilled ratchets in time mode (E10, D-027).** A `step_time_us` change
  (§2.6) can bring the next step's on before a sub-note of the previous step
  that is still pending. At that step's evaluation, every pending sub-note of
  earlier steps at or after its on is **dropped** (none has a NoteOn yet, so
  none is owed a terminal). Sub-notes already started keep their offs. A
  pending sub-note exactly **at** that on is dropped too: in time mode it
  yields to the step at its own sample, ahead of §1.1's pending-first order,
  so the step's own on never retriggers it.
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
- **Length randomness (D-036).** With `gate_random` > 0, each arp note's
  gate-derived length moves by up to that share of itself, up or down, by its
  `U` from lane 4 (§4.9):

      len' = max(1, len + fdiv(len · gate_random · U, 100000))

  then the ratchet cap applies as above. For a tied note the same factor
  applies to the tie's tail term `max(1, fdiv(span(kAbs+t) · gate · …))`,
  using the `U` of the sub-note the tail extends (the last, `sub = r − 1`). At
  `gate_random` = 0 nothing changes; chord-stage notes are never randomised.
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
- **Same key still sounding (T2).** "Same" means the same **(channel, key) within one stream**. An `arp`
  note and a `chord` note may sound on one key at once; they are told apart by output id (r4 H2).
  If a new on arrives while that key sounds, the old note's `NoteOff` and the
  new note's `NoteOn` go out at one sample, in that order.

---

## 6. Scale quantize: horde's scale schema (D-010)

**The schema.** A scale is `{root, mask}` and nothing else. This is exactly
horde's shape: `scaleRoot` (0–11) and `scaleDeg0..11`, legacy host ids
116–128 (horde B260, B391).
- **The mask is relative to the root.** Degree `i` admits the pitch class
  `i` semitones above `root`, so a key `x` is in the scale iff
  `mask` bit `mod(x − root, 12)` is set. That is horde's `quantise()`.
- **"The mask is the truth, the name is UI."** Arp stores and transmits only
  `{root, mask}`, never a scale name.
  - horde's 18 named scales (`params.json` → `ui_scale_names`) are UI
    vocabulary. Picking one writes the twelve degrees.
  - A mask that matches no name reads "custom".

**Settings:**
- `quantize`, one of:
  - `off`;
  - `scale`: snap to the scale anchored at its root;
  - `chord_root`: horde's movable-do "scale (offset)" mode for an arp. The
    scale **shape** is anchored on the **lowest held key**, so the root
    setting is ignored (horde ADR-112). As in horde, the anchor's own pitch
    class is **always admitted**, even when degree 0 is off (r2 M4).
- `scale_source`, one of:
  - `global`: horde's scale, from the block input. Read, never owned. This
    is the default.
  - `own`: Arp's own `scale_root` and `scale_deg0..11`, in the same shape.
    The default own scale is C major.

All of these are discrete settings and morph targets, so they apply at the
next step (§9). `quantize`, `scale_source`, `scale_root` and `scale_deg0..11`
form one **morph group**, resolved by a single leading-corner decision. A
morph between two scales therefore never passes through an unintended
hybrid, such as C major's root with A minor's degrees (r2 M2).

**The rule:**
- Keys outside 0–127 were already dropped (§4.3). Each kept key moves to
  the nearest in-scale key within 0–127. A key already in the scale is
  unchanged. A zero mask disables quantize.
- **A tie goes toward the previous quantized output** of this instance,
  `lastQ`. This is horde's rule, from Tonality: the output line is the melody
  heard, so continuity is owed to it.
  - With no `lastQ`, or with both candidates equally near it, the tie goes
    to the **lower** key.
  - **When `lastQ` updates** (r2 H1). It updates at **step evaluation**, not
    at emission, so the block partition cannot change it.
    - Each kept key of the step is processed in ascending pre-quantize key
      order, and each sets `lastQ` to its own output key before the next key
      is quantized.
    - A refused or capped on still counts, because `lastQ` follows
      evaluation.
    - With quantize `off`, or a zero mask, `lastQ` is not touched.
  - **When `lastQ` resets, SETTLED (r2 H2; D-021).** It resets to "none" at every
    release-all (T4, T6, T7, T10: switch-off, any transport edge, panic,
    reset, prepare, deactivate). Every play-from-here then sounds the same,
    and a bounce matches what was auditioned. The human confirms this at
    spec ratification.
- **Collisions:** when two keys of one step land on one key, it is emitted
  once, with the channel and velocity of the earliest source in the style's
  order.
- The scale and the mode are read at step evaluation. A scale change never
  moves a sounding note.

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
| T17 | the user pattern is replaced (`setUserPattern`) | none: it applies from the first step evaluated in the next `process`; a note already tied keeps its scheduled off |
| T18 | `step_time_us` changes in time mode | none: the piecewise grid (§2.6) moves future steps only; sounding notes keep their offs |
| T19 | `rate_mode` switches | release-all at offset 0 of that block (T6), then re-anchor |
| T20 | repeats run out | none: no new steps; sounding notes end by their gates |
| T21 | chord-stage notes (§14) | each ends exactly once: when its stored source count reaches 0, at a re-map that drops it, at `chord_enabled` off, at the module's `enabled` off (T4), at panic (T7), or at T10; never at a transport edge (T6) or an `arp_enabled` change |
| T22 | a randomize result arrives (§15) | none of its own: it is an ordinary settings change, adopted as one snapshot; its keys act through the rows they already have (T8, T9, T15, T18, T20, and §14's re-map with T21) |

T3, T8, T9, T12, T14, T15, T16 and T22 are rows on purpose: each must be shown to
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
- **Continuous settings:** `gate` (1–200 %), `swing` (500–750 ‰, latched per step pair, §5.1), `vel_fixed`, `vel_random`, `gate_random`, `random_curve` (§4.9), and `step_time_us`
  (time mode; it changes smoothly per §2.6).
- **Discrete settings:** all the rest. All of them are morph targets
  (D-000 Open 5).
- **Q-A1, SETTLED (D-021).** horde resolves a discrete setting to the value of the
  corner with the largest weight, **with hysteresis**: it switches only when
  the leading corner's weight exceeds the current value's corner weight by
  0.05. Ties go to the lower corner index. Arp applies any change from the
  next evaluated step. A change of `rate` applies at the next step of the
  **old** rate. With a long rate (2/1) that can lag up to 8 beats. That lag
  is accepted and audible in the lab.
- **Q-A2, SETTLED (D-021).** A change of `restart` takes effect from the next
  evaluated step:
  - into `bar` or `free`: the anchor is recomputed per §4.5, and `m := 0`;
  - into `chord`: the anchor is kept until the next gesture start.
- **`enabled`** is the single source for on/off. It applies at offset 0 of
  its block, not at the next step (T4, T5).

---

## 10. Determinism

    draw(seed, x, lane, epoch) = mulberry32( seed ⊕ imul(x, 0x9E3779B1) ⊕ imul(lane, 0x85EBCA77) ⊕ imul(epoch, 0xC2B2AE3D) )

- It takes the first output, in uint32 arithmetic, with `x` taken mod 2³².
- **Lanes:** 0 is `random`, 1 is `shuffle`, 2 is `prob`, 3 is velocity
  randomness and 4 is length randomness (§4.9, D-036).
- No RNG state is carried between steps.
- §15's randomize draw is a separate function with its own salt and constants, keyed by horde's randomize seed: a stream independent of these lanes.

**D1.** These must all match to give an identical output stream:
- the same input events at the same absolute samples;
- the same transport;
- the same settings;
- the same seed;
- the same user pattern, delivered before the same block.

---

## 11. Invariants (each with a planted must-fail control)

| id | Invariant | Planted control |
|---|---|---|
| N1 | exactly one NoteOff per NoteOn, same ref, ≥ 1 sample later, over sessions exercising T1–T16 | drop one release; double one release; emit a 1-tick note's off at its on sample |
| G1 | event absolute samples identical at block sizes 1, 7, 64, 512, 4096 | clamp offsets; accumulate the position in float |
| G2 | ons land at `cdiv` of their exact position | round instead of ceil |
| G3 | swing parity follows `kAbs`, not `k` | use `k` under `restart = chord` |
| G4 | the **tick** of every event is identical at 44.1, 48, 96 and 192 kHz (samples differ by rate), for events whose causes lie **more than two samples apart** at every rate (enough for a host that rounds an event's time down, to the nearest sample or up). A cause is an input event, or a settings, transport or scale change, which takes effect at its block's first sample. An input within that distance after an off, pending on or grid step can count as on it (§1.1, E13), so near such a tie the order can depend on the rate | read a sample-rate-dependent value into tick math |
| G5 | under re-base jitter (the host position perturbed by ±1/32 beat), every grid step is evaluated exactly once | drop the once-guard |
| D1 | same seed → same stream; a different seed changes `random` / `shuffle` / `prob` output | put a time-varying value in the draw |
| Q1 | every quantized key is in the **effective** scale (source, mode, anchor) and in 0–127 | skip quantize on chord steps |
| Q2 | a tie resolves toward the previous quantized output (lower when there is none) | always resolve downward |
| Q3 | with `scale_source = own`, keys follow Arp's own scale, not the global one | ignore the own scale |
| Q4 | `chord_root` anchors the scale shape on the lowest held key | anchor on the root setting |
| R1 | rest, miss, tie, trigger-only and explicit-`pick` steps don't advance `m` | advance `m` on rests; advance `m` on explicit picks |
| N2 | for a fixed seed, `shuffle` never plays the same key twice in a row (e ≥ 2), and every key once per cycle (a seed change, by morph or a randomize re-roll, may repeat or skip one key once in its cycle: D-048 answer 12) | skip the boundary swap |
| Q5 | with ties toward a previous note above the tie, ties resolve UP; G1 holds with quantize ties in play; `lastQ` resets at release-all | always down; update `lastQ` at emission; never reset `lastQ` |
| TM1 | time mode: step k sounds at `cdiv((k·step_time_us + swingDelay(k))·sr, 10⁶)` samples after the gesture's key press, whatever the host tempo, position or transport does; identical at every block size and sample rate (in µs) | use the host tempo; anchor at the transport |
| TM2 | a `step_time_us` change mid-gesture: the next step lands at `max(lastTick + L_new, changeTick)`, then every `L_new`; no gap between grid positions (before swing) longer than `max(L_old, L_new)`, no burst | re-grid on absolute multiples of `L_new`, or `kAbs·L_new` |
| TM3 | in time mode `restart` is inert (`bar` = `free` = `chord`) | count bars of 61,440 µs |
| RP1 | `repeats = N`: `repeats·C` evaluated steps from the gesture start (or the last key-adding press), then exhausted (spent steps consume the once-guard, no burst) until a gesture start or a key-adding press; then nothing until the next gesture; 0 never stops | count step-pattern passes instead of full cycles; never stop |
| CH1 | chord stage: each held key emits itself plus its enabled shifts (semitones, degrees, or quantized) as `stream = chord`, deduplicated by key with a source count (E8); one NoteOff per chord note when its last source leaves; the arp plays the chord stage's keys when the stage is on; the stage works with the arp off | no dedupe (two ons for a shared key); end on the first source's release; feed the arp the raw keys |
| ST1 | the step offset is `step × pattern step_size` in the pattern's unit; at +12 semitones `step ±1` equals the old octave; degrees count in-scale keys | treat every step size as 12; read the GLOBAL step_size/unit for the step offset (tested with values that differ from the pattern's) |
| X1 | transposition steps: pass t of every style is the base pass shifted by t·step_size (semitones), or moved t·step_size in-scale keys (degrees); `steps = 0` never shifts; the pass returns to 0 after `steps` | merge copies into one range (r2.6 octaves); count degrees as semitones while a scale is on |
| U1 | the user pattern plays its steps' picks (k-th, low, high, chord) and rhythm exactly; a `setUserPattern` takes effect at the next step | ignore `pick`; swap mid-step |
| P1 | `prob` 0 never fires and 100 always fires (exact); 30 and 50 are within ±2 % over 10,000 steps | use `<=` (caught at the exact endpoints, since a ±2 % band cannot see it) |
| T1 | triggers fire once per firing step, at its first on | fire on every ratchet |
| S1 | every emitted NoteOn carries `stream = arp` | drop the tag on some ons |
| K1 | ties engage at gate 80 (`tied_legato` ≠ `none`) | evaluate ties at the tie step instead of by look-ahead |
| C1 | at the event cap, no NoteOn is emitted without its NoteOff | drop the off-reserve |
| A1 | `process` never allocates (C++ only: an allocation hook) | allocate in `process` |
| RN1 | note randomness (§4.9): at `vel_random` = `gate_random` = 0 the output is byte-identical to the core without it; otherwise each arp note's change stays within ±amount and equals the formula with `U` from `draw(seed, x, lane, epoch)`; identical at every block size and sample rate; at curve 0 the values are uniform, at 100 clustered (variance about a quarter); chord-stage notes untouched | take `x` from the block instead of the grid; ignore the curve; randomise chord-stage notes |
| RZ1 | randomize (§15) is deterministic: a pinned digest of 10,000 calls, identical on every platform | carry one mulberry32 stream across keys; mix an address into the draw |
| RZ2 | randomize draws are position-free: locking one key never changes another key's result | the carried stream |
| RZ3 | every randomize result is valid (`params.json` ranges) when its input is; no pick meets an empty candidate set or a zero total weight, at every chaos with locks swept | a chord shift of +25; the span filter on a `steps` above 3; a bell centre left out |
| RZ4 | every locked key equals `cur` | honour locks in Values only |
| RZ5 | the never-draw keys equal `cur` on every verb, unlocked | draw `latch` |
| RZ6 | chaos 0 with `home = cur` returns `cur`; with a separate home, every key is `cur`'s or home's | must change at chaos 0; a forced redraw at chaos 0 |
| RZ7 | amount nesting, per verb, must change off: keys changed at amount a change at b > a to the same values | liveness read from the keys just drawn; an amount-dependent pick |
| RZ8 | Structure changes only structure keys, Values only value keys (a literal class list); Both = Values after Structure | swing drawn by Structure |
| RZ9 | a key not live on `cur` is never drawn | step time drawn in sync; the arp's keys drawn with the arp off |
| RZ10 | distribution: the mean move rises with chaos; flat at 1000 (KS < 0.05); a drawn rate within one rung ≥ 90 % at 350; the root moves both ways round the circle of fifths | ignore chaos; read the σ table backwards; a linear root distance |
| RZ11 | must change: when due, a key changes unless nothing is movable (reported) | must change off; an ordered move by bell; each group's first move only |
| RZ12 | musical safety: the span bound for drawn passes, repeats never down and forever kept, rate in its family and band, unlit patterns and degrees-with-quantize-off never drawn into, every value in band ∪ {home} | drop the span filter; repeats below `cur`; light `trigger_pulse` |
| RZ13 | randomize allocates nothing | allocate a scratch vector |
| RZ14 | 30 randomized snapshots (Both, chaos 1000) in a core session keep N1, CH1 and G1, and lose no NoteOff | the N1 plant still fires under the fuzz |
| RZ15 | the generated `randomize_data.h` and the compiled classes and exemptions equal `spec/randomize.json`; its tables regenerate from their laws; key ids collision-free | perturb a taste weight, a band, a σ entry, a chord interval, a key's class |
| RZ16 | with the arp off and the chord stage on, no click turns the stage off (chaos 0 and a home with the stage off included) | the keep test returns home's off |

---

## 12. Settled at ratification (D-021, 2026-10-23)

The listening and technical questions are all settled, by the human's poll:
- **Q-A1** (the discrete morph rule with 0.05 hysteresis, §9);
- **Q-A2** (restart under morph, §9);
- **Q-A3** (the bank: all 16 order styles and all 11 step patterns ship;
  factory presets are curated at M3);
- **Q-A5** (pre-roll plays; the grid extends to negative ticks);
- **Q-A7** (a pick takes the next order note's pass, §13b.7);
- **Q-A8** (a chord-shape change re-voices only the changed notes, at once,
  §14);
- **r2 H2** (`lastQ` resets at every release-all, §6);
- **D-008** (the three-band host-position policy, §2.2).

**Still with horde, not blocking the spec:**
- Q-H5 (a pedal that sustains direct voices only);
- Q-H6 (how presets carry the user pattern and its library);
- Q-H7 (routing the `chord` stream);
- after 1.0, Q-H4 (per-channel and MPE pedals, and mapping per-note
  expressions).

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

**A listening note (Layer-E, not a gate; r2 L3).** Under the D-010 tie rule,
`up` over C♯ D♯ F♯ G♯ A♯ in C major plays 60 62 65 67 69, then **62 62** 65
67 69: whole-tone black-key pairs can collapse. Listen to this in the lab
before ratifying.

### 13b. Clarifications from the r2.6 lab build (normative)
1. **`chord_root`'s always-admitted anchor** applies only when the mask is
   non-zero. A zero mask still disables quantize.
2. **The lab's "clear"** restores the default user pattern (8 plain steps).
3. **There is no plant for "`lastQ` updated at emission".** In the lab,
   evaluation and emission interleave in time order at every block size, so
   G1 cannot diverge on it. The rule is enforced by construction (`_quant`
   runs inside step evaluation). The C++ M1 suite revisits this.
4. **`lastQ` under offsets:** superseded by §4.7 "Applying offsets" stage 6 (r2.9). `lastQ` is the
   final key, set only when the key survives the last drop.
5. **A degree move that runs off 0–127** drops the key (counted, T16).
6. **Pass 0 in degrees mode** takes the ordinary path. Quantizing first
   changes nothing there.
7. **Which pass an explicit pick takes, SETTLED (Q-A7, D-021).** A pick reads `t` from `m` at its own step: the pass of the
   **next** order note. A pick that *leads* a pass shifts with it, while a
   pick right after a pass's last order note already belongs to the next
   pass. The smoke pins it: `60 60 64 60 67 | 72 72 72 76 72 79 | 60 60 60`.
   The alternative, the pass of the *previous* order note, gets the opposite
   case wrong.
8. **Time mode `changeTick`** is the block-start position, rounded up to a
   whole microsecond.
9. **A tie's look-ahead across a step-time change** extends at the `L` in
   force when the tied note is scheduled. That note's off is fixed then and
   does not move.
10. **A step-time change before any step of the gesture is evaluated** just
    sets `L`, so step 0 still lands on the press.
11. **Stage 6 of "Applying offsets"** (a dropped key never sets `lastQ`) has
    no observable case in the lab smoke. Keys run in ascending order, so a
    dropped lower key cannot pull a later tie. It is implemented and
    asserted by construction; the C++ M1 suite revisits it.

---

## 14. The chord stage (D-018, r2.11)

A **second note processor**, placed **before** the arp, in the same library.
Each played note becomes the note plus up to six intervals, on its own
`chord` stream, so horde can send the chord to some oscillators and the arp
to others.

**Chord notes behave like the player's held notes (the human, by poll, r4).**
They follow only the keys, the pedal and the latch. They **ignore** transport
edges (no T6), the arp's on/off, and the arp's notes.

**Settings:**
- `chord_enabled`;
- `chord_unit` (`semitones` or `degrees`);
- six slots, `chord_on1..6` and `chord_shift1..6` (−24…+24).

**The module and the arp switch separately.**
- `enabled` is the **whole module**. Off means horde routes input around
  Arp, and every chord and arp note ends (T4).
- **`arp_enabled`** (new, r4 H3) is the arp alone. With `arp_enabled` off
  and `chord_enabled` on, Arp is a **chord generator**. Toggling the arp
  never touches chord notes.

**These settings apply at offset 0** of the block whose snapshot changes
them, like `enabled`, not at the next arp step (r4 M4): the chord_*
settings, `arp_enabled`, and (for the chord stage) the scale settings.

**The mapping is stored, not recomputed (r4 H1).** When a held key `h`
enters H (§3, any of DOWN, PEDAL or LATCHED), its chord keys are computed
**once** and **stored per (h, slot)**:
- **One quantize switch (D-020, the human).** The arp's `quantize` setting
  governs the chord stage too. While it is not `off`, **every** chord key is
  snapped into the effective scale, **the played note itself included**
  (§6: source, root and mask, the `chord_root` anchor on the played keys H).
  So nothing Arp emits is out of scale.
  - Chord-stage ties **always go to the lower key**.
  - The stage **never reads or writes `lastQ`**, which is the arp's.
- **The `+0` slot** is `h`, snapped while quantize is on, otherwise as
  played.
- **In semitones:** `h + shift`, then snapped while quantize is on.
- **In degrees:** start from `h`'s nearest in-scale key (ties lower) and
  move `shift` in-scale keys. With a zero mask, a degree is one semitone.
- Keys outside 0–127 are dropped (T16).

**When `h` leaves H**, its **stored** keys are decremented. Nothing is
recomputed at release, so a scale or anchor change in between can never
strand a note. Stored mappings are re-mapped only at these points, all at
offset 0 of the block:
- a chord_* setting change;
- a scale-setting change;
- `chord_enabled` turning on, which maps every held key.

At each of these, keys that leave the set end, keys that join start, and
unchanged keys keep sounding.

**Dedupe by key** (r4 M2). The chord set is keyed by **key** alone. Each
chord key keeps a **source count**. Its channel and velocity are fixed from
its earliest source **at its NoteOn**. A source count that goes 1→0→1
within one sample keeps the existing note, with no off/on pair (r4 M3).

**Output** (`stream = chord`, own refs):
- A chord key **starts** at the sample its count goes 0→1, and **ends**
  (exactly one NoteOff) at the sample its count reaches 0.
- It also ends at:
  - `chord_enabled` off;
  - the module's `enabled` off (T4);
  - panic (T7);
  - `reset`, `prepare` or deactivate (T10).
- **Not** at transport edges (T6), and not at `arp_enabled` changes (T21).
- At one offset, chord-stream ons sort **before** arp ons (§1.1, r4 L2).
- A chord note refused at the event cap stays silent and is counted. It has
  no sounding entry, so no off is owed.

**Feeding the arp.** With `chord_enabled` on, the arp's held set **H′** is
the chord set, built as follows:
- each key's velocity and channel are the chord note's;
- `Pp` press order is by first source press, then slot order;
- `chord_root` for the arp's own quantize anchors on the played keys H.

With the stage off, H′ = H.

**Capacity** (r4 M6). The chord stage has **its own** sounding table: 224
entries, preallocated (32 held keys × 7). So arp-note eviction (T11) never
touches chord notes.

**Invariant CH1** (§11), extended: stored mappings survive an anchor or
scale change without stranding a note; both streams can sound on one key;
the arp toggle leaves chord notes alone; a transport edge leaves chord notes
alone; and N1 covers every chord note.
12. **`latch` applies at offset 0 of its block**, like `enabled`, not at the
    next arp step. It changes the held set H, and the chord stage follows H
    with no arp steps at all (chord-generator mode). The lab build had applied
    it at the next arp step; with the arp off, a latched chord then droned
    forever after latch was turned off.
13. **The chord stage's effective scale** is the arp's scale source, root
    and mask, with the `chord_root` anchor on the played keys H. The stage
    snaps keys only while `quantize` is not `off` (D-020; `chord_quantize`
    is removed). In degrees with quantize `off`, or with a zero mask, a
    degree is one semitone (as §4.7).
14. **`arp_enabled` off** ends the arp's own notes (the arp's half of T4)
    and clears its pending queue. Chord notes are untouched.
15. **A chord re-map is triggered** by a changed signature: the chord_*
    settings, the scale settings, the arp's `quantize` mode, and the global
    root and mask when the global scale is the source.
16. **With the module off**, the chord stage forgets its held keys.
    Turning the module on with keys held maps them afresh.
17. **A chord note refused at the event cap** stays silent and counted, and
    is not retried while its count stays above 0.
18. **Spent (exhausted) steps** leave no step record for the UI's step
    highlight.

---

## 15. Randomize: Arp's own, for horde's Random panel (D-044, D-048)

Added 2026-10-29 as a recorded exception to the freeze (D-044); the human's answers D-048. The design, its reasoning and its three
reviews: `docs/design/randomize.md`. **Normative data:** `spec/randomize.json` (classes, groups, exemptions, bands, taste weights,
interval tables, rate families, and the σ, `exp` and step-time tables as integers). **Reference implementation:**
`core/include/arp/randomize.h`, `core/src/randomize.cpp`. Where this section and the data disagree, fixing them together is a spec change.

### 15.1 What it is
- **One pure function** `randomize(cur, home, request) → result`. It produces new settings and nothing else. Integers only: no
  float, no libm, no allocation, no state, bounded work. It never runs in `process`: horde calls it off the audio thread.
- **The result is an ordinary settings change** (T22). It reaches the core as a snapshot, like a preset or a morph corner, so
  §9's timing and §7's terminals apply unchanged.
- **Scope:** settings only. The user pattern's steps (§4.6) are never drawn.
- **Verbs:** `Structure` (horde's ARCH), `Values` (PARAMS), `Both` (Structure, then Values run on its result at the same click).
- **Inputs:** `cur` (keys not drawn keep it; it decides liveness and locks); `home` (the bell's centre; clamped into range before
  use); `seed`, `click`, `corner` (the draw key); `chaos` and `amount` in permille, 0–1000 (larger values read as 1000); `locks`
  (one bit per key); `mustChange`. **Result:** the settings, a changed mask, `nothingToDraw`, and `emptyPicks` (always 0).

### 15.2 The draw
    rdraw(seed, click, corner, keyId, idx) = mulberry32( seed ⊕ salt ⊕ imul(click, k_click) ⊕ imul(corner, k_corner) ⊕ imul(keyId, k_key) ⊕ imul(idx + 1, k_idx) )
- The constants are `randomize.json` `draw`. One output, uint32 arithmetic, no state carried.
- `keyId` is FNV-1a 32 of a key's `params.json` name, of a group's name (`scale`, `transposition`, `chord`), or of `scale_mask`.
- `idx`: 0 the amount threshold, 1 the keep test, 2 the pick, 3 must change.
- `below(r, pm)` is `r < ⌊pm · 2³² / 1000⌋`. A weighted pick takes `t = ⌊r · Σw / 2³²⌋` and returns the first candidate whose
  running sum exceeds `t`.

### 15.3 Which units a click draws
A **unit** is a key or a group (§15.6). A unit is drawn when its class matches the verb, it has an unlocked member, it is **live
on `cur`** (`randomize.json` `live`, and §15.6), and `below(rdraw(unit, 0), amount)`. A key not drawn keeps `cur`'s value.
Liveness and joint rules read `cur` and the unit's own draws, never another unit's draw from the same click. So, for one verb with
must change off, the keys changed at amount a are a subset of those changed at any b > a, with the same values (RZ7). Both reads
liveness from Structure's result, so `vel_fixed` and `seed` may not nest under Both (D-048 answer 11). "The arp is live" means
`cur.enabled` and `cur.arp_enabled`.

### 15.4 Unordered choices
At chaos 0, or when `¬below(rdraw(key, 1), chaos)`, the result is home, unless home is not admitted (§15.5), then `cur`.
Otherwise a taste-weighted pick among the lit values (taste > 0) with `rdraw(key, 2)`. An unlit home is kept by the keep test.

### 15.5 Ordered choices and numbers: the discrete bell
- Every number is drawn over a **ladder**: every integer of the legal range; for `step_time_us` the table
  `step_time_ladder_us`; for `rate` home's family (`rate_families`); for `scale_root` the circle of fifths, circular.
- **Candidates:** the band (`randomize.json`), widened to include the **centre**. The centre is home, except where home is not
  admitted: `repeats` below `cur` (§15.7), `steps` under a locked `step_size` (§15.6), `chord_enabled` with the arp off (§15.6),
  and a step size or chord shift whose unit is pinned to `cur`'s, which differs from home's. There it is `cur`'s value. The centre
  is always a candidate.
- **Width:** `s8 = ⌊σ[chaos] · n / 2⁸⌋`, `s2 = s8²`, with `σ` the table `sigma_q16` and `n` the full ladder's length. **Weight**
  of a candidate at distance `d` from the centre: `2¹⁶` at `d = 0`; otherwise `exp_neg_q16[x8]` for `x8 = ⌊d² · 2²⁴ / (2 · s2)⌋`
  when `s2 > 0` and `x8 < 4096`, else 0. The pick is §15.2's with `rdraw(key, 2)`. At chaos 0 the result is the centre.

### 15.6 Groups
Each group has one amount threshold and a fixed internal order. Its members' joint rules read only the group's proposals and
`cur` for locked members. **Chaos 0 draws nothing:** members return to their centre, and no band applies (the span bound included).
- **Transposition** (`steps`, `step_unit`, `step_size`; live when the arp is). The span bound is 24 semitones or 14 degrees.
  1. `steps`: the bell over 0–3 ∪ {home}; with `step_size` locked only values whose span with `cur.step_size` is within the bound,
     plus `cur.steps`.
  2. Only if `steps > 0`: `step_unit`, unordered, kept at `cur` if it or `step_size` is locked; `degrees` lit only while
     `cur.quantize ≠ off`.
  3. Only if `steps > 0` and unlocked: `step_size` from the unit's table (`intervals.transposition`), only intervals within the bound
     for `steps` ≤ 3 (a `steps` above 3 is home's or `cur`'s own shape: no filter). Forced (no keep test) when the unit flipped from
     home's or home's size breaks the bound; otherwise unordered. At chaos 0: home's size, or `cur`'s when the unit differs from home's.
- **Chord** (`chord_enabled`, `chord_on1..6`, `chord_unit`, `chord_shift1..6`; live when `cur.enabled`).
  0. `chord_enabled`, unordered. **With the arp off it is never turned off:** if `cur` has it on, it stays on; otherwise only
     on is lit. If the result is off, no other member changes.
  1. `chord_on1..6`, unordered bools.
  2. `chord_unit`, unordered, kept at `cur` if it or any `chord_shift` is locked; `degrees` lit only while `cur.quantize ≠ off`.
  3. Each slot on after step 1, unless locked: its shift from the unit's table (`intervals.chord`), forced when the slot is off in
     `cur` or the unit flipped from home's; otherwise unordered. At chaos 0: home's shift, or `cur`'s when the unit differs from home's.
- **Scale** (`quantize`, `scale_root`, `scale_deg0..11`; all exempt by default; live when `cur.enabled` and the arp is live or
  `cur.chord_enabled` is on).
  1. `quantize`, unordered.
  2. Only if `cur.scale_source = own` and `quantize` is not off after step 1, and none of the thirteen own-scale keys is locked:
     the mask is a named scale (`params.json` `ui_scale_names`, taste `named_scale_taste`, key `scale_mask`; a home mask that
     matches no name is kept by the keep test), and the root is the circular bell on the circle of fifths.

### 15.7 Keys, locks and exemptions
- **Classes** are `randomize.json` `keys[].class`: 37 structure, 8 value, 5 never drawn (`enabled`, `arp_enabled`, `rate_mode`,
  `latch`, `scale_source`). A never-draw key equals `cur` on every verb, whatever the locks say.
- **Exempt by default** (`keys[].exempt`, 15 keys: `rate`, `quantize`, `scale_root`, `scale_deg0..11`): the locks horde starts
  from. horde owns the lock state.
- **Repeats:** live only when `cur.repeats ≠ 0`. The admitted set is `cur.repeats`, every count above it up to 8, and home if home
  is 0 or above `cur.repeats`.
- **Seed:** live when the arp is and something reads it (`order` random or shuffle, `vel_random` or `gate_random` above 0, or a
  pattern in `seed_reading_patterns`). A re-roll: home with probability 1 − chaos, else `rdraw(seed, 2)` itself.

### 15.8 Order and must change
- **Structure** runs `order`, `pattern`, `rate`, `restart`, `repeats`, `vel_mode`, the scale group, the transposition group, the
  chord group. **Values** runs `gate`, `swing`, `step_time_us`, `vel_fixed`, `vel_random`, `gate_random`, `random_curve`, `seed`.
- **Must change** (Structure only, `mustChange`, chaos > 0): if no structure key changed, the movable unit (live, unlocked, with a
  move that changes a key) with the lowest `rdraw(unit, 0)` moves, with `rdraw(…, 3)`:
  - an unordered key: by taste among its lit values other than `cur`'s;
  - `rate` or `repeats`: the nearest admitted rung above or below `cur` in `cur`'s family, the side from the draw's top bit;
  - transposition: `steps` as an ordered key (the span rule then re-applied to the size); else a different size within the bound;
    else the unit flipped, with a forced size;
  - chord: the first on, unlocked shift redrawn; else the first off, unlocked slot turned on; else the last on slot turned off;
    else the unit flipped with every on shift forced; else `chord_enabled` flipped alone (never off with the arp off);
  - scale: `quantize`; else a different named scale; else the root one fifth either way.
  - If no unit is movable, the result is unchanged and `nothingToDraw` is true.

### 15.9 Chaos 0
With `home = cur`, chaos 0 returns `cur` exactly, for every verb, amount, lock set and must-change setting. With a separate home,
every drawn member returns to its centre (§15.5) and every other key keeps `cur` (RZ6).

