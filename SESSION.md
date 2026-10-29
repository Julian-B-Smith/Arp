# SESSION — Arp (hot state; the only prior-session context the next session trusts)

_Closed 2026-10-29 by `/breakdown`. Written last, so it describes the tree as it ends._

## Where it stands
- **Phase: M3, horde admission** (ROADMAP T-007).
  - The spec is RATIFIED (2026-10-23) and functionality is frozen.
  - The C++ core is built through M2, plus note randomness (D-036).
  - The test suite runs 539 checks on each int128 path, in `./verify fast`.
- **Merged on main** (main at `a90f03c`):
  - #29: randomness;
  - #31: the G4 hand-over and E13 option C (D-039);
  - #32: the manual page, with `manual_gate`;
  - #33: the replies to horde (D-040).
- **The manual page** (`docs/manual/arp.md`) is written. Its settings table is generated from `spec/params.json` and checked by `./verify fast`. Five questions stay marked OPEN on the page, not guessed. Filing it into horde's manual comes later.
- **horde replies filed** (seq 11 and seq 12, `integrations/hypersaw/`):
  - two arps behind one chord stage: nothing changes for 1.0;
  - the preset format: we asked horde to carry all 11 enums by name, and what goes in `basis`.
- **CPU re-measure (D-038): attempt 1 not quotable** (D-041, `traces/2026-10-29-cpu-remeasure-1.md`).
  - The machine was not idle. The two-word path read 2.08 % at 192 kHz with randomness at 100 on B++, over the 2 % slice; the native path read 1.93 %.
  - D-039 costs about x1.02, from an A/B against e1dbd3c.

## Verify
`./verify fast` is **green** at this close (exit 0). The private-name check is skipped in worktrees; this session's new files were checked by hand against the main checkout's list.

## Next first move (the human's pick)
**The idle CPU re-measure.**
1. Before starting, the human restarts the Mac, lets charging finish and opens nothing else (Samsung Magician included).
2. Check that `uptime` load is under 1.
3. Run `bash tools/core_bench.sh` (full, Release).
4. Repeat the A/B: build e1dbd3c from `git archive` into the scratchpad, then alternate `arp_bench_soft --cpu-time --reps 5 --only "WORST CASE B++ with randomness"` with the current build, three times each.
5. Compare against the trace.
   - If everything fits the slice with margin, quote one input figure covering every setting (D-038) in `docs/horde-copy-in.md`.
   - If not, put D-041's three options to the human.

The branch `m3/cpu-remeasure` holds attempt 1's trace and this close.

## Open threads
- **With horde (ball theirs):**
  - enums carried by name, and the preset `basis` (our seq 12). The factory presets (B550) wait on these.
  - Their post-1.0 two-arp plan needs nothing from us now (our seq 11).
- **The human's:**
  - listen to piece 17 (randomness) on the listening page;
  - the idle re-measure (above).
- **Remaining M3 after that:**
  - factory presets;
  - name clearance (Phase N);
  - the goldens decision;
  - the readiness notice to horde, after the presets and the CPU figure.
- **Housekeeping, with the human's OK:** two old worktrees (`randomness`, `zealous-noyce-aaac9d`).
- **Clean-room wording:** "Ableton" appears in SPEC §4.7's heading, the README and the lab's map row. `spec/` is protected, so fixing the heading needs the human's gate.

## This session's traces
- `traces/2026-10-28-manual-page.md`
- `traces/2026-10-28-manual-page-rework.md`
- `traces/2026-10-28-horde-replies.md`
- `traces/2026-10-29-cpu-remeasure-1.md`
