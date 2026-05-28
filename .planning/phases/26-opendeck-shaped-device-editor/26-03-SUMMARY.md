---
phase: 26-opendeck-shaped-device-editor
plan: '03'
subsystem: core-schema + profile-controller
tags: [profile, schema-v2, touch-zones, serializer, q-invokable, cod-031]
dependency_graph:
  requires: [26-01]
  provides: [TouchZoneBinding struct, Profile::touchZones map, profileToJson v2, profileFromJson v1-migrate, ProfileController::commitTouchZoneBinding]
  affects: [src/core/include/ajazz/core/profile.hpp, src/core/src/profile.cpp, tests/unit/test_profile_serialization.cpp, src/app/src/profile_controller.hpp, src/app/src/profile_controller.cpp]
tech_stack:
  patterns: [hand-rolled JSON (COD-031), Q_INVOKABLE QML bridge, Catch2 unit tests]
key_files:
  created: []
  modified:
    - src/core/include/ajazz/core/profile.hpp
    - src/core/src/profile.cpp
    - tests/unit/test_profile_serialization.cpp
    - src/app/src/profile_controller.hpp
    - src/app/src/profile_controller.cpp
decisions:
  - D-11: TouchZoneBinding is a NEW struct (not reusing Binding) with onTap + state; stored in Profile::touchZones map keyed by uint8_t zone index
  - D-12: Schema bumps to v2 (writer always emits _schemaVersion:2 + touchZones block); reader defaults to v1 (empty touchZones) when _schemaVersion is absent
  - T-26-07: Unknown _schemaVersion >= 2 treated as v2 (forward compat — read touchZones if present, ignore unknown keys)
  - T-26-09: commitTouchZoneBinding validates zoneIndex in [0, 255]; AJAZZ_LOG_WARN + early return on violation
metrics:
  duration_seconds: 564
  completed_date: '2026-05-28'
  tasks_completed: 3
  tasks_total: 3
  files_modified: 5
  tests_added: 2
  test_baseline: 645
  test_final: 647
requirements:
  - REQ-26-B
---

# Phase 26 Plan 03: Profile Schema v2 + commitTouchZoneBinding Summary

**One-liner:** TouchZoneBinding struct + schema v2 auto-migrate (D-11/D-12) with hand-rolled serialiser (COD-031) + QML-callable commitTouchZoneBinding (REQ-26-B).

## What Was Built

### Task 1 — TouchZoneBinding struct + Profile::touchZones (D-11)

Added `struct TouchZoneBinding { std::vector<Action> onTap; KeyState state; }` immediately after `EncoderBinding` in `profile.hpp`. Added `std::unordered_map<std::uint8_t, TouchZoneBinding> touchZones` to the `Profile` struct, placed after `encoders` and before `mouseButtons`. The `uint8_t` key matches `DeviceDescriptor::touchZoneCount` (also `uint8_t`).

**Commit:** `a5a3f61`

### Task 2 — Schema v2 serialiser + v1 migration + tests (D-12)

Added `writeTouchZoneBinding` helper to `profile.cpp` following the exact `writeEncoderBinding` shape (hand-rolled `std::ostringstream`, COD-031 compliant). Updated `profileToJson` to:

- Emit `"_schemaVersion":2` immediately after `"id"` (before `"name"`)
- Emit `"touchZones":{...}` block after `"encoders":{...}`

Updated `profileFromJson` to:

- Read `_schemaVersion` from root JSON; defaults to 1 when absent
- Parse `"touchZones"` only when `schemaVersion >= 2` (v1 profiles get empty map)
- Unknown `_schemaVersion >= 2` treated as v2 (T-26-07 forward compat)

Added two new Catch2 tests:

- `"v1 profile migrates touchZones to empty on load"` — v1 JSON (no `_schemaVersion`) loads with empty touchZones; re-serialised output includes `_schemaVersion:2` + `touchZones:{}` key
- `"v2 profile touchZones round-trip"` — Profile with zones 0 and 3 serialises and restores identically (size, keys, onTap chain, state.text)

