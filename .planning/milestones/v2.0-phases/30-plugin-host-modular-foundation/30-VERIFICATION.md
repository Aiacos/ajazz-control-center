---
status: passed
phase: 30-plugin-host-modular-foundation
verified: 2026-06-06
verifier: orchestrator-inline (gsd-verifier unavailable — Sonnet quota exhausted; goal-backward verification run on Opus with full build + test evidence)
requirements: [HOST-01, HOST-02, HOST-03]
must_haves_total: 5
must_haves_verified: 5
---

# Phase 30 — Verification: Plugin-Host Modular Foundation

**Goal:** The plugin layer has a clean, modular contract — all runtimes share one spawn/lifecycle/IPC interface; crash mid-handshake never brings down the app; device SKU details never bleed into plugin code.

**Verdict: PASSED** — all 5 ROADMAP success criteria met; HOST-01/02/03 covered; full suite green (701/701); COD-031 and HOST-03 boundary audits clean.

## Evidence (orchestrator-run)

- **Build:** `cmake --build --preset linux-release --target ajazz_unit_tests` — compiles + links the new `i_plugin_host2.hpp` / `unified_plugin_host.{hpp,cpp}` and the PluginManager/Application refactor.
- **Full suite:** `ctest --preset linux-release` → **701/701 passed**, 0 failed (~127s).
- IDE clangd diagnostics on the new files were confirmed false positives (no Qt include flags in the bare clang invocation); the files compile and the suite runs.

## Success Criteria (goal-backward)

| #   | Criterion                                                                                                   | Status | Evidence                                                                                                                                                                                                                                                                   |
| --- | ----------------------------------------------------------------------------------------------------------- | ------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Disconnect-before-register → zero crash, zero dangling ptr; `plugin.list` returns 0 connected (not a crash) | ✅     | ctest #611 `disconnect-before-register leaves zero connected and does not crash` PASS; 30-02 executor ran the live `AJAZZ_DEBUG_CONTROL=1` + `scripts/ajazz-debug plugin.list` check — connectedCount stayed 0, app responsive                                             |
| 2   | PluginManager dispatch routes Node/HTML/native/Python with no SKU branch                                    | ✅     | Four-runtime tests (#249/#286/#328/#366 by name) PASS + contract test "unified host dispatch reaches Python through the IPluginHost2 pointer" PASS; `grep -rn "akp05e\|akp03\|akp153" src/app/src/plugin_manager.cpp src/app/src/unified_plugin_host.cpp` = 0              |
| 3   | `grep -rn "QHostAddress::Any" src/` = 0 AND SIGPIPE in main.cpp — both CI-gated                             | ✅     | Loopback-only confirmed (`QHostAddress::LocalHost`); `::signal(SIGPIPE, SIG_IGN)` at main.cpp:55; CI step "Enforce loopback-only + SIGPIPE invariants (Phase 30)" added to `.github/workflows/ci.yml` (commit 611a097)                                                     |
| 4   | 3-in-30s disable+notify, restart, exitApp pass; pre-registration exits NOT counted                          | ✅     | ctest "crash 3 in 30s disables not restarts" (#592), "shutdown sends exitApp before terminate" (#594), and "pre-registration-exit is not counted as a crash" (#612) all PASS; `onProcessFailed` `m_live.find` guard (commit 81a65a4)                                       |
| 5   | ADR committed: IPluginHost2 unification decision with rationale                                             | ✅     | `.planning/phases/30-plugin-host-modular-foundation/30-ADR-plugin-host-unification.md` (commit 3af724c) — documents the UNIFY decision (user override of the keep-separate research verdict) + UnifiedPluginHost aggregator + per-runtime regression budget + COD-031 note |

## Requirement Traceability

| Req                                                                                                                            | Covered by                                                              | Status |
| ------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------- | ------ |
| HOST-01 (single IPluginHost contract over all runtimes; PluginManager dispatch refactored, no regression)                      | 30-03 (IPluginHost2 + UnifiedPluginHost aggregator; four-runtime tests) | ✅     |
| HOST-02 (pre-registration-safe crash isolation; 3-in-30s/restart/exitApp preserved)                                            | 30-01 (RED tests) + 30-02 (sentinel-UUID + onProcessFailed guard)       | ✅     |
| HOST-03 (zero compile/link coupling to SKUs/wire; device I/O only via PluginDeviceBridge → StreamDockControlService → sidecar) | 30-03 SKU audit (0 hits) + code-review-only boundary per CONTEXT.md     | ✅     |

## Locked-Decision Compliance

- **FULL UNIFICATION NOW** (user override): delivered via `UnifiedPluginHost` aggregator implementing `IPluginHost2`, owning both sub-hosts, routing dispatch by UUID internally; `Application::dispatch()` no longer branches by runtime. The contract is genuine (verified by the IPluginHost2\*-pointer Python dispatch test), not a facade.
- **SINGLE MAP + SENTINEL UUID**: implemented in `SdPluginServer::m_connections` (`"__pending__"+QUuid`), race-guarded rekey by socket pointer.
- **SKU code-review-only**: no CI SKU grep gate added; the separate QHostAddress/SIGPIPE CI gate IS added.
- **COD-031**: `IPluginHost2`/`UnifiedPluginHost` are app-layer (`src/app/src/`); no nlohmann include in `src/core/include/` or the new headers (boundary-assertion comments only).

## Human Verification (advisory — non-blocking)

1. **Merged-inventory live check via the unified host.** Launch `AJAZZ_DEBUG_CONTROL=1`, install/register both a `.sdPlugin` and a Python plugin, and confirm `scripts/ajazz-debug plugin.list` shows BOTH via the unified host. (The disconnect-before-register live check was already run during 30-02; this merged-inventory check was deferred during the rate-limit close-out.)

## Notes

- Plan 30-03 was closed out by the orchestrator after the executing Sonnet agent hit its daily quota immediately after committing all 3 task commits (clean tree, no partial code). See 30-03-SUMMARY.md "Issues Encountered".
