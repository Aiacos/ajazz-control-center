---
phase: 32-binding-layer-fix-multi-toggle-action-device-editor
plan: 04
subsystem: app
tags: [plugin-device-bridge, willAppear, owner-resolution, device-generic-dispatch, regression-test, bind-03, bind-06]

# Dependency graph
requires:
  - phase: 32-binding-layer-fix-multi-toggle-action-device-editor
    provides: already-shipped BIND-03 (eventEnvelope action field, commit ddabc16; resolveOwner stored-owner-first, commit 519ecd0) and BIND-06 (device-generic onDeviceEvent switch) production code
provides:
  - Regression tests pinning the willAppear/keyDown/dialRotate/touchTap envelope top-level "action" field (BIND-03 Hypothesis-A guard)
  - Regression test pinning stored-owner resolution of a non-dotted-prefix action via the injected resolveOwner seam (BIND-03 Hypothesis-B guard)
  - Parametrized regression test pinning per-controller-type routing (Keypad key / Encoder dial / Encoder-row0 touch-zone) through one generic onDeviceEvent path with no SKU branch (BIND-06)
affects: [test_plugin_device_bridge, future ad-hoc commits on plugin_device_bridge.cpp/application.cpp]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - 'Verify-and-lock pattern: when production code is already correct (RESEARCH refuted both BIND-03 bug hypotheses), add regression tests that assert against the code AS-IS and prove NO production diff, rather than rewriting'
    - Catch2 GENERATE_REF parametrizes one TEST_CASE across the three controller types so a single registered case exercises Keypad/Encoder/touch-zone through the same onDeviceEvent

key-files:
  created: []
  modified:
    - tests/unit/test_plugin_device_bridge.cpp

key-decisions:
  - Production code (plugin_device_bridge.cpp, application.cpp) left BYTE-FOR-BYTE unchanged; the plan acceptance criterion `git diff --stat` shows tests only -- verified empty
  - touch-zone synthetic event uses X=80 so (x*4)/256 == zone 1, matching the locked TouchUp->Encoder/row0/col=zoneIndex registration convention (NOT SKU code)
  - Reused the existing E2eFixture-style context-registration + loopback-client helpers (connectAndRegister, firstEventForEvent, receivedEventNames); no new harness introduced

patterns-established:
  - 'BIND-03/BIND-06 behavior is now pinned: envelope.action presence, stored-owner resolution, and per-controller-type generic routing each have a dedicated regression case in the [plugin-device-bridge] suite'

requirements-completed: [BIND-03, BIND-06]

# Metrics
duration: 22min
completed: 2026-06-08
---

# Phase 32 Plan 04: Lock BIND-03 + BIND-06 with regression tests Summary

**Regression tests that pin the already-shipped, device-generic binding layer -- the willAppear/keyDown/dialRotate/touchTap envelope's top-level `action` field, stored-owner resolution of a non-dotted-prefix action, and one generic key/encoder/touch-zone dispatch path with no SKU branch -- so a future ad-hoc commit on this churned branch cannot silently regress them. Zero production code changed.**

## What was done

This was a VERIFY + REGRESSION-TEST plan, not a rewrite. RESEARCH Findings 1 and 4 established that both BIND-03 "known bugs" were already fixed 2026-06-02 (commits `ddabc16` + `519ecd0`) and that `onDeviceEvent` was already device-generic. The work added three Catch2 regression cases to `tests/unit/test_plugin_device_bridge.cpp` (inside the existing `AJAZZ_HAVE_WEBSOCKETS` guard, reusing the loopback-client + context-registration helpers):

1. **BIND-03 envelope.action (all three controller types).**
   `PluginDeviceBridge BIND-03 outbound envelope carries top-level action field for key encoder and touch contexts` — registers a Keypad, an Encoder, and a touch-zone (Encoder/row0/col1) context, feeds `KeyPressed`/`EncoderTurned`/`TouchUp`, and asserts the outbound `keyDown`/`dialRotate`/`touchTap` each carry a non-empty top-level `"action"` equal to that context's `actionUUID` (guards `eventEnvelope` line 337).

