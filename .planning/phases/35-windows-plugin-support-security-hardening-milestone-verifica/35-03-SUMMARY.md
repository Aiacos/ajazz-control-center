---
phase: 35-windows-plugin-support-security-hardening-milestone-verifica
plan: 03
subsystem: ui
tags: [plugins, windows, classification, qml, chip, catch2, verif-01, debug-channel]

# Dependency graph
requires:
  - phase: 35-windows-plugin-support-security-hardening-milestone-verifica (Plan 01)
    provides: PluginInfo.winClass plain-int verdict (0/1/2) stamped at scan time
  - phase: 35-windows-plugin-support-security-hardening-milestone-verifica (Plan 02)
    provides: milestone-v2.0-modularity-audit.md (PROVISIONAL objectName section, PLGSEC verify-locks)
provides:
  - LoadedPluginsModel.platformStatus role (PlatformStatusRole) derived from PluginInfo.winClass
  - platformStatusOf() derive (1->native, 2->unsupported [wine false], 0->"") with explicit deferred-wine branch
  - LoadedPluginsPage chips: platformStatusChip + unsignedConsentChip + objectName/label on the pre-existing trustChip
  - test_loaded_plugins_model.cpp (NEW TU, registered) locking the derive + role contract
  - finalized objectName-coverage audit section + 35-HUMAN-UAT (honest deferred walks)
affects: [future WINPLG-03 Wine-launch phase, future LoadedPluginsModel host-wiring fix]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Model-role derived-string -> QML display chip (mirror TrustLevelRole/trustLevelOf end-to-end)
    - objectName + readable-label property on every display chip (mirror SettingsPage waylandCapabilityWarningChip)
    - Explicit deferred-input branch (kWineAvailable=false) so a future phase flips one input, not the derive shape

key-files:
  created:
    - tests/unit/test_loaded_plugins_model.cpp
    - .planning/phases/35-windows-plugin-support-security-hardening-milestone-verifica/35-HUMAN-UAT.md
  modified:
    - src/app/src/loaded_plugins_model.hpp
    - src/app/src/loaded_plugins_model.cpp
    - src/app/qml/LoadedPluginsPage.qml
    - tests/unit/CMakeLists.txt
    - docs/milestone-v2.0-modularity-audit.md

key-decisions:
  - platformStatusOf mirrors PluginInfo.winClass (not a re-scan); wine branch kept explicit + hard-false (kWineAvailable) so WINPLG-03 launch flips one input
  - All three chips are plain display Rectangles (no Switch/CheckBox) per the CLAUDE.md harness gap (qml.invoke does not reproduce Switch.toggled)
  - Test exercises the private-static derive THROUGH the public PlatformStatusRole via data() (also proves the role is wired) rather than befriending the test
  - Architectural finding (LoadedPluginsModel not wired to .sdPlugin inventory) recorded as a Rule-4 follow-up, NOT silently patched this phase

patterns-established:
  - Derive-through-public-role test idiom for a private-static model derive
  - Honest live-gate reconciliation: confirm what is reproducible headlessly, record blockers in HUMAN-UAT + audit doc, never fabricate

requirements-completed: [WINPLG-03, VERIF-01]

# Metrics
duration: 50min
completed: 2026-06-08
---

# Phase 35 Plan 03: WINPLG Status Chip + VERIF-01 objectNames + Live Phase Gate Summary

**A platformStatus model role derived from PluginInfo.winClass plus three objectName-addressed, headless-readable chips on LoadedPluginsPage (status + unsigned-consent + the previously un-named trustChip), unit-locked and security-invariant-confirmed — with an honestly-recorded live-render blocker (the model is not wired to .sdPlugin inventory in the shipping build).**

## Performance

- **Duration:** ~50 min
- **Started:** 2026-06-08
- **Completed:** 2026-06-08
- **Tasks:** 3 (2 code + 1 live-gate/docs checkpoint, executed sequentially)
- **Files modified:** 7 (5 modified, 2 created)

