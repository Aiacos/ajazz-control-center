---
phase: 35-windows-plugin-support-security-hardening-milestone-verifica
reviewed: 2026-06-08T00:00:00Z
depth: standard
files_reviewed: 14
files_reviewed_list:
  - src/app/src/plugin_manifest.hpp
  - src/app/src/plugin_manifest.cpp
  - src/app/src/plugin_manager.cpp
  - src/plugins/include/ajazz/plugins/i_plugin_host.hpp
  - src/app/src/loaded_plugins_model.hpp
  - src/app/src/loaded_plugins_model.cpp
  - src/app/qml/LoadedPluginsPage.qml
  - src/app/src/application.cpp
  - src/app/src/unified_plugin_host.hpp
  - src/app/src/unified_plugin_host.cpp
  - tests/unit/test_win_plugin_classification.cpp
  - tests/unit/test_loaded_plugins_model.cpp
  - scripts/verif-milestone-gates.sh
  - .github/workflows/ci.yml
findings:
  critical: 1
  warning: 2
  info: 3
  total: 6
status: issues_found
---

# Phase 35: Code Review Report

**Reviewed:** 2026-06-08
**Depth:** standard
**Files Reviewed:** 14
**Status:** issues_found

## Summary

Phase 35 adds the WINPLG Windows-plugin classifier (`classifyWindowsPlugin` /
`supportsCurrentPlatform`), the WINPLG status chip in `LoadedPluginsPage.qml`,
the PLGSEC verify gates (`verif-milestone-gates.sh` + ci.yml wiring), and unit
tests. The classifier itself is correct, bounded, and read-only (PE-magic scan
reads exactly 2 bytes/file, caps at 4096 files, never executes). COD-031 is
preserved: `i_plugin_host.hpp` stays Qt/nlohmann-free and stores `winClass` as a
plain int. The LOCKED linux-accept exception and the strict VendorDll reject are
both intact. Tests are real (not tautological) and both new TUs are registered.

**However, the milestone's headline feature — the WINPLG status chip — is dead
for the only plugins that can carry a meaningful `winClass`.** The
`LoadedPluginsModel` is populated *only* from the Python `OutOfProcessPluginHost`,
which never sets `winClass` (always 0 → no chip). The `.sdPlugin` inventory,
which is where `classifyWindowsPlugin` actually stamps a verdict, is never handed
to the model. A safe, minimal, one-block fix exists (detailed below). This is the
key correctness defect of the phase.

## Critical Issues

### CR-01: WINPLG status chip is a no-op in the shipping build — `.sdPlugin` plugins never reach `LoadedPluginsModel`

**File:** `src/app/src/application.cpp:957` (also `:1091`, `:1098`)

**Issue:** Confirmed against the full wiring. The model is filled exactly once,
from the Python host:

- `Application::initPluginHost()` (entire body guarded by `#ifdef AJAZZ_PYTHON_HOST`,
  called from `bootstrap()` at `:877`) does
  `m_loadedPlugins->setPlugins(host->plugins())` at `:957`, where `host` is the
  Python `OutOfProcessPluginHost` constructed at `:943`. Python `PluginInfo`
  entries default `winClass{0}` (NotWindowsOnly) — they never go through
  `classifyWindowsPlugin`, so `platformStatusOf` always returns `""` and the chip
  is always hidden.
- The only producer of a real `winClass` is `PluginManager::plugins()`
  (`plugin_manager.cpp:965`: `info.winClass = static_cast<int>(livePlugin.manifest.winClass)`),
  which is stamped at scan time in `discover()` (`:285`).
- `UnifiedPluginHost::plugins()` correctly merges `.sdPlugin` + Python inventories
  (`unified_plugin_host.cpp:93-117`), but `m_pluginHost2` is constructed at
  `application.cpp:1091` — *after* the model was already filled — and is **never**
  passed to `LoadedPluginsModel` via `setPlugins`/`setPluginHost`. Grep confirms
  the only `setPlugins`/`setPluginHost` callsites are `:956`/`:957` (Python host).

