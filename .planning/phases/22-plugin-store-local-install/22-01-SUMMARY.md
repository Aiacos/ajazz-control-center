---
phase: 22-plugin-store-local-install
plan: '01'
subsystem: plugin-store-verify-gate
tags:
  - plugin-verify-gate
  - manifest-signer
  - ed25519
  - plugin-14
  - cod-031
  - trust-boundary
dependency_graph:
  requires: [18-04]
  provides: [plugin_verify_gate, verifyStagedPlugin, VerifyVerdict, verdictToTrustLevel]
  affects: [22-02-local-install, plugin_catalog_model, launch-sweep]
tech_stack:
  added: []
  patterns:
    - fail-closed-verify-gate (verifyStagedPlugin returns Refused when verifier absent)
    - cmake-unconditional-link (ajazz::plugins un-gated from AJAZZ_BUILD_PYTHON_HOST)
    - post-sweep-verify-seam (launch sweep verify added after extractor call, no extractor changes)
key_files:
  created:
    - src/app/src/plugin_verify_gate.hpp
    - src/app/src/plugin_verify_gate.cpp
    - tests/unit/test_plugin_verify_gate.cpp
  modified:
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/qml/CMakeLists.txt
    - src/app/src/plugin_catalog_model.cpp
decisions:
  - 'CMake un-gate: ajazz::plugins link + AJAZZ_PLUGIN_VERIFIER_SCRIPT/AJAZZ_PLUGIN_TRUST_ROOTS moved out of AJAZZ_BUILD_PYTHON_HOST guard; host-only defs (AJAZZ_PYTHON_HOST, HOST_SCRIPT, PYTHONPATH) remain inside the guard. Reason: signer calls python at runtime, does not need the build-time host flag.'
  - 'verifyStagedPlugin seam: optional configOverride param (default {} = use makeSignerConfig()). Test injects config with non-empty verifierScript pointing at /nonexistent-path to exercise fail-closed; production callers use default.'
  - 'Launch-sweep verify seam: added post-extractor scan in constructor body (not in sdplugin_extractor internals); scans .sdPlugin dirs after extractStandalonePluginArchives completes and removeRecursively() any Refused package.'
  - QML smoke test target (tests/qml) also needs ajazz::plugins link and verifier/trust-root defs since plugin_verify_gate.cpp is in ACC_QML_MODULE_SOURCES.
  - SelfSigned is allowed in network install() path (same as plan; sideload confirm UX is plan 02 concern); logged via verdictToTrustLevel for auditability.
metrics:
  duration_minutes: 10
  completed_date: '2026-05-24'
  tasks_completed: 2
  files_created: 3
  files_modified: 4
  tests_added: 7
  test_suite_total: 485
---

# Phase 22 Plan 01: Plugin Verify Gate Summary

**One-liner:** Fail-closed Ed25519 verify gate (verifyStagedPlugin) wired unconditionally
to app build; tampered/unsigned packages refused by network install() and launch sweep.

## STOP Gate Result

Phase 18 dependency SATISFIED: `18-04-SUMMARY.md` exists and was read.

