# Spec review r3: the independent critic's findings on r2.8, and their disposition

- **Reviewer:** the `critic` agent (Opus, pinned), 2026-10-22. It was
  sourced separately from the author and was read-only.
- **Scope:** time mode (§2.6), the step offset (§4.3, §4.7), the pattern
  library and its format (§4.6), TM1 and ST1.
- **Verdict:** REWORK. The time-mode grid was unsafe.
- **Checked sound:** time-mode offsets are exact and partition-invariant; no
  overflow at 768 kHz; swing, ratchet and 1-sample resolution hold; degree
  moves create no new chord collisions.

All findings are folded into **SPEC r2.9** (D-017).

| # | Sev | Finding (short) | r2.9 disposition |
|---|---|---|---|
| HIGH-1 | HIGH | a continuous `step_time_us` change re-indexes the grid: 125 → 250 ms left a 2.25 s silence; 2 s → 10 ms fired 299 overdue steps at one sample, breaking the step bound and the 512-slot pending queue; morph rounding alone caused bursts; §9 called it discrete | **the human chose "smooth", by poll**: a piecewise grid, where the next step lands at `max(lastTick + L_new, changeTick)` and later steps are spaced by `L_new`; no gap longer than max(L), no burst; §9 lists it as continuous; new terminal row T18; new invariant TM2 with a plant |
| HIGH-2 | HIGH | `bar` in time mode used 61.44 ms bars, so `m` reset every step and `up` played E[0] forever; restart was silently inert | restart is **inert** in time mode (`bar` = `free` = `chord`), and the UI greys it out; invariant TM3 |
| MED-3 | MED | the "Applying offsets" order could be read per key or per stage (different `lastQ` chains); the drop and quantize were duplicated in §4.3; §13b.4 was stale | **per key**, in ascending order, through six stages; `lastQ` is set only by a key that survives the last drop; the duplicates are removed and §13b.4 is superseded |
| MED-4 | MED | the timing of a `rate_mode` switch, entering time mode with keys held, and a `rate` morph in time mode were all undefined | the switch applies at offset 0 of its block (T19); entry with H held starts the clock; the inactive rate setting is ignored |
| MED-5 | MED | ST1 could not catch reading the global step_size (same names and defaults); TM1 omitted swing; no checks for L changes, bar in time mode, or rate_mode in N1 | an ST1 plant reads the global size, tested with differing values; TM1 includes swingDelay; TM2 and TM3 are added; rate_mode is in the N1 sessions |
| LOW-6 | LOW | `arp-user-pattern/1` was underspecified, and the repo file carried a prose `step_defaults` | format rules added: unknown fields ignored, other versions rejected, pad or truncate to length, validate, repo entries read-only, a browser same-name save is a separate entry, bank per-pattern overrides; the file's `step_defaults` became `note` |
| LOW-7 | LOW | with the global unit in degrees, the new order changes r2.7 behaviour (a key near the edge can drop; a tie can flip) | accepted and recorded (§4.7 note, D-017) |
| LOW-8 | LOW | §2.6 sat before §2.5; §2.5 did not cover time mode; bank patterns had no per-entry override | reordered; §2.5 amended; per-pattern overrides allowed |

**The critic's question for the human** (smooth versus restart on a
step-time sweep) was answered by poll: **smooth**.
