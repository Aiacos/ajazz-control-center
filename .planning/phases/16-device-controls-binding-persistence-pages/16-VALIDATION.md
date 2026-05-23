---
phase: 16
slug: device-controls-binding-persistence-pages
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 16 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                 |
| ---------------------- | ----------------------------------------------------- |
| **Framework**          | Catch2 (C++ unit) under CMake/CTest                   |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)          |
| **Quick run command**  | \`ctest --preset linux-release -R "StreamDockControls |
| **Full suite command** | `ctest --preset linux-release`                        |
| **Estimated runtime**  | quick \<15s; full ~minutes                            |

Hardware-free: brightness/clear assert `LIG`/`CLE` writes via MockTransport / control-service
spy; persistence asserts a `Profile` round-trip through a fresh `ProfileController` (save → load →
equality) plus a repaint of saved keys; pages assert `repaintPage` paints the new page's bindings.
ASCII-only test names. Physical-device witness deferred to Phase 25.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner — one row per task. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                             | Test Type | Automated Command                                    | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ----------------------------------------------------------- | --------- | ---------------------------------------------------- | ----------- | ---------- |
| 16-01-01 | 01   | 1    | DISPLAY-09  | —          | brightness slider drives a LIG write with the dragged value | unit      | `ctest --preset linux-release -R StreamDockControls` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_stream_dock_controls.cpp` — brightness slider → LIG; clear-all → CLE (DISPLAY-09)
- [ ] `tests/unit/test_profile_persistence.cpp` — assign keys/encoders/touch, save, reload in fresh controller, assert round-trip + repaint (PROFILE-01)
- [ ] `tests/unit/test_profile_pages.cpp` — repaintPage(child) paints child bindings; pageNavRequested switches + repaints (PROFILE-02)
- [ ] Reuse `mock_transport.hpp` + control-service spy; no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                                                                         | Requirement                              | Why Manual                                   | Test Instructions                                                                    |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------ |
| Dragging the slider visibly changes panel brightness; clear-all blanks the panel; bindings survive a real restart and repaint; swipe changes pages on the device | DISPLAY-09, PROFILE-01/02 (live witness) | Requires the physical AKP05E + `uaccess` ACL | Deferred to Phase 25; MockTransport + round-trip tests are the Phase 16 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~15s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
