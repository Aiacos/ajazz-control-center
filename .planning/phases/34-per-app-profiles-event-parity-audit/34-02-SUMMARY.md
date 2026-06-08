---
phase: 34-per-app-profiles-event-parity-audit
plan: 02
subsystem: plugin-events
tags: [event-parity, willappear, controller-token, knob-encoder, catch2, audit, cod-031, doc]

# Dependency graph
requires:
  - phase: 19-plugin-device-bridge
    provides: instancePayload/eventEnvelope willAppear builders + ContextRegistry + the loopback-WebSocket e2e seam (server+bridge+profile accessor) the EVENT-02/04 tests reuse
  - phase: 18-plugin-manifest
    provides: affordanceMask() Knob||Encoder normalizer at the manifest boundary (EVENT-04 (1) asserts it)
  - phase: 34-01
    provides: Wave-1 test idiom (test-case titles feature-token-prefixed so ctest -R matches by name)
provides:
  - docs/plugin-event-parity.md coverage table (EVENT-01) classifying every OpenDeck inbound+outbound + Elgato SDK event supported/partial/missing/hardware-gated with a per-supported-event test reference + implementing file:symbol
  - willappear_payload Catch2 suite (EVENT-02) locking the willAppear envelope+payload completeness against the REAL bridge builders (not a hand-rolled shape)
  - controller_token Catch2 suite + grep gate (EVENT-04) proving Knob and Encoder normalize identically at the manifest and no wire-emission site emits the literal Knob
affects: [34-03, 34-04, event-parity, plugin-handler-contract]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - EVENT-02/04 assert the ACTUAL emitted envelope captured off a loopback QWebSocket client (the test_plugin_device_bridge.cpp e2e seam) rather than duplicating the file-local instancePayload/eventEnvelope shape -- drift in the real builders breaks the test
    - 'plugin-event-parity.md is a living verification deliverable: per-event status + test reference kept in lockstep with the implementing site'

key-files:
  created:
    - docs/plugin-event-parity.md
    - tests/unit/willappear_payload_test.cpp
    - tests/unit/controller_token_test.cpp
  modified:
    - tests/unit/CMakeLists.txt

key-decisions:
  - EVENT-02/EVENT-04 reuse the existing loopback-WebSocket bridge e2e seam (server + bridge + profile accessor -> onPluginRegistered / onDeviceEvent -> capture the envelope off the client) because instancePayload/eventEnvelope are file-local (anonymous namespace) and must not be duplicated; the test asserts the real wire output
  - controller_token EVENT-04 (2) asserts the Encoder token via a dialRotate emission (encoder willAppear and dialRotate copy ctx.controller verbatim, which is wire-normalized at registration); paired with the grep gate this proves no path emits Knob
  - Both new TUs registered inside the existing AJAZZ_HAVE_WEBSOCKETS bridge block (they share its plugin_device_bridge.cpp + Qt6::WebSockets link); plugin_manifest.cpp (affordanceMask) is already linked unconditionally -- not re-added (duplicate-symbol avoidance)

requirements-completed: [EVENT-01, EVENT-02, EVENT-04]

# Metrics
duration: ~10min
completed: 2026-06-08
---

# Phase 34 Plan 02: EVENT Audit Track (Parity Doc + willAppear Payload + Knob->Encoder Normalization) Summary

**A committed `docs/plugin-event-parity.md` coverage table classifying every OpenDeck/Elgato event with status + test reference, plus two Catch2 suites that lock the willAppear envelope+payload completeness (EVENT-02) and the Knob->Encoder controller-token normalization (EVENT-04) against the REAL bridge wire output -- 764/764 ctest green.**

## Performance

- **Duration:** ~10 min
- **Completed:** 2026-06-08
- **Tasks:** 3 (all auto; Tasks 2/3 tdd)
- **Files created/modified:** 4 (3 created, 1 modified)
- **Tests:** 760 -> 764 (4 new EVENT cases)

## Accomplishments

