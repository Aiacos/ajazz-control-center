---
phase: 32-binding-layer-fix-multi-toggle-action-device-editor
verified: 2026-06-08T10:40:00Z
status: human_needed
score: 5/5 must-haves verified (deterministic layer); 2 live walks await human
overrides_applied: 0
human_verification:
  - test: Multi Action live sequencing walk (BIND-04)
    expected: Bind a 2-child Multi Action to a key (second child carries delayMs); fire via scripts/ajazz-debug input.key; plugin.protocolLog shows BOTH children firing in order with the inter-step gap visible between them.
    why_human: Requires authoring a Multi Action ActionInstance binding live; the debug-channel instance-commit RPC is not yet wired, so the end-to-end author->press->protocolLog walk cannot be driven headlessly. Adapter + dispatch seam + engine.run are fully unit-covered (tests 321-325, 366-368) and the dispatch pipeline is live-confirmed, but the authored-binding sequencing has not been walked on hardware.
  - test: Toggle Action 3-state live cycle + persist walk (BIND-05/07)
    expected: Bind a 3-state Toggle to a key; press input.key x4; screenshot after each press shows the key face cycling 0->1->2 and WRAPPING to 0; relaunch the app and confirm the key renders the last persisted currentState.
    why_human: Requires authoring a 3-state Toggle ActionInstance binding live (same un-wired instance-commit RPC) and a visual confirmation of the per-state repaint + restart persistence. cycleInstanceState (mod-N, persist), the render seam, and the state-change willAppear are unit-covered (tests 197-202) and the dispatch path is live-confirmed (input.key index=8 -> context Keypad#1#3), but the visual face-cycle + cross-restart persistence has not been walked on hardware.
---

# Phase 32: Binding Layer Fix + Multi/Toggle Action + Device Editor Verification Report

**Phase Goal:** Drag-to-bind fires willAppear immediately without a reconnect; Multi Action and Toggle Action dispatch correctly cycle state and render per-state images; the device editor scrolls for large SKU grids; the binding layer is device-generic across all Stream Dock SKUs.
**Verified:** 2026-06-08T10:40:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| #   | Truth                                                                                     | Status                                | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| --- | ----------------------------------------------------------------------------------------- | ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Drag-to-bind fires willAppear in the same interaction, no reconnect (BIND-03)             | ✓ VERIFIED                            | Envelope carries top-level `action` field at `plugin_device_bridge.cpp:338` (`{"action", ctx.actionUUID}`); `resolveOwner` (:852) uses the stored-owner map (last changed pre-phase in `519ecd0`/`ddabc16` — plan-04 added tests only, production untouched). Regression tests #709 (`BIND-03 outbound envelope carries top-level action field`) and #710 (`stored-owner resolver resolves a non-dotted-prefix action`) PASS.                                                                                                                                                                                                     |
| 2   | Multi Action runs children sequentially with inter-step delay on a single press (BIND-04) | ✓ VERIFIED (logic); live walk pending | `ActionInstance.delayMs` added (`action_instance.hpp:105`), round-trips through profile JSON (tests #310-312). `instanceChildrenToChain` adapter (`action_chain_adapter.cpp`) flattens children preserving order + delayMs, depth-cap kMaxDepth=64 (tests #321-325). Dispatch seam at `stream_dock_input_service.cpp:90` (`engine.run(core::instanceChildrenToChain(*binding.instance))`) via `runPressBinding`. Built-in registered, `handles()==true` (test #367). Live sequencing walk deferred (see Human Verification).                                                                                                      |
| 3   | Toggle press cycles currentState mod N, renders per-state image, persists (BIND-05/07)    | ✓ VERIFIED (logic); live walk pending | `cycleInstanceState` (`profile_controller.cpp:665`) advances `(currentState+1) % states.size()`, guards `states.size() <= 1`, persists via real `saveActiveProfile()` (:789, writeProfileToDisk), emits profileChanged. Render via `renderToggleState` (`plugin_device_bridge.cpp:1011`) reusing assignKeyImage + state-change willAppear. Hooks wired in `application.cpp:510` (cycle) + `:579` (render). Tests #197-202 PASS incl. 0->1->2->0 wrap + persistence-across-reload + render+willAppear-per-press. Live face-cycle/persist walk deferred.                                                                            |
| 4   | input.key/encoder/touch route to plugin contexts, device-generic, no SKU code (BIND-06)   | ✓ VERIFIED                            | Dispatch switches on `ev.kind` only (`stream_dock_input_service.cpp:246`); toggle/multi branches contain no SKU literal. `onDeviceEvent` resolves by control type via `byCoord` with no codename branch. Test #711 (`BIND-06 onDeviceEvent routes Keypad Encoder and touch-zone generically by control type with no SKU branch`) PASS. AKP05-specific `zoneForX` is touch-strip wire decode gated by `descriptor.hasTouchStrip` — pre-existing, not a binding-layer SKU branch.                                                                                                                                                   |
| 5   | Device editor scrolls >8col/>4row, scroll position qml.get-addressable (EDIT-01)          | ✓ VERIFIED                            | `ScrollView { id: deviceCanvasScroll; objectName: "deviceCanvasScroll" }` + `contentItem.objectName: "deviceCanvasFlick"` in `DeviceView.qml:370-378`; `ScrollBar.horizontal.policy: ScrollBar.AlwaysOff` (:376); `width: deviceCanvasScroll.availableWidth` (:383); `_gridOverflows` helper (:80). Collapse regression fixed `34a0b73` via `height: implicitHeight` (:393). QML tests #737-744 (addressable, over/under-threshold overflow + scrollable, no-horizontal-ever, trash-pinned, delegates-distinct) PASS. Orchestrator live-confirmed canvas renders 5x2 grid + zones and scrolls (contentHeight 397 > viewport 256). |

**Score:** 5/5 truths verified at the deterministic (code + automated-test) layer. Two of them (BIND-04, BIND-05/07) carry a deferred live-walk human-verify item.

### Required Artifacts

| Artifact                                               | Expected                                           | Status     | Details                                                                                                    |
| ------------------------------------------------------ | -------------------------------------------------- | ---------- | ---------------------------------------------------------------------------------------------------------- |
| `src/core/include/ajazz/core/action_instance.hpp`      | ActionInstance.delayMs field                       | ✓ VERIFIED | `delayMs{0}` at :105, doc-commented; nlohmann/Qt-free                                                      |
| `src/core/include/ajazz/core/action_chain_adapter.hpp` | instanceChildrenToChain decl                       | ✓ VERIFIED | `[[nodiscard]] ActionChain instanceChildrenToChain(...)` :46; Qt/nlohmann-free                             |
| `src/core/src/action_chain_adapter.cpp`                | children->ActionChain w/ delayMs + depth cap       | ✓ VERIFIED | flatten pre-order, kMaxDepth=64, copies id/settings/delayMs; 0 nlohmann/Qt includes                        |
| `src/app/src/stream_dock_input_service.cpp`            | Multi + Toggle dispatch seam, generic              | ✓ VERIFIED | `instanceChildrenToChain` :90; `dispatchToggle` :425; switch on ev.kind; no SKU branch in binding dispatch |
| `src/app/src/profile_controller.cpp`                   | cycleInstanceState mutate+persist                  | ✓ VERIFIED | :665, mod-N, `<=1` guard, real saveActiveProfile + profileChanged                                          |
| `src/app/src/plugin_device_bridge.cpp`                 | renderToggleState + envelope.action + resolveOwner | ✓ VERIFIED | renderToggleState :1011; envelope action :338; resolveOwner :852 (unchanged by phase)                      |
| `src/app/qml/DeviceView.qml`                           | ScrollView#deviceCanvasScroll                      | ✓ VERIFIED | :370-393, AlwaysOff horizontal, availableWidth, implicitHeight                                             |
| `tests/unit/test_plugin_device_bridge.cpp`             | BIND-03/06 regression                              | ✓ VERIFIED | TEST_CASEs :1836 (action), :1926 (stored-owner), :1989 (per-controller routing); production diff = none    |
| `tests/qml/test_device_view_geometry.qml`              | scroll geometry asserts                            | ✓ VERIFIED | DeviceViewScroll suite #737-744                                                                            |

### Key Link Verification

| From                      | To                          | Via                                    | Status  | Details                                                          |
| ------------------------- | --------------------------- | -------------------------------------- | ------- | ---------------------------------------------------------------- |
| action_chain_adapter.cpp  | ActionEngine::run           | produces ActionChain                   | ✓ WIRED | runPressBinding calls `engine.run(instanceChildrenToChain(...))` |
| stream_dock_input_service | cycleInstanceState          | setToggleCycleHook                     | ✓ WIRED | application.cpp:510-511                                          |
| stream_dock_input_service | renderToggleState           | setToggleRenderHook                    | ✓ WIRED | application.cpp:579-581                                          |
| cycleInstanceState        | profile JSON                | saveActiveProfile (writeProfileToDisk) | ✓ WIRED | profile_controller.cpp:715 -> :789 (real disk write)             |
| renderToggleState         | assignKeyImage + willAppear | setState repaint path                  | ✓ WIRED | plugin_device_bridge.cpp:1057 + :1098 sendEvent                  |

### Data-Flow Trace (Level 4)

| Artifact                | Data Variable               | Source                                        | Produces Real Data                                                     | Status    |
| ----------------------- | --------------------------- | --------------------------------------------- | ---------------------------------------------------------------------- | --------- |
| cycleInstanceState      | inst.currentState           | mutated in m_profile.keys/encoders, persisted | ✓ (saveActiveProfile -> writeProfileToDisk, reload test #199 confirms) | ✓ FLOWING |
| renderToggleState       | states[currentState].visual | live re-read via ProfileAccessor after cycle  | ✓ (assignKeyImage real; willAppear via real SdPluginServer::sendEvent) | ✓ FLOWING |
| DeviceCanvas iconSource | bindings model              | \_syncFromProfile + onKeyImageAssigned        | ✓ (orchestrator live screenshot confirmed CPU key face updates)        | ✓ FLOWING |

### Index-Base Sanity Check (CR-01/CR-02 disposition independent re-verification)

The review claimed `renderToggleState`'s `index + 1` double-converts. Independently verified REFUTED:

- `dispatchToggle("Keypad", ev.index)` passes the SAME `index` used by `prof.keys.find(ev.index)` (input service treats it as the profile-key index).
- `populateContextsForActivePage` registers contexts at `coordsForKeyIndex(keyIdx0 + 1)` where keyIdx0 is the 0-based profile key (`plugin_device_bridge.cpp:1143-1145`).
- `renderToggleState` resolves the willAppear context at `coordsForKeyIndex(index + 1)` (:1080) — the SAME `+1` convention used at registration. Conventions agree.
- `coordsForKeyIndex` math confirmed: for index=8, `coordsForKeyIndex(9, cols=5)` -> row (9-1)/5=1, col (9-1)%5=3 -> `Keypad#1#3`, matching the disposition's live-observed context.
- (The reviewer's confusion stemmed from `onDeviceEvent` at :884 which feeds a *differently-based* `ev.index` directly; that is a separate path and does not feed renderToggleState.)

Residual the reviewer surfaced is legitimate but non-blocking: `test_toggle_dispatch.cpp` stubs the render hook, so the real `renderToggleState` coordinate conversion has no automated regression guard (it is unit-tested only via the cycle/persist half + the stub-hook seam). Covered by the deferred live Toggle walk.

### Behavioral Spot-Checks

| Behavior                          | Command                                                                                                             | Result                                               | Status          |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- | --------------- |
| Phase-32 targeted suites green    | `ctest --preset linux-release -R "action_instance\|multiaction\|toggle\|plugin_device\|action_engine\|device_view"` | 25/25 + 9/9                                          | ✓ PASS          |
| BIND-03/06/07 regression green    | `ctest --preset linux-release -R "[toggle]\|BIND-03\|BIND-06\|BIND-07"`                                             | 9/9                                                  | ✓ PASS          |
| Full suite + qml                  | `ctest --preset linux-release`                                                                                      | 744/744, 17 qml                                      | ✓ PASS          |
| COD-031 boundary                  | `grep -rn '#include.*nlohmann' src/core/include/`                                                                   | 0                                                    | ✓ PASS          |
| New core TU Qt/nlohmann-free      | `grep -nE 'nlohmann\|#include <Q' src/core/src/action_chain_adapter.cpp`                                            | 0                                                    | ✓ PASS          |
| Live Multi Action sequencing      | (author binding + input.key + protocolLog)                                                                          | not runnable headless (un-wired instance-commit RPC) | ? SKIP -> human |
| Live Toggle 3-state cycle+persist | (author binding + input.key x4 + screenshot + restart)                                                              | not runnable headless                                | ? SKIP -> human |

### Requirements Coverage

| Requirement | Source Plan | Description                                                 | Status                          | Evidence                                                     |
| ----------- | ----------- | ----------------------------------------------------------- | ------------------------------- | ------------------------------------------------------------ |
| BIND-03     | 32-04       | willAppear fires in-interaction, no reconnect               | ✓ SATISFIED                     | envelope.action :338 + resolveOwner :852; tests #709/#710    |
| BIND-04     | 32-01/02    | Multi Action children run sequentially w/ delay             | ✓ SATISFIED (live walk pending) | adapter + seam + engine.run; tests #321-325/#366-368         |
| BIND-05     | 32-02/03    | Toggle cycles currentState + renders + willAppear           | ✓ SATISFIED (live walk pending) | cycleInstanceState + renderToggleState; tests #197-202       |
| BIND-06     | 32-04       | device-generic key/encoder/touch, no SKU code               | ✓ SATISFIED                     | switch-on-kind; test #711                                    |
| BIND-07     | 32-03       | setState renders correct per-state image; index round-trips | ✓ SATISFIED (live walk pending) | renderToggleState + currentState round-trip; tests #199/#200 |
| EDIT-01     | 32-05       | editor scrolls >8col/>4row, controllers distinct            | ✓ SATISFIED                     | ScrollView#deviceCanvasScroll; tests #737-744 + live         |

No orphaned requirements: all six declared IDs map to plans and are covered.

### Anti-Patterns Found

| File                          | Line | Pattern       | Severity | Impact                                                                                                                                         |
| ----------------------------- | ---- | ------------- | -------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| stream_dock_input_service.cpp | 306  | `TODO(WR-05)` | ℹ️ Info  | Tracked follow-up (encoder onRelease dispatch once profile.hpp adds the field). Out of scope for all phase-32 success criteria; not a blocker. |

No TBD/FIXME/XXX debt markers in any phase-32-modified production file. The single TODO references a formal review item (WR-05) and a future field, so it is auditable.

### Doc Note (non-blocking)

`REQUIREMENTS.md` BIND-04 prose still says "via a built-in `opendeck.multiaction` handler"; the shipped code correctly uses the `com.hotspot.streamdock.` prefix (so `handles()` returns true). BIND-05's prose was updated to call this out; BIND-04's prose is stale. The implementation is correct — this is a documentation inconsistency, not a code gap.

### Human Verification Required

Both items are legitimately deferred (the disposition + both plan SUMMARY files record them as "Pending live verification"): authoring a Multi/Toggle ActionInstance binding live needs a debug-channel instance-commit RPC that is not yet wired. The underlying logic is fully unit-covered and the dispatch pipeline is live-confirmed (input.key -> context resolution). Not addressed by any later milestone phase (Phase 33+ are PI / per-app / Windows).

#### 1. Multi Action live sequencing walk (BIND-04)

**Test:** Bind a 2-child Multi Action to a key (second child carries a delayMs); fire it via `scripts/ajazz-debug input.key {index,pressed:true}` then release.
**Expected:** `scripts/ajazz-debug plugin.protocolLog` shows BOTH children firing in order, with the inter-step gap visible between them (second child arrives ~delayMs after the first).
**Why human:** Requires authoring a Multi Action binding live (un-wired instance-commit RPC); the author->press->protocolLog walk cannot be driven headlessly.

#### 2. Toggle Action 3-state live cycle + persist walk (BIND-05/07)

**Test:** Bind a 3-state Toggle to a key (3 distinct state images); press `input.key` four times; `screenshot` after each press; then relaunch the app.
**Expected:** Key face cycles 0->1->2 and WRAPS to 0 on the 4th press; after relaunch the key renders the last persisted currentState; if a plugin owns the action, protocolLog shows a willAppear with incrementing `payload.state`.
**Why human:** Requires authoring a 3-state Toggle binding live + visual confirmation of the per-state repaint and cross-restart persistence.

### Gaps Summary

No code gaps. The deterministic layer is complete and green: 744/744 unit + 17 qml tests pass, COD-031 boundary intact, both code-review BLOCKERs independently confirmed as false positives (index-base conventions agree end-to-end). All five ROADMAP success criteria and all six requirement IDs are satisfied at the implementation + automated-test level, with three artifacts (toggle render hook coordinate path, Multi sequencing, Toggle face-cycle persistence) additionally relying on two deferred live human-verify walks that cannot be automated until a debug-channel instance-commit RPC is wired. Per the phase's own checkpoints and the orchestrator disposition, these two live walks are the sole outstanding items — hence status `human_needed` rather than `gaps_found`.

______________________________________________________________________

_Verified: 2026-06-08T10:40:00Z_
_Verifier: Claude (gsd-verifier)_
