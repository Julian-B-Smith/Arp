# 2026-10-29 — randomize-design — Arp's own randomize, designed and reviewed (phase 1; D-044, D-047)

- **Queue item:** T-007 (M3: horde admission), phase 1 of the randomize task
  the lead started on 2026-10-29. The criteria came in the lead's brief, not
  ROADMAP: this tree's ROADMAP has no randomize entry yet, and D-044 and D-047
  are recorded in PR #38, unmerged. The lead owns ROADMAP and DECISIONS; this
  change touches neither.
- **What changed:**
  - `docs/design/randomize.md` (new): the design. Two verbs (Structure,
    Values) plus Both; all 50 keys classed (36 structure, 8 value, 6 never
    drawn; 15 exempt by default); horde's lab laws rewritten in integers (a
    counter-based `rdraw`, a discrete bell from a σ table and an `exp` table,
    nested amount thresholds, the keep test, must change); three groups
    (transposition, chord, scale) with one amount threshold each; the planned
    API; how horde passes through and why the result is an ordinary settings
    change under SPEC §9; tests RZ1–RZ15 with plants; the spec amendment
    described, not applied; 12 open questions with recommendations.
  - `integrations/hypersaw/response-arp-randomize.md` (new): the **draft**
    seq-13 reply to horde's seq 10, not filed. It answers the four asks, asks
    the named-scale question (D-047) and whether profiles need to narrow Arp's
    bands, and reminds horde of seq 12 items 2 and 3.
  - `integrations/README.md`: the inbound row for seq 10 and the draft.
- **Why:** horde's Random page randomizes Arp in 1.0 (horde ADR-321), and the
  human ruled that Arp declares its own randomize (D-044). The design comes
  before code and spec, so the human can rule on it first.
- **Evidence consulted:**
  - `integrations/hypersaw/brief-arp-randomize.md`;
  - horde `origin/main`, read with `git show`, not edited:
    `docs/design/random-lab-report.md` (§3, §3a, §4, §4b.5a–c, §5, §6, §7),
    `docs/design/random-lab.html` (`sigma`, `drawNum`, `enumWeights`,
    `drawEnum`, `applyRoll`), DECISIONS ADR-312 and ADR-317 to ADR-322, and
    `integrations/sluice/notice-randomisers-shipped.md` (Sluice's
    declaration: a carried stream, libm ulps);
  - `spec/params.json` (all 50 keys), `spec/bank/starter.json` (the 11
    patterns: `trigger_pulse` has no `on` step; `bar_trigger` fires lane 1);
  - `spec/SPEC.md` §1.3, §2.6, §3, §4.1–4.9, §5, §6, §7, §9, §10, §11, §13,
    §14;
  - `core/include/arp/types.h` (`Settings`), `core/include/arp/draw.h`,
    `core/src/core.cpp` (`validated()` covers the user pattern only, so no
    settings validator exists; `shuffleAt` at 1345–1362 reads the live seed
    for both cycles).
- **Measured (Layer-E, a scratch Python sketch, not committed):** the σ table;
  chaos 0 identity; amount nesting with the controlling keys on (0 violations
  in 16,000 pairs; revision 1's rule as a plant gives 12); no empty candidate
  set over home `steps` 0–8; rate and gate distributions; the Structure
  no-change rate (44 % at horde's defaults without must change); no key-id
  collisions. Numbers in the design's §9.
- **Critic (Opus, independent, model pinned):**
  - **Round 1: REWORK.** 2 HIGH (amount nesting broken by same-click
    dependencies; empty or zero-weight candidate sets), 7 MED, 14 LOW. No path
    breaks a terminal invariant (`Core::adopt` takes the snapshot whole). All
    findings applied; the per-finding table is the design's §12.
  - **Round 2: REWORK.** H1 and H2 closed for the cases reported; 2 new HIGH
    (the bell's centre could drop out of the candidates: `repeats` below
    `cur`, `steps` under a locked size; must change breaks the "exact"
    nesting claim), 1 MED, 3 LOW. All applied (revision 3).
  - **Round 3: PASS.** N1, N2 and M1 closed; nothing new at HIGH or MED. One
    LOW wording fix ("distance from the centre"), applied.
- **Alternatives rejected:**
  - drawing Arp generically from horde's classes (the human chose its own,
    D-044);
  - floats in the draw: libm differs between platforms;
  - liveness read from the keys just drawn (revision 1): it breaks amount
    nesting;
  - fields in `params.json` instead of `spec/randomize.json`: they would change
    the sha256 horde uses as `module_version` (Q6 asks the human).
- **Verify:** `./verify fast` exit 0 (the commit hash is in the PR).
- **Open questions:** the design's §10, Q1–Q12, for the human. Also open for
  phase 2: adding the randomize digest to Linux CI (behind the human's gate);
  N2 under a seed change (Q12).
