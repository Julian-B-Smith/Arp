# CODEMAP — Arp

Where things are. Update in the same change that moves them.

| Path | Role |
|---|---|
| `verify` | Oracle dispatcher (project-owned gates: private-name, structure, manifest, intake pin) |
| `.kit/` | Kit-owned gates, vendored + sha256-pinned by `kit_sync.py` — never hand-edit |
| `.claude/` | Harness: settings (permissions), hooks (deny, dirty, stop gate), agents (critic, implementer, verifier), provenance skill |
| `intake/` | horde's packet (note vocabulary, engine note interface, quantizer, tempo divisions), read-only, pinned by `intake/MANIFEST` |
| `integrations/` | Inbound mailbox + outbound thread table |
| `docs/filings/` | Working copies of outbound filings |
| `docs/prior-art.md` | Phase 0 landscape, failure-mode rules, grid recipe, name check |
| `traces/` | One provenance entry per merged change |
| `spec/` | SPEC.md (draft r2.1), params.json (settings + division ticks), bank/starter.json (step patterns as data) |
| `lab/arp-lab.html` | Single-file twin: CORE (pure; sliced by the smoke) / UI (adapter) banners |
| `tools/lab_smoke.mjs` | Headless lab smoke: SPEC §11 invariants with planted controls |
| `.claude/launch.json` | `lab` preview server (python http.server on localhost) |
| `core/` | _planned, M1_ — C++20 library (CMake, Unix Makefiles) |
