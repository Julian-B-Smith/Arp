# 2026-10-29 — D-038 re-measure, attempt 2 (idle): 1,000 events/s with every setting (D-042)

**What changed.**
- `docs/horde-copy-in.md`: a new section, "The quoted input limit", with the
  idle figures and the 1,000 quote; the older estimates and the 1,300 are
  marked superseded.
- `tests/core/bench.cpp`: a `benchOnly` flag, kept out of both digests; the
  112-note randomness load factored into `load112Random`; a 4-keys input
  family on it.
- D-042, and ROADMAP T-007's D-038 line marked done.

**Why.** D-038 and D-041: quote one input figure covering every setting,
measured on an idle machine, before the readiness notice.

**Conditions.**
- A fresh restart, the battery charged, OneDrive paused by the human, a
  culture session's work finished.
- The human raised the quiet rule to Maw's (1-min load < 1.5, 5-min < 2.0).
  The run started at 12:00:40, at load 1.44 / 1.96.
- Apple M3, Release, main `a90f03c`'s core (the bench change adds a scenario
  and touches no core file).

**Evidence (scratchpad outputs, summarised in the doc):**
- `core_bench.sh`, wall clock and `--cpu-time`: settings alone, 0 of 195
  over on both paths. Worst 1.79 % native and 1.91 % two-word.
- A/B against `e1dbd3c`, `--cpu-time`, three runs each: D-039 costs
  x1.015-1.02 at 48, 96 and 192 kHz, and x1.004-1.013 at 44.1 kHz.
- Binding input: B++ with randomness, 4 keys re-struck, two-word, 192 kHz:
  1,461 (wall), then 1,416 and 1,415 (cpu).
- Two suspicious lows (456 and 1,532) were re-run four times each.
  - The 456's re-runs read 11,300-12,700.
  - The 1,532's family read 3,930-4,252, except one 1,976 in another
    configuration.
  - Both are taken as scheduling, a judgement recorded as such.
- `./verify fast` green. `core_test` shows the bench digests unchanged
  (randomness bench digest 56 of 56; main bench digest clean).

**The human's choices (polls, 2026-10-29):**
- Maw's quiet rule plus a CPU-time pass;
- quote 1,000 with every setting, dropping D-033's 10 % margin;
- after the critic: keep 1,000 and state the thin margin plainly.

**Critic (Opus, independent), verdict REWORK; all findings applied to the
wording. The code was clean.**
- HIGH: the spike claim overstated (the 1,976 re-run); "30 % below the edge"
  hid a CPU margin of about 1.5 % at 1,000 events/s, and the 1.97 %
  CPU-time reading.
- MEDIUM:
  - three load-alone-over readings, not two;
  - the B++ (no randomness), B+ and A family figures were wrong or missing;
  - "every setting" and "measured" overclaimed.
- LOW: D-039's 44.1 kHz ratio; the "reads higher" note.
