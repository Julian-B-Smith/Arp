# REFLECTIONS — Arp

Dated notes worth holding between sessions. `/wakeup` prunes stale ones. Format: `- [YYYY-MM-DD] text`.


  The Phase A gate, and so M1, waits on this.
- [2026-10-22] **Stacked PRs:** after the base PR merges, retarget the stacked PR to `main` before merging it. #2 merged into its dead base branch after #1 had landed, and needed #3 to reach main. Later stacks (#9 → #10 → #11) targeted `main` directly and were merged in order, which worked.
- [2026-10-22] **A hidden Browser pane throttles `requestAnimationFrame` to near zero.** The lab's thumbnail matrix looked stalled (177 of 204 blank) until the pane was visible. Check `document.visibilityState` before calling a render slow.
- [2026-10-22] **1.0 widened five times in one day:**
  - D-004, the bank;
  - D-006, four extras;
  - D-007, triggers;
  - D-011, chord modes and the user pattern;
  - D-014, steps.

  The yield order if the 2026-12-11 cutoff tightens: ratchets, then the user pattern, then probability, then triggers. Re-check scope against the date at the next phase gate. (2026-10-22, kept at the /wakeup prune: the time mode and the pattern library make it a sixth and seventh widening.)
