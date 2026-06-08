# Phase 32: Binding Layer Fix + Multi/Toggle Action + Device Editor - Research

**Researched:** 2026-06-08
**Domain:** In-process action dispatch (C++20 / Qt 6 core+app) + QML editor container (Qt Quick Controls)
**Confidence:** HIGH (all claims verified against current code on `experiment/mirajazz`; no external packages)

\<user_constraints>

## User Constraints (from 32-CONTEXT.md)

### Locked Decisions

- **Multi/Toggle built-in handlers live in `builtin_action_registry.cpp`** — register
  `opendeck.multiaction` and `opendeck.toggleaction` alongside the existing built-ins, via the
  established `ActionKind`/built-in mechanism. *(Note: handler registration actually happens in
  `builtin_actions_service.cpp::populate()`; `builtin_action_registry.cpp` is the dumb dispatch
  table. See "Critical correction" below.)*
- **Multi Action REUSES the ActionEngine chain walker** — sequence `instance.children`
  (ActionInstances) through the existing sequential walk honoring each child's `delayMs`
  (`scheduleAfter`/sleep). No new async runner.
- **Toggle Action cycles `currentState` through ALL `states[]` (mod N)** per press; renders
  `states[currentState]` via `setState`; emits the state-change `willAppear`. Not 2-state-limited.
- **BIND-07 setState renders from `instance.states[currentState]`** (imagePath/title) via the
  existing repaint path; per-state settings carry the state index.
- **BIND-03 is CLOSE-THE-GAP + VERIFY, not rewire** — the `profileChanged → populateContextsForActivePage` wire ALREADY EXISTS (`application.cpp:635-643`, PLUGIN-19,
  T-28-09 empty-device guard).
- **Fix the two known willAppear bugs** — (1) payload lacks `action` field; (2) owner-match needs
  root-UUID prefix. **CONFIRM against live code first (they are HYPOTHESES).**
- **BIND-06: one generic dispatch path** keyed on control TYPE not SKU; no SKU-specific code.
- **EDIT-01: wrap the device grid in a vertical `ScrollView`**, `objectName "deviceCanvasScroll"`,
  exposing scroll-position; overflow rule `keyColumns > 8 OR rowCount > 4` (rowCount counts encoder
  row + touch-zone row); keep delegates distinct.

### Claude's Discretion

- Exact built-in id strings, internal handler signatures, ScrollView styling, and how children
  ActionInstances are adapted into the ActionEngine's ActionChain.

### Deferred Ideas (OUT OF SCOPE)

- Property Inspector settings round-trip over `$SD` bridge → Phase 33 (PI-01..).
- Per-app profile auto-switching → Phase 34 (APROF-..).
- Real encoder/touch wire decode (HARDWARE-GATED, needs retail AKP05E) — routing validated via
  synthetic `input.*` RPCs only.
  \</user_constraints>

\<phase_requirements>

## Phase Requirements

| ID                     | Description                                                                                    | Research Support                                                                                                                                                                                                                            |
| ---------------------- | ---------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| BIND-03                | Drag-to-bind fires `willAppear` immediately, no reconnect                                      | Wire exists (`application.cpp:635-643`); both hypothesized bugs ALREADY FIXED in current code — see Finding 1. Scope shrinks to **verify-live + regression test**.                                                                          |
| BIND-04                | Multi Action dispatches children sequentially with inter-step delay                            | New `opendeck.multiaction` handler + an `ActionInstance.children → ActionChain` adapter feeding the existing `ActionEngine::run` (Finding 2).                                                                                               |
| BIND-05                | (Multi Action — same family as BIND-04; sequencing semantics)                                  | Reuse `ActionEngine::runFrom` delay deferral (`action_engine.cpp:114-131`).                                                                                                                                                                 |
| BIND-06                | One generic dispatch path for key / encoder-dial / touch-zone, no SKU branching                | ALREADY device-generic in `PluginDeviceBridge::onDeviceEvent` (switch on `DeviceEvent::Kind`, `byCoord(deviceId, controller,…)`) and `StreamDockInputService::dispatch` — see Finding 4. Scope = **verify, add per-controller-type tests**. |
| BIND-07                | Toggle Action cycles `currentState`, renders per-state visual, emits state-change `willAppear` | New `opendeck.toggleaction` handler; needs a **binding-identity + profile-mutation seam** (the central new design problem — Finding 3). Reuses the `setState` repaint path at `plugin_device_bridge.cpp:612-655`.                           |
| EDIT-01                | Wrap device grid in vertical `ScrollView`, debug-addressable scroll position                   | Wrap `DeviceCanvas#deviceCanvas` inside `chassisArea` (`DeviceView.qml:337-341`); keep `trashBtn`/`EmptyState`/`chassisPhoto` as siblings — Finding 5.                                                                                      |
| \</phase_requirements> |                                                                                                |                                                                                                                                                                                                                                             |

## Summary

This phase is **mostly wiring + one genuinely new design seam**, not greenfield. Three of the six
requirements are already substantially implemented and reduce to *verify-live + add regression
tests*:

- **BIND-03's two "known bugs" are already fixed.** The `willAppear` envelope **does** carry the
  top-level `action` field (`plugin_device_bridge.cpp:337`), and owner-match **does** use the
  stored-owner map first (`resolveOwner` → `PluginManager::ownerForAction`,
  `plugin_device_bridge.cpp:851-863`, wired at `application.cpp:524-526`). These were fixed
  2026-06-02 (commits `ddabc16` + `519ecd0`, recorded in auto-memory
  `project_plugin_install_demo_working`). The phase task is to **prove** drag-to-bind fires
  `willAppear` in the same interaction via the debug channel and lock it with a test — not to
  re-fix.
- **BIND-06 is already device-generic.** `onDeviceEvent` switches on `DeviceEvent::Kind` and looks
  up contexts by `(deviceId, controller, row, column)` with **zero** SKU branching. Same for
  `StreamDockInputService::dispatch`. Verify + add per-controller-type tests.

