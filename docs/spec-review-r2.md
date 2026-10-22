# Spec review r2: the independent critic's findings on r2.3–r2.4, and their disposition

- **Reviewer:** the `critic` agent (Opus, pinned), 2026-10-22. It was
  sourced separately from the author and was read-only.
- **Scope:** §6 (quantize in horde's schema), the four chord + note styles,
  `pick`, the user pattern, and §11 Q1–Q4, R1, U1.
- **Verdict:** REWORK, "small and local, like r1".

All findings are folded into **SPEC r2.6** (D-013).

| # | Sev | Finding (short) | r2.6 disposition |
|---|---|---|---|
| H1 | HIGH | `lastQ` had no update point or order; emission-time updates break G1; chord-key chaining is circular | updates at **evaluation**; keys chained in ascending pre-quantize order; a refused on still counts; off or zero mask leaves it untouched (§6); test Q5 |
| H2 | HIGH | `lastQ` survived transport edges, so a bounce could differ from what was auditioned | resets at every release-all (T4/T6/T7/T10) — **PROPOSED**; the human confirms at ratification (§6) |
| H3 | HIGH | `prepare` would wipe the user pattern; `setUserPattern` can come before `prepare` | storage is fixed members sized at construction; `prepare` and `reset` never touch it (§4.6) |
| H4 | HIGH | double-buffering tears or blocks with two calls between steps; no getter | triple buffer, serialised writer, latest wins, atomic index exchange; the flag is read once per `process`; `getUserPattern()` returns what a preset saves (§4.6) |
| H5 | HIGH | `pairs` with e = 1 indexes E[1] | e = 1 → {E[0]} (§4.2) |
| M1 | MED | quantize before drop kept or dropped out-of-range keys by pitch class | drop before quantize; only kept keys are quantized and touch `lastQ` (§4.3, §6) |
| M2 | MED | morphing 13 separate scale params passes through hybrid scales | {quantize, scale_source, scale_root, scale_deg0..11} is one morph group with a single decision (§6). The lab already resolves all discrete settings by one corner pick |
| M3 | MED | the charter still said "never owned"; Q1 is false under chord_root | CLAUDE.md §Domain amended to D-010; Q1 now reads "effective scale" (§11) |
| M4 | MED | chord_root did not match ADR-112's "anchor always admitted" | adopted (§6) |
| M5 | MED | `stride` with one key at oct ≥ 2 was ambiguous | key +12·(1 + fdiv(p, 2)); step `oct` applies to every key, the bass included (§4.2) |
| M6 | MED | no terminal row for a user-pattern swap; D1 omitted the pattern | T17 (§7); D1 lists the user pattern (§10) |
| M7 | MED | horde ADR-169 (module presets embed by value; preset identity per corner) may already answer, or contradict, Q-H6 | **read ADR-169 before filing Q-H6** (ROADMAP note) |
| M8 | MED | the `chord` style's H order was unstated | H is in pitch order everywhere in §4 (§4.2) |
| L1 | LOW | "with nothing held, the root applies" is unreachable | removed |
| L2 | LOW | `pick` mixed types, and "clamp" was meaningless for an enum | encoding stated; invalid → `order` (§4.6) |
| L3 | LOW | the tie rule collapses whole-tone black-key pairs (62 62) | a Layer-E listening note, not a gate (end of SPEC) |
| L4 | LOW | deactivate was missing from the `lastQ` reset list | covered: release-all includes T10 |
| — | gaps | no G1 session with quantize ties; Q2's plant discriminates only with `lastQ` above the tie; collisions untested | Q5 adds both; collisions are added to the lab smoke brief |

**The critic's question for the human:** should a quantize tie remember
notes played before the transport started? It is answered PROPOSED by H2: no
(reset at release-all). The human confirms at spec ratification.
