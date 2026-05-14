---
phase: 08-scaffolded-device-wiring
plan: 02
type: summary
status: complete
commit: 77d5efb
requirements_closed: [DEVICES-02]
---

# Plan 08-02 — MaturityRole + sidebar tooltip

## Commit + diffstat

**Commit:** `77d5efb feat(app): add MaturityRole to DeviceModel + sidebar tooltip (Phase 8 DEVICES-02)`

```
 src/app/qml/DeviceList.qml           |  5 +++
 src/app/qml/components/DeviceRow.qml | 16 ++++++++++
 src/app/src/device_model.cpp         | 60 ++++++++++++++++++++++++++++++++++++
 src/app/src/device_model.hpp         |  2 ++
 4 files changed, 83 insertions(+)
```

## What landed

- `DeviceModel` exposes a new `MaturityRole` keyed on codename via a hand-written codename→tier table in `device_model.cpp` (mirrors `devices.yaml` as the authoritative source).
- QML sidebar `DeviceRow` consumes the role to render a per-row maturity tooltip (badge styling consistent with v1.0 sidebar vocabulary).
- 178/178 tests still green — addition is purely additive role surface; no behavior changes to existing roles.

## Requirements closed

- **DEVICES-02** — `MaturityRole` available to QML; sidebar surfaces the tier as a tooltip.

## Tech debt noted

- Codename→tier table is a hand-written C++ map. If the catalogue grows further, promote to a Qt resource + runtime YAML parse (carried into v1.1.x backlog).
- MaturityRole tooltip visibility on real hardware: NOT VERIFIED (Linux dev box without devices).
