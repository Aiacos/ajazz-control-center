---
phase: 33-property-inspector-end-to-end
plan: 01
subsystem: plugins
tags: [property-inspector, sdk-2, lifecycle-events, qtwebengine, sd-plugin-server, elgato]

# Dependency graph
requires:
  - phase: 32
    provides: BIND-* plugin action binding pipeline + willAppear/willDisappear emission
provides:
  - titleParametersDidChange emitted inline after each willAppear (keys / encoders / touch zones), same context, same envelope
  - propertyInspectorDidAppear emitted on PI open (loadInspector completes)
  - propertyInspectorDidDisappear emitted on PI close (closeInspector) AND on the PI->PI switch teardown edge
  - new PropertyInspectorController inspectorOpened / inspectorClosed signals routed through the Application seam (controller stays free of a raw SdPluginServer*)
  - Catch2 test locking the titleParametersDidChange SDK-2 payload shape + willAppear-then-title ordering
affects: [Phase 33 plan 02 (PI thin-UI affordance + live didAppear/didDisappear protocolLog proof), Phase 34 EVENT-01 event-parity audit]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - PI lifecycle events routed through Application (new controller signals -> Application lambda -> SdPluginServer::sendEvent), preserving the audited indirection used by toPluginRequested / activeBridgeChanged
    - 'PI->PI switch disappear: capture inspector identity at load time as controller members so didDisappear carries the correct plugin/action/context after the outgoing bridge is torn down'

key-files:
  created: []
  modified:
    - src/app/src/plugin_device_bridge.cpp
    - tests/unit/test_plugin_device_bridge.cpp
    - src/app/src/property_inspector_controller.hpp
    - src/app/src/property_inspector_controller.cpp
    - src/app/src/application.cpp

key-decisions:
  - titleParametersDidChange emitted INLINE immediately after each willAppear send (not a separate pass) so wire ordering is guaranteed with no extra plumbing
  - Appear/disappear routed via new controller signals through Application, keeping PropertyInspectorController free of a raw SdPluginServer* (matches the existing toPluginRequested indirection)
  - PI->PI switch teardown fires inspectorClosed for the outgoing PI (safe superset; a plugin that ignores it is harmless)
  - titleParameters DEFAULT VALUES are [ASSUMED] from the Elgato SDK-2 reference (research A1/A2); the Catch2 test LOCKS the chosen shape, the exact values await the end-of-phase human-verify if a real plugin reads them
  - No sendEvent event-name allowlist added — sendEvent intentionally forwards any event string (research anti-pattern)

patterns-established:
  - Host->plugin lifecycle event emission via the controller-signal -> Application-seam -> sendEvent indirection

requirements-completed: [PI-04]

# Metrics
duration: ~25min
completed: 2026-06-08
---

# Phase 33 Plan 01: PI-04 Lifecycle Events Summary

**Wires the three missing SDK-2 PI lifecycle events on the wire — titleParametersDidChange inline after every willAppear, plus propertyInspectorDidAppear/DidDisappear routed through the Application seam (including the PI->PI switch teardown edge) — locked by a Catch2 payload-completeness + ordering test.**

## Performance

- **Duration:** ~25 min
- **Tasks:** 2 (Task 1 TDD)
- **Files modified:** 5

## Accomplishments

- `titleParametersDidChange` now ships on the wire immediately after each of the 3 `willAppear` registration sends (keys 1174 / encoders 1217 / touch zones 1266), same `owner` + `ctx`, via a new `titlePayload(ctx)` helper carrying the full SDK-2 shape (`settings`, `coordinates`, `controller`, `state`, `title`, `titleParameters{fontFamily,fontSize,fontStyle,fontUnderline,showTitle,titleAlignment,titleColor}`).
- `PropertyInspectorController` gained `inspectorOpened` / `inspectorClosed` signals; `loadInspector` emits open on completion and closed on the PI->PI switch teardown, `closeInspector` emits closed before teardown — all with correct plugin/action/context identity captured at load time.
- `application.cpp` routes both signals to `SdPluginServer::sendEvent(propertyInspectorDidAppear / propertyInspectorDidDisappear)` with the SDK-2 `{action, context}` envelope, mirroring the `activeBridgeChanged` seam. The controller never gains a raw `SdPluginServer*`.
- New Catch2 test asserts the title event follows `willAppear` for the same context AND carries every SDK-2 payload key (locks the [ASSUMED] shape).

