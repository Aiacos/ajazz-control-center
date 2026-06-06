# Phase 20: Property Inspector + Settings - Research

**Researched:** 2026-05-23
**Domain:** Qt 6 WebEngine + QWebChannel JS↔C++ bridge; Stream Deck SDK-2 Property Inspector protocol; on-disk settings persistence
**Confidence:** HIGH (the stack is in-tree and read line-by-line; gaps confirmed by grep)

## Summary

The Property Inspector (PI) stack is **substantially pre-built and correct**. `pi_bridge.cpp`
(418 LoC) fully implements PLUGIN-13 settings persistence (atomic `QSaveFile`, 1 MiB size cap,
UUID path-traversal guard, JSON-validate-before-write, `didReceiveSettings`/`didReceiveGlobalSettings`
emit on read). `property_inspector_controller.cpp` (261 LoC) owns the per-plugin
`QQuickWebEngineProfile`, re-creates a fresh `QQmlWebChannel` with the `$SD` bridge per load, and
installs a per-profile `PIUrlRequestInterceptor`. `PIWebView.qml` binds the `profile`/`webChannel`/`url`
trio (the no-`page`-property gotcha is already handled). `pi_url_policy.cpp` + the interceptor gate
all sub-resource loads. The whole stack is `AJAZZ_HAVE_WEBENGINE`-gated with a clean native fallback.

Phase 20 is therefore **verification + four concrete gap-closes**, NOT a rebuild. The gaps, in
priority order, are: (1) **no `cefQuery` polyfill exists** — `grep cefQuery` returns zero source hits
(PLUGIN-09); (2) **no `sdpi.css` is bundled or served** — `find sdpi*` returns nothing, the
interceptor has no qrc-serving branch for a canonical CSS URL (PLUGIN-09); (3) **the `registerPropertyInspector`
load handshake is not driven from the UI** — `loadInspector` has zero callers in QML
(`grep loadInspector src/app/qml` finds only doc comments), and `Inspector.qml` does not reference
`PropertyInspector`/`PropertyInspectorController` at all (PLUGIN-09); (4) **no restart round-trip
test exists** — `test_pi_bridge.cpp` covers only the pure URL-policy helpers, not the persistence
round-trip (PLUGIN-13). A fifth surprise: the **Mirabox compat shim from the 18-03 PLAN was never
landed** — `grep kMiraboxShimSource/makeMiraboxShim/connectMiraBoxSDSocket` returns zero hits and no
`QWebEngineScript` is used anywhere in `src/`. That is PLUGIN-11 (Phase 18, "planned not executed"),
and it is the natural sibling of the cefQuery shim because both inject a `QWebEngineScript` at
`DocumentCreation`/`MainWorld` onto the same profile — see Open Questions.

**Primary recommendation:** Add a single `QWebEngineScript` (DocumentCreation/MainWorld,
`runsOnSubFrames`) carrying the `cefQuery` polyfill, insert it into the per-plugin profile's
`scripts()` in `loadInspector` (the same mechanism the unbuilt 18-03 Mirabox shim specified); add a
qrc-served `sdpi.css` branch to the URL interceptor at a canonical URL; wire `Inspector.qml`'s
action selection to `PropertyInspectorController.loadInspector(...)`; and add the
persistence-restart round-trip + cefQuery-shim-source + sdpi.css-served unit tests. Everything is
hardware-free and Qt6-only (zero external packages).

## Architectural Responsibility Map

| Capability                                | Primary Tier                                 | Secondary Tier                                 | Rationale                                                                        |
| ----------------------------------------- | -------------------------------------------- | ---------------------------------------------- | -------------------------------------------------------------------------------- |
| Settings persistence (read/write JSON)    | App / `PIBridge` (C++)                       | Filesystem (`QStandardPaths::AppDataLocation`) | Already wired in `pi_bridge.cpp`; bridge owns the I/O, not the controller        |
| PI page lifecycle (profile/channel/url)   | App / `PropertyInspectorController` (C++)    | Browser/Chromium (WebEngine)                   | Controller creates profile + channel; QML binds; Chromium renders                |
| JS `$SD.*` method surface                 | Browser (PI JS) → bridge slot (C++)          | —                                              | `Q_INVOKABLE` slots on `PIBridge` registered as `"$SD"` on the channel           |
| `cefQuery` polyfill (GAP)                 | Browser (injected JS, MainWorld)             | App / `PIBridge` (C++)                         | `QWebEngineScript` defines `window.cefQuery`, delegates to `channel.objects.$SD` |
| `sdpi.css` serving (GAP)                  | App / `PIUrlRequestInterceptor` + qrc        | Browser (fetch)                                | A canonical URL must resolve to a bundled Qt resource                            |
| Sub-resource URL gating                   | App / `pi_url_policy` + interceptor          | Browser (network layer)                        | Pure-C++ policy; interceptor enforces at the Chromium request layer              |
| `registerPropertyInspector` / `passHello` | App / `SdPluginServer` (WebSocket, Phase 17) | App / editor (trigger)                         | The WS server binds the PI's WS connection; the *load* trigger is the GAP        |
| Action-selection → PI load (GAP)          | UI / `Inspector.qml` (QML)                   | App / `PropertyInspectorController`            | Editor must call `loadInspector(plugin, htmlAbsPath, action, context)`           |

## Phase Requirements

