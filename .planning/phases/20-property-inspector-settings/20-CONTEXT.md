# Phase 20: Property Inspector + Settings - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + akp_plugin_sdk.md §7/§8 + existing-code survey

<domain>
## Phase Boundary

Phase 20 delivers each action's **Property Inspector** (the per-action settings UI) and persists
its settings. **Much is already built** — `pi_bridge.cpp` (418 LoC) already persists
`get/set/getGlobal/setGlobalSettings` to atomic on-disk JSON (size-capped, uuid-validated) and
relays `sendToPlugin`/`sendToPropertyInspector`; `property_inspector_controller.cpp` (261 LoC),
`PIWebView.qml`, `PropertyInspector.qml`, `pi_url_policy.hpp`, `pi_url_request_interceptor` all
exist. This phase **completes + verifies** that stack and closes the remaining gaps.

**Delivers (PLUGIN-09, PLUGIN-13):**

- The per-action Property Inspector renders in **`QWebEngineView` + `QWebChannel`** (the existing
  stack); a **`cefQuery` polyfill** maps `cefQuery({request,onSuccess,onFailure})` to the
  QWebChannel bridge (§8); `sendToPlugin`/`sendToPropertyInspector` relay (already wired);
  **Elgato `sdpi.css` is served from a built-in URL** (gap — not present today); the
  `registerPropertyInspector` load handshake (click action with `PropertyInspectorPath` → load
  the relative HTML → `connectElgatoStreamDeckSocket(...,"registerPropertyInspector",...)` →
  host `passHello` + `didReceiveSettings`) (PLUGIN-09).
- Per-context `get/setSettings` + plugin-wide `get/setGlobalSettings` persist and **survive an app
  restart** (largely done in `pi_bridge` — verify the round-trip + close any gap) (PLUGIN-13).

**Out of scope:** built-in in-process actions (Phase 21); plugin store (Phase 22); auxiliary
surfaces (Phase 23). The plugin *main view* HTML hosting was set up in Phase 18; Phase 20 is the
*Property Inspector* pane specifically.
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Reuse + complete (do NOT rebuild)

- Build on the existing `pi_bridge` / `property_inspector_controller` / `PIWebView.qml` /
  `pi_url_policy` / `pi_url_request_interceptor` stack. Settings persistence (PLUGIN-13) is
  already implemented in `pi_bridge` — Phase 20 VERIFIES the round-trip survives restart and
  closes any gap, it does not reimplement it.
- **`QWebEngineView` + `QWebChannel`, never QCefView** (CLAUDE.md). The `Qt6::WebChannelQuick` /
  `WebEngineView`-no-`page`-Q_PROPERTY gotchas apply.

### cefQuery polyfill (§8)

- Inject a JS shim (via `QWebEngineScript` at `DocumentCreation`/`MainWorld`, the same mechanism
  as the Phase-18 Mirabox shim) mapping `cefQuery({request,onSuccess,onFailure})` →
  `channel.objects.bridge.invoke(json).then(onSuccess).catch(onFailure)`. Per the §8 mapping
  table (`invokeMethod`→`bridge.signal_<m>`, `broadcastEvent`→`bridge.signal_<name>`).

### sdpi.css (gap)

- Serve Elgato's standard `static/css/sdpi.css` from a built-in URL prefix via the existing
  `pi_url_request_interceptor`/`pi_url_policy` (bundle as a Qt resource), so PI HTML that
  references it renders without each plugin shipping its own copy.

### registerPropertyInspector handshake

- When the user selects an action with a `PropertyInspectorPath`, load that relative HTML in the
  PI WebView; the PI's `connectElgatoStreamDeckSocket(...,"registerPropertyInspector",...)`
  connects back; the host replies `passHello` (Phase-17) + the action's current settings via
  `didReceiveSettings` (already wired in `pi_bridge`).

### Claude's Discretion (planner/executor)

- Whether the cefQuery polyfill + Mirabox alias share one injected `QWebEngineScript` or two.
- sdpi.css bundling mechanism (qrc vs install path) consistent with `pi_url_policy`.
- How the action-selection → PI-load is triggered from the key editor (signal from the Phase-16 editor / device panel).
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `docs/protocols/streamdeck/akp_plugin_sdk.md` §7 (Property Inspector handshake), §8 (QCefView→QWebChannel mapping table + cefQuery shim), §4.3 (`get/set/getGlobal/setGlobalSettings`, `sendToPlugin`/`sendToPropertyInspector`).
- `src/app/src/pi_bridge.{hpp,cpp}` — settings persistence (already wired) + relay; the QWebChannel bridge object.
- `src/app/src/property_inspector_controller.{hpp,cpp}` — PI lifecycle/controller.
- `src/app/qml/PIWebView.qml` + `PropertyInspector.qml` + `NativePropertyInspector.qml` — the PI UI.
- `src/app/src/pi_url_policy.hpp` + `pi_url_request_interceptor.hpp` — URL gating (serve sdpi.css here; restrict navigation).
- `.planning/phases/18-...spawn/18-03-PLAN.md` — the Mirabox `QWebEngineScript` injection pattern to mirror for the cefQuery polyfill.
- `.planning/phases/17-plugin-protocol-completion/17-*` — passHello + the registerPropertyInspector variant.
- CLAUDE.md — never QCefView; `Qt6::WebChannelQuick`; COD-031; never skip pre-commit; ASCII test names.
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free: extend the existing PI/pi_bridge tests. Assert (1) settings
  round-trip survives a fresh `PIBridge`/controller (write `setSettings`, reconstruct, read back
  equal — PLUGIN-13); (2) `sdpi.css` is served from the built-in URL (the interceptor returns it
  with `text/css`); (3) the cefQuery polyfill shim defines `window.cefQuery` and routes to the
  bridge (assert the injected script content + that a `cefQuery` call reaches a bridge slot —
  offscreen QWebEngine test if the project's `tests/qml` offscreen harness supports it, else a
  script-content + bridge-slot unit test); (4) selecting an action with a `PropertyInspectorPath`
  loads the PI and emits `didReceiveSettings`. ASCII test names; reuse the existing PI test file.

</specifics>

<deferred>
## Deferred Ideas

- Built-in in-process actions → Phase 21. Plugin store → Phase 22. Auxiliary surfaces → Phase 23.
- Live third-party `.sdPlugin` PI round-trip on hardware → Phase 25.

</deferred>

______________________________________________________________________

*Phase: 20-property-inspector-settings*
*Context gathered: 2026-05-23 (v1.3 replan locked decisions; akp_plugin_sdk.md §7/§8 is the spec; PI stack largely pre-built)*
