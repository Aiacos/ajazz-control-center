---
phase: 34-per-app-profiles-event-parity-audit
verified: 2026-06-08T18:10:00Z
status: human_needed
score: 5/5 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: none
  previous_score: n/a
human_verification:
  - test: 'Wayland live focus-walk on niri: focus a mapped app A, then app B; confirm the watcher emits the app_id change and the device repaints within 500ms; log.tail shows willDisappear (outgoing) then willAppear (incoming) in order.'
    expected: Device repaints with the matched profile <500ms; ordered willDisappear->willAppear in log.tail.
    why_human: Real wlr compositor focus events cannot be synthesized on this machine; window.setForeground only drives the StubActiveWindowWatcher, not the live wlr backend.
  - test: 'X11/XWayland live switch: force the X11 backend, focus app A then app B, confirm the auto-switch + repaint and ordered willDisappear->willAppear.'
    expected: Profile auto-switches on the mapped app gaining focus; bridge lifecycle fires in order.
    why_human: Requires a real X11 focus change and a live device; not automatable headless.
  - test: 'Graceful degradation on a capability-absent compositor (GNOME/KDE without wlr, or forced isActive()==false): confirm no crash, the waylandCapabilityWarningChip shows non-empty amber warning text, and manual profile switching still works.'
    expected: No crash/silent break; capability-warning chip visible with non-empty warningText; manual switching unaffected.
    why_human: Requires a non-wlr compositor or a live capability-absent session; the synthetic setForegroundCapabilityAvailable seam covers the chip binding but not a real degradation walk.
  - test: 'EVENT dispatch end-to-end via a registered plugin over the live WS: trigger switchToProfile, systemDidWakeUp, applicationDidLaunch/Terminate and confirm each appears in plugin.protocolLog at the subscribed plugin.'
    expected: Each host command/event is observed in plugin.protocolLog at the registered/subscribed plugin only.
    why_human: Requires a live WebSocket plugin session; unit tests prove the fan-out helpers but not the over-the-wire delivery on a running app.
  - test: Retail-device repaint latency (<500ms) for the auto-switch path.
    expected: Real device repaints within 500ms of the foreground change.
    why_human: 'Hardware-gated: the only available unit is the 0x3004 demo (zero input, no retail AKP05E); cannot measure live repaint latency.'
  - test: systemDidWakeUp from a real OS source (logind PrepareForSleep D-Bus) — confirm the dispatch fires on actual suspend/resume.
    expected: Real OS wake triggers systemDidWakeUp to subscribed plugins.
    why_human: OS-source wiring (logind D-Bus) is deferred; dispatch is unit-proven but the OS event source is not wired this phase.
  - test: WR-04 Win32 recycled-HWND dedup correctness on a real Windows host.
    expected: A foreground change to a process that reuses a previously-seen HWND is NOT suppressed (dedup on resolved appId).
    why_human: Compile-only on Linux; no Windows host this phase. Flag for the Phase 35 Windows walk.
---

# Phase 34: Per-App Profiles + Event-Parity Audit Verification Report

**Phase Goal:** Profiles auto-switch when the foreground application changes; a full event-parity coverage table is produced; all non-hardware-gated event gaps are closed.
**Verified:** 2026-06-08T18:10:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

All five ROADMAP success criteria have real, substantive code backing and green
automated coverage for their non-gated portions. Every requirement ID
(APROF-01..04, EVENT-01..04) is accounted for with verified artifacts. The two
review findings flagged as goal-relevant (the CR-01 X11 crash BLOCKER and the
WR-01 default-fallback clobber) are confirmed FIXED in source. The remaining
success-criterion clauses are the inherently live/hardware items the phase
explicitly scoped to human/hardware verification — these route to
`human_verification`, not to gaps.

### Observable Truths (ROADMAP Success Criteria)

