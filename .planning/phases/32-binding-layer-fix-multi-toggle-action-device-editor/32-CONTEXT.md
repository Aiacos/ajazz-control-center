# Phase 32: Binding Layer Fix + Multi/Toggle Action + Device Editor - Context

**Gathered:** 2026-06-08
**Status:** Ready for planning

<domain>
## Phase Boundary

Make the binding layer actually fire plugins: a drag-to-bind commit must fire the plugin
`willAppear` immediately (no device reconnect); Multi Action and Toggle Action must dispatch
correctly (sequential children with inter-step delay; cycle `currentState` + render per-state
images); the binding/action layer must be device-generic across key / encoder-dial / touch-zone on
all sidecar-backed Stream Dock SKUs (AKP03 / AKP05-N4 / AKP153) with NO SKU-specific dispatch code;
and the device-view editor must scroll for SKU grids exceeding OpenDeck's overflow rules.

Builds directly on the Phase 31 ActionInstance model (`instance.states[]` / `currentState` /
`children`). Satisfies BIND-03, BIND-04, BIND-05, BIND-06, BIND-07, EDIT-01.

This phase does NOT implement the Property Inspector settings round-trip (Phase 33) or per-app
profile switching (Phase 34).

</domain>

<decisions>
## Implementation Decisions

### Multi Action & Toggle Action dispatch (BIND-04, BIND-05, BIND-07)

- **Built-in handlers live in `builtin_action_registry.cpp`** — register `opendeck.multiaction`
  and `opendeck.toggleaction` alongside the existing built-ins (Sleep/KeyPress/RunCommand/etc.),
  dispatched through the established ActionKind/built-in mechanism.
- **Multi Action reuses the ActionEngine chain walker** — sequence `instance.children`
  (ActionInstances) through the existing sequential walk, honoring each child's inter-step
  `delayMs` (the existing `scheduleAfter`/`sleep` mechanic in action_engine.cpp). No new async
  runner.
- **Toggle Action cycles `currentState` through ALL `states[]` (mod N)** on each press; renders
  `states[currentState]` via `setState`; emits the state-change `willAppear`. Not hard-limited to
  2 states.
- **BIND-07 setState renders from `instance.states[currentState]`** (imagePath/title) via the
  existing repaint path; per-state settings encode + round-trip the state index.

### BIND-03 drag-to-bind willAppear (wire already exists) + device-generic (BIND-06)

- **Scope = close-the-gap + verify, NOT rewire.** The `profileChanged → populateContextsForActivePage` connection ALREADY EXISTS (`src/app/src/application.cpp:625-643`,
  PLUGIN-19, with the T-28-09 empty-activeDeviceId guard). The task is to confirm a drag-drop
  commit emits `profileChanged` synchronously and that `willAppear` reaches the plugin process in
  the same QML interaction — then fix whatever still blocks it.
- **Fix the two known `willAppear` bugs from prior live debugging** (recorded in auto-memory
  `project_plugin_install_demo_working`): (1) the `willAppear` payload lacks the `action` field;
  (2) owner-match requires a root-UUID prefix, so a mismatch yields a silent no-`willAppear`.
  These are the actual reasons binding silently no-ops; both are in scope.
- **Verify live via the debug-channel** — `scripts/ajazz-debug plugin.protocolLog` must show
  `willAppear` arriving within the same interaction (no disconnect/reconnect); Toggle state changes
  confirmed via `screenshot`; Multi Action sequencing via `protocolLog`. ctest green is necessary
  but NOT sufficient (CLAUDE.md debug-channel rule).
- **One generic dispatch path for key / encoder-dial / touch-zone**, keyed on control TYPE not
  SKU — verify `input.encoder` and `input.touch` route to their registered plugin contexts without
  a reconnect on AKP03 / AKP05-N4 / AKP153. No SKU-specific binding code (BIND-06).

### Device editor scroll (EDIT-01)

