# morph-blend-design — horde's seq 14 folded in; the blend's draft design

- **Queue item:** T-007, the D-050 row (criterion 2) and the seq 14 rulings (D-051).
- **Why:** horde answered seq 12 and seq 13, and the human ruled the order of the remaining M3 work. The blend's design had to come first, because a new setting would change `spec/params.json` and force the presets to be redone.
- **What changed:** `DECISIONS.md` D-051; the ROADMAP rows; `docs/design/morph-blend.md` (draft). No code, no spec.
- **Evidence consulted:** horde's filing seq 14; horde (read-only, no writes) `DECISIONS.md` ADR-104, ADR-291, ADR-317, ADR-324, ADR-325 and `ROADMAP.md` B557; `integrations/hypersaw/notice-preset-format.md:46-48`; SPEC §4.3, §4.6, §5.2, §9.
- **Finding:** horde's morph laws and per-parameter menu postdate SPEC §9's rule, and nothing in horde's text says what they do to a note processor's discrete settings. Proposed: the per-step draw is what the Blend law means for `pattern`. Unconfirmed by horde.
- **Alternatives rejected:** thresholds on one draw (with four corners a step can jump between corners whose weights did not rise), put to the human anyway; a shared pattern position (cannot equal the pattern alone at a corner).
- **Verify:** see the commit's `./verify fast`.
- **Open:** the human's four answers; a critic on the answered design; the brief to horde.
- **Revision 2 (the human's save-any-position contract, and the critic's REWORK of revision 1):** D-053; the design rewritten; the brief drafted, not filed. Evidence: the scratch model, now `docs/design/morph-blend-model.mjs` (its first plant could not fire, because it keyed by position in an already merged and sorted list; replaced by two that fire), and `docs/design/morph-blend-sizes.mjs`; horde's SPEC-ENGINE at origin/main `4b2fdae` (MO-1 to MO-18, the morph settings table), ADR-351, RS-6.12, the 1.0 bar and the preset format. The critic's citations to horde were checked before use. A critic re-check of the HIGH findings is the next step.
