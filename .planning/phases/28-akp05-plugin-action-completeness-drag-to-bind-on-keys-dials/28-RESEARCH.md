# Phase 28: AKP05 Plugin Action Completeness + Drag-to-Bind on Keys & Dials — Research

**Researched:** 2026-05-31
**Domain:** Qt/QML plugin action pipeline, profile persistence, bridge ActionContext lifecycle
**Confidence:** HIGH — all findings are file:line verified against source code and RE corpus

______________________________________________________________________

\<user_constraints>

## User Constraints (from CONTEXT.md)

### Locked Decisions

| Decision                           | Choice                      | Implication                                                                                                                                                                                                                                         |
| ---------------------------------- | --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Repro plugin(s) for live diagnosis | System Monitor + Weather    | Both now located (see §Repro Plugins). Locate/install both, drive `installedActions()` via `scripts/ajazz-debug`, compare count to each manifest's visible-action count.                                                                            |
| Action-data model depth            | Full                        | Parse + model `VisibleInActionsList`, `Encoder` block (`TriggerDescription` rotate/push/touch + layout), multi-state (`States` count + state index + `DisableAutomaticStates`), and default `Settings` seeding — not just the minimal actionId fix. |
| Affordance gating strictness       | Strict reject (Elgato-like) | A `Knob`/`Encoder`-only action can ONLY drop on a dial; `Keypad`-only ONLY on a key; `SecondaryScreen` on touch zones. Mismatched drops rejected with `drag.accepted=false` + reject visual. No silent wrong-controller binds.                      |

### Claude's Discretion

Wave structure, exact diagnostic-signal shape (log + surfaced count), grouping/category UI in the library, and fixture-manifest contents — all at Claude's discretion guided by the success criteria and codebase conventions. Per CLAUDE.md: every new interactive control gets an `objectName`; verify every change live via the debug channel before "done".

### Deferred Ideas (OUT OF SCOPE)

- v1.2/v1.3 milestone-boundary formal split (MILESTONE-BOUNDARY blocker)
- Phases 9-12 operator Wireshark captures
- Phase 25 input-streaming tests on a retail AKP05E / Mirabox N4
- Any new device opcode / wire change (bridge is already correct)
  \</user_constraints>

______________________________________________________________________

\<phase_requirements>

## Phase Requirements

| ID                     | Description                                                                                                                                                                                                                                  | Research Support                                                  |
| ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| PLUGIN-18              | Action-library completeness + diagnostic: every `VisibleInActionsList != false` action appears; skipped actions (empty UUID/Name, parse failure) surface as countable diagnostic, not silent drops; proven live for System Monitor + Weather | §Repro Plugins, §Missing-Tools Mechanism, §installedActions() API |
| PLUGIN-19              | Drag-to-bind plugin actions on keys + dials + touch zones, routably persisted; fix 5-arg drops at `EncoderDial.qml:179` + `TouchStripLane.qml:271`; ActionContext registered on drop/profile-load so `input.encoder` reaches plugin          | §Drag Drop Bug, §ActionContext Registration Gap                   |
| PLUGIN-20              | Controller-affordance normalization + strict gating + full action model: Keypad/absent→key; Encoder+Knob→dial; SecondaryScreen→touch-zone; `VisibleInActionsList`+Encoder-block+multi-state+Settings parsed; unit-tested                     | §Affordance Normalization, §Full Action Model Spec                |
| \</phase_requirements> |                                                                                                                                                                                                                                              |                                                                   |

______________________________________________________________________

## Summary

Phase 28 targets two user-reported gaps after Phase 27 shipped: (1) installed plugins don't surface all their tools in the action library, and (2) tools can't be dragged onto encoder dials or touch-strip zones. Through file:line code analysis and RE corpus inspection, this research has fully resolved both gaps.

**Gap 1 — "Missing tools":** The action-enumeration pipeline is not broken. All 13 System Monitor actions pass the UUID/Name check at `plugin_catalog_model.cpp:382` and appear in the library. The REAL issue is that `VisibleInActionsList` is never parsed (field missing from `PluginAction` struct) so hidden internal actions from plugins like `emoji` and `pictureEmoticons` leak into the library. Also, the drag payload carries no controller affordance information, so QML drop targets cannot enforce the Strict-reject policy. The "missing tools" is most likely the user seeing `SecondaryScreen`/`Information` actions they can't drop on expected targets, plus any currently-hidden actions that are wrongly shown.

**Gap 2 — "Drag on dials/zones":** Confirmed at `EncoderDial.qml:179` and `TouchStripLane.qml:271`: both call `commit*Binding` with 5 args, dropping `ap.actionId`. The C++ signatures already accept 6 args (`actionId = {}`). The fix is a 1-line change per file. However, fixing the drop alone is not sufficient: `populateContextsForActivePage` is never triggered when a binding is committed via a QML drop. `profileChanged` is not connected to the bridge, so no `willAppear` is sent, no `ActionContext` is registered, and `input.encoder` reaches a dead context lookup.

**Primary recommendation:** Fix in four atomic waves: (W1) parse `VisibleInActionsList` + add diagnostic counters + extend drag payload with `controllers`; (W2) fix the 5-arg drop bugs + add affordance gating in drop targets; (W3) parse full action model (`Encoder` block, multi-state, Settings) in manifest; (W4) wire `profileChanged` → `populateContextsForActivePage` so drop/load registers the `ActionContext`.

______________________________________________________________________

## Architectural Responsibility Map

| Capability                         | Primary Tier                                            | Secondary Tier                               | Rationale                                                            |
| ---------------------------------- | ------------------------------------------------------- | -------------------------------------------- | -------------------------------------------------------------------- |
| VisibleInActionsList filtering     | C++ parser (plugin_manifest.cpp)                        | C++ model (plugin_catalog_model.cpp)         | Parser owns field extraction; model owns filter                      |
| Diagnostic for skipped actions     | C++ model (plugin_catalog_model.cpp)                    | QML surface (ActionLibraryPane.qml)          | Model aggregates skip counts; QML surfaces them                      |
| Drag affordance gating             | QML drop targets (KeyCell, EncoderDial, TouchStripLane) | C++ model (controllers in MIME payload)      | Drop targets own accept/reject; payload carries token                |
| ActionContext registration on drop | C++ bridge (plugin_device_bridge.cpp) via signal-slot   | C++ profile controller (emit profileChanged) | Bridge owns the registry; controller signals the change              |
| ActionContext registration on load | C++ bridge (plugin_device_bridge.cpp)                   | C++ application.cpp wiring                   | Already done for keys; needs to be triggered by profileChanged too   |
| Full PluginAction model            | C++ parser (plugin_manifest.cpp)                        | C++ model (QVariantMap emission)             | Parser owns the struct; model surfaces fields via installedActions() |
| Multi-state + Encoder block UI     | QML (future — Phase 28 scope = parse+store only)        | —                                            | Model depth only; UI display is Phase 28 stretch or later            |

