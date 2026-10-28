# The arpeggiator

<!-- Provenance: written 2026-10-28 by the Arp lead session's implementer, ROADMAP T-007 (M3), horde's note-processor 1.0 bar row "A manual page (SHOULD)". This is the content of horde's manual section "The arpeggiator" (chapter 17, MIDI, MPE, the arpeggiator and automation). Each spec citation sits in an HTML comment after the paragraph it supports; the prose never sends the reader to the spec. When horde copies this page in, every heading moves down one level (this H1 becomes the section's H2). The settings table at the end is generated: python3 tools/gen_manual_params.py --write. -->

Purpose: explain Arp, horde's note processor: what it does to the notes you play, every control it has, and how the controls combine, in the words of the panel. Arp is the simple arpeggiator of horde 1.0 together with a pattern bank, patterns you build yourself, a chord stage and seeded randomness.

<!-- The tool is tools/gen_manual_params.py --check. The lead adds the review's date to the line below. -->
Last verified: the settings table, the figure and audio placeholders, the status lines and the wording rules (no spec citations or internal names in the prose) are checked by a tool; the accuracy of the prose is reviewed by hand. Panel paths are marked *(page pending)* until the interface page for Arp exists.

## What Arp does

Status: READY-TO-WRITE — SPEC §1, §14, D-009, D-010

Arp sits on the instrument's note input, ahead of the engine. You play and hold notes; Arp turns them into a stream of notes in time with your DAW; horde's engine sounds that stream. Around that core it offers a bank of rhythm patterns, patterns you build yourself, transposition steps, scale quantize, a chord stage, triggers for horde's other modules, latch, restart rules, a free time mode, and velocity and length randomness that always repeats the same way for the same seed.

<!-- SPEC §1 (a pure note processor between the keys and the engine); README "What it will do (1.0)" -->

Four things hold whatever you set:

