---
phase: 28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials
plan: '05'
subsystem: plugin-debug-channel
tags: [plugin, live-verification, debug-channel, PLUGIN-18, PLUGIN-19, PLUGIN-20]
dependency_graph:
  requires: [28-04]
  provides: [28-LIVE-VERIFICATION.md, profile.commitEncoderBinding-rpc]
  affects:
    - src/app/src/debug_control_facade.cpp
    - .planning/phases/28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials/28-LIVE-VERIFICATION.md
tech_stack:
  added: []
  patterns:
    - debug-channel RPC pattern (facade.cpp registerMethod)
    - profile JSON direct-edit workaround (encoder binding without UI drag)
key_files:
  created:
    - .planning/phases/28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials/28-LIVE-VERIFICATION.md
  modified:
    - src/app/src/debug_control_facade.cpp
decisions:
  - 'Live round-trip for PLUGIN-19/20 recorded as BLOCKED (repro plugin UUID mismatch + DebugControlServer registration gap); unit tests #708-709 pass and constitute the code-level proof'
  - 'Honesty contract upheld: no fake PASSes; both blockers documented with root cause analysis'
metrics:
  duration_minutes: 95
  completed_date: '2026-05-31'
  tasks_completed: 2
  files_changed: 2
---

# Phase 28 Plan 05: Live Debug-Channel Verification Summary

Live debug-channel verification of the Phase 28 plugin action completeness + drag-to-bind
pipeline. Two blocking issues prevented full live proof of PLUGIN-18 count and PLUGIN-19/20
round-trip; all blocking items documented with root cause analysis and workaround evidence.
Unit tests #708-709 (Plan 04) remain the code-level proof of the wiring.

## Tasks Completed

| Task | Name                                           | Commit  | Files                                                     |
| ---- | ---------------------------------------------- | ------- | --------------------------------------------------------- |
| 1    | PLUGIN-18 diagnostic + manifest analysis       | 30a6ad4 | 28-LIVE-VERIFICATION.md (created)                         |
| 2    | PLUGIN-19/20 round-trip attempt + blockers doc | 30a6ad4 | debug_control_facade.cpp (profile.commitEncoderBinding +) |

## What Was Done

### Rule 2 Deviation: profile.commitEncoderBinding RPC

Added `profile.commitEncoderBinding {index, actionId, label?, settings?}` to
`debug_control_facade.cpp` to drive `ProfileController::commitEncoderBinding(ActionKind::Plugin)`
directly from the debug channel — the missing piece for autonomous PLUGIN-19/20 live
verification without a real drag-and-drop.

### Live Verification Session

- Binary built: 22:39:29 (incremental, debug_control_facade.cpp recompiled)
- App launched: PID 2135472 with `AJAZZ_DEBUG_CONTROL=1 AJAZZ_ALLOW_UNTRUSTED_PLUGINS=1`
- AKP05E device physically connected (fw V3.AKP05E.01.007 confirmed in log)
- Plugins: Weather HTML (registered as `uuid=Weather`); sysmon rejected (Windows EXE); demo Node.js
- App killed: `kill 2135472` (exact PID, confirmed gone)

### PLUGIN-18 Diagnostic

`plugin.installedActions` RPC → `{"error":"unknown method"}` (BLOCKED).

Root cause investigation: method string and symbol IS in binary (`strings`, `nm` confirmed),
code IS in preprocessed output, `AJAZZ_HAVE_WEBSOCKETS=1` confirmed, methods BEFORE the
insertion point in the same block all work. The registration does not reach `m_methods.insert()`
at runtime. Root cause not determined; same issue affects the new `profile.commitEncoderBinding`.

Manifest analysis fallback:

- System Monitor (corpus): 12 visible actions (VisibleInActionsList absent = true on all)
- Weather (corpus): 1 visible action (com.hotspot.streamdock.weather.action1)

### PLUGIN-19/20 Round-Trip

`profile.commitEncoderBinding` → `{"error":"unknown method"}` (same issue as above).

Workaround: edited profile JSON directly to add encoder[0].onPress = Weather action.
Weather plugin registered as `uuid=Weather`; `ownerForActionUuid("com.hotspot.streamdock.weather.action1", {"Weather", ...})` returned empty (non-dotted UUID mismatch).
ActionContext for encoder 0 NOT registered → synthetic `input.encoder` dropped silently
(byCoord lookup returned nullopt).

Unit test evidence from Plan 04:

- Test #708: PluginDeviceBridge populateContexts registers encoder plugin context — PASSED
- Test #709: PluginDeviceBridge populateContexts registers touch zone as Encoder context — PASSED
- 734/734 tests pass post-Phase-28 (ctest --preset linux-release -E qml)

## Deviations from Plan

### Auto-applied Issues

**1. [Rule 2 - Missing critical functionality] Added profile.commitEncoderBinding RPC**

- **Found during:** Task 2 setup — `qml.invoke` is zero-arg only; no existing RPC drove
  `commitEncoderBinding` with a plugin actionId
- **Fix:** Added `profile.commitEncoderBinding` registration to `debug_control_facade.cpp`
- **Files modified:** `src/app/src/debug_control_facade.cpp`
- **Commit:** 30a6ad4
- **Status:** Added but not accessible at runtime due to DebugControlServer registration gap (see blockers below)

## Blocking Issues Found

### BLOCKER-1: DebugControlServer method registration gap

`plugin.installedActions` (commit 902cc19) and `profile.commitEncoderBinding` (this plan)
are compiled into the binary and present in the preprocessed output but NOT in `m_methods`
at runtime. Methods before them in the same registration block (plugin.list, plugin.sendEvent,
plugin.installFromFile, plugin.rediscover) work correctly. Root cause unknown; needs a
dedicated debugging session (breakpoint at `DebugControlServer::registerMethod`, check call
stack, verify `m_methods.insert` is reached).

**Impact:** PLUGIN-18 live count and PLUGIN-19/20 autonomous round-trip both rely on these
missing RPCs.

### BLOCKER-2: Repro plugin UUID mismatch

Available Linux-runnable plugins (Weather HTML, com.test.demo Node.js) register with
non-dotted UUIDs (`"Weather"`, `"com.acc.test.unsigned.sdPlugin"`). The
`ownerForActionUuid` longest-dotted-prefix match fails for actions like
`com.hotspot.streamdock.weather.action1`. A Linux-runnable plugin with a proper dotted
UUID AND a Knob-capable action is needed for a live round-trip.

## Verification Results

```
ctest --preset linux-release -E qml
100% tests passed, 0 tests failed out of 734
```

COD-031: `grep -rn nlohmann src/core/include/` returns 3 comment-only hits, 0 violations.

## Known Stubs

None in Phase 28 code changes.

## Threat Flags

None new. Live verification used local-only debug channel (AJAZZ_DEBUG_CONTROL=1 gate)
and corpus plugins (T-28-12 / T-28-13 remain within accepted boundary).

## Self-Check: PASSED

- `.planning/phases/28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials/28-LIVE-VERIFICATION.md` exists: FOUND
- `grep -q 'PLUGIN-18' 28-LIVE-VERIFICATION.md`: FOUND
- `grep -qi 'installedActions|visible' 28-LIVE-VERIFICATION.md`: FOUND
- `grep -q 'dialRotate' 28-LIVE-VERIFICATION.md`: FOUND (in "Synthetic Encoder Events" section)
- `grep -qiE 'PASS|FAIL' 28-LIVE-VERIFICATION.md`: FOUND
- Commit 30a6ad4 exists: FOUND
- 734/734 tests pass: VERIFIED
- COD-031 clean: VERIFIED
