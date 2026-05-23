---
phase: 20
slug: property-inspector-settings
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 20 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                                                  |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; pure unit (no WebEngine needed for persistence) + offscreen QWebEngine where available |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                                                           |
| **Quick run command**  | \`ctest --preset linux-release -R "PIBridge                                                                            |
| **Full suite command** | `ctest --preset linux-release`                                                                                         |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                                             |

Hardware-free: the settings round-trip is a pure function of `(AppDataLocation, pluginUuid, contextUuid)` — a fresh `PIBridge` (with `controller==nullptr`) reads what a prior one wrote, no
WebEngine needed. The cefQuery polyfill is asserted by injected-script content + bridge-slot
reachability; `sdpi.css` by the interceptor returning `text/css`. ASCII-only test names. Live
third-party PI round-trip = Phase 25.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner — one row per task. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                                            | Test Type | Automated Command                          | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | -------------------------------------------------------------------------- | --------- | ------------------------------------------ | ----------- | ---------- |
| 20-01-01 | 01   | 1    | PLUGIN-13   | T-20-path  | settings round-trip survives a fresh PIBridge; uuid path-traversal refused | unit      | `ctest --preset linux-release -R PIBridge` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] Extend `tests/unit/test_pi_bridge.cpp` — settings + global-settings round-trip survives a fresh `PIBridge` (write, reconstruct, read-equal); path-traversal uuid refused (PLUGIN-13)
- [ ] `tests/unit/test_cefquery_polyfill.cpp` (or extend) — injected script defines `window.cefQuery` and routes to the bridge `invoke` dispatcher (PLUGIN-09)
- [ ] `tests/unit/test_pi_assets.cpp` — interceptor serves `sdpi.css` from the built-in URL with `text/css` (PLUGIN-09)
- [ ] registerPropertyInspector load: action-select → `loadInspector` → `didReceiveSettings` (offscreen QWebEngine if the `tests/qml` harness supports it, else controller-slot unit)

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                      | Requirement                 | Why Manual                                           | Test Instructions                                                            |
| --------------------------------------------------------------------------------------------- | --------------------------- | ---------------------------------------------------- | ---------------------------------------------------------------------------- |
| A real plugin's Property Inspector HTML renders, edits settings, and the plugin receives them | PLUGIN-09/13 (live witness) | Needs a spawned plugin (Phase 18) + the editor wired | Deferred to Phase 25; the unit/offscreen tests are the Phase 20 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~15s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
