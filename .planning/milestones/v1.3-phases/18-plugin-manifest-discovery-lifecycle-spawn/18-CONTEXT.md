# Phase 18: Plugin Manifest + Discovery + Lifecycle + Spawn - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + akp_plugin_sdk.md §2/§3/§9 + existing-code survey

<domain>
## Phase Boundary

Phase 18 makes plugins **discoverable, validated, and runnable**: parse the manifest schema,
discover + extract `.sdPlugin` packages, spawn the three runtimes (Node.js / native exe / HTML),
manage the crash/restart/shutdown lifecycle, and load existing Elgato/Mirabox packages unchanged.
Spawned plugins connect back to the **Phase-17** `SdPluginServer` (which already speaks the full
protocol). It is hardware-free and device-free.

**Delivers (PLUGIN-06/07/08/11):**

- A **manifest parser** for the Elgato v6 schema **plus AJAZZ extensions** (`IsK1Pro`,
  `RunAsAdministrator`, `FSize`/`FFamily`, `Nodejs.Version`, `PUUID`, `Controllers` incl.
  **`"Knob"`** for encoders and `"SecondaryScreen"` for the touch strip); rejects on `OS` /
  `Software.MinimumVersion` mismatch (PLUGIN-06).
- **Discovery** of `defaultPlugins/` + `installedPlugins/` + `.sdPlugin` extraction (reuse the
  existing zip-slip-guarded extractor) + **lifecycle**: spawn, crash-3×-within-30s → disable +
  notify, restart, shutdown via the `exitApp` event (PLUGIN-07).
- **Process spawn** for all three runtimes — **system Node.js ≥20** (detected, rejected
  otherwise; **NOT bundled**), native `.exe`/`.app` via `QProcess` (optional Windows elevation),
  and `.html` via `QWebEngineView`+`QWebChannel` (reuse the existing PI WebView stack) (PLUGIN-08).
- A **Mirabox/Elgato compat shim** aliasing `connectMiraBoxSDSocket(...)` →
  `connectElgatoStreamDeckSocket(...)` so existing packages load unmodified (PLUGIN-11).

**Out of scope:** the device↔plugin bridge (Phase 19); the Property Inspector *settings UI*
(Phase 20 — but its WebView/bridge infra is reused here for HTML plugin hosting); built-in
in-process actions (Phase 21); plugin store install UX (Phase 22).
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Reuse-first

- **Extraction is done** — reuse `sdplugin_extractor` (`extractSdPluginArchive` +
  `extractStandalonePluginArchives` sweep). Do NOT rewrite extraction.
- **Extend the existing manifest parsing** in `plugin_catalog_model.cpp` (it already parses some
  manifest fields for the catalog) into a full schema parser/validator — or factor a shared
  `PluginManifest` parser both consume. Reuse `loaded_plugins_model` to track running plugins.
- **HTML plugins reuse the existing PI WebView/bridge stack** (`pi_bridge`, `PIWebView.qml`,
  `pi_url_request_interceptor`/`pi_url_policy`, `property_inspector_controller`) for hosting the
  plugin's `index.html` — the same `QWebEngineView`+`QWebChannel` infra. The full Property
  Inspector *settings UI* is Phase 20; Phase 18 reuses the WebView plumbing for the plugin main view.
- Spawned plugins connect back to the **Phase-17 `SdPluginServer`** port; pass the standard CLI:
  `<node> <codePath> -port <p> -pluginUUID <u> -registerEvent registerPlugin -info <info>`.

### Spawn (PLUGIN-08) — per akp_plugin_sdk.md §3, with our security deltas

- **Node.js: detect system `node` ≥ 20** (resolve on PATH, check `--version`); if absent/older,
  fail the plugin with a clear notification — **do NOT bundle `node20.exe`** (anti-feature).
- **Native exe/app:** `QProcess::start`; `RunAsAdministrator:true` → Windows UAC elevation only
  (Linux/mac: ignore or refuse — document).
- **HTML:** load `index.html` in a `QWebEngineView`; the JS calls
  `connectElgatoStreamDeckSocket(...)` (Mirabox alias shimmed).

### Manifest (PLUGIN-06)