| ID        | Description                                                                                                                                                                                                             | Research Support                                                                                                                                                                                                                                                                   |
| --------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PLUGIN-09 | PI renders in `QWebEngineView`+`QWebChannel` (never QCefView); `cefQuery` polyfill delegates to the bridge; `sendToPlugin`/`sendToPropertyInspector` relayed; Elgato `sdpi.css` served from a built-in URL (sdk §7, §8) | Render path DONE (`PIWebView.qml` + controller). `sendToPlugin` is a logging stub (`pi_bridge.cpp:332`), `sendToPropertyInspector` is a wired-but-unemitted signal (`pi_bridge.hpp:122`) — relay is M5 follow-up, see Open Questions. `cefQuery` shim MISSING. `sdpi.css` MISSING. |
| PLUGIN-13 | Per-context `get/setSettings` + plugin-wide `get/setGlobalSettings` persist and survive app restart (sdk §4.3)                                                                                                          | Persistence DONE in `pi_bridge.cpp` (atomic, capped, validated, on disk at `AppDataLocation/plugins/<uuid>/...`). Restart round-trip is path-deterministic (static `QStandardPaths`), so a fresh `PIBridge` reads what a prior one wrote. Only the **test** is missing.            |

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **Reuse + complete, do NOT rebuild.** Build on the existing `pi_bridge` /
  `property_inspector_controller` / `PIWebView.qml` / `pi_url_policy` / `pi_url_request_interceptor`
  stack. Settings persistence (PLUGIN-13) is already implemented — Phase 20 VERIFIES the round-trip
  survives restart and closes any gap; it does not reimplement it.
- **`QWebEngineView` + `QWebChannel`, never QCefView** (CLAUDE.md). The `Qt6::WebChannelQuick` /
  `WebEngineView`-no-`page`-Q_PROPERTY gotchas apply.
- **cefQuery polyfill (§8):** Inject a JS shim via `QWebEngineScript` at `DocumentCreation`/`MainWorld`
  (same mechanism as the Phase-18 Mirabox shim) mapping
  `cefQuery({request,onSuccess,onFailure})` → `channel.objects.bridge.invoke(json).then(onSuccess).catch(onFailure)`.
- **sdpi.css (gap):** Serve Elgato's standard `static/css/sdpi.css` from a built-in URL prefix via
  the existing interceptor/policy (bundle as a Qt resource).
- **registerPropertyInspector handshake:** When the user selects an action with a
  `PropertyInspectorPath`, load that relative HTML; the PI's
  `connectElgatoStreamDeckSocket(...,"registerPropertyInspector",...)` connects back; the host
  replies `passHello` (Phase-17) + the action's current settings via `didReceiveSettings`
  (already wired in `pi_bridge`).

### Claude's Discretion

- Whether the cefQuery polyfill + Mirabox alias share one injected `QWebEngineScript` or two.
- sdpi.css bundling mechanism (qrc vs install path) consistent with `pi_url_policy`.
- How the action-selection → PI-load is triggered from the key editor (signal from the Phase-16
  editor / device panel).

### Deferred Ideas (OUT OF SCOPE)

- Built-in in-process actions → Phase 21. Plugin store → Phase 22. Auxiliary surfaces → Phase 23.
- Live third-party `.sdPlugin` PI round-trip on hardware → Phase 25.

## Project Constraints (from CLAUDE.md)

- **No `nlohmann::json` in `ajazz_core` or any installed public header (COD-031).** The PI stack is
  in `src/app/` (the application target), where `nlohmann` linkage is not the boundary, but the PI
  bridge already uses Qt JSON (`QJsonDocument`) exclusively — keep it that way; do not introduce
  `nlohmann` into `pi_*` files. Verify: `grep -rn nlohmann src/app/src/pi_*.cpp` must stay 0.
- **Never QCefView.** Verify: `grep -rn QCefView src/app/src/` must stay 0.
- **ASCII-only test names.** ctest filter args go through the Win32 CMD codepage; use `-` and `->`,
  never em-dash or right-arrow. The existing `test_pi_bridge.cpp` titles are ASCII — match them.
- **Never skip pre-commit hooks** (`--no-verify` only for a broken hook, documented in the body).
- **Atomic commits; Conventional Commits** (`feat(plugins): ...` / `test(20): ...`).
- **Cap concurrent execute agents at 2.**
- **`MultiEffect.maskSource` needs a proper Item type** (raw `Rectangle` = silent SIGABRT on real
  GPU) — relevant only if new QML wrapping is added; the offscreen smoke test does NOT catch this.
- **`QML_SINGLETON` requires `qmlRegisterSingletonInstance`** (the `registerInstance`/`create`
  pattern) — `PropertyInspectorController` already follows it (`property_inspector_controller.cpp:58-69`).

## Standard Stack

This phase ships **zero external packages** — everything is Qt 6 in-tree.

### Core

| Library                                      | Version            | Purpose                                                                                  | Why Standard                                                              |
| -------------------------------------------- | ------------------ | ---------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| `Qt6::WebEngineQuick`                        | 6.7+ (project min) | Renders PI HTML in `WebEngineView` (Chromium ~118)                                       | Already the chosen QCefView replacement (CLAUDE.md, sdk §8)               |
| `Qt6::WebChannelQuick`                       | 6.7+               | `QQmlWebChannel` — the only type `WebEngineView.webChannel` accepts; exposes `$SD` to JS | Already wired; the QML-friendly bridge                                    |
| `Qt6::Core` (QJson/QSaveFile/QStandardPaths) | 6.7+               | Atomic JSON settings persistence                                                         | Already used by `pi_bridge.cpp`                                           |
| `QWebEngineScript` (from WebEngineCore)      | 6.7+               | Inject the `cefQuery` polyfill at `DocumentCreation`/`MainWorld`                         | The 18-03 PLAN selected this exact mechanism for the sibling Mirabox shim |

**Installation:** none. `find_package(Qt6 COMPONENTS WebEngineQuick WebChannelQuick)` already gated
in `src/app/CMakeLists.txt:18`. On Fedora the WebEngine packages must be present for
`AJAZZ_HAVE_WEBENGINE` to be defined (the build degrades gracefully to the native inspector when
absent — keep all new code under the same gate).

## Package Legitimacy Audit

Not applicable — this phase installs no external packages. All dependencies are Qt 6 modules already
present in the build. `[VERIFIED: in-tree CMake]`

## Architecture Patterns

### System Architecture Diagram