## Accomplishments

- **WINPLG-03 (chip-only):** `LoadedPluginsModel::PlatformStatusRole` (`"platformStatus"`) + `platformStatusOf()` mirroring `PluginInfo.winClass`: `1 (WsOnlyIpc) -> "native"`, `2 (VendorDll) -> "unsupported"` (wine hard-false this phase), `0 (NotWindowsOnly) -> ""`. The wine-vs-unsupported branch is explicit with a `constexpr bool kWineAvailable = false` deferral marker so a future WINPLG-03 launch phase flips one input rather than reshaping the derive. No Wine launcher built.
- **VERIF-01:** three chips on `LoadedPluginsPage.qml`, each with an `objectName` AND a headless-readable label property (mirror of `SettingsPage waylandCapabilityWarningChip`): `platformStatusChip` (`statusLabel` -> "Runs natively"/"Requires Wine"/"Unsupported on this OS"), `unsignedConsentChip` (`consentLabel` -> "Unsigned — requires consent", amber), and the pre-existing `trustChip` (added `objectName` + `trustLabel` — it had none; `grep -c objectName` was 0). All display-only (no Switch — harness gap).
- **Test lock:** new `tests/unit/test_loaded_plugins_model.cpp` (4 `[loaded-plugins-model]` cases) registered in `tests/unit/CMakeLists.txt` (links `loaded_plugins_model.cpp`); exercises the private-static derive through the public role + asserts `platformStatus` in `roleNames()`. TDD RED (compile-fail on missing role) confirmed before GREEN.
- **Live phase gate (headless, isolated):** confirmed loopback (PLGSEC-03) and the consent/tamper invariants (PLGSEC-01/02) against an isolated offscreen instance with the user's GUI untouched; recorded the chip-render walk as a deferred HUMAN-UAT item with the honest blockers.
- **Full suite green: 802/802** (785 unit/integration + 17 qml).

## Task Commits

1. **Task 1: platformStatus model role + derive unit test (TDD)** - `864ccea` (feat)
1. **Task 2: status chip + unsigned-consent chip + objectNames (VERIF-01)** - `a0319c0` (feat)
1. **Task 3: live phase gate + audit-doc finalize + 35-HUMAN-UAT** - `dd55ad7` (docs)

_Task 1 combined RED (the test that fails to compile on the missing `PlatformStatusRole`) and GREEN (the role + derive) into one atomic feat commit per project convention; RED was confirmed (compile failure on the undefined enumerator) before GREEN. Both code tasks hit the documented clang-format re-add dance (no `--no-verify`); the doc task hit the mdformat re-add dance — all re-staged + re-committed clean._

## Headless live-gate results (Task 3)

Isolated instance: fresh `XDG_RUNTIME_DIR=$(mktemp -d)` + `QT_QPA_PLATFORM=offscreen` + `AJAZZ_DEBUG_CONTROL=1`, own socket, killed by exact PID afterwards. The user's running GUI (PID 4156548) + its sidecars were never touched (single-instance socket name hashes the runtime dir, so the isolated dir yields a distinct lock).

