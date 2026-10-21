# pattern bank joins 1.0; spin-up filings filed

- **Queue item:** T-002 (spin-up filings); unqueued scope change D-004.
- **Why:** the human asked for "a bank of patterns for the arpeggiation, like
  Ableton's arpeggiator", and by poll chose both kinds (order styles and step
  patterns) with a starter bank in 1.0, and "file both now".
- **Evidence consulted:**
  - the human's poll (2026-10-21);
  - autonomous `doctrine/INTEGRATIONS.md` §3 (the mailbox exception across
    remotes);
  - horde `integrations/README.md` and `integrations/sluice/` (file naming);
  - the remotes, which showed that Echo's spin-up drafts were never filed, so
    there was no filing precedent from Echo.
- **Changed:**
  - manifest `scope_1_0.pattern_bank` + `open_at_survey.starter_bank_contents`
    (a protected path, covered by the D-004 poll);
  - DECISIONS D-004;
  - ROADMAP: Phase 0, Phase A, M2 and its gate; Q-A3; deferred items; T-002;
  - CLAUDE.md §Domain (the bank is data, clean-room);
  - README;
  - the horde notice (states the bank);
  - the filing frontmatter (status: filed, respond-by);
  - the outbound table.
- **Filed** through the GitHub API, so neither provider's local checkout was
  touched (horde's had uncommitted work in progress):
  - horde PR #1179, which adds only `integrations/arp/notice-spinup.md`;
  - autonomous PR #60, which adds only `integrations/arp/brief.md`.

  Merging each is that repo's resident act.
- **Verify:** `./verify fast` exit 0 before commit.
- **Open:** Q-A3 (starter bank entries); Q-H1/Q-H2 now with horde.
