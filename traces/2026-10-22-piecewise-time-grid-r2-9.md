# piecewise-time-grid-r2-9 — lab follows SPEC r2.9: a smooth step-time sweep, inert restart in time mode, per-key offsets, library rules

- **Queue item:** unqueued: lead dispatch of 2026-10-22 (D-017, the third independent review, `docs/spec-review-r3.md`; the human ruled
  by poll that a step-time sweep must be smooth). It follows `2026-10-22-time-mode-library-step-offset.md` and **supersedes its
  SPEC-GAPs 1 and 2** (the re-grid on absolute multiples, and `bar` counting from tick 0).
- **Changed** (`lab/arp-lab.html`, `tools/lab_smoke.mjs`; nothing else):
  - **Piecewise grid (§2.6).** In time mode the tick of step k is `_gtick(k) = origin + (k - k1)·L`. A changed `step_time_us` is folded
    in at the BLOCK START (`_timeRegrid`): the next unevaluated step `k1 = lastEvaluated + 1` lands at
    `max(lastTick + L_new, changeTick)`, `changeTick` being the block-start position rounded up to a whole microsecond. Later steps are
    `L_new` apart, `kAbs`, `m`, the pattern position and swing parity carry on, and sounding notes keep their offs (T18). A gesture
    start resets the origin (step 0 on the press). Sync mode still uses `k·L`. `_kAtOrAfter(pos)` replaces the four `cdiv(pos, L)` sites.
  - **Restart inert (§2.6).** `_rs()` returns `chord` in time mode, so `bar` and `free` anchor at each gesture; `_adopt` ignores a
    restart change there. The UI greys `restart` out.
  - **Mode switch.** Already a T19 clock switch at offset 0; entering time mode with H non-empty starts the time clock at that offset
    (was present, now checked). `rate` is ignored in time mode and `step_time_us` in sync (no T15, no re-anchor).
  - **Per-key offsets (§4.7).** Each key runs the six stages (`key + S`, drop, quantize, move `G`, drop, set `lastQ`) in ascending
    pre-quantize order; `_quant` no longer writes `lastQ`, the caller does, and only for a key that survives the final drop.
  - **Library rules (§4.6).** A `format` other than `arp-user-pattern/1` (or none) is rejected with a message, at import, on repo files
    and on stored browser entries; unknown fields are ignored; steps pad/truncate to `length` through `setUserPattern`. Repo entries are
    tagged "(repo)" and read-only; browser entries are "(browser)" and separate even when the name matches a repo pattern. Bank patterns
    may carry their own `step_size`/`step_unit` (already honoured).
  - **Plants (new):** `regridAbsolute`, `regridKAbs`, `timeBarsCount`, `offsetsPerStage`, `stepOffsetReadsGlobal`, `timeEntryNoStart`,
    `inactiveReanchors`.
  - **Smoke.** TM1 now includes the swing term (swing 500/600/667, bpm 60/120/174); new TM2 (125→250 ms: one exact 250 ms gap;
    2 s→10 ms: fires at the change point, then every 10 ms; a sweep up and down with no gap past max(L_old, L_new), no burst, N1), TM3
    (restart inert; entering time mode with a key held; the inactive setting changes nothing); ST1 gains the critic's example
    ({61, 63}, C major, +2 degrees → {64, 67}) and a pattern step size/unit that differs from the global ones.
- **Evidence consulted:** `spec/SPEC.md` r2.9 (§2.5, 2.6, 4.6, 4.7, 7 T18/T19, 11 TM1-TM3, ST1), `docs/spec-review-r3.md` (existence),
  the lab and smoke.
- **SPEC-GAPs (marked `// SPEC-GAP:` where they live):**
  1. `changeTick` is the block-start position rounded UP to a whole microsecond (positions are fractional µs at most sample rates).
  2. A tie's look-ahead across a regrid uses the current grid extended at `L` (the notes' later steps may move when the time changes again;
     their off is fixed at scheduling, per §13 item 10).
  3. With no step yet evaluated since the gesture (lastEvaluated < 0) a change just sets `L`: step 0 is still on the press.
  4. §4.7 stage 6 ("a dropped key never sets lastQ") has no observable difference in the smoke, because keys are processed in ascending
     order, so a later tie is never pulled up by a dropped lower key; the rule is implemented and exercised only through the six-stage
     example.
- **Verify:** `node tools/lab_smoke.mjs` exit 0; `./verify fast` exit 0. UI under a stubbed DOM only (library load/save/import, a
  rejected `arp-user-pattern/2`, a padded import, time mode on and off).
- **Open questions:** none.
