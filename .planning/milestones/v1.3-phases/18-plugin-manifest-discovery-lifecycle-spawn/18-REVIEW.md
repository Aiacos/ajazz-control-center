---
phase: 18-plugin-manifest-discovery-lifecycle-spawn
reviewed: 2026-05-24T00:00:00Z
depth: standard
files_reviewed: 5
files_reviewed_list:
  - src/app/src/plugin_manifest.cpp
  - src/app/src/node_runner.cpp
  - src/app/src/plugin_mirabox_shim.cpp
  - src/app/src/plugin_crash_tracker.cpp
  - src/app/src/plugin_manager.cpp
findings:
  critical: 3
  warning: 3
  info: 1
  total: 7
status: issues_found
---

# Phase 18: Code Review Report

**Reviewed:** 2026-05-24
**Depth:** standard
**Files Reviewed:** 5
**Status:** issues_found

## Summary

Phase 18 adds plugin discovery, manifest parsing, Node.js spawn, the Mirabox JS shim, a
crash-window tracker, and the orchestrating PluginManager. The manifest parser and crash
tracker are well-structured; COD-031 (no nlohmann::json) is clean across all five files.
The security paperwork is thorough (T-18-ARGV, T-18-PATHTRAV, T-18-ELEVATE, T-18-CHILD-PERSIST
all have dedicated mitigations).

Three BLOCKER-level problems were found: the spawned child processes inherit the full host
process environment (environment secrets leak into untrusted plugin children — the single most
important security invariant called out in the project conventions); a double-signal bug in Qt
causes `onProcessFailed` to fire twice per `FailedToStart` event, producing phantom crash
credits and an extra spawn; and the platform-override code path (`codePathWin`/`codePathMac`)
bypasses the only path-traversal guard in `spawn()`, leaving the actual executable path
unvalidated on Windows and macOS builds.

Three WARNING-level problems were also found: the private Qt header
`qquickwebenginescriptcollection_p.h` is required to get a complete type for
`QQuickWebEngineScriptCollection` and has no public-API alternative in Qt 6.7, but private
headers break across minor Qt versions; the `onProcessFailed` restart branch does not guard
against re-spawning HTML plugins (for which the comment claims a guard that does not exist in
code); and pluginId is derived from `manifest.codePath` (a filename) rather than
`manifest.puuid`, causing a semantically wrong `-pluginUUID` argument to be sent to the child
process.

______________________________________________________________________

## Critical Issues

### CR-01: Spawned child processes inherit the full host environment (host-state leakage)

**File:** `src/app/src/plugin_manager.cpp:227` (node path), `:304` (native path)
**Issue:** Neither `QProcess` created in `spawn()` calls `setProcessEnvironment()`. Qt's
default is to inherit the complete parent process environment. This propagates all host
environment variables — including `DBUS_SESSION_BUS_ADDRESS`, `XDG_RUNTIME_DIR`, Wayland
display sockets, potential `*_TOKEN` / `*_SECRET` / `*_PASSWORD` variables set by other
tools in the user's session, and any credentials exported by CI or shell scripts — into the
untrusted third-party plugin child. This directly violates the project convention: _"never
silently leaking host state into plugin children"_ (T-18-CHILD-ENV surface, listed in
`<project_conventions>`).

**Fix:** Build an explicit allow-list environment. At minimum:

```cpp
// Construct a minimal environment for plugin children (T-18-CHILD-ENV).
QProcessEnvironment childEnv = QProcessEnvironment::empty();
// Propagate only variables the plugin legitimately needs:
for (auto const& key : {
     "PATH",
     "HOME",
     "TMPDIR",
     "TEMP",
     "TMP",
     "LANG",
     "LC_ALL",
     "DISPLAY",       // X11 plugins need this
     "WAYLAND_DISPLAY"
     // Do NOT include: DBUS_SESSION_BUS_ADDRESS, XDG_RUNTIME_DIR,
     // or any *_TOKEN / *_SECRET / *_KEY / *_PASSWORD variables.
}) {
    auto const val = QProcessEnvironment::systemEnvironment().value(
        QString::fromLatin1(key));
    if (!val.isEmpty())
        childEnv.insert(QString::fromLatin1(key), val);
}
proc->setProcessEnvironment(childEnv);
```

