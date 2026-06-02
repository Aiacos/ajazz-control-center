---
phase: 29-plugin-gui-parity-real-drag-drop-pi-config-multi-action
plan: '02'
subsystem: plugin-gui
tags: [plugin, property-inspector, settings, context-id, pi-bridge]
dependency_graph:
  requires: []
  provides: [PI-wire-context-id, plugin-settings-unified-store]
  affects: [Inspector.qml, DeviceView.qml, pi_bridge.cpp, plugin_settings_store]
tech_stack:
  added: []
  patterns: [wire-context-id-derivation, plugin-settings-store-shared]
key_files:
  created: []
  modified:
    - src/app/qml/Inspector.qml
    - src/app/qml/DeviceView.qml
    - src/app/src/pi_bridge.cpp
    - tests/unit/test_pi_bridge.cpp
    - tests/unit/CMakeLists.txt
decisions:
  - PI contextUuid now equals wire context id (device#root#Keypad#row#col) assembled from deviceCodename+keyIndex fed by DeviceView; no ProfileController lookup
  - PIBridge per-context setSettings/getSettings route through plugin_settings_store::writeContext/readContext; global settings remain on the existing PIBridge-local path
  - plugin_settings_store.cpp added to unconditional pi_bridge test block in CMakeLists to avoid AJAZZ_HAVE_WEBSOCKETS conditionality gap
metrics:
  duration: ~15min
  completed_date: '2026-06-02'
  tasks_completed: 2
  tasks_total: 3
  files_modified: 5
---

# Phase 29 Plan 02: Property Inspector Settings Context Unification Summary

**One-liner:** PI contextUuid now equals the wire context id (device#root#Keypad#row#col) by threading deviceCodename+keyIndex from DeviceView into Inspector, and PIBridge per-context persistence is routed through the shared plugin_settings_store so the PI and the plugin WebSocket path read/write one record.

## Tasks Executed

| Task           | Name                                                                | Commit   | Files                                             |
| -------------- | ------------------------------------------------------------------- | -------- | ------------------------------------------------- |
| 1              | Thread deviceCodename+keyIndex into Inspector; emit wire context id | ca4b801  | Inspector.qml, DeviceView.qml                     |
| 2              | Unify PIBridge per-context persistence onto plugin_settings_store   | 1846b41  | pi_bridge.cpp, test_pi_bridge.cpp, CMakeLists.txt |
| 3 (checkpoint) | Verify PI settings round-trip shares one store record               | DEFERRED | —                                                 |

## Context-id Derivation: Before / After

### Before (wrong — profileId + sanitized label)

File: `src/app/qml/Inspector.qml:102-107` (pre-patch)

```
function _contextUuid() {
    var pid = ProfileController.activeProfileId();   // e.g. "profile-abc"
    var sel = selectionLabel.replace(...);            // e.g. "Key_3"
    return pid + "_" + sel;                           // "profile-abc_Key_3"
}
```

This string was never aligned with the wire context id produced by
`ContextRegistry::deriveContextId` (`plugin_device_bridge.cpp:66-69`), so
`PIBridge::setSettings` wrote to a different file than the plugin's
`willAppear`/`getSettings` read from.

### After (correct — wire context id)

File: `src/app/qml/Inspector.qml:112-126` (post-patch)

```
function _contextUuid() {
    if (root.keyIndex < 0 || root.deviceCodename === "") { return ""; }
    var col = root.keyIndex % 5;
    var row = Math.floor(root.keyIndex / 5);
    return root.deviceCodename + "#root#Keypad#" + row + "#" + col;
}
```

**Worked example (key index 7 on akp05e):**

- col = 7 % 5 = 2, row = floor(7/5) = 1
- contextUuid = `"akp05e#root#Keypad#1#2"`
- On-disk file: `~/.local/share/ajazz-control-center/plugins/<uuid>/settings/akp05e#root#Keypad#1#2.json`

This matches `ContextRegistry::deriveContextId` exactly:
`ctx.deviceId + "#" + ctx.pageId + "#" + ctx.controller + "#" + row + "#" + column`
with `pageId="root"`, `controller="Keypad"`.

### How deviceCodename+keyIndex reach the Inspector

Inspector.qml previously had NO device codename or numeric key index — only
`selectionLabel` (a localized string like "Key 3"), `hasSelection`, and `binding`.

Fix in DeviceView.qml:

```qml
Inspector {
    // ... existing props ...
    deviceCodename: root.codename          // DeviceView.qml:449
    keyIndex: root.selectedKeyIndex        // DeviceView.qml:450
}
```

`root.codename` is the wire codename (e.g. "akp05e"), confirmed at
`DeviceView.qml:54`. `root.selectedKeyIndex` is the numeric index, at
`DeviceView.qml:56`. ProfileController was NOT used (it has no
`activeDeviceCodename` accessor — confirmed by grep).

## PIBridge Persistence Unification

### Before

`pi_bridge.cpp` had a LOCAL `perContextPath()` helper that computed:
`<AppDataLocation>/plugins/<uuid>/settings/<contextUuid>.json`

`setSettings` called it, then its own `writeJsonAtomic`. `getSettings` called
it, then its own `readJsonOrEmpty`. These matched the `plugin_settings_store`
layout but were independent implementations — if the store was refactored, the
two paths could diverge silently.

### After

`setSettings` calls `plugin_settings_store::writeContext(pluginUuid_, contextUuid_, json)`.
`getSettings` calls `plugin_settings_store::readContext(pluginUuid_, contextUuid_)`.

Both now use the canonical shared store implementation (same size cap, same
validation, same atomic-write pattern, same error logging). The local
`perContextPath()` function was removed. Global settings retain the existing
PIBridge-local path (per CONTEXT.md deferred note).

## T-29-03 Regression Test Added

File: `tests/unit/test_pi_bridge.cpp` (end of file)

```cpp
REQUIRE(isSafeComponent(QStringLiteral("akp05e#root#Keypad#1#2")));  // wire ctx id
REQUIRE(!isSafeComponent(QStringLiteral("../x")));                   // traversal
REQUIRE(!isSafeComponent(QStringLiteral("a/b")));                    // slash
REQUIRE(!isSafeComponent(QStringLiteral("a\\b")));                   // backslash
REQUIRE(!isSafeComponent(QStringLiteral("..")));                     // parent ref
REQUIRE(!isSafeComponent(QStringLiteral("")));                       // empty
```

This locks the `plugin_settings_store::isSafeComponent` contract: `'#'` (0x23)
is printable ASCII in [0x20,0x7e] and is NOT a path separator, so wire context
ids pass the sanitiser unchanged. No change to `isSafeComponent` itself was
required.

## Orchestrator PI Round-trip Verification Commands (for batched live session)

Live verification is DEFERRED to the batched session (29-04). The exact
`scripts/ajazz-debug` commands to run:

```bash
# 1. Launch (AKP05E + a PI-bearing plugin installed, e.g. System Monitor)
setsid env AJAZZ_DEBUG_CONTROL=1 build/linux-release/src/app/ajazz-control-center &
sleep 2

# 2. Bind a plugin action to key 7 (AKP05E index 7 = row 1, col 2)
#    (drag or commitKeyBinding via debug channel from Plan 29-01)
scripts/ajazz-debug qml.invoke DeviceView commitKeyBinding \
  '{"index":7,"iconSource":"","label":"CPU","actionKind":0,"actionParams":"","actionId":"com.ajazz.sysmon.cpu"}'

# 3. Select key 7 to open its PI
scripts/ajazz-debug qml.set keyCellGrid 7 selected true
# (or click the key via input.pointer from Plan 29-01)

# 4. Confirm the PI loaded with the wire context id (check logs for "loadInspector")
scripts/ajazz-debug log.tail 20

# 5. Set a value in the PI (via qml.set on a PI input field, if accessible,
#    or interact with the PI web page and have it call $SD.setSettings)

# 6. Read the on-disk record to confirm it was written at the wire-context path:
ls ~/.local/share/ajazz-control-center/plugins/<uuid>/settings/
#    Expected: "akp05e#root#Keypad#1#2.json"
cat ~/.local/share/ajazz-control-center/plugins/<uuid>/settings/akp05e#root#Keypad#1#2.json

# 7. Confirm the plugin received it via didReceiveSettings:
scripts/ajazz-debug plugin.protocolLog
#    Expected: a didReceiveSettings entry with the same key/value
```

Pass criteria: the file exists at `akp05e#root#Keypad#1#2.json` (not
`profileId_Key_3.json`), contains the value set in the PI, and the plugin's
`didReceiveSettings` shows the same value.

## Build + Test Results

- `cmake --build build/linux-release --target ajazz-control-center ajazz_unit_tests`: 0 errors
- `ctest --preset linux-release -E qml`: 666/666 non-QML tests pass (9 QML "Not Run" — pre-existing harness gap, CLAUDE.md known issue; run with `-E qml` to skip)
- `grep -rn nlohmann src/core/include/` returns 3 lines — all doc comments, 0 actual includes (COD-031 clean)

## Deviations from Plan

None — plan executed exactly as written. The checkpoint (Task 3) is treated as
DEFERRED per the `<critical_deferral>` instruction (live verification batched to
29-04). Code is complete; live round-trip is pending the orchestrator's batched
session.

## Known Stubs

None. The data path is wired end-to-end at the code level; live verification
is deferred but the code is not stubbed.

## Threat Flags

None found beyond what the plan's threat model covers (T-29-03 addressed by
isSafeComponent regression test; T-29-04 accepted — path scoped under uuid).

## Self-Check: PASSED

- ca4b801 exists: confirmed (`git log --oneline | grep ca4b801`)
- 1846b41 exists: confirmed (`git log --oneline | grep 1846b41`)
- Inspector.qml declares `deviceCodename` + `keyIndex`: confirmed
- DeviceView.qml binds both: confirmed
- pi_bridge.cpp uses `plugin_settings_store::writeContext/readContext`: confirmed
- T-29-03 regression test in test_pi_bridge.cpp: confirmed
- Build: 0 errors
- Tests: 666/666 non-QML pass