**Phase-18 on-disk layout (for plan 02's promote target):**

- Discovery scans `<pluginsDir>/<x>.sdPlugin/manifest.json` where
  `pluginsDir = QStandardPaths::AppDataLocation/plugins/`
- `extractStandalonePluginArchives` converts leftover `*.sdPlugin` archives to expanded dirs
- Plugin key = `manifest.codePath` (or `manifest.name` as fallback); Phase 19 refines to catalog UUID
- The verify gate must point `verifyStagedPlugin` at `<pluginsDir>/<id>.sdPlugin/manifest.json`
  (the same path Phase-18 PluginManager discovers)

## What Was Built

### Task 1: Verify gate + CMake un-gate

**`src/app/src/plugin_verify_gate.hpp`:**

- `enum class VerifyVerdict { Trusted, SelfSigned, Refused }`
- `struct VerifyOutcome { verdict, publisherName, reason }`
- `makeSignerConfig()` - builds ManifestSignerConfig from AJAZZ_PLUGIN_VERIFIER_SCRIPT /
  AJAZZ_PLUGIN_TRUST_ROOTS compile defs; both paths empty when defs absent (fail-closed)
- `verifyStagedPlugin(manifestPath, configOverride={})` - fail-closed gate; returns Refused
  when verifierScript empty; else calls verifyManifest and collapses to three-way verdict
- `verdictToTrustLevel(verdict)` - maps to "trusted"/"self-signed"/"unsigned" (same
  vocabulary as LoadedPluginsModel::trustLevelOf)

**`src/app/src/plugin_verify_gate.cpp`:**

- Implements the gate with the fail-closed branch first
- No nlohmann includes (COD-031 compliant)

**CMake changes (`src/app/CMakeLists.txt`):**

- `target_link_libraries(ajazz-control-center PRIVATE ajazz::plugins)` moved outside
  `if(AJAZZ_BUILD_PYTHON_HOST)` guard
- `AJAZZ_PLUGIN_VERIFIER_SCRIPT` and `AJAZZ_PLUGIN_TRUST_ROOTS` defs moved outside guard
- `plugin_verify_gate.{cpp,hpp}` added to unconditional QML module SOURCES list
- `AJAZZ_PYTHON_HOST`, `AJAZZ_PLUGIN_HOST_SCRIPT`, `AJAZZ_PLUGIN_PYTHONPATH` remain inside guard

**Test CMakeLists (`tests/unit/CMakeLists.txt`):**

- `test_plugin_verify_gate.cpp` + `plugin_verify_gate.cpp` registered
- Per-file COMPILE_DEFINITIONS inject AJAZZ_TEST_REPO_ROOT + verifier/trust-root paths

### Task 2: Unit suite + existing path gating

**`tests/unit/test_plugin_verify_gate.cpp`** (7 TEST_CASEs, all green):

| Test                                   | Expected      | Result |
| -------------------------------------- | ------------- | ------ |
| verdictToTrustLevel: Trusted           | "trusted"     | PASS   |
| verdictToTrustLevel: SelfSigned        | "self-signed" | PASS   |
| verdictToTrustLevel: Refused           | "unsigned"    | PASS   |
| signer unavailable -> Refused no crash | Refused       | PASS   |
| signed manifest -> SelfSigned          | SelfSigned    | PASS   |
| tampered manifest -> Refused           | Refused       | PASS   |
| unsigned manifest -> Refused           | Refused       | PASS   |

**`src/app/src/plugin_catalog_model.cpp` - network install() gate:**

- After `extractSdPluginArchive` succeeds, calls `verifyStagedPlugin(archiveDir/archiveName/manifest.json)`
- If Refused: `QDir(extractedDir).removeRecursively()` then `emit installFinished(uuid, false, reason)` and returns
- SelfSigned/Trusted: proceeds to `installed=true` (plan 02 will add confirm UX for self-signed)

**`src/app/src/plugin_catalog_model.cpp` - launch sweep gate:**

- Post-`extractStandalonePluginArchives` verify scan in constructor body
- Scans all `*.sdPlugin` directories under pluginsDir
- Any whose `manifest.json` verdict is Refused: `removeRecursively()` (quarantine)
- Seam: no changes to `sdplugin_extractor` internals

**`tests/qml/CMakeLists.txt`:**

- Added `ajazz::plugins` link (plugin_verify_gate.cpp is in ACC_QML_MODULE_SOURCES)
- Added AJAZZ_PLUGIN_VERIFIER_SCRIPT / AJAZZ_PLUGIN_TRUST_ROOTS compile defs

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] QML smoke test target missing ajazz::plugins link**

- **Found during:** Task 2 full build (`cmake --build --preset linux-release`)
- **Issue:** `plugin_verify_gate.cpp` is in `ACC_QML_MODULE_SOURCES` so the QML smoke test
  target (`ajazz_qml_tests`) recompiles it via `_qmlt_sources`, but the target did not link
  `ajazz::plugins`. Linker error: "undefined reference to `ajazz::plugins::verifyManifest`"