1. **BIND-03 stored-owner resolution (non-dotted-prefix).**
   `PluginDeviceBridge BIND-03 stored-owner resolver resolves a non-dotted-prefix action to its owner plugin` — binds key 1 to `weather.current` (deliberately NOT a dotted child of `com.test.plug`), asserts the legacy `ownerForActionUuid` dotted-prefix path returns empty, injects the stored-owner resolver (production's `PluginManager::ownerForAction`), and asserts `willAppear` is still delivered carrying `weather.current` (guards `resolveOwner` :851-863 — the Hypothesis-B silent-no-willAppear regression).

1. **BIND-06 per-controller-type generic routing.**
   `PluginDeviceBridge BIND-06 onDeviceEvent routes Keypad Encoder and touch-zone generically by control type with no SKU branch` — a `GENERATE_REF`-parametrized case that, for each of {Keypad key, Encoder dial, touch-zone}, feeds the corresponding synthetic `DeviceEvent` through the SAME `onDeviceEvent` and asserts the right outbound event reaches the right plugin uuid with the right `action`. The same code path resolving all three proves there is no codename/SKU branch in dispatch.

## Verification (build + automated)

- `cmake --build --preset linux-release --target ajazz_unit_tests` — compiles clean under `-Werror` (fixed `[[nodiscard]] registerContext` consumption + `std::uint16_t`/`std::int32_t` field types to satisfy `-Wconversion`).
- `ctest --preset linux-release -R "PluginDeviceBridge"` — **47/47 passed**, including the 3 new cases (#697, #698, #699).
- `ctest --preset linux-release -LE qml` — **715/715 passed**, no regression.
- `git diff --stat src/app/src/plugin_device_bridge.cpp src/app/src/application.cpp` — **empty** (acceptance criterion: production code untouched).
- `grep -c '"action"' tests/unit/test_plugin_device_bridge.cpp` = 9 (> 0).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `-Werror` build breaks in the new test code**

- **Found during:** Task 1 first build.
- **Issue:** (a) `ContextRegistry::registerContext` is `[[nodiscard]]`; the `regCtx` helper lambdas ignored the return -> `-Werror=unused-result`. (b) The parametrized `Case` struct used `int` for index/value; `DeviceEvent::index` is `std::uint16_t` and `value` is `std::int32_t` -> `-Werror=conversion`.
- **Fix:** Consumed the return via `[[maybe_unused]] auto const`; typed the `Case` fields and `GENERATE_REF` literals as `std::uint16_t`/`std::int32_t`; added `#include <cstdint>` and `#include <catch2/generators/catch_generators.hpp>`.
- **Files modified:** `tests/unit/test_plugin_device_bridge.cpp` (the in-flight new code only).
- **Commit:** this plan's task commit.

No production deviations. No architectural changes.

## Known Stubs

None. The plan added tests only.

## Pending live verification

The plan's `checkpoint:human-verify` (Task 2) requires the MANDATORY live debug-channel pass (CLAUDE.md: ctest is necessary but NOT sufficient). It was **not run here** (executor does not launch the GUI). The orchestrator should run these in a consolidated live pass on an AKP05E-class (5-column) device:

1. **Launch headless** (kill any running instance + stale socket first):

   ```
   AJAZZ_DEBUG_CONTROL=1 AJAZZ_ALLOW_UNTRUSTED_PLUGINS=1 nohup build/linux-release/src/app/ajazz-control-center &
   ```

1. **Install + bind a REAL plugin action** (System Monitor — NOT a built-in; built-ins have no plugin process so willAppear is correctly skipped, RESEARCH Finding 1). Use the documented install+bind recipe (auto-memory `project_plugin_install_demo_working`).

1. **In-interaction willAppear (BIND-03):** drag the action onto a key, then:

   ```
   scripts/ajazz-debug plugin.protocolLog
   ```

   **Expected:** a `willAppear` line arrives within the SAME interaction, WITHOUT any disconnect/reconnect in the log.

1. **Three-controller routing (BIND-06):**

   ```
   scripts/ajazz-debug input.key      '{"index":N,"pressed":true}'
   scripts/ajazz-debug input.encoder  '{"index":0,"delta":1}'
   scripts/ajazz-debug input.touch    '{"x":80}'
   ```

   then `scripts/ajazz-debug plugin.protocolLog` after each.
   **Expected:** `plugin.protocolLog` shows `keyDown` / `dialRotate` / `touchTap` reaching the registered context each time, with no reconnect.

**Caveat (RESEARCH Finding 1):** `populateContextsForActivePage` hardcodes 5 key columns — verify on an AKP05E-class (5-col) device; flag non-5-col SKUs as a known limitation, out of scope.

## Self-Check: PASSED

- `tests/unit/test_plugin_device_bridge.cpp` — FOUND (modified, contains the 3 new TEST_CASEs at lines 1836/1925/1988).
- Production diff — empty (verified).
- Task commit: `74e192b` (FOUND in git log).
