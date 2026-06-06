# Phase 29: Plugin GUI Parity — Context

**Gathered:** 2026-06-02
**Status:** Ready for planning
**Source:** Live recheck + OpenDeck deep-dive (this session) — substitutes for discuss-phase

<domain>
## Phase Boundary

Make the plugin system fully usable from the **real GUI with a real mouse on Wayland**, not just via the `AJAZZ_DEBUG_CONTROL` channel. Three deliverables: (1) physical-mouse drag-drop of plugin tiles onto keys/dials/touch-zones actually binds + renders; (2) Property Inspector opens + round-trips settings end-to-end against the same store the plugin reads; (3) multi-action per key + drag-to-reorder/remove (OpenDeck per-button editing).

OUT of scope (deferred, captured in ROADMAP): per-app profile auto-switch, action folders/pages.

The device-facing wire/runtime is DONE and verified this session (commits 519ecd0..361cc77): Elgato envelope (action/context/device top-level + payload.settings), owner-from-manifest, crash-defer, consent persistence, `plugin_settings_store`, plugin-side settings round-trip, PI relay signal. Phase 29 is the GUI/editing layer ON TOP — UI + ProfileController + core::Profile only. No protocol/opcode/wire changes.
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### PLUGIN-21 — real-input drag-drop (the user-blocking defect)

