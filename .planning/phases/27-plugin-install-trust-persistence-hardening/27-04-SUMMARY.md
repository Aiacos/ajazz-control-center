---
phase: 27-plugin-install-trust-persistence-hardening
plan: '04'
subsystem: plugin-trust-ux
tags: [trust, plugin-install, qml, ux, security, cr-01]
dependency_graph:
  requires: [27-01]
  provides: [PLUGIN-16-trust-ux]
  affects: [src/app/src/plugin_catalog_model, src/app/qml/LoadedPluginsPage]
tech_stack:
  added: []
  patterns:
    - QSettings-persisted Q_PROPERTY (mirrors onlineCatalogEnabled pattern)
    - consentToUnsigned() member predicate (env-var OR setting)
    - CR-01 tampered-never-consentable guard in allowPlugin + installFromFile
key_files:
  created: []
  modified:
    - src/app/src/plugin_catalog_model.hpp
    - src/app/src/plugin_catalog_model.cpp
    - src/app/qml/LoadedPluginsPage.qml
    - tests/unit/test_plugin_install_from_file.cpp
decisions:
  - allowUnsignedPlugins setting uses consentToUnsigned() member predicate (env-var OR setting) at single call site in Unsigned branch only
  - untrustedPluginsAllowed() free function removed (superseded by consentToUnsigned())
  - allowPlugin() re-verifies the manifest via verifyStagedPlugin() before recording consent — avoids trusting stale state
  - Allow action in QML uses visible=false (absent) not disabled=true for tampered rows — CR-01 requires no UI path, not just no affordance
metrics:
  duration: ~25 minutes
  completed: '2026-05-31'
  tasks_completed: 2
  tasks_total: 2
  files_changed: 4
requirements: [PLUGIN-16]
---

# Phase 27 Plan 04: Trust UX — allowUnsignedPlugins Setting + Per-Plugin Allow Action

**One-liner:** QSettings-persisted allowUnsignedPlugins toggle + allowPlugin() Q_INVOKABLE replace the env var as the primary unsigned-install consent path, with distinct "tampered" chip and zero UI consent path for Ed25519-invalid packages (CR-01 reinforced).

## Tasks Completed

| Task | Name                                                                        | Commit  | Key Files                                                         |
| ---- | --------------------------------------------------------------------------- | ------- | ----------------------------------------------------------------- |
| 1    | App-level allowUnsignedPlugins setting + per-plugin allowPlugin Q_INVOKABLE | b591a5a | plugin_catalog_model.{hpp,cpp}, test_plugin_install_from_file.cpp |
| 2    | Trust UX in LoadedPluginsPage.qml                                           | 77b3586 | LoadedPluginsPage.qml                                             |

## What Was Built

### Task 1: C++ trust policy

**`allowUnsignedPlugins` Q_PROPERTY** added to `PluginCatalogModel`:

- Mirrors `onlineCatalogEnabled` exactly: READ/WRITE/NOTIFY + `m_allowUnsignedPlugins` loaded from `QSettings("plugins/allowUnsignedPlugins", false)` in the ctor.
- `setAllowUnsignedPlugins(bool)` writes to QSettings and emits `allowUnsignedPluginsChanged()`.

**`consentToUnsigned()` member predicate** — single source of truth for unsigned consent:

- Returns `m_allowUnsignedPlugins || !qEnvironmentVariable("AJAZZ_ALLOW_UNTRUSTED_PLUGINS").isEmpty()`
- Called only in the `VerifyVerdict::Unsigned` branch (installFromFile and launch sweep).
- The `untrustedPluginsAllowed()` free function was removed — all callers now use `consentToUnsigned()`.

**CR-01 invariant enforced in two places:**

1. `installFromFile`: `VerifyVerdict::Refused` branch is unconditional-quarantine with no reference to `consentToUnsigned()` or `allowUnsigned`.
1. `allowPlugin()`: re-verifies the manifest via `verifyStagedPlugin()` and returns `false` immediately if verdict is `Refused` (tampered).

**`allowPlugin(uuid)` Q_INVOKABLE:**

- Looks up `<pluginsDir>/<uuid>.sdPlugin/manifest.json`.
- Returns `false` + warns if UUID unknown or manifest tampered (Refused).
- Records `plugins/allowed/<uuid>=true` in QSettings on success.
- Emits `installedCountChanged()` so callers re-scan.

**Launch sweep updated:** now also quarantines Unsigned plugins without consent (previously only quarantined Refused). Unsigned + `consentToUnsigned()` = kept; Unsigned + no consent = removed.

### Task 2: QML trust UX

**`LoadedPluginsPage.qml` extended with four trust states:**

