# Arp core: the copy-in file list for horde (D-015)

> **Origin.** Arp project, M3a dispatch of ROADMAP T-007, 2026-10-24 (base commit `ff115db` plus the M3a working tree). horde's
> note-processor 1.0 bar says the core is copied into horde's tree at Arp's V1 by a horde PR that records Arp's commit (D-015,
> Q-H2 ANSWERED). This file is the contract for that copy: **exactly these files, nothing else.**
> Last verified: 2026-10-24 by `bash tools/core_test.sh` (the check described at the end).

## What horde copies

Keep the relative layout: `core/include` is the include root (`#include "arp/core.h"`), `core/src/core.cpp` is the one translation unit.

| file | sha256 |
|---|---|
| `core/include/arp/arith.h` | 702d89b13848b554a6e32392f5f48a22bbdd157e8a531270f194e0a103f37162 |
| `core/include/arp/bank_data.h` | 87876a9f7681733500c17e65463f043b1be4f5bda8a77aff5949347c3a4b3723 |
| `core/include/arp/core.h` | 1b8873952ff09f037a6475d77bf787af6b2e5e2696f6fbdadb45ccb3bfb4a41e |
| `core/include/arp/draw.h` | 8ea2c17c6eb55baa621fd37659c7f19bcb6c46b0304d59870d371eca0ca8dc92 |
| `core/include/arp/int128.h` | 1c0125596aef5df298bd3c3db1533e0d1899949100f59d937a334bcb0a254eda |
| `core/include/arp/pattern.h` | 1ad8f47861d91587aa6df2b6180d85cfc7d611fc8001769d42547cf1714e3899 |
| `core/include/arp/types.h` | 509619c7b671701d0c16b88c88d3d14e3e3dfef41b019c57b317d929153d7952 |
| `core/src/core.cpp` | 6bdfd5940d48885706de0cebb873272686f58e3a1aff9af1894be6b076407bff |

- **Pinned at commit:** the commit that contains this file. horde records `git log -1 --format=%h -- core/ docs/horde-copy-in.md`
  in its copy-in PR; the sha256 column above is the authority (it is what `tools/core_test.sh` checks), the commit is the label.
- **Not copied:** `core/CMakeLists.txt` (a convenience for building the library standalone or via `add_subdirectory`; horde brings its own
  build), everything under `tests/`, `tools/`, `lab/`, `spec/`, `intake/`, `docs/`.
- `core/include/arp/bank_data.h` is GENERATED from `spec/bank/starter.json` by `tools/gen_bank.py`; horde copies the generated header and never
  regenerates it (Arp's `tools/core_params_check.py` guards the drift on this side).

## Build notes

- **C++20**, standard library only: `<algorithm> <array> <atomic> <cmath> <cstdint> <cstring> <functional> <iterator> <numeric> <span> <vector>`.
  No third-party dependency, no thread library (the user-pattern hand-over is a lock-free `std::atomic<int>`, asserted at compile time).
- **No defines.** In particular **`ARP_PLANTS` must NOT be defined** in horde's build: it compiles in the test-only fault switches of the suite
  (`arp::Plants`) and the shipped library must not contain them. `arp::plantsCompiledIntoLibrary()` returns `false` in a correct build.
- `ARP_FORCE_INT128_FALLBACK` is optional and only for testing: the core uses `__int128` where the compiler has it and a two-word fallback
  otherwise (MSVC), selected automatically, same results on both paths (the suite runs the whole core on each).
- **Never `-ffast-math` / `-ffinite-math-only`**: `core.cpp` refuses to compile under them (`#error`, IEEE semantics for the host's float inputs).
- Warning-clean under `-Wall -Wextra` (`/W4` on MSVC is the project's own setting, untested by CI).
- **Memory:** `sizeof(arp::Core)` is about 96 KiB of fixed tables; put it on the heap or as a member of a long-lived object, not on a small stack.
  `prepare(sampleRate, maxBlock)` allocates the output buffer (`max(1024, 4 * maxBlock)` events) and the step log; nothing else allocates after
  construction. The full figure, worst case included, is printed by `bash tools/core_bench.sh`.
- **Threads:** `process()` / `reset()` on the audio thread; `prepare()` off it; `setUserPattern()` / `getUserPattern()` off the audio thread,
  one writer at a time (SPEC 4.6).

## How this list is kept honest

`bash tools/core_test.sh` (and so `./verify fast`) does, every run:

1. fails if any listed file is missing or its sha256 is stale, or if any file under `core/` (other than `CMakeLists.txt`) is missing from the list;
2. copies exactly the listed files into an empty scratch directory and builds `tests/core/shipped_smoke.cpp` from them alone (C++20,
   `-Wall -Wextra -Werror`, no defines, nothing of the repository's `core/` on the include path) and runs it;
3. proves each of those detectors with a planted fault first: a list with a row removed, a list with a wrong hash, and, for every listed file in
   turn, a copy that omits it (the build must fail).

When `core/` changes on purpose, regenerate the table with `bash tools/core_test.sh copyin-table` and paste it above.
