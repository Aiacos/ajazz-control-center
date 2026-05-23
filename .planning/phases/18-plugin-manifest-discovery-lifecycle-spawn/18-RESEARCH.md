# Phase 18: Plugin Manifest + Discovery + Lifecycle + Spawn - Research

**Researched:** 2026-05-23
**Domain:** Qt 6 / C++20 out-of-process plugin host — manifest parsing, discovery, QProcess spawn, QWebEngine HTML hosting, crash lifecycle
**Confidence:** HIGH (codebase-verified) / MEDIUM (Qt API behaviours from official docs)

## Summary

Phase 18 makes Stream Dock `.sdPlugin` packages discoverable, validated, and runnable. Almost
everything it needs already exists in the codebase as reusable building blocks: the
zip-slip-guarded **extractor** (`sdplugin_extractor.{hpp,cpp}`), the **catalog** that already does
light manifest reading, the **Phase-17 WebSocket server** (`SdPluginServer`) that spawned plugins
connect back to, the **QWebEngine + QWebChannel PI stack** (`PropertyInspectorController`,
`PIWebView.qml`, `pi_bridge`, `pi_url_policy`), and the **`LoadedPluginsModel`** for surfacing
running/crashed state. The phase is overwhelmingly *new wiring + a manifest schema parser*, not
new infrastructure — exactly as the locked CONTEXT decisions require.

Two findings change the planning shape and must be surfaced to the user/planner:

1. **The Phase-17 `SdPluginServer` does NOT yet expose any `sendEvent` / `sendTextMessage` /
   `exitApp` method.** The CONTEXT.md and the task brief both say "shutdown via the Phase-17
   `sendEvent`", but `grep` confirms no such API exists (the server is receive-only: it parses
   inbound frames and emits Qt signals; it never sends a frame back). Phase 18 must **add** a
   host→plugin send path to `SdPluginServer` (e.g. `sendEvent(uuid, QJsonObject)` writing to the
   matching `QWebSocket`) before the `exitApp` shutdown can work. This is a small additive change
   but it is a real prerequisite, not a "reuse".

1. **The manifest parsing in `plugin_catalog_model.cpp` is NOT a manifest parser** — it parses a
   *catalogue feed* (mock fixture + remote Streamdock/OpenDeck JSON), not on-disk `manifest.json`
   files. There is **no `PluginManifest` struct anywhere** and **no manifest.json reader**. So
   PLUGIN-06 is a genuinely new parser; "extend the catalog parser" is the wrong framing — the
   catalog never reads manifests. Recommend a **standalone `PluginManifest` struct + free
   `parsePluginManifest()` parser** in `src/app/src/` that both discovery (Phase 18) and any future
   catalog enrichment can call.

**Primary recommendation:** Build a small `PluginManager`/`PluginHostController` in
`src/app/src/` that composes (a) a new `PluginManifest` parser, (b) the existing extractor, (c) a
new `NodeRunner` argv builder + a `QProcess`-based spawner, (d) the existing
`PropertyInspectorController` WebEngine stack for HTML plugins with a new `QWebEngineScript`
document-creation injection for the Mirabox alias, and (e) the existing `LoadedPluginsModel` for
crash/disable surfacing — plus a small additive `sendEvent` on `SdPluginServer`. Keep every new
piece pure/injectable so the bulk of verification is hardware-free and process-free via fixtures.

## User Constraints (from CONTEXT.md)

### Locked Decisions

**Reuse-first:**

- Extraction is done — reuse `sdplugin_extractor` (`extractSdPluginArchive` +
  `extractStandalonePluginArchives`). Do NOT rewrite extraction.
- Extend existing manifest parsing in `plugin_catalog_model.cpp` into a full schema
  parser/validator, OR factor a shared `PluginManifest` parser both consume. Reuse
  `loaded_plugins_model` to track running plugins. *(See finding #2 — the catalog does NOT parse
  manifests today; the "shared `PluginManifest` parser" branch is the correct one.)*
- HTML plugins reuse the existing PI WebView/bridge stack (`pi_bridge`, `PIWebView.qml`,
  `pi_url_request_interceptor`/`pi_url_policy`, `property_inspector_controller`) for hosting the
  plugin `index.html`. Full PI settings UI is Phase 20; Phase 18 reuses the WebView plumbing for the
  plugin main view.
- Spawned plugins connect back to the Phase-17 `SdPluginServer` port; pass the standard CLI:
  `<node> <codePath> -port <p> -pluginUUID <u> -registerEvent registerPlugin -info <info>`.

**Spawn (PLUGIN-08):**

- Node.js: detect system `node` ≥ 20 (resolve on PATH, check `--version`); if absent/older, fail
  the plugin with a clear notification — do NOT bundle `node20.exe` (anti-feature).
- Native exe/app: `QProcess::start`; `RunAsAdministrator:true` → Windows UAC elevation only
  (Linux/mac: ignore or refuse — document).
- HTML: load `index.html` in a `QWebEngineView`; JS calls `connectElgatoStreamDeckSocket(...)`
  (Mirabox alias shimmed).

**Manifest (PLUGIN-06):**

