# m3c-dense-input — per-key ref counts and an incremental chord stage: dense host input 30-45x cheaper, output byte-identical to the M3a core; the digest's session fingerprint made sharp

- **Queue item:** T-007 (ROADMAP M3), dispatch M3c, 2026-10-25, on `m3/input-rate` (based on `m3/cpu-optimisation` HEAD `01eb4e7`, PR #23 not yet merged). Working tree uncommitted (the lead commits;
  no git operation was made, `git show` / `ls-tree` read-only excepted). Authored by an implementer dispatch from the Arp lead session, motivated by D-032 ("dense host input: optimise the two hot spots,
  then publish the input rate that fits") and by the critic's findings on `01eb4e7` (MEDIUM-1 dense input, MEDIUM-2 digest coverage, LOW-2 stack).
- **Why:** the CPU evidence of M3b assumed nearly static note input. A host that keeps sending notes cost 2.1-5 % after x4 (a chord re-struck at every 1/64 step, floods of presses on new keys), from two
  causes: `noteOn` counted distinct keys in O(refs^2) and bumped the held-set version on every press (so the chord stage rebuilt at every one), and the chord stage rebuilt whole for every sample that
  changed the held set. Both are fixed, output byte-identical, and the room that is left is measured and written down for horde's readiness notice.

## What changed

Output: **byte-identical to the M3a core (`0ef0903`)**; the committed digests were regenerated only from that commit (`bash tools/core_digest.sh regen 0ef0903`). Public interface unchanged (`core.h` private members, and
`Plants` which exists only under `ARP_PLANTS`).

1. **Bench first** (`tests/core/bench.cpp`), before any optimisation, measured on the `01eb4e7` core. New input scenarios, each family a load plus an input rate (rate 0 is the load alone): the critic's loads (32-key
   chord re-struck at every 1/64 step, off and on in one sample; 4 keys re-struck up to 8x per step with the on one sample after the off; 32 keys x 4 refs with presses on new keys at 1k / 2k / 4k / 8k / 17k
   events/s, chord stage off and on) on load A at ratchet 4 (the critic's) and, for the room it leaves, on A at ratchet 1; the settings-only "B with ratchet 4, a trigger, quantize, degrees" load (`WORST CASE B+`,
   and `B++` with 4 transposition steps); block 16 in the timing sweep; an `in/s` column; a per-family "input envelope" in the summary; `--input-seconds`. The 660 existing digest lines are unchanged (the new
   scenarios are appended and the Feeder reproduces the old inputs exactly): `bash tools/core_digest.sh regen 0ef0903` gave 759 configurations, 99 added, none altered. The heavy input scenarios digest 3 rate / block pairs
   for 1 s (`lightDigest`), which keeps `core_test` at 47 s; by default the input scenarios take 5 repetitions of 0.5 s (`--reps N` restores N of 1 s), so `./verify full` is 1 min 43 s, not 35 s.
