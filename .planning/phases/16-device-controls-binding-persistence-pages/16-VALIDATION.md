---
phase: 16
slug: device-controls-binding-persistence-pages
status: planned
nyquist_compliant: true
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

- **After every task commit:** the quick `-R` run above (+ `-R Profile` for persistence tasks)
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement | Threat Ref     | Secure Behavior                                                                                          | Test Type | Automated Command                                     | File Exists           | Status     |
| -------- | ---- | ---- | ----------- | -------------- | -------------------------------------------------------------------------------------------------------- | --------- | ----------------------------------------------------- | --------------------- | ---------- |
| 16-01-01 | 01   | 1    | DISPLAY-09  | T-16a-02/03/04 | setBrightness→LIG (clamped 0..100); clearAll→CLE; non-display device no-op; single QML_SINGLETON         | unit      | `ctest --preset linux-release -R StreamDockControls`  | ❌ W0                 | ⬜ pending |
| 16-01-02 | 01   | 1    | DISPLAY-09  | T-16a-01       | debounced slider (bounded LIG writes) + clear-all bound to the QML-exposed service                       | unit+qml  | \`ctest --preset linux-release -R "QmlSmoke           | StreamDockControls"\` | ❌ W0      |
| 16-02-01 | 02   | 1    | PROFILE-01  | T-16b-01/03/04 | default path (sanitized id, mkpath); commit→save→fresh-controller round-trip (keys/encoders/touch/pages) | unit      | `ctest --preset linux-release -R ProfilePersistence`  | ❌ W0                 | ⬜ pending |
| 16-02-02 | 02   | 1    | PROFILE-01  | T-16b-02/05    | KeyDesigner→Profile commit; Main Apply/Revert real save/load; loaded profile repaints (BAT+ULEND)        | unit      | \`ctest --preset linux-release -R "ProfilePersistence | StreamDockControl"\`  | ❌ W0      |
| 16-03-01 | 03   | 2    | PROFILE-02  | T-16c-01/03    | repaintPage("root"/child) paints the right page's bindings; missing page no-ops                          | unit      | \`ctest --preset linux-release -R "ProfilePages       | StreamDockControl"\`  | ❌ W0      |
| 16-03-02 | 03   | 2    | PROFILE-02  | T-16c-02/04/05 | pageNavRequested(±1) carousel switch+repaint; single-root no-op; no device opcode; reused engine         | unit      | `ctest --preset linux-release -R ProfilePages`        | ❌ W0                 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_stream_dock_controls.cpp` — brightness slider → LIG (clamped); clear-all → CLE (DISPLAY-09)
- [ ] `tests/unit/test_profile_persistence.cpp` — commit→save→fresh-controller round-trip (keys/encoders/touch/pages) + load→repaint (PROFILE-01)
- [ ] `tests/unit/test_profile_pages.cpp` — repaintPage(child) paints child bindings; pageNavRequested(±1) carousel switches + repaints; single-root no-op (PROFILE-02)
- [ ] Reuse `mock_transport.hpp` + `makeAkp05WithTransport` + control-service spy; no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                                                                         | Requirement                              | Why Manual                                   | Test Instructions                                                                    |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------ |
| Dragging the slider visibly changes panel brightness; clear-all blanks the panel; bindings survive a real restart and repaint; swipe changes pages on the device | DISPLAY-09, PROFILE-01/02 (live witness) | Requires the physical AKP05E + `uaccess` ACL | Deferred to Phase 25; MockTransport + round-trip tests are the Phase 16 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < ~15s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planner — 2026-05-23
