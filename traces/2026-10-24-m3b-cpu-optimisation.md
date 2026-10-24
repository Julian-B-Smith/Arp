# m3b-cpu-optimisation — the sounding table and pending queue indexed, a chord-stage pass skipped when idle: the capacity-maximal case 11.5 % -> 0.33 % of a core, output byte-identical

- **Queue item:** T-007 (ROADMAP M3, horde admission), dispatch M3b, 2026-10-24, on `m3/admission`. Base `0ef0903`; working tree uncommitted (the lead commits; no git
  operation was made, `git show` / `rev-parse` / `ls-tree` read-only excepted). Authored by an implementer dispatch from the Arp lead session, motivated by D-030
  ("optimise the worst case into the CPU budget, byte-identical").
- **Why:** D-030: horde's CPU slice is 2 % of a min-spec core (x4 derate), the M3a benchmark's capacity-maximal case did not fit, and the human chose to optimise rather
  than report or cap. This change makes it fit (x4 about 1.3-1.5 % on both int128 paths against the 2 % slice) without changing one output byte, and adds the independent
  reference that proves it: a committed event-stream digest generated from the M3a core.

## A finding the lead must weigh first: the M3a benchmark never installed its user patterns

`tests/core/bench.cpp` (M3a) built a `UserPattern` for three scenarios ("ratchet 4 on every step", "triggers on lanes 1-8", and the WORST CASE: `ratchet 4`, gate 200) and
stored it in `Scenario::user`, but `measure()` never called `Core::setUserPattern`. With `pattern = user` the core played the default user pattern (8 plain steps, ratchet 1,
no triggers). So the "WORST CASE ... ratchet 4" of the M3a trace ran at ratchet 1: **59 700 events/s, 2.7-3.2 % raw**. With the pattern installed (fixed in this change, 1 line
in `measure()` and 1 in the new digest runner) the same scenario is **238 700 events/s, and the M3a core costs 11.3-13.1 % of a core raw, 45-52 % after x4** (476-548 ns per event),
four times what M3a and D-030's text ("about 11-12 % after x4") recorded. Every number below is measured with the fixed bench, before and after, back to back; the M3a numbers are
not comparable. (The M3a trace is append-only and is left as it is; this entry corrects it.)

## What changed

Output, from the first line to the last: **byte-identical** to the M3a core (proof below). The core's public interface is unchanged (`core.h` private members only).

1. **Sounding table: slots + indexes instead of a dense, linearly scanned, shifted list** (`core.h` members, `core.cpp` "the sounding table" / "the off schedule").
   Every `process()` iteration scanned all notes for the next off, `emitOn` scanned them twice (retrigger victims, the T11 live count), `release` scanned for the ref and then shifted the
   tail down (80-byte entries), and `releaseAll` snapshotted them. Now a note owns a slot for its whole life; a per-key chain finds it by (key, id); `liveN_` / `liveChordN_` count
   T11's "live" notes; the due offs live in `sched_` (a ring of slot indices sorted by `offPos`, front = soonest) plus `quick_` (the few notes whose off is not later than their own on
   sample, or that are dying: SPEC 1.1's "never at the sample of its own on" makes their due offset a per-block number, not an order of `offPos`; they move to `sched_` at the next
   block start). Offs due at one offset go out together in ascending output id (`takeDueOffs`), which is what the one-off-per-iteration scan of the dense table did. The comment block in
   `core.h` states every invariant. *Effect (true worst case, 48 kHz / block 32, native):* 11.5 % -> 0.78 % raw.
2. **Pending queue: a ring sorted by `pos`, equal positions in queue order** (`queueOn` files from the back, `popPending` takes the front). It replaces "scan for the first smallest
   `pos`, then shift the tail down" (64-byte entries, up to 448 queued). Same entry at every pick, so the same emission order (and so the same ids).
3. **`I128` division fast path** (`int128.h`, Native only): a dividend that fits an int64 (a position difference; an absolute tick times D for hours of music) divides in hardware instead
   of through the compiler's `__divti3`. Identical rounding; the wide case is unchanged and the Native/Soft cross-checks in `test_int128.cpp` still pass. *48 kHz / 32: 0.78 -> 0.51 %.*
