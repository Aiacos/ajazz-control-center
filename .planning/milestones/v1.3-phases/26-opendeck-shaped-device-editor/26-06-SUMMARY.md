---
phase: 26-opendeck-shaped-device-editor
plan: '06'
subsystem: device-layouts
tags: [layout-json, device-photo, fair-use, req-26-b, akp05e, akp05, akp153, akp03, mirabox]
dependency_graph:
  requires: [26-04]
  provides: [per-sku-layout-jsons, fair-use-readme]
  affects: [DeviceView.qml loadLayout, qrc device-layouts glob]
tech_stack:
  added: []
  patterns:
    - Per-SKU JSON layout files in resources/device-layouts/ (CONFIGURE_DEPENDS GLOB from Plan 26-04)
    - viewBox coordinate space for device-scaled cell positioning (D-08)
    - photo field referencing existing resources/devices/products/ vendor photos (D-04, D-05)
    - Outline-frame fallback preserved for AKP815 (D-13 sentinel -- no JSON)
key_files:
  created:
    - resources/device-layouts/akp05e.json
    - resources/device-layouts/akp05.json
    - resources/device-layouts/mirabox_n4.json
    - resources/device-layouts/akp153.json
    - resources/device-layouts/akp153_v1.json
    - resources/device-layouts/akp153e.json
    - resources/device-layouts/akp153e_v2.json
    - resources/device-layouts/akp153r.json
    - resources/device-layouts/akp03.json
    - resources/device-layouts/akp03_legacy.json
    - resources/device-layouts/akp03e.json
    - resources/device-layouts/akp03r.json
    - resources/device-layouts/akp03r_rev2.json
    - resources/device-layouts/mirabox_n3.json
    - resources/device-layouts/mirabox_n3e.json
    - resources/device-layouts/mirabox_n3_rev3.json
    - resources/device-layouts/mirabox_n3en.json
    - resources/device-layouts/README.md
  modified: []
decisions:
  - AKP05E JSON created first (operator-priority SKU for Phase 25 UAT Tests 1+6 re-walk)
  - All AKP05 family files reuse exact akp05e.json geometry (identical chassis)
  - All AKP153 family files use 3x5 grid in 480x320 viewBox; akp153r kept at 15 keys (3x6 discrepancy in backlog)
  - All AKP03 family + Mirabox N3 files use 2x3 grid + 3 encoders in 400x320 viewBox
  - Mirabox N3 variants reuse product-akp03.png (no dedicated Mirabox N3 photo in tree)
  - AKP815 deliberately has NO layout JSON (D-13 sentinel; outline-frame fallback active)
  - README placed at resources/device-layouts/README.md (D-05 path override per plan; covers both layout JSONs and existing resources/devices/products/ photos)
  - mdformat pre-commit dance applied to README.md (re-staged after hook reformatted; standard CLAUDE.md dance)
metrics:
  duration: 8 minutes
  completed: '2026-05-28T17:40:00Z'
  tasks_completed: 3
  files_changed: 18
---

# Phase 26 Plan 06: Per-SKU Device Layout JSONs + Fair-use Attribution README Summary

17 per-SKU layout JSON files for all in-scope LCD-key AJAZZ and Mirabox SKUs, plus a
fair-use attribution README; AKP05E created first so the operator can immediately
re-walk Phase 25 UAT Tests 1+6 against the live demo unit.

## What Was Built

### Task 1 -- AKP05E layout JSON + attribution README (operator-priority SKU)

**resources/device-layouts/akp05e.json:**

- viewBox 600x400 (landscape); 10 keys in 2x5 grid (i 1..10); 4 encoder dials;
  4 touch zones
- photo references existing `product-akp05e.png`
- Cell positions: key grid rows at y=60 and y=152 (92px row spacing); encoder row
  at y=260; touch zones at y=330; 76px key cells with 92px column pitch

**resources/device-layouts/README.md:**

- Documents fair-use posture for vendor product photos in `resources/devices/products/`
- Per-SKU attribution table (AJAZZ / Mirabox as copyright holders)
- Four-point fair-use rationale (purpose, nature, amount, market effect)
- Replacement policy: remove `photo` field -> DeviceView outline-frame fallback (D-07)
- mdformat reformatted on first commit attempt; re-staged and committed (standard dance)

### Task 2 -- AKP05 family + AKP153 family layout JSONs (7 files)

**AKP05 family (3 files):**

