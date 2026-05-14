# Phase 8: Scaffolded-Device Wiring - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-14
**Phase:** 08-scaffolded-device-wiring
**Areas discussed:** Maturity field, DEVICES-04 picks
**Mode:** `/gsd-autonomous --interactive` (default discuss-phase mode, lightweight because pre-work landed in 62da68c)

______________________________________________________________________

## Maturity field

### Q1 — DEVICES-01: how to land the 5-tier maturity vocabulary in devices.yaml?

| Option                                       | Description                                                                                                                                            | Selected |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | -------- |
| Rename status → maturity, expand to 5 values | Single source of truth. 1:1 migration of existing values. README/wiki AUTOGEN regenerator updates to read maturity. Cleaner, fits DEVICES-01 verbatim. | ✓        |
| Keep status, add maturity as new field       | Backwards-compat with AUTOGEN; risks drift between two fields.                                                                                         |          |
| Expand status to 5 values without renaming   | Misleading once values include capability tiers like 'probed'.                                                                                         |          |

**User's choice:** Rename status → maturity, expand to 5 values.
**Notes:** Vocabulary: scaffolded / probed / partial / functional / verified (lowercase per existing convention). Migration is 1:1 for the 2 existing values; new values for the new tiers. Single atomic commit covers YAML + regenerator + AUTOGEN re-run.

______________________________________________________________________

## DEVICES-04 picks

### Q1 — Does AKP815 (already shipped in 62da68c) count as the #1 promotion?

| Option                                                   | Description                                                                                                                              | Selected |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| AKP815 counts as #1; pick 1 more during planning         | AKP815 already has backend + protocol .md + register entry. Counts as DEVICES-04's first promotion template. Planner picks #2 candidate. | ✓        |
| AKP815 is just catalog work, doesn't count; pick 2 fresh | Reserve DEVICES-04 for new promotions only. Higher work commitment.                                                                      |          |
| AKP815 counts as both #1 and #2 (single satisfies)       | DEVICES-04 says '1-2'; one is the lower bound.                                                                                           |          |

**User's choice:** AKP815 counts as #1; pick 1 more during planning.
**Notes:** Default suggested #2: Mirabox N3 (one rev) — broadens product coverage beyond AJAZZ-branded SKUs; protocol artefact is a small annotated cross-reference rather than a full new protocol doc. Planner can pick differently if a contributor's hardware availability or capture priority surfaces a better candidate.

______________________________________________________________________

## Claude's Discretion

The following were left for Claude / planner to decide:

- **Maturity → badge color mapping** — if planner adopts colored badge: gray/blue/amber/green/green+check.
- **devices.yaml schema validation** — JSON Schema / yamllint enum enforcement.
- **AKP815 maturity tier value** — `probed` if no real-device capture exists; `partial` if backend exercised end-to-end. Planner picks at execute time.
- **Atomic commit boundaries** — 4 commits (rename + regenerator, MaturityRole + sidebar, promoted-device-#2, feature_summary rollout for the 2 promoted).
- **`feature_summary` field shape** — works/partial/pending sub-lists per device. Optional; only filled for the 2 promoted devices in Phase 8.
- **DeviceModel-side maturity source** — embedded YAML resource vs C++-side descriptor field. Planner picks based on existing convention.

## Deferred Ideas

- **AKB980 PRO promotion** — out of scope (wine/innoextract not available).
- **Mass promotion of 5-6 scaffolded devices** — defer to v1.1.x / v1.2.
- **`feature_summary` for ALL devices** — only 2 promoted devices in Phase 8; rest lazy.
- **Real-hardware CI for verified tier** — needs dedicated hardware runners; out of scope for v1.1.
- **devices.yaml as published vendored package** — v1.2 idea.
- **Vendor product-page links in README** — bandwidth-cheap but URL-drift-prone; deferred.
