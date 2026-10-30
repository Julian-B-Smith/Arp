# 2026-10-29 — Another product's name out of the forward-facing text, wording only (D-045)

**What changed.**
- `spec/SPEC.md`:
  - the header note (r2.7 line) now says "transposition **steps**";
  - §4.7's heading is "Transposition steps (D-014)". Its intro describes the
    behaviour in our own words: the whole pattern repeats, shifted by one
    more `step_size` on each pass;
  - the status line records this edit (D-045, wording only).
- `spec/params.json` and its embedded copy in `lab/arp-lab.html`: the `steps`
  note is reworded. No number, default or range changed.
- `spec/MANIFEST`: both lines re-pinned in the same change.
- `README.md`, `project.manifest.json`: the same rewording. The manifest's
  `pattern_bank` drops a quote of the human's words, which D-004 still keeps.
- `lab/arp-lab.html`:
  - the keyboard help says "Piano-style computer keys", and "Note names put
    middle C (MIDI 60) at C3";
  - comments that named the DAW are neutral now;
  - the constant `ABLETON` is renamed `KEYMAP` at all 5 uses. The name was
    new, so nothing collides with it.
  The `<h3>Live</h3>` heading of the live roll is a view name, not the
  product, and stays.

**Why.** D-045: the human's poll of 2026-10-29 (recorded in PR #38). The name
leaves forward-facing text. History keeps it: DECISIONS.md, traces/,
docs/prior-art.md, REFLECTIONS.md and SESSION.md are untouched. The same poll
is the human's approval for this edit of the protected `spec/` and
`project.manifest.json`. The D-045 record itself lands with PR #38 (the lead
owns DECISIONS.md).

**Evidence.**
- `git grep -n -i ableton -- . ':!DECISIONS.md' ':!traces' ':!docs/prior-art.md' ':!REFLECTIONS.md' ':!SESSION.md'`
  finds nothing (exit 1).
- `python3 tools/gen_manual_params.py --check`: all PASS. The generated
  settings table was unchanged, so the manual needed no regeneration.
- Lab smoke: "embedded ARP_PARAMS deep-equals spec/params.json" PASS;
  `lab_smoke: OK`.
- Core: `core_test: all binaries green`. The stream digest and the randomness
  digest both match their committed values, so behaviour did not change.

**Critic (Opus, independent): no HIGH open.**
- MEDIUM 1: the spec edit needs its decision record. That record is D-045,
  in PR #38, which is the lead's to merge first.
- MEDIUM 2: the status line did not list this edit. Applied.
- LOW 1-3 applied: the §4.7 intro says "one more `step_size`"; the KEYMAP
  continuation line is re-aligned; the C3 comment is clearer.
- LOW 4 accepted: the manifest quote survives in D-004.
- LOW 5 is not acted on. These are oracle gaps:
  - the smoke does not exercise the lab's keyboard handler;
  - no gate stops the product name coming back.
  Closing either edits `./verify`, which is the human's gate.

**Verify.** `./verify fast` exit 0 and `./verify full` exit 0 on this tree,
base c5741de; the commit hash is in the PR.
