---
phase: 08-scaffolded-device-wiring
plan: 03
type: summary
status: complete
commit: 5bef983
requirements_closed: [DEVICES-03]
---

# Plan 08-03 — feature_summary block + per-family prose

## Commit + diffstat

**Commit:** `5bef983 feat(devices): feature_summary block + emit works/partial/pending prose (Phase 8 DEVICES-03)`

```
 README.md                      |  6 +++---
 docs/_data/devices.yaml        | 28 ++++++++++++++++++++++++----
 docs/wiki/Home.md              |  2 +-
 docs/wiki/Supported-Devices.md | 10 +++++-----
 scripts/generate-docs.py       | 19 ++++++++++++++++++-
 5 files changed, 51 insertions(+), 14 deletions(-)
```

## What landed

- `docs/_data/devices.yaml` gains a `feature_summary` block per device with `works` / `partial` / `pending` arrays — the source-of-truth for what each device honestly supports today.
- `scripts/generate-docs.py` emits the prose ("Works: …; Partial: …; Pending: …") into the AUTOGEN block of README.md, `docs/wiki/Home.md`, and `docs/wiki/Supported-Devices.md`.
- Users now see honest capability advertisement at the README / wiki surface (no over-claiming).

## Requirements closed

- **DEVICES-03** — README regeneration includes per-family "what works / what doesn't" prose populated from YAML.
