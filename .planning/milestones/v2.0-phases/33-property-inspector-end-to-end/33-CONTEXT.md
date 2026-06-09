# Phase 33: Property Inspector End-to-End - Context

**Gathered:** 2026-06-08
**Status:** Ready for planning

<domain>
## Phase Boundary

Close the Property Inspector to a working end-to-end state: a real plugin's PI HTML renders in
`QWebEngineView` with the `$SD` bridge; `sendToPlugin`/`sendToPropertyInspector` relay; per-context
and plugin-wide global settings round-trip AND survive an app restart; the PI lifecycle events
(`propertyInspectorDidAppear`/`DidDisappear`, `titleParametersDidChange`) fire correctly — the v1.3
stub is fully closed. Satisfies PI-01, PI-02, PI-03, PI-04.

Substantial PI infrastructure ALREADY EXISTS (pi_bridge, pi_cef_shim, property_inspector_controller,
pi_url_request_interceptor/pi_url_policy, plugin_mirabox_shim, PIWebView.qml, test_pi_bridge.cpp) —
so this phase is verify-vs-build, not greenfield. Does NOT implement per-app profile switching
(Phase 34) or Windows plugin support (Phase 35).

</domain>

<decisions>
## Implementation Decisions

### Scope, isolation & settings persistence

- **Research-first verify-vs-build.** Like Phase 32 (where BIND-03/06 were already done), the FIRST
  task is to determine what PI-01..04 already deliver against the live code (pi_bridge.cpp,
  property_inspector_controller.cpp, pi_cef_shim.cpp, PIWebView.qml, the existing PI lifecycle event
  emitters, the simulate RPCs) and build ONLY the real gaps. Do not rebuild working machinery.
- **Per-plugin WebEngine isolation:** each plugin UUID gets its own `QQuickWebEngineProfile`
  (PI-01 isolation requirement).
- **cefQuery polyfill is injected at `QWebEngineScript::DocumentCreation`** — the mandatory injection
  point (CLAUDE.md / STATE.md key decision); NEVER defer to `runJavaScript`.
- **Settings persistence is per-context AND plugin-wide global**, both round-trip through the bridge
  AND survive an app restart (persisted to disk). `didReceiveSettings` reaches the plugin (PI-03).

### Lifecycle events & verification

- **Full PI-04 event set:** `propertyInspectorDidAppear` on PI open, `propertyInspectorDidDisappear`
  on PI close, `titleParametersDidChange` immediately after `willAppear` is registered for a context.
- **Headless verification path:** `scripts/ajazz-debug plugin.simulatePiSettings` → `didReceiveSettings`
  in `plugin.protocolLog`; the PI panel open/close controls are `objectName`-addressed (reachable via
  `qml.invoke`); `titleParametersDidChange` payload completeness is asserted by a Catch2 unit test;
  `screenshot` confirms PI HTML content rendered (not a blank frame). ctest is necessary but NOT
  sufficient — the live debug-channel screenshot + protocolLog checks are part of done.
- **The PI HUMAN-VERIFY checkpoint (criterion 5) is DEFERRED to end-of-phase.** Build + headless-verify
  everything first; then PAUSE and hand the user a concrete walk-through: open the PI panel for a real
  bound action, have the PI JS call `$SD.setSettings({...})`, confirm `didReceiveSettings` reaches the
  plugin AND the setting survives an app restart. This is the one criterion that cannot be driven
  headlessly (real PI JS interaction).
- **Use a real `.sdPlugin` with a settings PI** for the human-verify (a vendor plugin or System
  Monitor / a weather plugin with a configurable PI) — not a synthetic test PI.

### Claude's Discretion

- Exact WebEngineProfile lifetime management, the precise gap set (determined by research), and PI
  panel control naming are at the executor's discretion, consistent with existing PI patterns.

</decisions>

\<code_context>

## Existing Code Insights

### Reusable Assets (the existing PI machinery — verify, don't rebuild)

- `src/app/src/pi_bridge.{hpp,cpp}` — the `$SD` bridge object (sendToPlugin/sendToPropertyInspector,
  getSettings/setSettings).
- `src/app/src/property_inspector_controller.{hpp,cpp}` — PI controller (loadInspector, lifecycle).
- `src/app/src/pi_cef_shim.cpp` — the cefQuery polyfill.
- `src/app/src/pi_url_request_interceptor.{hpp,cpp}` + `pi_url_policy.{hpp,cpp}` — sdpi.css serving +
  URL policy.
- `src/app/src/plugin_mirabox_shim.{hpp,cpp}` — vendor PI shim.
- `src/app/qml/PIWebView.qml`, `PropertyInspector.qml`, `NativePropertyInspector.qml`, `Inspector.qml`.
- `tests/unit/test_pi_bridge.cpp` — existing PI bridge tests.
- `src/app/src/debug_control_facade.cpp` — has simulate RPCs (simulatePiSettings/simulateAction).

### Established Patterns

- `Qt6::WebEngineQuick` + `Qt6::WebChannelQuick` (NOT WebChannel); WebEngine is gated behind
  `AJAZZ_HAVE_WEBENGINE` (the qml smoke test uses the minimal-Qt fallback — PI HTML path is covered
  by test_pi_bridge.cpp).
- `WebEngineView` has no `page` Q_PROPERTY — use `Qt6::WebChannelQuick` (CLAUDE.md gotcha).
- Every new interactive control needs an `objectName` (debug-addressability DOD).
- Live debug-channel verification mandatory.

### Integration Points

- property_inspector_controller ↔ pi_bridge ↔ sd_plugin_server (the protocol relay).
- PI panel open affordance in the device canvas / Inspector (PI-02 loadInspector wiring).
- Settings persistence store (plugin_settings_store) ↔ getSettings/setSettings round-trip.

\</code_context>

<specifics>
## Specific Ideas

- The v1.3 "stub" being closed: prior phases shipped partial PI (the bridge, shims, URL policy). The
  research must pin exactly which of PI-01..04 are already green vs stubbed.
- WebEngine availability: the build gates PI behind AJAZZ_HAVE_WEBENGINE — confirm the live binary has
  it enabled (the device editor app build) so the human-verify checkpoint can actually render PI HTML.
- The human-verify checkpoint is the user-flagged pause point — coordinate the walk-through at
  end-of-phase.

</specifics>

<deferred>
## Deferred Ideas

- Per-app profile auto-switching → Phase 34 (APROF-..).
- Full event-parity audit → Phase 34 (EVENT-01).
- Windows-only plugin PI handling → Phase 35.

</deferred>
