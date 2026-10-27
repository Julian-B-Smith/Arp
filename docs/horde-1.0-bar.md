# Arp against horde's note-processor 1.0 bar

> horde's checklist is `docs/proposals/note-processor-1.0-bar.md` in horde's tree (B539, ADR-269), M3's gate (D-015).
> This page maps each of its rows to Arp's evidence, so the readiness notice can point here. **Last verified:
> 2026-10-27** on `main` after PR #24 (455 checks per int128 path, `./verify full` green; Linux CI green under ASan,
> TSan and UBSan). Status per row: **met**, **horde's** (horde's work, Arp supplies what it needs), or **open**.

## 1. It is defined

| Row | Status | Evidence |
|---|---|---|
| A scope list, frozen at the feature freeze | met | `README.md` "What it will do (1.0)"; the freeze (D-018) is in `ROADMAP.md`; after 1.0: `docs/deferred/landed.md` and ROADMAP "After 1.0" |
| Declared in a manifest: key, unit, range, default, morph class, rate class; keys frozen | met | `spec/params.json` (RATIFIED, hash-pinned by `spec/MANIFEST`): 47 settings, each with `key`, `label`, `kind` (the morph class), range, default, and unit / display format / enum labels (D-035). Checks: `tools/core_params_check.py` (the C++ defaults and enum lists equal the file; every setting carries what the face needs; each with a plant). Rate class: every setting is a per-block snapshot (SPEC §9); `enabled` acts at offset 0 |

## 2. It plugs in

| Row | Status | Evidence |
|---|---|---|
| The note vocabulary: `NoteRef` identity, never a bare key; base pitch, bend separate | met | SPEC §1.2; `core/include/arp/types.h`. Arp emits base keys only, never a bend. Checks: N1 (one NoteOff per NoteOn, same ref) over id-ful and id-less refs; the id-less re-press checks; LOW-A (no ref reuse after `prepare`) |
| No engine assumed downstream | met | The shipped library is standard-library C++20 only; `tools/core_test.sh` builds and runs the shipped smoke from the 8 files in `docs/horde-copy-in.md` alone, with plants for a missing file and a stale hash |
| Morph and presets: every setting a per-corner target; discrete switches at the threshold; the bank selection too | met (core side) | horde resolves the blend and the 0.05 hysteresis (SPEC §9, D-021); the core applies each change where §9 says. Checks: T8/T9 over all 42 discrete settings, 47 morph-driven sessions (N1, G1), the bank- and order-selection morph checks (M3a), a preset load changing all 47 settings at once at six states. The user pattern rides in the module preset, per corner (horde, D-034 Q-H6) |
| Triggers never create a history node | met (core side) | Triggers are `Trigger{lane, sampleOffset}` events with no state and no terminal (SPEC §8). Check T1 (once per firing step, at its first on). Undo is horde's (D-034 Q-H9) |

## 3. It is correct

| Row | Status | Evidence |
|---|---|---|
| Every emitted note ends exactly once, on every path (incl. preset load) | met | N1 with three plants over every T-row (T1-T21 counted), id-ful and id-less; C1 (event cap keeps the off-reserve); the preset-load sessions (M3a); 30 + 30 seeded random sessions; 2,000 seeded tiny-capacity sessions in the digest |
| Sample-exact timing against the transport, never at block boundaries; time signatures as data | met | G1 (blocks 1/7/64/512/4096), G2, G2b, G5; bar starts at 3/4, 6/8, 7/8, 5/4 and mid-session meter changes at several tempos (M3a) |
| Deterministic: same input, transport and seed give the same output; mulberry32; no wall clock | met | D1 (known answers against the lab and an independent Python implementation; the epoch in every draw); the byte-identical digest (42,226 runs) |
| Sample-rate and block-size independent | met | G1 across block sizes; G4 (ticks identical at 44.1/48/96/192 kHz) over every session family; chord-stage notes checked on their cause's sample |
| Real-time safe: no allocation, locks or system calls | met | A1 (an allocation hook: 0 calls in `process`/`reset`/settings changes over every session); the CI `sanitize` job (ASan, TSan incl. the real-thread user-pattern test, UBSan) on Linux |
| No input, no output (notes) | met | 1,728 sessions with no keys and no latch emit 0 notes (M3a). Trigger lanes may run with no keys under restart bar/free, as SPEC §2.5 rules; horde amends the row to say the row is about notes (D-034 Q-H8) |
| The pedal and restart behave as ruled | met | The held-set checks (pedal holds Arp's input, CC 64 consumed, D-000 Open 3); restart chord/bar/free known answers and RP1 (M2b); horde's own pedal holds `direct` voices only (D-034 Q-H5) |
| Scale quantize keeps every note in the scale | met | Q1-Q5 (an exhaustive nearest-key oracle over every mask, root and key) |

## 4. It is affordable

| Row | Status | Evidence |
|---|---|---|
| A measured CPU cost in Release, inside the ≤ 2 % slice | met (with a published input envelope) | `docs/horde-copy-in.md` "CPU envelope": every settings-only configuration (720 per int128 path) within 2 % after horde's ×4 heuristic, the heaviest 1.53 / 1.69 %; incoming notes within the slice with 10 % headroom up to **1,300 note events/s** on the heaviest load (D-033), the measured edge 3,600, far more on ordinary loads. **×4 is horde's heuristic, not min-spec verified.** `./verify full` reports it on every run |
| A memory figure, worst case, all allocated at prepare | met | `sizeof(arp::Core)` about 95 KiB of fixed tables, plus at most 1.26 MiB at `prepare()`; 1.35 MiB worst case in all; nothing allocated while processing (the bench and A1) |

## 5. It is usable

| Row | Status | Evidence |
|---|---|---|
| A face in GUI 3's design system | horde's | horde builds it (D-034 Q-H9) from `spec/params.json`'s names, units, formats and enum labels (D-035) |
| Undo covers every control edit | horde's | horde's, one step per control gesture; a trigger makes none (D-034 Q-H9) |
| Factory content: an init and the starter bank | **open** | Arp authors them as horde preset files (D-034 Q-H10) once horde sends the preset format (their B550). The content exists as data: the init is `spec/params.json`'s defaults; the bank is `spec/bank/starter.json` (11 patterns, compiled in) over the 16 order styles |
| A manual page (SHOULD) | open | not started |

## 6. It is signed off

| Row | Status | Evidence |
|---|---|---|
| An independent critic review of the core and its oracles | met | Opus critic reviews at every milestone, sourced separately from the Sonnet author: M1a, M1b, M2a, M2b and its re-check, M3b and two re-checks, M3c; no HIGH open (`docs/spec-review-r*.md` for the spec; the traces for the core) |
| The human's listening sign-off | **open** | to be recorded in ROADMAP |
| Its name cleared (B433) | **open** | ROADMAP Phase N |
