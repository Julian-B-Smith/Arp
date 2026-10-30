# 2026-10-29 — The manual states the input limit (D-042)

**What changed.** `docs/manual/arp.md`, "Performance" section only (plus the
"Last verified" line):
- The OPEN note on the input-rate limit is gone.
- New prose: Arp stays inside its share with up to 1,000 note events a second
  (each press and each release), on every setting including **Velocity
  random**, **Gate random** and **Random curve** at 100, at 44.1-192 kHz. A
  fast player sends about 40. The 1,000 is a processor budget, not a cap on
  notes: above it Arp follows the same rules (32 held keys and so on) and
  uses more of the processor.
- The thin margin is stated plainly: in the heaviest corner (randomness at
  100, heaviest settings, highest sample rates) Arp is within measurement
  noise of its share at 1,000 a second. The "development Mac with a safety
  multiplier, not the slowest machine horde supports" wording (x4, not
  min-spec verified) is kept.
- Line 440 now says "all three randomness settings at 100" (was "both").
- The HTML comment cites D-042 and the copy-in, and records what is
  inferred (every setting; blocks under 16 not measured).
- `tools/gen_manual_params.py` is unchanged: no new number needed registering.

**Why.** D-042 decided the figure; the human's poll asked for the OPEN note
to be filled in (T-007). The brief came from the Arp lead session.

**Evidence.** DECISIONS.md D-042; `docs/horde-copy-in.md` "The quoted input
limit" and "What is measured and what is inferred"; SPEC §1.3 (capacities are
fixed counts, none per second).

**Critic (Opus, independent), three rounds, no HIGH open.**
- Round 1, REWORK. HIGH: "can take in up to" read as a cap that drops notes.
  Fixed (reworded to a budget). MEDIUM: "everywhere else there is more room"
  understated the corner; the randomness controls were not named in bold; the
  "Last verified" line was not re-dated. All fixed.
- Round 2, REWORK. HIGH: my fix overclaimed ("still plays every note
  correctly"), contradicting the 32-key limit. Fixed. MEDIUM: wrong heading
  in the comment, a superseded source, and "both" vs three randomness
  settings. Fixed.
- Round 3, APPROVE. No HIGH or MEDIUM. Optional LOW not taken: add "its
  per-block note limit" for an extreme flood (the comment names it).

**Open (not for this change).** The manual checker does not test the figures
1,000, 44.1-192 or 40, and nothing gates the CPU figure, so the page goes
stale silently if the core's cost changes. horde copies this page, so a
re-measure means updating it too.

**Verify.** `python3 tools/gen_manual_params.py --check` exit 0 (24 stated
numbers match params.json); `./verify fast` exit 0. The commit hash is in
the PR.
