---
phase: 08-scaffolded-device-wiring
plan: 01
type: summary
status: complete
commit: be5250d
requirements_closed: [DEVICES-01]
---

# Plan 08-01 — rename status→maturity + 5-tier vocabulary

## Commit + diffstat

**Commit:** `be5250d feat(devices): rename status->maturity + expand to 5-tier vocabulary (Phase 8 DEVICES-01)`

```
 README.md                      |  2 +-
 docs/_data/devices.yaml        | 67 +++++++++++++++++++++---------------------
 docs/wiki/Supported-Devices.md |  2 +-
 scripts/generate-docs.py       | 39 +++++++++++++-----------
 4 files changed, 58 insertions(+), 52 deletions(-)
```

## What landed

- `docs/_data/devices.yaml` field renamed `status` → `maturity`; the 5-tier enum (`scaffolded` / `probed` / `partial` / `functional` / `verified`) is populated on every catalogued device row.
- `scripts/generate-docs.py` updated to read `dev['maturity']` and emit the 5-tier legend in the AUTOGEN tables.
- README.md and `docs/wiki/Supported-Devices.md` regenerated through the pre-commit AUTOGEN hook so the rendered vocabulary matches.

## Requirements closed

- **DEVICES-01** — `maturity` field with 5-tier vocabulary populated across all catalogued devices.