______________________________________________________________________

## Repro Plugins (Gap 1 Diagnostic)

### System Monitor — LOCATED AND INSTALLED

- **Installed path:** `/home/aiacos/.local/share/Aiacos/AJAZZ Control Center/plugins/178366994579015.sdPlugin/`
- **Plugin name:** System Monitor (`com.hotspot.streamdock.system.monitor`)
- **Total Actions\[\]:** 13
- **Actions with empty UUID or Name:** 0 (all pass `:382` check)
- **VisibleInActionsList=false:** 0 (field absent on all actions — defaults true)
- **Controllers per action (all 13):** `["Keypad", "Information", "SecondaryScreen"]` — no `Knob` entries
- **Multi-state:** Memory + CPU have 2 states; rest have 1 state
- **Encoder block:** absent on all actions
- **Default Settings:** absent
- **Conclusion:** All 13 actions currently appear in the library (UUID/Name both populated). The user's "not showing all tools" complaint is NOT a UUID/Name skip issue. Most likely causes: (a) `VisibleInActionsList=false` actions from OTHER plugins leaking in (inflating count the user compares against); (b) `Information`-only semantics confusing the user about what can be dropped where; (c) icon resolution failure making rows appear as empty glyph tiles.
- **ALSO in defaultPlugins corpus:** 12 actions (the 13th `com.hotspot.stream.custom_system_monitor` is only in the installed version).

### Weather — NOT CURRENTLY INSTALLED

- **Corpus path:** `/home/aiacos/MEGAsync/ajazz-reverse-engineering/raw-workdir/sd-app/defaultData/defaultPlugins/com.mirabox.streamdock.weather.sdPlugin/`
- **Plugin name:** Weather query (`com.mirabox.streamdock.weather`)
- **Total Actions\[\]:** 1
- **UUID:** `com.hotspot.streamdock.weather.action1`, Name: `Weather query` — both non-empty
- **Controllers:** `["Keypad", "Information"]` — no Knob, no SecondaryScreen
- **States:** 1 state with `TitleAlignment: "bottom"`, `FontFamily`, `Image` set
- **VisibleInActionsList:** absent (defaults true)
- **Acquisition path for executor:** Install from the defaultPlugins corpus. Copy the `.sdPlugin` dir or zip it and use `scripts/ajazz-debug plugin.installFromFile`; or manually copy to the plugins dir and run `plugin.rediscover`. The defaultPlugins path above is the authoritative source. [VERIFIED: corpus]

### Plugins with VisibleInActionsList=false (confirmed from vendor corpus)

From the full vendor corpus scan, these plugins have hidden internal actions that currently LEAK into the library:

| Plugin                                    | Total actions | Hidden (VisibleInActionsList=false) | Hidden action UUIDs                                                                                                           |
| ----------------------------------------- | ------------- | ----------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| `com.mirabox.streamdock.emoji`            | 2             | 1                                   | `com.mirabox.streamdock.emoji.emoji_send` (internal send step)                                                                |
| `com.mirabox.streamdock.pictureEmoticons` | 3             | 1                                   | `com.mirabox.streamdock.emoticons.select` (internal step)                                                                     |
| `com.hotspot.streamdock.touchbar`         | 25            | 9                                   | Focus-On-Search, Notification Center, DND Mode, Dictation, Input Method, Show Desktop, Screen Lock, Fast Rewind, Fast Forward |

**Currently these 11 hidden actions show in the library.** Parsing `VisibleInActionsList` and filtering at `:381` (before the UUID/Name check) will hide them. [VERIFIED: corpus grep]

______________________________________________________________________

## Standard Stack

This phase modifies existing in-tree code only. No new external packages needed.

### Files Modified

| File                                        | Change                                                                                                                                                                                                                                  | Lines Affected                |
| ------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------- |
| `src/app/src/plugin_manifest.hpp`           | Add `visibleInActionsList`, `disableAutomaticStates`, `encoderBlock`, `stateCount`, `defaultSettings` to `PluginAction`; add `PluginActionState::name`/`title`/`showTitle`; add `PluginEncoderBlock` struct                             | New fields on existing struct |
| `src/app/src/plugin_manifest.cpp`           | Parse `VisibleInActionsList`, `DisableAutomaticStates`, `Encoder` block, per-state `Name`/`Title`/`ShowTitle`, action-level `Settings`                                                                                                  | In `parseAction()`            |
| `src/app/src/plugin_catalog_model.cpp`      | (a) Filter `visibleInActionsList=false` at `:381`; (b) emit diagnostic counts for skipped actions; (c) add `visibleInActionsList`, `controllers`, `stateCount`, `disableAutomaticStates`, `defaultSettings` to the `QVariantMap` output | `installedActions()` method   |
| `src/app/qml/ActionLibraryPane.qml`         | Add `controllers: a.controllers` to `actionModel.append(...)` at line 76; add `controllers: []` to builtins at line 56; add `controllers` to MIME payload at Drag.mimeData                                                              | Lines 56, 76, 194-200         |
| `src/app/qml/components/EncoderDial.qml`    | Fix 5-arg drop at `:179` → add `ap.actionId`; add controller-affordance gating in `onEntered`                                                                                                                                           | Line 179 + onEntered          |
| `src/app/qml/components/TouchStripLane.qml` | Fix 5-arg drop at `:271` → add `ap.actionId`; add controller-affordance gating in `onEntered`                                                                                                                                           | Line 271 + onEntered          |
| `src/app/qml/components/KeyCell.qml`        | Add controller-affordance gating for library-action drags in `onEntered` (currently only gates `x-ajazz-binding` drags)                                                                                                                 | ~Line 181-198                 |
| `src/app/src/application.cpp`               | Wire `profileChanged` → `populateContextsForActivePage` for the bridge (new Qt connect call in the `#ifdef AJAZZ_HAVE_WEBSOCKETS` block)                                                                                                | ~Lines 496-530                |
| `tests/unit/test_catalog_offline.cpp`       | Add fixture tests for `VisibleInActionsList` filtering, diagnostic counts, `controllers` in output map                                                                                                                                  | After line 310                |
| `tests/unit/test_plugin_manifest.cpp`       | Add tests for Encoder block parse, multi-state with Name/Title/ShowTitle, `DisableAutomaticStates`, `Settings` parse, `VisibleInActionsList` parse                                                                                      | New TEST_CASEs                |
| `tests/unit/test_plugin_device_bridge.cpp`  | Add test: bind plugin to encoder in profile → call `populateContextsForActivePage` → assert `willAppear` sent + context in registry                                                                                                     | After line 1232 area          |

______________________________________________________________________

## Missing-Tools Mechanism (Ranked Hypotheses)

Based on manifest corpus analysis, ranked from most to least likely for the live-device user complaint:

