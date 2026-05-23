---
phase: 19
slug: device-plugin-bridge
status: planned
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-23
---

# Phase 19 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                                    |
| ---------------------- | -------------------------------------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; loopback `QWebSocket` client + control-service spy + Phase-15 input feed |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                                             |
| **Quick run command**  | `ctest --preset linux-release -R PluginDeviceBridge`                                                     |
| **Full suite command** | `ctest --preset linux-release`                                                                           |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                               |

Hardware-free + real-plugin-free: a loopback `QWebSocket` test client stands in for the plugin
(no Phase-18 spawn), a control-service spy (MockTransport-backed `Akp05Device`) stands in for the
device (no hardware), and canned `DeviceEvent`s stand in for physical input. Live `.sdPlugin` +
physical witness = Phase 25 (VERIFY-06). ASCII-only test names.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R PluginDeviceBridge` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement | Threat Ref             | Secure Behavior                                                                                                 | Test Type   | Automated Command                                                    | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------------------- | --------------------------------------------------------------------------------------------------------------- | ----------- | -------------------------------------------------------------------- | ----------- | ---------- |
| 19-01-01 | 01   | 1    | PLUGIN-10   | T-19-SC                | STOP-gate confirms dep SUMMARY files present; bridge shell + ContextRegistry compile                            | build       | `cmake --build --preset linux-release --target ajazz-control-center` | ❌ W0       | ⬜ pending |
| 19-01-02 | 01   | 1    | PLUGIN-10   | T-19-img/coord/owner   | data-URI decode (malformed->fail, no crash), keyIndex\<->coords round-trip, longest-prefix owner                | unit        | `ctest --preset linux-release -R PluginDeviceBridge`                 | ❌ W0       | ⬜ pending |
| 19-02-01 | 02   | 2    | PLUGIN-10   | T-19-img/xplugin/stale | onSetImage decode->paint, context-ownership denial, placeholder no-crash                                        | unit/integ  | `ctest --preset linux-release -R PluginDeviceBridge`                 | ❌ W0       | ⬜ pending |
| 19-02-02 | 02   | 2    | PLUGIN-10   | T-19-xplugin/img       | loopback setImage->control-spy BAT+ULEND on right 1-based key; malformed->placeholder; cross-plugin denied      | integration | `ctest --preset linux-release -R PluginDeviceBridge`                 | ❌ W0       | ⬜ pending |
| 19-03-01 | 03   | 3    | PLUGIN-10   | T-19-leak/sock         | DeviceEvent->§4.4 envelope (keyDown coords, dialRotate signed ticks); unbound coord dropped                     | unit/integ  | `ctest --preset linux-release -R PluginDeviceBridge`                 | ❌ W0       | ⬜ pending |
| 19-03-02 | 03   | 3    | PLUGIN-10   | T-19-leak/owner/stale  | loopback receives keyDown(coords)/dialRotate(ticks)/willAppear(context); no cross-plugin leak; lifecycle retire | integration | `ctest --preset linux-release -R PluginDeviceBridge`                 | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_plugin_device_bridge.cpp` — the single test file grows across the three plans:
  19-01 lands the pure-helper unit cases (coordinate round-trip, data-URI decode incl. malformed,
  UUID-prefix resolution, registry lookups); 19-02 adds the loopback-client + control-spy inbound
  e2e (setImage paints the right 1-based key via BAT+ULEND, malformed->placeholder, cross-plugin
  denial, visual-family no-crash); 19-03 adds the outbound e2e (keyDown with 0-based coordinates,
  dialRotate with signed ticks, willAppear with context, unbound-coordinate drop / no leak).
- [ ] Register in `tests/unit/CMakeLists.txt` linking `plugin_device_bridge.cpp` + `sd_plugin_server.cpp`
  - `stream_dock_control_service.cpp` + `stream_dock_input_service.cpp` + the streamdeck device source,
    inside the `AJAZZ_HAVE_WEBSOCKETS` gate (mirror `test_sd_plugin_server` + the Phase-14 link blocks).
- [ ] Add `plugin_device_bridge.{cpp,hpp}` to `src/app/CMakeLists.txt` source + AUTOMOC blocks (QObject moc).
- [ ] Reuse the loopback client harness (`test_sd_plugin_server.cpp` `ensureQCoreApp`/`pump`/`waitForSpy`)
  - the Phase-14 control-service spy + a Phase-15 `DeviceEvent` feed; no new framework.

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                    | Requirement              | Why Manual                             | Test Instructions                                                                               |
| ----------------------------------------------------------------------------------------------------------- | ------------------------ | -------------------------------------- | ----------------------------------------------------------------------------------------------- |
| A real plugin paints a physical key via setImage and a physical key press / encoder turn reaches the plugin | PLUGIN-10 (live witness) | Needs Phase-18 spawn + physical AKP05E | Deferred to Phase 25 (VERIFY-06); the loopback+spy+input-feed test is the Phase 19 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < ~15s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planned 2026-05-23