| trustLevel    | Chip                     | Action                                         |
| ------------- | ------------------------ | ---------------------------------------------- |
| `trusted`     | None (positive signal)   | None                                           |
| `self-signed` | Amber "self-signed" chip | None                                           |
| `unsigned`    | Red "unsigned" chip      | "Allow" button calling `allowPlugin(pluginId)` |
| `tampered`    | Red "tampered" chip      | ABSENT (not disabled — no UI path, CR-01)      |

**"Allow unsigned plugins" Switch** added above the plugin list, bound two-way to `PluginCatalog.allowUnsignedPlugins` via `onToggled`. Follows the PluginStore.qml online catalog toggle pattern exactly.

**Tooltips updated:** unsigned and tampered each get distinct descriptions. The old tooltip conflated "unsigned or tampered" — now each state is clearly explained including "It cannot be allowed from the UI" for tampered.

## Test Results

### CR-01 Tampered-still-refused assertion

Test: `PluginCatalog allowUnsignedPlugins=true tampered STILL refused (CR-01)` — **PASSED**

With `allowUnsignedPlugins=true`, a tampered archive (signed + byte-flipped) triggers `VerifyVerdict::Refused` and is quarantined. `installFinished(false)` is emitted and no manifest appears in `installedPlugins/`.

### Full test suite

```
ctest --preset linux-release -E qml
711/711 tests passed, 0 failed
```

### New tests (5 tests added)

| Test                                                                                    | Result |
| --------------------------------------------------------------------------------------- | ------ |
| `PluginCatalog allowUnsignedPlugins persists to QSettings`                              | PASSED |
| `PluginCatalog allowUnsignedPlugins setting installs unsigned without per-call consent` | PASSED |
| `PluginCatalog allowUnsignedPlugins=false refuses unsigned without consent`             | PASSED |
| `PluginCatalog allowUnsignedPlugins=true tampered STILL refused (CR-01)`                | PASSED |
| `PluginCatalog allowPlugin refuses tampered rows`                                       | PASSED |

## Deviations from Plan

### Auto-fixed Issues

**[Rule 1 - Bug] Removed `untrustedPluginsAllowed()` free function**

- **Found during:** Task 1 implementation
- **Issue:** After replacing all call sites with `consentToUnsigned()`, the free function was unused and triggered `-Werror=unused-function`.
- **Fix:** Removed the function entirely. All consumers now call `consentToUnsigned()` (the single source of truth as the plan intended).
- **Files modified:** `src/app/src/plugin_catalog_model.cpp`
- **Commit:** b591a5a

**[Rule 2 - Missing critical functionality] Launch sweep now also quarantines Unsigned without consent**

- **Found during:** Task 1 — the launch sweep only quarantined `Refused`, not `Unsigned` without consent.
- **Fix:** Added `else if (vout.verdict == VerifyVerdict::Unsigned)` branch to remove Unsigned plugins at launch when no consent is set. This is correct security behavior that was missing from the sweep.
- **Files modified:** `src/app/src/plugin_catalog_model.cpp`
- **Commit:** b591a5a

## Known Stubs

None. The allowPlugin() invokable emits `installedCountChanged()` but does not directly spawn the plugin (that is the rediscover() concern from Plan 27-02, which is a separate wave). This is intentional: allowPlugin records consent + signals; the manager re-scans on the signal. The behavior is documented in the invokable's doc comment.

## Threat Flags

None. No new network endpoints, auth paths, file access patterns, or schema changes beyond what the plan's threat model already covers (T-27-TRUST, T-27-UIBYPASS). The `consentToUnsigned()` predicate is correctly scoped to the Unsigned branch only, and both `allowPlugin()` and `installFromFile` enforce CR-01 independently.

## Self-Check: PASSED

- `src/app/src/plugin_catalog_model.hpp` — FOUND; contains `allowUnsignedPlugins` Q_PROPERTY, `allowPlugin` invokable, `consentToUnsigned()` private method, `allowUnsignedPluginsChanged` signal, `m_allowUnsignedPlugins` member.
- `src/app/src/plugin_catalog_model.cpp` — FOUND; contains `plugins/allowUnsignedPlugins` QSettings key, `consentToUnsigned()` implementation, `allowPlugin()` implementation.
- `src/app/qml/LoadedPluginsPage.qml` — FOUND; contains `allowUnsignedPlugins` toggle, "tampered" chip, "Allow" button gated on `trustLevel === "unsigned"`.
- `tests/unit/test_plugin_install_from_file.cpp` — FOUND; contains 5 new trust tests.
- Commits b591a5a and 77b3586 — VERIFIED in git log.
- `ctest --preset linux-release -E qml` — 711/711 PASSED.
