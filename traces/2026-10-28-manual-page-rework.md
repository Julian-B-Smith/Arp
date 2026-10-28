# manual-page-rework — the critic's REWORK on the Arp manual page, and the tool's oracle gaps

- **Queue item:** T-007 (M3), horde's note-processor 1.0 bar row "A manual page (SHOULD)", branch `m3/manual`.
  Corrects and extends `traces/2026-10-28-manual-page.md` (not edited; traces are append-only).
- **Corrections to the first trace:** it said `./verify` and ROADMAP.md were untouched and the tool was not wired in.
  The lead has since edited `./verify` (a `manual_gate`, run by `./verify fast`: `--plant` first, then `--check`) and
  ROADMAP.md. This change set touches neither, nor `spec/`.
- **Why:** the independent Opus critic returned REWORK on HEAD 644620c. Each finding was re-checked against the SPEC
  section it concerns before the prose changed.
- **Prose fixes (all in `docs/manual/arp.md`):**
  - HIGH: with the DAW stopped `Bar` and `Free` act as `New chord` (SPEC §2.4 H7a, §13.7); a used-up arp is revived
    by a transport start/stop/jump/loop while keys are held or latched, and fires no triggers (§4.8 E12); the
    "next step" claim for morph now names the settings that act at once (Arp module T4, Arp on §13.14, Rate mode T19,
    Latch §13.12, Chord and its settings §14); the sample-rate claim carries the D-039/G4 exception.
  - MEDIUM: transport edges and Rate mode end the arp's notes only (T21); a pass is the style's own cycle (§4.7);
    switching Restart into Bar/Free re-aligns and restarts the order (Q-A2); only the chord stage's copy of the played
    note is snapped (§14); a scale change re-voices chord notes at once (§14); stopped DAW: step 0 on the press (H7a).
  - LOW: pedal/latch hold (line 58); a tie's tail runs past the last tied step at a gate above 100 % (§5.2; for a
    ratchet the length is capped at its slice, so the critic's "past the last sub-note" was read as the tie tail);
    Stride with one key (§4.2); trigger-only steps fire while released notes still sound on a stopped DAW (§2.4);
    "Arp module" vs "Arp on" in the restart list (§4.5 names `enabled`); the line-27 wording about the chord stage's
    copy of the played note (the clash was guessed: the "+0 slot" term now kept out of the prose and the comment says the
    played note is not one of the six intervals).
  - D-034 (lead's ruling): the horde sustain pedal paragraph is its own H3 with `Status: PENDING — D-034, horde RC-6.7`
    and says what horde WILL do; the per-corner pattern is kept, the comment notes SPEC §4.6's "one pattern per preset"
    is per module preset.
  - The panel path uses horde's single-bold form (`**Arp › Arp module** *(page pending)*`). The step fields are now
    italic (they are not settings), which also lets the number check tell them from the settings of the same name.
- **Tool fixes (`tools/gen_manual_params.py`):**
  - `--plant` exits **3** and prints the exact line `every plant was detected` when every plant is detected (a crashed
    Python exits 1, so it cannot be mistaken for success); 2 = a plant missed or a setup error. The lead's
    `manual_gate` must accept exit 3 only.
  - New `numbers` check: a sentence naming a numeric setting in bold and stating "default N" or "N to M" is held to
    `spec/params.json` (unit-aware: ms/s for Step time, % for Swing); it refuses to pass if it finds fewer than 8
    stated numbers (24 found today). Plants: a prose default off by one, a prose range changed.
  - New `depends_on` rule: only a horde part id, `Bnnn` or `ADR-nnn` (what horde's `manual_scaffold_check.py` accepts);
    plant: a SPEC reference in `depends_on`. `figures.json` now carries the SPEC and decision references in `source`.
  - Docstring and CODEMAP no longer say the tool is outside `./verify`.
- **Evidence consulted:** SPEC §1.1, §2.4, §2.6, §4.2, §4.5, §4.7, §4.8, §5.2, §6, §9, §11 G4, §13, §14; DECISIONS D-034,
  D-039; horde `tools/manual_scaffold_check.py`, `docs/H2-PLAN.md` parts table, ROADMAP rows (read-only).
- **Verify:** `python3 tools/gen_manual_params.py --check` exit 0; `--plant` exit 3 (every plant detected);
  `./verify fast` exit 1 at git 644620c, tree 1118f932e732fbf7f06ffcc01085b5710a9292fc, EXPECTED until the lead
  updates `manual_gate` to accept exit 3 (it printed "a planted fault was NOT detected" while listing every plant as
  detected). sha256: `arp.md` d1e9f41b05d50ec284605368df0079f560c708fa05e7dae7ed9af45f97ab9d4b, `figures.json`
  ad0094666d958a439431bcbb55a916d3f1c06c3bf2f615d48abbd38bc56d28f9, `gen_manual_params.py`
  c1228adbc6d645ce99c639a717d38c9b5349dbbc43dabb39a60036f2a3885308.
- **Open questions:** unchanged from the first trace except Q-H3, Q-H5, Q-H6 and Q-H7, which horde answered; the
  step-field labels, named scales for the own scale, the factory presets (Q-H10 waits on horde's B550), the final
  input-rate figure (D-038) and the editor's place on the page remain OPEN.

## Lead close (2026-10-28)
- Critic re-review of 644620c..9d44c8f: all four HIGHs closed, **no HIGH**, APPROVE-WITH-NOTES. The one MEDIUM (a pedal-tails clause in a READY section that depends on horde's unbuilt RC-6.7) and the LOWs (pass length in played notes, the CODEMAP exit code) fixed by the lead; "Last verified" dated with the review.
- `manual_gate` in `./verify` (approved by the human 2026-10-28) accepts the plant run only at exit 3 with the exact line "every plant was detected".
- `docs/horde-1.0-bar.md`: the manual row is met on Arp's side.
