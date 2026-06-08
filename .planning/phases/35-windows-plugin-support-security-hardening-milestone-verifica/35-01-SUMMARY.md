---
phase: 35-windows-plugin-support-security-hardening-milestone-verifica
plan: 01
subsystem: api
tags: [plugins, windows, classification, sdplugin, qt, catch2, security]

# Dependency graph
requires:
  - phase: 18-plugin-manifest-discovery-lifecycle-spawn
    provides: PluginManifest parser (os[]/codePath/codePathWin), manifestRunnableHere gate, currentPlatformString
  - phase: 30-plugin-host-modular-foundation
    provides: PluginInfo carrier (i_plugin_host.hpp), UnifiedPluginHost aggregating .sdPlugin into LoadedPluginsModel
provides:
  - WinPluginClass enum + classifyWindowsPlugin() (CodePath suffix + bounded PE-magic corroborator)
  - supportsCurrentPlatform() native-run helper (WS-only-IPC runs natively; VendorDll Windows-only, Wine deferred)
  - PluginInfo.winClass plain-int cache carrying the verdict to the UI tier without re-scan
  - scan-time classification + WINPLG-02 native-run override at the PluginManager::discover() gate
  - WINPLG-01 feasibility-spike ADR
affects: [35-02 status chip plan, future WINPLG-03 Wine launch phase]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Pure never-throwing free-function classifier mirroring manifestRunnableHere idiom
    - Bounded magic-byte bundle scan (2-byte reads, 4096-file cap, no PE parse, no execution)
    - Classify-at-scan-time + cache on runtime-populated manifest field; plain-int cross-tier carrier (COD-031-safe)

key-files:
  created:
    - tests/unit/test_win_plugin_classification.cpp
    - .planning/phases/35-windows-plugin-support-security-hardening-milestone-verifica/35-ADR-windows-plugin-classification.md
  modified:
    - src/app/src/plugin_manifest.hpp
    - src/app/src/plugin_manifest.cpp
    - src/app/src/plugin_manager.cpp
    - src/plugins/include/ajazz/plugins/i_plugin_host.hpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - Classify once at scan time in discover() (bundle dir present); cache on PluginManifest.winClass, carry through spawn() to plugins() — no lazy launch-time scan (PluginInfo has no bundle path)
  - PluginInfo.winClass is a plain int (0/1/2), not the app-tier enum, to keep the plugins-tier public header Qt/JSON-free (COD-031)
  - 'WINPLG-02 native-run is an OR-override at the gate: accept when manifestRunnableHere OR supportsCurrentPlatform; LOCKED linux-accept policy and VendorDll strict-reject both preserved'
  - Wine launch (WINPLG-03) explicitly deferred; wineDetected hard-coded false this phase with an inline deferral marker; never bundle Wine

patterns-established:
  - Bounded magic-byte corroborator overrides a mislabeled manifest (declares .js, ships .dll -> VendorDll)
  - Runtime-populated manifest cache field (winClass) alongside sourceDir for scan-time-computed verdicts

requirements-completed: [WINPLG-01, WINPLG-02]

# Metrics
duration: 35min
completed: 2026-06-08
---

# Phase 35 Plan 01: Windows Plugin Classification + Native-Run Gate Summary

**A pure classifyWindowsPlugin() (CodePath .exe/.dll suffix + bounded MZ-magic bundle scan) plus supportsCurrentPlatform() that lets WS-only-IPC win plugins run natively on Linux, with the verdict cached at scan time onto PluginInfo.winClass and a WINPLG-01 ADR.**

## Performance

- **Duration:** ~35 min
- **Started:** 2026-06-08
- **Completed:** 2026-06-08
- **Tasks:** 2
- **Files modified:** 6 (4 modified, 2 created)

## Accomplishments

- `WinPluginClass { NotWindowsOnly, WsOnlyIpc, VendorDll }` + `classifyWindowsPlugin()`: effective-code-path `.exe`/`.dll` suffix (case-insensitive) as primary signal, with a bounded `MZ`-magic bundle corroborator that reclassifies a mislabeled manifest (declares `.js`, ships a `.dll`) to `VendorDll`. Magic-bytes only (2-byte reads, 4096-file cap, no PE parse, no execution).
- `supportsCurrentPlatform()`: `WsOnlyIpc` runs natively on every platform (no Wine); `VendorDll` accepted only on Windows (Wine deferred -> false off Windows); `NotWindowsOnly` false (caller uses `manifestRunnableHere`).
- Scan-time wiring in `PluginManager::discover()`: classify where the bundle dir exists, cache on `PluginManifest.winClass`, and a WINPLG-02 native-run override (`manifestRunnableHere || supportsCurrentPlatform`) that admits WS-only win plugins on Linux while keeping VendorDll strict-rejected and the LOCKED linux-accept policy unchanged.
- `PluginInfo.winClass` (plain int) stamped by `plugins()` so the UI model (Plan 02) renders the chip with no re-scan; plugins-tier header stays Qt/JSON-free (COD-031).
- Catch2 `[win-plugin-classification]` suite (13 cases) + WINPLG-01 ADR.
- Full suite green: **798/798** (781 unit/integration + 17 qml).