| #   | Truth (Success Criterion)                                                                                                 | Status                                              | Evidence                                                                                                                                                                                                                                                                                            |
| --- | ------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | X11 foreground app matching `applicationHints` repaints device \<500ms; log shows willDisappear+willAppear                | ✓ VERIFIED (code+unit) / human (live latency)       | Auto-switch wired at `application.cpp:1200-1201` (`resolveProfileForApp(..., allowDefaultFallback=false)`); drives existing `profileChanged->populateContextsForActivePage` reconcile; `test_app_profile_switch` 26 cases PASS. Live \<500ms repaint = human.                                       |
| 2   | Wayland/GNOME capability-absent → UI capability-warning chip shows non-empty text; no crash/silent break                  | ✓ VERIFIED (code) / human (live walk)               | `waylandCapabilityWarningChip` (SettingsPage.qml:209-218) bound to `ProfileController.foregroundCapabilityWarning`; factory degrades gracefully (`capabilityAvailable()==false`, no crash); `setForegroundCapabilityAvailable` seam at debug_control_facade.cpp:391. Real degradation walk = human. |
| 3   | `docs/plugin-event-parity.md` classifies every OpenDeck/Elgato event supported/partial/missing/hardware-gated + test refs | ✓ VERIFIED                                          | Doc present (183 lines), 52 classification markers, willAppear x4, 3 test refs, Knob\<->Encoder section §35-42.                                                                                                                                                                                     |
| 4   | `willAppear` payload completeness asserted by Catch2; controller-token normalization audited                              | ✓ VERIFIED                                          | `willappear_payload_test` 27 assertions PASS (envelope action/context/device/event + payload row/column/controller/state/isInMultiAction); `controller_token_test` 13 PASS; grep confirms NO stray `"Knob"` wire emission outside plugin_manifest.                                                  |
| 5   | `systemDidWakeUp`+`switchToProfile` dispatch + `applicationDidLaunch/Terminate` fire from watcher, unit-tested            | ✓ VERIFIED (unit) / human (live WS, OS wake source) | `test_switch_to_profile` 16, `test_system_did_wake` 13, `test_app_lifecycle_events` 26 — all PASS; registered-only + ApplicationsToMonitor-filtered + length-bounded confirmed in source (application.cpp:1250-1282). Live WS + real OS wake = human.                                               |

**Score:** 5/5 truths verified (automated portions); live/hardware clauses routed to human_verification.

### Required Artifacts