- **PLGSEC-03 loopback — CONFIRMED (live + runtime + test).** `ajazz-debug --socket <iso> ping` -> `{"pong": true}`. `ss -ltnp` showed `LISTEN 127.0.0.1:45429` owned by the isolated PID (loopback only, never `0.0.0.0`). Code: `sd_plugin_server.cpp:93` `listen(QHostAddress::LocalHost, port)`. Tests `SdPluginServer binds loopback only - never QHostAddress::Any` + `bind loopback only on random port` green.
- **PLGSEC-01/02 — CONFIRMED (test-locked).** `tampered plugin refused even with consent`, `allowUnsignedPlugins=true tampered STILL refused (CR-01)`, `per-plugin allow survives launch-sweep with global toggle OFF` all green. Consent persistence write at `plugin_catalog_model.cpp:1072` (`plugins/allowed/<uuid>`), read by `perPluginAllowed()` (`:66`).
- **CHIP `qml.get` + screenshot — DEFERRED (honest blocker, NOT fabricated).** Navigated the isolated instance to the Loaded-plugins surface and captured a screenshot (`/tmp/acc-loaded-chips.png`, read back). The chips did NOT render and `qml.get` on `statusLabel`/`consentLabel`/`trustLabel` returned "no such property" for two independent reasons (recorded in `35-HUMAN-UAT.md`):
  1. **`LoadedPluginsModel` is not populated with `.sdPlugin` plugins in this build.** It is fed only by the Python `OutOfProcessPluginHost` (`application.cpp:956-957`, `#ifdef AJAZZ_PYTHON_HOST`); the `UnifiedPluginHost` aggregator (`m_pluginHost2`, `:1091`) that merges `.sdPlugin` inventory is constructed but never `setPluginHost`/`setPlugins`-wired to the model. The session had 0 Python plugins, so the model row count was 0 and no delegate (hence no chip) instantiated. The `.sdPlugin` sysmon plugin was registered with the SdPluginServer but never reaches `LoadedPluginsModel`.
  1. **The `loadedPluginsDrawer` (modal `Drawer`/`Popup`) cannot be opened headlessly** — `qml.invoke clicked` on `navLoaded` (ToolButton) and `qml.invoke open` on the Drawer both fail per the documented CLAUDE.md harness gap. A real windowed click is required to realize the delegate chips.

The chip source is correct (`objectName` + readable label, `row.platformStatus`/`row.trustLevel` bindings) and unit-locked; only the live windowed render is deferred. See **Threat Flags** below for the architectural follow-up.

## Files Created/Modified

- `src/app/src/loaded_plugins_model.hpp` - `PlatformStatusRole` enumerator + `platformStatusOf()` declaration with the documented mapping/deferral comment.
- `src/app/src/loaded_plugins_model.cpp` - `data()` case + `roleNames()` entry + `platformStatusOf()` switch on `info.winClass` with `kWineAvailable=false`.
- `src/app/qml/LoadedPluginsPage.qml` - `platformStatusChip` + `unsignedConsentChip` (objectName + readable label + ToolTip, display-only) and `objectName`/`trustLabel` added to the pre-existing `trustChip`.
- `tests/unit/test_loaded_plugins_model.cpp` - NEW TU, 4 `[loaded-plugins-model]` cases (native/unsupported/empty derive + role-in-roleNames).
- `tests/unit/CMakeLists.txt` - registered the new TU (links `loaded_plugins_model.cpp`; AUTOMOC + Qt6::Qml already on for the target).
- `docs/milestone-v2.0-modularity-audit.md` - finalized the (previously PROVISIONAL) objectName-coverage section to reflect the 3 chips + the honest live-render blocker.
- `.planning/phases/35-.../35-HUMAN-UAT.md` - NEW; deferred chip-render + consent-UI walks + the recorded blockers; confirmed-this-phase invariants listed.

## Decisions Made