- **The beat comes from the DAW.** Steps fall on the DAW's grid, so the arp stays in time with the song. Buffer size makes no difference to what you hear, and neither does sample rate, with one exception: a note you play within a fraction of a millisecond of a step can land just before or just after that step, depending on the sample rate and how the DAW rounds the note's time. A bounce matches what you heard live, with the same exception. (Time mode runs on a clock of its own, not the DAW's.)
- **Every note Arp starts ends exactly once.** Letting go of a key, changing the pattern, stopping the transport, switching Arp off or sending a panic never leaves a note hanging.
- **Nothing is left to chance.** Random order, random choices and randomised velocity and length all come from the **Seed**, so the same playing with the same settings gives the same notes every time.
- **Arp plays notes at their plain pitch.** It never bends them. Pitch bend you play passes through to the engine untouched.

<!-- SPEC §1.2 (base keys only, never bends), §7 N1, §10 D1, §2.3 G1; the sample-rate exception is G4 (§11) and §1.1 "Inside one sample, an input comes first" (E13, D-039): G4 covers causes more than two samples apart. -->

Each oscillator in horde chooses its note source with three toggles *(name pending)*: the notes you play, the arp, and the chord stage. Switch on the played notes and the arp together on one oscillator to hear your keys alongside the arp, or give the arp to one oscillator and the chord to another. With **Chord** on, the chord stage's output contains the key you play as well as the notes added to it, so an oscillator set to the chord stage alone plays the whole chord. Apart from that, Arp never plays your keys back; horde passes them on before Arp.

<!-- SPEC §1 Streams (D-009, horde ADR-270): arp and chord streams; the player's held notes reach the engine through horde's own tee, never through Arp. Q-H7 answered by horde (seq 6, their ADR-272): three toggles per source, ROADMAP "Open questions"; D-029: with the chord stage on, the played note goes out as `chord` too (SPEC §14; the played note is always in the chord set and is not one of the six intervals), so a source on Chord alone plays the whole chord; D-034: the tee is confirmed. The toggles' panel names are horde's and not yet given. -->

## Quick start

Status: READY-TO-WRITE — SPEC §3, §4.2, §7 T4/T5, §13.17

[[FIG:arp.panel]]

1. Switch **Arp › Arp module** *(page pending)* on. It starts off. **Arp on** is already on.
2. Hold a chord. Arp plays the notes you hold from the lowest to the highest and round again, in sixteenth notes (**Rate** 1/16). With the DAW playing, it starts on the next step of the DAW's grid; with the DAW stopped, the first step lands on your key press.
3. Let go. The arp stops; notes already sounding finish their length.
4. Try **Order** to change the way through the notes, **Pattern** to change the rhythm, and **Latch** set to `Replace` to keep the chord going after you let go.

<!-- SPEC §4.2 `up`, §2.1 default rate, §4.5 restart on a new chord, §2.4 H7a (a stopped DAW: step 0 on the press), §3 held set, §7 T3; params.json defaults; §3 "While disabled" (keys are tracked while off, so switching on with a chord held plays it). -->

[[AUD:arp.quick-start]]

Arp keeps track of your keys even while it is off, so switching it on with a chord already held plays that chord straight away.

<!-- SPEC §3 "While disabled", H13 -->

## Switching on, and the arp versus the chord stage

Status: READY-TO-WRITE — SPEC §14, §7 T4/T21, D-018, D-019

Arp has three switches, from the whole down to the part:

- **Arp module** is the whole of Arp. Off, horde sends your keys around Arp, and every note Arp has started (arp and chord) ends at once. It takes effect at the instant you change it, not at the next step.
- **Arp on** is the arpeggiator alone. Off, the arp's notes end at once and no more are played; the chord stage is not touched.
- **Chord** is the chord stage (see below). With **Chord** on and **Arp on** off, Arp is a plain chord generator: each key you press sounds as a chord, for as long as the key is held (by your finger, the pedal or the latch).

With **Chord** and **Arp on** both on, the arp plays the notes of the chords the stage builds. With both off, Arp plays nothing.

<!-- SPEC §14 "The module and the arp switch separately", T4, T21, §13 item 14 (arp_enabled off ends only the arp's notes), §14 "Feeding the arp". -->

### What passes through

Status: READY-TO-WRITE — SPEC §1, §3, §7 T7

Pitch bend, channel pressure, the mod wheel and other controller messages pass through Arp unchanged. The sustain pedal does not: Arp takes it for itself (see Latch and the sustain pedal). Per-note expression from an MPE controller is not mapped onto the notes Arp plays in 1.0; it reaches no arp note.

A panic (all notes off) is obeyed at once: Arp ends its notes, forgets the keys you were holding, the latch and the pedal, and passes the panic on so the engine cuts too.

<!-- SPEC §1 "Pass-through" (per-note expressions addressed to input notes reach no Arp note; mapping them is after 1.0, Q-H4), §3 "Pedal down" (CC 64 is consumed), §7 T7. -->

## Order

Status: READY-TO-WRITE — SPEC §4.2, D-011, D-012a, D-021

**Order** decides which of the notes you hold plays next. The notes are taken in pitch order, lowest to highest, except where the style says otherwise. Order never adds octave copies; to cover more range use **Steps** (below). You can hold up to 32 keys.

<!-- SPEC §4.2 (no octave copies; range comes from transposition steps), §1.3 (32 held keys). -->

- `Up` and `Down`: lowest to highest, or highest to lowest, and round again.
- `Up-down`: up, then down, each end note played once per turn. `Up-down (ends twice)`: the same, but the top and bottom notes are played twice at each turn.
- `As played`: the notes in the order you pressed them.
- `Chord`: all the held notes together on every step.
- `Random`: each step picks one of the held notes at random, so the same note can come twice in a row.
- `Shuffle (no repeats)`: every held note once, in a random order, then a new random order. The same note is never played twice in a row.
- `Converge`: the lowest, then the highest, then the second lowest, the second highest, working in towards the middle. `Diverge`: the same read backwards, starting near the middle and finishing on the lowest note.
- `Pedal low`: the lowest note between every other note, going up (low, second, low, third, ...). `Pedal high`: the highest note between every other note, going down.
- `Chord then walk`: the whole chord, then each note one by one from the lowest, then the chord again.
- `Stride`: the lowest note on its own, then all the other notes together as a chord. With only one key held, it alternates that key with the same key an octave higher.
- `Chord between notes`: the chord, a single note, the chord, the next note, and so on up.
- `Pairs`: two neighbouring notes at a time, sliding up one note each step. With one key held, that note alone.

The four styles that mix chords with single notes (`Chord then walk`, `Stride`, `Chord between notes`, `Pairs`) treat a step that plays several notes as a `Chord` step does: every key plays as its own note, and keys that land on the same note after quantize play once.

<!-- SPEC §4.2 table: up, down, bounce (Up-down), bounce2 (ends twice), played, chord, random (E[draw mod e]), shuffle (no back-to-back repeat, e >= 2), converge, diverge (converge read backwards), pedal_low, pedal_high, chord_walk, stride (one key: the rest is that key +12), chord_between, pairs (one key: that key); "Chord + note styles"; §6 collisions. -->

[[TAB:arp.order-styles]]

[[AUD:arp.order-tour]]

## Patterns

Status: READY-TO-WRITE — SPEC §4.3, §4.6, D-004, D-011, D-016, D-021

**Order** says which note comes next. **Pattern** says when, how long, how loud and how often. A pattern is a short loop of steps laid over the rate grid. It can have up to 64 steps; the pattern you build yourself has up to 16. Each step can play, rest, tie to the note before, accent, jump in pitch, repeat as a roll, play only some of the time, or fire a trigger. The default, `Plain`, has one step that always plays.

The order moves on only when a step plays a new note from the order. A rest, a tie, a step that misses on chance, a trigger-only step, and a step that plays a fixed note (see Pick below) leave the order where it was, so gaps in the rhythm never skip notes.

<!-- SPEC §4.3 (pattern length 1-64; none = a single plain step; "Only order picks advance m"), §4.6 (user length 1-16), §11 R1. -->

### The pattern bank

Status: READY-TO-WRITE — SPEC §4.3, spec/bank/starter.json, D-021

Eleven patterns come with Arp. They work with every order style.

- `Plain`: every step plays.
- `Off-beat`: only every second step plays, the off-beats.
- `Gallop`: a note, a rest, then two notes.
- `Three-three-two`: the 3 + 3 + 2 syncopation over eight steps.
- `Downbeat accent`: the first of every four steps is louder and the other three softer.
- `Tied legato`: ties hold notes across steps, so the notes are of different lengths and the order advances only on new notes.
- `Octave leap`: the second step jumps up an octave and the fourth down.
- `Roll at the end`: six plain steps, then a step that repeats its note twice and one that repeats it four times.
- `Sparse chance`: five sure hits in sixteen steps, the other eleven playing 30 % of the time. It repeats the same way for the same **Seed**.
- `Bar trigger`: every step plays, and the first of its sixteen steps also fires trigger 1 (for example, to clear a module).
- `Trigger pulse`: no notes; fires trigger 1 on every fourth step (every beat at the default rate).

<!-- SPEC §4.3 and spec/bank/starter.json (steps, vel 130/85, tie, step +1/-1 at the default step size of 12 semitones, ratchet 2 and 4, prob 30, trig 1). Names are the bank's `name` fields. -->
<!-- OPEN: which factory presets combine these patterns with orders and rates is curated at M3 (SPEC §12 Q-A3) and is not ruled yet; not described here. -->

[[AUD:arp.pattern-tour]]

### What a step can do

Status: READY-TO-WRITE — SPEC §4.3, §5.2, §8, D-011, D-016

Every step, in the bank or in your own pattern, carries the same fields.

[[FIG:arp.step-fields]]

- *Play*: whether the step sounds.
- *Tie*: instead of a new note, the note before is held on through this step. Ties work at any **Gate**. A tie straight after a rest does nothing.
- *Velocity* (0 to 200 %): scales the velocity of the step's note. The result stays within 1 to 127.
- *Gate* (1 to 200 %): scales this step's length against the global **Gate**.
- *Step* (−8 to +8): shifts the step's pitch by that many times the pattern's own step size, in semitones or scale degrees. At the default step size of +12 semitones, one step is an octave. The pattern's own step size is separate from the **Step size** setting, which is for transposition steps.
- *Chance* (0 to 100 %): how often the step plays. The result comes from the **Seed**, so it repeats.
- *Ratchet* (1 to 4): plays the step's note that many times, evenly spaced across the step.
- *Trigger* (0 to 8): fires one of Arp's eight triggers (0 fires none). See Triggers below.
- *Pick*: which held note or notes the step plays. `Order` (the default) plays the next note of the order style. A number from 1 to 8 plays the 1st to 8th lowest held note, counting round again if fewer are held. `Low` and `High` play the lowest and highest. `Chord` plays every held note.

<!-- SPEC §4.3 (fields and ranges; pick encoding §4.6), §5.2 (ties by look-ahead at any gate; a tie after a rest acts as a rest), §8 (triggers). The editor's own field labels are not in params.json (see OPEN in "Build your own"). -->
<!-- OPEN: the labels GUI 3 will give the step fields are not ruled; the names above are descriptive. -->

Ratchets and ties change a note's length, not the beat: a tied note carries on through the tied steps and ends where the gate of the last tied step says (at a gate above 100 % that is past the end of that step), and a ratcheted step's notes share the step's span, so the next step still lands on the grid.

<!-- SPEC §5.1 (ratchet sub-note i starts at on + i*span/r), §5.2 (tie tail: off = on(kAbs+t) + span(kAbs+t) * gate * step gate, so a gate above 100 % ends past the last tied step; a ratchet's length is capped at its slice). -->

[[AUD:arp.ratchets-ties-chance]]

### Build your own

Status: READY-TO-WRITE — SPEC §4.6, D-011, D-016

Choose `Build your own` in **Pattern** to play your own pattern, of up to 16 steps with every field above. A new pattern starts as eight plain steps. Besides the steps it has two settings of its own: a step size (−48 to +48, default +12) and whether that size counts in semitones or scale degrees; they scale each step's *Step* field.

[[FIG:arp.pattern-editor]]

Your pattern is saved inside Arp's module preset, not as a parameter you can automate. Each morph corner can carry its own pattern, and the pattern flips at the morph's switching point like any other choice; the contents of a pattern never blend. A change you make to the pattern takes effect from the next step, never in the middle of one, and values out of range are brought back into range.

A library keeps named copies of patterns you like, so you can recall one into the editor.

<!-- SPEC §4.6 (shape, default 8 plain steps, preset data not parameters, applies from the first step evaluated after the change, validation clamps, morph, library = named saved copies, step_size/step_unit pattern-level fields). -->
<!-- Q-H6 answered by horde (seq 8, D-034, their ADR-169): the user pattern is saved by value inside Arp's module preset, one blob per preset, and each morph corner may carry its own, flipping at the morph threshold. SPEC §4.6's "one pattern per preset" is per module preset: a corner holds Arp's module preset. -->
<!-- OPEN: where the pattern editor and the library sit on Arp's page is not designed (GUI 3); arp.pattern-editor stays pending-design. -->

[[AUD:arp.user-pattern]]

## Rate, sync and time mode

Status: READY-TO-WRITE — SPEC §2.1, §2.4, §2.6, §9, D-016

Arp has two ways to set the speed of its steps: **Rate mode** `Sync` (the default) follows the DAW's tempo, and `Time` runs a clock of its own.

### Sync

Status: READY-TO-WRITE — SPEC §2.1, §2.4, §9, §1.3

In `Sync`, **Rate** sets the length of a step in note values, from 2/1 down to 1/64, with triplets (a `T` after the value, for example 1/8T) and dotted values (for example 3/16, shown with its dotted name 1/8D). **Rate** defaults to 1/16. The step grid is the DAW's own: a step always falls on the same place in the song, whatever the buffer size, and follows tempo changes. Tempo is held between 20 and 999 bpm.

Changing **Rate** while the arp is running takes effect at the next step of the old rate, so with a long rate such as 2/1 the change can lag by up to eight beats. Notes already sounding keep their length.

<!-- SPEC §2.1 (22 divisions, TPQ), §1.3 (tempo clamp 20-999), §9 Q-A1 (a rate change applies at the next step of the old rate; up to 8 beats at 2/1), T15; params.json `rate` (dotted aliases). -->

When the DAW is stopped, Arp keeps playing: its steps count from your key press at the last tempo the DAW gave (120 bpm if it gave none), and stop once nothing is held, sounding or waiting. When the DAW starts, stops, jumps or loops, the arp's notes sounding at that moment end and the arp starts afresh from the new position. Notes of the chord stage are not affected.

<!-- SPEC §2.4 (the free clock: runs from a gesture start until idle, at the current tempo else 120), §7 T6, §14 (chord notes ignore transport edges). -->

### Time mode

Status: READY-TO-WRITE — SPEC §2.6, D-016

In `Time`, **Step time** sets the length of a step directly, from 10 ms to 2 s, shown in ms and in Hz together (10 ms is 100 Hz, 2 s is 0.5 Hz; the default is 125 ms, 8 Hz). The first step lands on your key press, whatever the DAW's tempo, position or transport are doing. **Rate** is ignored in `Time`, and **Step time** is ignored in `Sync`.

You can sweep **Step time** while playing. The arp speeds up or slows down smoothly from the step it is on, with no gap longer than the longer of the old and new step lengths and no burst of late notes. Notes sounding at the time of a change keep their length. Swing works in time mode as it does in sync.

In `Time` there are no bars, so **Restart** has nothing to count against and is greyed out: the pattern starts afresh at every new chord. With no keys held, no steps run, so nothing plays and no triggers fire.

Switching **Rate mode** ends the arp's notes sounding at that moment (not the chord's) and starts the pattern again.

<!-- SPEC §2.6 (L = step_time_us 10,000-2,000,000; both ms and Hz; fires from the key press; smooth change, no burst, E7; restart inert, r3 HIGH-2; no step while H is empty, E5; switching rate_mode = release-all then re-anchor, T19), §11 TM1-TM3. Swing in time mode: §2.6 "every §2.3 and §5 formula (offsets, swing, ...) applies unchanged". -->

[[AUD:arp.time-mode]]

## Gate and swing

Status: READY-TO-WRITE — SPEC §5.1, §5.2

**Gate** sets how long each note sounds as a share of its step: 1 to 200 %, default 80 %. Below 100 % the notes are separate; at 100 % they touch; above 100 % they overlap, which gives a legato line. If a new note starts on a key that is still sounding, the old note ends and the new one starts at the same moment. Each step's own gate scales this setting. With ratchets, the gate is a share of each repeat, not of the whole step, and a repeat never runs past its own slice, so ratcheted notes do not overlap.

**Swing** delays every second step: 50.0 % is straight, 66.7 % is a triplet feel and 75.0 % is the maximum. Swing follows the DAW's grid, so the same steps are swung however the pattern is restarted. A swing change applies from the next pair of steps, so the two steps of a pair always share one swing value.

<!-- SPEC §5.1 (swing 500-750 permille; parity from the grid step, not the pattern step; latched per pair, E6), §5.2 (len = span * gate * step gate; ratchet length is of the sub-span; T2 same key retriggered; legato above 100 via overlap); params.json ranges. -->

[[AUD:arp.swing]]

## Velocity

Status: READY-TO-WRITE — SPEC §4.4, §4.9, §5.2, D-036

### Played or fixed

Status: READY-TO-WRITE — SPEC §4.4

**Velocity** `As played` gives each arp note the velocity of the held key it comes from; `Fixed` gives every note the **Fixed velocity** (1 to 127, default 100). Each step's own velocity scales the result, which always stays within 1 to 127.

<!-- SPEC §4.4 (base = source key velocity or vel_fixed; out = clamp(base * step vel, 1, 127)). -->

### Randomness and the curve

Status: READY-TO-WRITE — SPEC §4.4, §4.9, §5.2, D-036

**Velocity random** and **Gate random** (0 to 100 %, default 0) make each arp note a little different. Each note's velocity, or its length, moves up or down by up to that share of itself; at 0 nothing changes. Velocity stays within 1 to 127. With ratchets, a note never runs past its own share of the step. The start time of a note never moves: only how loud and how long.

**Random curve** (0 to 100 %, default 0) shapes both. At 0, every value up to the amount is equally likely. Toward 100, most notes land near the setting with the occasional big one, like a human player.

The notes of the chord stage are never randomised: they follow your hands. The randomness comes from the **Seed**, so it repeats exactly.

<!-- SPEC §4.4 (velocity randomness formula), §5.2 (length randomness; ratchet cap kept), §4.9 (lane per note, curve blends uniform and bell), §11 RN1 (chord-stage notes untouched; timing never moves); D-036. -->

[[AUD:arp.randomness]]

## Transposition steps

Status: READY-TO-WRITE — SPEC §4.7, D-014

**Steps** repeats the whole pattern shifted. A pass is one full cycle of the order style: `Up` over three held notes is three steps, `Up-down` over three is four, and `Random` counts as many steps as there are held notes. After one pass the next pass is shifted by **Step size**; after **Steps** shifted passes, it drops back to the unshifted pass and begins again. **Steps** runs from 0 to 8 and defaults to 0, which is no transposition. **Step size** runs from −48 to +48 and defaults to +12; a negative size shifts down. **Step unit** chooses `Semitones` or `Scale degrees`.

For example, `Up` over C E G with **Steps** 2 and a **Step size** of +7 semitones plays C E G, then G B D, then D F♯ A, then C E G again.

In `Scale degrees`, a shift moves up or down through the keys of the current scale instead of by a fixed interval, so the shape stays in key. Without a scale (**Quantize** off) a degree is one semitone. A note a shift would push outside the keyboard (below 0 or above 127) is left out, not folded back.

A step that plays a fixed note (a **Pick** other than `Order`) shifts with the pass it falls in. The pass count starts again at 0 whenever the pattern restarts.

<!-- SPEC §4.7 (pass index t = mod(fdiv(m, P), steps + 1); worked example C E G with 2 steps of +7; degrees = in-scale keys of the effective scale, a degree is a semitone with quantize off or a zero mask; drop outside 0-127, T16; explicit pick takes the current pass, §13b.7; t resets with m at every anchor). -->

[[AUD:arp.transposition]]

## Latch and the sustain pedal

Status: READY-TO-WRITE — SPEC §3, D-000 Open 3

**Latch** keeps the chord going after you let go.

- `Off`: the arp plays only the keys you hold.
- `Replace`: when you let go the notes stay latched. Start a new chord (press a key when no key is held down) and the old latched chord is replaced by it.
- `Add`: every key you press is added to the latched notes, and pressing a key that is already latched takes it out again. Adding a key does not restart the pattern.

Switching **Latch** to `Off` lets go of every latched note at once; keys you are still holding stay.

The sustain pedal holds Arp's input: keys you let go of while the pedal is down stay in the pattern until you lift the pedal (or stay latched, if **Latch** is on). The pedal is one pedal for all channels. Arp takes the pedal for itself and does not pass it on to the engine, so Arp's own notes never pile up as tails.

<!-- SPEC §3 (DOWN / PEDAL / LATCHED; replace drops every LATCHED ref at a gesture start; append toggles a latched key out; pedal global in 1.0; CC 64 consumed), §13 item 6 (a press that joins a non-empty latched set neither re-anchors nor re-bases), §13 item 12 / §14 (latch applies at once). -->

[[AUD:arp.latch-pedal]]

### horde's own sustain pedal

Status: PENDING — D-034, horde RC-6.7

Once horde builds this behaviour, horde's own sustain pedal will hold only the notes you play directly. It will not hold the arp's or the chord's notes: their holding is Arp's, described above.

<!-- Q-H5 answered by horde (D-034, their ADR-282): the engine's sustain pedal holds `direct` voices only once the stream behaviour is built (their RC-6.7, round 36); `chord` and `arp` voices are held by Arp's input hold. Not built yet, so this is what horde will do, and the section stays PENDING (lead's ruling). -->

## Restart

Status: READY-TO-WRITE — SPEC §4.5, §9 Q-A2, D-000 Open 4

**Restart** decides which step of the pattern plays first; it never moves when steps fall, which is always the grid.

- `New chord`: the pattern starts from its beginning, on the next grid step, whenever you start a new chord (a key pressed with nothing held). It also starts again when you switch **Arp module** on, when the DAW jumps or loops, and when **Rate** changes.
- `Bar`: the pattern starts again at the first grid step of every bar, by the DAW's bars and time signature. If the rate does not divide the bar evenly, the first step at or after the bar line starts it.
- `Free`: with the DAW playing, the pattern runs along with the song and does not start again on a new chord; a new chord simply joins at whatever step is next. It starts again only when the DAW jumps, loops, starts or stops.

With the DAW stopped there is no song to follow, so `Bar` and `Free` behave like `New chord`: every new chord starts the pattern from its beginning, on your key press.

Changing **Restart** takes effect from the next step. Into `Bar` the pattern re-aligns to the current bar, and into `Free` to the song position, and in both the order starts again from its first note; into `New chord` the pattern keeps going until your next new chord. In `Time` mode **Restart** is greyed out and every new chord starts the pattern afresh.

<!-- SPEC §4.5 (the anchor table; bars from barStartBeats or the time signature; 10 of 22 rates do not divide a bar), §9 Q-A2, §2.6 (restart inert in time mode). Free: "re-anchors only on a jump or a clock switch"; the order position then carries on (m is reset only at an anchor). With the transport stopped, §2.4 H7a and §13.7: every gesture start re-bases the free clock and anchors (anchorK := 0, m := 0 under chord and free; bars recomputed under bar), so bar and free act as chord. §9 Q-A2: into bar or free the anchor is recomputed and m := 0. -->

[[AUD:arp.restart]]

## Repeats

Status: READY-TO-WRITE — SPEC §4.8, D-018

**Repeats** (0 to 16, default 0, shown as ∞) is how many times the pattern plays per chord before the arp stops and waits for the next one. 0 plays forever.

One repeat is one full cycle of everything: the pattern, the order style through every held note, and the transposition passes all coming round together. So 2 repeats of `Up` over C E G with the `Plain` pattern plays C E G C E G, then silence. A bigger chord or more **Steps** make each repeat longer.

The count starts when you start a chord, whatever **Restart** is set to. When it has run out, the arp stays silent until you start a new chord or add a key to the notes already held, which starts the count again. If keys are held or latched when the DAW starts, stops, jumps or loops, the count starts again then too. Neither letting go nor changing **Repeats**, **Steps** or **Pattern** revives it. A used-up arp plays no notes and fires no triggers; notes already sounding finish by their gates.

<!-- SPEC §4.8 (C = full cycle; the count runs from the gesture start; exhausted is a latched flag cleared by a gesture start or any NoteOn that adds a key, and by T6/T7 while H' is non-empty (E12: a transport start, stop, jump or loop with keys held or latched); spent steps emit no notes and no triggers; morphs of repeats, steps or pattern never revive it; E9, E11, E12), T20. -->

[[AUD:arp.repeats]]

## Scale quantize

Status: READY-TO-WRITE — SPEC §6, §4.7, D-010, D-020

**Quantize** pulls the notes Arp plays into a scale. One switch governs the whole of Arp: while it is on, every note Arp emits is in the scale, the arp's notes and the chord stage's notes (the chord stage's copy of the key you played included). The keys you play, which reach the engine around Arp, are not snapped.

- `Off`: notes are played as they are.
- `Scale`: each note moves to the nearest note of the scale. Notes already in the scale are unchanged.
- `Chord root`: the shape of the scale is anchored on the lowest note you are holding, so the scale moves with the chord. The lowest held note itself is always allowed. The root setting is ignored.

A scale with all twelve notes off disables quantize.

<!-- SPEC §6 (quantize off / scale / chord_root; the anchor pitch class always admitted; a zero mask disables quantize), §14 (one quantize switch, D-020; the played note is snapped too), §13b.1. -->

### Which scale

Status: READY-TO-WRITE — SPEC §6, D-010

**Scale** chooses where the scale comes from. `horde's scale` (the default) is horde's own global scale: Arp reads it and never changes it. `Arp's own` uses a scale kept in Arp, in exactly the same shape as horde's: a root (**Own scale root**, C to B) and twelve switches (**Own scale: +0** to **+11**), one for each note from the root up. A switch for a note that is on means that note, in every octave, is in the scale. The own scale starts as C major.

A change of scale reaches the arp from the next step and never moves an arp note that is already sounding. The chord stage follows at once: its changed notes are re-voiced immediately. The settings of one scale (**Quantize**, **Scale**, **Own scale root** and the twelve switches) move together under morph, so a morph between two scales never passes through a hybrid of both.

<!-- SPEC §6 (the schema {root, mask}; the mask is relative to the root; scale_source global | own; default own scale C major; one morph group, r2 M2; "A scale change never moves a sounding note" is the arp's rule; the chord stage re-maps at offset 0 on a scale-setting change, §14). -->
<!-- OPEN: whether Arp's page offers a list of named scales (major, dorian, ...) for the own scale, as horde's own scale control does, is not ruled; params.json carries the names only as UI vocabulary (ui_scale_names). Not described here. -->

### How notes are snapped

Status: READY-TO-WRITE — SPEC §6, §4.7

When a note is equally near two scale notes, it goes toward the note Arp played just before, so the line stays a melody; with no earlier note, or when both are equally near it, it goes down. Every stop, start, jump or panic forgets the earlier note, so playing from the same place always sounds the same and a bounce matches what you heard.

If two notes of one step land on the same key, that key plays once. A scale of few notes can therefore make a line repeat notes, because neighbouring keys snap to the same one.

In `Scale degrees`, the move is counted in the scale's own notes, so with a scale on, **Steps** and the chord stage keep the shape in key.

<!-- SPEC §6 (tie toward lastQ, else lower; lastQ resets at every release-all, SETTLED r2 H2; collisions emitted once), §13 "A listening note" (up over C# D# F# G# A# in C major collapses pairs), §4.7 degrees. -->

[[AUD:arp.scale-quantize]]

## The chord stage

Status: READY-TO-WRITE — SPEC §14, D-018, D-019, D-020

The chord stage works before the arp. Switch **Chord** on and every key you press becomes that key plus up to six other notes, held for as long as you hold the key. The chord is its own source, separate from the arp's, so horde can send the chord to some oscillators and the arp to others.

Each of the six slots, **Chord interval 1** to **Chord interval 6**, has an on switch (**Chord interval 1 on** and so on) and an interval from −24 to +24. **Chord unit** says whether the interval counts in `Semitones` or `Scale degrees`. By default intervals 1 and 2 are on, at +4 and +7: a major triad. The key you play is always part of the chord.

[[AUD:arp.chord-stage]]

With **Quantize** on, every chord note is snapped into the scale, the chord's copy of the key you played included; where a snap is a tie, it goes to the lower note. In `Scale degrees` the interval is counted from the nearest scale note to the key you played; with **Quantize** off, or with all twelve notes of the scale off, a degree is one semitone. Notes that would fall outside the keyboard are left out.

The chord follows only your keys, the pedal and the latch. It ignores the DAW's transport and the arp's on and off. If two held keys produce the same chord note, it plays once and ends only when the last key that made it is let go. If you change an interval, the unit or the scale while holding keys, only the notes that changed are re-voiced, at once, and the others keep sounding.

With **Chord** on, the arp plays the chord's notes instead of your raw keys. With **Arp on** off, the stage alone makes Arp a chord generator.

[[AUD:arp.chord-generator]]

<!-- SPEC §14 (the mapping stored per key and slot; one quantize switch and chord-stage ties go lower; chord notes behave like held notes: ignore transport edges, arp on/off; dedupe by key with a source count; changes re-map only the changed notes, Q-A8 and "These settings apply at offset 0"; "Feeding the arp"; Chord interval defaults +4 +7 on, from params.json), §13 item 13. -->

## Triggers

Status: READY-TO-WRITE — SPEC §8, §2.5, D-007

A step can fire one of eight triggers (1 to 8). A trigger is not a sound: it is a signal event that horde can use to act on another module (for example, to clear one), at the exact moment of the step. horde lists the eight triggers as event sources named Arp · T1 to Arp · T8; how to route them is covered in the modulation chapter. A trigger fires at the step's first note, once, even if the step is a ratchet. A step with no note (a rest, or no keys held) can still fire its trigger, and a trigger waits for its **Chance** like a note does.

While the DAW is playing, a step with a trigger fires even with no keys held when **Restart** is `Bar` or `Free`, so `Trigger pulse` keeps ticking. With `New chord` nothing fires until the first chord. With the DAW stopped, a trigger-only step can still fire after you let go, until Arp goes idle (while notes you released are still sounding or waiting). In `Time` mode nothing fires once no keys are held.

<!-- SPEC §8 (lane = s.trig at the step's first on; no state, no terminal), §4.3 (a trigger-only step fires if `fires`), §2.5 (steps run only when the clock runs; H11; under restart = chord there is no anchor before the first gesture; time mode idle while H empty; §2.4 the free clock runs until idle, i.e. also while released notes still sound). -->
<!-- Q-H3 answered by horde (D-015, ADR-267 A1): Trigger{lane 1-8, sampleOffset}; horde shows the lanes as event sources "Arp · T1–T8". Routing is in horde's modulation chapter. -->

## Seed

Status: READY-TO-WRITE — SPEC §10, §4.9, D-036

**Seed** is a whole number from 0 to 4,294,967,295 (default 0). Everything Arp leaves to chance, `Random` and `Shuffle` orders, a step's **Chance**, **Velocity random** and **Gate random**, is drawn from it, never from the moment or the machine. The same playing, settings, transport and **Seed** give the same notes every time; a different **Seed** gives a different take of the same randomness. The result is the same at every buffer size, and at every sample rate apart from the exception described at the top of this page (a note played within a fraction of a millisecond of a step).

While the DAW is stopped, each new chord starts a new run of random choices, so playing the same chord twice does not repeat the same random run.

<!-- SPEC §10 (draw(seed, x, lane, epoch); D1), §2.4 (epoch: H7c, random and prob do not replay the same sequence on each chord while the transport is stopped), §2.3 G1, §11 D1. -->

## Arp and morph

Status: READY-TO-WRITE — SPEC §9, D-021

Every Arp setting can be a target of horde's morph. **Gate**, **Swing**, **Fixed velocity**, **Velocity random**, **Gate random**, **Random curve** and **Step time** blend smoothly between corners. Every other setting switches at a point on the way, to the corner that clearly leads. Most take effect from the next step and never cut a sounding note. A few act at the moment they change and can end or re-voice notes that are sounding: **Arp module**, **Arp on** (which ends the arp's notes), **Rate mode** (which ends the arp's notes and starts the pattern again), **Latch**, and **Chord** with its unit, intervals and the scale settings (which re-voice the chord's notes). Morphing the choice of **Pattern** to `Build your own` works like any choice. A morph never revives an arp that has run out of **Repeats**.

The table at the end says, for each setting, whether it blends or switches.

<!-- SPEC §9 (continuous vs discrete settings; Q-A1: the leading corner must exceed the current by 0.05, so it does not flutter at the half-way point; applies from the next evaluated step), T8/T9, §4.8 (morphs of repeats, steps or pattern never revive). T4/T5 (Arp module), §13.14 / §14 (Arp on, Chord and the chord settings, the scale settings for the chord stage: offset 0), T19 / §2.6 (Rate mode), §13 item 12 (Latch). -->

## Tips

Status: READY-TO-WRITE — SPEC §4.3, §4.7, §5.2, §14, D-037

- **Legato lines:** raise **Gate** above 100 %, or use `Tied legato`, which holds notes across steps at any gate.
- **A chord that moves, an arp that stays:** switch **Chord** on, give one oscillator the chord stage alone and another the arp alone with their note source toggles *(name pending)*, then play single keys.
- **Humanise:** a little **Velocity random** and **Gate random**, with **Random curve** raised, keeps most notes near the setting and lets a few stray.
- **Different takes:** change **Seed** to get a different run from `Random`, `Shuffle`, a step's **Chance** and the randomness sliders.
- **Play once:** set **Repeats** to 1 and **Latch** to `Replace` to tap a chord and have it play through once. While the chord stays latched, it plays again each time the DAW starts, stops, jumps or loops.
- **An arp that ignores the song:** use **Rate mode** `Time` and sweep **Step time** for accelerandos.
- **A scale that follows the chord:** **Quantize** `Chord root` moves the scale with the lowest note you hold.
- **Buzz from ratchets:** a roll at a fast rate and tempo (for example four repeats inside a 1/32T step at 174 bpm) is a note every few milliseconds and is heard as a pitch. That is the pattern doing what it says; nothing limits it.
- **Too many keys:** Arp holds up to 32 keys at a time and ignores further presses.
- **A stuck latch:** switch **Latch** off, or send a panic from the DAW, to clear it.

<!-- SPEC §5.2 (gate > 100 and ties), §14 (chord and arp are separate sources), §4.9 / §10 (randomness and seed), §4.8 + §3 (repeats with latch), §2.6 (time mode), §6 (chord_root), §1.3 (32 held keys: "a new key is ignored"), §3 (latch off removes every latched ref), §7 T7 (panic clears latch and pedal). D-037: the "dense" buzz at 4x ratchet in 1/32T at 174 bpm is the pattern's own doing; no rule limits audio-rate ratchets. -->

## Performance

Status: READY-TO-WRITE — SPEC §1.3, docs/horde-copy-in.md "CPU envelope", D-031, D-038

Arp is light. horde sets aside a small share of one core for it, and the heaviest combinations measured stay inside that share: many held keys, rolls of four, the chord stage with all six intervals, the fastest rates at the fastest tempos and both randomness settings at 100. The cost grows with how many notes Arp plays and with how much note traffic reaches it, not with how long it has been running. Arp never allocates memory or waits on anything while it plays.

The heaviest settings take the most of the share, and a very dense stream of incoming notes, such as a whole chord re-struck on every 64th step at a very fast tempo, adds to it. Both are stress cases, not normal playing. Measurements were made on a development Mac with a safety multiplier, not on the slowest machine horde supports.

<!-- docs/horde-copy-in.md "CPU envelope" (slice 2 % of a min-spec core; every settings-only load fits; x4 = horde's min-spec heuristic, not min-spec verified); SPEC §1 (process: no allocation, no locks, no system calls); D-031. Numbers are deliberately not quoted: D-038 says the input-rate figure is re-measured on a quiet machine before the readiness notice. -->
<!-- OPEN: the final input-rate limit (events per second) horde will quote, and the min-spec verification, are pending (D-038). -->

## Settings reference

Status: READY-TO-WRITE — SPEC §9, D-035

Every Arp setting, by its display name, with its control, range or choices, default and unit. "In a morph" says whether the setting blends smoothly or switches (see Arp and morph). The table is generated from the settings file, so it cannot drift from Arp.

<!-- BEGIN GENERATED: params -->
<!-- Generated by tools/gen_manual_params.py from spec/params.json and spec/bank/starter.json. Do not edit by hand. -->

| Setting | Control | Range or choices | Default | Unit | In a morph |
|---|---|---|---|---|---|
| **Arp module** | Switch | On, Off | Off | — | Switches |
| **Arp on** | Switch | On, Off | On | — | Switches |
| **Order** | Choice | Up, Down, Up-down, Up-down (ends twice), As played, Chord, Random, Shuffle (no repeats), Converge, Diverge, Pedal low, Pedal high, Chord then walk, Stride, Chord between notes, Pairs | Up | — | Switches |
| **Pattern** | Choice | Plain, Off-beat, Gallop, Three-three-two, Downbeat accent, Tied legato, Octave leap, Roll at the end, Sparse chance, Bar trigger, Trigger pulse, Build your own | Plain | — | Switches |
| **Rate** | Choice | 2/1, 3/2 (1/1D), 2/1T, 1/1, 3/4 (1/2D), 1/1T, 1/2, 3/8 (1/4D), 1/2T, 1/4, 3/16 (1/8D), 1/4T, 1/8, 3/32 (1/16D), 1/8T, 1/16, 3/64 (1/32D), 1/16T, 1/32, 3/128 (1/64D), 1/32T, 1/64 | 1/16 | — | Switches |
| **Rate mode** | Choice | Sync, Time | Sync | — | Switches |
| **Step time** | Number | 10 ms … 2 s (100 Hz … 0.5 Hz) | 125 ms (8 Hz) | ms (and Hz) | Blends |
| **Steps** | Number | 0 … 8 | 0 | passes | Switches |
| **Step size** | Number | −48 … +48 | +12 | st or scale degrees, per **Step unit** | Switches |
| **Step unit** | Choice | Semitones, Scale degrees | Semitones | — | Switches |
| **Gate** | Number | 1 … 200 | 80 | % | Blends |
| **Swing** | Number | 50.0 … 75.0 | 50.0 | % | Blends |
| **Latch** | Choice | Off, Replace, Add | Off | — | Switches |
| **Velocity** | Choice | As played, Fixed | As played | — | Switches |
| **Fixed velocity** | Number | 1 … 127 | 100 | MIDI velocity | Blends |
| **Velocity random** | Number | 0 … 100 | 0 | % | Blends |
| **Gate random** | Number | 0 … 100 | 0 | % | Blends |
| **Random curve** | Number | 0 … 100 | 0 | % | Blends |
| **Restart** | Choice | New chord, Bar, Free | New chord | — | Switches |
| **Repeats** | Number | 0 … 16 (0 = ∞, forever) | 0 (∞) | cycles | Switches |
| **Quantize** | Choice | Off, Scale, Chord root | Off | — | Switches |
| **Scale** | Choice | horde's scale, Arp's own | horde's scale | — | Switches |
| **Own scale root** | Number | C … B | C | — | Switches |
| **Own scale: +0 … +11** | 12 switches | On, Off each; +n is n semitones above the root | On at +0, +2, +4, +5, +7, +9, +11: major (ionian) | — | Switches |
| **Seed** | Number | 0 … 4,294,967,295 | 0 | — | Switches |
| **Chord** | Switch | On, Off | Off | — | Switches |
| **Chord unit** | Choice | Semitones, Scale degrees | Semitones | — | Switches |
| **Chord interval 1 … 6**, with their switches **Chord interval 1 on … 6 on** | 6 pairs: switch and number | −24 … +24 | 1: +4, on; 2: +7, on; 3: +12, off; 4–6: 0, off | st or scale degrees, per **Chord unit** | Switches |
<!-- END GENERATED: params -->
