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

_Applied 2026-10-23 (D-024): E1 and E3 amended in SPEC §2.3 and §1.3, spec re-pinned. E2 needed no amendment: fixed in code (owed offs are delivered at offset 0 of the first process after prepare), so T10 holds as written._

_Applied 2026-10-23 (D-025): E4 and E5 amended in SPEC §1.2 and §2.6, spec re-pinned._

_Applied 2026-10-23 (D-026): E6 and E7 amended in SPEC §5.1, §9, §2.6 and row TM2, spec re-pinned._
