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
| — | — | none yet (horde's spin-up packet arrived as `SPINUP-BRIEF.md` + `intake/`, not as a mailbox brief) |

## Outbound threads

| provider | file in THEIR tree | what |
|---|---|---|
| HYPERSAW (repo `horde`) | `integrations/arp/notice-spinup.md` | we exist; the survey's answers; states the pattern bank (D-004); asks Q-H1 (which MUST rows bind Arp) and Q-H2 (how horde consumes the core). **Filed 2026-10-21**; horde merged PR #1179; ball HYPERSAW, respond-by 2026-11-13 |
| HYPERSAW (repo `horde`) | `integrations/arp/notice-triggers.md` | seq 2: D-006/D-007 scope, Q-H3 (trigger lanes), the name finding, and a request to file through `integrations/hypersaw/`. **Filed 2026-10-21** as horde PR #1184 (path-only); ball HYPERSAW, respond-by 2026-11-13 |
| autonomous | `integrations/arp/brief.md` | brief `arp-001`: register Arp in the ecosystem tracks. **Filed 2026-10-21** as autonomous PR #60 (path-only); ball autonomous, respond-by 2026-11-04 |
| FOUNDATIONS | — | deferred until the C++ first touches NoteRef/HeldStack (survey Q6) |

Working copies are kept in `docs/filings/`; the copy in the correspondent's tree is the record (once their resident merges the PR).
