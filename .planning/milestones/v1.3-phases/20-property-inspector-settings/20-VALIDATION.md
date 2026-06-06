---
phase: 20
slug: property-inspector-settings
status: planned
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-23
---

# Phase 20 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                                                                                              |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Framework**          | Catch2 (C++) under CMake/CTest; pure unit (no WebEngine needed for persistence/dispatcher/css helper) + WebEngine-gated controller/injection cases where available |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                                                                                                       |
| **Quick run command**  | \`ctest --preset linux-release -R "pi-bridge                                                                                                                       |
| **Full suite command** | `ctest --preset linux-release`                                                                                                                                     |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                                                                                         |

Hardware-free: the settings round-trip is a pure function of `(AppDataLocation, pluginUuid, contextUuid)` — a fresh `PIBridge` (with `controller==nullptr`) reads what a prior one wrote, no
WebEngine needed. The cefQuery polyfill is asserted by injected-script content + the `invoke`
dispatcher reaching the typed `$SD` slots; `sdpi.css` by the pure `isSdpiCssRequest` helper + the
bundled qrc resource presence. The load handshake is asserted at controller level (WebEngine-gated).
ASCII-only test names. Live third-party PI round-trip = Phase 25.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement | Threat Ref       | Secure Behavior                                                                                                     | Test Type             | Automated Command                                              | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------------- | ------------------------------------------------------------------------------------------------------------------- | --------------------- | -------------------------------------------------------------- | ----------- | ---------- |
| 20-01-01 | 01   | 1    | PLUGIN-13   | T-20-path        | pi_bridge.cpp linked into the test binary with no WebEngine link leaked                                             | build                 | `ctest --preset linux-release -R "persistence"`                | ❌ W0       | ⬜ pending |
| 20-01-02 | 01   | 1    | PLUGIN-13   | T-20-path/dos    | settings + global round-trip survive a fresh PIBridge; path-traversal uuid creates no file + reads {}               | unit                  | `ctest --preset linux-release -R "persistence"`                | ❌ W0       | ⬜ pending |
| 20-02-01 | 02   | 2    | PLUGIN-09   | T-20-SHIM/RACE   | cefQuery shim defines window.cefQuery + routes via invoke dispatcher to typed $SD slots only                        | unit (+WE-gated)      | `ctest --preset linux-release -R "cefquery"`                   | ❌ W0       | ⬜ pending |
| 20-02-02 | 02   | 2    | PLUGIN-09   | T-20-CSS-REDIR   | sdpi.css bundled as qrc; isSdpiCssRequest suffix-exact; interceptor redirects to fixed qrc; policy unchanged        | unit (+ qrc presence) | `ctest --preset linux-release -R "sdpi"`                       | ❌ W0       | ⬜ pending |
| 20-03-01 | 03   | 3    | PLUGIN-09   | T-20-SHIM-ONCE   | loadInspector inserts cefQuery shim once per profile (DocumentCreation) + emits settings on load                    | controller (WE-gated) | `ctest --preset linux-release -R "inspector"`                  | ❌ W0       | ⬜ pending |
| 20-03-02 | 03   | 3    | PLUGIN-09   | T-20-NAV         | action-select -> loadInspector / native -> closeInspector; per-profile interceptor unchanged                        | qml build             | `cmake --build --preset linux-release -t ajazz-control-center` | ❌ W0       | ⬜ pending |
| 20-03-03 | 03   | 3    | PLUGIN-09   | T-20-RELAY-SCOPE | relay endpoints (sendToPropertyInspector signal + sendToPlugin stub) test-pinned; live route deferred per STOP gate | unit                  | `ctest --preset linux-release -R "relay"`                      | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] 20-01: link `pi_bridge.cpp` into `ajazz_unit_tests`; add settings + global round-trip + path-traversal-refusal cases to `tests/unit/test_pi_bridge.cpp` (PLUGIN-13)
- [ ] 20-02: `src/app/src/pi_cef_shim.{hpp,cpp}` (kCefQueryShimSource string test always-on + makeCefQueryShim injection-point test WE-gated) + `PIBridge::invoke` dispatcher case; `isSdpiCssRequest` helper case + bundled-css presence (PLUGIN-09)
- [ ] 20-03: controller-level `loadInspector` -> hasHtmlInspector/getSettings case (WE-gated); Inspector.qml trigger build; relay-endpoint case (PLUGIN-09)
- [ ] No framework install needed — Catch2 + offscreen QML harness already present.

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                      | Requirement                    | Why Manual                                                               | Test Instructions                                                               |
| ------------------------------------------------------------------------------------------------------------- | ------------------------------ | ------------------------------------------------------------------------ | ------------------------------------------------------------------------------- |
| A real plugin's Property Inspector HTML renders, calls cefQuery, edits settings, and the plugin receives them | PLUGIN-09/13 (live witness)    | Needs a spawned plugin (Phase 18) + SdPluginServer::sendEvent (Phase 17) | Deferred to Phase 25; the unit/controller tests are the Phase 20 gating proof   |
| sendToPlugin / sendToPropertyInspector live round-trip to a plugin process                                    | PLUGIN-09 (relay live witness) | Needs SdPluginServer::sendEvent (Phase 17) + a spawned plugin (Phase 18) | Deferred per the 20-03 STOP gate; wired when sendEvent lands, verified Phase 25 |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < ~15s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planned (pending execution)