Apply to both the node QProcess (around line 227) and the native QProcess (around line 304).

______________________________________________________________________

### CR-02: Double-fire of `onProcessFailed` on `QProcess::FailedToStart`

**File:** `src/app/src/plugin_manager.cpp:231-241` (node branch), `:307-317` (native branch)
**Issue:** When a QProcess cannot start (the node binary or native executable is missing or
not executable), Qt emits `errorOccurred(FailedToStart)` followed by `finished(-2, QProcess::CrashExit)` — both signals, in the same event-loop turn. Both are connected without
any guard, so `onProcessFailed(uuid)` is called **twice** for a single physical failure event.

Concrete effects:

- **Phantom crash credits:** two calls to `recordCrash` with the same timestamp count as two
  crashes. After one actual failure the crash window has 2 entries; a second real failure
  pushes it to 4, triggering `shouldDisable` after 2 physical failures instead of the intended
  3\. The 3-in-30s policy is silently broken.
- **Extra spawn per failure:** the first fire erases the entry from `m_live` and calls
  `spawn(manifest)`, inserting a new process. The second fire finds the new process in
  `m_live`, erases it (killing the freshly started child), and calls `spawn(manifest)` again.
  Each `FailedToStart` event produces two `spawn()` calls and transiently two child processes.
  For a chronically-absent node binary this is a rapid-restart storm with 2× process churn.

**Fix:** Introduce a per-UUID in-flight guard that discards the second fire, or disconnect
the `finished` signal once `errorOccurred` has been handled. The simplest approach is a
`QProcess::FailedToStart` early-return guard in the `finished` lambda:

```cpp
connect(rawProc, &QProcess::errorOccurred, this,
        [this, pluginId](QProcess::ProcessError err) {
            if (err != QProcess::FailedToStart)
                onProcessFailed(pluginId);
            // FailedToStart: Qt will also fire finished(CrashExit) — handle there only.
        });
connect(rawProc, &QProcess::finished, this,
        [this, pluginId](int exitCode, QProcess::ExitStatus status) {
            // Covers both FailedToStart (exit -2, CrashExit) and abnormal exits.
            if (status == QProcess::CrashExit || exitCode != 0) {
                onProcessFailed(pluginId);
            }
        });
```

This moves all failure dispatch to `finished`, using `errorOccurred` only for non-FailedToStart
errors that do not produce a `finished` signal (none in Qt 6, but the pattern is future-safe).
Alternatively, use a `std::once_flag` / `bool firedAlready` captured in the lambda pair for
the same UUID.

______________________________________________________________________

### CR-03: `resolveCodePath()` result is not path-validated — `codePathWin`/`codePathMac` bypass the traversal guard

**File:** `src/app/src/plugin_manager.cpp:197-204`
**Issue:** `spawn()` applies `isSafeUuidComponent(pluginId)` where `pluginId` derives from
`manifest.codePath` (the generic, typically Linux path). Then `code = resolveCodePath(manifest)`
is computed, which on a Windows build returns `manifest.codePathWin` when set, and on macOS
returns `manifest.codePathMac`. These platform-override paths are **never validated** by
`isSafeUuidComponent` or any other check. A manifest crafted with:

```json
{ "CodePath": "safe.js",
  "CodePathWin": "..\\..\\Windows\\System32\\evil.exe" }
```

would pass the `isSafeUuidComponent("safe.js")` guard on a Windows build, then execute
`..\..\Windows\System32\evil.exe` via `QProcess::start`. This is a direct path-traversal
attack against the spawn code path (T-18-PATHTRAV bypass).