- **Fix:** Added `ajazz::plugins` to `tests/qml/CMakeLists.txt` `target_link_libraries`
  - added the verifier/trust-root compile defs to match the app target's configuration
- **Files modified:** `tests/qml/CMakeLists.txt`

**2. [Rule 1 - Bug] Unused variable warning in initial verify gate implementation**

- **Found during:** Task 1 first build
- **Issue:** `bool const useOverride = ...` declared but immediately superseded by cleaner logic
- **Fix:** Simplified `verifyStagedPlugin` to use a single ternary expression; removed the
  over-engineered design commentary
- **Files modified:** `src/app/src/plugin_verify_gate.cpp`

**3. [Rule 3 - Blocking] `QZipWriter` include path wrong in test**

- **Found during:** Task 2 build (test binary)
- **Issue:** `#include <QZipWriter>` fails; correct include is `<private/qzipwriter_p.h>`
  (same pattern as `test_sdplugin_extractor.cpp`)
- **Fix:** Updated include in `test_plugin_verify_gate.cpp`

**4. [Rule 1 - Bug] Ambiguous `std::filesystem::path` assignment from `{}`**

- **Found during:** Task 2 build
- **Issue:** `emptyCfg.trustedPublishersFile = {}` is ambiguous (three operator= candidates)
- **Fix:** Changed to `emptyCfg.trustedPublishersFile = fs::path{}`

## Verification Checks

- `ctest --preset linux-release -R "PluginVerifyGate"` — 7/7 PASS
- `cmake --build --preset linux-release --target ajazz-control-center` — PASS
- `cmake --build --preset linux-release` (full build incl. QML tests) — PASS
- `grep -rn '#include.*nlohmann' plugin_verify_gate.*` — 0 (COD-031)
- `grep -c verifyStagedPlugin plugin_verify_gate.hpp` — 3 (declared, documented, implemented)
- `test -f 18-04-SUMMARY.md` — EXISTS (STOP gate passed)
- `AJAZZ_PLUGIN_VERIFIER_SCRIPT` defined outside AJAZZ_BUILD_PYTHON_HOST guard — CONFIRMED

## COD-031 Verification

- `plugin_verify_gate.hpp`: includes only `<ajazz/plugins/manifest_signer.hpp>` + `<QString>`. No nlohmann.
- `plugin_verify_gate.cpp`: includes only the gate header + logger + `<QString>`. No nlohmann.
- `plugin_catalog_model.cpp` modifications: added `#include "plugin_verify_gate.hpp"` only. No nlohmann.

## Known Stubs

None. The verify gate is fully functional:

- `makeSignerConfig()` resolves real paths from compile defs
- `verifyStagedPlugin` calls the real Ed25519 verifier subprocess
- `verdictToTrustLevel` returns production strings

The only known limitation is `trusted_publishers.json` ships a placeholder key (Pitfall 4 from
RESEARCH): every validly-signed plugin resolves as SelfSigned, never Trusted. This is expected
and documented — the gate is functional; the trust-root population is a deployment task.

## Threat Flags

No new surface beyond the plan's registered threat register. All T-22-\* mitigations applied:

| Flag          | File                     | Mitigation Applied                                         |
| ------------- | ------------------------ | ---------------------------------------------------------- |
| T-22-tamper   | plugin_verify_gate.cpp   | Ed25519 via verifyManifest -> valid=false -> Refused       |
| T-22-bypass   | src/app/CMakeLists.txt   | ajazz::plugins link un-gated; fail-closed when defs absent |
| T-22-backdoor | plugin_catalog_model.cpp | Both network install() + launch sweep routed through gate  |

## Self-Check: PASSED

Files exist:

- src/app/src/plugin_verify_gate.hpp: EXISTS
- src/app/src/plugin_verify_gate.cpp: EXISTS
- tests/unit/test_plugin_verify_gate.cpp: EXISTS

Commits exist:

- c5f785e: feat(22-01): add plugin_verify_gate + un-gate ajazz::plugins link (PLUGIN-14)
- 600df88: feat(22-01): wire verify gate into network install() + launch sweep (PLUGIN-14)