## Task Commits

1. **Task 1: WinPluginClass + classifyWindowsPlugin + supportsCurrentPlatform with Catch2 suite (TDD)** - `46a5afc` (feat)
1. **Task 2: Wire classification at scan time onto PluginInfo + native-run gate; WINPLG-01 ADR** - `ee6374d` (feat)

_Task 1 combined RED (failing-link test + header decls) and GREEN (impl) into one atomic feat commit: the header declarations were a prerequisite for the test to compile, and the project convention favors one revertable change per task. RED was confirmed (link failure on undefined symbols) before GREEN._

## Files Created/Modified

- `src/app/src/plugin_manifest.hpp` - `WinPluginClass` enum (above `PluginManifest` for the in-class initializer), `classifyWindowsPlugin()` / `supportsCurrentPlatform()` declarations, runtime-populated `PluginManifest.winClass` cache field.
- `src/app/src/plugin_manifest.cpp` - classifier heuristic (suffix + bounded MZ-magic corroborator, anonymous-namespace helpers) and native-run helper, next to `manifestRunnableHere`.
- `src/app/src/plugin_manager.cpp` - scan-time `classifyWindowsPlugin` call + WINPLG-02 native-run override at the discover() gate; `plugins()` stamps `info.winClass`.
- `src/plugins/include/ajazz/plugins/i_plugin_host.hpp` - `PluginInfo.winClass` plain-int cache with documented 0/1/2 mapping.
- `tests/unit/test_win_plugin_classification.cpp` - 13 `[win-plugin-classification]` cases (suffix, PE-magic override, robustness, cross-platform support matrix).
- `tests/unit/CMakeLists.txt` - registered the new TU (reuses the already-linked `plugin_manifest.cpp`, no ODR duplicate).
- `.planning/phases/35-.../35-ADR-windows-plugin-classification.md` - WINPLG-01 ADR.

## Decisions Made

- **Classify at scan time, cache on the manifest.** `discover()` is the only place the bundle dir is reliably present for the PE scan; the verdict rides `PluginManifest.winClass` (precedent: the runtime-populated `sourceDir`) through `spawn()` into the live inventory. `plugins()` reads it — `PluginInfo` carries no bundle path, so a lazy launch-time scan was never an option anyway (locked CONTEXT decision honored).
- **Plain-int carrier across the tier boundary.** `PluginInfo.winClass` is an `int`, not the app-tier enum, to avoid pulling app/Qt types into the plugins-tier public header (COD-031).
- **Enum moved above `PluginManifest`.** A forward declaration alone could not back an in-class initializer for the `winClass` member, so the full `enum class WinPluginClass : int` definition lives above the struct.

## Deviations from Plan

None - plan executed exactly as written. The plan's literal verify regex `-R "win-plugin-classification"` filters by ctest test *name* (Catch2 registers by TEST_CASE title, not tag), so the actual run used the name-prefix regex `-R "WinPluginClassification"`; per CLAUDE.md, the real green result is trusted over the literal shell quirk. The acceptance-criterion grep `grep -c "nlohmann|#include <Q"` on the plugins-tier header initially matched the word "nlohmann" inside an explanatory doc comment; the comment was reworded so the literal gate returns 0 (no functional change — there was never an actual include).

## Issues Encountered

- First build of Task 2 failed: the forward-declared incomplete `enum class WinPluginClass` could not back a `winClass{...}` in-class initializer (`cannot convert brace-enclosed initializer list`). Resolved by moving the full enum definition above `PluginManifest` and removing the later duplicate definition.
- Pre-commit `clang-format`/`mdformat` reformatted staged files and aborted the commit on both tasks (the documented re-add dance) — re-staged + re-committed (rebuilt + re-ran the targeted suite green after the Task 2 reformat before re-committing). No `--no-verify` used.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `PluginInfo.winClass` is populated and ready for Plan 02 to derive an objectName-addressed status chip ("Runs natively" / "Requires Wine" / "Unsupported on this OS") on the loaded-plugins surface (VERIF-01 live-drive).
- WINPLG-03 Wine launch remains deferred (documented in the ADR); the `wineDetected = false` marker in `supportsCurrentPlatform` is the wiring point for a future hardware-gated phase.
- Live debug-channel verification of the end-to-end native-run of a real WS-only win `.sdPlugin` is a Plan 02 / phase-gate item (this plan's deliverables are backend + ADR; no QML/UI surface was added here).

______________________________________________________________________

*Phase: 35-windows-plugin-support-security-hardening-milestone-verifica*
*Completed: 2026-06-08*

## Self-Check: PASSED

- Commits `46a5afc`, `ee6374d` present in git log.
- All created/modified files exist on disk.
- ADR contains "WS-only-IPC".
- Full suite green: 798/798 (781 unit/integration + 17 qml).
- COD-031 clean: 0 `#include` of nlohmann in `src/core/include/`; plugins-tier header `grep -c "nlohmann|#include <Q"` = 0.
- No Wine launcher built; LOCKED linux-accept exception preserved.
