# ADR: Windows Plugin Classification (WS-only-IPC vs Vendor-DLL)

**Status:** Accepted (2026-06-08)

______________________________________________________________________

## 1. Status

Accepted (2026-06-08). This ADR is the WINPLG-01 feasibility-spike decision record for
Phase 35. It documents the classification heuristic, the "WS-only-IPC runs natively"
decision, and the explicit deferral of the Wine launch path (WINPLG-03). It records a
locked CONTEXT decision (35-CONTEXT.md) and is not subject to re-litigation within v2.0.

______________________________________________________________________

## 2. Context

Elgato/AJAZZ `.sdPlugin` bundles declare an `OS` array and one or more code paths
(`CodePath`, `CodePathWin`, `CodePathMac`). Some plugins are Windows-only
(`OS=[{"Platform":"windows"}]`). Two very different kinds of plugin hide behind the same
"windows-only" label:

1. **WS-only-IPC plugins.** The code path is a `.js` / `.html` / `.cjs` entry point that
   connects back to the host over the loopback WebSocket (the Elgato `connectElgatoStreamDeckSocket`
   contract). These are platform-portable: Node.js / QWebEngine run them on Linux/macOS
   unchanged. They merely *declare* `windows` because the vendor ships them for the Windows
   Stream Deck app.

1. **Vendor-DLL plugins.** The code path is a native `.exe` / `.dll`, or the bundle ships a
   PE binary. These are genuinely Windows-native and cannot run on Linux/macOS without an
   emulation/compat layer.

**State before Phase 35:**

- `plugin_manifest.{hpp,cpp}` already parses `os[]`, `codePath`, `codePathWin`, `codePathMac`
  (`PluginManifest`, parsed at `parsePluginManifest`).
- `manifestRunnableHere()` (plugin_manifest.cpp) gates spawn eligibility and contains the
  LOCKED Linux OS-accept policy (a manifest with no explicit `linux` entry is accepted on
  Linux). It is wired at the discover() gate in `plugin_manager.cpp`.
- There was **no** way to tell a WS-only-IPC win plugin (safe to run natively) from a
  vendor-DLL win plugin (needs Windows). A strict `OS=["windows"]` array that does not match
  the host would be rejected by `manifestRunnableHere` for both kinds equally, OR (under the
  Linux-accept fall-through) accepted for both equally — neither is correct.
- `PluginInfo` (the UI-tier carrier, `i_plugin_host.hpp`) had no field to surface the verdict.

______________________________________________________________________

## 3. Decision

### 3.1 Classification heuristic — `classifyWindowsPlugin()`

A pure, never-throwing free function in `plugin_manifest.cpp` (mirroring the
`manifestRunnableHere` idiom) produces a three-way verdict
`WinPluginClass { NotWindowsOnly, WsOnlyIpc, VendorDll }`:

1. **Not windows-only.** OS array empty OR no `windows` entry -> `NotWindowsOnly`. (The
   caller continues to use `manifestRunnableHere` for these.)

1. **Primary signal — code-path suffix.** The effective Windows code path (`CodePathWin` if
   non-empty, else `CodePath`) ending case-insensitively in `.exe` / `.dll` -> `VendorDll`.

1. **Corroborator — bounded PE-magic scan.** Scan the extracted bundle dir for any file
   beginning with the DOS/PE magic bytes `MZ` (`0x4D 0x5A`). Any such file -> `VendorDll`,
   **regardless of the declared suffix**. This makes the classifier robust against a
   *mislabeled* manifest that declares a `.js` code path but ships a `.dll` (T-35-01-04).

1. **Otherwise** (windows-only, suffix `.js` / `.html` / `.cjs`, no PE) -> `WsOnlyIpc`.

**Security discipline (T-35-01-01 / T-35-01-02):** the scan reads **only the first 2 magic
bytes** of each file, never parses a full PE header, and **never executes** any bundle file.
It is bounded by a file-count cap (~4096) to prevent a DoS bundle from stalling the scan. The
bundle is already extracted and zip-slip-guarded upstream (Phase-13 CR-01), so there is no
archive recursion to bound. **No in-process PE loader is built — that path is permanently
rejected.**

### 3.2 Classify at scan time, cache the verdict (locked)

Classification runs **once, at install/scan time** in `PluginManager::discover()`, where the
bundle dir (`sourceDir`) is available for the PE-magic scan. The verdict is cached on the
runtime-populated `PluginManifest::winClass` field, carried through `spawn()` into the live
inventory, and stamped onto `PluginInfo::winClass` by `PluginManager::plugins()`. The UI model
(Plan 02) reads the cached `int` verdict **without re-scanning** — `PluginInfo` carries no
bundle path, so a lazy launch-time PE scan would be impossible anyway. Lazy classification at
launch is explicitly rejected (locked CONTEXT decision).