**Fix:** Validate the resolved `code` path immediately after resolution, before the dispatch
branch:

```cpp
QString const code = resolveCodePath(manifest);
// Validate the actual code path that will be executed, not just the key used for m_live.
// isSafeUuidComponent allows '.' for "plugin.js" but also validates for traversal components.
// Additionally reject paths containing directory separators:
if (code.isEmpty() || code.contains(QLatin1Char('/')) || code.contains(QLatin1Char('\\')) ||
    code.contains(QLatin1String(".."))) {
    // If the plugin genuinely needs a subdirectory-relative path, validate it is within
    // the plugin bundle directory instead (canonical path comparison).
    qWarning("PluginManager: rejecting plugin '%s': code path '%s' is unsafe",
             qPrintable(manifest.name), qPrintable(code));
    return;
}
```

Or canonicalise both the plugin bundle dir and the resolved code path and confirm the latter
is prefixed by the former before executing.

______________________________________________________________________

## Warnings

### WR-01: Private Qt header `qquickwebenginescriptcollection_p.h` has no public-API path

**File:** `src/app/src/plugin_manager.cpp:54`
**Issue:**

```cpp
#include <QtWebEngineQuick/private/qquickwebenginescriptcollection_p.h>
```

The comment and CMakeLists both acknowledge this is required because
`QQuickWebEngineScriptCollection` is only _forward-declared_ in the public
`qquickwebengineprofile.h`, and its complete definition lives in a private header. Private
headers are not part of the Qt stable ABI contract and break without warning on Qt minor
version bumps. `Qt6::WebEngineQuickPrivate` was introduced in Qt 6.x specifically to make this
include path available, but it is still a private dependency. The CMakeLists fallback
(`string(CONCAT)` with a CMake list variable) is also likely to produce a malformed include
path on installs where `Qt6WebEngineQuickPrivate_FOUND` is false.

The `CLAUDE.md` convention flags private headers as version-fragile and asks to flag them when
a public-API alternative exists. The correct public path (available in Qt 6.7+) is:
`QQuickWebEngineProfile::userScripts()` returns a pointer to an opaque
`QQuickWebEngineScriptCollection*`. The _insert_ method is part of that class's public
interface; only the header declaring the class is private. There is no current workaround
that avoids the private header while keeping `userScripts()->insert(...)`. This is a known
Qt API gap.

**Fix:** Document the constraint explicitly in the source and add a static assertion on the
minimum required Qt version so the build fails loudly rather than silently on a mismatched
Qt where the private header path changed. Track against the Qt bug tracker for a public
`QQuickWebEngineScriptCollection` declaration.

```cpp
// MAINTAINER NOTE: QQuickWebEngineScriptCollection is only forward-declared in the public
// header. The private header below is required until Qt exposes a public declaration.
// If this include fails, bump Qt minimum version or check for the public declaration.
static_assert(QT_VERSION >= QT_VERSION_CHECK(6, 7, 0),
              "qquickwebenginescriptcollection_p.h path may have changed; verify include");
#include <QtWebEngineQuick/private/qquickwebenginescriptcollection_p.h>
```

______________________________________________________________________

### WR-02: `onProcessFailed` restart branch comment claims a null-process guard that does not exist

**File:** `src/app/src/plugin_manager.cpp:346`
**Issue:** The comment reads:

```
// Re-spawn only if the process was previously live (not an HTML plugin).
```

The code that follows does NOT check `it->second.process != nullptr` before calling
`spawn(manifest)`. The check is actually `m_live.find(uuid) != m_live.end()` — which is true
for HTML plugins (they are in `m_live` with `process == nullptr`). If `onProcessFailed` is
called for an HTML plugin UUID (e.g., from a test or a future WebEngine crash callback), the
HTML plugin will be re-spawned, causing a duplicate call to
`profile->userScripts()->insert(makeMiraboxShim())` which inserts the shim script twice into
the WebEngine profile.