Net effect: a Windows-only `.sdPlugin` (the chip's entire intended audience)
never appears in `LoadedPluginsPage` at all (it is a `.sdPlugin`, surfaced only
through `PluginManager`/`UnifiedPluginHost`), so its WINPLG chip — `native` /
`wine` / `unsupported` — is never rendered. The feature is dead for its target.

**A SAFE minimal fix exists** — recommend fixing now (not deferring):

In `startBackgroundServices()`, after the `.sdPlugin` discover+spawn loop and
after `m_pluginHost2` is constructed (`application.cpp:1091-1098`), re-point the
model at the unified host:

```cpp
        for (auto const& manifest : runnable) {
            m_pluginManager->spawn(manifest);
        }

        // WINPLG: re-wire the loaded-plugins model to the MERGED inventory so
        // .sdPlugin plugins (the only ones carrying a real winClass) surface
        // their platform-status chip. setPlugins() does a full reset, so this
        // REPLACES the Python-only list with the merged list — no duplication.
        if (m_loadedPlugins && m_pluginHost2) {
            m_loadedPlugins->setPluginHost(m_pluginHost2.get());
            m_loadedPlugins->setPlugins(m_pluginHost2->plugins());
        }
```

Why this is safe (each hazard checked):

- **No double-population.** `LoadedPluginsModel::setPlugins`
  (`loaded_plugins_model.cpp:109-114`) does `beginResetModel(); m_plugins = move(...); endResetModel()`
  — a full *replace*, not an append. The second call discards the Python-only
  list and installs the merged list (Python entries are still present because
  `UnifiedPluginHost::plugins()` appends them, `unified_plugin_host.cpp:105-114`).
- **Inventory timing.** `PluginManager::spawn()` inserts into `m_live`
  synchronously (`plugin_manager.cpp:461/522/577`), and `PluginManager::plugins()`
  reads `m_live` (`:953`). So immediately after the spawn loop the `.sdPlugin`
  entries are already present — no need to await async WebSocket registration.
- **`refresh()` implication is an improvement.** Re-pointing `setPluginHost` to
  `m_pluginHost2` means a future QML "Reload" calls
  `UnifiedPluginHost::plugins()` (merged) instead of the Python-only host — which
  is the correct behavior for this page. `refresh()` already swallows IPC failures
  (`loaded_plugins_model.cpp:124-131`).
- **Ordering.** `startBackgroundServices` runs after `bootstrap`, so this strictly
  follows the existing `:957` population; no re-entrancy.

Residual nit (does not block the fix): `UnifiedPluginHost::plugins()` does not
dedup ids, so a Python plugin and a `.sdPlugin` sharing the same id would appear
twice. That is pre-existing aggregator behavior and is extremely unlikely
(distinct id namespaces); flagging only for awareness — not introduced by this fix.

## Warnings

### WR-01: WINPLG-02 native-run path bypasses the consent / Software.MinimumVersion gate for win-only WS plugins

**File:** `src/app/src/plugin_manager.cpp:297-305`

**Issue:** The runnability decision is
`if (!baseRunnable && !winNativeRunnable) { skip }`. For a `os=["windows"]`,
non-PE plugin, `supportsCurrentPlatform` returns `true` (WsOnlyIpc) on Linux even
when `manifestRunnableHere` returned `false`. `manifestRunnableHere` is also where
the `Software.MinimumVersion` gate lives (`plugin_manifest.cpp:359-363`). So a
win-only WS plugin that declares a `Software.MinimumVersion` *higher* than
`kEmulatedSdVersion` is now accepted on Linux purely on the strength of
`winNativeRunnable`, silently skipping the version floor that a plain (non-win)
plugin would still be subject to. This is a behavior change introduced by the OR.

**Fix:** Keep the OS-array override but still honor the version floor for the
win-native path, e.g. gate the native acceptance on the version check too:

```cpp
        bool const versionOk = manifestVersionGatePasses(*opt, kEmulatedSdVersion);
        if (!baseRunnable && !(winNativeRunnable && versionOk)) {
            // skip
        }
```

or, more simply, document explicitly that the win-native override is intended to
bypass `Software.MinimumVersion` (if that is the locked decision). If the bypass
is intentional, downgrade to Info — but it is currently undocumented and silently
divergent from the non-win path, which is the defect.

### WR-02: `platformStatus` and `trustLevel` chips are computed but their rows are unreachable until CR-01 is fixed — test coverage masks the gap

**File:** `tests/unit/test_loaded_plugins_model.cpp:41-53`; `src/app/qml/LoadedPluginsPage.qml:250`

**Issue:** The unit test exercises `platformStatusOf` by directly calling
`model.setPlugins(...)` with a hand-built `winClass=2` row. This passes (the
derive is correct) and gives the appearance of an end-to-end-verified chip — but
no test covers the *production* population path, which (per CR-01) never feeds a
nonzero `winClass` into the model. The green test is exactly the
"tests pass ≠ behavior works" trap called out in CLAUDE.md (Phase-27 "Allow"
button precedent: 713 tests + a review passed a control that was a no-op live).

**Fix:** After CR-01 is applied, add a wiring-level assertion (or a debug-channel
walk per CLAUDE.md "Debug-channel verification — MANDATORY") that a real win-only
`.sdPlugin` surfaces a non-empty `platformStatus` in `LoadedPluginsPage`. At
minimum, the milestone verification must drive the running app and `qml.get` the
`platformStatusChip.statusLabel` on a real installed win-only plugin — unit tests
cannot catch the CR-01 wiring bug.

## Info

### IN-01: PE-magic scan doc says "directly under bundleDir" but recurses subdirectories

**File:** `src/app/src/plugin_manifest.cpp:410-411,429`; `plugin_manifest.hpp:240`

**Issue:** The function comment says "any regular file **directly under**
`bundleDir`", but the `QDirIterator` is constructed with
`QDirIterator::Subdirectories` (`:429`), so it recurses. The recursion is the
*correct* behavior (a vendor DLL may live in a `bin/` subdir), but the comment is
wrong and will mislead a future maintainer reasoning about the DoS bound. The
4096-file cap still applies across the whole tree, so the bound holds.

**Fix:** Update the comment to "any regular file in the bundle tree (recursive,
bounded by `kMaxFilesScanned`)".

