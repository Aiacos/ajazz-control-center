---
phase: 22-plugin-store-local-install
reviewed: 2026-05-24T19:03:11Z
depth: standard
files_reviewed: 2
files_reviewed_list:
  - src/app/src/plugin_verify_gate.cpp
  - src/app/src/plugin_catalog_model.cpp
findings:
  critical: 3
  warning: 4
  info: 2
  total: 9
status: issues_found
---

# Phase 22: Code Review Report

**Reviewed:** 2026-05-24T19:03:11Z
**Depth:** standard
**Files Reviewed:** 2 (plus companion headers and `sdplugin_extractor.cpp` for cross-file analysis of security-critical call chains)
**Status:** issues_found

## Summary

Reviewed the Phase 22 plugin-store / local-install implementation. The core
security skeleton is sound: fail-closed gate in `verifyStagedPlugin`, staging-
before-promote in `installFromFile`, zip-slip guard in `sdplugin_extractor`,
and the no-phone-home default. Three critical defects were found that can each
result in an unverified plugin landing on disk and being loaded:

1. **The network-install path (`install()`) skips signature verification when
   extraction fails** — a Refused archive is left as an opaque zip, which the
   next-launch `extractStandalonePluginArchives` sweep then extracts and the
   follow-on verify scan approves or removes. But between launch cycles the
   file sits in `installedPlugins/` unsupervised, and if anything else reads
   it first it is unguarded.

1. **The `installFromFile` rename-fallback re-extracts directly into
   `pluginsDir/` bypassing the staging-then-verify sequence** — if the
   cross-filesystem rename fails, a second `extractSdPluginArchive` is called
   directly into the promoted location _without_ re-running `verifyStagedPlugin`,
   so the extracted manifest at `promotedDir/manifest.json` is never checked.

1. **`tr()` called in a free lambda body in `install()`** — C++ `tr()` without
   a class context resolves to `QObject::tr()` which is deprecated in Qt 6 and
   emits the wrong translation context; on some toolchains (MSVC `/W4 /WX`) it
   is a hard error.

Four warnings cover: the QML local-install path-heuristic that silently drops
outcomes on Windows paths, the SelfSigned staging residue left on disk
indefinitely, an unbounded `m_install` growth path via `localPath` keys, and
the undocumented `refreshOnline()` no-phone-home bypass.

______________________________________________________________________

## Critical Issues

### CR-01: `install()` marks plugin as installed when extraction fails — verify gate is skipped for the on-disk zip

**File:** `src/app/src/plugin_catalog_model.cpp:835-890`

**Issue:** The network-install path (`install()`) extracts the downloaded
archive, then runs `verifyStagedPlugin` only inside `if (extractOk)`. When
extraction fails the comment says "archive left so the user can retry". However
the archive has already been written to `pluginsDir/foo.sdPlugin` (a path
inside `installedPlugins/`). On the next app launch `extractStandalonePluginArchives`
is called unconditionally (constructor line 124), which extracts the zip — and
the per-directory verify sweep that follows (lines 132-155) _does_ run, so
most tampered zips will eventually be caught. But there is a launch-to-launch
window during which a failed-extract Refused zip sits in `installedPlugins/`
as a zip file. More critically: **if `extractOk` is false, `installFinished`
is never emitted** — control simply falls through to the `if (extractOk)` block
for the verify, which is skipped, and then the function continues to mark the
plugin as installed (lines 875-889) and emits `installFinished(uuid, true, "")`.
That means the verify gate is **completely bypassed** when extraction silently
fails but the write succeeded.

```
// CURRENT — extraction failure silently skips verify and succeeds
bool const extractOk = extractSdPluginArchive(destCopy, archiveDir, archiveName);
if (extractOk) {
    QFile::remove(destCopy);
} else {
    AJAZZ_LOG_WARN(...);
}
if (extractOk) {              // <-- verify only runs when extract succeeded
    // verify ...
}
// FALLS THROUGH to markInstalled() + installFinished(success=true) even when !extractOk
```

**Fix:** When extraction fails, refuse the install immediately — do not mark
installed and do not emit success:

```cpp
bool const extractOk = extractSdPluginArchive(destCopy, archiveDir, archiveName);
if (!extractOk) {
    AJAZZ_LOG_WARN("plugin-catalog",
                   "install '{}': extract failed; leaving archive at {} for retry",
                   uuidCopy.toStdString(), destCopy.toStdString());
    emit self->installFinished(uuidCopy, false,
        QStringLiteral("Failed to extract plugin archive."));
    return;
}
QFile::remove(destCopy);

// Verify the extracted manifest (always reached here)
QString const extractedManifest = QDir(archiveDir).filePath(archiveName + "/manifest.json");
VerifyOutcome const vout = verifyStagedPlugin(extractedManifest);
if (vout.verdict == VerifyVerdict::Refused) {
    QDir(QDir(archiveDir).filePath(archiveName)).removeRecursively();
    emit self->installFinished(uuidCopy, false,
        QStringLiteral("Plugin signature verification failed: %1").arg(vout.reason));
    return;
}
// ... mark installed ...
```

______________________________________________________________________

### CR-02: `installFromFile` rename-fallback re-extracts directly into `pluginsDir` without re-verifying

**File:** `src/app/src/plugin_catalog_model.cpp:638-683`

**Issue:** When `QDir().rename(stagedDir, promotedDir)` fails (cross-
filesystem install, line 638), the fallback path calls:

```cpp
bool const reExtract = extractSdPluginArchive(localPath, pluginsDir, archiveName);
```

This extracts directly into `pluginsDir/<archiveName>/` — the live installed-
plugins directory — without calling `verifyStagedPlugin` on the result.
The staging-then-verify invariant documented at lines 562-598 is violated:
the verify was run on the staged copy, but the promoted copy is a fresh
extraction from the same source file. The two copies should be identical, but
the source file `localPath` is user-controlled (not under our staging root),
so a TOCTOU race between the first extract (into staging) and the second
extract (into pluginsDir) could produce a different result if the file is
replaced during the window. At minimum, an attacker who can swap the local
file between the verify and the fallback re-extract can bypass the gate.

**Fix:** Re-run `verifyStagedPlugin` on the re-extracted result, or (simpler)
copy the staged directory rather than re-extracting:

```cpp
if (!renamed) {
    AJAZZ_LOG_WARN(...);
    // Copy-then-delete the already-verified staged dir instead of
    // re-extracting from the (potentially replaced) source file.
    bool const copyOk = QDir().rename(stagedDir, promotedDir);
    // If rename still fails (cross-fs), use QDir::entryInfoList + QFile::copy
    // to copy each file, then verify again:
    if (!copyOk) {
        bool const reExtract = extractSdPluginArchive(localPath, pluginsDir, archiveName);
        if (!reExtract) { /* ... */ return false; }
        // MUST re-verify: the source file was re-read
        VerifyOutcome const vout2 = verifyStagedPlugin(
            QDir(promotedDir).filePath(QStringLiteral("manifest.json")));
        if (vout2.verdict == VerifyVerdict::Refused) {
            QDir(promotedDir).removeRecursively();
            emit installFinished(localPath, false,
                QStringLiteral("Re-verification after copy failed."));
            return false;
        }
    }
}
```

______________________________________________________________________

### CR-03: `tr()` in static lambda context (network-install finished handler)

**File:** `src/app/src/plugin_catalog_model.cpp:864-866`

**Issue:** Inside the `QObject::connect(..., [self, reply, uuidCopy, destCopy]() { ... })`
lambda (line 778), the code calls bare `tr(...)` (line 866):

```cpp
emit self->installFinished(
    uuidCopy,
    false,
    tr("Plugin signature verification failed: %1").arg(vout.reason));
```