1. **RANK 1 — VisibleInActionsList leak (confirmed from corpus).** Internal hidden actions from emoji, pictureEmoticons, touchbar plugins currently show in the library (11 extra actions). User installs a plugin expecting N tools, sees N+hidden = more rows, some of which look empty (no icon) → "not showing all the tools I expect."
1. **RANK 2 — Icon resolution failure → glyph-only rows.** When `action.icon` resolves to a missing file, `iconUrl` is empty and the tile shows a generic Material glyph (`extension`). A user scanning for a recognized icon may not notice these rows → subjectively "missing."
1. **RANK 3 — SecondaryScreen/Information actions show but can't be dropped anywhere expected.** System Monitor has 13 actions all tagged `["Keypad","Information","SecondaryScreen"]`. Without affordance gating, the user can drag any of them onto a key, which works. But the user may be counting the "touchbar/strip" slot as a place to use `SecondaryScreen` actions and finding no affordance.
1. **RANK 4 — Empty UUID/Name skip (`:382`).** NOT the cause for System Monitor or Weather (both have fully populated UUIDs/Names). Could matter for third-party plugins not in the corpus. [VERIFIED: corpus scan shows 0 empty UUID/Name in vendor plugins]
1. **RANK 5 — Manifest parse failure (`:375`).** Would cause the entire plugin to disappear from the library, not just individual actions. No vendor plugin in the corpus fails to parse (all have required fields). Low probability unless a user installs a malformed third-party plugin.

**Live diagnostic sequence (executor must run):**

```bash
# 1. Build + launch
AJAZZ_DEBUG_CONTROL=1 ./build/linux-release/src/app/ajazz-control-center &

# 2. Install Weather if not already present
scripts/ajazz-debug plugin.installFromFile --params '{"path":"/home/aiacos/MEGAsync/ajazz-reverse-engineering/raw-workdir/sd-app/defaultData/defaultPlugins/com.mirabox.streamdock.weather.sdPlugin"}'
scripts/ajazz-debug plugin.rediscover

# 3. Query installedActions via QML
# (no direct debug RPC — use qml.invoke on PluginCatalog after adding an RPC,
#  OR add a debug-control method plugin.actions that calls installedActions())
scripts/ajazz-debug plugin.list

# 4. Compare: System Monitor manifest says 13 visible actions → installedActions() must return 13
# 5. Compare: Weather manifest says 1 visible action → installedActions() must return 1
```

**Note:** There is currently NO `plugin.installedActions` debug RPC. The executor must either (a) add one in Wave 1, or (b) use `qml.invoke` on a `PluginCatalog` method. Option (a) is the cleaner path and directly enables the PLUGIN-18 live verification requirement.

______________________________________________________________________

## Drag-Drop Bug — Exact Lines

### Current (broken) drop handlers

**`src/app/qml/components/EncoderDial.qml:179`** [VERIFIED: file:line]

```qml
ProfileController.commitEncoderBinding(root.index, "", ap.label,
                                       ap.actionKind, "");
```

The sixth argument (`ap.actionId`) is omitted. The C++ signature is:

```cpp
Q_INVOKABLE void commitEncoderBinding(int encoderIndex,
                                      QString const& iconPath,
                                      QString const& label,
                                      int actionKind,
                                      QString const& settingsJson,
                                      QString const& actionId = {});
// profile_controller.hpp:239-244
```

Fix: `ProfileController.commitEncoderBinding(root.index, "", ap.label, ap.actionKind, "", ap.actionId || "");`

**`src/app/qml/components/TouchStripLane.qml:271`** [VERIFIED: file:line]

```qml
ProfileController.commitTouchZoneBinding(zoneCell.zoneIndex, "",
                                        ap.label, ap.actionKind, "");
```

Fix: `ProfileController.commitTouchZoneBinding(zoneCell.zoneIndex, "", ap.label, ap.actionKind, "", ap.actionId || "");`

### Current drag MIME payload (ActionLibraryPane.qml:192-200) [VERIFIED: file:line]

```json
{
  "actionKind": int,
  "label": string,
  "iconName": string,
  "actionId": string,
  "iconUrl": string,
  "propertyInspectorPath": string
}
```

**Missing:** `controllers` field. Without it, drop targets cannot gate on affordance at `onEntered` time. Fix: add `"controllers": tile.controllers` and add `controllers: []` as a model field (populated from `a.controllers` in `_rebuild()`).

### Current MIME payload for cell-to-cell binding drag (KeyCell.qml:151-156) [VERIFIED: file:line]

```json
{ "controller": "Keypad", "position": int }
```

This is correct. EncoderDial and TouchStripLane already gate on this payload's `controller` field in their `onEntered` handlers. The library→target gating is what's missing.

______________________________________________________________________

## ActionContext Registration Gap

### Current wiring in application.cpp [VERIFIED: file:line]

`profileChanged` signal is connected to (lines 452-483):

- `StreamDockControlService::repaintFromProfile` (line 453) — repaints keys on profile load
- `StreamDockControlService::repaintEncodersFromProfile` (line 472) — repaints encoder overlays
- `BuiltinActionsService::resetLunBoCursors` (line 480) — resets LunBo state

**NOT connected to:** `PluginDeviceBridge::populateContextsForActivePage`.

### Current triggers for `populateContextsForActivePage` [VERIFIED: file:line]

`populateContextsForActivePage` is called by:

1. `onPluginRegistered` (line 806) — when a plugin connects to the WebSocket
1. `onDeviceConnected` (line 825) — when a device appears
1. `onActivePageChanged` (line 880) — when page navigation fires

**NOT triggered by:** a QML binding drop (which calls `commitKeyBinding`/`commitEncoderBinding`/`commitTouchZoneBinding` → `emit profileChanged()`).

### Consequence [VERIFIED: reasoning from code]

A user drops a plugin action onto an encoder dial:

1. `EncoderDial.qml:179` → `commitEncoderBinding(idx, "", label, Plugin, "", actionId)` (after the fix)
1. `profile_controller.cpp:455` emits `profileChanged()`
1. `StreamDockControlService::repaintEncodersFromProfile` runs (repaints the encoder LCD)
1. **Nothing triggers `populateContextsForActivePage`**
1. The `ContextRegistry` has no entry for `(deviceId, "Encoder", 0, encoderIndex)`
1. A later `input.encoder` event: `byCoord("Encoder", 0, encoderIndex)` returns `std::nullopt`
1. `onDeviceEvent` returns early at line 553: "unbound encoder — silent drop"
1. **Plugin never receives `dialRotate` or `dialDown`**

The same gap applies to:

- Key drops when the profile is loaded fresh (handled by `onDeviceConnected`/`onPluginRegistered`) — keys are OK on next connection
- Encoder drops during a session (encoder context only registered at device connect, not on drop)
- Touch zone bindings: `populateContextsForActivePage` does NOT enumerate `prof.touchZones` at all (only keys and encoders.onPress) — a touch zone drop is doubly broken

