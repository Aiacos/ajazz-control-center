---
phase: 08-scaffolded-device-wiring
type: phase-summary
status: complete
plans_total: 4
plans_complete: 4
commits:
  - be5250d (Plan 08-01) feat(devices): rename status->maturity + 5-tier vocabulary (DEVICES-01)
  - 77d5efb (Plan 08-02) feat(app): MaturityRole + sidebar tooltip (DEVICES-02)
  - 5bef983 (Plan 08-03) feat(devices): feature_summary block + prose emission (DEVICES-03)
  - 7d114fd (Plan 08-04) docs(streamdeck): Mirabox N3 cross-reference doc (DEVICES-04 #2)
  - 62da68c (Plan 08-04 #1) feat(streamdeck): AKP815 backend (pre-milestone, in-scope)
requirements_closed: [DEVICES-01, DEVICES-02, DEVICES-03, DEVICES-04]
---

# Phase 8 — Scaffolded-Device Wiring (rollup)

## Outcome

Users now see honest maturity tiers (`scaffolded` / `probed` / `partial` / `functional` / `verified`) per device across the catalogue YAML, the rendered README + wiki, and the QML sidebar. Two Stream-Dock siblings cleared off Tier 0: AKP815 (→ `probed`) and Mirabox N3 (→ `partial`).

## Tests

178/178 ctest pass — Phase 8 additions are purely additive (new role, new YAML field, new prose), no regressions.

## Tech debt carried forward

- Codename→maturity table in `device_model.cpp` is hand-written — promote to Qt resource + runtime YAML parse if the catalogue grows.
- MaturityRole tooltip visibility: NOT VERIFIED on real hardware (Linux dev box).
- AKP815 + Mirabox N3 promotion to `partial` / `functional` blocked on real-device captures.

## Requirements closed

- **DEVICES-01..04** — all four ticked in `.planning/REQUIREMENTS.md` traceability table.