The **one new design problem** is BIND-04/05/07 dispatch wired to the **Phase 31 `ActionInstance`
model**. The existing built-in `multiactions` handler
(`builtin_actions_service.cpp:444-451`) decodes a **flat** `settingsJson {"actions":[...]}` array —
it does **not** read `binding.instance.children`. And the toggle handler needs something the
built-in registry was never designed to provide: **knowledge of which binding fired it, and write
access to that binding's `currentState`**. `BuiltinHandler` is `void(string_view settingsJson)` —
no binding identity, no Profile handle. Bridging that gap (an `ActionInstance → ActionChain`
adapter for Multi, and a binding-identity + profile-mutation seam for Toggle) is the core of the
phase.

**Primary recommendation:** Treat BIND-03 and BIND-06 as verify-and-test-only. Concentrate design
effort on (a) a small `ActionInstance → ActionChain` adapter in core, and (b) the toggle
state-mutation seam — most cleanly solved by **resolving the toggle in `StreamDockInputService`
(which HAS the live Profile and the firing key index) before/instead of going through the opaque
built-in registry**, since the registry handler cannot see `currentState`.

## Architectural Responsibility Map

| Capability                                      | Primary Tier                                                                  | Secondary Tier                                         | Rationale                                                                                                                                                               |
| ----------------------------------------------- | ----------------------------------------------------------------------------- | ------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Multi Action sequencing                         | core (`ActionEngine`)                                                         | app (`BuiltinActionsService`/`StreamDockInputService`) | Chain walking + delay deferral is already a pure-core concern; only the children→chain decode is app-side (Qt JSON).                                                    |
| `ActionInstance.children → ActionChain` adapter | core                                                                          | —                                                      | Pure data transform over core structs; nlohmann-free, Qt-free → belongs in core next to `action_instance.hpp`/`action_engine`.                                          |
| Toggle `currentState` mutation                  | app (`ProfileController` / `StreamDockInputService`)                          | —                                                      | The live mutable Profile is owned by `ProfileController`; the firing key index is known only in `StreamDockInputService::dispatch`. Core has no event-routing identity. |
| Per-state visual repaint (setState)             | app (`PluginDeviceBridge` / `StreamDockControlService`)                       | —                                                      | Repaint path already exists at `plugin_device_bridge.cpp:612-655` + the `keyImageAssigned` mirror.                                                                      |
| willAppear emission                             | app (`PluginDeviceBridge`)                                                    | —                                                      | Owns the context registry + the SdPluginServer socket.                                                                                                                  |
| Device-generic input routing                    | app (`PluginDeviceBridge::onDeviceEvent`, `StreamDockInputService::dispatch`) | —                                                      | Already type-keyed; no SKU code.                                                                                                                                        |
| Scrollable editor container                     | QML (`DeviceView.qml`)                                                        | —                                                      | Pure container/layout change; no backend.                                                                                                                               |

## Standard Stack

No new external packages. This phase uses only in-tree code + existing Qt 6 / Catch2 / the debug
channel. **The Package Legitimacy Audit and Environment Availability sections are intentionally
omitted — zero new dependencies.**

| Component                                  | Where                                                            | Role in this phase                                                                              |
| ------------------------------------------ | ---------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `ajazz::core::ActionEngine`                | `src/core/src/action_engine.cpp`                                 | Reuse `run()`/`runFrom()` for Multi Action (delay-honoring sequential walk).                    |
| `ajazz::core::ActionInstance`              | `src/core/include/ajazz/core/action_instance.hpp`                | Phase 31 model: `{id, states[], currentState, settings, children[]}`.                           |
| `ajazz::core::BuiltinActionRegistry`       | `src/core/src/builtin_action_registry.cpp`                       | Dumb `uuid → handler(settingsJson)` dispatch table.                                             |
| `BuiltinActionsService`                    | `src/app/src/builtin_actions_service.cpp`                        | Where handlers are actually `registerAction`-ed in `populate()`.                                |
| `StreamDockInputService`                   | `src/app/src/stream_dock_input_service.cpp`                      | `dispatch()` routes `DeviceEvent → binding.onPress → engine.run`. The toggle seam belongs here. |
| `PluginDeviceBridge`                       | `src/app/src/plugin_device_bridge.cpp`                           | willAppear / setState / device-generic routing.                                                 |
| `ProfileController`                        | `src/app/src/profile_controller.cpp`                             | Owns the mutable live Profile; emits `profileChanged`.                                          |
| Qt Quick Controls `ScrollView`/`ScrollBar` | `DeviceView.qml` (already imports `QtQuick.Controls` at line 40) | EDIT-01 wrapper.                                                                                |
| Catch2                                     | `tests/unit/`                                                    | Core dispatch unit tests. `catch_discover_tests` at `tests/unit/CMakeLists.txt:683`.            |
| `scripts/ajazz-debug`                      | repo root                                                        | Live verification RPCs (mandatory per CLAUDE.md).                                               |

## Architecture Patterns

### Dispatch flow (verified, end-to-end)

```
scripts/ajazz-debug input.key {index, pressed}
  -> debug_control_facade.cpp:303  registerMethod("input.key")
  -> PluginDebugService::simulateKey(index, pressed)
  -> [synthetic DeviceEvent{KeyPressed}] fan-out to TWO consumers:

   (A) StreamDockInputService::dispatch(ev)            [stream_dock_input_service.cpp:195]
        case KeyPressed: m_engine->run(binding.onPress) [:204-208]
          -> ActionEngine::runFrom walks chain          [action_engine.cpp:58]
            case Plugin: execs.plugin(id, settingsJson) [:64-68]
              -> (application.cpp:341) m_builtinActions->onPluginAction(id, settings)
                -> BuiltinActionsService::onPluginAction  [builtin_actions_service.cpp:218]
                  -> registry.handles(id) ? registry.dispatch(id, settings) : fallback
                    -> the registered BuiltinHandler lambda runs   <-- Multi/Toggle land here

   (B) PluginDeviceBridge::onDeviceEvent(deviceId, ev)  [plugin_device_bridge.cpp:865]
        case KeyPressed: byCoord(deviceId,"Keypad",r,c)  [:881-899]
          -> sendEvent(pluginUuid, eventEnvelope("keyDown", ctx, instancePayload(ctx)))
             (this is the OUTBOUND keyDown to the plugin process; separate from action dispatch)
```

Path (A) is where **built-in** Multi/Toggle actions execute. Path (B) is the outbound
`keyDown`/`keyUp`/`dialRotate`/`touchTap` notification to **plugin processes** — already generic.

