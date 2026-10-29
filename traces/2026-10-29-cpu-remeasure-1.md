# 2026-10-29 — D-038 re-measure, attempt 1: not idle enough; D-039's cost measured

**What changed.** Nothing in the code. This trace records a measurement and
the human's decision on what to do next.

**Why.** D-038 asks for a quiet-machine re-measure before the readiness
notice, then one input figure that covers every setting, randomness
included.

**Conditions.**
- Apple M3, 8 cores, Darwin 25.6.0, Apple clang 16.0.0.
- Release `-O3 -DNDEBUG`, shipped core, at main a90f03c.
- Not idle: a 1 h 34 min horde test process had just been stopped, but
  load averaged 1.8–2.5 throughout, Samsung Magician used about 12 %, and the
  battery was charging.

**Full report** (`bash tools/core_bench.sh`, default repetitions). Worst x4 of
the settings-only loads, over block sizes:

| | 48 kHz | 44.1 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| native: B++ with randomness 100 | 1.84 % | 1.84 % | 1.88 % | 1.93 % |
| two-word: B++ with randomness 100 | 1.95 % | 1.95 % | 2.00 % | **2.08 % (over)** |

- **Two-word, 192 kHz:** 3 of 195 settings-only configurations are over the
  2 % slice.
- **Native:** none.
- **Loads without randomness read about 11 % above 2026-10-25.**
  - B++: 1.74 % native, 1.87 % two-word, against 1.53 % / 1.69 %.
  - B+: 1.86 % two-word, against 1.68 %.
- **The 2026-10-27 randomness table** (docs/horde-copy-in.md, "estimates")
  under-reads today's figures.

**Is it D-039?** Mostly not. The pre-D-039 core (main e1dbd3c) and the
current core were built side by side and timed in alternation:
`arp_bench_soft --cpu-time --reps 5 --only "WORST CASE B++ with randomness"`,
three times each.

| | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|
| pre-D-039 | 1.92 % | 1.96 % | 2.03–2.04 % |
| current | 1.95 % | 2.00 % | 2.07–2.08 % |

- D-039 costs about **×1.015–1.020**, the same in all three runs.
- The rest of the rise from 2026-10-25 shows up in both builds, so it
  belongs to the machine or the session, not to code.

**What today's figures would mean if they hold.**
- No room under 1.8 % (D-033's 10 % margin) on either path for the heaviest
  load with randomness at 100.
- The two-word path is over 2 % at 96 kHz and above.
- No single input figure covering every setting can be quoted from this
  reading.

**The human's decision (poll, 2026-10-29): re-measure on an idle machine,
then decide.** The idle run means:
- a fresh restart;
- the battery full, or charging finished;
- no other apps, including Samsung Magician;
- load under 1 before starting.

The run itself:
- `bash tools/core_bench.sh`;
- then the same A/B against e1dbd3c, to confirm D-039's share.

If idle still reads over the slice, the options put to the human were:
- optimise the two-word randomness path;
- quote with an exception (randomness high, 96 kHz and up, two-word);
- cap the randomness cost by a spec change.

**Verify.** See the commit (`./verify fast`).