All 647 unit tests pass (645 baseline + 2 new).

**Commit:** `67ffdfd`

### Task 3 — ProfileController::commitTouchZoneBinding Q_INVOKABLE (REQ-26-B)

Added `Q_INVOKABLE void commitTouchZoneBinding(int zoneIndex, QString const& iconPath, QString const& label, int actionKind, QString const& settingsJson)` declaration and implementation. Mirrors `commitKeyBinding` exactly:

- Range-validates `zoneIndex` in `[0, 255]` (`uint8_t` range; T-26-09 mitigation)
- Range-validates `actionKind` against `kMaxActionKind = static_cast<int>(ActionKind::BackToParent)`
- Mutates `m_profile.touchZones[static_cast<uint8_t>(zoneIndex)]` — state.imagePath, state.text, onTap chain
- Emits `profileChanged()`

App target (`ajazz-control-center`) builds clean. 647/647 unit tests pass.

**Commit:** `8e70378`

## Deviations from Plan

None — plan executed exactly as written.

## Threat Mitigations Applied

| Threat                                 | Mitigation                                                                                                                                |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| T-26-07 (unknown \_schemaVersion)      | Reader treats any schemaVersion >= 2 as v2: reads touchZones if present, ignores other unknown root keys                                  |
| T-26-09 (out-of-range zoneIndex)       | commitTouchZoneBinding validates [0, 255]; AJAZZ_LOG_WARN + early return                                                                  |
| T-26-10 (nlohmann::json in ajazz_core) | COD-031 verified: writeTouchZoneBinding uses std::ostringstream; grep -rn 'nlohmann::json' src/core/src/profile.cpp returns only comments |

## Acceptance Criteria Verification

| Criterion                                                                                     | Result                 |
| --------------------------------------------------------------------------------------------- | ---------------------- |
| `grep -c 'struct TouchZoneBinding' src/core/include/ajazz/core/profile.hpp` >= 1              | 1                      |
| `grep -c 'touchZones' src/core/include/ajazz/core/profile.hpp` >= 1                           | 1                      |
| `grep -c 'writeTouchZoneBinding' src/core/src/profile.cpp` >= 2                               | 2                      |
| `grep -c '_schemaVersion' src/core/src/profile.cpp` >= 2                                      | pass                   |
| `grep -c 'commitTouchZoneBinding' src/app/src/profile_controller.hpp` = 1                     | 2 (decl + doc comment) |
| `grep -c 'ProfileController::commitTouchZoneBinding' src/app/src/profile_controller.cpp` >= 1 | 1                      |
| `grep -rn nlohmann src/core/include/` returns 0 import lines (comments only)                  | PASS                   |
| `cmake --build --preset linux-release` exits 0 (excluding pre-existing QML link issue)        | PASS                   |
| `ctest --preset linux-release -E qml --output-on-failure` 100% pass                           | 647/647 PASS           |
| `ctest` new migration test passes                                                             | PASS (Test #308)       |
| `ctest` new round-trip test passes                                                            | PASS (Test #309)       |
| ASCII-only test names                                                                         | PASS                   |

## Known Stubs

None — all functionality is fully implemented and tested.

## Threat Flags

None — no new network endpoints, auth paths, or trust boundaries introduced.

## Self-Check: PASSED

Files verified to exist:

- `src/core/include/ajazz/core/profile.hpp` — contains TouchZoneBinding struct
- `src/core/src/profile.cpp` — contains writeTouchZoneBinding
- `tests/unit/test_profile_serialization.cpp` — contains both new TEST_CASEs
- `src/app/src/profile_controller.hpp` — contains commitTouchZoneBinding declaration
- `src/app/src/profile_controller.cpp` — contains commitTouchZoneBinding implementation

Commits verified:

- `a5a3f61` feat(26-03): add TouchZoneBinding struct + Profile::touchZones map (D-11)
- `67ffdfd` feat(26-03): schema v2 serialiser + touchZones + v1 migration tests (D-12)
- `8e70378` feat(26-03): add ProfileController::commitTouchZoneBinding Q_INVOKABLE (REQ-26-B)