### Pattern 1: New built-in handler registration (the idiom to copy)

```cpp
// Source: src/app/src/builtin_actions_service.cpp:444 (existing "multiactions")
m_registry.registerAction("com.hotspot.streamdock.multiactions",
    [this](std::string_view settingsJson) {
        auto const obj = parseSettings(settingsJson, "multiactions"sv);
        auto const arr = obj.value(QStringLiteral("actions")).toArray();
        if (m_engine) {
            m_engine->run(decodeActionChain(arr));   // <- decodeActionChain at :182
        }
    });
```

`decodeActionChain` (`builtin_actions_service.cpp:182-196`) already turns a JSON array of action
descriptors into a `core::ActionChain`. **For BIND-04 the children come from
`binding.instance.children` (typed `ActionInstance`), not from a JSON array** — so a
`children → ActionChain` adapter is the new piece (Finding 2).

### Pattern 2: setState repaint (reuse for BIND-07)

```cpp
// Source: src/app/src/plugin_device_bridge.cpp:612-655 (onAction "setState")
m_registry.setState(contextId, newState);                 // tracks state on ActionContext
if (ok && m_stateImageResolver && ctx.controller == "Keypad") {
    QString imgPath = m_stateImageResolver(ctx.actionUUID, newState);  // manifest States[idx].Image
    QImage stateImg(imgPath);
    m_control->assignKeyImage(keyIndex, stateImg);        // paints the LCD
    reapplyTitle(keyIndex);
}
```

This is **plugin-driven** setState today (a plugin process sends `setState` over WS). For BIND-07
the built-in toggle must drive the equivalent **internally**, rendering from
`instance.states[currentState]` (imagePath/title) rather than the plugin manifest. The render
mirror to the on-screen cell (`keyImageAssigned` → `DeviceView.qml:88-104`) is automatic once
`assignKeyImage` runs (per 32-UI-SPEC "Render expectation").

### Anti-Patterns to Avoid

- **Do NOT add a new async runner for Multi Action.** `ActionEngine::runFrom` already defers each
  `delayMs`/`Sleep` step through the injected `QtExecutor` (`action_engine.cpp:114-131`). Building
  a parallel sequencer duplicates the liveness-flag/`scheduleAfter` machinery.
- **Do NOT put toggle state in the `BuiltinHandler`.** `BuiltinHandler = void(string_view settingsJson)` (`builtin_action_registry.hpp:47`) has no binding identity and no Profile handle.
  Mutating `currentState` there is impossible without smuggling identity through `settingsJson`
  (fragile). Resolve the toggle where identity + Profile are both in scope.
- **Do NOT add `nlohmann::json` to any `src/core/include/` header** (COD-031 release-blocker). The
  `children → ActionChain` adapter operates on typed structs — no JSON needed in core. Keep
  `decodeActionChain`-style JSON parsing in the app tier (`builtin_actions_service.cpp` uses Qt
  JSON, which is fine app-side).
- **Do NOT branch on SKU/codename in any dispatch path** (BIND-06). Key on `DeviceEvent::Kind` /
  `controller` string only.

## Don't Hand-Roll

| Problem                                            | Don't Build                    | Use Instead                                                                                                  | Why                                                                                                                            |
| -------------------------------------------------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| Sequential delayed execution of Multi Action steps | A new timer-driven runner      | `ActionEngine::run` / `runFrom` (`action_engine.cpp:48,58`)                                                  | Already handles per-step `delayMs`, Sleep deferral via QtExecutor, and the destroy-during-pending-continuation liveness guard. |
| Painting a key for a state change                  | Direct hidraw / sidecar writes | `StreamDockControlService::assignKeyImage` + the existing setState path (`plugin_device_bridge.cpp:633-648`) | Drives the sidecar AND mirrors to the QML cell via `keyImageAssigned`.                                                         |
| Owner resolution (action UUID → plugin)            | Re-deriving dotted prefixes    | `PluginDeviceBridge::resolveOwner` (stored-owner map, `:851-863`)                                            | Already fixed (519ecd0); falls back to longest-prefix only if the manifest stored-owner map misses.                            |
| willAppear envelope construction                   | Hand-assembling JSON           | `eventEnvelope()` + `instancePayload()` (`plugin_device_bridge.cpp:310-342`)                                 | Already Elgato/OpenDeck-shaped with `action`/`context`/`device` + GenericInstancePayload.                                      |

**Key insight:** ~70% of this phase is already built. The trap is *re-implementing* the willAppear
fix and the device-generic routing because the CONTEXT.md framed them as bugs. They were fixed
2026-06-02. Verify with the debug channel; do not rewrite.

## Detailed Findings (the planner's surgical map)

### Finding 1 — BIND-03: both "known bugs" are ALREADY FIXED. Scope = verify + regression test.

**Hypothesis A: "willAppear payload lacks the `action` field."** → **REFUTED.**
`eventEnvelope` (`plugin_device_bridge.cpp:333-342`) emits:

```cpp
return QJsonObject{
    {"event", event},
    {"action", ctx.actionUUID},          // <-- line 337: action IS present
    {"context", ContextRegistry::deriveContextId(ctx)},
    {"device", ctx.deviceId},
    {"payload", payload},
};
```

All three willAppear emission sites (keys `:1081`, encoders `:1124`, touch `:1173`) call
`eventEnvelope("willAppear", ctx, instancePayload(ctx))` — so `action` is on every willAppear.
Fixed in commit `ddabc16`.

**Hypothesis B: "owner-match needs a root-UUID dotted prefix → silent no-willAppear."** →
**REFUTED for the production path.** `resolveOwner` (`:851-863`) consults the injected
`m_actionOwnerResolver` (the stored-owner map) **first**:

```cpp
if (m_actionOwnerResolver) {
    QString owner = m_actionOwnerResolver(actionUuid);   // PluginManager::ownerForAction
    if (!owner.isEmpty()) return owner;                  // stored owner from manifest
}
return ownerForActionUuid(actionUuid, m_registeredPlugins);  // legacy longest-prefix fallback
```

The resolver is wired at `application.cpp:524-526` to `PluginManager::ownerForAction`. Fixed in
commit `519ecd0`. The legacy dotted-prefix path (`ownerForActionUuid`, `:276-299`) remains only as
a fallback for Elgato-style UUIDs.

