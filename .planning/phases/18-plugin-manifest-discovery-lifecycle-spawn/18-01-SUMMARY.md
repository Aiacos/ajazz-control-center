---
phase: 18-plugin-manifest-discovery-lifecycle-spawn
plan: '01'
subsystem: plugin-manifest
tags: [plugin-manifest, manifest-parser, elgato-v6, ajazz-extensions, plugin-06]
dependency_graph:
  requires: [17]
  provides: [PluginManifest, parsePluginManifest, manifestRunnableHere]
  affects: [18-04-plugin-manager, future-catalog-enrichment]
tech_stack:
  added: []
  patterns:
    - pure-function-parser (QJsonDocument; no side effects; returns std::optional)
    - Linux-OS-accept-policy (Assumption A1 locked; SPAWN step is the real gate)
    - QVersionNumber-version-compare (guards against string sort mis-ordering 2.9 vs 2.10)
    - AJAZZ_TEST_REPO_ROOT compile-define for fixture loading (same pattern as test_manifest_signer)
key_files:
  created:
    - src/app/src/plugin_manifest.hpp
    - src/app/src/plugin_manifest.cpp
    - tests/unit/test_plugin_manifest.cpp
    - tests/unit/fixtures/manifests/elgato_v6_keypad.json
    - tests/unit/fixtures/manifests/ajazz_ext_knob.json
    - tests/unit/fixtures/manifests/mac_only.json
    - tests/unit/fixtures/manifests/high_minver.json
  modified:
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt
decisions:
  - "Linux OS-accept policy (A1 locked): manifest with no linux OS entry is accepted on Linux; vendor manifests never emit 'linux' so strict match would block every real plugin on primary OS"
  - AJAZZ-extension fields (IsK1Pro, RunAsAdministrator, Nodejs.Version, PUUID, FSize/FFamily) are parsed silently without any validation rejection
  - 'Required key set: Name, Author, Version, SDKVersion, OS, Actions, any-CodePath-variant; all others are optional (no Category/Description required by parser unlike JSON Schema)'
  - 'FSize/FFamily Mirabox synonyms: parser prefers Elgato standard FontSize/FontFamily; falls back to FSize/FFamily only when the standard key is absent'
metrics:
  duration_minutes: 8
  completed_date: '2026-05-24'
  tasks_completed: 2
  files_created: 7
  files_modified: 2
  tests_added: 5
  test_suite_total: 461
---

# Phase 18 Plan 01: PluginManifest Parser + Gate Summary

**One-liner:** Pure QJson manifest parser for Elgato v6 schema + AJAZZ extensions (IsK1Pro/Knob/FSize/PUUID) with locked Linux OS-accept policy and QVersionNumber MinimumVersion gate.

## What Was Built

A standalone `PluginManifest` struct and two free functions:

- `parsePluginManifest(QByteArray)` — parses the Elgato Stream Deck v6 manifest schema plus AJAZZ extensions from raw JSON bytes; returns `std::optional<PluginManifest>` (nullopt on invalid JSON or missing required keys).
- `manifestRunnableHere(manifest, platform, appVer)` — applies the OS-array gate (with locked Linux-accept policy) and the `Software.MinimumVersion` gate via `QVersionNumber`.

Both are pure functions: no I/O, no Qt event loop, no WebEngine, no nlohmann (COD-031).

A `PluginManifestTest` suite of 5 Catch2 TEST_CASEs covers:

1. `rejects_macOnly_onLinux` — proves BOTH branches of the Linux-accept rule
1. `accepts elgato v6 plus ajazz extensions` — Keypad + AJAZZ field population + FSize/FFamily mapping
1. `rejects too high minimum version` — QVersionNumber gate at appVer "0.1.0" vs "99.0"
1. `returns nullopt on invalid json` — 7 sub-cases covering garbage/missing-required-keys
1. `currentPlatformString is a known value` — compile-time platform mapping

## Deviations from Plan

None - plan executed exactly as written.

## Key Decisions Made

1. **Linux OS-accept policy (A1 LOCKED):** When `platform == "linux"` and no "linux" entry exists in the OS array, `manifestRunnableHere` returns `true`. Vendor manifests only ever list "mac"/"windows". A strict match would reject every real plugin on our primary OS. The SPAWN step (18-04) checks `CodePath` existence as the real runnability gate. Code comment cites 18-RESEARCH.md Assumption A1 / Open Question 1.

1. **FSize/FFamily fallback:** Parser prefers the Elgato-standard `FontSize`/`FontFamily`; only reads `FSize`/`FFamily` (Mirabox synonyms) when the standard key is absent. This matches the vendor RE findings in akp_plugin_sdk.md §2.1.

1. **Required key set:** `Name`, `Author`, `Version`, `SDKVersion`, `OS`, `Actions`, and at least one of `{CodePath, CodePathWin, CodePathMac}` are required. All other fields (Icon, Category, Description, etc.) are optional — the parser is more lenient than the JSON schema (which also requires `UUID`, `Software`, etc.) to maximise compatibility with real-world packages.

## Test Results

| Test                                                       | Status |
| ---------------------------------------------------------- | ------ |
| PluginManifestTest rejects_macOnly_onLinux                 | PASS   |
| PluginManifestTest accepts elgato v6 plus ajazz extensions | PASS   |
| PluginManifestTest rejects too high minimum version        | PASS   |
| PluginManifestTest returns nullopt on invalid json         | PASS   |
| PluginManifestTest currentPlatformString is a known value  | PASS   |
| Full suite (461 tests)                                     | PASS   |

## COD-031 Verification

`grep -rn nlohmann src/app/src/plugin_manifest.cpp src/app/src/plugin_manifest.hpp` returns 0 lines.
`grep -c parsePluginManifest src/app/src/plugin_manifest.hpp` returns 2 (declaration + docs).

## Known Stubs

None - all fields are fully parsed and the gate logic is complete.

## Threat Flags

None introduced beyond the plan's documented threat model (T-18-MANIFEST / T-18-MANIFEST-OS).

## Self-Check: PASSED

Files exist:

- src/app/src/plugin_manifest.hpp: EXISTS
- src/app/src/plugin_manifest.cpp: EXISTS
- tests/unit/test_plugin_manifest.cpp: EXISTS
- tests/unit/fixtures/manifests/elgato_v6_keypad.json: EXISTS
- tests/unit/fixtures/manifests/ajazz_ext_knob.json: EXISTS
- tests/unit/fixtures/manifests/mac_only.json: EXISTS
- tests/unit/fixtures/manifests/high_minver.json: EXISTS

Commits exist:

- da85cb0: feat(18-01): add PluginManifest struct + parsePluginManifest + manifestRunnableHere
- 94ea135: test(18-01): add PluginManifestTest suite with fixture manifests (PLUGIN-06)