| Artifact                                                                                                                     | Expected                                      | Status     | Details                                                                                                                                                                     |
| ---------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------- | ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/core/include/ajazz/core/active_window_watcher.hpp`                                                                      | IActiveWindowWatcher (Qt-free, nlohmann-free) | ✓ VERIFIED | 187 lines; COD-031 clean (only doc-comment mentions of nlohmann).                                                                                                           |
| `src/core/src/active_window_watcher_stub.cpp`                                                                                | Recording stub + default factory              | ✓ VERIFIED | 93 lines; `injectForeground` seam used by debug RPC.                                                                                                                        |
| `src/app/src/active_window_watcher_wayland.cpp`                                                                              | wlr-foreign-toplevel backend                  | ✓ VERIFIED | Binds `zwlr_foreign_toplevel_manager_v1`; `isActive()`->`capabilityAvailable()`; WR-03 empty-appId guard present (:140).                                                    |
| `src/app/src/active_window_watcher_x11.cpp`                                                                                  | X11/EWMH backend                              | ✓ VERIFIED | `_NET_ACTIVE_WINDOW`+WM_CLASS; **CR-01 fixed**: `nonFatalXErrorHandler` via `XSetErrorHandler`/`std::call_once` (:63-105).                                                  |
| `src/app/src/active_window_watcher_win.cpp`                                                                                  | Win32 GetForegroundWindow (compile-guarded)   | ✓ VERIFIED | Present; WR-04 HWND-dedup flagged for Phase 35 Windows walk.                                                                                                                |
| `src/app/src/active_window_watcher_mac.mm`                                                                                   | macOS NSWorkspace (compile-guarded)           | ✓ VERIFIED | Present (135 lines).                                                                                                                                                        |
| `src/app/src/active_window_watcher_factory.cpp`                                                                              | Runtime wayland/x11 selection                 | ✓ VERIFIED | `isWaylandSession()` via `platformName()`/`XDG_SESSION_TYPE` runtime, not compile-time.                                                                                     |
| `src/app/src/active_window_debounce.hpp`                                                                                     | Debounce (~150-250ms)                         | ✓ VERIFIED | Present (96 lines); idempotent appId dedup.                                                                                                                                 |
| `docs/plugin-event-parity.md`                                                                                                | EVENT-01 coverage table                       | ✓ VERIFIED | 183 lines, full classification + test refs.                                                                                                                                 |
| `src/app/qml/SettingsPage.qml`                                                                                               | Assign-profile surface + warning chip         | ✓ VERIFIED | objectNames on all controls; Button/SecondaryButton (not Switch); chip wired to capability prop.                                                                            |
| `src/app/src/profile_controller.{hpp,cpp}`                                                                                   | applicationHints writer + capability property | ✓ VERIFIED | Q_INVOKABLE `setApplicationHints`/`addAppProfileMapping`/`removeAppProfileMapping`; Q_PROPERTY `foregroundCapabilityWarning`; `resolveProfileForApp(allowDefaultFallback)`. |
| test_active_window_watcher.cpp (49)                                                                                          | APROF-01 factory/stub/debounce                | ✓ VERIFIED | Registered + 36/36 phase tests PASS. NOTE: planner named `*_test.cpp`; actual files use `test_*` prefix — naming deviation, coverage intact.                                |
| test_app_profile_switch / app_lifecycle_events / switch_to_profile / system_did_wake / willappear_payload / controller_token | APROF-02/04, EVENT-02/03/04                   | ✓ VERIFIED | All registered in tests/unit/CMakeLists.txt; all PASS.                                                                                                                      |

### Key Link Verification

| From                                      | To                                       | Via                                                | Status  | Details                                                              |
| ----------------------------------------- | ---------------------------------------- | -------------------------------------------------- | ------- | -------------------------------------------------------------------- |
| IActiveWindowWatcher onChange             | ProfileController::resolveProfileForApp  | applicationHints match, allowDefaultFallback=false | ✓ WIRED | application.cpp:1200-1201 (WR-01 fix applied).                       |
| SdPluginServer actionReceived             | switchToProfile activation               | auth-gated, length-bounded lookup                  | ✓ WIRED | application.cpp:625-657; auth+trim+lookup-only.                      |
| watcher / wake source                     | sendEvent (registered-only)              | applicationDidLaunch/Terminate, systemDidWakeUp    | ✓ WIRED | application.cpp:1250-1282; ApplicationsToMonitor filter (WR-02 fix). |
| waylandCapabilityWarningChip.visible      | capabilityAvailable()                    | ProfileController.foregroundCapabilityWarning      | ✓ WIRED | SettingsPage.qml:209-218.                                            |
| appProfileAppNameField/Selector           | Profile::applicationHints                | addAppProfileMapping Q_INVOKABLE                   | ✓ WIRED | SettingsPage.qml:401-412 -> profile_controller.cpp.                  |
| debug_control_facade window.setForeground | StubActiveWindowWatcher injectForeground | registerMethod seam                                | ✓ WIRED | debug_control_facade.cpp:354-370.                                    |

### Behavioral Spot-Checks

| Behavior                                   | Command                                                                    | Result                  | Status         |
| ------------------------------------------ | -------------------------------------------------------------------------- | ----------------------- | -------------- |
| Phase-34 unit suites pass                  | `ctest --preset linux-release -R "active_window\|...\|controller_token"`   | 36/36 passed            | ✓ PASS         |
| COD-031 boundary: no nlohmann in interface | `grep -c nlohmann active_window_watcher.hpp` (matches = doc comments only) | 2 (comments)            | ✓ PASS         |
| EVENT-04: no stray "Knob" wire emission    | \`grep -rn '"Knob"' src/                                                   | grep -v plugin_manifest | grep -v test\` |

### Requirements Coverage

| Requirement | Source Plan  | Description                                            | Status      | Evidence                                                               |
| ----------- | ------------ | ------------------------------------------------------ | ----------- | ---------------------------------------------------------------------- |
| APROF-01    | 34-01, 34-03 | IActiveWindowWatcher + 4 backends + factory + debounce | ✓ SATISFIED | Interface+4 backends+factory present; test_active_window_watcher PASS. |
| APROF-02    | 34-04        | Auto-switch via applicationHints + lifecycle drive     | ✓ SATISFIED | application.cpp wiring + test_app_profile_switch PASS.                 |
| APROF-03    | 34-05        | Assign-profile UI + capability-warning chip            | ✓ SATISFIED | SettingsPage.qml + ProfileController writers/property.                 |
| APROF-04    | 34-04        | applicationDidLaunch/Terminate to subscribed plugins   | ✓ SATISFIED | Registered-only + ApplicationsToMonitor filter; test PASS.             |
| EVENT-01    | 34-02        | Event-parity coverage table                            | ✓ SATISFIED | docs/plugin-event-parity.md complete.                                  |
| EVENT-02    | 34-02        | willAppear payload completeness test                   | ✓ SATISFIED | willappear_payload_test 27 assertions PASS.                            |
| EVENT-03    | 34-04        | switchToProfile + systemDidWakeUp dispatch             | ✓ SATISFIED | test_switch_to_profile + test_system_did_wake PASS.                    |
| EVENT-04    | 34-02        | Knob->Encoder normalization audit                      | ✓ SATISFIED | controller_token_test PASS + grep confirms no stray Knob.              |

No orphaned requirements: all 8 IDs mapped in REQUIREMENTS.md to Phase 34 (Complete) and claimed by plans.

### Anti-Patterns Found

| File   | Line | Pattern                                                            | Severity | Impact |
| ------ | ---- | ------------------------------------------------------------------ | -------- | ------ |
| (none) | —    | No TBD/FIXME/XXX debt markers in any Phase-34 modified source file | —        | Clean  |

### Review-Finding Resolution

| Finding                                                     | Severity     | Status in source | Evidence                                                                                                                     |
| ----------------------------------------------------------- | ------------ | ---------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| CR-01: X11 no Xlib error handler (crash on stale window id) | BLOCKER      | ✓ FIXED          | `nonFatalXErrorHandler` returns 0; installed via `std::call_once`/`XSetErrorHandler` (active_window_watcher_x11.cpp:63-105). |
| WR-01: default-fallback clobbers manual selection           | WARNING      | ✓ FIXED          | `allowDefaultFallback=false` on the focus path (application.cpp:1201; profile_controller.cpp:393-394 returns {}).            |
| WR-02: launch/terminate ignores ApplicationsToMonitor       | WARNING      | ✓ FIXED          | `appMonitorFilter()` gates fan-out (application.cpp:1270-1282); test "ApplicationsToMonitor filter gates delivery" PASS.     |
| WR-03: Wayland empty-appId not guarded                      | WARNING      | ✓ FIXED          | empty-appId guard mirrored from X11 (active_window_watcher_wayland.cpp:140).                                                 |
| WR-04: Win32 HWND-only dedup                                | WARNING      | ⚠ DEFERRED       | Compile-only (no Windows host); routed to Phase 35 Windows walk.                                                             |
| WR-05 / IN-01..03                                           | WARNING/INFO | informational    | Non-blocking robustness/quality notes.                                                                                       |

### Human Verification Required

See frontmatter `human_verification` — 7 items, all live-compositor / live-WS /
OS-source / hardware-gated. These are the success-criterion clauses that cannot
be automated on this machine (no retail device, synthetic seam drives the stub
not the wlr backend, no Windows host, no real OS wake source). They are NOT
gaps — the code and unit coverage for the automatable portions are all green.

### Gaps Summary

No gaps. All 8 requirements have real, substantive code and green automated
coverage for the non-gated portions. The BLOCKER (CR-01) and the goal-relevant
WARNING (WR-01) from the code review are confirmed fixed in source. Status is
`human_needed` solely because the live focus-walk, live-WS dispatch, real OS
wake source, and retail-device repaint-latency items require human/hardware
verification that cannot run headless on this machine.

______________________________________________________________________

_Verified: 2026-06-08T18:10:00Z_
_Verifier: Claude (gsd-verifier)_
