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
| HYPERSAW (repo `horde`) | `integrations/arp/notice-spinup.md` | we exist; the survey's answers; asks Q-H1 (which MUST rows bind Arp) and Q-H2 (how horde consumes the core) — draft in `docs/filings/`, **not yet filed** (ROADMAP T-002) |
| autonomous | `integrations/arp/brief.md` | register Arp in the ecosystem tracks — draft in `docs/filings/`, **not yet filed** (T-002) |
| FOUNDATIONS | — | deferred until the C++ first touches NoteRef/HeldStack (survey Q6) |

Drafts are kept in `docs/filings/`; the copy in the correspondent's tree is the record.