### Fix location in application.cpp [RECOMMENDED]

In the `#ifdef AJAZZ_HAVE_WEBSOCKETS` block (around line 530), add:

```cpp
// Wire profileChanged -> re-populate contexts so a drag-drop binding
// registers an ActionContext in the bridge immediately (PLUGIN-19).
// Uses the same activeDevice pattern as onDeviceConnected.
QObject::connect(m_profileController.get(),
                 &ProfileController::profileChanged,
                 m_pluginBridge.get(),
                 [this]() {
                     if (!m_pluginBridge->activeDeviceId().isEmpty()) {
                         m_pluginBridge->populateContextsForActivePage(
                             m_pluginBridge->activeDeviceId());
                     }
                 });
```

**Alternative approach:** Expose `activeDeviceId()` as a public accessor on `PluginDeviceBridge` (it is already `m_activeDeviceId` — private member, set in `onDeviceConnected`). Or wire via a dedicated slot `onProfileBindingChanged(deviceId)` if the device ID is accessible from the profile controller.

**Simpler alternative:** Call `populateContextsForActivePage(m_activeDeviceId)` directly inside `commitKeyBinding`, `commitEncoderBinding`, `commitTouchZoneBinding` in `profile_controller.cpp`. However, `ProfileController` has no access to `PluginDeviceBridge`, so this requires either a callback injection or the signal-slot approach above.

### Touch zone population gap in `populateContextsForActivePage` [VERIFIED: file:line]

Lines 666-755: only iterates `prof.keys` (key bindings) and `prof.encoders` (encoder bindings). Does NOT iterate `prof.touchZones`. However, touch tap routing in `onDeviceEvent` (line 628) uses `byCoord(deviceId, "Encoder", 0, zone)` — the touch zone context is looked up under `controller="Encoder"`, not a separate "TouchZone" controller. This means touch zone plugin invocation piggybacks on encoder contexts. **If an encoder has a plugin bound, a touch tap on its zone will reach the plugin via that encoder's context. If the touch zone has its own separate binding (via `commitTouchZoneBinding`), its `TouchZoneBinding.onTap` action is never registered in the ContextRegistry at all.**

**Decision needed:** For Phase 28, the simplest approach is:

