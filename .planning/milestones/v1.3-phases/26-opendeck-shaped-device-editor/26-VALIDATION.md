---
phase: 26
slug: opendeck-shaped-device-editor
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-05-28
---

# Phase 26 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Cross-references `26-RESEARCH.md` "## Validation Architecture" for rationale.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                               |
| ---------------------- | ----------------------------------------------------------------------------------- |
| **Framework**          | Catch2 v3 (vendored at `external/Catch2/`)                                          |
| **Config file**        | `CMakePresets.json` (preset `linux-release`) + `tests/unit/CMakeLists.txt`          |
| **Quick run command**  | `ctest --preset linux-release -E qml -R "geom\|profile_serial" --output-on-failure` |
| **Full suite command** | `ctest --preset linux-release -E qml --output-on-failure`                           |
| **Estimated runtime**  | ~5s quick / ~30s full (unit + integration, excluding QML offscreen)                 |

> **Note on `tests/qml/`:** the QML offscreen harness has a pre-existing latent link issue
> (`PluginDeviceBridge::onPluginRegistered`/`onPluginDisconnected`/`onActivePageChanged`
> undefined-reference) — see CLAUDE.md "Latent items". The Phase 26 plan decides whether
> to fix the link or run the new QML tests through a slimmer target. Until then, the QML
> suite runs with `-E qml` to skip it.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** `ctest --preset linux-release -E qml -R "geom|profile_serial" --output-on-failure`
- **After every plan wave:** `ctest --preset linux-release -E qml --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green AND operator UAT (REQ-26-E) must be re-walked on the live AKP05E with PASS recorded in `25-UAT.md`.
- **Max feedback latency:** ~30s for the full automated suite; REQ-26-E is operator-gated and may take ~5 min of human interaction time.

______________________________________________________________________

## Per-Task Verification Map

> Task IDs are placeholders pending plan-phase output (`26-NN-MM-{task-slug}`). The planner
> will refine this table to match the actual wave/plan structure. The REQ → test mapping
> below is authoritative.

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                                                       | Test Type           | Automated Command                                                                                | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ------------------------------------------------------------------------------------- | ------------------- | ------------------------------------------------------------------------------------------------ | ----------- | ---------- |
| 26-NN-MM | NN   | 1    | REQ-26-A    | —          | Sidebar selection produces a real device-open (not a no-op); no leaked handle on swap | grep + manual + UAT | `grep -n "setActiveDevice" src/app/qml/Main.qml` + REQ-26-E live walk                            | ❌ W0       | ⬜ pending |
| 26-NN-MM | NN   | 2    | REQ-26-C    | —          | DeviceDescriptor extension is additive with zero defaults (no breaking change)        | unit (compile)      | `cmake --build --preset linux-release --target ajazz_core` (must compile)                        | ❌ W0       | ⬜ pending |
| 26-NN-MM | NN   | 2    | REQ-26-D    | —          | LCD-key SKUs declare geometry; AKP815 deferred via explicit allow-list                | unit (Catch2)       | `ctest --preset linux-release -R Geometry --output-on-failure`                                   | ❌ W0       | ⬜ pending |
| 26-NN-MM | NN   | 3    | D-11/D-12   | —          | v1 profile JSON loads as v2 with empty `touchZones`; v2 round-trips cleanly           | unit (Catch2)       | `ctest --preset linux-release -R profile_serial --output-on-failure`                             | ❌ W0       | ⬜ pending |
| 26-NN-MM | NN   | 4    | REQ-26-B    | —          | DeviceView renders 3 stacked rows discriminated by descriptor; KeyDesigner deleted    | QML offscreen       | `ctest --preset linux-release -R device_view --output-on-failure` (after QML link fixed/slimmed) | ❌ W0       | ⬜ pending |
| 26-NN-MM | NN   | 4    | REQ-26-B    | —          | Drag from ActionLibraryPane tile to each cell type fires the correct commit method    | QML offscreen       | `ctest --preset linux-release -R drag_drop --output-on-failure`                                  | ❌ W0       | ⬜ pending |
| 26-NN-MM | NN   | 6    | REQ-26-E    | —          | Tests 1 + 6 of `25-UAT.md` PASS on AKP05E demo unit `0300:3004`                       | manual (hardware)   | `grep "result: PASS" .planning/phases/25-*/25-UAT.md \| wc -l` ≥ 5                               | n/a         | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

Wave 0 is **trivial for this phase** — the test infrastructure (Catch2 + ctest + offscreen QML harness) is already in tree. Plans may inline test scaffolding inside the same task that introduces the production code (RED → GREEN within one wave), or split into a "test stub" task in Wave 2/4. The planner decides.

- [x] Catch2 v3 vendored at `external/Catch2/`
- [x] `tests/unit/CMakeLists.txt` registers unit-test target `ajazz_unit_tests`
- [x] `tests/qml/CMakeLists.txt` registers QML test target `ajazz_qml_tests` (excluded via `-E qml` until link issue fixed)
- [x] CMake preset `linux-release` (and matching `windows-release`, `macos-release` for cross-platform CI)
- [ ] **NEW:** `tests/unit/test_streamdeck_register_geometry.cpp` — REQ-26-D regression. Owner: planner (Wave 2).
- [ ] **NEW:** `tests/qml/test_device_view_geometry.qml` (or `.cpp` driver) — REQ-26-B coverage. Owner: planner (Wave 4).
- [ ] **NEW:** `tests/qml/test_device_view_drag_drop.qml` — drag-drop grammar coverage. Owner: planner (Wave 4).
- [ ] **EXTEND:** `tests/unit/test_profile_serialiser.cpp` — schema v1 → v2 migration. Owner: planner (Wave 3).

______________________________________________________________________

## Cross-cutting Constraints

- `ctest --preset linux-release -E qml` MUST remain ≥ 645 passed, 0 failed (no regressions in shipped unit suite — anchors ROADMAP success criterion #6).
- All new test names ASCII-only (Win32 CMD codepage mangling per CLAUDE.md).
- Pre-commit hooks (clang-format, mdformat, gitleaks, conventional-commit, typos, ASCII-only test names) must pass on every Phase 26 commit.
- Atomic-commit rule per CLAUDE.md: `KeyDesigner.qml` deletion lands in the SAME commit as `DeviceView.qml` first compile. No interim broken-editor state.

______________________________________________________________________

## Out of Scope (deferred)

- Visual regression testing (screenshot diffs of `DeviceView.qml`). UI-SPEC component states are spec-only for Phase 26.
- Performance profiling (drag-drop frame timing, layout JSON parse time).
- Localisation coverage testing — UI-SPEC declares `qsTr()` everywhere; manual review during code-review is sufficient.
- Accessibility automated checks — UI-SPEC declares Accessible.name + role on all interactive cells; manual screen-reader walk is in operator UAT.