- Validate `OS` array contains the current platform and `Software.MinimumVersion` ≤ running
  version → else reject (don't spawn). Accept the AJAZZ extension fields without choking.
- `Controllers` must accept `"Keypad"`, `"Knob"`, `"Information"`, `"SecondaryScreen"` (the Knob
  value is what binds an action to an encoder — first-class for the AKP05E's 4 knobs).

### Lifecycle (PLUGIN-07)

- `QProcess::errorOccurred`/`finished` → log `"The plugin '%1' crashed: %2"`, increment a
  per-plugin crash counter; **3 crashes within ~30 s → disable + notify** (surface via
  `loaded_plugins_model`). Restart = teardown + re-spawn. Shutdown at app exit = send the
  `exitApp` event (via the Phase-17 `sendEvent`), then `QProcess::terminate()` (1 s grace) → `kill()`.

### Mirabox shim (PLUGIN-11)

- Inject a JS pre-script into HTML plugins aliasing `connectMiraBoxSDSocket` →
  `connectElgatoStreamDeckSocket` (same signature) so AJAZZ-only packages load unmodified.

### Claude's Discretion (planner/executor)

- One `PluginManifest` struct + parser shared by catalog + spawn, vs extending the catalog parser in place.
- Where the spawn/lifecycle manager lives (a new `PluginManager`/`PluginHostController` in `src/app/src/` composing the extractor + SdPluginServer + loaded_plugins_model).
- Node-version detection mechanism + how absence is surfaced.
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `docs/protocols/streamdeck/akp_plugin_sdk.md` — §2 manifest schema (the field table + AJAZZ extensions), §3 lifecycle (discovery/spawn/crash/restart/shutdown + the node CLI argv), §9 corrections (target test names: `PluginManifestTest::rejects_macOnly_onLinux`, `NodeRunnerTest::buildsCorrectArgv`, `CompatTest::miraboxSocket_aliasedToElgato`).
- `src/app/src/sdplugin_extractor.{hpp,cpp}` — `extractSdPluginArchive` + `extractStandalonePluginArchives` (reuse; zip-slip-guarded).
- `src/app/src/plugin_catalog_model.{hpp,cpp}` — existing manifest parsing to extend/share.
- `src/app/src/loaded_plugins_model.{hpp,cpp}` — running-plugin tracking (crash/disable surface).
- `src/app/src/pi_bridge.hpp` + `PIWebView.qml` + `pi_url_request_interceptor.hpp` + `pi_url_policy.hpp` + `property_inspector_controller.{hpp,cpp}` — the QWebEngine+QWebChannel stack to reuse for HTML plugin hosting.
- `src/app/src/sd_plugin_server.{hpp,cpp}` (Phase 17) — the loopback server spawned plugins connect to; the `-port`/`-pluginUUID` args + `sendEvent` (for `exitApp`).
- CLAUDE.md — never QCefView (use QWebEngineView + Qt6::WebChannelQuick); COD-031; never skip pre-commit; ASCII test names. Anti-feature: do NOT bundle node20.
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free and mostly process-free via fixtures: manifest parse/validate
  unit tests (accept Elgato v6 + AJAZZ ext incl. `Controllers:["Knob"]`; reject mac-only on
  Linux and a too-high `Software.MinimumVersion`); discovery over a temp dir seeded with fixture
  `.sdPlugin` packages; **node argv construction** test (assert the exact CLI without needing a
  live node — `NodeRunnerTest::buildsCorrectArgv`); node-version detection (mock the probe);
  crash-counter logic (3-in-30s → disabled) via injected fake process events; Mirabox shim JS
  injection test (the alias is defined before the plugin script runs). A real end-to-end spawn
  (live node, live WebSocket round-trip) is an optional integration test gated on node presence;
  the full live `.sdPlugin` witness is Phase 25 (VERIFY-06). ASCII-only test names.

</specifics>

<deferred>
## Deferred Ideas

- Device↔plugin bridge (actionReceived→device; device input→sendEvent; setImage e2e) → Phase 19.
- Property Inspector *settings UI* + settings persistence → Phase 20 (its WebView infra is reused here).
- Built-in in-process actions → Phase 21. Plugin store install UX → Phase 22.
- Live third-party `.sdPlugin` end-to-end witness → Phase 25 (VERIFY-06).

</deferred>

______________________________________________________________________

*Phase: 18-plugin-manifest-discovery-lifecycle-spawn*
*Context gathered: 2026-05-23 (v1.3 replan locked decisions; akp_plugin_sdk.md §2/§3 is the spec)*
