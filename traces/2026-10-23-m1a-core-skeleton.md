# m1a-core-skeleton — the C++ core skeleton and the seam (M1a)

- **Queue item:** T-003 (ROADMAP M1a), approved by the human by poll
  ("Proceed"), 2026-10-23. Authored by an implementer dispatch from the lead
  session of this project (D-015: horde copies the core in at our V1; D-022:
  spec RATIFIED; D-023: spec pinned).
- **Why:** M1a proves the hard parts first, on the smallest feature set: exact
  128-bit positions, the three-band host clock and the free clock, the
  once-guard and pending queue, the single release path with its caps, and
  the held set. The ratified `spec/SPEC.md` is the judge; the lab was read for
  understanding only and is not a parity target.
- **Changed (all new):**
  - `core/`: `CMakeLists.txt`; `include/arp/{int128,arith,draw,types,core}.h`;
    `src/core.cpp`. These six are the whole shippable core.
  - `tests/core/`: `CMakeLists.txt`, `check.h`, `harness.h`, `alloc_hook.{h,cpp}`,
    `test_int128.{h,cpp}`, `test_core.cpp`, `shipped_smoke.cpp`.
  - `tools/core_test.sh` (build + run), `tools/core_params_check.py` (drift guard).
- **Shape:**
  - C++20, standard library only, no dependency. `I128` is `I128Native`
    (`__int128`) where available, else `I128Soft` (two words); both classes are
    always compiled when `__int128` exists, so one test cross-checks them on
    30,000 random operand sets, and `arp_tests_fallback`
    (`-DARP_FORCE_INT128_FALLBACK`) runs the WHOLE suite on the fallback.
  - Plants live behind `ARP_PLANTS`, which only the test targets define. The
    shipped `arp_core` target is built without it, and `arp_shipped_smoke`
    links that target and checks `kPlantsCompiled == false`.
  - Every table is fixed-size; `prepare()` allocates only the event buffer and
    the step log. A1 counts `operator new` calls inside `process`/`reset`.
- **Evidence consulted:** `spec/SPEC.md` (Arithmetic conventions, §1-§5.2, §6
  only for the `lastQ` reset it does not need yet, §7, §9, §10, §11, §13/§13b),
  `spec/params.json`, `CLAUDE.md` §Domain, the lab CORE (`lab/arp-lab.html`)
  and `tools/lab_smoke.mjs` for how each invariant was tested.
- **Planted controls, each proven to fire in the suite's own output:**
  - N1: `dropRelease` (on without off), `doubleRelease` (double off),
    `sameSampleOff` (off 0 samples after its on); the row-coverage check on an
    empty set;
  - G1: `clampOffset`, `floatPosition` (block 512 differs); G2 and G2b:
    `roundNotCeil` (on at 1795, expected 1796; and against the host's own
    position function); G4: `srTickMath`; G5: `noOnceGuard`;
  - D1: `timeInDraw`; A1: `allocInProcess` (690 calls), `allocInReset`,
    `allocInSettings`; C1: `noOffReserve` (on without off);
  - held set: `noLatchToggle`, `pedalIgnored`, `latchReplaceKeeps`,
    `latchAtNextStep`;
  - int128: a truncating floor division caught by the Soft-vs-Native cross-check;
  - `core_params_check.py`: a perturbed default and a perturbed enum list are
    both reported; the same script caught a REAL temporary drift (`gate = 81`).
  - `t2AcrossStreams` is not modelled: there is one stream until the chord
    stage (M2).
- **Mutation check (my own, not committed):** 41 hand-made faults in
  `core.cpp` were compiled against the suite; 35 are caught (one of them by
  hanging the run). The 6 survivors
  are equivalent in this scope and become observable only when later work
  lands: the pending queue is always empty between iterations until swing or
  ratchets exist (`pending not cleared at release-all`, `late event not
  clamped`); the step's velocity is always 100 % (`vel round`); no trigger can
  fire yet (`idle check`); a repeated NoteOn of a toggled-out ref (`ignored
  ref`); and a rate adopted at an edge reaches the same grid through
  `switchRate` (`rate adopted at edge`).
- **Alternatives rejected:**
  - one shared `Plants` global instead of a per-instance member: the lab's
    `timeInDraw` needs state that outlives one core, which is kept as a
    function-local static in `Core::draw` under `ARP_PLANTS` only;
  - reading `swing` in M1a: the brief says leave it straight, so the formula
    is SPEC 5.1's with a constant `kStraightSwing = 500` for M2 to replace;
  - aborting on a bad block size or an out-of-window input offset (SPEC 2.3
    says "assertion failure"): the audio thread must not abort, so the core
    refuses and counts (`badInput`).
- **Verify:** `bash tools/core_test.sh` exit 0 (arp_tests 72 passed,
  arp_tests_fallback 72 passed, arp_shipped_smoke PASS);
  `python3 tools/core_params_check.py` exit 0; `python3 tools/core_params_check.py
  --plant` exit 1 (red, as it must be); `./verify fast` exit 0 at git 9de1980
  (branch `m1a/core-skeleton`, working tree uncommitted at the time of writing;
  `./verify` does not run the C++ suite yet, the lead wires it).