A lambda has no implicit `this` from the enclosing class. `tr()` without a
`this`-context resolves to `QObject::tr()`, which is deprecated in Qt 6 and
emits into the wrong translation context (`QObject` instead of
`PluginCatalogModel`). MSVC `/W4 /WX` (the project's Windows CI config)
treats this as a hard error (C4996 — deprecated function). The project's CI
matrix includes `windows-2022` with `/W4 /WX`, making this a build break on
Windows. The same issue exists at line 610 in `installFromFile` (a member
function context), but there it is actually valid; line 866 in the lambda is
the broken one.

**Fix:**

```cpp
emit self->installFinished(
    uuidCopy,
    false,
    PluginCatalogModel::tr("Plugin signature verification failed: %1")
        .arg(vout.reason));
```

______________________________________________________________________

## Warnings

### WR-01: QML `installFinished` local-path heuristic breaks on Windows paths

**File:** `src/app/qml/PluginStore.qml:110`

**Issue:** The `onInstallFinished` handler in the page-level `Connections`
block filters local-install outcomes with:

```js
if (!path.startsWith("/") && !path.startsWith("file:")) {
    return;
}
```

On Windows, `localPath` emitted by `installFromFile` is an absolute Win32
path such as `C:\Users\…\foo.sdPlugin`. It starts with neither `/` nor
`file:`, so the handler silently discards all local-install outcomes
(success, error, and the self-signed confirm trigger) on Windows. The user
sees no status update and the self-signed dialog never opens.

**Fix:** Use a more portable check. Since `installFromFile` normalises the
path via `QUrl::fromUserInput`/`toLocalFile`, the signal always emits an
absolute local path — on all platforms a QUrl-derived local path does not
look like a UUID. The cleaner approach is to emit a distinct signal or a
distinguishing prefix, but a minimal fix is:

```js
// Match any absolute local path (Unix /, Windows drive letter, or file:)
if (!path.startsWith("/") && !path.startsWith("file:") &&
    !/^[A-Za-z]:[\\/]/.test(path)) {
    return;
}
```

______________________________________________________________________

### WR-02: SelfSigned staging directory never cleaned up when user cancels

**File:** `src/app/src/plugin_catalog_model.cpp:614-624`

**Issue:** When `installFromFile` detects a `SelfSigned` verdict without
`userConfirmedUnsigned`, it explicitly leaves the staging directory on disk
with the comment "the confirm path will re-use it". However:

1. The re-confirm invocation (QML line 167) calls `installFromFile(path, true)`
   with the **original source file path**, not the staged directory. The
   function re-reads the file, re-extracts into a new staging dir, and
   re-verifies — it does not re-use the old staging dir. So the old staging
   dir is orphaned unconditionally.

1. When the user cancels (`selfSignedDialog.onRejected`), no cleanup is
   performed. The staging dir remains in `../.plugin_staging/` across app
   restarts, growing without bound across repeated cancel/retry cycles.

1. The launch-sweep verify at lines 132-155 scans `*.sdPlugin` directories
   inside `pluginsDir/` but not the sibling `../.plugin_staging/` directory,
   so orphaned SelfSigned staging dirs are never quarantined or collected.

**Fix:** Remove the staging directory in the SelfSigned-no-confirm branch:

```cpp
if (vout.verdict == VerifyVerdict::SelfSigned && !userConfirmedUnsigned) {
    // Clean up the staging dir — the re-confirm path re-extracts from scratch.
    QDir(QDir(stagingParent).filePath(archiveName)).removeRecursively();
    // Also try to remove stagingParent if now empty.
    QDir(stagingParent).rmdir(QStringLiteral("."));
    emit installFinished(localPath, false,
        QStringLiteral("self-signed plugin -- confirm to install"));
    return false;
}
```

______________________________________________________________________

### WR-03: `m_install` map grows unbounded via `localPath` keys in `installFromFile`

**File:** `src/app/src/plugin_catalog_model.cpp:661-676`

**Issue:** On every successful `installFromFile` call, the code unconditionally
inserts `localPath` (the full filesystem path of the source archive) as a key
into `m_install`:

```cpp
auto& state = m_install[localPath];
state.installed = true;
state.enabled = true;
```

This key is never removed by `reload()` (the reconciliation loop only keeps
UUIDs that appear in `m_rows`, line 301-306) and is never cleaned up by
`uninstall()` (which looks up by UUID, not localPath). Every unique file path
from which the user installs a plugin permanently leaks an `InstallState` entry
in the side map. Over time (reinstalls from different paths, CI runs pointing at
temp dirs, etc.) this accumulates. Additionally, the `installedCount()` method
(line 282-290) iterates ALL install-state entries including these orphaned
localPath entries, so the displayed count drifts above the true number of
installed plugins.

**Fix:** Do not insert a `localPath` key. The per-catalogue-row key (UUID) is
set just below (lines 669-675); the `localPath` key is dead code for any row
that matches the catalogue, and useless for rows that don't (they have no
corresponding `m_rows` entry to display):

