# 2026-10-28 — Replies to horde's two-arp brief (seq 9) and preset format (seq 10)

**What changed.** Two replies filed beside horde's messages in
`integrations/hypersaw/`: `response-two-arps.md` (seq 11) and
`response-preset-format.md` (seq 12). Decision D-040 records the human's
four poll answers. `integrations/README.md` lists both threads.

**Why.** Both messages had the ball on Arp (the SessionStart mailbox scan).
The preset format gates our factory presets (B550), the last M3 build item.

**Evidence consulted.**
- Global state: `grep static` over `core/` finds only `core/src/core.cpp:284`
  and `:294`, both inside `#ifdef ARP_PLANTS`.
- Draws: `core.cpp:292` (seed, position, lane, epoch). Output ids: a
  per-instance counter, `core/include/arp/types.h:43–46` and SPEC §1.2.
- The chord stage in one object: SPEC §14 and the CH1 plants in
  `core/include/arp/core.h:107–123`.
- Keys and kinds: `spec/params.json`, 50 params (7 continuous, 43 discrete).
  `pattern` and `rate` use `values_from`, not literal lists.
- horde's format: `docs/proposals/note-processor-preset-format.md` on horde's
  `origin/main` (b4a60a1).
- FOUNDATIONS `core/include/foundations/preset_cascade.h:21–23, 63–67, 172–175`:
  `absolute` is `map<string, double>`, and a missing basis reads as 0.0 on load.

**The human's answers (poll, 2026-10-28):**
- Q-H12: the tag stays `arp`, plus an instance number (recommended).
- Two-arp presets: decide after 1.0.
- Pattern and Rate: ask horde to carry them by name. The recommended option,
  freezing the lists, was declined.
- After the critic: all 11 enums by name (recommended), because
  `spec/params.json`'s `note` already says "Enums are stored by NAME".

**Critic (Opus, independent), verdict REWORK; all findings applied.**
- HIGH: the reply said the chord set depends only on `chord_*`. Fixed: the
  full list of settings it reads (SPEC §14 items 12, 13 and 15).
- MEDIUM, all fixed:
  - the bar lock holds in sync mode only;
  - horde can already leave Arp 2's `chord` stream unrouted;
  - the manifest's "stored by NAME" line (the second poll).
- LOW, all fixed:
  - ids start at 0;
  - the load-side basis citation;
  - store a division's main name, not its alias;
  - names do not protect automation (IF-11);
  - whether enum keys also stay in `absolute`.

**Critic round 2, verdict REWORK; all findings applied.**
- HIGH: the reply said append-only keeps automation stable. False: n changes
  whenever a list grows. Fixed in the reply and in D-040.
- LOW, all fixed:
  - only entries after the first move;
  - item 7's full conditions;
  - the opening now matches item 7;
  - two stale trace lines.

**Critic round 3: APPROVE-WITH-NOTES, no HIGH and no MEDIUM.** Both LOW notes
were applied: item 5's heading now says "could change", and D-040 carries
item 7's conditions.

**Verify.** See the commit (`./verify fast`).
