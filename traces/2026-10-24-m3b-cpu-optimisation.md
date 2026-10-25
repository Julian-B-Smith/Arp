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

## Rework (2026-10-24, same queue item; base `ebb0c58`, branch `m3/cpu-optimisation`, PR #23)

Brief: CI red on Linux/g++ (the stream digest), HIGH-1 (the bench's "worst case" is not the worst case), MEDIUM-1 (a mutant of the eviction test passes everything),
LOW-1..4. No git operation was made; everything is uncommitted.

### 0. Why the digest was red on Linux: the run SET depended on the compiler (found by reading the CI log with `gh`, then the code)
The log: `the suite made 23295 recorded runs in 728 chunks, the digest has 729`, first differing chunk 90 (runs 2880-2911). Locally runs 2883, 2892, 2901 and 2910 are four 14 000-sample
sessions at block 128 from `test_m1b.cpp`'s `badInact`: `inact(...) != inact(...) || inact(...) != inact(...)`. **The order in which the two operands of a comparison (and the arguments of a call)
are evaluated is the compiler's choice: clang goes left to right, g++ right to left.** The harness recorded runs in the order the suite made them, so the same suite produced a different
SEQUENCE on Linux, and the replay set differed too: "one replay per distinct OUTPUT" kept the replays of whichever of two equal-output sessions ran first (and skipped the block that first
run used), so a different order replayed different sessions. Two more instances were in the tests themselves and change the SESSIONS, not just their order: the seeded M1b session drew
`rnd()` four times inside one `make_shared<SimHost>(...)` argument list, three times inside `K(...)`, and twice in `emplace_back(...)`, and M2a's `scaleAt` did the same, so "a seed means the
same session everywhere" was false (the header said so, wrongly).
Fixes, none of them an exclusion of the core's behaviour:
- **The draws are statements** (`test_m1b.cpp` x3 sites, `test_m2a.cpp` x1), in the order clang drew them, so the sessions on macOS are exactly the ones the digest was generated from.
- **The replay set is a function of the session alone**: `sessionFingerprint()` hashes what a session IS (settings, every input event, the settings changes applied to a copy, the jitter /
  tweak closures sampled at fixed positions, host ops, caps, plants, scale, user patterns), the replay runs once per fingerprint, and it replays EVERY block size of the matrix including the one
  the test used (before, the test's own block was skipped, so the set depended on which run came first).
- **The runs are compared as a SET** (sorted by every column before they are chunked): the order a suite runs in is not the core's behaviour. A run that exists but produced a different
  stream, a missing run and an extra run are all still named. (The dump `ARP_DIGEST_DUMP` is sorted the same way.)
- **Named exclusions, justified here and in `harness.h`:** sessions with plant `floatPosition` (float32 position accumulation: a fused multiply-add on arm64 and none on x86-64 can round
  it differently, so they pin a platform) and with plant `timeInDraw` (it mixes a process-wide counter into the draw ON PURPOSE, so a run's output depends on how many such runs came before it:
  the suite's order, not the session). Both keep their detectors and plants in the suite's checks; neither is shipped behaviour. No other exclusion.
Proxies run locally for what a g++ run could differ in (g++ is not installed here; `c++` is Apple clang): `-ftrivial-auto-var-init=pattern` and `=zero`, native and two-word int128, and
`-ffp-contract=off` / `=fast` (the FMA question): all 441 checks green with the digest identical. **Not verified: an actual Linux/g++ run.** If CI is still red, the first line to read is
the `digest: runs A..B differ` lines (chunks are over the sorted set; `ARP_DIGEST_DUMP=<file>` writes the sorted per-run list to diff against a macOS dump).

### 1. HIGH-1: loads A and B, and what the profile said
`bench.cpp` gained the critic's two loads, named `WORST CASE A` / `WORST CASE B` (so the summary treats all three as deliberate worst cases and still counts them in "heaviest overall"),
a channel per held key (`Scenario::channels`), 192 kHz in the rate sweep, and a summary line `configurations over the 2 % slice after x4: N of M` per rate. A: 32 keys 0,4..124 on 16 channels,
chord stage +1/+2/+3 (128 distinct notes), 8 transposition steps of +1, order chord, ratchet 1, gate 200 and user-step gate 200, 1/64 at 999 bpm; B: the same on one channel, no transposition.
Measured on the `ebb0c58` core: A 0.98-1.01 % raw (x4 3.9-4.1 %), 148-154 ns per event, 66 000 events/s, 110 000 evictions per 10 s; B 0.49-0.52 % (x4 1.95-2.09 %), 72-76 ns per event.
`sample` on A: `emitOn` 1943 (the eviction victim search over 368 slots), `sortEvents` 1401, `schedErase` 678, of about 4 300. What was found and done (each measured, digest identical after each):
1. **`sortEvents` was quadratic** (the critic's attribution missed it; it is 27 % of A and most of B): every eviction or retrigger puts the victim's NoteOff right before the NoteOn that caused it,
   `[off on off on ...]` at one offset, and the insertion sort moved each off past all the ons before it (80-byte events, 128 pairs per step). Now one pass finds each run of equal offsets and whether it
   is out of order; a short run is insertion-sorted, a long one (<= 512 events) is sorted through a 24-byte key per event (kind, then id / step+key / lane, then position as the last key, so ties keep
   emission order) and moved into place by following the permutation's cycles; longer runs, and any offset that goes DOWN (a plant), take the old insertion sort. Same unique stable order.
   A 1.00 -> 0.41 %, B 0.51 -> 0.37 % (with the next two: A 0.28 %, B 0.18 %).
2. **The eviction victim search** (the critic's attribution, confirmed): the live arp notes are now a doubly linked list in id order (`liveHead_`, `liveNext` / `livePrev`, kept by alloc / free / dying);
   the victim is the first one whose on is in an earlier sample, almost always the head. The `sharedSoundingTable` plant (chord notes count) keeps the table scan.
3. **`schedErase`**: removing the front (a due off, the oldest note retriggered or evicted) is O(1); any other erase closes the gap from the nearer end of the ring instead of always the tail.
   (The critic's lazy deletion was not needed: the cost was the shift of ~80 entries, not the search.)
`Sounding` stays 64 bytes (`quickT` is an int32, the two list links are int16); `sizeof(Core)` 96 768 B native (was 96 752), 92 648 B two-word.

Before (`ebb0c58`) / after, worst block size of 32/64/128/512, medians of 15 repetitions on a shared machine (spread of the "after" medians in brackets); `x4` = horde's heuristic:

| load | path | rate | before raw % / x4 % | after raw % / x4 % | ns per event |
|---|---|---|---|---|---|
| A (evictions) | native | 44.1 / 48 / 96 / 192 kHz | 0.980 / 3.92, 0.977 / 3.91, 0.979 / 3.91, 0.987 / 3.95 | 0.274 / 1.10, 0.277 / 1.11, 0.278 / 1.11, 0.285 / 1.14 (up to 58 % at 192k) | 148 -> 41-43 |
| A | two-word | same | 1.001 / 4.00, 1.001 / 4.00, 1.003 / 4.01, 1.015 / 4.06 | 0.298 / 1.19, 0.304 / 1.21, 0.301 / 1.20, 0.307 / 1.23 | 151 -> 45-47 |
| B (retriggers) | native | same | 0.488 / 1.95, 0.488 / 1.95, 0.490 / 1.96, 0.496 / 1.99 | 0.179 / 0.71, 0.179 / 0.72, 0.181 / 0.72, 0.188 / 0.75 | 72 -> 26-28 |
| B | two-word | same | 0.510 / 2.04, 0.509 / 2.04, 0.515 / 2.06, 0.521 / 2.09 | 0.190 / 0.76, 0.189 / 0.75, 0.194 / 0.78, 0.202 / 0.81 | 75 -> 28-30 |
| the old worst case (112 notes, ratchet 4) | native | same | 0.309 / 1.24, 0.311 / 1.24, 0.326 / 1.30, 0.321 / 1.29 | 0.315 / 1.26, 0.317 / 1.27, 0.326 / 1.31, 0.331 / 1.32 | 15 -> 14-16 |
| the old worst case | two-word | same | 0.339 / 1.36, 0.339 / 1.36, 0.350 / 1.40, 0.354 / 1.42 | 0.346 / 1.38, 0.344 / 1.38, 0.352 / 1.41, 0.360 / 1.44 | 15-17 -> same |

**The target holds for every scenario, A and B included: `configurations over the 2 % slice after x4: 0 of 132` at each of 44.1 / 48 / 96 / 192 kHz, on both int128 paths** (33 scenarios x
4 blocks 32/64/128/512). The heaviest scenario anywhere is still the old worst case, 0.331 % / x4 1.32 % native and 0.360 % / x4 1.44 % two-word at 192 kHz; the heaviest of A and B is A at
x4 1.14 % native / 1.23 % two-word. Margin to the slice: 1.4x (two-word, old worst case) to 1.8x (A native). The old worst case is about 2-3 % slower than before (the live-list upkeep per note and a
second pass in `sortEvents`: 0.308 -> 0.319 % at 48 kHz / 128 in an alternating A/B run); no other scenario is more than noise slower (the `cmp` of all 33 scenarios x 16 configurations: none > 10 %).

### 2. MEDIUM-1: the eviction boundary is pinned, and the digest now has teeth for it
- **(a) Known answer** (`tests/core/test_evict.cpp`, 4 checks): drives `emitOn` directly (the `private` -> `public` access the bench already uses) on a table of one: the old note born in the SAME sample
  is not evictable (the new on is refused), born ONE sample earlier is (`on60@5 off60@6 on62@6`), born two earlier is. **Plant `evictBornPrevSample`** (new in `core.h`, `ARP_PLANTS` only) is the critic's
  mutant (`onAbs + 1 > now - 1`): it leaves the one-sample-old note alive and refuses the new on, and the check reads red on it. The public inputs cannot place two arp ons one sample apart on a
  fixed-rate grid (the finest step is 30 samples at 8 kHz / 999 bpm), which is why no suite session ever hit the boundary.
- **(b) 2 000 seeded random sessions in the digest** (`tests/core/digest_corpus.{h,cpp}`, kind `F`, about 1 s): tiny `Caps` (sounding 1-6, pending 1-10, events 2-40 or the default, held 1-4, refs per
  key 1-4), 16 channels, id-less refs and a small set of repeating ids, a block size that changes per block (the standard sizes and random 1-4096), rates 8 kHz-768 kHz, `reset()` and `prepare()` mid-session,
  stops / starts / jumps / loop wraps / tempo changes (incl. 5 and 2000 bpm, clamped), time-signature changes, `hasRep` and the exact quarter-beat `beats` fallback, mid-session settings changes, the chord
  stage, quantize, time mode, restart modes, repeats, user patterns with ratchet / tie / extreme-gate / trigger steps and a mid-session edit, and a "stutter" motif (presses on consecutive samples, each released
  at once, mostly on a stopped transport): on the free clock each press restarts the pattern at its own sample, which is what finally puts two arp ons exactly one sample apart. One generator (mulberry32),
  every draw a statement. **The mutant now fails the digest in 5 of the 2 000 sessions** (and nowhere else): before the stutter motif it failed in none; scratch build with the plant forced on.
- **Reference:** `bash tools/core_digest.sh regen 0ef0903` (the M3a core read out of git, unchanged) generated both files BEFORE any of the rework's optimisation, with the plant-aware harness (`setPlant` is a
  template that skips a plant the core does not have, so the suite builds against the M3a core). The `ebb0c58` core and then the reworked one both match them: 42 210 runs (7 382 the suite's own, 32 828 wide replays,
  2 000 seeded) in 1 320 chunk hashes, and 660 bench configurations (33 scenarios x 4 rates x 5 blocks, incl. A and B). Suite time 6.5 s -> 7.5 s per binary.

### 3. LOW items
- **LOW-1** `firstAbs` / `firstSlot` / `firstSrc` in `chordSync` are zero-initialised. No digest entry changed (the reference is the unmodified M3a core, and the fixed core matches it), so no separate regeneration was needed.
- **LOW-2** the `std::reverse` in the T2 retrigger stays, with a comment: `nv > 1` needs two live notes on one (channel, key), which only `t2AcrossStreams` allows, and the order of releases is observable through
  `dropRelease` / `doubleRelease`; in the shipped build it is a no-op. The comments that presented the `refsVer_` bumps as needed everywhere are corrected and the redundant bumps are gone (noteOff / pedal state changes and
  `resetState`: a state change leaves H alone, and `resetState` clears `chordSigValid_`); the bumps that remain are the ones that change what H is made of (a ref joins, `eraseRefs`, Panic). The `chordSync` skip's reliance on
  an idempotent pass (a refused `chordStart` is never retried) is documented where it is taken.
- **LOW-3** `offDueBy` asserts `inc_ > 0`, and `I128::floorDiv` / `ceilDiv` assert `d > 0` (both int128 classes), with the reason beside each. The suite also ran once with asserts on (`-O1`, no `NDEBUG`) across all 441 checks and the digest: clean.
  This adds `<cassert>` to the headers the core uses: `docs/horde-copy-in.md`'s build-notes list does not name it (only the table was regenerated, per the brief).
- **LOW-4** `docs/horde-copy-in.md`: only the sha256 table regenerated (`core.h`, `int128.h`, `core.cpp`).

### Verify (rework; base `ebb0c58`, tree uncommitted)
```
bash tools/core_test.sh                       exit 0   (33 s)  OK: 441 passed, 0 failed (x2: native, two-word) = 430 + 7 digest + 4 eviction checks per path
python3 tools/core_params_check.py            exit 0
python3 tools/core_params_check.py --plant    exit 1   (red, as designed)
node tools/lab_smoke.mjs                      exit 0   lab_smoke: OK (1818 ms)
bash tools/core_sanitize.sh ubsan             exit 0   OK: 441 passed, 0 failed (x2); PASS  core_sanitize(ubsan): every binary green, no sanitizer report   (58 s)
./verify fast                                 exit 0
./verify full                                 exit 0
```