- **Wrap the device grid in a vertical `ScrollView`** with an `objectName`. Overflow rule matches
  OpenDeck: scroll engages when the grid exceeds 8 columns OR 4 rows.
- **The 4-row overflow threshold counts the encoder row and the touch-zone row** (per EDIT-01
  wording), not just key rows.
- **Key / encoder-dial / touch-zone controllers stay visually AND functionally distinct** (existing
  delegate types; no merging into a unified cell).
- **The scroll container's `objectName` exposes scroll-position properties** so
  `scripts/ajazz-debug qml.get` can read scroll position (success criterion 5; every new
  interactive control must be debug-addressable per CLAUDE.md).

### Claude's Discretion

- Exact built-in id strings, internal handler signatures, ScrollView styling, and how children
  ActionInstances are adapted into the ActionEngine's ActionChain are at the executor's discretion,
  consistent with existing patterns.

</decisions>

\<code_context>

## Existing Code Insights

### Reusable Assets

- `src/app/src/application.cpp:625-643` — the `profileChanged → populateContextsForActivePage`
  wire ALREADY EXISTS (PLUGIN-19; T-28-09 empty-device guard). BIND-03 closes the gap, not rewires.
- `src/app/src/plugin_device_bridge.cpp:1010` — `populateContextsForActivePage()` impl; the
  willAppear emission path lives near here. The `action`-field-missing + root-UUID owner-match bugs
  are in this dispatch path.
- `src/core/src/action_engine.cpp` — `ActionEngine::run`/`runFrom` already walk an ActionChain
  sequentially honoring per-step `delayMs` (sleep + `scheduleAfter`). Reuse for Multi Action.
- `src/core/src/builtin_action_registry.cpp` — home for the new `opendeck.multiaction` /
  `opendeck.toggleaction` built-ins.
- `src/core/include/ajazz/core/action_instance.hpp` — the Phase 31 model: `ActionInstance`
  (states[]/currentState/settings/children) + `ActionState`/`KeyState` (KeyState was RELOCATED here
  in Phase 31 — include this header for KeyState).
- `src/app/qml/DeviceView.qml` — the device-view editor to wrap in a ScrollView.

### Established Patterns

- Built-in actions dispatched via `ActionKind`; plugin actions via `ActionKind::Plugin` → plugin host.
- Debug-channel addressing by `objectName` (`findByName`); every new interactive control MUST set
  `objectName` to be drivable from `scripts/ajazz-debug`.
- Live debug-channel verification is mandatory (build → `AJAZZ_DEBUG_CONTROL=1` → drive → screenshot
  → read) — unit tests miss wiring bugs.

### Integration Points

- builtin_action_registry (new handlers) → action_engine (chain walk) → plugin host (willAppear/setState).
- ProfileController::profileChanged → PluginDeviceBridge::populateContextsForActivePage (exists).
- DeviceView.qml grid → ScrollView wrapper (new) with debug-addressable objectName.

\</code_context>

<specifics>
## Specific Ideas

- OpenDeck parity is the north star for Multi/Toggle semantics and the editor overflow rules
  (reference: mirajazz / opendeck-akp05).
- The known willAppear bugs are documented in auto-memory `project_plugin_install_demo_working`
  (willAppear lacks `action`; owner-match needs root UUID prefix). Cross-check there during planning.
- Demo unit (`0x0300:0x3004`) delivers zero real input — Multi/Toggle/encoder/touch routing is
  verified through synthetic `scripts/ajazz-debug input.key/encoder/touch` RPCs through the full
  pipeline (the documented autonomous-input-test method), not real hardware input.

</specifics>

<deferred>
## Deferred Ideas

- Property Inspector settings round-trip over the $SD bridge → Phase 33 (PI-01..).
- Per-app profile auto-switching → Phase 34 (APROF-..).
- Real encoder/touch wire decode (HARDWARE-GATED — needs a retail AKP05E) — out of scope; routing
  is validated via synthetic input RPCs.

</deferred>