```
  User selects an action in the key editor (Inspector.qml)
        │  (GAP: this call does not exist yet)
        ▼
  PropertyInspectorController.loadInspector(pluginUuid, htmlAbsPath, actionUuid, contextUuid)
        │  creates/reuses per-plugin QQuickWebEngineProfile
        │  installs PIUrlRequestInterceptor(piDir)  ──────► pi_url_policy: allow/deny each fetch
        │  inserts cefQuery QWebEngineScript into profile->scripts()  (GAP)
        │  builds fresh QQmlWebChannel, registers PIBridge as "$SD"
        │  sets activeProfile/activeChannel/activeUrl  ──► emit activeInspectorChanged()
        ▼
  PIWebView.qml binds {profile, webChannel, url} onto one WebEngineView
        ▼
  Chromium loads PI HTML  ──► fetches qwebchannel.js (qrc, allowed)
        │                  ──► fetches sdpi.css (GAP: must resolve to bundled qrc)
        ▼
  PI JS calls window.cefQuery({request, onSuccess, onFailure})   (polyfill, MainWorld)
        │      OR calls $SD.setSettings(json) directly via the channel
        ▼
  channel.objects.$SD.<method>(...)  ──► PIBridge Q_INVOKABLE slot (C++)
        │
        ├─ setSettings/getSettings  ──► writeJsonAtomic / readJsonOrEmpty
        │      AppDataLocation/plugins/<pluginUuid>/settings/<contextUuid>.json
        │      AppDataLocation/plugins/<pluginUuid>/global.json
        │      getSettings ──► emit didReceiveSettings(json) ──► PI JS handler
        │
        └─ sendToPlugin  ──► (M5 GAP) route to plugin WS via SdPluginServer/PluginHost
                              ◄── sendToPropertyInspector signal back to PI JS

  Separately, the plugin process's WS connection:
  PI HTML calls connectElgatoStreamDeckSocket(port, uuid, "registerPropertyInspector", info, action)
        ▼
  SdPluginServer.dispatchClientMessage: binds the WS to uuid, emits pluginRegistered  (Phase 17 DONE)
        │  host should reply passHello + didReceiveSettings
```

### Recommended Project Structure (existing — do not restructure)

```
src/app/src/
├── pi_bridge.{hpp,cpp}                    # $SD JS API; settings persistence (DONE)
├── property_inspector_controller.{hpp,cpp}# profile/channel/url lifecycle (DONE; add script insert)
├── pi_url_policy.{hpp,cpp}                 # pure allow/deny (DONE; add sdpi.css branch or new helper)
├── pi_url_request_interceptor.{hpp,cpp}    # enforces policy (DONE; add qrc-serve for sdpi.css)
├── pi_cef_shim.{hpp,cpp}                   # NEW: kCefQueryShimSource + makeCefQueryShim() (GAP)
├── plugin_mirabox_shim.{hpp,cpp}           # NEW (18-03 never landed): kMiraboxShimSource (PLUGIN-11)
└── sd_plugin_server.{hpp,cpp}              # WS registerPropertyInspector handler (Phase 17 DONE)
src/app/qml/
├── PIWebView.qml                           # WebEngineView binding (DONE)
├── PropertyInspector.qml                   # HTML/native switcher (DONE)
├── NativePropertyInspector.qml             # schema-driven fallback (DONE)
└── Inspector.qml                           # key editor — must trigger loadInspector (GAP)
tests/unit/
└── test_pi_bridge.cpp                      # URL-policy tests only today; add persistence + shim
```

### Pattern 1: `QWebEngineScript` injection at DocumentCreation/MainWorld (the cefQuery shim)

**What:** Define `window.cefQuery` as a polyfill before the plugin's own JS runs.
**When to use:** Any host-injected global the untrusted page depends on.
**Example (mirrors the unbuilt 18-03 Mirabox shim spec — `[CITED: 18-03-PLAN.md]`):**

```cpp
// Source pattern: 18-03-PLAN.md <interfaces> + sdk §8 mapping table.  [ASSUMED] exact JS body.
// pi_cef_shim.hpp — kCefQueryShimSource is plain JS, unit-testable WITHOUT WebEngine.
inline constexpr std::string_view kCefQueryShimSource = R"JS(
window.cefQuery = function(opts) {
    // sdk §8: cefQuery({request, onSuccess, onFailure}) -> channel bridge.
    // The QWebChannel `$SD` object is reachable as channel.objects.$SD once
    // qwebchannel.js has run; the PI's own connect code already sets that up.
    try {
        new QWebChannel(qt.webChannelTransport, function(channel) {
            var sd = channel.objects["$SD"];
            // delegate: invoke a generic entrypoint, resolve onSuccess/onFailure
            sd.invoke(opts.request).then(opts.onSuccess).catch(opts.onFailure);
        });
    } catch (e) { if (opts.onFailure) opts.onFailure(String(e)); }
};
)JS";

#if defined(AJAZZ_HAVE_WEBENGINE)
QWebEngineScript makeCefQueryShim();  // DocumentCreation / MainWorld / runsOnSubFrames
#endif
```

```cpp
// pi_cef_shim.cpp
QWebEngineScript makeCefQueryShim() {
    QWebEngineScript s;
    s.setName(QStringLiteral("ajazz-cefquery-shim"));
    s.setInjectionPoint(QWebEngineScript::DocumentCreation);  // BEFORE plugin JS (Pitfall below)
    s.setWorldId(QWebEngineScript::MainWorld);                // plugin's connect call is in MainWorld
    s.setRunsOnSubFrames(true);
    s.setSourceCode(QString::fromUtf8(kCefQueryShimSource));
    return s;
}
```

```cpp
// Insertion site in property_inspector_controller.cpp loadInspector(), after the profile is created:
//   profile->scripts()->insert(makeCefQueryShim());     // (and makeMiraboxShim() if landed here)
// Insert once per fresh profile (guard on freshProfile, mirroring the interceptor wiring at :198).
```