## Task Commits

1. **Task 1: Emit titleParametersDidChange after each willAppear + Catch2 lock** - `f34e227` (feat, TDD test+impl in one commit)
1. **Task 2: Emit propertyInspectorDidAppear/DidDisappear via Application seam** - `2a98c8e` (feat)

## Files Created/Modified

- `src/app/src/plugin_device_bridge.cpp` - `titlePayload(ctx)` helper + 3 inline `titleParametersDidChange` sends after the willAppear sites.
- `tests/unit/test_plugin_device_bridge.cpp` - `titleParametersDidChange follows willAppear with complete SDK-2 payload` test (ASCII-only title, `[plugin-device-bridge][e2e][lifecycle][PI-04]`).
- `src/app/src/property_inspector_controller.hpp` - `inspectorOpened` / `inspectorClosed` signals + `activePluginUuid_/activeActionUuid_/activeContextUuid_` identity members.
- `src/app/src/property_inspector_controller.cpp` - emit open in `loadInspector`, closed in `closeInspector` + the PI->PI switch teardown.
- `src/app/src/application.cpp` - two connects routing the new signals to `sendEvent(propertyInspectorDidAppear / DidDisappear)`.

## Decisions Made

- **titleParameters default values are [ASSUMED]** (research A1/A2): `fontFamily ""`, `fontSize 12`, `fontStyle ""`, `fontUnderline false`, `showTitle true`, `titleAlignment "middle"`, `titleColor "#ffffff"`. The Catch2 test locks the chosen shape; the exact values are confirmed only if a real plugin reads them at the end-of-phase human-verify.
- **`title` resolves to empty string** — `ActionContext` carries no label/title field today; an empty SDK-2 `title` is a valid value (per the plan: "empty string is acceptable when no label").
- **No sendEvent allowlist** added — `sendEvent` already forwards any event name (sd_plugin_server.cpp:534-566 confirmed); the new names ship unchanged. GREEN files (pi_cef_shim, pi_bridge, plugin_settings_store, pi_url_policy, pi_url_request_interceptor, plugin_mirabox_shim, sd_plugin_server) untouched.

## Deviations from Plan

None - plan executed exactly as written. (The PI->PI switch identity capture used new controller members per the plan's explicitly-offered executor discretion.)

## Issues Encountered

- The pre-commit clang-format hook reflowed the new multi-line `sendEvent(...)` calls in `plugin_device_bridge.cpp` and `application.cpp` on first commit attempt (aborting the commit). Resolved with the standard re-add dance (`git add` the reformatted files, re-`git commit`) — never `--no-verify`. Functionally identical; no logic change.

## Live Verification Note

The **live didAppear / didDisappear `plugin.protocolLog` proof is plan 33-02's job** (it depends on the PI thin-UI open/close affordance + objectNames that plan 02 adds, driven via `scripts/ajazz-debug qml.invoke`). This plan delivers the emission + wiring + the unit-level lock for `titleParametersDidChange`; the full headless debug-channel walk-through is deferred to plan 02, and the real-PI human-verify to end-of-phase.

## Validation

- `ctest --preset linux-release -R "plugin_device|plugin-device-bridge"` -> 15/15 passed (incl. the new title test #700).
- `ctest -R "pi-bridge|plugin-server|property-inspector|..."` -> 20/20 passed.
- Full suite `ctest --preset linux-release -LE qml` -> **728/728 passed** (727 baseline + 1 new title test; no regression).

## Next Phase Readiness

- PI-04 emission + wiring complete; ready for plan 33-02 (PI thin-UI affordance + objectNames + live debug-channel didAppear/didDisappear protocolLog proof).
- Open item for the end-of-phase human-verify: confirm the [ASSUMED] titleParameters default values against a real `.sdPlugin` that reads them.

## Self-Check: PASSED

- FOUND: `.planning/phases/33-property-inspector-end-to-end/33-01-SUMMARY.md`
- FOUND: commit `f34e227` (Task 1)
- FOUND: commit `2a98c8e` (Task 2)

______________________________________________________________________

*Phase: 33-property-inspector-end-to-end*
*Completed: 2026-06-08*
