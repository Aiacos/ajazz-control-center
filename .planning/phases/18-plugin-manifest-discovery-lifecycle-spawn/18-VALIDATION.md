---
phase: 18
slug: plugin-manifest-discovery-lifecycle-spawn
status: planned
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-23
---

# Phase 18 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                                          |
| ---------------------- | -------------------------------------------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; fixture `.sdPlugin` dirs + injected fake QProcess events                       |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                                                   |
| **Quick run command**  | `ctest --preset linux-release -R "plugin-manifest\|node-runner\|mirabox-compat\|plugin-crash\|plugin-manager"` |
| **Full suite command** | `ctest --preset linux-release`                                                                                 |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                                     |

Hardware-free + mostly process-free: manifest parse/validate over fixture manifests; node argv
**built and asserted without launching node** (`NodeRunnerTest::buildsCorrectArgv`); node-version
detection probe is mockable; crash-counter logic driven by injected timestamps; the Mirabox
`QWebEngineScript` alias asserted by unit (pure string always; injection-point under WebEngine).
A real spawn (live node + WS round-trip) is an OPTIONAL integration test gated on `node` presence;
the live `.sdPlugin` witness is Phase 25 (VERIFY-06). ASCII-only test names.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above (or the single new target for that task)
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement  | Threat Ref         | Secure Behavior                                                                                          | Test Type  | Automated Command                                                    | File Exists | Status     |
| -------- | ---- | ---- | ------------ | ------------------ | -------------------------------------------------------------------------------------------------------- | ---------- | -------------------------------------------------------------------- | ----------- | ---------- |
| 18-01-01 | 01   | 1    | PLUGIN-06    | T-18-MANIFEST      | parser rejects malformed/oversized JSON + missing required keys (QJson bounds-safe; nullopt not partial) | unit/build | `cmake --build --preset linux-release --target ajazz-control-center` | ❌ W0       | ⬜ pending |
| 18-01-02 | 01   | 1    | PLUGIN-06    | T-18-MANIFEST-OS   | Linux OS-accept rule tested; mac-only rejected when windows asked; too-high MinimumVersion rejected      | unit       | `ctest --preset linux-release -R plugin-manifest`                    | ❌ W0       | ⬜ pending |
| 18-02-01 | 02   | 2    | PLUGIN-08    | T-18-ARGV          | node argv built as a discrete-token QStringList (no shell string); codePath is argv[0]                   | unit/build | `cmake --build --preset linux-release --target ajazz-control-center` | ❌ W0       | ⬜ pending |
| 18-02-02 | 02   | 2    | PLUGIN-08    | T-18-NODEVER       | node>=20 detection rejects absent/too-old via injected probe; no live node launched                      | unit       | `ctest --preset linux-release -R node-runner`                        | ❌ W0       | ⬜ pending |
| 18-03-01 | 03   | 3    | PLUGIN-11    | T-18-SHIM-RACE     | alias QWebEngineScript injected at DocumentCreation/MainWorld (not runJavaScript-after-load)             | unit/build | `cmake --build --preset linux-release --target ajazz-control-center` | ❌ W0       | ⬜ pending |
| 18-03-02 | 03   | 3    | PLUGIN-11    | T-18-SHIM-RACE     | shim source aliases connectMiraBoxSDSocket -> connectElgatoStreamDeckSocket (forwarder)                  | unit       | `ctest --preset linux-release -R mirabox-compat`                     | ❌ W0       | ⬜ pending |
| 18-04-01 | 04   | 4    | PLUGIN-07    | T-18-CHILD-PERSIST | pure 3-in-30s crash window (injected clock); disable vs restart decision                                 | unit       | `ctest --preset linux-release -R plugin-crash`                       | ❌ W0       | ⬜ pending |
| 18-04-02 | 04   | 4    | PLUGIN-07/08 | T-18-ZIPSLIP/ARGV  | discover reuses zip-slip-guarded extractor; spawn argv as list (no startDetached); node-absent disables  | unit       | `ctest --preset linux-release -R plugin-manager`                     | ❌ W0       | ⬜ pending |
| 18-04-03 | 04   | 4    | PLUGIN-07    | T-18-CHILD-PERSIST | crash-3-in-30s disables not restarts; exitApp via sendEvent before terminate/kill                        | unit       | `ctest --preset linux-release -R plugin-manager`                     | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_plugin_manifest.cpp` — Elgato v6 + AJAZZ ext parse; Linux OS-accept rule + mac-only-when-windows-asked reject; too-high Software.MinimumVersion reject; Controllers accepts Knob/SecondaryScreen; invalid-JSON nullopt (PLUGIN-06) + 4 JSON fixtures under tests/unit/fixtures/manifests/
- [ ] `tests/unit/test_node_runner.cpp` — exact node argv `-port/-pluginUUID/-registerEvent/-info` without launching node; version-detection probe mocked present/absent/too-old (PLUGIN-08)
- [ ] `tests/unit/test_mirabox_compat.cpp` — `connectMiraBoxSDSocket` aliased to connectElgatoStreamDeckSocket (pure string always; DocumentCreation/MainWorld under WebEngine) (PLUGIN-11)
- [ ] `tests/unit/test_plugin_crash_tracker.cpp` — 3-in-30s disable, 2-crash restart, out-of-window ignore, uuid isolation (PLUGIN-07)
- [ ] `tests/unit/test_plugin_lifecycle.cpp` — discovery over a temp dir of fixture `.sdPlugin` (incl. a leftover archive extracted first); spawn dispatch (node argv pinned, node-absent disables); crash-3-in-30s disable; exitApp shutdown via SdPluginServer::sendEvent (PLUGIN-07/08)
- [ ] Reuse `sdplugin_extractor`, the PI QWebEngine stack, `SdPluginServer::sendEvent` (17-02) — no new framework
- [ ] Wire each new TU + its `src/app/src/*.cpp` into `tests/unit/CMakeLists.txt` (mirror the plugin source + WebSockets/WebEngine/CorePrivate gating blocks)

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                                | Requirement                    | Why Manual                                                                           | Test Instructions                                                            |
| ----------------------------------------------------------------------------------------------------------------------- | ------------------------------ | ------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------- |
| A real third-party `.sdPlugin` (node + HTML) spawns, connects to the loopback server, and exchanges messages end-to-end | PLUGIN-07/08/11 (live witness) | Needs the device bridge (Phase 19) + a real package; full E2E is the milestone close | Optional node-gated integration test here; full witness Phase 25 (VERIFY-06) |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < ~15s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planned (per-task map filled; await execution)
</content>