2. **Held-key aggregates** (`core.h` "Per-key aggregates of refs_", `core.cpp` `addRef` / `refGone` / `clearRefs` / `refCountOf` / `distinctHeld`): `keyRefCnt_[128]`, `keysHeld_`, `refsOutOfRange_`, the invariant written
   next to them; `noteOn` asks them instead of scanning (a key outside 0..127, a host bug, takes an exact slow path). `eraseRefs` now bumps `refsVer_` only if it removed something (it bumped on EVERY press,
   so the chord stage's skip never held while keys arrived: a refused press cost a whole rebuild); `noteOn` skips the by-id erase when the ref was not known.
3. **Cheaper input bookkeeping around it:** `updateCycle` needs only the NUMBER of keys in H' (it copied up to 128 keys at every input sample to count them): `heldCountForArp`; `chordEndAll` skips its 128-entry walk when no
   chord note exists (it ran at every input sample while the stage was off).
4. **Incremental chord stage** (`chordIncremental`; the old body is `chordRebuild`, unchanged but for `chordMapRec` / `chordQuant` / `chordSnap` being member functions instead of lambdas, and H' being kept by key
   (`chordSet_[k]`, valid iff `chordNote_[k].used`; `chordSetN_` counts them; `heldForArp` lists them in key order)). Refs mark their key dirty (`keysDirty_`, `dirtyAll_` for Panic / reset / a key outside 0..127).
   With the signature unchanged and at most 16 dirty keys, a pass finds the held entry of each dirty key (one scan of the ref table), drops the records of keys that left, refreshes those whose earliest live ref moved,
   maps joiners once (ascending), recounts ONLY the chord keys those records touch (the full pass's loop over all records, restricted), and starts / stops those notes in ascending key order, so ids and event order are the
   full pass's. Any other pass (a re-map, a new signature, more than 16 keys, a plant) is the full pass. The rules the tests pin hold because the full pass is still the definition: 1 -> 0 -> 1 at one sample keeps the
   note (the sync waits for the last input of a sample; the dirty set just accumulates), the block-start rule of D-027 is about WHEN a sync runs, not what it does, the earliest-source tie-break is the same tuple
   comparison, a settings / scale change changes the signature, and the `refsVer_` / signature skip is ahead of both. `chordRebuild` is its own function so `chordSync` does not hold its ~5 KB of arrays while the incremental
   pass runs.
5. **MEDIUM-2** (`harness.h`): `sessionFingerprint` calls `jitter` and `tweak` at EVERY position 0..total (only when the session has one; the old fingerprint called them at 7 positions and is kept as `sampledOnly`, the must-fail
   control). The three G5 band-edge sessions now have three fingerprints: the digest records 16 more wide replays (42,210 -> 42,226 runs, regenerated from `0ef0903`). `test_digest.cpp`: the committed control: the 960 / 961 / -961
   tick sessions merged by the sampled fingerprint give two different outputs under one key and the conflict check names it; the real fingerprint separates them; and the check over the whole suite (**no two runs with
   the same fingerprint and block size differ in output: 7,382 runs of 6,135 distinct sessions**, no false positive).
6. **New checks** (`tests/core/test_input.{h,cpp}`, 7; `test_digest.cpp`, 3; 441 -> 451 per int128 path): the counters equal a recount after every block of 600 drawn sessions (plant: a counter off by one is named);
   the incremental pass equals plant `chordFullPass` (the reference mode: every pass rebuilds) byte for byte on 600 drawn sessions of churning input (big chords, several refs per key, off+on at one sample, id-less refs,
   latch, pedal, chord settings changed mid-session, keys outside 0..127, Panic, reset); two faults planted in the incremental pass are caught (a stale record: 10 of 600 sessions; notes started in descending key order: 524 of 600).
   The file is a template on the core type and does nothing against the M3a core (which has neither the counters nor the plants), so the digest can still be generated from it; it records nothing in the digest.
7. **LOW-2:** `docs/horde-copy-in.md` "Stack on the audio thread": the deepest chain of frames is about 16 KB (`evalStep` 9.8 + `notesFor` 4.2 + `process` 1.8), `sortSegment`'s `Key keys[512]` is 12 KB. Not changed (the brief: note it).

## The proof that nothing moved

- Suite digest (42,226 runs: 7,382 the suite's, 32,844 wide replays, 2,000 seeded) and bench digest (759 configurations) both generated from `0ef0903`, both int128 paths: **match** after each step (after the counters, after `updateCycle` /
  `chordEndAll`, after the incremental pass, at the end).
- Coverage of the new path (llvm-cov over the suite, scratch): `chordIncremental` was entered 17.5k times; 10.2k completed (leave, refresh, join and the recount / start / stop all executed); the rest fell back.
- **Differential fuzz against the M3a core (scratch, not committed):** the drawn sessions of `test_input.cpp`, 160,000 (default capacities) + 120,000 (random capacities: held 1-32, refs per key 1-4, sounding 1-128, events 8-80, pending 2-60,
  a `prepare()` mid-run), block sizes 1 / 7 / 64 / 333 / 4096: the M3a core, the new core and the new core on the two-word path agree on every stream and every counter (0 differences).

## Before / after (Release `-O3 -DNDEBUG`, Apple M3, shared machine; x4 = horde's heuristic against the 2 % slice, not min-spec verified)

Per step, native path, 48 kHz, worst block of 16-512, x4 % (single passes while building; the last column is the least disturbed of three passes):

| scenario (load A at ratchet 4) | before (01eb4e7) | step 1: counters, no spurious version bump | steps 2-4: + updateCycle, chordEndAll, incremental chord pass | final |
|---|---|---|---|---|
| 32 keys re-struck at every 1/64 step (17,000 events/s) | 2.40 | 2.21 | 2.30 | 1.95 |
| 4 keys re-struck 8x per step (17,000 events/s) | 6.73 | 5.98 | 3.54 | 3.28 |
| new-key flood 4,000/s, chord stage off | 5.07 | 2.40 | 0.73 | 0.62 |
| new-key flood 17,000/s, chord stage off | 19.64 | 8.54 | 1.09 | 0.89 |
| new-key flood 4,000/s, chord stage on | 6.75 | 2.79 | 1.85 | 1.68 |
| new-key flood 17,000/s, chord stage on | 20.19 | 3.89 | 2.19 | 2.02 |
| the load alone (chord stage on, no input) | 1.71 | 1.83 | 1.79 | 1.62 |

(Steps 2-4 were measured together; the step-2 run was disturbed by builds. The "load alone" row is the baseline, 1.6-1.8 %: A at ratchet 4 is 264,000 output events/s.)

Settings alone, three interleaved passes of 15 repetitions, least disturbed per configuration, worst of 44.1 / 48 / 96 / 192 kHz and blocks 16-512, x4 %: native before -> after / two-word before -> after:
the 112-note ratchet-4 case 1.53 -> 1.46 / 1.63 -> 1.59 (the heaviest anywhere); A 1.28 -> 1.20 / 1.37 -> 1.27; B 0.86 -> 0.85 / 0.92 -> 0.91; B+ 0.91 -> 0.92 / 1.02 -> 1.00; B++ 0.93 -> 0.90 / 1.01 -> 0.98; 700 configurations
per path, **none over the slice before or after**; no configuration more than 5 % slower (182 / 186 compared). The common 16-key loads are 2-3x cheaper (the per-step `updateCycle` and chord-stage work): e.g. order chord 0.37 -> 0.16 % at 192 kHz.

Input, per incoming note event (ns) and the largest incoming rate within the slice (worst over 4 rates x 5 blocks x 2 paths); `~` = beyond the highest measured rate, read off the slope:

| shape | cost before -> after | on load A (x4 1.1-1.3 % alone): before -> after | on A at ratchet 4 (x4 1.5-1.8 % alone): before -> after |
|---|---|---|---|
| whole 32-key chord re-struck | 93-105 / 29-41 -> 50-58 / ~0 | ~54,000 -> none found below 17,000 measured | 3,800 -> 11,600 |
| 4 keys re-struck over and over | 530-700 -> 150-250 | 3,100 -> ~9,700 | 860 -> 3,100 |
| flood of refused presses, chord stage off | 2,480-2,690 -> 58-68 | 1,500 -> ~66,000 | 1,070 -> ~53,000 |
| the same, chord stage on | 2,730-2,850 -> 60-80 | 640 -> ~24,000 | 220 -> 9,000 |

Written, with the machine, the x4 caveat and the date, in `docs/horde-copy-in.md` "CPU envelope". Allocations inside `process()` / `reset()` over every timed loop: 0; NoteOffs lost: 0. `sizeof(Core)` 96,928 B native (was 96,768), 92,808 two-word.

## Files touched

`core/include/arp/core.h`, `core/src/core.cpp`, `docs/horde-copy-in.md` (the sha256 table, `<bit>` in the header list, the stack note, the CPU envelope, the dates), `tests/core/bench.cpp`, `tests/core/harness.h`
(fingerprint, `DigestEntry::fp`, two `setPlant` names), `tests/core/test_digest.cpp` (fingerprint control and check), `tests/core/test_core.cpp` (one include, one call), `tests/core/CMakeLists.txt` (one source),
`tests/core/stream_digest.txt` and `tests/core/stream_digest_bench.txt` (regenerated from `0ef0903`), `tools/core_digest.sh` (one source added to the list), `tools/core_test.sh` (the bench-digest message only: no check
changed), new `tests/core/test_input.h`, `tests/core/test_input.cpp`, this trace. Not touched: `spec/`, `verify`, `.kit/`, ROADMAP, DECISIONS, README, CODEMAP, `lab/`, `.github/`.

## Evidence consulted

`CLAUDE.md` §Domain; DECISIONS D-030, D-031, D-032; `traces/2026-10-24-m3b-cpu-optimisation.md` (with "Rework"); `core/include/arp/core.h`, `core/src/core.cpp`, `tests/core/{harness.h,test_digest.cpp,digest_corpus.cpp,bench.cpp,test_evict.cpp}`,
`tools/{core_digest,core_test,core_bench}.sh`, `docs/horde-copy-in.md`; `sample(1)` profiles of the re-struck-chord and the 4-key load before and after (before: `emitOn`, `noteOn`, `releaseSlot`, `chordSync` + `collectHeld`;
after: the floor is the chord notes' own start / stop, `emitOn`, `releaseSlot`, `sortSegment`).

## Alternatives rejected

- Counting per-key latched refs and gesture state (`noActive`, `hadLatched`) incrementally too: those are O(refs) scans that cost about 0.1 us at 128 refs; the profile put the cost elsewhere, and each counter is one more invariant.
- A dense `chordSet_` kept sorted by insertion / erasure: the by-key table with a used flag needs no shifting, and `heldForArp` lists it in key order (the 128-entry flag scan measured no slower on loads A and B in an alternating A/B run).
- Incremental update of the source counts and earliest source by subtracting a leaver's contribution: the earliest source is not decrementable. Recounting the touched keys from all records (about 224 pairs) is simple, obviously equal to the full pass, and cheap.
- Raising the 16-key limit of the incremental pass or making the full pass fast: a pass for more than 16 changed keys is proportional to the held set anyway, and the 32-key whole-chord case costs 50-58 ns an event now.
- Hashing the harness `Rec` instead of raw events, capping events per block: already rejected (D-030).

## Verify (base git `01eb4e7`, tree uncommitted; `.harness/last-verify.json`: tree `0c9f6e97...`)

```
bash tools/core_test.sh                       exit 0   (47 s)  OK: 451 passed, 0 failed (x2: native, two-word); bench digest 759 configurations (x2) match
python3 tools/core_params_check.py            exit 0
python3 tools/core_params_check.py --plant    exit 1   (red, as designed)
node tools/lab_smoke.mjs                      exit 0   lab_smoke: OK (1826 ms)
bash tools/core_sanitize.sh ubsan             exit 0   OK: 451 passed, 0 failed (x2); PASS  core_sanitize(ubsan): every binary green, no sanitizer report   (63 s)
./verify fast                                 exit 0   (50 s)
./verify full                                 exit 0   (1 min 43 s; includes the bench report)
```
Also built with `-Wall -Wextra -Wshadow -Wmisleading-indentation -Werror` (clang) over `core.cpp`, `test_input.cpp`, `test_digest.cpp`, `test_core.cpp` and `bench.cpp`: clean. g++ is not installed here: not run. ASan / TSan not run (they cannot start on this machine).

## Open questions

- **The critic's 1.84 % for "B with ratchet 4, a trigger, quantize and degrees" is not reproduced:** my reading of that load (`WORST CASE B+` / `B++`) measures 0.8-1.0 % after x4. Which settings did the critic use? If theirs was a heavier load than the 112-note ratchet-4 case (1.46 / 1.59 %), it should become a scenario.
- **What to quote.** The envelope depends on the rest of the module's cost. I wrote the conservative reading into the doc (3,000 events/s over the heaviest load measured, 9,000 over load A, with the formula), but the number horde's notice
  quotes is the lead's call.
- **`./verify full` takes 1 min 43 s now (35 s before)** and `core_test` 47 s (33 s): the bench grew from 33 to 66 scenarios and the digest from 660 to 759 configurations. The input scenarios already take fewer repetitions by default.
- **Machine noise:** other work ran on the machine during measurement (load average up to 23); a single pass of the full bench read up to 1.7x high. The tables use the least disturbed of three interleaved passes; read them to 5-10 %, not to the digit.
- **g++ -Werror is unverified here.** The new arrays are all initialised (no `-Wmaybe-uninitialized` candidates); the digest corpus lesson (one statement per line) was followed.
- Remaining cost of dense input is the chord notes' own start / stop (about 50 ns each) and the same-offset event sort; both are M3b territory. `ignoreRef` shifts 256 entries when the ignored ring is full (a host that never releases refused
  presses); not measured, not changed.
- README / CODEMAP list the tests; `test_input.{h,cpp}` are new (out of scope here).
