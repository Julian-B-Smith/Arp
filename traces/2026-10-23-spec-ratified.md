# spec-ratified — the 1.0 spec is RATIFIED; Phase A closes

- **Queue item:** the ROADMAP Phase A gate (D-021, D-022).
- **Why:** the human said "let's ratify the spec".
- **Done:**
  - **D-021, first:** every rule still PROPOSED was put to the human by
    poll and accepted:
    - the morph rule;
    - restart under morph;
    - pre-roll;
    - tie memory resetting at release-all;
    - the pick pass;
    - the chord re-voice;
    - the host-position bands.

    The bank question was settled too: all 16 styles and all 11 patterns
    ship, and presets are curated at M3.
  - **D-022, then:** the ratification poll, answered "Ratify".
- **Changed:**
  - `spec/SPEC.md` header → RATIFIED r2.12, with §12 rewritten as "settled";
  - params and bank status;
  - DECISIONS D-021 and D-022;
  - ROADMAP: Phase A DONE, M1 ← current, Q-A1/A2/A3/A5 settled;
  - README;
  - CLAUDE.md §Domain (`spec/` protected; the lab built and editable).
- **Verify:** `./verify fast` exit 0.
- **Open:**
  - with horde: Q-H5, Q-H6 (read ADR-169 first) and Q-H7;
  - **a proposal:** pin `spec/` by hash in `./verify`, as `intake/` is, so
    the protection is enforced and not just stated. This needs the human's
    go-ahead (an edit to `./verify`).