### IN-02: 4096-file scan cap is a magic number with the bound semantics only in a comment

**File:** `src/app/src/plugin_manifest.cpp:426`

**Issue:** `constexpr int kMaxFilesScanned = 4096;` — the value is named, which is
good, but the off-by-one in the loop is worth noting: `if (++scanned > kMaxFilesScanned) break;`
examines exactly 4096 files then breaks on the 4097th iteration before reading it.
Correct, but a `>=`-vs-`>` reviewer should confirm: as written it scans 4096 files
(intended). No change required; documenting for the record.

### IN-03: `LoadedPluginsPage.qml` header comment lists `platformStatus` as a consumed role but omits it from the "Roles consumed" block

**File:** `src/app/qml/LoadedPluginsPage.qml:16-18`

**Issue:** The file-top "Roles consumed (from LoadedPluginsModel)" comment lists
`pluginId, name, version, authors, permissions, isSigned, publisher, trustLevel`
but not `platformStatus`, which the delegate now reads (`:243,:250,:257,...`).
Documentation drift only — no functional impact.

**Fix:** Add `platformStatus` to the roles-consumed comment.

______________________________________________________________________

## Verified clean (no findings)

- **COD-031 boundary:** `i_plugin_host.hpp` carries `winClass` as a plain `int`
  with the 0/1/2 mapping documented; no Qt, no nlohmann, no app dependency. The
  app-tier `WinPluginClass` enum mirror lives only in `plugin_manifest.hpp`.
- **Classifier security:** `bundleHasPeMagic` reads exactly 2 bytes per file,
  never parses a PE, never executes, handles empty/missing/short files
  (`f.read(2)` + `head.size() == 2` guard), and caps at 4096 files. Robust against
  missing bundleDir (returns false, no crash) — proven by the tests.
- **CodePath/CodePathWin suffix logic:** `effectiveWinCodePath` prefers
  `codePathWin` when non-empty else `codePath` (`:400-402`); case-insensitive
  `.exe`/`.dll` match. Tests cover the override-precedence and case-insensitive
  cases.
- **LOCKED linux-accept + strict VendorDll reject:** `manifestRunnableHere`
  preserves the Assumption-A1 linux exception (`:349-353`);
  `supportsCurrentPlatform` returns `false` for VendorDll off Windows
  (`:484-490`, `wineDetected=false`). Tests pin both.
- **QML chips:** all three (`trustChip`, `platformStatusChip`,
  `unsignedConsentChip`) have `objectName` + a readable label property
  (`trustLabel`/`statusLabel`/`consentLabel`); display-only (no `Switch`); the
  trustChip objectName gap is closed (`:173`). Visibility binds correctly to the
  empty-string sentinel.
- **verif-milestone-gates.sh:** all four grep gates are comment-aware
  (`strip_comments` drops `//` and `*` matches), `set -euo pipefail`, exit
  non-zero on any real hit, and report every gate. ci.yml `:93-95` wires it as a
  Linux step that EXTENDS — does not replace — the Phase-30 `QHostAddress::Any` +
  SIGPIPE gate at `:67-83`.
- **Tests:** `test_win_plugin_classification.cpp` and
  `test_loaded_plugins_model.cpp` make real assertions (verdict values, role
  presence, role name string); ASCII-only names; both TUs registered in
  `tests/unit/CMakeLists.txt` (`:523`, `:531`).

______________________________________________________________________

_Reviewed: 2026-06-08_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
