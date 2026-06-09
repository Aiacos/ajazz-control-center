---
phase: 33-property-inspector-end-to-end
reviewed: 2026-06-08T11:20:34Z
depth: standard
files_reviewed: 7
files_reviewed_list:
  - src/app/qml/Inspector.qml
  - src/app/qml/PIWebView.qml
  - src/app/src/application.cpp
  - src/app/src/plugin_device_bridge.cpp
  - src/app/src/property_inspector_controller.cpp
  - src/app/src/property_inspector_controller.hpp
  - tests/unit/test_plugin_device_bridge.cpp
findings:
  critical: 0
  warning: 3
  info: 4
  total: 7
status: issues_found
---

# Phase 33: Code Review Report

**Reviewed:** 2026-06-08T11:20:34Z
**Depth:** standard
**Files Reviewed:** 7
**Status:** issues_found

## Summary

Phase 33 is a small, additive change (251 insertions, 0 deletions) layering two
SDK-2 lifecycle surfaces onto the existing plugin pipeline:

1. **PI-04 `titleParametersDidChange`** — a new `titlePayload(ctx)` helper in
   `plugin_device_bridge.cpp`, emitted inline immediately after each of the 3
   `willAppear` sends (keypad / encoder / touch-zone branches of
   `populateContextsForActivePage`).
1. **PI-04 `propertyInspectorDidAppear` / `DidDisappear`** — new
   `inspectorOpened` / `inspectorClosed` signals on `PropertyInspectorController`,
   emitted on open / close / PI→PI-switch teardown, routed through the
   `Application` seam to `SdPluginServer::sendEvent`.
1. **PI-02 QML** — `objectName`s added to `Inspector.qml`
   (`openPiButton` / `closePiButton` / `piPanelLoader`) and `PIWebView.qml`
   (`piWebView`); open/close are `SecondaryButton`s, not `Switch`es.

**Scope verification (all clean):**

- GREEN files (`pi_cef_shim`, `pi_bridge`, `plugin_settings_store`,
  `pi_url_policy`) do NOT appear in the diff. Confirmed via
  `git diff --name-only 4f4222a..HEAD`.
- No `sendEvent` allowlist exists or was added — `SdPluginServer::sendEvent`
  is a generic passthrough (verified against `sd_plugin_server.cpp` and the
  representative-event test at `test_sd_plugin_server.cpp:474`). The two new PI
  event names ship on the wire unchanged. Confirmed.
- COD-031 N/A (app layer; QJsonObject only, no `nlohmann`).
- Qt: `PIWebView.qml` uses `QtWebChannelQuick` via the controller's
  `activeChannel` property and does NOT touch a `page` property (it binds the
  documented `profile` / `webChannel` / `url` trio). Confirmed.
- The controller holds NO raw `SdPluginServer*`; the indirection is preserved —
  `Application` owns `m_pluginServer` and performs the `sendEvent` in the
  connected lambdas (`this`-scoped, so lifetime-safe).

The wiring is correct and the per-context identity is captured at load time and
replayed correctly on the PI→PI-switch teardown edge. The defects below are
quality/robustness issues, not correctness blockers: redundant lifecycle churn
on same-key re-selection, an untested Application routing seam, and code
duplication in the payload builder.

## Warnings

### WR-01: Redundant `inspectorClosed`+`inspectorOpened` churn on identical re-selection

**File:** `src/app/src/property_inspector_controller.cpp:239-287`, `src/app/qml/Inspector.qml:142-147`
**Issue:** `loadInspector()` unconditionally tears down the previous channel and
rebuilds it — there is no guard for the case where the incoming
`(pluginUuid, actionUuid, contextUuid)` is identical to the currently-loaded
inspector. The teardown block emits `inspectorClosed(old)` (→
`propertyInspectorDidDisappear`) and the tail emits `inspectorOpened(new)` (→
`propertyInspectorDidAppear`). `Inspector.qml` calls `maybeLoadInspector()` from
THREE handlers — `onBindingChanged`, `onKeyIndexChanged`, `onEncoderIndexChanged`
— which can all fire for a single key selection (DeviceView binds `binding`,
`keyIndex`, and `encoderIndex` as the selection settles). The result is the
owning plugin can receive a spurious `propertyInspectorDidDisappear` immediately
followed by `propertyInspectorDidAppear` for an inspector that never actually
went away. A plugin that does teardown work (timers, polls) on disappear will
needlessly thrash.
**Fix:** Short-circuit when the requested inspector is already the active one,
before any teardown/emit. For example, at the top of the WebEngine branch in
`loadInspector`:

```cpp
if (activePluginUuid_ == pluginUuid && activeActionUuid_ == actionUuid &&
    activeContextUuid_ == contextUuid && webEngine_->activeChannel != nullptr) {
    return; // identical inspector already loaded — no churn
}
```

Alternatively (or additionally) de-duplicate the trigger in `Inspector.qml` so a
single selection does not invoke `maybeLoadInspector()` multiple times.

### WR-02: New Application routing seam (`inspectorOpened`/`inspectorClosed` → sendEvent) has zero automated coverage