```cpp
// Remove the m_install[localPath] insertion entirely.
// The catalogue-UUID key below is sufficient.
QString const candidateUuid = ...;
int const r = findRow(m_rows, candidateUuid);
if (r >= 0) {
    auto& rowState = m_install[candidateUuid];
    rowState.installed = true;
    rowState.enabled = true;
    ...
}
```

______________________________________________________________________

### WR-04: `refreshOnline()` bypasses the phone-home opt-in gate without user-visible disclosure

**File:** `src/app/src/plugin_catalog_model.cpp:376-387`

**Issue:** `refreshOnline()` is documented as "trigger a live fetch
unconditionally, regardless of `onlineCatalogEnabled()`". It clears any
`"disabled"` override on the fetchers and fires immediately. The function is:
(a) `Q_INVOKABLE` — callable from QML; (b) exposed on the status-line Refresh
button (PluginStore.qml lines 442 and 576) which is visible even when the
online-catalog switch is off. The UX implication is that a user who has
explicitly set the switch to OFF can still trigger an outbound network request
by clicking the Refresh glyph, with no indication that the switch will be
overridden. The button tooltip says "fetches from the internet" but there is no
additional confirmation step and no update to the persisted `onlineCatalogEnabled`
flag, so the behaviour is inconsistent: the setting reads `false` but traffic
flows. This is a PLUGIN-14 / T-22-phonehome partial bypass — not a full bypass
(the Refresh button is explicit user action) but it violates the principle that
`onlineCatalogEnabled=false` means "no outbound traffic".

**Fix:** Either gate the Refresh button on `onlineCatalogEnabled` (disable it
when the switch is off and replace with an explanatory tooltip), or flip the
persisted flag to `true` when the user explicitly clicks Refresh:

```cpp
void PluginCatalogModel::refreshOnline() {
    // Treat explicit Refresh as an implicit opt-in for this session.
    // If the user has the setting off, warn in the log but still honour
    // the click (it is an explicit action). For strict PLUGIN-14 compliance,
    // gate on the flag instead:
    if (!m_onlineCatalogEnabled) {
        AJAZZ_LOG_INFO("plugin-catalog",
                       "refreshOnline: skipped (onlineCatalogEnabled=false)");
        return;
    }
    ...
}
```

______________________________________________________________________

## Info

### IN-01: Duplicate `relativeAge` function in QML — code duplication

**File:** `src/app/qml/PluginStore.qml:337-353` and `474-490`

**Issue:** `relativeAge(unixMs)` is defined identically twice — once in
`streamdockBanner` and once in `opendeckBanner`. Any bug fix or localisation
change must be applied to both copies.

**Fix:** Extract to a page-level JS function or a reusable QML
helper component so both banners share a single implementation.

______________________________________________________________________

### IN-02: `DownloadUrl` role missing from `Roles` enum and `roleNames()`

**File:** `src/app/src/plugin_catalog_model.hpp:143-161` / `src/app/src/plugin_catalog_model.cpp:261-280`

**Issue:** `CatalogEntry` has a `downloadUrl` field (line 97 of the .hpp) but
there is no corresponding `DownloadUrlRole` in the `Roles` enum and no entry
in `roleNames()`. QML delegates therefore cannot read `downloadUrl` via the
model API. The install button on the tile calls `PluginCatalog.install(uuid)`
which resolves the URL server-side, so this is not a current functional bug,
but the missing role means a QML delegate can never independently inspect
whether a direct download is available (to show a different button label, for
example). The field exists in the struct but is dark from the QML side.

**Fix:** Add the role if QML access to `downloadUrl` is intended:

```cpp
// In Roles enum:
DownloadUrlRole,  ///< Direct download URL (QUrl).

// In roleNames():
{DownloadUrlRole, "downloadUrl"},

// In data():
case DownloadUrlRole:
    return row.downloadUrl;
```

______________________________________________________________________

_Reviewed: 2026-05-24T19:03:11Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