`PluginInfo::winClass` is a plain `int` (0 = NotWindowsOnly, 1 = WsOnlyIpc, 2 = VendorDll),
**not** the app-tier `WinPluginClass` enum, to keep the plugins-tier public header free of any
Qt / nlohmann / app dependency (COD-031 boundary).

### 3.3 WS-only-IPC runs natively — `supportsCurrentPlatform()`

A second pure helper decides native-run eligibility from the verdict:

- `WsOnlyIpc` -> **true on every platform.** A WS/IPC win-only plugin runs natively on
  Linux/macOS with no Wine. At the discover() gate the plugin is accepted when
  `manifestRunnableHere(...) || supportsCurrentPlatform(...)` — so an `OS=["windows"]` WS-only
  plugin that the base gate would strict-reject is admitted via the native-run override. The
  existing LOCKED Linux-accept policy in `manifestRunnableHere` is **preserved unchanged**.
- `VendorDll` -> **true only when** `platform == "windows"`. Off Windows it is **strict-rejected**
  (not spawned); it surfaces only as a status chip.
- `NotWindowsOnly` -> false (the caller falls back to `manifestRunnableHere`).

### 3.4 Wine launch — EXPLICITLY DEFERRED (WINPLG-03)

The actual Wine launch path for `VendorDll` plugins — `QStandardPaths::findExecutable("wine")`,
a per-plugin `WINEPREFIX`, and spawning the DLL host under Wine — is **DEFERRED to a future
phase / HUMAN-UAT follow-up**, for two reasons:

1. **No Wine/Windows hardware to live-test here.** Building untestable launch code is rejected
   on this project (CLAUDE.md debug-channel verification rule — ctest-green is necessary but
   not sufficient; every behavior must be live-driven before "done").

1. **Anti-decision: never bundle Wine.** Wine is never shipped with the application. If a
   future phase wires the launch path, it must detect a *user-installed* Wine, not bundle one.

In `supportsCurrentPlatform`, `wineDetected` is therefore hard-coded `false` this phase, with
an inline comment marking the WINPLG-03 deferral point. The detection + chip ("Requires Wine"
/ "Unsupported on this OS") is the deliverable; the launch is not.

______________________________________________________________________

## 4. Consequences

### Positive

- WS-only-IPC win plugins now load and run natively on Linux/macOS — the largest practical
  class of "Windows" plugins becomes usable on the primary OS without Wine.
- The classifier is robust against a mislabeled manifest (the PE-magic corroborator).
- Zero new attack surface this phase: no PE loader, no Wine, magic-bytes-only inspection.
- The verdict flows to the UI tier as a cached `int` with no COD-031 violation and no re-scan.

### Negative / accepted trade-offs

- A WS-only win plugin that ships an *unused* `.dll` in its bundle is conservatively
  classified `VendorDll` and will not run natively. This is the accepted CONTEXT trade-off
  ("robust against mislabeled manifest"): a false-positive vendor-DLL is a missed-native-run
  inconvenience, whereas a false-negative would risk treating a native binary as portable.
- `VendorDll` plugins surface as a **chip only** this phase (Plan 02) — they cannot be launched
  off Windows until WINPLG-03 lands. This is recorded as a partial/deferred status, honestly.

### Follow-ups

- WINPLG-03 launch path (Wine detection + per-plugin prefix + DLL-host spawn) — future phase
  / HUMAN-UAT, gated on Wine/Windows hardware.
- Plan 02 consumes `PluginInfo::winClass` to render the objectName-addressed status chip on
  the loaded-plugins surface (VERIF-01).

______________________________________________________________________

## 5. Implementation references

- `src/app/src/plugin_manifest.hpp` — `WinPluginClass` enum, `classifyWindowsPlugin()`,
  `supportsCurrentPlatform()` declarations.
- `src/app/src/plugin_manifest.cpp` — heuristic (suffix + bounded MZ-magic corroborator) +
  native-run helper, next to `manifestRunnableHere`.
- `src/app/src/plugin_manager.cpp` — scan-time `classifyWindowsPlugin` call + the WINPLG-02
  native-run override at the discover() gate; `plugins()` stamps `PluginInfo::winClass`.
- `src/plugins/include/ajazz/plugins/i_plugin_host.hpp` — `PluginInfo::winClass` (plain int).
- `tests/unit/test_win_plugin_classification.cpp` — `[win-plugin-classification]` Catch2 suite.
