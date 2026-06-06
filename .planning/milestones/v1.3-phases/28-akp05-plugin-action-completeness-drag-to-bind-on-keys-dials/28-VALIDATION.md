---
phase: 28
slug: akp05-plugin-action-completeness-drag-to-bind-on-keys-dials
status: ready
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-31
---

# Phase 28 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                                        |
| ---------------------- | ------------------------------------------------------------------------------------------------------------ |
| **Framework**          | Catch2 (existing; `[catch2]` style)                                                                          |
| **Config file**        | `CMakeLists.txt` (auto-discovered via `tests/unit/`)                                                         |
| **Quick run command**  | `ctest --preset linux-release -R "manifest\|catalog\|bridge\|ProfilePersistence" -E qml --output-on-failure` |
| **Full suite command** | `ctest --preset linux-release -E qml`                                                                        |
| **Estimated runtime**  | ~60-120 seconds full; ~15s scoped                                                                            |

Note: the `tests/qml/ajazz_qml_tests` target has a pre-existing undefined-references
link issue (CLAUDE.md "Latent items"); run with `-E qml`. The C++ unit suite
(`ajazz_unit_tests`) is the authoritative automated gate; QML UI behavior is covered
by the Plan 05 live debug-channel verification.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** Run the quick run command (scoped to the touched suite).
- **After every plan wave:** Run the full suite command.
- **Before `/gsd:verify-work`:** Full suite green AND `grep -rn nlohmann src/core/include/` == 0 (COD-031).
- **Max feedback latency:** ~120 seconds.

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement  | Threat Ref | Secure Behavior                                      | Test Type  | Automated Command                                                    | File Exists | Status     |
| -------- | ---- | ---- | ------------ | ---------- | ---------------------------------------------------- | ---------- | -------------------------------------------------------------------- | ----------- | ---------- |
| 28-01-01 | 01   | 1    | PLUGIN-20    | T-28-01    | nested-extract guards; default-on-absent             | unit       | `ctest --preset linux-release -R plugin-manifest -E qml`             | ✅ extend   | ⬜ pending |
| 28-01-02 | 01   | 1    | PLUGIN-20    | T-28-03    | affordance fail-safe (unknown token → no bit)        | unit       | `ctest --preset linux-release -R plugin-manifest -E qml`             | ✅ extend   | ⬜ pending |
| 28-02-01 | 02   | 2    | PLUGIN-18    | T-28-05    | hidden actions never routable (filtered pre-UUID)    | unit       | `ctest --preset linux-release -R catalog -E qml`                     | ✅ extend   | ⬜ pending |
| 28-02-02 | 02   | 2    | PLUGIN-18/20 | T-28-04    | debug RPC gated by AJAZZ_DEBUG_CONTROL=1             | build+live | `cmake --build ... --target ajazz-control-center` + Plan 05          | ✅ extend   | ⬜ pending |
| 28-03-01 | 03   | 3    | PLUGIN-19/20 | T-28-07    | STRICT reject of mismatched-affordance drops         | build+qml  | app build + `tests/qml/test_device_view_drag_drop.qml` (best-effort) | ✅ extend   | ⬜ pending |
| 28-03-02 | 03   | 3    | PLUGIN-19    | T-28-08    | actionId persists routably; garbage payload rejected | unit       | `ctest --preset linux-release -R ProfilePersistence -E qml`          | ✅ extend   | ⬜ pending |
| 28-04-01 | 04   | 4    | PLUGIN-19    | T-28-10    | context registered only for bound plugin UUID        | unit       | `ctest --preset linux-release -R bridge -E qml`                      | ✅ extend   | ⬜ pending |
| 28-04-02 | 04   | 4    | PLUGIN-19    | T-28-09    | empty-deviceId guard (no fallback)                   | build+live | app build + Plan 05                                                  | ✅ extend   | ⬜ pending |
| 28-05-01 | 05   | 5    | PLUGIN-18    | T-28-04    | installedActions count == manifest visible count     | live       | debug channel (`plugin.installedActions`)                            | ❌ doc      | ⬜ pending |
| 28-05-02 | 05   | 5    | PLUGIN-19/20 | T-28-12    | dial-bound plugin receives dialRotate/dialDown       | live       | debug channel (`input.encoder` + `log.tail`)                         | ❌ doc      | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/fixtures/manifests/manifest_visibility.json` — VisibleInActionsList + Encoder block + multi-state (Plan 01 Task 1)
- [ ] `tests/unit/fixtures/manifests/manifest_affordances.json` — all controller token variants (Plan 01 Task 1)
- [ ] `plugin.installedActions` debug RPC — needed for live PLUGIN-18 verification (Plan 02 Task 2)

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                      | Requirement  | Why Manual                                                                        | Test Instructions                                                                                                                       |
| ----------------------------------------------------------------------------- | ------------ | --------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| installedActions count == manifest visible count for System Monitor + Weather | PLUGIN-18    | Requires the running app + installed plugins over the WS; not a unit-test surface | Plan 05 Task 1: launch `AJAZZ_DEBUG_CONTROL=1`, install Weather, `scripts/ajazz-debug plugin.installedActions`, compare to manifest     |
| Dial-bound plugin receives dialRotate/dialDown on synthetic input             | PLUGIN-19/20 | Integration/wiring path invisible to ctest (Phase-27 no-op precedent)             | Plan 05 Task 2: bind via commitEncoderBinding, fire `input.encoder`/`input.encoderPress`, read `log.tail` for controller=Encoder events |
| Real-mouse drag onto a dial visually rejects a mismatched action              | PLUGIN-20    | Synthetic harness can't reproduce a true mouse drag (`qml.invoke` gap)            | Plan 05 Task 2 screenshot readout; QML test is best-effort on the excluded target                                                       |

Synthetic `input.*` injection routes through the full pipeline (no input hardware needed),
so the dial round-trip is autonomous-capable even on the input-unreachable 0x3004 demo unit.

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references (fixtures + debug RPC)
- [x] No watch-mode flags
- [x] Feedback latency < 120s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved 2026-05-31