> **Note on the exact `cefQuery` → bridge JS body:** the sdk §8 table maps `cefQuery(...)` to
> `channel.objects.bridge.invoke(json).then(...)`. The current `PIBridge` does **not** expose a
> generic `invoke(QString)` slot — it exposes the typed `$SD.*` surface. The planner must decide
> between (a) adding a thin `Q_INVOKABLE QString invoke(QString json)` dispatcher to `PIBridge`
> that parses `{event,...}` and fans out to the existing slots, or (b) writing the polyfill to map
> known `request` shapes onto the existing typed slots. Option (a) matches §8 verbatim and is the
> recommended path. This is tagged `[ASSUMED]` (A1) — confirm with discuss-phase. The shim **source
> string** is unit-testable regardless (assert it defines `window.cefQuery` and references the
> channel), per the 18-03 string-test discipline.

### Pattern 2: Serving a bundled `sdpi.css` from a canonical URL

**What:** PI HTML references `static/css/sdpi.css` (Elgato standard). Instead of every plugin
shipping a copy, the host serves one bundled file.
**When to use:** Shared static assets the host owns.
**Two viable mechanisms (Claude's Discretion — pick consistent with `pi_url_policy`):**

1. **qrc + interceptor redirect (recommended):** bundle `resources/streamdeck/sdpi.css` as a Qt
   resource (alias e.g. `qrc:/qt/qml/AjazzControlCenter/streamdeck/sdpi.css`, following the
   `set_source_files_properties(... QT_RESOURCE_ALIAS ...)` convention at
   `src/app/CMakeLists.txt:144`). In `PIUrlRequestInterceptor::interceptRequest`, detect a request
   whose path ends in `sdpi.css` (or a canonical `file://<piDir>/static/css/sdpi.css` /
   custom-scheme URL) and `info.redirect(QUrl("qrc:/.../sdpi.css"))`. `qrc:` is already in the
   allowlist (`pi_url_policy.cpp:81`), so the redirected fetch passes.
   `[CITED: pi_url_policy.cpp:81; pi_url_request_interceptor.cpp:61]`
1. **Custom URL scheme handler** (`QWebEngineUrlSchemeHandler` registered on the profile, e.g.
   `sdpi://css`). More moving parts; only choose if redirect proves awkward.

The `RESOURCES ${ACC_QML_MODULE_RESOURCES}` list in `src/app/CMakeLists.txt:334-347,369` is where a
new `ACC_SDPI_CSS` entry plugs in. `[VERIFIED: src/app/CMakeLists.txt]`

> **Where to obtain `sdpi.css`:** Elgato's `sdpi-css` is the standard PI stylesheet (the
> `sdpi-wrapper` / `.sdpi-item` classes). Bundle a vendored copy under `resources/streamdeck/`.
> The exact file contents/version are `[ASSUMED]` (A2) — verify the vendored copy against the
> upstream `elgatosf/sdpi-css` or the AJAZZ defaultPlugins' bundled copy before locking. This is a
> static asset, not a package install, so no slopcheck applies.

### Pattern 3: Action-selection → `loadInspector` trigger (the missing UI wire)

**What:** When the editor selects an action that has a `PropertyInspectorPath`, resolve it to an
absolute HTML path and call `PropertyInspectorController.loadInspector(...)`; when selecting a
built-in/native action, call `closeInspector()` so the native renderer takes over.
**Current state:** `Inspector.qml` does **not** reference `PropertyInspector` or
`PropertyInspectorController` — `grep` confirms zero hits. `loadInspector` has no QML caller
(`grep loadInspector src/app/qml` → only doc comments). `[VERIFIED: grep src/app/qml]`
**Example (sketch):**

```qml
// Inspector.qml — on action selection:
function onActionSelected(action) {
    if (PropertyInspectorController.webEngineAvailable && action.propertyInspectorPath !== "") {
        PropertyInspectorController.loadInspector(action.pluginUuid,
                                                  action.piHtmlAbsPath,   // resolved by manifest reader
                                                  action.uuid,
                                                  action.contextUuid)
    } else {
        PropertyInspectorController.closeInspector()  // native fallback renders schema form
    }
}
```

> The resolution of `PropertyInspectorPath` (relative) → absolute HTML path is the manifest reader's
> job (Phase 18 territory). Whether the action model already carries `piHtmlAbsPath` /
> `pluginUuid` / `contextUuid` is `[ASSUMED]` (A3) — the planner must confirm the editor's action
> model exposes these, or add them. This is the highest-uncertainty gap.

### Anti-Patterns to Avoid

- **Injecting the cefQuery shim via `runJavaScript` after load.** Races the plugin's own script →
  `cefQuery is not defined` ReferenceError. Use `DocumentCreation` (the 18-03 Pitfall 4 lesson).
- **Reimplementing settings persistence.** It is done and tested-by-construction. Adding a parallel
  store is a CONTEXT.md violation.
- **Serving `sdpi.css` over `http://` or an arbitrary `https://`.** The policy blocks both; serve
  from `qrc:` (already allowed) or a custom scheme.
- **Putting the cefQuery JS body in a way that bypasses the URL interceptor.** The shim runs in
  MainWorld with the page; it must route through the channel, not open its own transport.

## Don't Hand-Roll

| Problem                   | Don't Build                            | Use Instead                                                 | Why                                                                         |
| ------------------------- | -------------------------------------- | ----------------------------------------------------------- | --------------------------------------------------------------------------- |
| Atomic JSON write         | Custom temp-file + rename              | `QSaveFile` (already in `writeJsonAtomic`)                  | Cross-platform atomic-rename + fsync handled                                |
| JS↔C++ bridge             | Custom message bus / WebSocket to self | `QQmlWebChannel` + `Q_INVOKABLE` (already wired)            | Bidirectional, type-marshalled, the §8-blessed QCefView replacement         |
| Pre-page JS injection     | `runJavaScript` polling                | `QWebEngineScript` at `DocumentCreation`                    | Guaranteed-before-page-script ordering                                      |
| Path-traversal defence    | ad-hoc string checks                   | `isSafeUuidComponent` + `pathIsInsideDir` (already in tree) | Already covers `..`, separators, control bytes, suffix look-alike sequences |
| Settings storage location | Hardcoded paths                        | `QStandardPaths::AppDataLocation` (already used)            | Per-OS correct, restart-stable                                              |

