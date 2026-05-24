---
phase: 22-plugin-store-local-install
fixed_at: 2026-05-24T21:15:00Z
review_path: .planning/phases/22-plugin-store-local-install/22-REVIEW.md
iteration: 1
findings_in_scope: 9
fixed: 9
skipped: 0
status: all_fixed
---

# Phase 22: Code Review Fix Report

**Fixed at:** 2026-05-24T21:15:00Z
**Source review:** `.planning/phases/22-plugin-store-local-install/22-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 9 (3 Critical + 4 Warning + 2 Info)
- Fixed: 9
- Skipped: 0

## Fixed Issues

### CR-01: `install()` marks plugin as installed when extraction fails

**Files modified:** `src/app/src/plugin_catalog_model.cpp`
**Commit:** `13a2c99`
**Applied fix:** When `extractSdPluginArchive` returns false in the network-install
`install()` lambda, the old code fell through to `markInstalled + installFinished(true)`,
completely bypassing the verify gate. Fixed by adding an early `!extractOk` guard that
removes the archive from disk, emits `installFinished(uuid, false, "Failed to extract plugin archive.")`, and returns immediately. The verify block is now unconditional
(no wrapping `if(extractOk)`), so the gate can never be bypassed by extraction failure.

Note: CR-03 (bare `tr()` in lambda body) is resolved in the same commit — the
`tr("Plugin signature verification failed: %1")` call inside the lambda (now inside the
unconditional verify block) was replaced with `PluginCatalogModel::tr(...)`, which uses
the correct translation context and is not deprecated under MSVC `/W4 /WX`.

### CR-02: `installFromFile` rename-fallback re-extracts directly into `pluginsDir` without re-verifying

**Files modified:** `src/app/src/plugin_catalog_model.cpp`
**Commit:** `f9a3bd4`
**Applied fix:** The cross-filesystem rename-fallback previously called
`extractSdPluginArchive(localPath, pluginsDir, archiveName)` directly — re-reading
from the user-controlled `localPath` without re-running `verifyStagedPlugin`. A TOCTOU
race between the first extract (into staging) and this fallback could bypass the gate.

Fixed by replacing the re-extract with a file-by-file copy of the already-verified
staged directory into `promotedDir`, followed by a re-run of `verifyStagedPlugin` on
the copied manifest. A Refused result quarantines `promotedDir` and emits
`installFinished(path, false, ...)`. The staging-then-verify-then-promote invariant is
preserved on both the rename and copy-fallback code paths.

This commit also resolves WR-02 and WR-03 (see below).

### CR-03: `tr()` in static lambda context (network-install finished handler)

**Files modified:** `src/app/src/plugin_catalog_model.cpp`
**Commit:** `13a2c99` (resolved together with CR-01)
**Applied fix:** The bare `tr()` call in the `QNetworkReply::finished` lambda (which
has no implicit `this` from the enclosing class) was replaced with
`PluginCatalogModel::tr(...)`. This uses the correct `PluginCatalogModel` translation
context instead of the deprecated `QObject::tr()`, preventing the MSVC C4996 hard error.

### WR-01: QML `installFinished` local-path heuristic breaks on Windows paths

**Files modified:** `src/app/qml/PluginStore.qml`
**Commit:** `78a3710`
**Applied fix:** The `onInstallFinished` handler previously checked only
`path.startsWith("/")` and `path.startsWith("file:")`, silently discarding all
local-install outcomes on Windows (where `installFromFile` emits an absolute Win32 path
like `C:\Users\...\foo.sdPlugin`). Fixed by adding a `/^[A-Za-z]:[\\/]/` regex test so
Windows drive-letter paths are recognised as local-install outcomes. The self-signed
confirmation dialog now opens correctly on all platforms.

### WR-02: SelfSigned staging directory never cleaned up when user cancels

**Files modified:** `src/app/src/plugin_catalog_model.cpp`
**Commit:** `f9a3bd4` (resolved together with CR-02)
**Applied fix:** The `SelfSigned && !userConfirmedUnsigned` branch previously left the
staging directory on disk with the comment "confirm path will re-use it." However, the
confirm path always re-extracts from the source file into a fresh staging dir — the old
dir was unconditionally orphaned. Fixed by adding `QDir(stagingDir).removeRecursively()`
and `QDir(stagingParent).rmdir(".")` before the `installFinished` emission, preventing
orphaned staging dirs from accumulating across cancel/retry cycles.

### WR-03: `m_install` map grows unbounded via `localPath` keys in `installFromFile`

**Files modified:** `src/app/src/plugin_catalog_model.cpp`
**Commit:** `f9a3bd4` (resolved together with CR-02)
**Applied fix:** Removed the `auto& state = m_install[localPath]` insertion entirely.
This key was never cleaned up by `reload()` (reconciles only by UUID) or `uninstall()`
(looks up by UUID), so every unique file path from which a user installed a plugin
permanently leaked an `InstallState` entry and caused `installedCount()` to drift above
the true count. The UUID-keyed entry below (for catalogue-matched rows) is sufficient.

### WR-04: `refreshOnline()` bypasses the phone-home opt-in gate

**Files modified:** `src/app/src/plugin_catalog_model.cpp`, `src/app/src/plugin_catalog_model.hpp`,
`src/app/qml/PluginStore.qml`
**Commit:** `4571860`
**Applied fix:**

- **C++ (authoritative):** Added an early-return guard in `refreshOnline()`: when
  `m_onlineCatalogEnabled` is false, logs a message and returns immediately without
  calling the fetchers. The no-phone-home invariant is enforced at the C++ level.
- **QML (UI consistency):** Disabled both Refresh `ToolButton` elements (in
  `streamdockBanner` and `opendeckBanner`) when `PluginCatalog.onlineCatalogEnabled` is
  false. Updated their `ToolTip.text` to explain that the Online catalog switch must be
  enabled. This keeps the UI consistent with the C++ enforcement.
- **Header:** Updated `refreshOnline()` doc comment to document the new gated semantics.

### IN-01: Duplicate `relativeAge` function in QML

**Files modified:** `src/app/qml/PluginStore.qml`
**Commit:** `b4fe441`
**Applied fix:** Extracted the two identical `relativeAge(unixMs)` functions from
`streamdockBanner` and `opendeckBanner` into a single page-level
`function relativeAge(unixMs, tick)`. The `tick` parameter is the caller's
`relativeAgeTick` property, which triggers re-evaluation when the banner's Timer fires.
Both banners now call `root.relativeAge(ts, <banner>.relativeAgeTick)`. The local
function definitions have been removed from both Frame items.

### IN-02: `DownloadUrl` role missing from `Roles` enum and `roleNames()`

**Files modified:** `src/app/src/plugin_catalog_model.hpp`, `src/app/src/plugin_catalog_model.cpp`
**Commit:** `b4fe441` (resolved together with IN-01)
**Applied fix:** Added `DownloadUrlRole` to the `Roles` enum with a doc comment, added
`{DownloadUrlRole, "downloadUrl"}` to `roleNames()`, and added a `case DownloadUrlRole: return row.downloadUrl;` handler to `data()`. QML delegates can now read the
`downloadUrl` field directly from the model.

## Regression Tests Added

**Commit:** `88fed1b`
**File:** `tests/unit/test_plugin_install_from_file.cpp`

Three regression tests were added to cover CR-01 and CR-02:

1. **"CR-01 extract+verify failure does not mark installed"** — builds an unsigned archive
   (no `Ajazz.Signing` block), calls `installFromFile` with `confirm=true`; asserts
   `result==false`, plugin NOT in `installedPlugins/`, `installedCount()` unchanged,
   `installFinished` emitted with `success=false`.

1. **"CR-01 Refused verdict -> not installed, success=false"** — signs then tampers a
   manifest (guaranteed Refused); asserts `installFinished` emits `false` and nothing
   appears under `installedPlugins/`.

1. **"CR-02 Refused never lands in pluginsDir"** — same sign+tamper pattern; asserts
   the Refused plugin does not appear in `installedPlugins/` and `installFinished` emits
   `false`, regardless of which promotion path ran.

All test names are ASCII-only (CLAUDE.md Win32 CMD codepage guard).

## Build and Test Verification

**Build result:** `cmake --build build/linux-release` completed successfully (no errors,
no FAILED targets). Both `src/app/ajazz-control-center`, `tests/unit/ajazz_unit_tests`,
and `tests/qml/ajazz_qml_tests` linked successfully.

**QML build:** `PluginStore_qml.cpp` compiled without errors — the IN-01 `relativeAge`
refactor (page-level function + `void(tick)` dependency pattern) was accepted by the QML
compiler.

**Test count:** `ctest --preset linux-release`: **617/617 tests passed** (100%), 0
failed. Total time: 99.62 s.

**Windows C4996 verification:** The `tr()` → `PluginCatalogModel::tr()` replacement is
the non-deprecated static form that does not trigger C4996 under MSVC `/W4 /WX`. Cannot
verify locally (Linux CI), but the form is correct per the Qt 6 API documentation.

______________________________________________________________________

_Fixed: 2026-05-24T21:15:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
