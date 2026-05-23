---
phase: 19
slug: device-plugin-bridge
status: draft
nyquist_compliant: false
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
| **Quick run command**  | \`ctest --preset linux-release -R "PluginDeviceBridge                                                    |
| **Full suite command** | `ctest --preset linux-release`                                                                           |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                               |

Hardware-free + real-plugin-free: a loopback `QWebSocket` test client stands in for the plugin
(no Phase-18 spawn), a control-service spy stands in for the device (no hardware), and canned
`DeviceEvent`s stand in for physical input. Live `.sdPlugin` + physical witness = Phase 25.
ASCII-only test names.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner — one row per task. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                 | Test Type | Automated Command                         | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ----------------------------------------------- | --------- | ----------------------------------------- | ----------- | ---------- |
| 19-01-01 | 01   | 1    | PLUGIN-10   | T-19-img   | malformed data-uri yields placeholder, no crash | unit      | `ctest --preset linux-release -R DataUri` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_plugin_device_bridge.cpp` — setImage(context,dataUri) paints the right 1-based key (control-service spy sees BAT+ULEND); keyDown delivered with 0-based coordinates {row=(k-1)/5,col=(k-1)%5}; dialRotate with signed ticks; willAppear with context on activation; PLUGIN-10
- [ ] `tests/unit/test_data_uri_image.cpp` — strip `data:` prefix + base64 decode + QImage::loadFromData; malformed -> placeholder, no crash
- [ ] Reuse the loopback client harness (test_sd_plugin_server.cpp), the control-service spy (Phase-14 tests), the Phase-15 input feed; no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                    | Requirement              | Why Manual                             | Test Instructions                                                                               |
| ----------------------------------------------------------------------------------------------------------- | ------------------------ | -------------------------------------- | ----------------------------------------------------------------------------------------------- |
| A real plugin paints a physical key via setImage and a physical key press / encoder turn reaches the plugin | PLUGIN-10 (live witness) | Needs Phase-18 spawn + physical AKP05E | Deferred to Phase 25 (VERIFY-06); the loopback+spy+input-feed test is the Phase 19 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~15s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