- The DATA/commit path is CORRECT and must not be rewritten: library tile packs `{actionKind,label,iconName,actionId,iconUrl,propertyInspectorPath,affordanceMask}` (ActionLibraryPane.qml:212-219); KeyCell DropArea reads `application/x-ajazz-action` + gates on `affordanceMask & 1` (KeyCell.qml:187-279); DeviceView commits 6-arg `commitKeyBinding` (DeviceView.qml:316-330). Confirmed live: `com.ajazz.sysmon.cpu/ram` have `affordanceMask=1`, `visible=true` — tiles ARE eligible.
- The BUG is the GESTURE layer: the Phase-29 `DragRelay` singleton + `Drag.Internal` cursor-follow ghost (Main.qml:346-427) on a real Wayland mouse. A physical drag produces no drop. Root-cause it with file:line (candidates: DragRelay never activated by the tile's DragHandler/MouseArea on Wayland; ghost not following; `Drag.drop()` not fired; DropArea not seeing the internal mime keys).
- **Verification is the crux (Phase-28 trap):** Phase 28 verified `commitBinding` via the debug channel and called drag "done" — it NEVER drove a real pointer drag. PLUGIN-21 MUST be verified by driving a real pointer sequence (press tile → move over key → release) through the running GUI. The debug channel has NO real-drag driver today (`qml.click`/`qml.invoke` emit signals, not pointer events, and per CLAUDE.md don't reproduce `onToggled`/gesture side effects). So this phase ADDS a debug-channel pointer/drag driver (e.g. `input.pointer` press/move/release against window coords, or a `qml.drag` from objectName A to objectName B) — itself debug-addressable per CLAUDE.md.

### PLUGIN-22 — Property Inspector config unification

- BUG: PI uses `contextUuid` = `profileId + selection-label` (Inspector.qml `_contextUuid()`), but the plugin + wire use `device#page#controller#row#column` (ContextRegistry::deriveContextId). PIBridge persists per `contextUuid`; `plugin_settings_store` persists per wire context. → PI-written and plugin-read settings land in DIFFERENT files. They must share ONE record.
- DECISION: Inspector.qml passes the WIRE context id as `contextUuid` to `PropertyInspectorController.loadInspector(...)`. PIBridge then reads/writes via `plugin_settings_store` keyed by that same wire context (unify PIBridge's persistence onto the shared store from commit dea557d). The plugin->PI relay (`PluginDeviceBridge::relayToPropertyInspector` -> `PIBridge::deliverToPropertyInspector`, commit 361cc77) is now meaningful and must be exercised live.
- WebEngine IS compiled (`AJAZZ_HAVE_WEBENGINE=1`, symbols present). The PI page can render offscreen; verifying the JS round-trip needs a driven interaction + reading the on-disk store + the plugin's `didReceiveSettings` (protocolLog).

### PLUGIN-23 — multi-action + reorder/remove

- `core::Profile` Binding already holds `onPress: std::vector<Action>` (multi capable at the model layer) — but QML/ProfileController commit a SINGLE action (`binding.onPress = {act}` overwrites). OpenDeck wraps multiple child actions per button (InstanceEditor; /tmp/opendeck-study/OpenDeck/src).
- DECISION: extend ProfileController with append/reorder/remove-at-index for a key's action list; QML renders a per-key binding list with drag-reorder + drag-to-trash (trash DropArea already exists per Phase 26 D-09). Persist across restart. Keep affordance gating.

### Cross-cutting

- Verify EVERY change live through `AJAZZ_DEBUG_CONTROL` before claiming done (CLAUDE.md MANDATORY). Build/relaunch recipe: `setsid env AJAZZ_DEBUG_CONTROL=1 build/linux-release/src/app/ajazz-control-center` (GUI) or `QT_QPA_PLATFORM=offscreen` (headless); `scripts/ajazz-debug` over the unix socket.
- Every new control sets `objectName`. New debug drivers are RPC methods. COD-031 clean. No wire/opcode changes.
  </decisions>

\<canonical_refs>

## Canonical References (read before planning/implementing)

### Drag-drop (PLUGIN-21)

- `src/app/qml/Main.qml:346-427` — DragRelay singleton + ghost (Drag.Internal) — the suspect gesture path.
- `src/app/qml/ActionLibraryPane.qml:212-219` — tile drag SOURCE payload.
- `src/app/qml/components/KeyCell.qml:187-279` — key DropArea (onEntered affordance gate, onDropped).
- `src/app/qml/components/EncoderDial.qml:178-219`, `src/app/qml/components/TouchStripLane.qml` — dial/zone drop (working commit path to mirror).
- `src/app/qml/DeviceView.qml:316-330` — onKeyActionDropped -> ProfileController.commitKeyBinding.
- `src/app/src/debug_control_qml.cpp`, `src/app/src/debug_control_facade.cpp` — where to add a real pointer/drag driver RPC.

### Property Inspector (PLUGIN-22)

- `src/app/qml/Inspector.qml` (`_contextUuid()`, `maybeLoadInspector()`), `PropertyInspector.qml`, `PIWebView.qml`.
- `src/app/src/property_inspector_controller.{hpp,cpp}` (`loadInspector(pluginUuid, htmlAbsPath, actionUuid, contextUuid)`).
- `src/app/src/pi_bridge.{hpp,cpp}` (per-context persistence; unify onto the shared store).
- `src/app/src/plugin_settings_store.{hpp,cpp}` (the shared store, keyed by wire context).
- `src/app/src/plugin_device_bridge.cpp` (ContextRegistry::deriveContextId — the wire context id format; relayToPropertyInspector).

### Multi-action (PLUGIN-23)

- `src/core/include/ajazz/core/profile.hpp` (Binding.onPress vector + Action).
- `src/app/src/profile_controller.{hpp,cpp}` (commit\*Binding — extend for multi/reorder/remove).
- `/tmp/opendeck-study/OpenDeck/src` — OpenDeck Svelte InstanceEditor / per-button editing reference.

### Session work this phase builds on

- commits 519ecd0 (owner map), ddabc16 (Elgato envelope), 06ca4f5 (consent), c7c95f4 (crash defer), dea557d (settings store + round-trip), 361cc77 (PI relay).
  \</canonical_refs>

<specifics>
## Specific Ideas
- Live drag driver: prefer `input.pointer {action:press|move|release, x, y}` in window coords (drives the real QML gesture), OR `qml.drag {from:objectName, to:objectName}` that synthesizes press-at-source-center → moves → release-at-target-center. Must make the DragRelay path fire exactly as a human mouse would.
- For the drag root-cause: compare what activates DragRelay (the tile's pointer handler) vs. what the ghost/DropArea expect; check Wayland-specific `Drag.Internal` behavior and whether the tile uses DragHandler vs MouseArea (Phase 29 memory: "Drag.Automatic broken on Wayland → Drag.Internal + cursor-follow ghost").
- PI unification: the wire context for the active key = `${deviceCodename}#root#Keypad#${row}#${col}` (col = index % 5, row = index / 5 for AKP05E). Inspector.qml knows the device + selected key index.
</specifics>

<deferred>
## Deferred Ideas
- Per-application automatic profile switching (foreground-window watcher) — own phase.
- Visual action folders/pages organization — own phase.
- Unifying PIBridge's legacy file-store fully onto plugin_settings_store for GLOBAL settings (per-context is the priority here).
</deferred>

______________________________________________________________________

*Phase: 29-plugin-gui-parity-real-drag-drop-pi-config-multi-action*
*Context gathered: 2026-06-02 via live recheck + OpenDeck deep-dive*