**Key insight:** The hard parts (atomic I/O, sandboxing, the bridge, the no-`page`-property QML
binding) are already solved correctly. The remaining work is *gluing* (a shim, a redirect, a UI
call, four tests), not building.

## Runtime State Inventory

This is an additive feature phase, not a rename/refactor, but settings persistence touches on-disk
state — documented here for completeness.

| Category            | Items Found                                                                                                                                                                                        | Action Required                                                     |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| Stored data         | PI settings at `QStandardPaths::AppDataLocation/plugins/<pluginUuid>/settings/<contextUuid>.json` and `/global.json` (`pi_bridge.cpp:96-126`). On Linux: `~/.local/share/<org>/<app>/plugins/...`. | None — this is the format under test; do not migrate                |
| Live service config | None — PI settings are local files only                                                                                                                                                            | None — verified by reading `pi_bridge.cpp`                          |
| OS-registered state | None                                                                                                                                                                                               | None                                                                |
| Secrets/env vars    | None — settings are plugin-authored JSON, not secrets                                                                                                                                              | None                                                                |
| Build artifacts     | New qrc resource for `sdpi.css` will appear in the QML module's generated `.qrc`                                                                                                                   | Rebuild picks it up automatically via `qt_add_qml_module RESOURCES` |

**Restart round-trip proof (PLUGIN-13):** the settings path is a pure function of
`(AppDataLocation, pluginUuid, contextUuid)` — all static at runtime. A `PIBridge` constructed in a
new process with the same UUIDs computes the same path and `readJsonOrEmpty` returns the
previously-written JSON. The round-trip is therefore correct *by construction*; the phase adds a
test that exercises it (write via one `PIBridge`, destroy, construct a second `PIBridge` with the
same UUIDs pointed at a temp `AppDataLocation`, assert `didReceiveSettings` carries the same JSON).
`[VERIFIED: pi_bridge.cpp:96-126,234-330]`

## Common Pitfalls

### Pitfall 1: cefQuery shim injected too late

**What goes wrong:** PI JS calls `cefQuery(...)` and gets `ReferenceError: cefQuery is not defined`.
**Why it happens:** `runJavaScript`-after-load (or `DocumentReady`) races the plugin's own
`<script>`.
**How to avoid:** `setInjectionPoint(QWebEngineScript::DocumentCreation)` + `MainWorld`.
**Warning signs:** intermittent "is not defined" in the WebEngine JS console; works on slow loads,
fails on fast ones.

### Pitfall 2: `sdpi.css` request blocked by the interceptor

**What goes wrong:** PI renders unstyled; the interceptor logged `file-outside-pi-dir` or
`https-not-in-allowlist` for the CSS URL.
**Why it happens:** The PI references `static/css/sdpi.css` which resolves outside the per-plugin PI
dir, or to a CDN not in the 2-host allowlist.
**How to avoid:** Redirect the canonical CSS request to the bundled `qrc:` resource *inside* the
interceptor before the policy check, or whitelist the specific qrc path.
**Warning signs:** `plugin-pi: blocked request: ... url='.../sdpi.css' reason=...` in logs.

### Pitfall 3: `QML_SINGLETON` dual-instance

**What goes wrong:** Two `PropertyInspectorController` instances; QML binds to a different one than
C++ wired.
**Why it happens:** A default-constructible `QML_SINGLETON` makes Qt pick Constructor mode and
bypass `create()`.
**How to avoid:** Already handled — `static_assert(!is_default_constructible)` +
`registerInstance`/`create` pattern (`property_inspector_controller.hpp:183`). Don't add a default
ctor.
**Warning signs:** settings load but the UI never updates (or vice versa).

### Pitfall 4: WebEngine-absent CI breaks the build

**What goes wrong:** New `pi_cef_shim.cpp` includes `QWebEngineScript` unconditionally → fails on
minimal Qt / lint runs.
**Why it happens:** The clang-tidy lint job compiles every `.cpp` under `src/`.
**How to avoid:** Mirror the existing gating: keep the JS source string reachable always; guard the
`QWebEngineScript` builder behind `#if defined(AJAZZ_HAVE_WEBENGINE)`; register the `.cpp` in the
WebEngine-gated `target_sources` block (`src/app/CMakeLists.txt:409`).
**Warning signs:** `Qt6::WebEngineCore` link error or `QWebEngineScript: no such file` on a minimal
build.

### Pitfall 5: `sendToPlugin`/`sendToPropertyInspector` relay is still a stub

**What goes wrong:** PLUGIN-09 says "relayed" but `sendToPlugin` only logs (`pi_bridge.cpp:332`) and
`sendToPropertyInspector` is an unemitted signal.
**Why it happens:** The relay was scoped to "M5" (the plugin-host WS router) which is the Phase-19
convergence point, not Phase 20.
**How to avoid:** The planner must decide whether PLUGIN-09's "relay" clause is satisfied by the
*existing wired signal/slot surface* (the bridge has the methods; routing to a live plugin process
is Phase 19/25) or requires connecting `PIBridge::sendToPlugin` → `SdPluginServer` in this phase.
Recommend: scope the relay *wiring* (bridge ↔ server signal connection) into Phase 20 but the live
plugin-process round-trip witness to Phase 25 (hardware/integration). `[ASSUMED]` (A4) — confirm.

## Code Examples

### Persistence restart round-trip test (PLUGIN-13) — sketch