**The wire that does the work:** `application.cpp:635-643` connects
`ProfileController::profileChanged → populateContextsForActivePage(activeDeviceId())`, guarded by
the non-empty-device check (T-28-09). `commitKeyBinding` emits `profileChanged`
(`profile_controller.cpp:456`), so a drag-drop commit → `profileChanged` → repopulate contexts →
willAppear, synchronously, on the GUI thread.

**Residual risk to verify (this is the real BIND-03 task):**

- `populateContextsForActivePage` only enumerates `binding.onPress` actions with
  `kind == ActionKind::Plugin` (`:1034-1035`). A drag-drop that commits a **built-in** Multi/Toggle
  action (`kind == Plugin`, id `com.hotspot.streamdock.*`) WILL be enumerated and a context
  registered — but built-ins have **no plugin process** to receive willAppear, so `resolveOwner`
  returns empty (`:1044`) and it's skipped. That is correct behavior, not a bug. **Confirm the
  drag-drop target for BIND-03 verification is a real plugin action (e.g. System Monitor), not a
  built-in.**
- `populateContextsForActivePage` hardcodes `kDefaultKeyCols = 5` (`:1019`) — fine for AKP05E,
  wrong coordinates for other SKU column counts. Out of scope unless a non-5-column SKU is used in
  verification; flag as a known limitation.

**Action for planner:** One verify-live task (drag System Monitor onto a key via `qml.drag` →
`plugin.protocolLog` shows `willAppear` within the same interaction, no reconnect) + one Catch2
regression test asserting `eventEnvelope` includes `action` and that a stored-owner action
resolves (the bridge test fixture at `tests/unit/test_plugin_device_bridge.cpp` already exercises
this path).

### Finding 2 — BIND-04/05: Multi Action via an `ActionInstance.children → ActionChain` adapter.

**The gap:** the existing `multiactions` built-in (`builtin_actions_service.cpp:444-451`) decodes a
flat JSON `{"actions":[...]}` array. The Phase 31 model stores children as
`std::vector<ActionInstance>` on `binding.instance.children` (`action_instance.hpp:98`). **There is
no `ActionInstance → Action`/`ActionChain` adapter anywhere** (verified by grep; `action_engine`
only `run`s an `ActionChain`).

**The seam to build:** a pure-core function, e.g.

```cpp
// proposed: core, next to action_engine — nlohmann-free, Qt-free (COD-031 safe)
ActionChain instanceChildrenToChain(ActionInstance const& parent);
```

that flattens each `child` into an `Action{kind, id, settingsJson, label, delayMs}`. Mapping notes:

