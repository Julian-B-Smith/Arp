# manual-page — the Arp user-manual page and its generated settings table

- **Queue item:** T-007 (M3), horde's note-processor 1.0 bar row "A manual page (SHOULD)", branch `m3/manual`.
- **Why:** horde copies the section "The arpeggiator" into its manual. The page is written for musicians from the
  ratified spec; the settings table is generated from `spec/params.json` so it cannot drift, with a check and must-fail
  plants (detectors need a plant before they gate anything).
- **Evidence consulted:** horde `docs/manual/README.md`, `17-midi-and-mpe.md` and `figures.json` (read-only, for the
  conventions and the entry shape); `spec/SPEC.md` (every behaviour; citations are HTML comments after each paragraph),
  `spec/params.json` (labels, ranges, defaults, units, value labels), `spec/bank/starter.json`; `README.md`;
  `DECISIONS.md` D-035 to D-039; `docs/horde-copy-in.md` "CPU envelope" (consequences only; D-038 says the input-rate
  figure is re-measured, so no number is quoted); `tests/core/render.cpp` scenario ids for the audio sources.
- **Changed:** new `docs/manual/arp.md`, `docs/manual/figures.json` (19 placeholders, one entry each), new
  `tools/gen_manual_params.py` (`--write`, `--check`, `--plant`), `CODEMAP.md` (two rows).
- **Not touched:** `spec/`, `intake/`, `./verify`, `.kit/`, `core/`, `lab/`, ROADMAP.md, DECISIONS.md, the horde repo.
  The tool is NOT wired into `./verify` (a human gate; the lead asks).
- **Alternatives rejected:** a `[[TAB:...]]` placeholder for the settings reference (it is generated inline between
  markers instead, so there is one source of truth and no empty placeholder); quoting CPU or input-rate numbers (D-038
  makes them provisional); naming GUI 3 labels for the pattern-step fields (not ruled, so described, with an OPEN note).
- **Detectors and plants (`--plant`):** table (one default off by one), figures (a placeholder loses its entry), status
  (a section loses its status line), prose (a spec citation in the prose). Each goes red; a hand edit of the page's
  Gate default was also seen red by `--check`, then reverted.
- **Verify:** `python3 tools/gen_manual_params.py --check` exit 0; `--plant` exit 1 (all four plants detected);
  `./verify fast` exit 0 at git b63adab (tree is the uncommitted working tree; see `.harness/last-verify.json`).
- **Revision (same day, lead's message):** Q-H7, Q-H5, Q-H6 and Q-H3 folded into the prose as horde's behaviour
  (ROADMAP "Open questions", D-015, D-029, D-034), cited in comments; toggle names marked *(name pending)*;
  `arp.pattern-editor` stays pending-design. The "Last verified" line now claims only what the tool checks.
- **Open questions (still OPEN in the page):** whether the own scale offers named scales; the GUI 3 labels of the
  step fields; the factory presets (curated at M3, Q-H10 waits on horde's B550); the final input-rate figure (D-038);
  where the pattern editor sits on the page.