- `akp05.json` -- identical geometry to akp05e.json; codename `akp05`; photo `product-akp05.png`
- `mirabox_n4.json` -- identical geometry; codename `mirabox_n4`; photo `product-mirabox_n4.png`
- Both: 10 keys (2x5) + 4 encoders + 4 touch zones in 600x400 viewBox

**AKP153 family (5 files):**

- `akp153.json`, `akp153_v1.json`, `akp153e.json`, `akp153e_v2.json`, `akp153r.json`
- 15 keys in 3x5 grid (i 1..15); `encoders: []`; `touchZones: []`; 480x320 viewBox
- All 5 files reference `product-akp153.png` (closest analog for all variants)
- akp153r: kept at 15 keys (3x5); 3x6 discrepancy from RESEARCH.md stays in open backlog

### Task 3 -- AKP03 family + Mirabox N3 variants layout JSONs (9 files)

**AKP03 family (5 files):**

- `akp03.json`, `akp03_legacy.json`, `akp03e.json`, `akp03r.json`, `akp03r_rev2.json`
- 6 keys in 2x3 grid (i 1..6); 3 encoder dials (i 0..2); `touchZones: []`; 400x320 viewBox
- All 5 files reference `product-akp03.png`

**Mirabox N3 family (4 files):**

- `mirabox_n3.json`, `mirabox_n3e.json`, `mirabox_n3_rev3.json`, `mirabox_n3en.json`
- Same geometry as AKP03 family (same chassis profile); reuses `product-akp03.png`
  (no dedicated Mirabox N3 product photo in the existing `resources/devices/products/` tree)

**AKP815 sentinel:** No `akp815.json` created (D-13; outline-frame fallback continues to render it).

## Deviations from Plan

### Auto-addressed Issues

**1. [Rule 1 - Pre-commit dance] mdformat reformatted README.md**

- **Found during:** Task 1 commit
- **Issue:** mdformat pre-commit hook reformatted `resources/device-layouts/README.md`
  (ordered list markers normalized from `1. 2. 3.` to `1. 1. 1.`)
- **Fix:** Re-staged the reformatted file and committed on the second attempt
- **Files modified:** resources/device-layouts/README.md
- **Commit:** c0f2d1f (includes the reformatted README)

## Threat Surface Scan

No new network endpoints, auth paths, or file access patterns introduced. The JSON files
are static qrc resources consumed by DeviceView.loadLayout() which already wraps JSON.parse
in try/catch (T-26-20 mitigate, shipped in Plan 26-04). The fair-use README documents the
vendor photo IP posture (T-26-21 mitigate). No files outside `resources/device-layouts/`
were modified.

## Commits

| Task   | Commit  | Message                                                            |
| ------ | ------- | ------------------------------------------------------------------ |
| Task 1 | c0f2d1f | feat(26-06): land AKP05E layout JSON + fair-use attribution README |
| Task 2 | c6ea5b1 | feat(26-06): land AKP05 family + AKP153 family layout JSONs        |
| Task 3 | b6e123a | feat(26-06): land AKP03 family + Mirabox N3 variant layout JSONs   |

## Verification Results

```
ls resources/device-layouts/*.json | wc -l: 17 (PASS - all 17 in-scope SKUs)
test ! -f resources/device-layouts/akp815.json: PASS (D-13 sentinel respected)
python3 -m json.tool on all 17 files: all exit 0 (valid JSON)
grep -nP "[^[:ascii:]]" on all 17 files + README.md: 0 lines (ASCII-only)
grep -ci 'fair use' resources/device-layouts/README.md: 2 (>= 1 required)
grep -c '"codename": "akp05"' akp05.json: 1 (exact, not akp05e)
grep -c '"i": 15' akp153.json: 1 (15 keys present)
grep -c '"encoders": []' akp153.json: 1 (empty array)
grep -c '"i": 6' akp03.json: 1 (6 keys present)
grep -c '"touchZones": []' akp03.json: 1 (empty array)
cmake --build --preset linux-release: exit 0 (main repo build clean)
ctest --preset linux-release -E qml --output-on-failure: 648/648 PASSED
```

## Self-Check: PASSED

All 17 JSON files confirmed on disk. README.md confirmed on disk.
All 3 task commits confirmed in git log (c0f2d1f, c6ea5b1, b6e123a).
AKP815.json absent confirmed.
648/648 ctest passes confirmed.
