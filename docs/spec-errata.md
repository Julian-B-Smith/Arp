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

_Applied 2026-10-23 (D-024): E1 and E3 amended in SPEC §2.3 and §1.3, spec re-pinned. E2 needed no amendment: fixed in code (owed offs are delivered at offset 0 of the first process after prepare), so T10 holds as written._
