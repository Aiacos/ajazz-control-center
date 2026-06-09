---
phase: 33
slug: property-inspector-end-to-end
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-06-08
---

# Phase 33 — Validation Strategy

> Per-phase validation contract. Derived from 33-RESEARCH.md "Validation Architecture".
> Phase 33 is ~70% already built (PI-01/PI-03 GREEN); the validation focuses on the two real
> gaps (PI-04 events, PI-02 objectNames/affordance) + the deferred human-verify.
> ctest is necessary but NOT sufficient — the live debug-channel pass (criteria 1-4) is part of done.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                       |
| ---------------------- | --------------------------------------------------------------------------- |
| **Framework**          | Catch2 (ctest) + `scripts/ajazz-debug` live channel                         |
| **Quick run command**  | `ctest --preset linux-release -R "pi-bridge\|plugin-server\|plugin_device"` |
| **Full suite command** | `ctest --preset linux-release` (~744 green baseline at Phase 32 close)      |
| **Filter flag**        | `-R` / `--tests-regex` (NOT `--test-regex`)                                 |
| **Estimated runtime**  | quick \<10s; full ~2 min                                                    |

______________________________________________________________________

## Sampling Rate

- **After every task commit:** `ctest --preset linux-release -R "pi-bridge\|plugin_device"` (fast).
- **After every plan wave:** full `ctest --preset linux-release`.
- **Phase gate:** full suite green AND the live debug-channel pass (criteria 1-4) before verify;
  then the DEFERRED human-verify (criterion 5).

______________________________________________________________________

## Per-Requirement Verification Map

| Req   | Behavior                                                        | Test Type | Command / Method                                                                        | Exists?                          |
| ----- | --------------------------------------------------------------- | --------- | --------------------------------------------------------------------------------------- | -------------------------------- |
| PI-01 | cefQuery@DocumentCreation, sdpi.css, per-plugin isolation       | unit      | `ctest -R pi-bridge` (test_pi_bridge.cpp:419,682)                                       | ✅ GREEN                         |
| PI-02 | loadInspector wired; PI panel renders not-blank                 | live      | `ajazz-debug qml.invoke openPiButton` → `qml.get piPanelLoader` → `screenshot`          | ❌ W0 (objectNames + affordance) |
| PI-03 | per-context + global round-trip + restart                       | unit      | `ctest -R pi-bridge` (:224,252,279)                                                     | ✅ GREEN                         |
| PI-03 | didReceiveSettings reaches plugin (headless)                    | live      | `ajazz-debug plugin.simulatePiSettings` → `plugin.protocolLog` shows didReceiveSettings | ✅ (RPC exists)                  |
| PI-04 | titleParametersDidChange payload completeness, after willAppear | unit      | NEW Catch2 (test_plugin_device_bridge.cpp) asserting all payload keys                   | ❌ W0                            |
| PI-04 | propertyInspectorDidAppear on open / DidDisappear on close      | live      | `ajazz-debug qml.invoke openPiButton`/`closePiButton` → `plugin.protocolLog` shows both | ❌ W0 (emit + objectNames)       |
| PI-03 | real PI JS `$SD.setSettings()` + survives restart               | manual    | **DEFERRED human-verify** (open real PI, edit, restart, confirm)                        | deferred by design               |

______________________________________________________________________

## Wave 0 Requirements

- [ ] NEW Catch2 test asserting `titleParametersDidChange` payload completeness (every key) AND that it
  follows `willAppear` for the same context — covers PI-04 (in test_plugin_device_bridge.cpp).
- [ ] `objectName`s (`openPiButton` / `closePiButton` / `piPanelLoader` / `piWebView`) + an open
  affordance (Button or controller `Q_INVOKABLE` to dodge the Switch.toggled harness gap) — covers
  PI-02 thin-UI contract.
- [ ] No new framework install — Catch2 + debug channel already present.

______________________________________________________________________

## Manual / Live-Only Verifications

| Behavior                                      | Requirement       | Why                        | Instructions                                                                                                                       |
| --------------------------------------------- | ----------------- | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| PI HTML renders not-blank                     | PI-02/criterion 1 | needs the WebEngine render | launch app (WebEngine build), select a bound action, `qml.invoke openPiButton`, `screenshot` shows PI content                      |
| didAppear/didDisappear fire                   | PI-04/criterion 3 | lifecycle wiring           | `qml.invoke openPiButton`/`closePiButton`, `plugin.protocolLog` shows both events                                                  |
| Real `$SD.setSettings()` round-trip + restart | PI-03/criterion 5 | real PI JS, not headless   | **DEFERRED human-verify** at end-of-phase: open a real .sdPlugin PI, edit a setting, confirm didReceiveSettings + survives restart |

______________________________________________________________________

## Validation Sign-Off

- [x] Every requirement has an automated verify or a live/Wave-0 entry
- [x] Sampling continuity: each gap has a quick command
- [x] Wave 0 covers the 2 real gaps
- [x] No watch-mode flags
- [x] Live debug-channel checks enumerated (mandatory per CLAUDE.md)
- [x] Human-verify checkpoint (criterion 5) explicitly deferred + coordinated
- [x] `nyquist_compliant: true`

**Approval:** pending