- Validate `OS` array contains the current platform and `Software.MinimumVersion` ≤ running
  version → else reject (don't spawn). Accept the AJAZZ extension fields without choking.
- `Controllers` must accept `"Keypad"`, `"Knob"`, `"Information"`, `"SecondaryScreen"` (the `Knob`
  value binds an action to an encoder — first-class for the AKP05E's 4 knobs).

**Lifecycle (PLUGIN-07):**

- `QProcess::errorOccurred`/`finished` → log `"The plugin '%1' crashed: %2"`, increment a
  per-plugin crash counter; 3 crashes within ~30 s → disable + notify (surface via
  `loaded_plugins_model`). Restart = teardown + re-spawn. Shutdown at app exit = send the `exitApp`
  event (via the Phase-17 `sendEvent`), then `QProcess::terminate()` (1 s grace) → `kill()`.

**Mirabox shim (PLUGIN-11):**

- Inject a JS pre-script into HTML plugins aliasing `connectMiraBoxSDSocket` →
  `connectElgatoStreamDeckSocket` (same signature) so AJAZZ-only packages load unmodified.

### Claude's Discretion

- One `PluginManifest` struct + parser shared by catalog + spawn, vs extending the catalog parser
  in place.
- Where the spawn/lifecycle manager lives (a new `PluginManager`/`PluginHostController` in
  `src/app/src/` composing the extractor + SdPluginServer + loaded_plugins_model).
- Node-version detection mechanism + how absence is surfaced.

### Deferred Ideas (OUT OF SCOPE)

- Device↔plugin bridge (actionReceived→device; device input→sendEvent; setImage e2e) → Phase 19.
- Property Inspector *settings UI* + settings persistence → Phase 20 (its WebView infra is reused
  here).
- Built-in in-process actions → Phase 21. Plugin store install UX → Phase 22.
- Live third-party `.sdPlugin` end-to-end witness → Phase 25 (VERIFY-06).

## Phase Requirements

| ID        | Description                                                                                                                                                                                                                        | Research Support                                                                                                                                                                                                                                     |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PLUGIN-06 | Manifest parser: Elgato v6 + AJAZZ extensions (`IsK1Pro`, `RunAsAdministrator`, `FSize`/`FFamily`, `Nodejs.Version`, `PUUID`, `Controllers` incl. `"Knob"`/`"SecondaryScreen"`); reject on `OS`/`Software.MinimumVersion` mismatch | New `PluginManifest` struct + `parsePluginManifest()` (no existing manifest reader); schema table in akp_plugin_sdk.md §2; running version via `QCoreApplication::applicationVersion()` (set in `main.cpp:63` = `"0.1.0"`)                           |
| PLUGIN-07 | Discovery (`defaultPlugins/` + `installedPlugins/`) + extraction + lifecycle (spawn / crash-3×-in-30s→disable+notify / restart / shutdown via `exitApp`)                                                                           | Reuse `extractStandalonePluginArchives`; `QStandardPaths::AppDataLocation/plugins` already used by catalog; crash counter is new logic; `LoadedPluginsModel::setPlugins` for surfacing; `exitApp` needs new `SdPluginServer::sendEvent` (finding #1) |
| PLUGIN-08 | Spawn all three runtimes: system Node ≥20 (detected, not bundled), native exe/app via `QProcess`, HTML via `QWebEngineView`+`QWebChannel`                                                                                          | New `NodeRunner` argv builder + `QProcess` spawner; `QStandardPaths::findExecutable` for node; reuse `PropertyInspectorController` WebEngine surface for HTML                                                                                        |
| PLUGIN-11 | Compat shim aliasing `connectMiraBoxSDSocket` → `connectElgatoStreamDeckSocket` so packages load unmodified                                                                                                                        | New `QWebEngineScript` at `DocumentCreation` injection point on the per-plugin profile (no existing shim — verified absent)                                                                                                                          |

## Architectural Responsibility Map

| Capability                     | Primary Tier                       | Secondary Tier       | Rationale                                                                             |
| ------------------------------ | ---------------------------------- | -------------------- | ------------------------------------------------------------------------------------- |
| Manifest parse + validate      | App / C++ logic (pure)             | —                    | Pure data transform; must be unit-testable without Qt GUI/WebEngine                   |
| Discovery (dir scan + extract) | App / C++ logic                    | Filesystem           | Reuses extractor; reads `defaultPlugins/`+`installedPlugins/` under `QStandardPaths`  |
| Node.js spawn                  | App / OS process                   | OS PATH (`node`)     | Child process is the plugin runtime; host owns lifecycle, node is external dependency |
| Native exe/app spawn           | App / OS process                   | OS (UAC on Win)      | `QProcess`; elevation is Windows-shell concern                                        |
| HTML plugin hosting            | App / QWebEngine (Chromium)        | QWebChannel bridge   | Reuse PI WebEngine surface; the plugin's index.html runs in-process in Chromium       |
| Mirabox→Elgato alias           | App / QWebEngine UserScript        | —                    | JS injected at document-creation, before plugin script runs                           |
| Plugin↔host transport          | App / WebSocket (`SdPluginServer`) | loopback TCP         | Spawned plugins dial `ws://127.0.0.1:<port>`; host must add a send path for `exitApp` |
| Crash counter + disable        | App / C++ logic (pure)             | `LoadedPluginsModel` | Counting logic is pure + injectable; model surfaces state to QML                      |

## Standard Stack

All Qt 6 — no new third-party dependencies. Everything below is already linked into the app or
trivially addable.

### Core

| Library                | Version | Purpose                                                                                  | Why Standard                                                     |
| ---------------------- | ------- | ---------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| `Qt6::Core`            | 6.7+    | `QProcess`, `QStandardPaths`, `QJsonDocument`, `QVersionNumber`, `QDir`                  | Already linked; manifest parse + spawn + discovery all use Core  |
| `Qt6::CorePrivate`     | 6.7+    | `QZipReader`/`QZipWriter` (already used by extractor + its tests)                        | The only Qt module exposing zip; extractor already depends on it |
| `Qt6::WebSockets`      | 6.7+    | `SdPluginServer` transport (Phase 17) — needs a new `sendEvent` send path                | Already gated via `AJAZZ_HAVE_WEBSOCKETS`                        |
| `Qt6::WebEngineQuick`  | 6.7+    | HTML plugin hosting (`WebEngineView`)                                                    | Already gated via `AJAZZ_HAVE_WEBENGINE`; PI stack uses it       |
| `Qt6::WebChannelQuick` | 6.7+    | JS↔C++ bridge (`QQmlWebChannel`, `$SD`)                                                  | Mandated by CLAUDE.md (NOT `WebChannel`, NOT QCefView)           |
| `Qt6::WebEngineCore`   | 6.7+    | `QWebEngineScript` / `QWebEngineProfile::scripts()` for the Mirabox UserScript injection | Same module family; pulled by WebEngineQuick                     |

**No `npm`/`pip`/`cargo` packages** — this is a C++ phase with zero new external package
dependencies. The "node ≥20" is a *runtime probe of a user-installed tool*, not a bundled
dependency (anti-feature per CONTEXT). The Package Legitimacy Audit section is therefore N/A.

### Supporting

| API                                                                    | Purpose                                           | When to Use                                                                                        |
| ---------------------------------------------------------------------- | ------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| `QStandardPaths::findExecutable("node")`                               | Resolve `node` on PATH                            | Node detection; returns empty if absent `[CITED: doc.qt.io/qt-6/qstandardpaths.html]`              |
| `QProcess` (`start`, `errorOccurred`, `finished`, `terminate`, `kill`) | Spawn + lifecycle                                 | All process spawn paths                                                                            |
| `QVersionNumber::fromString` / `compare`                               | `Software.MinimumVersion` ≤ running version check | Manifest validation; tolerant of `"2.9"` / `"1.0.0"` `[CITED: doc.qt.io/qt-6/qversionnumber.html]` |
| `QWebEngineScript` + `QWebEngineProfile::scripts()->insert()`          | Inject Mirabox alias at `DocumentCreation`        | HTML plugin load (PLUGIN-11) `[CITED: doc.qt.io/qt-6/qwebenginescript.html]`                       |
| `QSysInfo::productType()` / compile-time `Q_OS_*`                      | Current platform for `OS` array match             | Manifest validation                                                                                |

### Alternatives Considered

| Instead of                       | Could Use                      | Tradeoff                                                                                                                                     |
| -------------------------------- | ------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `QStandardPaths::findExecutable` | manual `PATH` walk             | Reinvents a tested Qt helper; don't                                                                                                          |
| `QWebEngineScript` injection     | `runJavaScript` after load     | Race: plugin's own script may run first; document-creation UserScript is the only correct injection point for an alias the plugin depends on |
| New `PluginManager` class        | bolt onto `PluginCatalogModel` | Catalog is a QML store-grid model; mixing process lifecycle into it violates its single responsibility and the QML_SINGLETON pattern         |
| `QProcess::startDetached`        | `QProcess::start` (owned)      | Detached gives no `finished`/`errorOccurred` signals → no crash counting. Must use owned `QProcess`                                          |

**No install command** — all dependencies already present in the build. WebEngine/WebSockets are
optional and gracefully degrade (`AJAZZ_HAVE_*` gates).

## Architecture Patterns

### System Architecture Diagram

```
                       ┌─────────────────────────────────────────────┐
  app startup ───────► │  PluginManager / PluginHostController (new)  │
                       └───────────────┬─────────────────────────────┘
                                       │
       ┌───────────────────────────────┼───────────────────────────────┐
       │                               │                               │
       ▼                               ▼                               ▼
 ┌───────────┐               ┌──────────────────┐            ┌──────────────────┐
 │ DISCOVERY │               │ MANIFEST PARSE   │            │  SdPluginServer  │
 │ scan dirs │──manifest.json──►│ + VALIDATE (new) │            │ (Phase 17, +new  │
 │ defaultP/ │  paths        │ PluginManifest   │            │  sendEvent path) │
 │ installed/│               │ OS / MinVer gate │            │  ws://127.0.0.1  │
 └─────┬─────┘               └────────┬─────────┘            └────────▲─────────┘
       │ leftover *.sdPlugin           │ valid?                        │ registerPlugin
       ▼ (extract via reuse)           ▼ yes                           │ + exitApp (new)
 ┌───────────────────┐        ┌───────────────────┐                    │
 │ sdplugin_extractor│        │   SPAWN (by ext)  │                    │
 │ (REUSE, zip-slip) │        ├───────────────────┤                    │
 └───────────────────┘        │ .js/.mjs/.cjs ────┼── QProcess(node argv)──► node child ──┐
                              │ .exe/.app/none ───┼── QProcess(codePath) ──► native child ┤
                              │ .html ────────────┼── QWebEngineView ──────► in-proc page ┤
                              └─────────┬─────────┘    (Mirabox UserScript)               │
                                        │                                                  │
                                        ▼ errorOccurred / finished        connect back ────┘
                              ┌───────────────────┐
                              │ CRASH COUNTER (new)│  3-in-30s ──► disable + notify
                              │ per-plugin window  │──────────────► LoadedPluginsModel (REUSE)
                              └───────────────────┘                  └─► QML running-plugins UI
```

Primary use case trace: startup → discover dirs → (extract leftovers) → parse+validate each
manifest → if valid, spawn by `CodePath` extension → child dials the WebSocket server and sends
`registerPlugin` → server emits `pluginRegistered` → manager records it in `LoadedPluginsModel`.
On crash, the per-plugin counter decides restart-vs-disable. On app exit, manager sends `exitApp`
to each plugin then terminates the processes.

### Recommended Project Structure

```
src/app/src/
├── plugin_manifest.hpp/.cpp        # NEW: PluginManifest struct + parsePluginManifest() (pure)
├── node_runner.hpp/.cpp            # NEW: buildNodeArgv() (pure) + node detection (injectable)
├── plugin_manager.hpp/.cpp         # NEW: discovery + spawn + crash lifecycle orchestrator
├── plugin_crash_tracker.hpp/.cpp   # NEW (or inline in manager): pure 3-in-30s window logic
├── sdplugin_extractor.hpp/.cpp     # REUSE unchanged
├── sd_plugin_server.hpp/.cpp       # EXTEND: add sendEvent(uuid, QJsonObject) host→plugin
├── loaded_plugins_model.hpp/.cpp   # REUSE: setPlugins() to surface running/disabled state
├── property_inspector_controller   # REUSE: WebEngine surface for HTML plugins
├── pi_bridge / pi_url_policy        # REUSE: $SD bridge + URL allow/deny
└── plugin_mirabox_shim.hpp          # NEW: the JS alias source string (constexpr) + QWebEngineScript builder
```

### Pattern 1: Pure parser, injectable side effects

**What:** `parsePluginManifest(QByteArray json) -> std::optional<PluginManifest>` and
`buildNodeArgv(...) -> QStringList` are **pure functions**; the `QProcess` spawn, the node-version
probe, and the directory scan are thin wrappers that call them. Node detection takes a resolver
functor so tests inject a fake.
**When to use:** Everywhere the verification must be hardware-free/process-free.
**Example (argv builder — matches `NodeRunnerTest::buildsCorrectArgv`):**

```cpp
// Source: akp_plugin_sdk.md §3 spawn CLI + §9 NodeRunner target
QStringList buildNodeArgv(QString const& codePath, quint16 port,
                          QString const& pluginUuid, QString const& infoJson) {
    return { codePath, "-port", QString::number(port),
             "-pluginUUID", pluginUuid,
             "-registerEvent", "registerPlugin",
             "-info", infoJson };
    // QProcess::start(nodeExe, buildNodeArgv(...)) — nodeExe resolved separately.
}
```

### Pattern 2: Mirabox alias via document-creation UserScript

**What:** Inject a `QWebEngineScript` whose `injectionPoint == DocumentCreation` and
`worldId == MainWorld` into the per-plugin `QWebEngineProfile` *before* loading the plugin's
`index.html`, so `connectMiraBoxSDSocket` is defined before the plugin's own script runs and tries
to call it.
**When to use:** HTML plugin hosting (PLUGIN-11).
**Example:**

```cpp
// Source: doc.qt.io/qt-6/qwebenginescript.html (injectionPoint, worldId)
QWebEngineScript shim;
shim.setName("ajazz-mirabox-shim");
shim.setInjectionPoint(QWebEngineScript::DocumentCreation);
shim.setWorldId(QWebEngineScript::MainWorld);
shim.setRunsOnSubFrames(true);
shim.setSourceCode(QStringLiteral(
    "window.connectMiraBoxSDSocket = function(){"
    "  return window.connectElgatoStreamDeckSocket.apply(window, arguments);"
    "};"));
profile->scripts()->insert(shim);   // profile is the per-plugin QQuickWebEngineProfile
```

Note: `PropertyInspectorController` owns a `QQuickWebEngineProfile` per plugin UUID (see
`property_inspector_controller.hpp:104` `activeProfile`). The shim attaches to that same profile.
`PropertyInspectorController::WebEngineImpl` is a PIMPL — the planner must decide whether the shim
lives in the controller or a sibling helper the manager wires in.

### Pattern 3: Crash window counter

**What:** Per-plugin ring of recent crash timestamps; on each `errorOccurred`/abnormal `finished`,
push `now`, drop entries older than 30 s, and if `count >= 3` mark disabled + notify instead of
restarting.
**When to use:** PLUGIN-07 lifecycle. Keep the window logic pure (inject a clock) so
`PluginCrashTrackerTest` feeds synthetic timestamps with no real process.

### Anti-Patterns to Avoid

- **Creating `src/host/plugin-host/`.** akp_plugin_sdk.md §9 proposes that module path, but
  CONTEXT.md + the task brief explicitly forbid it — reuse `src/app/src/`. The §9 table is a
  pre-GSD RE artifact; CONTEXT wins.
- **Using QCefView / bundling node20.** Forbidden by CLAUDE.md + CONTEXT. Use QWebEngineView; detect
  system node.
- **`QProcess::startDetached`.** Loses the crash signals the lifecycle depends on.
- **Aligning the manifest reader to C++ field names.** CLAUDE.md hard rule: the schema doc
  (`akp_plugin_sdk.md §2`) is the source of truth for JSON keys. Read `"CodePath"`, `"Nodejs"`,
  `"OS"` exactly as the schema spells them.
- **`runJavaScript` after page load for the alias.** Races the plugin script; use
  document-creation injection.

## Don't Hand-Roll

| Problem                         | Don't Build             | Use Instead                                                  | Why                                                               |
| ------------------------------- | ----------------------- | ------------------------------------------------------------ | ----------------------------------------------------------------- |
| Zip extraction + zip-slip guard | custom unzip            | `extractSdPluginArchive` / `extractStandalonePluginArchives` | Already exists, tested, slip-guarded (issue #62 / Phase 13 CR-01) |
| Find `node` on PATH             | manual PATH split       | `QStandardPaths::findExecutable`                             | Cross-platform, handles `.exe` resolution on Windows              |
| Version comparison              | string compare          | `QVersionNumber`                                             | `"2.10"` > `"2.9"` correctly; string compare gets it wrong        |
| JS↔C++ HTML bridge              | raw IPC / QCefView      | `QQmlWebChannel` + `$SD` (`pi_bridge`)                       | Already built; mandated by CLAUDE.md                              |
| Per-plugin storage isolation    | shared profile          | per-UUID `QQuickWebEngineProfile`                            | Already done in `PropertyInspectorController`                     |
| Atomic settings write           | hand-rolled temp+rename | `QSaveFile` (see `pi_bridge.cpp`)                            | Established pattern                                               |
| Running-plugin UI model         | new model               | `LoadedPluginsModel::setPlugins`                             | Already a QML singleton with trust/state roles                    |

**Key insight:** Phase 18 is ~80% composition of existing, tested parts. The only genuinely new
*infrastructure* is the manifest parser, the node argv/detection, the crash-window counter, and a
small `sendEvent` addition to the server — all small and pure-testable.

## Runtime State Inventory

> Phase 18 is greenfield wiring (no rename/refactor of stored state). The "state" here is what the
> manager reads/writes at runtime, surfaced for the planner.

| Category            | Items Found                                                                                                                                                                                            | Action Required                                                                                                                                                                        |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Stored data         | Per-plugin settings under `<AppDataLocation>/plugins/<uuid>/` (written by `pi_bridge.cpp`); downloaded `.sdPlugin` archives + extracted dirs under `<AppDataLocation>/plugins/` (catalog install path) | None — discovery reads these dirs; no migration                                                                                                                                        |
| Discovery dirs      | `defaultPlugins/` (bundled, read-only) + `installedPlugins/` per akp_plugin_sdk.md §3. Today the codebase only uses `<AppDataLocation>/plugins/` as the install/extract target                         | Planner must decide the canonical on-disk layout: map vendor `defaultPlugins/`+`installedPlugins/` onto our `QStandardPaths` dirs. Vendor paths (`%APPDATA%/HotSpot/...`) are NOT ours |
| Live service config | `SdPluginServer` port is OS-assigned (`start(0)`), not stable across runs (matches vendor `getsARandomServerPort`). Passed to each child via `-port` argv                                              | None — port is minted per session and handed to children                                                                                                                               |
| Secrets/env vars    | passHello/salt/challenge auth is Phase-17 deferred (server header §"NOT YET IMPLEMENTED"); not required for Phase 18 spawn                                                                             | None for this phase                                                                                                                                                                    |
| Build artifacts     | None                                                                                                                                                                                                   | None                                                                                                                                                                                   |

**Nothing found** in the rename/migration sense — verified by reading the existing
`plugin_catalog_model.cpp` install path and `pi_bridge.cpp` storage paths.

## Common Pitfalls

### Pitfall 1: "Reuse the catalog manifest parser" — there isn't one

**What goes wrong:** Planner writes tasks to "extend `plugin_catalog_model.cpp` manifest parsing".
**Why it happens:** CONTEXT.md says so, and the file is named plausibly.
**How to avoid:** `plugin_catalog_model.cpp` parses a *catalogue feed* (mock fixture + remote
Streamdock/OpenDeck JSON) into `CatalogEntry`. It never opens a `manifest.json`. There is no
`PluginManifest` struct. PLUGIN-06 is a new parser. **Warning sign:** searching for a manifest.json
read in the catalog returns nothing.

### Pitfall 2: `SdPluginServer` cannot send `exitApp` today

**What goes wrong:** Shutdown task assumes a Phase-17 `sendEvent`; build/grep shows it doesn't
exist.
**Why it happens:** The server is receive-only (parses inbound, emits signals). CONTEXT overstated
Phase-17's surface.
**How to avoid:** Add `SdPluginServer::sendEvent(QString uuid, QJsonObject)` that looks up the
plugin's `QWebSocket` in `m_connections` and `sendTextMessage`s the JSON. This is the prerequisite
for the `exitApp` shutdown step. **Warning sign:** `grep sendEvent src/app/src/sd_plugin_server.*`
is empty (verified 2026-05-23).

### Pitfall 3: Node argv must NOT include the node exe as `argv[0]`

**What goes wrong:** Putting `node` into the `QStringList` passed to `QProcess::start`.
**Why it happens:** Confusing the `system()`-style single string with `QProcess`'s (program, args).
**How to avoid:** `QProcess::start(nodeExe, argv)` — `argv[0]` is `codePath`, not the node binary.
The `NodeRunnerTest::buildsCorrectArgv` test asserts the arg list starts with `codePath`.

### Pitfall 4: Mirabox alias injected too late

**What goes wrong:** Plugin's `index.html` script calls `connectMiraBoxSDSocket` before the alias is
defined → ReferenceError.
**How to avoid:** `QWebEngineScript::DocumentCreation` injection point, `MainWorld`. Verify by a
test that loads a fixture HTML calling `connectMiraBoxSDSocket` and asserts it reached the bridge
(`CompatTest::miraboxSocket_aliasedToElgato`).

### Pitfall 5: `QProcess::terminate()` on Windows is a no-op for console apps

**What goes wrong:** Graceful 1 s `terminate()` does nothing on a Windows GUI-less child; only
`kill()` works.
**Why it happens:** `terminate()` posts WM_CLOSE; console/no-window children don't pump it.
**How to avoid:** Send `exitApp` over the WebSocket first (plugins persist + exit themselves), THEN
`terminate()` (grace) → `kill()`. Don't rely on `terminate()` alone for clean shutdown.
`[CITED: doc.qt.io/qt-6/qprocess.html#terminate]`

### Pitfall 6: ASCII-only test names + ASCII-only manifest test fixtures

**What goes wrong:** Manifest fixtures with CJK names (the schema explicitly allows CJK in `Name`)
are fine in JSON, but TEST_CASE *names* with `—`/`→` break ctest's Win32 codepage filter.
**How to avoid:** CJK inside fixture JSON content is OK and good coverage; keep TEST_CASE titles
ASCII (`-`, `->`). CLAUDE.md hard rule.

## Code Examples

### Node detection (injectable resolver, mockable)

```cpp
// Source: doc.qt.io/qt-6/qstandardpaths.html#findExecutable + QProcess --version probe
// Resolve + version-gate. The resolver is a std::function so tests inject a fake path
// and a fake `node --version` output without a live node.
struct NodeProbe {
    std::function<QString()> findNode = [] { return QStandardPaths::findExecutable("node"); };
    std::function<QString(QString const&)> queryVersion;  // runs `<node> --version`, returns "v26.0.0"
};
std::optional<QString> resolveNode20Plus(NodeProbe const& probe) {
    QString const exe = probe.findNode();
    if (exe.isEmpty()) return std::nullopt;                 // not installed → reject plugin
    QString const v = probe.queryVersion(exe);              // e.g. "v26.0.0\n"
    QVersionNumber const n = QVersionNumber::fromString(v.mid(1)); // strip leading 'v'
    if (n.majorVersion() < 20) return std::nullopt;         // too old → reject
    return exe;
}
```

(Verified locally: `node --version` on this machine prints `v26.0.0`; detection must tolerate the
leading `v` and trailing newline.)

### Manifest validation: OS + MinimumVersion gate

```cpp
// Source: akp_plugin_sdk.md §2 (OS array, Software.MinimumVersion) + §3 validation rule.
// Running version: QCoreApplication::applicationVersion() — set in main.cpp:63 ("0.1.0").
bool manifestRunnableHere(PluginManifest const& m, QString const& platform, QString const& appVer) {
    bool osMatch = false;
    for (auto const& os : m.os)            // each {platform, minimumVersion}
        if (os.platform == platform) osMatch = true;
    if (!osMatch) return false;            // -> PluginManifestTest::rejects_macOnly_onLinux
    if (!m.softwareMinimumVersion.isEmpty()) {
        if (QVersionNumber::fromString(appVer) <
            QVersionNumber::fromString(m.softwareMinimumVersion))
            return false;                  // app too old for this plugin
    }
    return true;
}
```

Platform string convention: schema uses `"mac"` / `"windows"`. Map our build to those: `Q_OS_MACOS`
→ `"mac"`, `Q_OS_WIN` → `"windows"`. **Open question:** the schema's `OS` array in shipped
manifests only lists `mac`/`windows` (vendor never shipped Linux). Linux is our primary platform —
the planner must decide whether to treat a manifest with no Linux entry as runnable on Linux
(recommended: accept and best-effort, since the vendor never emitted a `"linux"` platform value, so
a strict match would reject *every* real package on our primary OS). `[ASSUMED]` — see Assumptions
A1.

### Spawn dispatch by CodePath extension

```cpp
// Source: akp_plugin_sdk.md §3 spawn rules. CodePath / CodePathWin / CodePathMac per §2.
QString code = resolveCodePath(m);   // CodePathWin/Mac override CodePath per platform
QString ext = QFileInfo(code).suffix().toLower();
if (ext == "js" || ext == "mjs" || ext == "cjs") {
    auto nodeExe = resolveNode20Plus(probe);
    if (!nodeExe) { disableWithNotice(m.uuid, "Node.js >= 20 not found"); return; }
    proc->start(*nodeExe, buildNodeArgv(code, port, m.uuid, infoJson));
} else if (ext == "html" || ext == "htm") {
    hostHtmlPlugin(m, code);          // QWebEngineView via PropertyInspectorController surface
} else {
    // .exe / .app / no extension -> native
    if (m.runAsAdministrator) startElevatedWindows(code);  // Win only; Linux/mac: log+refuse
    else proc->start(code, {});
}
```

## State of the Art

| Old Approach                               | Current Approach                              | When Changed             | Impact                                                               |
| ------------------------------------------ | --------------------------------------------- | ------------------------ | -------------------------------------------------------------------- |
| QCefView (Chromium 109, Jan 2023) for HTML | QWebEngineView + QWebChannel (Chromium ~118+) | This project (v1.0 arch) | Modern web platform; bidirectional bridge without cef_query polyfill |
| Bundle `node20.exe` (vendor)               | Detect system node ≥20, reject otherwise      | This project (CONTEXT)   | No bundled runtime; smaller install; user-managed node               |
| Bind `QHostAddress::Any` (vendor)          | Loopback-only `127.0.0.1`                     | Phase 17                 | Security: no LAN exposure                                            |

**Deprecated/outdated:**

- `src/host/plugin-host/` module layout from akp_plugin_sdk.md §9 — superseded by CONTEXT's
  reuse-`src/app/src/` decision.
- passHello/salt/challenge auth — still deferred (Phase-17 header marks it NOT YET IMPLEMENTED);
  out of scope for Phase 18.

## Assumptions Log

| #   | Claim                                                                                                                                                                                       | Section                       | Risk if Wrong                                                                                                                                                                          |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | Manifests with no `"linux"` entry in `OS` should be treated as runnable on Linux (vendor only ever emitted `mac`/`windows`, so a strict match rejects every real package on our primary OS) | Code Examples / manifest gate | If wrong, either every package is rejected on Linux (too strict) or genuinely incompatible packages spawn (too loose). Needs user confirmation — this is a policy decision, not a fact |
| A2  | `infoJson` for the `-info` argv is the Elgato application-info envelope (`{application:{...},devicePixelRatio,devices:[...]}`); exact shape for AJAZZ not pinned in §3                      | Spawn dispatch                | A wrong shape may make some plugins mis-detect the host; real-package witness is Phase 25. Keep minimal + documented                                                                   |
| A3  | `QProcess::terminate()` is a no-op for windowless Windows children (so `exitApp`-then-kill is required)                                                                                     | Pitfall 5                     | If terminate works, the belt-and-braces kill is harmless; low risk                                                                                                                     |
| A4  | The Mirabox alias is a pure forwarding wrapper (same signature) — no semantic difference between the two connect functions                                                                  | Pattern 2 / PLUGIN-11         | If Mirabox's signature diverges, the alias breaks; akp_plugin_sdk.md §9 states "same signature", treated as cited not assumed, but unverified against a live package until Phase 25    |

## Open Questions

1. **Linux `OS`-array policy (A1).**

   - What we know: shipped vendor manifests list only `mac`/`windows`; Linux is our primary OS.
   - What's unclear: whether to accept-on-Linux-by-default or require an explicit `"linux"` entry.
   - Recommendation: accept by default on Linux (best-effort), log a note; revisit if real packages
     break. **Needs user confirmation before locking.**

1. **`defaultPlugins/` vs `installedPlugins/` on-disk layout.**

   - What we know: vendor uses `<install>/defaultData/defaultPlugins/` + `%APPDATA%/HotSpot/...`.
     Our catalog uses `<AppDataLocation>/plugins/`.
   - What's unclear: do we ship a bundled `defaultPlugins/` at all in Phase 18, or only discover
     `installedPlugins/`?
   - Recommendation: Phase 18 discovers the user `installedPlugins/` dir
     (`<AppDataLocation>/plugins/`, already populated by the catalog install path); a bundled
     `defaultPlugins/` is optional and can be a fixture-only concern until there are real first-party
     plugins. Planner to confirm scope.

1. **Where the Mirabox `QWebEngineScript` lives.**

   - What we know: the per-plugin profile is owned by `PropertyInspectorController` (PIMPL).
   - What's unclear: whether to add the shim inside the controller or a sibling helper.
   - Recommendation: a small `plugin_mirabox_shim.hpp` exposing a `constexpr` JS source + a
     `QWebEngineScript makeMiraboxShim()` builder, inserted by whoever owns the plugin's profile.
     Keeps the alias source unit-testable as a string.

## Environment Availability

| Dependency                                | Required By                      | Available        | Version | Fallback                                                                      |
| ----------------------------------------- | -------------------------------- | ---------------- | ------- | ----------------------------------------------------------------------------- |
| `node` (≥20)                              | Node.js plugin spawn (PLUGIN-08) | ✓ (this machine) | v26.0.0 | Plugin disabled with notice if absent/old — by design, NOT a build blocker    |
| `Qt6::WebEngineQuick` + `WebChannelQuick` | HTML plugin hosting (PLUGIN-08)  | ✓ (gated)        | 6.7+    | `AJAZZ_HAVE_WEBENGINE` off → HTML plugins unavailable; native+node still work |
| `Qt6::WebSockets`                         | plugin↔host transport            | ✓ (gated)        | 6.7+    | `AJAZZ_HAVE_WEBSOCKETS` off → plugin functionality disabled at runtime        |
| `Qt6::CorePrivate`                        | extractor (reuse)                | ✓                | 6.7+    | — (required, already a dep)                                                   |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** `node` (runtime probe; plugin disabled if absent — this is
the intended behaviour, not a failure). WebEngine/WebSockets degrade gracefully via existing gates.

## Validation Architecture

> `workflow.nyquist_validation` is `true` in `.planning/config.json` — section included.

### Test Framework

| Property           | Value                                                                              |
| ------------------ | ---------------------------------------------------------------------------------- |
| Framework          | Catch2 (v3, `Catch2::Catch2WithMain`)                                              |
| Config file        | `tests/unit/CMakeLists.txt` (`catch_discover_tests(ajazz_unit_tests)` at line 358) |
| Quick run command  | \`ctest --preset linux-release -R "PluginManifest                                  |
| Full suite command | `ctest --preset linux-release`                                                     |

### Phase Requirements → Test Map

| Req ID    | Behavior                                                                                                            | Test Type                          | Automated Command                                      | File Exists?                                                                                    |
| --------- | ------------------------------------------------------------------------------------------------------------------- | ---------------------------------- | ------------------------------------------------------ | ----------------------------------------------------------------------------------------------- |
| PLUGIN-06 | mac-only manifest rejected on Linux                                                                                 | unit                               | `ctest --preset linux-release -R "PluginManifestTest"` | ❌ Wave 0 (`test_plugin_manifest.cpp`)                                                          |
| PLUGIN-06 | accepts Elgato v6 + AJAZZ ext incl. `Controllers:["Knob"]`, `IsK1Pro`, `FSize`/`FFamily`, `PUUID`, `Nodejs.Version` | unit                               | same target                                            | ❌ Wave 0                                                                                       |
| PLUGIN-06 | rejects `Software.MinimumVersion` > running app version                                                             | unit                               | same target                                            | ❌ Wave 0                                                                                       |
| PLUGIN-07 | discovery over a temp dir seeded with fixture `.sdPlugin` packages                                                  | unit                               | `ctest -R "PluginManagerTest"`                         | ❌ Wave 0 (`test_plugin_manager.cpp`)                                                           |
| PLUGIN-07 | crash 3-in-30s → disabled (injected fake clock + fake process events)                                               | unit                               | `ctest -R "PluginCrashTracker"`                        | ❌ Wave 0 (`test_plugin_crash_tracker.cpp`)                                                     |
| PLUGIN-07 | `exitApp` shutdown sends frame then terminates (server `sendEvent` added)                                           | unit                               | `ctest -R "SdPluginServer"` (extend existing)          | ⚠️ extend `test_sd_plugin_server.cpp`                                                           |
| PLUGIN-08 | node argv exact construction, no live node                                                                          | unit                               | `ctest -R "NodeRunnerTest"`                            | ❌ Wave 0 (`test_node_runner.cpp`)                                                              |
| PLUGIN-08 | node-version detection (mocked probe: present/absent/too-old)                                                       | unit                               | same target                                            | ❌ Wave 0                                                                                       |
| PLUGIN-08 | real spawn round-trip (live node + WS register)                                                                     | integration (node-gated)           | `ctest -R "PluginSpawnE2E"`                            | ❌ optional, `tests/integration/`                                                               |
| PLUGIN-11 | Mirabox alias defined before plugin script runs                                                                     | unit/integration (WebEngine-gated) | `ctest -R "CompatTest"`                                | ❌ Wave 0 (`test_plugin_mirabox_shim.cpp` — pure string test; full DOM test gated on WebEngine) |

Target test names (from akp_plugin_sdk.md §9, ASCII-normalized):
`PluginManifestTest::rejects_macOnly_onLinux`, `NodeRunnerTest::buildsCorrectArgv`,
`CompatTest::miraboxSocket_aliasedToElgato`.

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R "<the new target for this task>"`
- **Per wave merge:** `ctest --preset linux-release -R "Plugin|Node|Compat|SdPluginServer"`
- **Phase gate:** Full suite green (`ctest --preset linux-release`) before `/gsd:verify-work`.

### Wave 0 Gaps

- [ ] `tests/unit/test_plugin_manifest.cpp` — covers PLUGIN-06 (fixtures: mac-only, knob, k1pro,
  high-minver, full AJAZZ-ext)
- [ ] `tests/unit/test_node_runner.cpp` — covers PLUGIN-08 argv + mocked detection
- [ ] `tests/unit/test_plugin_crash_tracker.cpp` — covers PLUGIN-07 crash window (inject clock)
- [ ] `tests/unit/test_plugin_manager.cpp` — covers PLUGIN-07 discovery over temp dir
- [ ] `tests/unit/test_plugin_mirabox_shim.cpp` — covers PLUGIN-11 alias source (pure string +
  optional WebEngine-gated DOM test)
- [ ] Extend `tests/unit/test_sd_plugin_server.cpp` — covers new `sendEvent` host→plugin path
- [ ] Optional `tests/integration/test_plugin_spawn_e2e.cpp` — node-gated real spawn (skip if no node)
- [ ] Wire each new TU + its `src/app/src/*.cpp` into `tests/unit/CMakeLists.txt` (mirror the
  `test_sd_plugin_server` gating block at lines 335-342; add `Qt6::CorePrivate`/WebSockets as
  needed); ensure `catch_discover_tests` picks them up
- [ ] No framework install needed — Catch2 already wired

## Security Domain

> `security_enforcement` not set to `false` in config — section included.

### Applicable ASVS Categories

| ASVS Category       | Applies         | Standard Control                                                                                                                                                                                                                   |
| ------------------- | --------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V1 Architecture     | yes             | Loopback-only WS bind (Phase 17, `127.0.0.1`); spawned children are untrusted, host validates manifest before spawn                                                                                                                |
| V5 Input Validation | yes             | `parsePluginManifest` rejects malformed/oversized JSON; reuse `pi_url_policy` allow/deny for HTML plugin sub-resource loads; reuse extractor's zip-slip guard                                                                      |
| V6 Cryptography     | no (this phase) | passHello/salt/challenge auth is deferred (Phase-17 NOT YET IMPLEMENTED)                                                                                                                                                           |
| V10 Malicious Code  | yes             | No signature verification in vendor (akp_plugin_sdk.md §6 flags this as a gap "we must not replicate"); `LoadedPluginsModel` already surfaces trust level (unsigned/self-signed/trusted) — Phase 18 spawn should feed that surface |
| V12 File/Resource   | yes             | Discovery + extraction confined to `<AppDataLocation>/plugins/`; extractor strips wrapper + guards zip-slip (embedded `..` + drive prefixes — see QZipReader sanitization memo)                                                    |

### Known Threat Patterns for Qt plugin host

| Pattern                                                    | STRIDE                 | Standard Mitigation                                                                                                                                                            |
| ---------------------------------------------------------- | ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Zip-slip in `.sdPlugin` (embedded `..`, `C:/`)             | Tampering              | Reuse `extractSdPluginArchive` (guarded; QZipReader passes embedded `..` through, so the guard is in our extractor)                                                            |
| Path traversal via plugin-supplied UUID into settings dir  | Tampering              | Reuse `isSafeUuidComponent` (`pi_bridge.cpp:66`) for any UUID used in a filesystem path                                                                                        |
| HTML plugin loading hostile remote resources               | Info disclosure / SSRF | Reuse `pi_url_policy` (`kPiHttpsCdnAllowlist`, http rejected, file confined to plugin dir) + `PIWebView.qml` WebEngineSettings (no remote-from-local, no clipboard, no popups) |
| Node child writing outside its dir / persisting after exit | Tampering / DoS        | Owned `QProcess` + crash window + `exitApp`-then-kill shutdown; do not `startDetached`                                                                                         |
| Unsigned/malicious plugin spawned silently                 | Spoofing               | Surface trust level via `LoadedPluginsModel`; manifest validated before spawn; no signature enforcement yet (documented gap, not introduced)                                   |
| LAN exposure of plugin transport                           | Info disclosure        | Loopback-only bind already enforced (Phase 17, asserted by tests)                                                                                                              |

## Project Constraints (from CLAUDE.md)

- **No `nlohmann::json` in `ajazz_core` or installed public headers** (COD-031). New code is in
  `src/app/src/` (not core); use `QJsonDocument`/`QJsonObject` for manifest parsing (Qt-native,
  already used by `pi_bridge.cpp`) — no nlohmann needed at all here.
- **Schema doc is the source of truth for JSON wire keys** — read manifest keys exactly as
  akp_plugin_sdk.md §2 spells them (`CodePath`, `Nodejs.Version`, `OS`, `Software.MinimumVersion`,
  `Controllers`, `IsK1Pro`, `RunAsAdministrator`, `FSize`/`FFamily`, `PUUID`). Never align to a C++
  field name.
- **QWebEngineView + `Qt6::WebChannelQuick`** (never QCefView, never bare `WebChannel`). HTML
  plugin hosting reuses the existing PI stack.
- **Do NOT bundle node20** — detect system node (anti-feature explicitly named).
- **Do NOT create `src/host/plugin-host/`** — reuse `src/app/src/`.
- **ASCII-only test names** — `-`/`->` not `—`/`→` (Win32 ctest codepage).
- **`qmlRegisterSingletonInstance` for `QML_SINGLETON`** + `static_assert(!is_default_constructible)`
  — if any new type is a QML singleton (likely the `PluginManager` is NOT QML-exposed; if it is,
  follow the `LoadedPluginsModel`/`PropertyInspectorController` pattern exactly).
- **MSVC `/W4 /WX`, Apple Clang `-Werror`** — version-guard / `_s`-variant discipline; land all
  three platforms.
- **Atomic commits, Conventional Commits, never skip pre-commit, fetch+rebase before push.**

## Sources

### Primary (HIGH confidence)

- Codebase (read directly, 2026-05-23): `plugin_catalog_model.{hpp,cpp}`,
  `sdplugin_extractor.hpp`, `sd_plugin_server.{hpp,cpp}`, `loaded_plugins_model.{hpp,cpp}`,
  `pi_bridge.{hpp,cpp}`, `property_inspector_controller.hpp`, `pi_url_policy.hpp`, `PIWebView.qml`,
  `tests/unit/test_sdplugin_extractor.cpp`, `tests/unit/test_sd_plugin_server.cpp`,
  `tests/unit/CMakeLists.txt`, `src/app/CMakeLists.txt`, `main.cpp:63`.
- `docs/protocols/streamdeck/akp_plugin_sdk.md` §2 (manifest schema), §3 (lifecycle/spawn/argv),
  §7-8 (PI/CEF), §9 (corrections + target test names) — project RE source of truth.
- `.planning/phases/18-.../18-CONTEXT.md` — locked decisions.
- `.planning/REQUIREMENTS.md` lines 61-66, 281-284 — PLUGIN-06/07/08/11.
- `CLAUDE.md` — project constraints.

### Secondary (MEDIUM confidence — official Qt docs, training-knowledge of stable Qt 6 APIs)

- `QStandardPaths::findExecutable`, `QVersionNumber`, `QProcess` (`terminate`/`kill` semantics),
  `QWebEngineScript` (`injectionPoint`/`worldId`/`DocumentCreation`),
  `QWebEngineProfile::scripts()` — Qt 6 official documentation (doc.qt.io).

### Tertiary (LOW confidence)

- `infoJson` exact envelope shape (A2) — inferred from Elgato SDK conventions, unverified against a
  live AJAZZ package until Phase 25.

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — all Qt, all already linked or trivially gated; verified against
  CMakeLists.
- Architecture: HIGH — composition of existing tested parts; the two prerequisite gaps
  (`sendEvent`, no manifest parser) are codebase-verified.
- Pitfalls: HIGH for #1/#2 (grep-verified), MEDIUM for #5 (Qt-doc behaviour).
- Manifest schema: HIGH — directly from akp_plugin_sdk.md §2.
- Linux `OS` policy + `infoJson` shape: LOW — assumptions flagged A1/A2 for user confirmation.

**Research date:** 2026-05-23
**Valid until:** 2026-06-22 (stable Qt 6 APIs + project RE doc; 30 days)