```cpp
// tests/unit/test_pi_bridge.cpp — NEW case, ASCII title, hardware-free.
// Point AppDataLocation at a temp dir so the test is hermetic.
TEST_CASE("PI settings round-trip survives a fresh PIBridge", "[pi-bridge][persistence]") {
    QTemporaryDir tmp;
    qputenv("XDG_DATA_HOME", tmp.path().toUtf8());  // drives QStandardPaths::AppDataLocation on Linux
    // (cross-platform: prefer QStandardPaths::setTestModeEnabled(true) + a known AppDataLocation)
    {
        ajazz::app::PIBridge w(nullptr, "com.example.foo", "act1", "ctx1");
        w.setSettings(R"({"hello":"world"})");
    } // first bridge destroyed — simulates app exit
    ajazz::app::PIBridge r(nullptr, "com.example.foo", "act1", "ctx1");
    QString seen;
    QObject::connect(&r, &ajazz::app::PIBridge::didReceiveSettings,
                     [&](QString j){ seen = j; });
    r.getSettings();
    REQUIRE(QJsonDocument::fromJson(seen.toUtf8()).object().value("hello").toString()
            == QStringLiteral("world"));
}
```

> `[ASSUMED]` (A5): the exact `QStandardPaths` test-isolation idiom — prefer
> `QStandardPaths::setTestModeEnabled(true)` (Qt-blessed) over `XDG_DATA_HOME` for cross-platform
> hermeticity. `PIBridge`'s constructor takes a `controller*` which can be `nullptr` for the
> persistence path (`setSettings`/`getSettings` only `(void)controller_`, see `pi_bridge.cpp:241`).
> `[VERIFIED: pi_bridge.cpp:241]`

### cefQuery shim source test (PLUGIN-09) — sketch

```cpp
TEST_CASE("cefQuery shim defines window.cefQuery and routes to the bridge",
          "[pi-bridge][cefquery]") {
    QString src = QString::fromUtf8(ajazz::app::kCefQueryShimSource.data(),
                                    int(ajazz::app::kCefQueryShimSource.size()));
    REQUIRE(src.contains("window.cefQuery"));
    REQUIRE(src.contains("onSuccess"));
    REQUIRE(src.contains("onFailure"));
    REQUIRE(src.contains("$SD"));  // delegates to the channel-registered bridge
}
#if defined(AJAZZ_HAVE_WEBENGINE)
TEST_CASE("cefQuery shim injects at document creation", "[pi-bridge][cefquery]") {
    auto s = ajazz::app::makeCefQueryShim();
    REQUIRE(s.injectionPoint() == QWebEngineScript::DocumentCreation);
    REQUIRE(s.worldId() == QWebEngineScript::MainWorld);
}
#endif
```

## State of the Art

| Old Approach                           | Current Approach                                          | When Changed               | Impact                                                           |
| -------------------------------------- | --------------------------------------------------------- | -------------------------- | ---------------------------------------------------------------- |
| QCefView + `cefQuery` native binding   | `QWebEngineView` + `QWebChannel` + `cefQuery` JS polyfill | This project (sdk §8)      | No native CEF dep; modern Chromium; polyfill bridges the API gap |
| `WebEngineView.page = QWebEnginePage*` | bind `profile`/`webChannel`/`url` trio                    | already in `PIWebView.qml` | `page` is not a QML property; the trio is the supported path     |
| Per-plugin bundled `sdpi.css`          | host-served single canonical `sdpi.css`                   | this phase                 | plugins render styled without shipping the CSS                   |

**Deprecated/outdated:**

- The 18-03 PLAN's Mirabox shim (`plugin_mirabox_shim.{hpp,cpp}`) was **planned but never landed**
  (no files, no symbols). PLUGIN-11 is still open. If the planner co-locates the cefQuery shim and
  the Mirabox shim in one injected `QWebEngineScript` (Claude's Discretion), this phase could close
  PLUGIN-11 as a bonus — but PLUGIN-11 is formally Phase 18's requirement; coordinate scope.

## Assumptions Log

| #   | Claim                                                                                                                                                                       | Section       | Risk if Wrong                                                                                             |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------- | --------------------------------------------------------------------------------------------------------- |
| A1  | `PIBridge` should gain a generic `Q_INVOKABLE QString invoke(QString json)` dispatcher so the cefQuery polyfill maps 1:1 to §8 (`channel.objects.$SD.invoke(...)`)          | Pattern 1     | Polyfill must instead map known request shapes onto typed slots — more JS, slightly off-spec              |
| A2  | A vendored `sdpi.css` (Elgato `sdpi-css`) is the correct stylesheet to bundle; exact contents/version TBD                                                                   | Pattern 2     | Wrong/stale CSS → PI renders with broken styling; verify against upstream or AJAZZ defaultPlugins copy    |
| A3  | The editor's action model exposes (or can expose) `propertyInspectorPath` resolved to an absolute HTML path, plus `pluginUuid`/`contextUuid`                                | Pattern 3     | If absent, the phase must add manifest-path resolution + context minting (larger scope, Phase-18 overlap) |
| A4  | PLUGIN-09's "`sendToPlugin`/`sendToPropertyInspector` relayed" is satisfied by wiring `PIBridge` ↔ `SdPluginServer` signals; the live plugin-process round-trip is Phase 25 | Pitfall 5     | If a live round-trip is required in Phase 20, scope grows and it stops being hardware-free                |
| A5  | `QStandardPaths::setTestModeEnabled(true)` (not `XDG_DATA_HOME`) is the cross-platform-correct test isolation for the persistence round-trip                                | Code Examples | Test may not be hermetic on Windows/macOS if the wrong idiom is used                                      |
| A6  | Exact `cefQuery` polyfill JS body                                                                                                                                           | Pattern 1     | The shim *source string* test still holds; only the runtime delegation detail is unverified offscreen     |

**These assumptions need confirmation in discuss-phase before becoming locked plan decisions.**

## Open Questions

1. **One injected script or two (cefQuery vs Mirabox)?**

   - What we know: both want `DocumentCreation`/`MainWorld`/`runsOnSubFrames` on the same profile;
     CONTEXT.md leaves this to Claude's Discretion.
   - What's unclear: whether PLUGIN-11 (Phase 18's req) should be closed here opportunistically.
   - Recommendation: ship `pi_cef_shim.{hpp,cpp}` for PLUGIN-09 now; if the Mirabox shim is also
     landed, insert both scripts (or concatenate into one) in `loadInspector`. Keep the Phase-11
     accounting explicit so the requirement isn't double-counted.

