# REFLECTIONS — Arp

Dated notes worth holding between sessions. `/wakeup` prunes stale ones. Format: `- [YYYY-MM-DD] text`.


  The Phase A gate, and so M1, waits on this.
- [2026-10-22] **Stacked PRs:** after the base PR merges, retarget the stacked PR to `main` before merging it. #2 merged into its dead base branch after #1 had landed, and needed #3 to reach main. Later stacks (#9 → #10 → #11) targeted `main` directly and were merged in order, which worked.
- [2026-10-22] **A hidden Browser pane throttles `requestAnimationFrame` to near zero.** The lab's thumbnail matrix looked stalled (177 of 204 blank) until the pane was visible. Check `document.visibilityState` before calling a render slow.
- [2026-10-29] **Loose threads, as the human named them at close:**
  - horde owes two answers before factory presets can start: enums carried by name, and the preset `basis` (our seq 12, D-040).
  - Piece 17 (randomness) on the listening page is still unheard.
  - Two old worktrees (`randomness`, `zealous-noyce-aaac9d`) could be cleaned up, only with the human's OK.
  - "Ableton" is named in SPEC §4.7's heading and in the README and the lab row. The clean-room rule says we use our own names. `spec/` is protected, so fixing the heading needs the human's gate.
