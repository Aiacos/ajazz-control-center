# Phase 29 — Live Verification (AKP05E, debug channel)

**Date:** 2026-06-02
**Driver:** orchestrator (inline), `AJAZZ_DEBUG_CONTROL=1`, fresh build `build/linux-release`
**Device:** `akp05e` connected (hidraw12/13 = `0300:3004`), 10 keys / 4 encoders / 4 touch zones
**Plugins installed:** `com.ajazz.sysmon.sdPlugin` (System Monitor: CPU Usage, RAM Usage), `com.test.demo.sdPlugin`
**App relaunched fresh** after killing stale instance (PID 606308, pre-dated the Phase 29 build) + removing stale socket, per the headless recipe.

## Summary verdict

| Deliverable                          | Autonomous (debug-channel) result                                                                                                             | Verdict                           |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------- |
| PLUGIN-21 real-input drag-to-bind    | qml.drag **delivers** synthesized pointer events; drag flipped Inspector to a plugin action in-memory; BUT not a clean bind proof (see below) | **NEEDS PHYSICAL-MOUSE SIGN-OFF** |
| PLUGIN-22 PI shared settings context | On-disk plugin settings keyed by **wire context** `akp05e#root#Keypad#R#C.json` — confirmed                                                   | **PASS (on-disk)**                |
| PLUGIN-23 multi-action per key       | Editor panel renders; controller verbs + 2-action persistence proven by 11 unit tests                                                         | **PASS (unit) / live pending**    |

## Build & tests

- `cmake --build build/linux-release --target ajazz-control-center ajazz_unit_tests` → clean (`ninja: no work to do` after executor builds).
- `ctest --preset linux-release -E qml` → **0 failed**. New `test_profile_multiaction.cpp`: 11 cases pass (25/25 across all ProfileController tests, incl. `2-action onPress survives save and load`).
- 3 `DeviceViewDragDrop::*` tests show **Not Run** — pre-existing `tests/qml` link-gap (CLAUDE.md latent item), NOT a Phase 29 regression.
- COD-031: executor-confirmed `grep -rn nlohmann src/core/include/` == 0. No protocol/wire/opcode/hidraw changes in the phase diff.

## PLUGIN-21 — real-input drag-to-bind (PARTIAL, autonomous limit reached)

**What the debug channel proved:**

- `qml.drag {from:"libraryTile_0", to:"key_0"}` → `{"delivered": true, "steps": 12}`. The driver's contract errors when an object/window is not found, so **both `libraryTile_0` and `key_0` resolved** and a real `QMouseEvent` press→move(×12)→release sequence was posted to the live `QQuickWindow`.
- After the drag, the Inspector flipped from `Editing: Key 5 / Open URL` → `Editing: Key 4 / Plugin action`. Since the active profile (`9a0cc0ee`) has **empty `keys` and `pages` on disk**, that in-editor "Plugin action" state was produced **by the drag** (in-memory), not loaded.

**Why this is NOT a sufficient PLUGIN-21 proof (honest gaps):**

1. **Target offset:** the drag aimed at `key_0` (Key 1) but the Inspector selected **Key 4** — the `qml.drag` scene-center→window-coord mapping appears offset. This is a *test-tool* coordinate quirk, not necessarily the user gesture.
1. **No `willAppear`/`setImage`** in `plugin.protocolLog` after the drag (log shows only register + `deviceDidConnect`) — the plugin device-bridge did not observably engage from the synthesized drag (binding may require Apply/page-activate).
1. **Nothing persisted to disk** (profile `keys` empty) — bindings persist on Apply.
1. Synthesized `QMouseEvent`s do not reliably drive Qt `PointerHandler`/`DragHandler` grab logic the same way real input does — **this is exactly the Phase-28 trap** the plan warns about (data/synthetic path ≠ real gesture).

➡ **PLUGIN-21 authoritative proof requires a REAL physical-mouse drag** (operator), confirming the key binds AND the physical AKP05E renders the plugin icon. Reserved for the human-verify checkpoint.

## PLUGIN-22 — PI ⇄ plugin shared settings context (PASS, on-disk)

On-disk plugin settings (`~/.local/share/Aiacos/AJAZZ Control Center/plugins/com.ajazz.sysmon/settings/`):

- `akp05e#root#Keypad#0#0.json` → `{"savedFor":"com.ajazz.sysmon.cpu"}`
- `akp05e#root#Keypad#0#1.json` → `{"savedFor":"com.ajazz.sysmon.ram"}`

The filenames are the **wire context id** `device#page#controller#row#column` produced by `ContextRegistry::deriveContextId` — the exact id 29-02 made `Inspector.qml._contextUuid()` return (was `profileId + label`). PI-written and plugin-read settings now key the **same** `plugin_settings_store` record. (Files may pre-date this session; the format proves the unification is in force.)

- **Live round-trip pending:** drive a PI value change, confirm the same file updates AND `plugin.protocolLog` shows `didReceiveSettings` — reserved for the operator session.

