---
phase: 32
slug: binding-layer-fix-multi-toggle-action-device-editor
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-06-08
---

# Phase 32 — Validation Strategy

> Per-phase validation contract. Derived from 32-RESEARCH.md "Validation Architecture".
> NOTE: ctest green is necessary but NOT sufficient — CLAUDE.md mandates live debug-channel
> verification for every behavioral/UI change. The phase gate requires BOTH.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                                                                            |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Framework**          | Catch2 v3 (`ajazz_unit_tests`) for core/app units; QML offscreen harness (`tests/qml/`) for editor; `scripts/ajazz-debug` for live wiring/render |
| **Config file**        | CMake presets; `catch_discover_tests(ajazz_unit_tests)`                                                                                          |
| **Quick run command**  | `ctest --preset linux-release -R "action\|builtin\|plugin_device\|device_view" --output-on-failure`                                              |
| **Full suite command** | `ctest --preset linux-release`                                                                                                                   |
| **Estimated runtime**  | quick \<15s; full ~2 min                                                                                                                         |

______________________________________________________________________

## Sampling Rate

- **After every task commit:** `ctest --preset linux-release -R "<touched suite>" --output-on-failure`
- **After every plan wave:** `ctest --preset linux-release` (full)
- **Phase gate:** Full suite green AND the live debug-channel acceptance criteria (32-UI-SPEC scroll
  checks + willAppear/Toggle/Multi live checks) pass. ctest necessary, not sufficient.
- **Max feedback latency:** ~15s (quick run).

______________________________________________________________________

## Per-Task Verification Map

| Req ID     | Behavior                                                                                                               | Test Type   | Automated Command                                                                                                                                                                           | File                                             |
| ---------- | ---------------------------------------------------------------------------------------------------------------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| BIND-03    | envelope carries `action`; stored-owner resolves; drag-drop fires willAppear same interaction (no reconnect)           | unit + live | `ctest --preset linux-release -R plugin_device` + live `plugin.protocolLog` after `qml.drag`→key                                                                                            | extend `test_plugin_device_bridge.cpp`           |
| BIND-04/05 | children→chain order + inter-step delay (delayMs on ActionInstance)                                                    | unit + live | `ctest -R "action_engine\|action_instance\|builtin\|multiaction"` + live `input.key` on 2-child Multi → ordered protocolLog with gap                                                        | new `test_multiaction_dispatch.cpp` (+extend)    |
| BIND-06    | key/encoder/touch each route to registered context, no SKU branch                                                      | unit + live | `ctest -R plugin_device` (parametrized by controller type) + live `input.key`/`input.encoder`/`input.touch` → protocolLog                                                                   | extend `test_plugin_device_bridge.cpp`           |
| BIND-07    | currentState cycles mod N (N>2); per-state visual renders; state-change willAppear; persists to profile                | unit + live | `ctest -R "toggle\|action_instance"` + live bind 3-state toggle → `input.key`×3 → `screenshot` per press (face changes + wraps)                                                             | new `test_toggle_dispatch.cpp`                   |
| EDIT-01    | scroll container addressable; over/under threshold; no horizontal; position readable; delegates distinct; trash pinned | qml + live  | `ctest -R device_view` (Linux/macOS only) + live `device.setActiveDevice` over/under threshold → `qml.get deviceCanvasScroll` (`ScrollBar.vertical.size < 1.0` ⇔ scrollable) + `screenshot` | extend `tests/qml/test_device_view_geometry.qml` |

*Status: ⬜ pending. Plan/wave split + final task IDs finalized by the planner.*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_multiaction_dispatch.cpp` (or extend `test_builtin_actions.cpp`) — BIND-04/05:
  the `ActionInstance.children → ActionChain` adapter (order + delay placement) with a recording executor.
- [ ] `tests/unit/test_toggle_dispatch.cpp` (or extend `test_action_instance.cpp` + `test_plugin_device_bridge.cpp`)
  — BIND-07: cycle-mod-N for N>2 + the setState render seam from `instance.states[idx]` + currentState persistence.
- [ ] Extend `tests/qml/test_device_view_geometry.qml` for EDIT-01 over/under-threshold +
  `ScrollBar.vertical.size`/`contentY` readability + trash-pinned + delegate-distinctness assertions.
- [ ] No framework install needed — Catch2 + QML harness already wired.

______________________________________________________________________

## Manual / Live-Only Verifications

| Behavior                                        | Requirement | Why live                                 | Test Instructions                                                                                                    |
| ----------------------------------------------- | ----------- | ---------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| willAppear arrives in-interaction, no reconnect | BIND-03     | wiring bug class invisible to unit tests | launch `AJAZZ_DEBUG_CONTROL=1`, install System Monitor, `qml.drag` action→key, `plugin.protocolLog` shows willAppear |
| Toggle key face changes per press               | BIND-07     | render path needs visual confirmation    | bind 3-state toggle, `input.key`×3, `screenshot` after each                                                          |
| Editor scrolls past threshold                   | EDIT-01     | scroll behavior is visual + interactive  | `device.setActiveDevice` to a >8col/>4row SKU, `qml.get deviceCanvasScroll`, `screenshot`                            |

______________________________________________________________________

## Validation Sign-Off

- [x] All requirements have an `<automated>` verify or Wave 0 dependency
- [x] Sampling continuity: every requirement has a quick command
- [x] Wave 0 covers all MISSING test references
- [x] No watch-mode flags
- [x] Live debug-channel checks enumerated (mandatory per CLAUDE.md)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