1. **Does `PIBridge` need a generic `invoke()` slot?** (A1) — §8 maps `cefQuery` to
   `bridge.invoke(json)`, which doesn't exist yet. Decide dispatcher-vs-typed-mapping in
   discuss-phase.

1. **Is the relay (`sendToPlugin`/`sendToPropertyInspector`) in scope?** (A4) — the methods exist
   but are stubs/unemitted. Confirm whether Phase 20 wires the bridge↔server connection or defers
   the entire relay to Phase 19/25.

1. **Where does action-selection happen, and does the action model carry the PI path + context?**
   (A3) — `Inspector.qml` is the editor surface but doesn't touch the controller today. The trigger
   wiring is the largest-uncertainty deliverable.

## Environment Availability

| Dependency             | Required By                              | Available                                            | Version | Fallback                                                 |
| ---------------------- | ---------------------------------------- | ---------------------------------------------------- | ------- | -------------------------------------------------------- |
| `Qt6::WebEngineQuick`  | PI HTML render + cefQuery shim build     | gated at configure (`AJAZZ_HAVE_WEBENGINE`)          | 6.7+    | Native inspector path; WebEngine-gated tests compile-out |
| `Qt6::WebChannelQuick` | `$SD` bridge                             | gated together with WebEngineQuick                   | 6.7+    | same                                                     |
| Qt offscreen QPA       | QML smoke + any offscreen WebEngine test | ✓ (used by `tests/qml`, `QT_QPA_PLATFORM=offscreen`) | —       | string-only unit tests                                   |

**Missing dependencies with no fallback:** none — the phase degrades to native inspector + pure
string/unit tests on minimal Qt.

**Missing dependencies with fallback:** WebEngine-gated assertions (injection-point checks) compile
out when WebEngine is absent; the pure source-string and persistence tests still run, preserving
PLUGIN-09/13 coverage on minimal builds.

## Validation Architecture

> `workflow.nyquist_validation` is not disabled in config → section included.

### Test Framework

| Property           | Value                                                                        |
| ------------------ | ---------------------------------------------------------------------------- |
| Framework          | Catch2 (unit) + offscreen QML smoke harness (`tests/qml/test_qml_smoke.cpp`) |
| Config file        | CMake presets; `ctest --preset linux-release`                                |
| Quick run command  | `ctest --preset linux-release -R "pi-bridge"`                                |
| Full suite command | `ctest --preset linux-release`                                               |

### Phase Requirements → Test Map

| Req ID    | Behavior                                                                                 | Test Type                         | Automated Command                               | File Exists?                            |
| --------- | ---------------------------------------------------------------------------------------- | --------------------------------- | ----------------------------------------------- | --------------------------------------- |
| PLUGIN-13 | settings round-trip survives a fresh `PIBridge`                                          | unit                              | `ctest --preset linux-release -R "persistence"` | ❌ Wave 0 (extend `test_pi_bridge.cpp`) |
| PLUGIN-13 | global settings round-trip                                                               | unit                              | `ctest --preset linux-release -R "persistence"` | ❌ Wave 0                               |
| PLUGIN-09 | cefQuery shim source defines `window.cefQuery` + routes to `$SD`                         | unit (pure string)                | `ctest --preset linux-release -R "cefquery"`    | ❌ Wave 0 (new `pi_cef_shim`)           |
| PLUGIN-09 | cefQuery shim injects at DocumentCreation/MainWorld                                      | unit (WebEngine-gated)            | `ctest --preset linux-release -R "cefquery"`    | ❌ Wave 0                               |
| PLUGIN-09 | `sdpi.css` is served from the built-in qrc URL with `text/css`                           | unit (interceptor / qrc presence) | `ctest --preset linux-release -R "sdpi"`        | ❌ Wave 0                               |
| PLUGIN-09 | selecting an action with a `PropertyInspectorPath` loads PI + emits `didReceiveSettings` | QML smoke / controller unit       | `ctest --preset linux-release -R "inspector"`   | ❌ Wave 0                               |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R "pi-bridge|cefquery|sdpi|persistence"`
- **Per wave merge:** `ctest --preset linux-release` (full ~408-case suite)
- **Phase gate:** full suite green before `/gsd:verify-work`

### Wave 0 Gaps

- [ ] `tests/unit/test_pi_bridge.cpp` — add persistence round-trip cases (per-context + global) for
  PLUGIN-13. Use `QStandardPaths::setTestModeEnabled(true)` for hermeticity.
- [ ] `src/app/src/pi_cef_shim.{hpp,cpp}` + cases — `kCefQueryShimSource` string test (always-on) +
  `makeCefQueryShim()` injection-point test (WebEngine-gated). Mirror the 18-03 string-test
  discipline.
- [ ] `sdpi.css` served test — assert the bundled qrc resource exists and (if testable) the
  interceptor redirects the canonical URL to it.
- [ ] Action-selection → `loadInspector` wiring test — controller-level (`loadInspector` sets
  `hasHtmlInspector`/`activeUrl` and a `getSettings` emits `didReceiveSettings`) and/or QML
  smoke that `PropertyInspector.qml` switches to the HTML component when `hasHtmlInspector`.
- [ ] No framework install needed — Catch2 + offscreen QML harness already present.

## Security Domain

> `security_enforcement` not disabled → included. The PI hosts **untrusted plugin-authored HTML/JS**,
> so this is a genuine trust boundary.

### Applicable ASVS Categories

| ASVS Category                      | Applies | Standard Control                                                                                                                            |
| ---------------------------------- | ------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| V1 Architecture / Trust Boundaries | yes     | Per-plugin isolated `QQuickWebEngineProfile`; per-profile URL interceptor; fresh channel per load (all DONE)                                |
| V5 Input Validation / Sanitization | yes     | `isSafeUuidComponent` (path-traversal guard) + JSON-validate-before-write + 1 MiB cap (DONE, `pi_bridge.cpp:66-86,244-258`)                 |
| V12 Files & Resources              | yes     | `pathIsInsideDir` scoping of `file://`; per-plugin `AppDataLocation` dir (DONE)                                                             |
| V13 API / Web Service (SSRF)       | yes     | `https://` restricted to a 2-host allowlist; `http://` always blocked; exotic schemes blocked (DONE, `pi_url_policy.cpp`)                   |
| V14 Configuration                  | yes     | WebEngineSettings hardened in `PIWebView.qml` (no remote-from-local, no clipboard, no popups, no plugins, no fullscreen, no screen capture) |
| V6 Cryptography                    | no      | No crypto in this phase (auth challenge/salt is the WS layer, Phase 17)                                                                     |