- `child.id` (dotted) → `Action.id`; `Action.kind = ActionKind::Plugin` for plugin children (the
  engine's `plugin` executor then routes them through the registry/bridge as today).
- `child.settings` (escaped-JSON string, `action_instance.hpp:93`) → `Action.settingsJson`.
- **Inter-step delay (BIND-04 requirement):** `Action` carries `delayMs` (`profile.hpp:77`) and
  `ActionEngine::runFrom` already defers it (`action_engine.cpp:114-131`). But `ActionInstance` has
  **no `delayMs` field** — the delay must come from somewhere. Options for the planner:
  (a) read a `delayMs`/`Delay` key out of `child.settings`, or (b) interleave explicit
  `Action{kind=Sleep, delayMs=N}` steps between children. Either is fine; document the chosen
  source. (OpenDeck's Multi Action uses an explicit "Delay" sub-action — option (b) is the closest
  parity.)
- **Nested Multi Action:** `children` is recursive. The reader already guards recursion depth
  (commit `61bb6c6`, CR-01). The adapter should flatten depth-first or emit a nested
  `multiaction` step; flatten is simpler and matches a single `ActionChain`.

**Where it runs:** register `opendeck.multiaction` (id string per CLAUDE's discretion; suggest
`com.hotspot.streamdock.multiaction` for prefix consistency with the registry's `kBuiltinPrefix`
guard at `builtin_action_registry.cpp:11`) whose handler resolves the firing binding's
`instance.children`, calls the adapter, and `m_engine->run(chain)`. **But note the same identity
problem as toggle:** the registry handler only gets `settingsJson`, not the binding. The cleanest
path is to resolve Multi Action in `StreamDockInputService::dispatch` (which has `binding.instance`
in hand) — see Finding 3's recommendation; the registry handler can stay as a thin fallback that
decodes children embedded in `settingsJson`.

**Tests:** Catch2 over the pure adapter (children → chain ordering + delay placement) +
`test_action_engine`-style assertion that delays defer via a fake executor. Live: bind a 2-child
Multi Action, `input.key`, confirm `plugin.protocolLog` shows both children fire in order with the
gap.

### Finding 3 — BIND-07: Toggle Action — the central new design seam.

**The mutation problem:** Toggle must, per press: `currentState = (currentState + 1) % N`, render
`states[currentState]`, emit a state-change `willAppear`. `currentState` lives on the **in-memory
mutable** `binding.instance` inside the active Profile (`profile.hpp:103`,
`action_instance.hpp:88`). The Profile is owned by `ProfileController`
(`activeProfile()` returns `const&` at `profile_controller.cpp:364`; mutators emit `profileChanged`).

The built-in registry **cannot** do this: `BuiltinHandler` is `void(string_view settingsJson)` —
no binding identity, no Profile handle (`builtin_action_registry.hpp:47`).

**Recommended seam (resolve in `StreamDockInputService::dispatch`):** the press handler at
`stream_dock_input_service.cpp:205-207` already has `it->second` = the firing `Binding` (with its
`instance`) and the key index. Add a pre-check there: if `binding.instance` is a toggle
(N states > 1, or id == toggle id), then:

1. Mutate `currentState` (needs a non-const path — either a `ProfileController` mutator like
   `ProfileController::cycleInstanceState(controller, index)` that mutates + emits `profileChanged`,
   or a direct mutable-profile accessor on the input service).
1. Drive the per-state render: call into the bridge/control to paint `states[currentState].visual`
   (imagePath/title) via `assignKeyImage` — mirror the existing setState repaint at
   `plugin_device_bridge.cpp:633-648` but sourced from `instance.states[idx]` not the plugin
   manifest.
1. Emit the state-change willAppear: re-run the per-context willAppear with the new `stateIndex`
   (the bridge already preserves `stateIndex` across `registerContext` and reflects it in the
   serialized payload, `:1073-1077`). The simplest correct emission is to update the
   `ContextRegistry` state (`m_registry.setState`, `:162`) then re-send `willAppear` for that one
   context.

**Why not the registry handler:** to do it there you would have to encode the binding identity
(controller+index) into `settingsJson` and give `BuiltinActionsService` a mutable Profile handle —
both are larger, leakier changes than resolving at the one site that already owns the identity.
The CONTEXT.md "handlers live in builtin_action_registry.cpp" decision is satisfiable by
registering a `opendeck.toggleaction` entry that the input service consults for *classification*
(is-this-a-toggle), while the *state mutation* happens at the dispatch site. **Surface this
trade-off to the planner — it is the one place the locked decision and the code reality need a
reconciliation.**

**State persistence:** `currentState` round-trips through `profileToJson`/`profileFromJson`
(verified: `test_action_instance.cpp:88` round-trips currentState). If the toggle mutation should
persist across restarts, route it through a `ProfileController` mutator that saves; if it should be
session-only, mutate the in-memory profile only. CONTEXT.md doesn't specify — **flag as an open
question** (likely session-only for parity with Elgato, which resets state on profile reload).

**Tests:** Catch2 over the cycle math (mod N, N>2) on a pure helper. Live: bind a 3-state toggle,
`input.key` three times, `screenshot` after each press to confirm the cell face changes through all
three states and wraps; `plugin.protocolLog` shows a willAppear with incrementing
`payload.state` (if a plugin owns the action) — for a built-in toggle with no plugin, verify the
render mirror only.

### Finding 4 — BIND-06: device-generic dispatch is ALREADY in place. Scope = verify + test.

**Outbound (`PluginDeviceBridge::onDeviceEvent`, `:865-1004`):** a single `switch (ev.kind)` with
cases `KeyPressed/KeyReleased` → `byCoord(deviceId, "Keypad", r, c)` (`:888`),
`EncoderTurned/Pressed/Released` → `byCoord(deviceId, "Encoder", 0, encIdx)` (`:910, :928, :948`),
`TouchUp` → `byCoord(deviceId, "Encoder", 0, zone)` (`:985`). **No SKU/codename branch anywhere.**
The control type is the `controller` string ("Keypad"/"Encoder"); coordinates come from the event.

**Inbound action dispatch (`StreamDockInputService::dispatch`, `:195-…`):** switches on
`DeviceEvent::Kind`, indexes `prof.keys` / `prof.encoders` / touch-zone maps — also SKU-agnostic.

**Caveat (a touch-zone convention, not SKU code):** touch zones are registered AND looked up under
`controller="Encoder", row=0, column=zoneIndex` (locked convention, `:1131-1135` and `:985`). This
is a type convention, not SKU branching — but the planner's BIND-06 verification must exercise all
three controller types (key, encoder-dial, touch-zone) via `input.key` / `input.encoder` /
`input.touch` and confirm each routes to its registered context without reconnect, on the
sidecar-backed family (AKP03/AKP05-N4/AKP153 share the same generic path).

**Tests:** parametrized Catch2 over the bridge fixture asserting key/encoder/touch each resolve a
pre-registered context; live verify via the three synthetic RPCs.

### Finding 5 — EDIT-01: ScrollView wrapping (exact nodes).

**Current structure** (`DeviceView.qml:302-498`):

```
Item#chassisArea  (anchors.fill: parent)              [:302-304]
├─ Image#chassisPhoto   (anchors.fill, margins spacingMd, visible: _hasPhoto)  [:310-327]
├─ DeviceCanvas#deviceCanvas (anchors.fill, margins: Theme.spacingMd,
│                             visible: !chassisPhoto.visible)                   [:337-341]
├─ RoundButton#trashBtn (top-right overlay)                                     [:426-…]
└─ EmptyState           (centered overlay)                                      [:498-…]
```

**Target** (per 32-UI-SPEC): insert a `ScrollView#deviceCanvasScroll` (anchors.fill chassisArea,
margins `Theme.spacingMd`) as a **new sibling** of `chassisPhoto`/`trashBtn`/`EmptyState`, and move
`DeviceCanvas#deviceCanvas` to be the ScrollView's content child. `QtQuick.Controls` is already
imported (`:40`) — no new import.

Content sizing (vertical-only): set `deviceCanvas.width: deviceCanvasScroll.availableWidth` so Qt
never produces a horizontal scrollbar; let height be natural content height. Set
`ScrollBar.horizontal.policy: ScrollBar.AlwaysOff`, `ScrollBar.vertical.policy: ScrollBar.AsNeeded`,
`clip: true`.

**Debug-addressability (DOD):** `objectName: "deviceCanvasScroll"`; also give the underlying
Flickable `objectName: "deviceCanvasFlick"` so `qml.get deviceCanvasFlick` exposes `contentY` +
`contentHeight` (the SPEC notes `ScrollBar.vertical.position` may not be reachable through `qml.get`
on the ScrollView in this harness — expose at least one machine-readable scroll surface).

**Overflow predicate (optional helper):** the SPEC suggests a readonly
`_gridOverflows` on `DeviceView` computed from `keyRowsResolved` (`:69`), `_keyColumnsResolved`
(`:66`), `encoderCount`, `touchZoneCount`: `keyColumns > 8 || (keyRows + (enc>0?1:0) + (touch>0?1:0)) > 4`. Whether the bar actually paints is ultimately content-height-vs-viewport
(`AsNeeded`); the geometry rule is the acceptance predicate.

**Keep delegates untouched:** do not redeclare `index` on KeyCell/dial delegates (existing trap,
`DeviceCanvas.qml:108-109`); the wrapper touches none of them.

## Runtime State Inventory

> This phase is code/UI only — no rename, no migration, no stored-string change. The only runtime
> state touched is the **in-memory** `ActionInstance.currentState` (toggle), which is a deliberate
> new write, not a migration.

| Category            | Items Found                                                                                                       | Action Required                                                                          |
| ------------------- | ----------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| Stored data         | `currentState` round-trips in profile JSON (`profileToJson`). Toggle mutation may write it back.                  | Decide session-only vs persisted (open question Q1) — code edit only, no data migration. |
| Live service config | None — verified: no external service stores Phase-32 state.                                                       | None.                                                                                    |
| OS-registered state | None.                                                                                                             | None.                                                                                    |
| Secrets/env vars    | None new. `AJAZZ_DEBUG_CONTROL=1` / `AJAZZ_ALLOW_UNTRUSTED_PLUGINS=1` used only for live verification, unchanged. | None.                                                                                    |
| Build artifacts     | None — no package rename.                                                                                         | None.                                                                                    |

## Common Pitfalls

### Pitfall 1: Re-fixing already-fixed BIND-03 bugs

**What goes wrong:** Treating "willAppear lacks action" / "owner needs prefix" as live bugs and
rewriting `eventEnvelope` / `resolveOwner`.
**Why:** CONTEXT.md framed them as bugs to fix; they were fixed 2026-06-02.
**Avoid:** Read `plugin_device_bridge.cpp:337` and `:851-863` first. Scope BIND-03 to verify+test.

### Pitfall 2: Demo unit delivers ZERO real input

**What goes wrong:** Waiting for real key/encoder/touch from the `0x0300:0x3004` demo unit.
**Why:** Input is unreachable on that unit (CLAUDE.md glossary, proven five ways).
**Avoid:** Use synthetic `scripts/ajazz-debug input.key/encoder/encoderPress/touch` — they route
through the full pipeline (`debug_control_facade.cpp:303-346` → `PluginDebugService` →
`dispatch`/`onDeviceEvent`). This is the documented autonomous-input-test method.

### Pitfall 3: IN-02 ordering invariant (repaint before context registration)

**What goes wrong:** Reordering the `profileChanged` connections so context registration fires
before repaint, breaking the carousel index reset.
**Why:** `application.cpp` deliberately registers `repaintFromProfile` (`:456-459`) and
`repaintEncodersFromProfile` (`:475-478`) **before** `populateContextsForActivePage` (`:635-643`).
Qt delivers same-thread direct connections in registration order; repaint must run first
(`repaintFromProfile` resets `m_carouselIndex = 0`).
**Avoid:** If the toggle render is wired via a new `profileChanged` connection, register it in the
correct order relative to these. Prefer driving the toggle repaint directly (not via a new
`profileChanged` slot) to avoid touching this ordering.

### Pitfall 4: Toggle state mutation through the opaque registry handler

**What goes wrong:** Trying to mutate `currentState` inside a `BuiltinHandler` that only sees
`settingsJson`.
**Avoid:** Resolve toggle at `StreamDockInputService::dispatch` where the firing `Binding` (and its
`instance`) and key index are both in scope (Finding 3).

### Pitfall 5: QML ScrollView producing a phantom horizontal scrollbar

**What goes wrong:** Content item wider than the viewport → Qt auto-creates a horizontal bar.
**Why:** Default ScrollView sizes to content.
**Avoid:** Constrain `deviceCanvas.width = deviceCanvasScroll.availableWidth` AND set
`ScrollBar.horizontal.policy: ScrollBar.AlwaysOff` (32-UI-SPEC). Also `clip: true` so overflow
doesn't paint over the Inspector below.

### Pitfall 6: Un-named scroll container is invisible to the debug channel

**What goes wrong:** No `objectName` → `qml.get` can't find it → acceptance criterion 1 fails.
**Avoid:** `objectName: "deviceCanvasScroll"` (+ `"deviceCanvasFlick"` on the Flickable). CLAUDE.md
hard rule: every new interactive control must be debug-addressable.

### Pitfall 7: ASCII-only test names

**What goes wrong:** em-dash/right-arrow in Catch2 TEST_CASE names mangle under the Win32 CMD
codepage and the filter stops matching.
**Avoid:** Use `-` and `->` only (CLAUDE.md; `test_action_instance.cpp:20-21` already follows this).

## State of the Art

| Old Approach                                         | Current Approach                                                          | When Changed                  | Impact                                                                                        |
| ---------------------------------------------------- | ------------------------------------------------------------------------- | ----------------------------- | --------------------------------------------------------------------------------------------- |
| willAppear owner via dotted-prefix only              | Stored-owner map (`PluginManager::ownerForAction`) first, prefix fallback | commit `519ecd0` (2026-06-02) | Action UUID need not be a dotted child of the plugin UUID — BIND-03 owner bug already gone.   |
| willAppear payload had no `action`                   | Full Elgato envelope with top-level `action`/`context`/`device`           | commit `ddabc16` (2026-06-02) | Multi-action plugins self-identify the firing action — BIND-03 action-field bug already gone. |
| `multiactions` decodes flat JSON `{"actions":[...]}` | (this phase) `ActionInstance.children` model-driven Multi Action          | Phase 32                      | New adapter; the flat-JSON handler can remain as a fallback.                                  |
| `ActionInstance` model-only (Phase 31)               | (this phase) dispatch wired (Multi/Toggle)                                | Phase 32                      | Phase 31 explicitly deferred dispatch to Phase 32 (`action_instance.hpp:73`).                 |

## Validation Architecture

### Test Framework

| Property           | Value                                                                                                                                                                                          |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Framework          | Catch2 (v3, `Catch2::Catch2WithMain`, `tests/unit/CMakeLists.txt:430`) for core/app units; QML offscreen harness (`tests/qml/`) for editor smoke; `scripts/ajazz-debug` for live wiring/render |
| Config file        | CMake presets; `catch_discover_tests(ajazz_unit_tests)` at `tests/unit/CMakeLists.txt:683`                                                                                                     |
| Quick run command  | \`ctest --preset linux-release -R "action                                                                                                                                                      |
| Full suite command | `ctest --preset linux-release` (~408 cases; trust the live count)                                                                                                                              |

### Phase Requirements → Test Map

| Req ID     | Behavior                                                                                                               | Test Type   | Automated Command                                                                                                                                                                                                                                                  | File Exists?                                                                                                 |
| ---------- | ---------------------------------------------------------------------------------------------------------------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------ |
| BIND-03    | Envelope carries `action`; stored-owner resolves; drag-drop fires willAppear same interaction                          | unit + live | `ctest --preset linux-release -R plugin_device --output-on-failure` ; then live: launch `AJAZZ_DEBUG_CONTROL=1 AJAZZ_ALLOW_UNTRUSTED_PLUGINS=1` → install System Monitor → `qml.drag libraryTile_0 key_4` → `plugin.protocolLog` shows `willAppear` (no reconnect) | ✅ `test_plugin_device_bridge.cpp` (extend)                                                                  |
| BIND-04/05 | children→chain order + inter-step delay                                                                                | unit + live | \`ctest --preset linux-release -R "action_engine                                                                                                                                                                                                                   | action_instance                                                                                              |
| BIND-06    | key/encoder/touch each route to registered context, no SKU branch                                                      | unit + live | `ctest --preset linux-release -R plugin_device` ; live: `input.key`/`input.encoder`/`input.touch` → `plugin.protocolLog` shows keyDown/dialRotate/touchTap to the right context                                                                                    | ✅ `test_plugin_device_bridge.cpp` (extend, parametrize controller type)                                     |
| BIND-07    | currentState cycles mod N (N>2); per-state visual renders; state-change willAppear                                     | unit + live | \`ctest --preset linux-release -R "toggle                                                                                                                                                                                                                          | action_instance"`; live: bind 3-state toggle →`input.key`×3 →`screenshot\` after each (face changes + wraps) |
| EDIT-01    | scroll container addressable; over/under threshold; no horizontal; position readable; delegates distinct; trash pinned | qml + live  | `ctest --preset linux-release -R device_view` (Linux/macOS; not Windows per CLAUDE.md `if(NOT WIN32)`) ; live: `device.setActiveDevice` over/under threshold → `qml.get deviceCanvasScroll` (`ScrollBar.vertical.size < 1.0` ⇔ scrollable) + `screenshot`          | ✅ `tests/qml/test_device_view_geometry.qml` (extend)                                                        |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R "<the touched suite>" --output-on-failure`
- **Per wave merge:** `ctest --preset linux-release` (full)
- **Phase gate:** Full suite green **AND** the live debug-channel acceptance criteria in
  32-UI-SPEC + the willAppear/toggle/Multi live checks pass (ctest is necessary but NOT sufficient
  — CLAUDE.md MANDATORY debug-channel rule).

### Wave 0 Gaps

- [ ] `tests/unit/test_multiaction_dispatch.cpp` (or extend `test_builtin_actions.cpp`) — covers
  BIND-04/05: the `ActionInstance.children → ActionChain` adapter (order + delay placement) with
  a fake/recording executor.
- [ ] `tests/unit/test_toggle_dispatch.cpp` (or extend `test_action_instance.cpp` +
  `test_plugin_device_bridge.cpp`) — covers BIND-07: cycle-mod-N for N>2 and the setState render
  seam from `instance.states[idx]`.
- [ ] Extend `tests/qml/test_device_view_geometry.qml` for EDIT-01 over/under-threshold +
  `ScrollBar.vertical.size`/`contentY` readability + trash-pinned assertions.
- [ ] No framework install needed — Catch2 + QML harness already wired.

## Security Domain

> `security_enforcement` config: not located in `.planning/config.json` for this query; treating as
> enabled per the default. This phase introduces no auth/session/crypto surfaces — it is in-process
> dispatch + a UI container.

### Applicable ASVS Categories

| ASVS Category         | Applies       | Standard Control                                                                                                                                                                                                                                                                 |
| --------------------- | ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | no            | —                                                                                                                                                                                                                                                                                |
| V3 Session Management | no            | —                                                                                                                                                                                                                                                                                |
| V4 Access Control     | yes (lightly) | Cross-plugin denial already enforced: `onAction` rejects a plugin driving a context it does not own (`plugin_device_bridge.cpp:573-575`, T-19-xplugin). Toggle/Multi must not let a built-in drive a plugin-owned context it doesn't own.                                        |
| V5 Input Validation   | yes           | `settingsJson` is parsed defensively (`parseSettings`, empty/invalid → `{}`); `decodeActionChain` tolerates missing keys. New adapter must clamp `currentState` (model already clamps on read, `action_instance.hpp:88`) and honor the recursion-depth guard (commit `61bb6c6`). |
| V6 Cryptography       | no            | —                                                                                                                                                                                                                                                                                |

### Known Threat Patterns for in-process dispatch

| Pattern                                                   | STRIDE                | Standard Mitigation                                                                                                                                                                                                                                                                     |
| --------------------------------------------------------- | --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Unbounded Multi Action recursion (nested children)        | Denial of Service     | Reuse the existing reader depth guard (CR-01, `61bb6c6`); the adapter must not re-introduce unbounded recursion — flatten with a depth cap.                                                                                                                                             |
| `runCommand`/`openUrl` reachable via a Multi Action child | Elevation / Tampering | Children route through the SAME `ActionEngine` executors as today — `openUrl` already scheme-validates (http/https only, `application.cpp:321-332`), `runCommand` uses explicit argv (no shell, `:307-308`). No new surface if the adapter only sets `Action.kind`/`id`/`settingsJson`. |
| Cross-plugin context drive via toggle                     | Tampering             | Keep the `ctx.pluginUuid == pluginUuid` ownership check on any willAppear/setState the toggle triggers (`:573-575`).                                                                                                                                                                    |

## Assumptions Log

| #   | Claim                                                                                                                                        | Section         | Risk if Wrong                                                                                                                                                                                                                                                                                                                                        |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------- | --------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | Inter-step delay for Multi Action children comes from `child.settings` or interleaved Sleep steps (ActionInstance has no `delayMs` field)    | Finding 2       | If a `delayMs` field is expected on the model, the adapter source is wrong — but `action_instance.hpp:75-99` confirms no such field today. Discuss the delay source with the user.                                                                                                                                                                   |
| A2  | Toggle `currentState` mutation is session-only (resets on profile reload), matching Elgato                                                   | Finding 3, Q1   | If persistence is required, the mutation must route through a saving `ProfileController` mutator.                                                                                                                                                                                                                                                    |
| A3  | The built-in id strings should use the `com.hotspot.streamdock.` prefix (registry's `kBuiltinPrefix` guard) rather than literal `opendeck.*` | Findings 2/3    | CONTEXT.md says `opendeck.multiaction`/`opendeck.toggleaction`; the registry `handles()` only matches the `com.hotspot.streamdock.` prefix (`builtin_action_registry.cpp:11`). If the literal `opendeck.*` ids are used, `handles()` returns false and dispatch never fires. **Reconcile the id namespace with the user (this is a real conflict).** |
| A4  | `security_enforcement` is enabled (config key not found this session)                                                                        | Security Domain | If disabled, the Security section is informational only.                                                                                                                                                                                                                                                                                             |

## Open Questions (RESOLVED)

> All four resolved post-discuss and locked in 32-CONTEXT.md "Research-resolved decisions":
> Q1 = **PERSIST** to profile JSON (user decision); Q2 = `com.hotspot.streamdock.*` prefix;
> Q3 = **add optional `delayMs` to ActionInstance** (user decision); Q4 = Toggle stateful mutation
> at the `StreamDockInputService::dispatch` seam (registry handler is opaque).
> Also: **BIND-05 is the Toggle Action requirement** (REQUIREMENTS.md:28) — the row at line 55
> labelling it "Multi family" is loose; BIND-05's Toggle semantics are delivered by plan 32-03.

1. **Toggle state persistence (Q1) — RESOLVED: persist to profile JSON.** Should a toggle's `currentState` persist across app restarts
   (write back to profile JSON) or reset on profile reload (Elgato parity)?

   - What we know: `currentState` round-trips through `profileToJson`/`profileFromJson`.
   - What's unclear: CONTEXT.md doesn't specify.
   - Recommendation: session-only (parity) unless the user wants persistence; plan the mutator
     accordingly.

1. **Built-in id namespace (Q2):** CONTEXT.md names `opendeck.multiaction`/`opendeck.toggleaction`,
   but the registry only dispatches `com.hotspot.streamdock.*` (`builtin_action_registry.cpp:11`).

   - Recommendation: register under `com.hotspot.streamdock.multiaction` /
     `…toggleaction` (or relax the prefix guard) — confirm with the user. **This is load-bearing:
     the wrong namespace means the handler never fires.**

1. **Multi Action delay source (Q3):** Where does each child's inter-step delay come from, given
   `ActionInstance` has no `delayMs`? (settings key vs interleaved Sleep). See A1.

1. **Toggle handler location vs CONTEXT.md (Q4):** CONTEXT.md locks "handlers live in
   builtin_action_registry.cpp", but toggle state mutation cannot happen in an opaque
   `BuiltinHandler` (Finding 3). Recommendation: register a classification entry in the registry,
   perform mutation at `StreamDockInputService::dispatch`. Confirm this reconciliation is acceptable.

## Sources

### Primary (HIGH confidence — read directly this session)

- `src/core/include/ajazz/core/action_instance.hpp` — Phase 31 model (no `delayMs`; recursive
  children; clamp-on-read).
- `src/core/include/ajazz/core/action_engine.hpp` + `src/core/src/action_engine.cpp` — `run`/
  `runFrom` chain walk, `delayMs`/Sleep deferral via injected Executor, liveness guard.
- `src/core/include/ajazz/core/builtin_action_registry.hpp` + `.cpp` — dispatch table; `kBuiltinPrefix`
  guard; `BuiltinHandler = void(string_view)`.
- `src/core/include/ajazz/core/profile.hpp` — `Action`, `Binding`/`EncoderBinding` (+optional
  `instance`), `ActionKind`.
- `src/app/src/plugin_device_bridge.cpp` — `eventEnvelope` (`:337` action field), `instancePayload`,
  `resolveOwner`/`ownerForActionUuid`, `populateContextsForActivePage` (`:1010`), `onDeviceEvent`
  (`:865`, device-generic), setState repaint (`:612-655`), cross-plugin guard (`:573`).
- `src/app/src/application.cpp` — engine executors (`:263-350`), `execs.plugin → onPluginAction`
  (`:341`), owner resolver wire (`:524-526`), repaint/profileChanged ordering (`:456-487`),
  willAppear wire (`:635-643`).
- `src/app/src/builtin_actions_service.cpp` — `onPluginAction` (`:218`), `populate()` registration
  idiom, `decodeActionChain` (`:182`), existing `multiactions` handler (`:444`).
- `src/app/src/stream_dock_input_service.cpp` — `dispatch()` (`:195`), `binding.onPress → engine.run` (`:206`).
- `src/app/src/debug_control_facade.cpp` — `input.key/encoder/touch` RPCs (`:303-346`), routing to
  `PluginDebugService` → full pipeline.
- `src/app/qml/DeviceView.qml` — `chassisArea`/`chassisPhoto`/`deviceCanvas`/`trashBtn`/`EmptyState`
  structure (`:302-498`), `QtQuick.Controls` import (`:40`), `keyRowsResolved`/`_keyColumnsResolved`.
- `32-CONTEXT.md`, `32-UI-SPEC.md` — locked decisions + the editor scroll design contract.
- Auto-memory `project_plugin_install_demo_working` — original bug report + the two fix commits
  (`ddabc16`, `519ecd0`).
- `git log --all --grep` — confirmed Phase 31 landed model-only; no `ActionInstance → ActionChain`
  adapter exists.

### Secondary (MEDIUM)

- CLAUDE.md project memory — debug-channel verification rule, COD-031, ASCII test names, ScrollView
  gotchas, demo-unit-zero-input.

## Metadata

**Confidence breakdown:**

- Standard stack / no-new-deps: HIGH — read every relevant file; zero external packages.
- BIND-03 already-fixed claim: HIGH — refuted both hypotheses against current source with line
  anchors + fix commits.
- BIND-06 device-generic claim: HIGH — read the full `onDeviceEvent` switch and `dispatch`.
- Multi/Toggle integration seam: HIGH on the gap (no adapter exists; handler signature is opaque);
  MEDIUM on the exact recommended seam (the toggle-in-input-service approach reconciles a locked
  decision — see Q4).
- EDIT-01: HIGH — exact nodes + the SPEC contract.

**Research date:** 2026-06-08
**Valid until:** 2026-06-22 (stable in-tree code; re-grep the cited line anchors if other
`experiment/mirajazz` work lands first, since this branch sees heavy ad-hoc commits).