4. **`chordSync` skips a pass that cannot change anything** (`refsVer_`, `chordSyncVer_`): the stage re-ran at every block start and every grid step even with the held keys and settings
   untouched (about 1 us each, 37 % of the profile at 96 kHz / 32). `refsVer_` moves at every write to `refs_`; the pass is skipped only when the signature is unchanged, the stage had
   completed a pass at this version, and no dedupe plant (which retries a refused second on at every pass) is on. *96 kHz / 32: 0.62 -> 0.42 %.*
5. **`ofs()` memos** for the three positions the timeline asks about at every pick (pending front, soonest off, next grid step; keyed by the position, dropped at the block start and in
   `freeStart`), **the `evalStep` output de-duplication made O(n)** (a key -> index table instead of a linear search per key: quadratic for a 112-key chord step), `sortEvents`
   not copying an event that is already in place, and `releaseSlot` not calling `unlinkOff` for an off already taken. *96 kHz / 32: 0.42 -> 0.31 %.*
6. Housekeeping: `blk_` (only incremented once the per-entry `ofsCache` was gone) removed; `Sounding` is 64 B (was 80), `sizeof(Core)` 96 752 B native (was 98 752), 92 640 B soft.
7. **`tests/core/bench.cpp`**: installs `Scenario::user` (above), and a `--digest` / `--digest-perturb` mode (the stream digest of the shipped core over the 31 scenarios x 4 rates x 5 block
   sizes, 2 s each; no timing, so it is meaningful on a loaded machine).

## The reference: a byte-identical proof that is independent of the code under test

- **`tests/core/stream_digest.h`** (hasher), **`tests/core/test_digest.{h,cpp}`** (the check), `harness.h` (`run()` records), **`tests/core/stream_digest.txt`** and
  **`tests/core/stream_digest_bench.txt`** (the committed reference), **`tools/core_digest.sh`** (`regen [REV]`), `tools/core_test.sh` (ADDED the bench-digest check; nothing weakened).
- What is hashed per run (FNV-1a 64 over explicit little-endian words, never struct memory): **every field of every output event** (raw, before the harness normaliser can drop one:
  `data[1..2]`, `down`, `offset` are in), every step record, and the final counters (kept as a second hash: some count blocks). The suite's digest covers **every `run()` of the whole
  suite (7 382 runs: N1/G1/G4/M1b/M2/M3 sessions, the 30 M1b seeded sessions, the feature fuzz, all plant sessions) plus one wide replay of each distinct session (15 919 replays): block
  sizes 1/7/64/512/4096 at the session's own rate (block 1 only for sessions up to 12 000 samples: it was 10 of the 14 s of the first, unrestricted version) and 44.1/48/96/192 kHz at
  block 512**, 23 301 runs in 729 chunk hashes. The bench digest covers 620 configurations. Both int128 paths must match the one file.
- **Independence:** both files were produced by `tools/core_digest.sh regen 0ef0903`, which reads the M3a core out of git (`git show`, read-only) into a scratch directory and builds
  the suite and the bench from there, before the optimised core existed in that form; the file headers record the sha256 of that `core.cpp` / `core.h`
  (`6bdfd594...`, `1b887395...`). `bash tools/core_digest.sh regen 0ef0903` reproduces them from any checkout. The digest files must be committed with (or before) the core change.
- **Planted controls, each shown red:** a hasher blind to one field is named by the field-perturbation check; all 16 event fields, 6 step-record fields and 32 counters are shown to move
  the hash; plant `untaggedOn` (one field of some events) changes a real run's digest; a digest chunk with one bit flipped is named by run range and a digest missing its last chunk is
  caught; `arp_bench --digest-perturb` (one field of one event) differs from the committed file in 600 of 620 configurations on each int128 path. **End to end, a deliberate bug in a copy
  of the new core** (`queueOn` letting an equal-position entry jump the queue, which only renumbers output ids) turned the suite's digest red in 129 of 729 chunks and the bench digest red
  in 140 of 620 configurations (scratch build, not committed).