### Known Threat Patterns for {untrusted PI HTML in QtWebEngine}

| Pattern                                                          | STRIDE                 | Standard Mitigation                                                                                                                  |
| ---------------------------------------------------------------- | ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| Path traversal via `pluginUuid`/`contextUuid` into settings path | Tampering / EoP        | `isSafeUuidComponent` rejects `..`, separators, control bytes (DONE)                                                                 |
| `file://` read of other plugins' dirs / `/etc/passwd`            | Information Disclosure | `pathIsInsideDir(piDir)` scoping; suffix-look-alike guarded (DONE + tested)                                                          |
| SSRF / data exfil via arbitrary `https://`/`http://` fetch       | Information Disclosure | host-equality allowlist (jsdelivr, unpkg only); http blocked (DONE)                                                                  |
| `javascript:`/`file:` in `$SD.openUrl` (XSS / local exec)        | EoP                    | `isOpenUrlAllowed` accepts https-only (DONE + tested); first-call prompt is a tracked TODO                                           |
| Disk-fill via `setSettings` in a loop                            | DoS                    | 1 MiB per-write cap (DONE)                                                                                                           |
| cefQuery shim leaking host capability into MainWorld             | Tampering / EoP        | shim is a pure forwarder to the already-sandboxed `$SD` slots; no new capability (NEW — verify the polyfill adds no privileged path) |
| `sdpi.css` redirect opening a new fetch surface                  | Tampering              | redirect target is a fixed `qrc:` resource, not attacker-controlled (NEW — pin the target)                                           |

**Net security posture:** the existing stack is already hardened for the untrusted-HTML boundary
and unit-tested for it. The two NEW surfaces (cefQuery shim, sdpi.css redirect) must each be
reviewed to confirm they add no privileged path and no attacker-controlled redirect target.

## Sources

### Primary (HIGH confidence)

- `src/app/src/pi_bridge.{hpp,cpp}` — settings persistence, `$SD` surface, `openUrl` policy (read in full)
- `src/app/src/property_inspector_controller.{hpp,cpp}` — profile/channel/url lifecycle (read in full)
- `src/app/src/pi_url_policy.{hpp,cpp}` + `pi_url_request_interceptor.{hpp,cpp}` — URL gating (read in full)
- `src/app/qml/{PIWebView,PropertyInspector}.qml` — render path + HTML/native switch (read in full)
- `src/app/src/sd_plugin_server.cpp` — `registerPropertyInspector`/`registerPlugin` WS handler (Phase 17)
- `src/app/CMakeLists.txt` — WebEngine gating, RESOURCES list, qrc alias convention
- `tests/unit/test_pi_bridge.cpp` — current coverage (URL policy only)
- `tests/qml/test_qml_smoke.cpp` — offscreen QML harness capabilities
- `docs/protocols/streamdeck/akp_plugin_sdk.md` §7/§8/§4.3 — PI handshake, QCefView→QWebChannel map, settings actions
- `.planning/phases/18-plugin-manifest-discovery-lifecycle-spawn/18-03-PLAN.md` — `QWebEngineScript` injection pattern (Mirabox shim, never landed)
- `.planning/REQUIREMENTS.md` — PLUGIN-09/11/13 wording + status
- grep verifications: `cefQuery` (0 hits), `sdpi` (0 file hits), `kMiraboxShimSource`/`QWebEngineScript`/`scripts()` (0 hits), `loadInspector` in QML (0 callers), `PropertyInspector` in `Inspector.qml` (0 hits)

### Secondary (MEDIUM confidence)

- `.planning/STATE.md` — Phases 17-18 "planned + verified, NOT executed" status

### Tertiary (LOW confidence)

- Exact `sdpi.css` upstream contents (Elgato `sdpi-css`) — to be vendored and verified before lock (A2)

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — entirely Qt6 in-tree, read line-by-line, zero external packages
- Architecture / what-is-done-vs-missing: HIGH — every PI file read; gaps confirmed by grep
- Pitfalls: HIGH — derived from the read code + the 18-03 PLAN's documented Pitfall 4
- The cefQuery polyfill JS body + `invoke()` dispatcher decision: MEDIUM — §8 gives the shape, the
  exact bridge slot is a design choice (A1/A6)
- Action-selection trigger model fields: MEDIUM — confirmed the wire is missing; uncertain whether
  the action model already carries the needed fields (A3)

**Research date:** 2026-05-23
**Valid until:** 2026-06-22 (stable in-tree code; re-verify if the PI files or REQUIREMENTS change)