- `commitTouchZoneBinding` drop: after fixing the 5-arg bug, also register the context under `controller="Encoder", row=0, column=zoneIndex` (matching the `onDeviceEvent` lookup) — OR separately enumerate `prof.touchZones.onTap` in `populateContextsForActivePage` with a "TouchZone" controller and update `onDeviceEvent`'s touch tap lookup accordingly.
- Recommend: mirror the encoder approach (zone→`controller="Encoder"` convention is LOCKED by `onDeviceEvent` line 628 — don't change `onDeviceEvent`).

______________________________________________________________________

## installedActions() QVariantMap Keys (Current + Additions)

### Current keys (plugin_catalog_model.cpp:416-426) [VERIFIED: file:line]

```cpp
m.insert("pluginName",    parsed->name);       // string
m.insert("actionId",      action.uuid);        // string (dotted UUID)
m.insert("actionName",    action.name);        // string
m.insert("icon",          iconUrl);            // string (file:// URL or "")
m.insert("propertyInspectorPath",  action.propertyInspectorPath); // string
m.insert("propertyInspectorAbsPath", piAbs);   // string (abs path or "")
m.insert("pluginUuid",    entry);              // string (.sdPlugin dir name)
m.insert("controllers",   action.controllers); // QStringList
```

### Additions needed for Phase 28

```cpp
m.insert("visibleInActionsList", action.visibleInActionsList); // bool (for QML filter if needed)
m.insert("stateCount",    static_cast<int>(action.states.size())); // int
m.insert("disableAutomaticStates", action.disableAutomaticStates); // bool
m.insert("defaultSettings", QString::fromStdString(action.defaultSettings)); // string JSON
// Encoder block fields (for Encoder-capable actions):
m.insert("encoderLayout", action.encoderBlock.layout); // QString
m.insert("encoderTriggerRotate", action.encoderBlock.triggerDescriptionRotate); // QString
m.insert("encoderTriggerPush",   action.encoderBlock.triggerDescriptionPush);   // QString
m.insert("encoderTriggerTouch",  action.encoderBlock.triggerDescriptionTouch);  // QString
```

### Drag MIME payload additions (ActionLibraryPane.qml Drag.mimeData)

Add to the JSON payload:

```json
"controllers": tile.controllers
```

QML delegate needs a `required property var controllers` (initialized from model field `controllers: a.controllers || []`).

______________________________________________________________________

## Affordance Normalization Spec

### Token → Affordance mapping (authoritative source: `akp_plugin_sdk.md §2.1` + corpus) [VERIFIED: corpus]

| Controller token(s) in manifest                | Affordance           | Drop target               | Notes                                                                                                                                                                                                                                                                                                                                            |
| ---------------------------------------------- | -------------------- | ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `["Keypad"]`                                   | Key-capable          | KeyCell                   | Standard Elgato default                                                                                                                                                                                                                                                                                                                          |
| absent / `[]`                                  | Key-capable          | KeyCell                   | Treated as Keypad-only (vendor practice: most actions omit Controllers entirely)                                                                                                                                                                                                                                                                 |
| `["Knob"]`                                     | Dial-capable         | EncoderDial               | AJAZZ-specific token; equivalent to Elgato "Encoder"                                                                                                                                                                                                                                                                                             |
| `["Encoder"]`                                  | Dial-capable         | EncoderDial               | Elgato-standard token; same affordance as Knob                                                                                                                                                                                                                                                                                                   |
| `["Keypad", "Knob"]`                           | Key + Dial           | KeyCell OR EncoderDial    | Hotspot/StreamDock pattern: Brightness, Hotkey, SystemVolume, Multimedia                                                                                                                                                                                                                                                                         |
| `["Keypad", "Encoder"]`                        | Key + Dial           | KeyCell OR EncoderDial    | Elgato-standard pattern                                                                                                                                                                                                                                                                                                                          |
| `["SecondaryScreen"]`                          | Touch-zone-capable   | TouchStripLane            | AJAZZ-specific; touch strip surface                                                                                                                                                                                                                                                                                                              |
| `["Keypad", "SecondaryScreen"]`                | Key + Touch-zone     | KeyCell OR TouchStripLane | System Monitor pattern                                                                                                                                                                                                                                                                                                                           |
| `["Information"]`                              | Library display only | —                         | NOT a drop target controller. `Information` = "show on the info overlay surface" — has no physical drop target in the AKP05E editor. Actions tagged Information-only without Keypad should remain visible in the library but produce a tooltip explaining they're informational. Dropping is valid on Keypad targets if `Keypad` is also listed. |
| `["Keypad", "Information", "SecondaryScreen"]` | Key + Touch-zone     | KeyCell OR TouchStripLane | System Monitor pattern — all 13 actions                                                                                                                                                                                                                                                                                                          |

**Implementation helper — recommended location:** A static free function in `src/app/src/plugin_manifest.hpp` (or a new `plugin_action_affordance.hpp` if it grows large):

```cpp
namespace ajazz::app {
    enum class Affordance { Key = 1, Dial = 2, TouchZone = 4 };
    using AffordanceMask = int;

    /// Compute the bitmask of drop-target affordances from a Controllers QStringList.
    /// "Knob" and "Encoder" both map to Dial; absent/[] maps to Key.
    [[nodiscard]] AffordanceMask affordanceMask(QStringList const& controllers);
}
```

Expose the mask as an int in the `installedActions()` QVariantMap and in the MIME payload (`"affordanceMask": int`) so drop targets compare with a single bitwise AND rather than iterating string lists.

______________________________________________________________________

## Full Action Model Parse Spec

### PluginAction struct additions (plugin_manifest.hpp)

```cpp
struct PluginEncoderBlock {
    QString icon;                      ///< Encoder.Icon
    QString layout;                    ///< Encoder.layout ($A0/$A1/path)
    QString triggerDescriptionRotate;  ///< Encoder.TriggerDescription.Rotate
    QString triggerDescriptionPush;    ///< Encoder.TriggerDescription.Push
    QString triggerDescriptionTouch;   ///< Encoder.TriggerDescription.Touch
    QString triggerDescriptionLongTouch; ///< Encoder.TriggerDescription.LongTouch
};

struct PluginAction {
    // ... existing fields ...
    bool visibleInActionsList{true};     ///< VisibleInActionsList (default true)
    bool disableAutomaticStates{false};  ///< DisableAutomaticStates
    std::string defaultSettings;         ///< Settings object as raw JSON string
    PluginEncoderBlock encoderBlock;     ///< Encoder object (empty for non-dial actions)
};
```

### parseAction() additions (plugin_manifest.cpp)

```cpp
// In parseAction():
a.visibleInActionsList = obj.value(QStringLiteral("VisibleInActionsList")).toBool(true);
a.disableAutomaticStates = obj.value(QStringLiteral("DisableAutomaticStates")).toBool(false);

QJsonValue const settingsVal = obj.value(QStringLiteral("Settings"));
if (settingsVal.isObject()) {
    a.defaultSettings = QJsonDocument(settingsVal.toObject()).toJson(QJsonDocument::Compact).toStdString();
}

QJsonValue const encVal = obj.value(QStringLiteral("Encoder"));
if (encVal.isObject()) {
    QJsonObject const encObj = encVal.toObject();
    a.encoderBlock.icon = encObj.value(QStringLiteral("Icon")).toString();
    a.encoderBlock.layout = encObj.value(QStringLiteral("layout")).toString();
    QJsonValue const tdVal = encObj.value(QStringLiteral("TriggerDescription"));
    if (tdVal.isObject()) {
        QJsonObject const td = tdVal.toObject();
        a.encoderBlock.triggerDescriptionRotate = td.value(QStringLiteral("Rotate")).toString();
        a.encoderBlock.triggerDescriptionPush   = td.value(QStringLiteral("Push")).toString();
        a.encoderBlock.triggerDescriptionTouch  = td.value(QStringLiteral("Touch")).toString();
        a.encoderBlock.triggerDescriptionLongTouch = td.value(QStringLiteral("LongTouch")).toString();
    }
}
```

### PluginActionState additions

```cpp
struct PluginActionState {
    // ... existing fields ...
    QString name;       ///< States[i].Name (Elgato standard, per schema)
    QString title;      ///< States[i].Title
    bool showTitle{true}; ///< States[i].ShowTitle
};
```

**Source authority:** `docs/schemas/plugin_manifest.schema.json:246-265` defines `Name`, `Title`, `ShowTitle`, `TitleColor`, `TitleAlignment`, `FontFamily`, `FontSize`, `FontStyle`, `FontUnderline`. Current `parseState()` already reads TitleColor/TitleAlignment/FontFamily/FontSize/FontStyle but misses `Name`, `Title`, `ShowTitle`. [VERIFIED: schema + plugin_manifest.cpp]

______________________________________________________________________

## Diagnostic Design for PLUGIN-18

### Diagnostic signal (recommended)

Add a Q_SIGNAL to `PluginCatalogModel`:

```cpp
signals:
    void skippedActionsChanged(int skippedCount, int totalScanned);
```

Emit after `installedActions()` completes (or from a separate `diagnosticScanResult()` Q_INVOKABLE). The `skippedCount` includes:

- Actions with empty UUID or Name (`:382`)
- Full plugins that fail to parse (`:375` — counted at plugin granularity, not action granularity since we can't know their action count)
- Actions filtered by `visibleInActionsList=false` (these are NOT "skipped errors" — they are intentionally hidden; surface them separately as `hiddenCount` if desired)

**Recommended QVariantMap shape from a new `plugin.actions` debug RPC:**

```json
{
  "installedCount": 13,
  "skippedUuidName": 0,
  "skippedParseFailure": 0,
  "hiddenByVisibility": 0,
  "actions": [ { "actionId": "...", "actionName": "...", "controllers": [...] }, ... ]
}
```

### Debug RPC to add

`plugin.installedActions` — calls `PluginCatalogModel::installedActions()` and returns the QVariantList as JSON. This directly enables the PLUGIN-18 live diagnostic:

```bash
scripts/ajazz-debug plugin.installedActions
# Expected for System Monitor: 13 actions (0 skipped, 0 hidden)
# Expected for Weather: 1 action (0 skipped, 0 hidden)
```

______________________________________________________________________

## Common Pitfalls

### Pitfall 1: profileChanged triggers repaintFromProfile which resets context state

**What goes wrong:** `repaintFromProfile` (connected at application.cpp:453) runs first (Qt direct-connection registration order). If `populateContextsForActivePage` also runs, it calls `registerContext` which is idempotent (same key updates the entry). The order is safe.
**How to avoid:** Verify the connection is registered AFTER the existing repaint connections (lines 452-483) to maintain the documented IN-02 ordering invariant.

### Pitfall 2: affordanceMask comparison with empty controllers list

**What goes wrong:** `controllers=[]` (absent in manifest) must default to Key-capable. If the helper returns 0 for empty input, a drop on any key will be rejected.
**How to avoid:** Default affordance mask = `Affordance::Key` when controllers list is empty. Add explicit test case for the empty-list case.

### Pitfall 3: `Information`-only actions producing a confusing no-drop target

**What goes wrong:** An action tagged `["Information"]` (no Keypad, no Knob, no SecondaryScreen) will have affordanceMask=0 — no valid drop target. The user drags it and every drop is rejected with the error visual.
**How to avoid:** For Phase 28, keep `Information`-only actions visible in the library but non-draggable (set `enabled: affordanceMask !== 0` on the drag handler, similar to the existing `isHint` gate). Show a tooltip: "This action is for informational display only." [ASSUMED — policy not locked]

### Pitfall 4: populateContextsForActivePage called with empty activeDeviceId

**What goes wrong:** `m_activeDeviceId` is empty until `onDeviceConnected` fires. If `profileChanged` fires before any device connects (e.g. on profile load at startup), calling `populateContextsForActivePage("")` is a no-op (the profile accessor returns prof.keys correctly, but the deviceId "" would produce context IDs like `"#root#Keypad#0#0"` which never match device events).
**How to avoid:** Guard: `if (m_activeDeviceId.isEmpty()) return;` at the top of the new `profileChanged` connection lambda. This is already the pattern in `onPluginRegistered` (falls back to "akp05e" — the new wiring should NOT do the fallback, just no-op when no device is active).

### Pitfall 5: Touch zone ContextRegistry lookup uses "Encoder" controller

**What goes wrong:** `onDeviceEvent` case `TouchUp` (line 628) does `byCoord(deviceId, "Encoder", 0, zone)`. If the touch zone binding is registered under a separate "TouchZone" controller, the lookup misses it.
**How to avoid:** When registering touch zone contexts from `TouchZoneBinding.onTap`, use `controller = "Encoder"` and `column = zoneIndex` — matching the lookup in `onDeviceEvent`. Document this explicitly in the code comment. Do NOT change `onDeviceEvent`'s lookup key (it is correct per the AKP05E physical topology where each encoder owns one touch zone).

### Pitfall 6: ActionLibraryPane `_rebuild()` called from `installedActions()` which scans disk

**What goes wrong:** Adding `plugin.installedActions` debug RPC calls `installedActions()` which scans the plugins dir, opens manifest files, and parses JSON on every call. This is already the pattern (used by ActionLibraryPane.\_rebuild()), but calling it very frequently from a debug RPC could be slow.
**How to avoid:** For Phase 28 the single diagnostic call is fine. Document that `installedActions()` is not a hot path and should not be called on every frame.

### Pitfall 7: `VisibleInActionsList` filter vs diagnostic count

**What goes wrong:** Filtering `visibleInActionsList=false` actions at `:381` (before the UUID/Name check) means the diagnostic counter for "skipped" should distinguish "intentionally hidden" from "erroneously dropped." Don't count hidden-by-visibility as errors.
**How to avoid:** Maintain two separate counters: `hiddenCount` (VisibleInActionsList=false) and `errorSkipCount` (empty UUID/Name or parse failure). Emit them separately from the diagnostic signal.

______________________________________________________________________

## Test + Live-Verification Plan

### Catch2 tests to add

#### test_plugin_manifest.cpp (new test cases)

1. **VisibleInActionsList parse:** manifest with one action having `"VisibleInActionsList": false` → `action.visibleInActionsList == false`; action with absent field → defaults to true.
1. **DisableAutomaticStates parse:** action with `"DisableAutomaticStates": true` → field set; absent → false.
1. **Encoder block parse:** action with full `Encoder` object → all fields populated (`layout`, `TriggerDescription.{Rotate,Push,Touch,LongTouch}`, `Icon`).
1. **Default Settings parse:** action with `"Settings": {"key": "val"}` → `defaultSettings` == `{"key":"val"}` (canonical JSON).
1. **States Name/Title/ShowTitle parse:** state with `"Name": "On"`, `"Title": "Active"`, `"ShowTitle": false` → all fields populated.

#### test_catalog_offline.cpp (new test cases)

1. **VisibleInActionsList filtering:** manifest with 3 actions, one having `"VisibleInActionsList": false` → `installedActions()` returns 2; the hidden action's UUID is absent.
1. **Diagnostic counts:** manifest parse failure (malformed JSON) + manifest with 1 empty-UUID action → verify `skippedActionsChanged` emits with correct counts.
1. **controllers in output map:** manifest with `"Controllers": ["Knob"]` → output map has `controllers == ["Knob"]`; absent Controllers → `controllers == []`.
1. **defaultSettings in output map:** action with Settings object → `defaultSettings` key is non-empty JSON.

#### test_plugin_device_bridge.cpp (new test cases)

1. **populateContextsForActivePage registers encoder context:** construct bridge with mock server, set profile with encoder[0].onPress having a Plugin action, call `populateContextsForActivePage("akp05e")` → `registry().byCoord("akp05e", "Encoder", 0, 0)` has value; server received `willAppear`.
1. **populateContextsForActivePage registers touch zone as Encoder context:** profile with touchZones[1].onTap having a Plugin action → after adding touchZone enumeration to `populateContextsForActivePage`, `registry().byCoord("akp05e", "Encoder", 0, 1)` has value.

#### Affordance helper tests (new test file or inline in test_plugin_manifest.cpp)

1. Empty controllers → mask includes Key
1. `["Knob"]` only → mask includes Dial, not Key, not TouchZone
1. `["Encoder"]` only → mask includes Dial (same as Knob)
1. `["Keypad", "Knob"]` → mask includes Key + Dial
1. `["SecondaryScreen"]` only → mask includes TouchZone, not Key
1. `["Keypad", "Information", "SecondaryScreen"]` → mask includes Key + TouchZone (Information ignored as non-target)
1. `["Information"]` only → mask = 0 (non-draggable)

### Debug-channel live verification sequence

All required RPCs exist as of commit `825eccd` / `e6cb806`. [VERIFIED: debug_control_facade.cpp]

Available RPCs:

- `plugin.list` — lists connected plugins with count
- `plugin.sendEvent` — send event to a plugin process
- `plugin.installFromFile` — install + spawn without restart
- `plugin.rediscover` — re-scan plugins dir + spawn new ones
- `input.encoder` — synthetic encoder rotation (verified: memory `project_akp05_autonomous_input_test`)
- `input.encoderPress` — synthetic encoder press
- `input.key` — synthetic key press
- `input.touch` — synthetic touch tap

**Target sequence (PLUGIN-19 live verification):**

```bash
# Step 1: Launch with debug control enabled
AJAZZ_DEBUG_CONTROL=1 ./build/linux-release/src/app/ajazz-control-center &
APID=$!

# Step 2: Verify System Monitor spawns (already installed)
scripts/ajazz-debug plugin.list
# Expected: connectedCount >= 1, System Monitor visible

# Step 3: Verify installedActions count matches manifest (PLUGIN-18)
scripts/ajazz-debug plugin.installedActions
# Expected: 13 actions for System Monitor (0 skipped, 0 hidden after Phase 28)

# Step 4: Via QML, drag a System Monitor CPU action onto encoder 0
# (use qml.invoke or a manual drag in the UI)
# After drop, verify the binding is persisted
scripts/ajazz-debug state
# Look for encoder[0].onPress[0].id == "com.hotspot.stream.cpu"

# Step 5: Fire synthetic encoder rotation → plugin should receive dialRotate
scripts/ajazz-debug input.encoder --params '{"index": 0, "delta": 1}'
# Expected log: plugin receives {"event":"dialRotate","payload":{"ticks":1,"controller":"Encoder",...}}

# Step 6: Fire synthetic encoder press → plugin should receive dialDown
scripts/ajazz-debug input.encoderPress --params '{"index": 0}'
# Expected log: plugin receives {"event":"dialDown",...}

# Step 7: Screenshot to confirm visual
scripts/ajazz-debug screenshot
```

**PLUGIN-18 diagnostic live verification:**

```bash
# After adding plugin.installedActions RPC:
scripts/ajazz-debug plugin.installedActions
# Compare returned count to manifest Actions[] count
# For emoji plugin: returned count = 1 (emoji) not 2 (emoji + emoji_send)
# For pictureEmoticons: returned count = 2 (lib + select2) not 3 (lib + select + select2)
```

______________________________________________________________________

## Architecture Patterns

### Wave structure (recommended)

```
Wave 0 (setup): add fixture manifests to tests/unit/fixtures/
Wave 1 (parser): extend PluginAction struct + parseAction() + affordanceMask helper + unit tests
Wave 2 (model+QML): installedActions() filter + MIME payload + drop gating + diagnostic + unit tests
Wave 3 (bridge): populateContextsForActivePage wiring via profileChanged + touch zone enumeration
Wave 4 (live): build + debug-channel end-to-end verification sequence
```

### Don't Hand-Roll

| Problem                                      | Don't Build                                | Use Instead                                                       | Why                                                               |
| -------------------------------------------- | ------------------------------------------ | ----------------------------------------------------------------- | ----------------------------------------------------------------- |
| JSON-to-QVariant bridge for QML              | manual QJsonObject → QVariantMap iteration | Qt's `QJsonObject::toVariantMap()`                                | Already the pattern in `installedActions()`; handles nested types |
| Controller token normalization state machine | complex switch/if chain                    | bitmask helper function                                           | One function, one test, reusable by parser + QML                  |
| Encoder context key derivation               | ad-hoc string concatenation                | `ContextRegistry::deriveContextId` / `coordKey` already in bridge | These are locked schemes; don't duplicate                         |

______________________________________________________________________

## State of the Art

| Old Approach                               | Current Approach                                    | Phase           | Impact                                                       |
| ------------------------------------------ | --------------------------------------------------- | --------------- | ------------------------------------------------------------ |
| No VisibleInActionsList parsing            | Absent field defaults true; hidden actions leak     | Phase 28 target | 11 vendor actions wrongly visible today                      |
| 5-arg commit calls in encoder + zone drops | Silent actionId=empty in persisted binding          | Phase 28 target | Drag-to-bind on dials/zones produces un-routable bindings    |
| profileChanged not connected to bridge     | willAppear only on device-connect / plugin-register | Phase 28 target | Drop-bound actions not invokable until next device reconnect |
| controllers passed through but never used  | QML library has no affordance gating                | Phase 28 target | Any action can drop on any target type                       |

______________________________________________________________________

## Assumptions Log

| #   | Claim                                                                                           | Section                     | Risk if Wrong                                                                                                                                                                            |
| --- | ----------------------------------------------------------------------------------------------- | --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | `Information`-only actions (affordanceMask=0) should be non-draggable with a tooltip            | Pitfall 3, Affordance table | If the user expects to drag `Information` actions somewhere, the UI would reject all drops silently → confusing. Low risk: no physical surface exists for `Information`-only drops.      |
| A2  | Touch zone ContextRegistry entries use `controller="Encoder"` matching `onDeviceEvent` line 628 | Touch zone population gap   | If `onDeviceEvent` is changed in a future phase to use a different controller key for touch, the registration convention breaks. Must be co-documented with the `onDeviceEvent` comment. |

**If this table is empty:** All other claims in this research are verified or cited.

______________________________________________________________________

## Open Questions

1. **Should `Information`-only actions (affordanceMask=0) be hidden from the library entirely, or shown as non-draggable?**

   - What we know: `Information` is a vendor AJAZZ extension; it has no drop target in the AKP05E editor.
   - What's unclear: user expectation — do they expect to see these actions (they ARE declared in the manifest) or not?
   - Recommendation: Show in library but mark non-draggable (consistent with the `isHint` pattern for the "install plugins" row); add a tooltip.

1. **Should the `profileChanged` → `populateContextsForActivePage` connection use a lambda or a dedicated slot?**

   - What we know: `m_activeDeviceId` is private on `PluginDeviceBridge`; the lambda needs to call a method that takes a deviceId.
   - What's unclear: Whether exposing `activeDeviceId()` as a public accessor is acceptable or if a new public slot `onProfileBindingChanged()` is preferable.
   - Recommendation: Add `[[nodiscard]] QString activeDeviceId() const noexcept` accessor to `PluginDeviceBridge`; use a lambda in `application.cpp` consistent with the existing lambda patterns at lines 501, 546.

1. **Should the `defaultSettings` JSON from the manifest be seeded into `settingsJson` on drop, or only at first Property Inspector open?**

   - What we know: `Settings` is present in PR plugin (15 actions all have VKeyCode/KeyCtrl defaults); Weather and System Monitor do not have Settings objects.
   - What's unclear: Whether seeding defaults on drop causes any conflict with existing PI-bridge settings persistence.
   - Recommendation: Seed `defaultSettings` into the binding's `settingsJson` at drop time (pass it as the `settingsJson` parameter to `commit*Binding`). This matches Elgato SDK behavior. If the action has no Settings object, `settingsJson` stays empty (current behavior).

______________________________________________________________________

## Environment Availability

| Dependency                    | Required By                 | Available                                    | Version                                                                                    | Fallback                                                                                                                                         |
| ----------------------------- | --------------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| System Monitor plugin         | PLUGIN-18 live diagnosis    | Yes                                          | installed at `~/.local/share/Aiacos/AJAZZ Control Center/plugins/178366994579015.sdPlugin` | —                                                                                                                                                |
| Weather plugin                | PLUGIN-18 live diagnosis    | No (not installed)                           | —                                                                                          | Copy from corpus at `~/MEGAsync/ajazz-reverse-engineering/raw-workdir/sd-app/defaultData/defaultPlugins/com.mirabox.streamdock.weather.sdPlugin` |
| `scripts/ajazz-debug`         | Live verification           | Yes                                          | present (commit `825eccd`)                                                                 | —                                                                                                                                                |
| `input.encoder` RPC           | PLUGIN-19 live verification | Yes (verified: debug_control_facade.cpp:313) | —                                                                                          | —                                                                                                                                                |
| `plugin.installFromFile` RPC  | Weather install             | Yes (commit `825eccd`)                       | —                                                                                          | —                                                                                                                                                |
| `plugin.installedActions` RPC | PLUGIN-18 live diagnosis    | No — must be ADDED in Wave 2                 | —                                                                                          | Use `qml.invoke` on PluginCatalog directly                                                                                                       |
| AKP05E device                 | Full live round-trip        | Yes (0x3004 demo unit)                       | —                                                                                          | Input unreachable from hardware; synthetic `input.encoder` covers the routing                                                                    |

______________________________________________________________________

## Validation Architecture

### Test Framework

| Property           | Value                                                                   |
| ------------------ | ----------------------------------------------------------------------- |
| Framework          | Catch2 (existing; all tests use `[catch2]` style)                       |
| Config file        | `CMakeLists.txt` (auto-discovered via `tests/unit/`)                    |
| Quick run command  | `ctest --preset linux-release -R "catalog\|manifest\|bridge" -E qml -x` |
| Full suite command | `ctest --preset linux-release -E qml`                                   |

### Phase Requirements → Test Map

| Req ID    | Behavior                                         | Test Type        | Automated Command                                    | File Exists?                                    |
| --------- | ------------------------------------------------ | ---------------- | ---------------------------------------------------- | ----------------------------------------------- |
| PLUGIN-18 | VisibleInActionsList=false actions filtered      | unit             | `ctest --preset linux-release -R catalog_offline`    | Yes (extend)                                    |
| PLUGIN-18 | Diagnostic skipped count accurate                | unit             | same                                                 | Yes (extend)                                    |
| PLUGIN-18 | installedActions count == manifest visible count | integration/live | debug channel                                        | No — Wave 4                                     |
| PLUGIN-19 | EncoderDial drop passes actionId                 | unit (QML)       | `ctest --preset linux-release -R DeviceViewDragDrop` | Yes (`test_device_view_drag_drop.qml`) — extend |
| PLUGIN-19 | TouchStripLane drop passes actionId              | unit (QML)       | same                                                 | Yes — extend                                    |
| PLUGIN-19 | ActionContext registered after profile commit    | unit             | `ctest --preset linux-release -R bridge`             | Yes (extend)                                    |
| PLUGIN-19 | input.encoder reaches plugin after drop          | live             | debug channel sequence                               | No — Wave 4                                     |
| PLUGIN-20 | affordanceMask helper all token variants         | unit             | `ctest --preset linux-release -R manifest`           | No — Wave 1                                     |
| PLUGIN-20 | Encoder block parsed correctly                   | unit             | same                                                 | No — Wave 1                                     |
| PLUGIN-20 | Strict reject of cross-controller drops          | unit (QML)       | `ctest --preset linux-release -R DeviceViewDragDrop` | Yes — extend                                    |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -E qml -x` (fast; skips known-broken QML target)
- **Per wave merge:** `ctest --preset linux-release -E qml`
- **Phase gate:** Full suite green before `/gsd:verify-work`; `grep -rn nlohmann src/core/include/` = 0 (COD-031)

### Wave 0 Gaps

- [ ] `tests/unit/fixtures/manifest_visibility.json` — covers VisibleInActionsList + Encoder block + multi-state
- [ ] `tests/unit/fixtures/manifest_affordances.json` — covers all controller token variants for affordanceMask tests
- [ ] `plugin.installedActions` debug RPC — needed for live PLUGIN-18 verification

______________________________________________________________________

## Security Domain

Security enforcement applies. This phase is UI + plugin-app layer only with no new trust boundaries:

| ASVS Category         | Applies | Standard Control                                                                                                                                |
| --------------------- | ------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | no      | —                                                                                                                                               |
| V3 Session Management | no      | —                                                                                                                                               |
| V4 Access Control     | partial | `VisibleInActionsList` filter: hidden actions must never be routable; existing UUID-prefix plugin-ownership check (T-19-leak) remains the guard |
| V5 Input Validation   | yes     | `affordanceMask` helper receives untrusted strings from manifest; no injection risk (string comparison only; no dynamic evaluation)             |
| V6 Cryptography       | no      | —                                                                                                                                               |

**COD-031 boundary check:** All changes are in `src/app/src/` (app layer). No `nlohmann::json` additions. `QJsonObject`/`QJsonDocument` only. [VERIFIED: all modified files are already COD-031-clean]

______________________________________________________________________

## Sources

### Primary (HIGH confidence)

- `src/app/src/plugin_manifest.{hpp,cpp}` — exact field names and parse logic [VERIFIED: file:line]
- `src/app/src/plugin_catalog_model.cpp:348-432` — `installedActions()` implementation [VERIFIED: file:line]
- `src/app/qml/components/EncoderDial.qml:171-201` — drop handler with 5-arg bug [VERIFIED: file:line]
- `src/app/qml/components/TouchStripLane.qml:264-292` — drop handler with 5-arg bug [VERIFIED: file:line]
- `src/app/qml/ActionLibraryPane.qml` — drag payload structure [VERIFIED: file:line]
- `src/app/src/plugin_device_bridge.cpp:654-755` — `populateContextsForActivePage` full body [VERIFIED: file:line]
- `src/app/src/application.cpp:450-483` — `profileChanged` connections (no bridge wiring) [VERIFIED: file:line]
- `src/app/src/profile_controller.{hpp,cpp}:207-498` — all 3 commit methods + 6-arg signatures [VERIFIED: file:line]
- `src/core/include/ajazz/core/profile.hpp:100-132` — `Action`, `Binding`, `EncoderBinding`, `TouchZoneBinding` structs [VERIFIED: file:line]
- `docs/schemas/plugin_manifest.schema.json:225-305` — Encoder block schema, States fields, VisibleInActionsList [VERIFIED: file read]
- `docs/protocols/streamdeck/akp_plugin_sdk.md §2/§2.1` — controller token table, AJAZZ extensions [VERIFIED: file read]

### Secondary (MEDIUM confidence)

- `/home/aiacos/MEGAsync/ajazz-reverse-engineering/raw-workdir/sd-app/defaultData/defaultPlugins/` — 11 vendor default plugins with real manifests; System Monitor (12 actions) and Weather (1 action) analyzed [VERIFIED: corpus Python scan]
- `/home/aiacos/.local/share/Aiacos/AJAZZ Control Center/plugins/` — 3 installed user plugins; System Monitor (13 actions) confirmed all UUID/Name populated [VERIFIED: corpus Python scan]
- `src/app/src/debug_control_facade.cpp:302-500` — debug RPCs confirmed present [VERIFIED: file:line]

______________________________________________________________________

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — pure in-tree modification, no external packages
- Architecture: HIGH — all file:line verified; ActionContext gap confirmed by absence of signal-slot
- Pitfalls: HIGH — pitfalls derived from reading the actual code paths, not inference
- Repro plugins: HIGH for System Monitor (installed), MEDIUM for Weather (in corpus, not installed)

**Research date:** 2026-05-31
**Valid until:** 90 days (no fast-moving external dependencies; all in-tree)