- **EVENT-01:** Authored `docs/plugin-event-parity.md` -- an ASCII-only, living coverage table cross-referencing `SdPluginServer`/`PluginDeviceBridge` against the OpenDeck `events/inbound`+`events/outbound` sets and the Elgato SDK event list. Every event carries a status from {supported, partial, missing, hardware-gated}, the implementing `file:symbol` where supported, and a ctest test reference for each supported event. Input wire decode (`touchTap`, encoder values) is correctly marked `hardware-gated`; `switchToProfile`/`systemDidWakeUp`/`applicationDid*` are marked `missing` with their Phase-34 plan landing (EVENT-03/APROF-04). The Knob->Encoder mapping is recorded and cross-linked to EVENT-04.
- **EVENT-02:** `willappear_payload` (2 cases) asserts the full Elgato envelope (top-level `action`/`context`/`device`/`event`) and `payload{coordinates.row, coordinates.column, controller, state, isInMultiAction, settings}` against the real bridge `instancePayload`+`eventEnvelope` builders, captured off a loopback WebSocket client. The controller is asserted normalized (`Keypad`/`Encoder`) and never `Knob`.
- **EVENT-04:** `controller_token` (2 cases) asserts (1) `affordanceMask()` maps both `Knob` and `Encoder` to the identical encoder (`Affordance::Dial`) bitmask at the manifest boundary, and (2) a representative encoder emission (`dialRotate`) carries the normalized `Encoder` token on the wire. The grep gate confirms the only `"Knob"` literal under `src/app/src` C++ sources is the manifest acceptance line (`plugin_manifest.cpp:144`).
- Full build green; full suite **764/764** (linux-release, incl. 17 qml).

## Task Commits

1. **Task 1: plugin-event-parity coverage table (EVENT-01)** - `205abba` (docs)
1. **Task 2: willAppear envelope+payload completeness test (EVENT-02)** - `806f9cc` (test)
1. **Task 3: Knob->Encoder controller-token normalization audit (EVENT-04)** - `6aa67eb` (test)

## Verification

- `ctest --preset linux-release -R willappear_payload` -> 2/2 passed.
- `ctest --preset linux-release -R controller_token` -> 2/2 passed.
- `grep -rn '"Knob"' src/app/src/*.cpp | grep -v plugin_manifest.cpp` -> no matches (manifest-only).
- `cmake --build --preset linux-release` -> clean.
- `ctest --preset linux-release` -> 764/764 passed (136 s).
- `docs/plugin-event-parity.md` ASCII-clean (LC_ALL=C non-ASCII scan empty).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `/*` inside a block comment trips `-Werror=comment`**

- **Found during:** Task 3 (first build)
- **Issue:** The controller_token_test.cpp file-header comment referenced `src/app/src/*.cpp`; the literal `/*` substring inside a `/* ... */` block tripped GCC `-Werror=comment` ("'/\*' within comment").
- **Fix:** Reworded the comment to "the src/app/src C++ sources" (no `/*` sequence). The grep gate in the verify step still uses the real `src/app/src/*.cpp` glob.
- **Files modified:** tests/unit/controller_token_test.cpp
- **Committed in:** `6aa67eb`

**2. [Rule 3 - Blocking] `int` key into `prof.keys` (uint16_t map key) trips `-Werror=conversion`**

- **Found during:** Task 2 (first build)
- **Issue:** `Profile::keys` is keyed by `std::uint16_t`; a helper took an `int keyIndex`, so `prof.keys[keyIndex] = ...` narrowed under `-Werror=conversion`.
- **Fix:** Changed the helper parameter to `std::uint16_t` (+`#include <cstdint>`).
- **Files modified:** tests/unit/willappear_payload_test.cpp
- **Committed in:** `806f9cc`

Both are Rule-3 blocking build fixes under the project's strict `-Werror`; no scope change. Note: the pre-commit `clang-format` hook reordered the test-file `#include`s and `using` lines on first commit attempt for both Tasks 2 and 3 -- handled by the standard re-`git add` + re-commit dance (NOT `--no-verify`), as the hook is functioning correctly.

## TDD Gate Compliance

Tasks 2 and 3 are `tdd="true"` but are **audit-assertion tests of already-shipped behavior** (the willAppear builders and the Knob||Encoder manifest normalizer both predate this plan -- Phases 18/19). The EVENT track is explicitly "mostly audit + assertion of code that already exists" (plan objective). The tests therefore passed GREEN on first run by design; there was no failing-RED phase to author because the implementation is pre-existing and the requirement is to LOCK it. Both commits are `test(...)`; no `feat(...)` was needed (no new production behavior added).

## Known Stubs

None. All three deliverables assert/document existing production code paths. Events marked `missing` in the parity doc (`switchToProfile`, `systemDidWakeUp`, `applicationDidLaunch/Terminate`) are explicitly scoped to EVENT-03/APROF-04 (Plans 03/04) with their RED scaffolds already registered in Plan 01 -- documented in the doc, not stubbed here.

## Threat Flags

None. The plan introduces only tests + a doc; no new network endpoint, auth path, file access, or schema surface. The threat register's two `mitigate` items (T-34-02-01 willAppear shape drift, T-34-02-02 controller token mismatch) are now covered by the EVENT-02 and EVENT-04 tests respectively.

## Self-Check: PASSED

All 3 created files exist on disk; all 3 task commits (205abba, 806f9cc, 6aa67eb) are present in git history.

______________________________________________________________________

*Phase: 34-per-app-profiles-event-parity-audit*
*Completed: 2026-06-08*