**File:** `src/app/src/application.cpp:730-757`, `tests/unit/test_plugin_device_bridge.cpp`
**Issue:** The riskiest new logic in this phase is the PI-04 lifecycle routing
and the PI→PI-switch teardown edge in `loadInspector` (which must emit
`inspectorClosed` with the *captured prior* identity, not the new one). The only
new test (`test_plugin_device_bridge.cpp:1293`) covers
`titleParametersDidChange` exclusively. There is NO test asserting that:
(a) `inspectorOpened` produces a `propertyInspectorDidAppear` with the correct
`{action, context}` payload; (b) `closeInspector` produces a
`propertyInspectorDidDisappear`; (c) the PI→PI switch fires exactly one
disappear (old identity) + one appear (new identity), with no missed or doubled
emit. Given the project's own hard rule that "ctest green is necessary but NOT
sufficient" and the documented prior incident where a PI control passed 713 unit
tests yet was a runtime no-op, shipping this seam untested is a real regression
risk.
**Fix:** Add a `PropertyInspectorController` test (under the
`AJAZZ_HAVE_WEBENGINE` guard) that drives `loadInspector` → `loadInspector`
(switch) → `closeInspector` and asserts the `inspectorOpened` / `inspectorClosed`
emission order and identity via `QSignalSpy`. Confirm the switch-edge disappear
carries the OLD `(pluginUuid, actionUuid, contextUuid)`. Additionally verify
end-to-end through the debug channel per CLAUDE.md (build → launch → drive
`openPiButton`/`closePiButton` → confirm the plugin receives the events).

### WR-03: `titlePayload()` duplicates the settings-parse + shared-key block from `instancePayload()`

**File:** `src/app/src/plugin_device_bridge.cpp:340-367`
**Issue:** `titlePayload()` copies the entire settings-parsing prologue and the
`settings` / `coordinates` / `controller` / `state` key construction verbatim
from `instancePayload()` (lines 311-328). The two now drift independently — a
future fix to the settings-parse logic or a coordinates-shape change must be
applied in both places or the `willAppear` and `titleParametersDidChange`
payloads silently disagree about the same context. The phase's own comment
("Mirrors instancePayload()") acknowledges the coupling without enforcing it.
**Fix:** Build the title payload by extending the instance payload rather than
re-deriving it:

```cpp
QJsonObject titlePayload(ActionContext const& ctx) {
    QJsonObject obj = instancePayload(ctx);
    obj.remove(QStringLiteral("isInMultiAction")); // not part of the title shape
    obj.insert(QStringLiteral("title"), QString{});
    obj.insert(QStringLiteral("titleParameters"), QJsonObject{ /* ...defaults... */ });
    return obj;
}
```

This guarantees the shared keys can never diverge.

## Info

### IN-01: `titleParametersDidChange` is NOT emitted after the state-change `willAppear` in `renderToggleState`

**File:** `src/app/src/plugin_device_bridge.cpp:1137-1138`
**Issue:** `renderToggleState()` re-sends a `willAppear` for a single owning
context on a state change (line 1137) but does NOT follow it with a
`titleParametersDidChange`. The phase contract is "titleParametersDidChange
follows each willAppear"; this is the one `willAppear` site that does not. It is
plausibly intentional (PI-04 scopes the title surface to appear-time, and a
state toggle is not an appear), but the asymmetry is undocumented and a plugin
relying on the pairing will see one unpaired `willAppear`.
**Fix:** Either emit `titleParametersDidChange` after the line-1137 `willAppear`
for consistency, or add a one-line comment at that site explaining why the title
event is deliberately omitted on a state-change re-appear.

### IN-02: New `titleParametersDidChange` test covers only the Keypad branch

**File:** `tests/unit/test_plugin_device_bridge.cpp:1293-1351`
**Issue:** The test binds only a keypad key (`prof.keys[2]`) and asserts
`controller == "Keypad"`. The encoder
(`plugin_device_bridge.cpp:1263`) and touch-zone (`:1316`) emit sites are
structurally identical but untested. Because the payload helper is shared, the
risk is low, but a future edit that special-cases the encoder/touch branch would
not be caught.
**Fix:** Extend the test (or add a sibling) with an encoder binding
(`prof.encoders`) and assert a `titleParametersDidChange` with
`controller == "Encoder"` is also emitted.

### IN-03: `titlePayload` always emits an empty `title` despite the docstring promising a binding label

**File:** `src/app/src/plugin_device_bridge.cpp:355`
**Issue:** The docstring says "`title` is sourced from the binding label on ctx",
but `ActionContext` carries no label field, so `title` is hardcoded to
`QString{}`. The behaviour is acknowledged in the comment and is a valid SDK-2
value, but a plugin reading `payload.title` at appear time will always see "",
even when the user has set a key label in the Inspector. This is a latent
functional gap (the title the device renders is not reflected to the plugin),
not a bug in the wire shape.
**Fix:** When the Inspector label is plumbed into `ActionContext` (future work),
populate `title` from it. For now, soften the docstring to state that `title` is
intentionally always empty until the label is plumbed through, so the comment
does not imply behaviour that is absent.

### IN-04: `closePiButton` enabled-state binds to a singleton property — verify it deselects correctly

**File:** `src/app/qml/Inspector.qml:217`
**Issue:** `enabled: PropertyInspectorController.hasHtmlInspector` is correct, but
in a no-WebEngine build `hasHtmlInspector` is always false, so the button is
permanently disabled while still visible (the `RowLayout` is gated only on
`root.hasSelection`). This is acceptable (there is nothing to close), but the
"Open inspector" button (`openPiButton`) has no such guard and is always enabled
even when WebEngine is absent — clicking it calls `maybeLoadInspector()`, which
early-returns via `closeInspector()` (a no-op in stub builds). Harmless, but the
affordance is misleading (an enabled "Open" button that can never open a panel on
minimal Qt installs).
**Fix:** Optionally gate the whole `RowLayout` (or at least `openPiButton`) on
`PropertyInspectorController.webEngineAvailable` so the controls only appear when
an HTML PI can actually be hosted.

______________________________________________________________________

_Reviewed: 2026-06-08T11:20:34Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
