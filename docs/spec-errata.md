# Spec errata: implementation findings pending the human's batched re-ratification

The 1.0 spec is RATIFIED and hash-pinned (D-022, D-023), so it is not edited
in passing. When the C++ build finds that the spec's text should change,
the finding is logged here. The human approves the batch at a milestone
gate, and the spec, its pin and a decision record then change together.

Gaps that are mere latitude (an implementation choice the spec permits) are
recorded in the build's trace, not here.

| # | Found | Spec text | What the C++ does, and why | Proposed amendment |
|---|---|---|---|---|
| E1 | M1a, 2026-10-23 | §2.3: "an offset outside the window is an **assertion failure**" | A block with `n` out of range, or an input offset outside `[0, n)`, is **refused and counted** (`badInput`). The audio thread must never abort | "…is refused and counted (`badInput`), never an abort." |
| E2 | M1a, 2026-10-23 | §1 `prepare` "resets all state", with no output channel | `prepare()` cannot emit T10's offs, so the adapter calls `reset()` first to collect them | §1: "the adapter calls `reset()` before `prepare()` to collect T10's offs." |
| E3 | M1a, 2026-10-23 | §1.3 sounding table: 128 | 16 extra physical slots hold notes released inside their own on-sample (the §1.1 one-sample deferral keeps them); if those run out, the on is refused and counted | §1.3: "plus a small slack for notes deferred by the 1-sample minimum." |
| E4 | M1b, 2026-10-23 | §1.2: the id counter "starts at 0 at `prepare` and `reset`" | After a `prepare()` that leaves owed offs, the counter starts above the largest owed id, so a new note never shares a ref with an owed off (M1a critic LOW-A) | "…or above the largest owed id when `prepare` leaves owed offs." |
| E5 | M1b, 2026-10-23 | §2.6: the time clock "stops when Arp is idle" (§2.4's idle), against §2.5's "idle while H is empty" | Trigger-only steps fired after key release while a note rang (M1b critic M1). Now no step is evaluated in time mode while H is empty | §2.6: "it evaluates no step while H is empty (§2.5)…" |
| E6 | M2a, 2026-10-23 | §5.1: `span(kAbs) = on(kAbs+1) − on(kAbs)` with ons frozen at evaluation, while `swing` is continuous (§9) | A swing change between two steps let a ratcheted step's last sub-notes sound after the next step's on and retrigger it on the same key (M2a critic M2) | §5.1: swing is latched per step pair (even step reads it, its odd partner reuses it) |
| E7 | M2a, 2026-10-23 | §2.6 and row TM2: "no gap longer than `max(L_old, L_new)`" | With swing above 500 an on-to-on gap is longer by the swing delay, so the sentence was false (M2a critic) | "no gap between grid positions (before swing)…" |
| E8 | M2b, 2026-10-23 | Row CH1: "deduplicated by (channel, key)", against §14: "keyed by **key** alone" (r4 M2) | The code follows §14 (M2b critic M3) | Row CH1: "deduplicated by key (E8)" |
| E9 | M2b, 2026-10-23 | §4.8: `C` from the live H′ at each step; with no keys held `P` is 0 | The count paused while no key was held, so trigger-only steps released early fired until the next chord (M2b critic M2) | §4.8: keep the last non-empty `C` while H′ is empty, so the count runs out |
| E10 | M2b, 2026-10-23 | §5.1 (E6): "a step's ratchets never sound after the next step's on" | False in time mode: a `step_time_us` change can bring the next on before a pending sub-note (M2b critic M4) | §5.1 and §2.6: drop the earlier steps' pending sub-notes at or after the next step's on (one exactly at it yields to the step, ahead of §1.1's pending-first order) |
| E11 | M2 re-check, 2026-10-23 | §4.8 (E9): `C` computed at evaluated steps with keys held | A chord tapped and released before the next step never set `C`: the first such tap fired trigger steps without end, later taps used the previous chord's `C` (re-check M1) | §4.8: `C` is also set the moment H′ becomes non-empty |
| E12 | M2 re-check, 2026-10-23 | §4.8: `exhausted` clears at T6, T7 or T10 | With no keys held, every loop wrap, jump or Panic revived trigger-only steps for another round (re-check L2) | §4.8: T6/T7 clear it, and reset `repeatSteps`, only while H′ is non-empty |
| E13 | M3 (T-007), 2026-10-27 | §1.1 orders output events at one offset and §2.3 puts a grid step at `cdiv` of its position; nothing orders an INPUT event against a grid step that land on the same sample | The core takes the input first (core.cpp, the timeline loop: "input, then offs, then pending ons, then the grid step"). An input less than one sample AFTER a step's tick shares the step's (rounded-up) sample at one rate and not at another, so the step reads a different held set: a G4 breach on musically identical input (T-007 critic H1). Known answer: 209 bpm, 1/32, keys 60 and 67 held from 0, key 67 released at tick 21120.704 (8 kHz sample 3158, exact at 48/96/192 kHz), 0.70 tick after step 11: step 11 plays key 60 at 48 kHz, key 67 at 96 and 192 kHz | (A), chosen: musical order inside one sample, for every event kind (§1.1) |
| E14 | M3 (T-007, D-039 work), 2026-10-27, PENDING the human | §1.1 (H4): an off that falls on its note's own on-sample is emitted one sample later; `Event.tick` of a NoteOff is "the tick its off was SCHEDULED at" | A note released inside its own on-sample (a Panic, a T6 edge, a retrigger) is deferred, and the deferred off reports `kNoTick`: its scheduled tick is dropped (core.cpp `releaseSlot`, `x.hasOff = false`). Whether the release lands on the on's sample depends on the sample rate, so the same musical release reports tick 14016 at 96 kHz and `kNoTick` at 48 kHz: a G4 breach on identical input in the off's tick field (the notes, their samples' order and their count agree). Known answer: 108 bpm, 1/16, ratchet 2, key 60 held, Panic 0.38 tick after the second sub-note of step 3 (test_core.cpp testG4Order, the pending case) | (A) a deferred off keeps the tick it was scheduled at (`kNoTick` only for a note that never had one); (B) no change, and G4 compares an off's tick only when the off was not deferred |

_Applied 2026-10-23 (D-024): E1 and E3 amended in SPEC §2.3 and §1.3, spec re-pinned. E2 needed no amendment: fixed in code (owed offs are delivered at offset 0 of the first process after prepare), so T10 holds as written._

_Applied 2026-10-23 (D-025): E4 and E5 amended in SPEC §1.2 and §2.6, spec re-pinned._

_Applied 2026-10-23 (D-026): E6 and E7 amended in SPEC §5.1, §9, §2.6 and row TM2, spec re-pinned._

_Applied 2026-10-23 (D-027): E8, E9 and E10 amended in the CH1 row, §4.8, §5.1 and §2.6, spec re-pinned._

_Applied 2026-10-23 (D-028): E11 and E12 amended in §4.8, spec re-pinned._

_Applied 2026-10-27 (D-039): E13 option (A) amended in SPEC §1.1 for every event kind (an input yields to an earlier off, pending on or grid step on its sample), spec re-pinned. The pending and off cases, the hypothesis below, were confirmed by known answers (test_core.cpp testG4Order, tools/lab_smoke.mjs §7b): a Panic just after a sub-note cleared it at 44.1 and 48 kHz only; one just after a gate end took over that note's terminal (T7 for T1) there only. E14 was found doing it, pending._

_E13, as proposed (T-007, 2026-10-27; trace `traces/2026-10-27-g4-handover.md`). The options:_
- _**(A), recommended: order by musical position.** At a shared sample, a grid step whose tick lies strictly before the input's exact position (the sample's position, `start + s·inc`) is evaluated first; on an exact tie (the step's tick ON the sample) the input still goes first, so a chord pressed exactly on the beat still plays that beat. Since an input and a step share a sample only when the step's tick is at or before the input's position, this is "the earlier musical event first, input first on an exact tie", which no sample rate can reorder. It moves output in the rare tie cases, so both digests move (a decided change). Hypothesis, not yet checked: the same class may exist between an input and a due off or pending sub-note on one sample; the rule would cover those too if worded for every event kind._
- _**(B) step first at a shared sample, always.** Rate-independent, but a chord pressed exactly on a step's tick would miss that step at every rate: worse to play._
- _**(C) no change; narrow G4** to events whose causes are more than one sample apart, and say so in the G4 row. No output change; G4 stays unproven near ties._