**Fix:** Add the missing guard and fix the comment:

```cpp
auto it = m_live.find(uuid);
if (it != m_live.end() && it->second.process != nullptr) {
    // Only re-spawn if a process-backed plugin (not an HTML/WebEngine plugin).
    PluginManifest const manifest = it->second.manifest;
    m_live.erase(it);
    spawn(manifest);
}
```

______________________________________________________________________

### WR-03: `-pluginUUID` argument uses `codePath` filename instead of the plugin's identity UUID

**File:** `src/app/src/plugin_manager.cpp:197`
**Issue:**

```cpp
QString const pluginId = manifest.codePath.isEmpty() ? manifest.name : manifest.codePath;
```

`pluginId` is used as both the `m_live` key **and** the value passed as `-pluginUUID` to the
node child process (via `buildNodeArgv(..., pluginId, ...)`). For a typical real-world plugin,
`manifest.codePath` is a filename like `"Weather.js"`. The plugin child therefore registers
with `{"event":"registerPlugin","uuid":"Weather.js"}` rather than the expected reverse-DNS
identity (e.g., `"com.elgato.weather"`).

Plugins often embed their expected UUID in their own code to validate event routing. Passing
`"Weather.js"` instead of `"com.elgato.weather"` breaks that validation. The schema document
provides `manifest.puuid` (the `PUUID` field) as the plugin-level identity UUID for exactly
this purpose. When `PUUID` is absent (which is allowed), the host should fall back to the
first action's UUID prefix or the directory bundle name.

**Fix:** Prefer `manifest.puuid` for `-pluginUUID` when it is non-empty, keeping `codePath`
only as the `m_live` key fallback:

```cpp
// Use PUUID as the plugin's identity UUID (per akp_plugin_sdk.md §2 PUUID).
// Fall back to codePath/name only as the m_live map key.
QString const pluginId = manifest.codePath.isEmpty() ? manifest.name : manifest.codePath;
QString const pluginUuid = manifest.puuid.isEmpty() ? pluginId : manifest.puuid;
// Validate both:
if (!isSafeUuidComponent(pluginId)) { ... }
if (!manifest.puuid.isEmpty() && !isSafeUuidComponent(pluginUuid)) { ... }
// Then pass pluginUuid to buildNodeArgv, not pluginId.
QStringList const argv = buildNodeArgv(code, port, pluginUuid, infoJson);
```

Note: the tests at `test_plugin_lifecycle.cpp:164` and `:193` pin the current (codePath)
behaviour, so they would need updating when this is fixed.

______________________________________________________________________

## Info

### IN-01: `discover()` reads manifest files without a size cap

**File:** `src/app/src/plugin_manager.cpp:164`
**Issue:**

```cpp
QByteArray const data = f.readAll();
```

`plugin_manifest.hpp:105` documents the `T-18-MANIFEST` invariant: _"hostile/oversized input
returns nullopt"_. `parsePluginManifest()` itself is safe (QJsonDocument returns null on bad
input), but `discover()` calls `f.readAll()` without any byte limit before the parser sees
the data. A malicious (or corrupt) manifest.json of several hundred megabytes would be fully
read into memory before the parser rejects it. The plugins directory is nominally trusted, but
the path is user-configurable.

**Fix:** Add a size guard before `readAll()`:

```cpp
constexpr qint64 kMaxManifestBytes = 1 * 1024 * 1024; // 1 MiB is generous for any real manifest
if (f.size() > kMaxManifestBytes) {
    qWarning("PluginManager: manifest at %s is suspiciously large (%lld bytes); skipping",
             qPrintable(manifestPath), static_cast<long long>(f.size()));
    continue;
}
QByteArray const data = f.readAll();
```

______________________________________________________________________

_Reviewed: 2026-05-24_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