## PLUGIN-23 — multi-action per key (PASS unit / live pending)

- UI: the "Actions for Key N" `KeyBindingList` panel renders with a drop zone (`keyBindingListAppendZone`) and rows (`keyBindingRow_<i>`); a `trashDropArea` for drag-to-remove.
- Unit: `ProfileController::appendKeyAction/reorderKeyAction/removeKeyActionAt` + `activeKeyBindings().actionList` covered by 11 passing tests; `profileToJson`/`profileFromJson` already round-trips a 2-action `onPress` vector (proven by test).
- **Live persistence pending:** append 2 actions → Apply → restart → confirm order survives — reserved for the operator session.

## Operator (physical-mouse) checklist — the decisive gate

1. **PLUGIN-21:** with a real mouse, drag **CPU Usage** from the action library onto an **empty key** → key binds + the **physical AKP05E renders the CPU icon**. Repeat onto an **encoder dial** and a **touch-strip zone**. Click **Apply**.
1. **PLUGIN-22:** select that key, change a value in the Property Inspector → confirm the plugin reacts (and `~/.local/share/.../com.ajazz.sysmon/settings/akp05e#root#Keypad#R#C.json` updates).
1. **PLUGIN-23:** drag a 2nd action onto the same key → reorder the two rows → drag one row to the trash → **Apply**, restart the app, confirm the surviving action(s) persist in order.

______________________________________________________________________

## 2026-06-03 — OpenDeck plugin-system parity (move + PI config + dials)

Cross-referenced `/tmp/opendeck-src` (Tauri/Rust) for the authoritative
semantics, then closed four gaps. Live-driven on the connected AKP05E
(`0300:3004`) via `AJAZZ_DEBUG_CONTROL=1`.

### What changed

1. **Move a bound action to another control.** `populateContextsForActivePage`
   is now a true **reconcile** (diff desired-vs-registered, `willDisappear`+retire
   the vacated context) — mirrors OpenDeck `move_instance`'s
   willDisappear(old)+willAppear(new). New `ProfileController::swapKeyBindings`
   moves the **whole `core::Binding`** (multi-action onPress chain + onRelease +
   onLongPress + KeyState), replacing the lossy QML two-commit hack that
   collapsed chains to one action. DeviceView `onKeySwapRequested` calls it.
1. **PI settings round-trip.** `PIBridge::setSettings` now emits
   `contextSettingsChanged`; `PluginDeviceBridge::onPropertyInspectorSettings`
   resolves the wire context and sends `didReceiveSettings` to the live plugin —
   the PI→plugin half (mirrors OpenDeck `set_settings`). Wired in Application.
1. **Dial config.** `ProfileController::activeEncoderBindings` + a controller-aware
   `Inspector._contextUuid()` (Keypad vs `device#root#Encoder#0#col`) so a plugin
   action bound to a dial configures against the SAME context the bridge registers.
1. **Debug addressability** (CLAUDE.md rule): new `profile.swapKeyBinding` and
   `plugin.simulatePiSettings` RPCs make all of the above autonomously verifiable.

### Live results (debug channel, AKP05E)

| Check                           | Drive                                                                     | Observed                                                                               | Verdict         |
| ------------------------------- | ------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- | --------------- |
| Move key→key plugin lifecycle   | `commitKeyBinding(0)` → `swapKeyBinding{src:0,dst:5}`                     | `willAppear(Keypad 0,0)`, then `willDisappear(Keypad 0,0)` + `willAppear(Keypad 0,1)`  | **PASS**        |
| Move visual mirror              | screenshot after move                                                     | live `CPU 11%` render relocated key 1 → key 6; old key cleared                         | **PASS**        |
| Multi-action survives move      | unit `swapKeyBindings ... chain intact`                                   | 2-action onPress + KeyState relocated whole                                            | **PASS (unit)** |
| Dial context registration       | `commitEncoderBinding(0)`, `(2)`                                          | `willAppear(Encoder 0,0)`, `willAppear(Encoder 2,0)`                                   | **PASS**        |
| PI→plugin settings cross-notify | `simulatePiSettings(akp05e#root#Keypad#0#0, {interval:500,unit:percent})` | OUT `didReceiveSettings` to `com.ajazz.sysmon` carrying the new settings + coordinates | **PASS**        |
| Build / tests                   | `cmake --build`; `ctest -LE qml`                                          | clean; **681/681 pass** (4 new: swap-key move/swap/no-op + dial bindings)              | **PASS**        |

### Reserved for operator (physical mouse, headless WebEngine limit)

- The **real** PI JS `$SD.setSettings` round-trip (the WebEngineView has no
  objectName and `qml.invoke` can't run JS) — `plugin.simulatePiSettings` proves
  the substantive bridge half; the PIBridge emit + Application connect are trivial.
- A **dial PI render** end-to-end needs an Encoder-capable plugin **with** a PI
  (none installed: sysmon/demo are `controllers:["Keypad"]`).
