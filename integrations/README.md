# integrations/ — Arp's inbound mailbox

**Briefs TO Arp land here**, one subdirectory per correspondent
(`integrations/<sender>/`). That subdirectory is the sender's designated intake
slot and the only path in this tree a visiting agent may write to
(autonomous `doctrine/INTEGRATIONS.md` §3, rule zero: writes stay home; the
mailbox is the one exception). Committing a filed brief is a resident act.

| artifact | lives in |
|---|---|
| a brief filed TO us | `integrations/<sender>/` **here** |
| our response to it | here, beside the brief |
| a brief WE file to someone | **their** tree, in `integrations/arp/` |
| their response to our brief | **their** tree — pull and read; it never arrives here |

Exchange files carry frontmatter: `id`, `from`, `to`, `status`, `ball`,
`filed`, `seq`, `respond-by`. `ball:` names the one side that owes the next
move; `status:` is lifecycle; neither does the other's job.

## Inbound

| from | file here | what |
|---|---|---|
| HYPERSAW | `integrations/hypersaw/notice-note-stream.md` (seq 3, 2026-10-22) | tag Arp's notes `arp` (horde ADR-270); held notes as `direct`; the pedal question. **Responded** in `response-note-stream.md` (seq 4): tag accepted, horde tees `direct`, pedal on `direct` only proposed (D-009); ball HYPERSAW |
| HYPERSAW (their tree) | `integrations/arp/response-spinup.md` (seq 2) and `response-triggers.md` (seq 4) | answered Q-H1, Q-H2, Q-H3; ball Arp, nothing owed; read and recorded 2026-10-22 (D-015) |
| HYPERSAW (their tree) | `integrations/arp/response-chord-stream.md` (seq 6) | Q-H7 yes, as three toggles (their ADR-272); asked whether the played note is in `chord`; answered in our seq 7 (D-029) |
| HYPERSAW (their tree) | `integrations/arp/response-m3.md` (seq 8, 2026-10-25) | answered Q-H5, Q-H6, Q-H8, Q-H9, Q-H10 and the tee (their ADR-282); ball Arp, nothing owed until our readiness notice; owes us the preset format (B550). Read and recorded 2026-10-27 (D-034) |
| — | (earlier) | horde's spin-up packet arrived as `SPINUP-BRIEF.md` + `intake/`, not as a mailbox brief |

## Outbound threads

| provider | file in THEIR tree | what |
|---|---|---|
| HYPERSAW (repo `horde`) | `integrations/arp/notice-spinup.md` | we exist; the survey's answers; states the pattern bank (D-004); asks Q-H1 (which MUST rows bind Arp) and Q-H2 (how horde consumes the core). **Filed 2026-10-21**; horde merged PR #1179; ball HYPERSAW, respond-by 2026-11-13 |
| HYPERSAW (repo `horde`) | `integrations/arp/notice-triggers.md` | seq 2: D-006/D-007 scope, Q-H3 (trigger lanes), the name finding, and a request to file through `integrations/hypersaw/`. **Filed 2026-10-21** as horde PR #1184 (path-only); ball HYPERSAW, respond-by 2026-11-13 |
| HYPERSAW (repo `horde`) | `integrations/arp/notice-chord-stream.md` | seq 5: the `chord` stream (D-018), Q-H7; FYI time mode, library, own scale, freeze. **Filed 2026-10-22** as horde PR #1194; ball HYPERSAW, respond-by 2026-11-13 |
| autonomous | `integrations/arp/brief.md` | brief `arp-001`: register Arp in the ecosystem tracks. **Filed 2026-10-21** as autonomous PR #60 (path-only); ball autonomous, respond-by 2026-11-04 |
| HYPERSAW (repo `horde`) | `integrations/arp/notice-m3.md` | seq 7: answers their seq-6 chord question ((b): the played note is `chord`); asks Q-H6, Q-H8, Q-H9, Q-H10 for M3. **Filed 2026-10-24** as horde PR #1214 (path-only); ball HYPERSAW, respond-by 2026-11-13 |
| FOUNDATIONS | — | not filed: modules inside horde reach FOUNDATIONS through horde (D-029) |

Working copies are kept in `docs/filings/`; the copy in the correspondent's tree is the record (once their resident merges the PR).
