---
phase: 34
slug: per-app-profiles-event-parity-audit
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-06-08
---

# Phase 34 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Derived from `34-RESEARCH.md` § Validation Architecture (HIGH confidence).

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                                        |
| ---------------------- | ------------------------------------------------------------------------------------------------------------ |
| **Framework**          | Catch2 (`tests/unit/`) + integration (`tests/integration/`) + offscreen QML smoke (`tests/qml/`)             |
| **Config file**        | CTest preset `linux-release`                                                                                 |
| **Quick run command**  | `ctest --preset linux-release -R 'active_window\|app_profile_switch\|willappear_payload\|switch_to_profile'` |
| **Full suite command** | `ctest --preset linux-release`                                                                               |
| **Estimated runtime**  | quick ~5s · full ~60–90s (~408+ cases baseline, grows this phase)                                            |

______________________________________________________________________

## Sampling Rate

- **After every task commit:** Run the quick suite (new `active_window` / payload / dispatch cases)
- **After every plan wave:** Run `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** ~90 seconds (full suite)

______________________________________________________________________

## Per-Task Verification Map

| Req ID   | Behavior                                                                                   | Test Type        | Automated Command                                                                                   | File Exists        |
| -------- | ------------------------------------------------------------------------------------------ | ---------------- | --------------------------------------------------------------------------------------------------- | ------------------ |
| APROF-01 | Watcher interface + factory selects backend by session type; stub records                  | unit             | `ctest ... -R active_window`                                                                        | ❌ W0              |
| APROF-01 | Debounce coalesces rapid changes to one emit                                               | unit             | `ctest ... -R active_window_debounce`                                                               | ❌ W0              |
| APROF-02 | Match foreground app → applicationHints; default fallback; drives willDisappear→willAppear | unit/integration | `ctest ... -R app_profile_switch`                                                                   | ❌ W0              |
| APROF-02 | Switch latency < 500 ms device repaint                                                     | live             | debug-channel: synthetic foreground → screenshot, time repaint                                      | live               |
| APROF-03 | Warning chip visible + non-empty text when capability absent                               | live (QML)       | `qml.get waylandCapabilityWarningChip` → visible + warningText                                      | live               |
| APROF-03 | Assign-profile controls addressable + write applicationHints                               | live (QML)       | `qml.set` / `qml.click` on the assign-mapping controls                                              | live               |
| APROF-04 | applicationDidLaunch/Terminate delivered to subscribed plugins only                        | unit/integration | `ctest ... -R app_lifecycle_events`                                                                 | ❌ W0              |
| EVENT-01 | Coverage doc exists + complete vs OpenDeck/Elgato events                                   | doc review       | grep `docs/plugin-event-parity.md` for every SDK event                                              | ❌ W0 (doc)        |
| EVENT-02 | willAppear payload completeness                                                            | unit             | `ctest ... -R willappear_payload`                                                                   | ❌ W0              |
| EVENT-03 | switchToProfile dispatch activates profile                                                 | unit             | `ctest ... -R switch_to_profile`                                                                    | ❌ W0              |
| EVENT-03 | systemDidWakeUp dispatch (synthetic wake)                                                  | unit             | `ctest ... -R system_did_wake`                                                                      | ❌ W0              |
| EVENT-04 | No path emits "Knob" on the wire (all "Encoder")                                           | unit + grep      | `ctest ... -R controller_token` + `grep -rn '"Knob"' src/app/src/*.cpp` (expect docs/manifest only) | partial (grep now) |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/active_window_watcher_test.cpp` — factory/backend-selection/debounce (APROF-01)
- [ ] `tests/unit/app_profile_switch_test.cpp` — hint match + default fallback + lifecycle drive (APROF-02)
- [ ] `tests/unit/app_lifecycle_events_test.cpp` — applicationDidLaunch/Terminate fan-out (APROF-04)
- [ ] `tests/unit/willappear_payload_test.cpp` — payload/envelope completeness (EVENT-02)
- [ ] `tests/unit/switch_to_profile_test.cpp` + `tests/unit/system_did_wake_test.cpp` (EVENT-03)
- [ ] `tests/unit/controller_token_test.cpp` — Knob→Encoder normalization (EVENT-04)
- [ ] `docs/plugin-event-parity.md` — coverage deliverable (EVENT-01)
- [ ] Test seam + debug RPC (`window.setForeground`) to inject a synthetic foreground-app change (mirror `input.*` synthetic RPC idiom) so APROF is testable without real focus changes and live-verifiable

______________________________________________________________________

## Manual-Only Verifications

| Behavior                   | Requirement | Why Manual                                                          | Test Instructions                                                                                                  |
| -------------------------- | ----------- | ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| X11 live switch            | APROF-02    | Real focus change under X11/XWayland                                | Force X11 backend, focus app A then B, confirm profile switch + repaint, `log.tail` shows willDisappear→willAppear |
| Wayland live switch (niri) | APROF-01/02 | Real compositor focus event                                         | Focus app A (mapped) then B; watcher emits app_id change; device repaints < 500 ms                                 |
| Graceful-degradation       | APROF-03    | Requires capability-absent compositor or forced `isActive()==false` | No crash; warning chip shows non-empty text; manual switching still works                                          |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 90s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