- **Extra evidence, not committed:** a differential fuzz (scratch, `fuzz.cpp` over the suite's harness): 100 000 random sessions x 2 block sizes (all 16 orders and 12 patterns, user
  patterns with ratchets / ties / triggers, time mode, latch, pedal, panic, restart modes, repeats, quantize, chord stage, transport stops / seeks / tempo changes, jitter, reset and re-prepare
  mid-run, tiny `Caps` for the sounding / pending / event / held limits, random plants incl. `dropRelease`, `t11EvictsFirst`, `sameSampleOff`, `roundNotCeil`, `chordNoDedupe`), 18.6 million
  events: the M3a core and the new core agree on every stream and every counter; 20 000 more on the two-word int128 path agree too. (The `clampOffset` plant is excluded: it
  loops forever on both cores for a note born in a block's last sample, by design of the plant.)
- Coverage (llvm-cov over the suite, measured on an intermediate version): `core.cpp` is 97.6 % line-covered by the suite alone; of the new code the suite does not reach the plant-only
  fallback in `takeDueOffs`, the queue compaction of the time-mode spill drop and `freeStart`'s shift of queued entries (the first is plant-only; the fuzz reaches the other two).

## Before / after (Release `-O3 -DNDEBUG`, Apple M3, shared machine, 15 repetitions, medians; fixed bench; `x4` = horde's min-spec heuristic against the 2 % slice)

True worst case (112 chord-stage notes, ratchet 4, gate 200, 1/64 at 999 bpm; 238 700 events/s), worst block size of 32/64/128/512 per rate:

| path | rate | before raw % | before x4 % | after raw % | after x4 % | ns/event before -> after |
|---|---|---|---|---|---|---|
| native | 48 kHz | 11.537 | 46.148 | 0.330 | 1.322 | 476 -> 14 |
| native | 44.1 kHz | 11.485 | 45.940 | 0.327 | 1.307 | 478 -> 14 |
| native | 96 kHz | 11.762 | 47.046 | 0.338 | 1.352 | 474 -> 14 |
| two-word | 48 kHz | 12.859 | 51.435 | 0.368 | 1.470 | 534 -> 15 |
| two-word | 44.1 kHz | 12.852 | 51.407 | 0.381 | 1.525 | 536 -> 16 |
| two-word | 96 kHz | 13.079 | 52.314 | 0.376 | 1.506 | 534 -> 16 |

Heaviest of the other 30 scenarios, per rate (block 32 or the stated one): native 48 kHz 0.128 % (x4 0.512 %, heavy but playable: order chord + chord stage, 6 shifts) -> 0.027 %
(x4 0.110 %, order chord, 16 keys, 1/64 at 999); 44.1 kHz 0.118 -> 0.027 %; 96 kHz 0.245 (x4 0.980) -> 0.037 % (x4 0.147). Two-word: 48 kHz 0.115 -> 0.029 %, 44.1 kHz 0.107 ->
0.028 %, 96 kHz 0.221 -> 0.039 %. No scenario got slower beyond noise at any rate / block / path (the largest per-scenario ratios of worst configuration: heavy-but-playable 34x, chord stage + key churn 28x, chord stage
with 6 shifts 18x, `stride` / `chord_between` 2.4x, the plain 16-key `up` family 1.2-1.3x); **no configuration is more than 10 % slower than before**. Full tables: `bash tools/core_bench.sh`.
Allocations inside `process()` / `reset()` over every timed loop: 0; NoteOffs lost: 0. Spread of the "after" medians is about 12 % (the figures are small and the machine was shared):
read the margin, not the third digit. **Target (<= 2 % after x4, <= 0.5 % raw): met on both paths at every sample rate and block size, with a margin of 1.3x-1.5x.**
At block 512 the true worst case saturates the SPEC 1.3 event buffer (`max(1024, 4 x maxBlock)` = 2048 events) on both cores (879 552 ons refused over a timed run, the same on both): there the load
is capped by the buffer, not by CPU.

## Files touched

`core/include/arp/core.h`, `core/include/arp/int128.h`, `core/src/core.cpp`, `docs/horde-copy-in.md` (the sha256 table only: `core.h`, `int128.h`, `core.cpp` changed),
`tests/core/CMakeLists.txt`, `tests/core/harness.h`, `tests/core/test_core.cpp` (one include and one call in `main`), `tests/core/bench.cpp`, `tools/core_test.sh` (ADDED the bench-digest
check and a header comment), new: `tests/core/stream_digest.h`, `tests/core/test_digest.h`, `tests/core/test_digest.cpp`, `tests/core/stream_digest.txt`,
`tests/core/stream_digest_bench.txt`, `tools/core_digest.sh`, this trace. Not touched: `spec/`, `verify`, `.kit/`, ROADMAP, DECISIONS, README, CODEMAP, `lab/`, `.github/`.

## Evidence consulted

`CLAUDE.md` §Domain; `traces/2026-10-24-m3a-admission-evidence.md` (the bench, its profile, the open questions); `DECISIONS.md` D-030; `core/src/core.cpp`, `core/include/arp/*.h`;
`tests/core/{bench,harness,test_core,test_m3}.cpp`; `tools/{core_test,core_bench,core_sanitize}.sh`; `docs/horde-copy-in.md`; `sample(1)` profiles of the worst case before and after each step
(top of stack: first `process` scans 906, `emitOn` 873, `release` 420, `sortEvents` 239 of about 2 750; then `__divti3` 30 %; then `chordSync` + `collectHeld` 37 %; then `emitOn` / `evalStep`
at the 13 ns-per-event floor); `git show 0ef0903:core/...` (the reference core).

## Alternatives rejected

- A heap for the off schedule: ties and the SPEC 1.1 clamp make the soonest off a `(due offset, id)` minimum, not an order of `offPos`; the sorted ring plus the small `quick_` set is exact
  and O(1) at the front. A lazy-deletion heap would have needed the same clamp handling plus stale-entry bookkeeping.
- Picking offs in any order among those due at one offset ("the output is sorted afterwards anyway"): true for the output, false for the `dropRelease` / `doubleRelease` plants, which hit
  "the first release"; ascending id keeps even the plants byte-identical.
- Hashing the harness's normalised `Rec` (lossy: drops `data[1..2]`, `down`, the in-block offset); the raw events are hashed instead (lesson L0002).
- Replaying block size 1 for every session in the wide matrix: 10 s of the 14 s; sessions over 12 000 samples keep blocks 7/64/512/4096 and the rate sweep.
- Keeping `floatPosition` plant sessions in the digest: that plant carries the block position in float32 and a fused multiply-add (arm64) or not (x86-64) can round it differently, so those
  sessions would pin a platform; they stay in the suite's checks, out of the digest.
- Capping events per block (D-030 already rejected it: a behaviour change needing an erratum).

## Verify (base git `0ef0903`, tree uncommitted)

```
bash tools/core_test.sh                       exit 0   (22 s; arp_tests and arp_tests_fallback: OK: 437 passed, 0 failed = 430 + 7 new digest checks per path)
python3 tools/core_params_check.py            exit 0   (4 PASS lines)
python3 tools/core_params_check.py --plant    exit 1   (red, as designed)
node tools/lab_smoke.mjs                      exit 0   lab_smoke: OK
bash tools/core_sanitize.sh ubsan             exit 0   arp_tests OK: 437 passed, 0 failed; arp_tests_fallback OK: 437 passed, 0 failed; "PASS  core_sanitize(ubsan): every binary green, no sanitizer report" (48 s)
./verify fast                                 exit 0
./verify full                                 exit 0   (includes the bench report; 35 s)
```
ASan / TSan not run (they cannot start on this machine, M1b trace); the digest check now runs inside them in the Linux CI `sanitize` job.

## Open questions

- **Cross-platform digest (unverified here):** the digests were generated on Apple clang 16 / arm64. The pipeline is integer end to end, and the one float-sensitive plant is excluded, but the
  Linux CI run is the first time another compiler and ISA will hash the same sessions. If CI reds on the digest alone, suspect a float path (host `beats` fallback, tempo rounding) before the core.
- **Record to correct:** M3a's trace and D-030's "about 11-12 % after x4" rest on a benchmark that did not install its user patterns (above); the true pre-optimisation figure is 45-52 % after x4.
  ROADMAP / DECISIONS / the horde-facing numbers are the lead's to amend; this entry is the evidence.
- **Suite time:** the digest adds about 4 s per test binary (about 2.5 s -> 6.5 s) and 48 s to the UBSan job (it replays the wide matrix under instrumentation); TSan will be slower still.
- **`docs/horde-copy-in.md`** still says "about 96 KiB"; the figure is now 94.5 KiB (native) / 90.5 KiB (two-word), still "about". Only the table was regenerated, per the brief.
- **At block sizes of 512 and up, the true worst case is limited by the SPEC 1.3 event buffer, not by CPU** (879 552 refused ons per timed run on both cores): an adapter-facing capacity fact for
  horde, not a change here.
- The 100 000-session differential fuzz is in the scratchpad only. It is cheap (about 3 minutes) and strictly broader than the committed corpus; say if you want it committed as an opt-in target.