- **Open questions:** see the lead report; every `SPEC-GAP:` is in
  `core/src/core.cpp`, and the cross-cutting ones are:
  - `prepare()` has no output channel, so the adapter must call `reset()` first
    if it wants the offs (T10);
  - the sounding table has 16 physical slots beyond its 128, for notes released
    inside their own on sample (SPEC 1.1 vs the 128 cap): the spec is silent on
    that interaction;
  - the "ignored refs" list is a ring of 256;
  - not verified on a real g++/Linux or MSVC here (this Mac's `g++` is clang).

## Rework (same day): the independent critic's REWORK verdict, 2 HIGH + 3 MED + LOW

The critic's probes (all reproduced before any fix) and what changed. Each new
test was run against the pre-rework core first (build of the new tests against a
snapshot of the old sources): it FAILED there, and passes now.

- **HIGH-1, a re-pressed key dropped when the host gives no note ids.** The first
  suite gave every label a unique ref id, so it never saw that with id-less refs
  every press of a key carries the SAME ref. `noteOn` returned on any same-ref
  entry; `noteOff` never cleared the ignored list. Now: only a same-ref entry still
  DOWN is a duplicate; a PEDAL or LATCHED one is the previous press, replaced after
  the gesture-start test and the latch rules have seen it; an ignored ref expires
  at its NoteOff. Tests (id-less refs): latch-append toggle and re-add; latch-replace
  re-press of the same chord re-anchors; replace C+E then C+F; pedal re-press; refused
  33rd key recoverable. Pre-fix: all 5 FAIL. Plant `sameRefDropsPress` restores the
  old rule: 5 of 5 red. `keyRefs` variants of every N1 session and of G1 were added.
- **HIGH-2, re-enable with keys held did not restart the free clock.** Turning
  `enabled` / `arp_enabled` on under the free clock with H non-empty now calls
  `freeStart(0)` (epoch + 1), even if the clock was left running. Tests: stopped
  transport, latched chord, off 3000-5000, on: first note exactly at 5000, epoch + 1,
  for both settings. Pre-fix: both FAIL.
- **MED-1, T11 vs the 1-sample minimum.** The victim is now the oldest note that is
  neither released-and-waiting nor on its own on-sample; with none, the new on is
  refused (counted). Test (caps.sounding = 1, a forward re-base at 1/64): never two
  notes at once. Pre-fix: 2 at once, FAIL. Plant `t11EvictsFirst`.
- **MED-2, prepare() and T10.** Notes owed an off at `prepare()` move to storage that
  survives it (sized at construction) and get their NoteOff at offset 0 of the next
  `process()` or `reset()`. Pre-fix: stranded, FAIL. Plant `prepareForgetsNotes`.
- **MED-3, host floats.** Non-finite or absurd `beats` means no beats timeline (free
  clock); a non-finite tempo means none reported (last valid, else 120); `#error` under
  `-ffast-math`. Tests: NaN / inf / 1e300 beats, NaN / inf tempo. Pre-fix: the new test
  HANGS (undefined float cast); a UBSan build of the suite is clean now.
- **LOW:** `plantsCompiledIntoLibrary()` is a function in core.cpp and the shipped
  smoke calls it; `clampOffset` no longer disables the 1-sample deferral; `NoteRef::id`
  and `Event::stepSeq` are 64-bit; `Transport` carries `loopActive`, `loopStartBeats`,
  `loopEndBeats` (a reported wrap is a jump, tested); `heldKeyCount()` documented as
  audio-thread-only; the A1 hook covers plain, array, nothrow and over-aligned new (a
  direct malloc and syscalls cannot be hooked portably).
- **Verify:** `bash tools/core_test.sh` exit 0 (94 + 94 passed, smoke PASS);
  `python3 tools/core_params_check.py` exit 0; `./verify fast` exit 0 (git 9de1980,
  uncommitted); UBSan builds of the suite (native and fallback int128) 94 passed,
  no runtime error. AddressSanitizer cannot run on this machine at all (a trivial
  program aborts in the sanitizer's own init), so no ASan result exists.
- **Open:** `docs/spec-errata.md` E2 is now stale (prepare DOES honour T10); E3 now also
  says dying notes do not count toward the 128. Not mine to edit.

## Lead's gate record (2026-10-23)
- **The critic review** (Opus, independent): REWORK, 2 HIGH (an id-less
  re-press was dropped; the free clock did not restart on re-enable), 4 MED
  and 6 LOW. All were fixed by the implementer, each with a test that
  failed before its fix.
- **The critic re-check:** "no open HIGH". All 8 findings were closed by
  re-run probes and fuzz (900 sessions × 5 block sizes, both int128 paths,
  id-ful and id-less refs: 0 N1, 0 G1, 0 lost offs).
  - New LOW-A (an id repeat across `prepare()`) and LOW-B (a narrow
    fast-math guard) are queued as M1b's first items.
- **Spec errata** (D-024): E1 and E3 approved and amended, with a re-pin;
  E2 fixed in code.
- **`./verify fast` wiring** (`core_gate`, approved by poll) is proven on
  plants: a real core bug (release-all as a no-op), a drifted default, and
  no cmake each go red.
- **Verify:** `./verify fast` exit 0; 94/94 on both int128 paths; UBSan
  clean. ASan cannot start on this Mac (an environment fault). g++ and MSVC
  are unbuilt locally, so the PR's Linux CI run is the first g++ build.