- **Derive mirrors the cached verdict, not a re-scan.** `PluginInfo` carries no bundle path; the verdict was stamped at scan time by Plan 01. `platformStatusOf` is a pure switch on `info.winClass`.
- **Deferred-wine branch kept explicit.** `kWineAvailable` is a `constexpr bool ... = false` with an inline deferral comment; the `wine` label path is dead this phase but present so WINPLG-03 launch is a one-input flip (and the unit test pins the shape).
- **Test through the public role.** Rather than befriend the test or expose the private static, the test populates the model and reads `PlatformStatusRole` via `data()` — this also asserts the data()/roleNames() wiring, not just the derive in isolation.
- **Architectural finding surfaced, not patched.** Wiring `m_pluginHost2` (UnifiedPluginHost) into `LoadedPluginsModel` is a Rule-4 data-source change the plan did not scope (it changes the model's feed and could affect the existing trust-chip behavior); recorded as a follow-up rather than changed under a UI plan.

## Deviations from Plan

### Honest scope note (not an auto-fix)

**[Rule 4 - Architectural, surfaced not applied] `LoadedPluginsModel` is not wired to the `.sdPlugin` inventory in the shipping build.**

- **Found during:** Task 3 live gate (model row count 0; chips never instantiated).
- **Issue:** 35-PATTERNS.md A1 asserts ".sdPlugin plugins DO flow into LoadedPluginsModel" via `UnifiedPluginHost`. In the actual build, `LoadedPluginsModel::setPluginHost/setPlugins` is called ONLY with the Python `OutOfProcessPluginHost` (`application.cpp:956-957`). The `UnifiedPluginHost` (`m_pluginHost2`, `:1091`) is built but never set on the model, so `.sdPlugin` plugins — the only ones with a meaningful `winClass` — never reach the model and the WINPLG status chip cannot render in production.
- **Why not auto-fixed:** changing the model's data source is a structural change (Rule 4) outside this UI plan's scope; it could alter the existing SEC-003 trust-chip behavior and warrants an explicit decision. The chip + derive are correct and unit-locked; the gap is the feed.
- **Recorded in:** `35-HUMAN-UAT.md` (blocker 1) + the audit doc live-gate note + Threat Flags below.

Otherwise the plan executed as written. The literal verify regex `-R "loaded-plugins|LoadedPlugins"` matched by test NAME (the `[loaded-plugins-model]` tag is reflected in the ASCII titles' "LoadedPluginsModel" prefix), green as required.

## Threat Flags

| Flag                         | File                             | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| ---------------------------- | -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| threat_flag: dead-ui-surface | src/app/src/application.cpp:1091 | `UnifiedPluginHost` (the `.sdPlugin`-aggregating host) is constructed but never wired into `LoadedPluginsModel`; the WINPLG status chip + the trust chip therefore never render for `.sdPlugin` plugins in the shipping build. Not a new attack surface (display-only chips grant no capability), but a UI-correctness gap: the security verdict for `.sdPlugin` plugins is computed (winClass) yet never shown. Follow-up: wire `m_pluginHost2` -> `LoadedPluginsModel`. |

## User Setup Required

None for this plan. To complete the deferred live walks, a windowed GUI session is required (see `35-HUMAN-UAT.md`): a WS-only-IPC and a vendor-DLL win `.sdPlugin`, plus the `LoadedPluginsModel` feed fix (Threat Flags) so the chips render.

## Next Phase Readiness

- WINPLG-03 chip + derive are in place and unit-locked; the deferred Wine launch flips `kWineAvailable` in `platformStatusOf` (the only input).
- VERIF-01 source coverage is complete (3 chips, objectName + readable label). The live windowed walk + the `LoadedPluginsModel` feed fix are the only open items.
- The architectural follow-up (wire `UnifiedPluginHost` -> `LoadedPluginsModel`) is documented for a future maintenance/UI plan.

______________________________________________________________________

*Phase: 35-windows-plugin-support-security-hardening-milestone-verifica*
*Completed: 2026-06-08*

## Self-Check: PASSED

- Commits `864ccea`, `a0319c0`, `dd55ad7` present in git log.
- All created/modified files exist on disk.
- `grep -c "objectName:" src/app/qml/LoadedPluginsPage.qml` = 3 (trustChip + platformStatusChip + unsignedConsentChip).
- Full suite green: 802/802 (785 unit/integration + 17 qml).
- No Wine launcher built; `kWineAvailable` hard-false in `platformStatusOf`.
- Live gate: loopback + tamper/consent invariants confirmed (isolated instance, user GUI untouched); chip-render walk deferred to HUMAN-UAT with honest blockers (not fabricated).
