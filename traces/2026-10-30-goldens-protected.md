# 2026-10-30 — The bank-pattern goldens are a protected path (D-046)

**What changed.** CLAUDE.md §Domain, "Protected paths": "goldens once cut" now
names `tests/core/goldens/`, its cut commit, and the only way to re-cut it.

**Why.** D-046 (the human's poll): the goldens are protected once cut. They
were cut in PR #41. Its session proposed this line for the lead rather than
editing the charter itself.

**Evidence.**
- `tests/core/goldens/*.txt` headers record the cut from c5741de.
- `tools/core_golden.sh cut` is the re-cut tool.
- The check runs in `tools/core_test.sh`, part of `./verify fast`.
- A byte-pin in `./verify` (a spec_gate-style sha256 check) would need a
  human gate. It was not asked for; the existing check already fails on any
  mismatch with the shipped core.

**Verify.** `./verify fast` exit 0 on this tree.
