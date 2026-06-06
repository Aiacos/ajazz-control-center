---
phase: 17-plugin-protocol-completion
plan: 01
subsystem: plugin-protocol
tags: [websocket, elgato, stream-deck, qt6, catch2, tdd, protocol-routing]

# Dependency graph
requires:
  - phase: P3.16-mvp
    provides: SdPluginServer with 13-action kStandardActions, LocalHost bind, registerPlugin handshake
provides:
  - sizeless kRoutedActions (CTAD, 15 standard + 26 AJAZZ = 41 total) replacing kStandardActions[13]
  - all 41 plugin->host actions route via actionReceived; none reach unhandledEventReceived
  - genuinely-unknown events still surface via unhandledEventReceived (forward-compat T-17-FWD)
  - TDD test suite: all-routed-actions case, envelope round-trip, loopback re-pin, inverted setBG case
affects: [17-02, 17-03, 18, 19, 20, 21]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Sizeless CTAD std::array for protocol dispatch tables (no explicit size = count can never drift)
    - 'TDD RED/GREEN: test commit before implementation commit, verified via ctest'

key-files:
  created: []
  modified:
    - src/app/src/sd_plugin_server.cpp
    - src/app/src/sd_plugin_server.hpp
    - tests/unit/test_sd_plugin_server.cpp

key-decisions:
  - kRoutedActions is CTAD sizeless (no explicit size) so count is derived from literal, not hardcoded
  - setFeedback belongs in standard routed group (spec 4.3 marks it Standard Elgato? yes)
  - registerPlugin / registerPropertyInspector NOT in kRoutedActions (handled in earlier branch)
  - 'Routing only (Phase 17): host-capability actions and device-targeting actions surface via actionReceived; fulfillment is Phase 19'
  - Test name uses hyphen and ASCII-only (CLAUDE.md Win32 ctest codepage requirement)

patterns-established:
  - 'Protocol dispatch tables: sizeless CTAD std::array, linear scan, compile-time count derived from literal'

requirements-completed: [PLUGIN-01, PLUGIN-02, PLUGIN-03]

# Metrics
duration: 5min
completed: 2026-05-24
---

# Phase 17 Plan 01: Plugin Protocol Routing Summary

**Replaced 13-action kStandardActions with sizeless kRoutedActions (CTAD, 41 entries: 15 standard + 26 AJAZZ) — all plugin->host actions now route via actionReceived; zero fall through to unhandledEventReceived**

## Performance

- **Duration:** ~5 min
- **Started:** 2026-05-24T11:12:15Z
- **Completed:** 2026-05-24T11:17:15Z
- **Tasks:** 2 (TDD: 3 commits each)
- **Files modified:** 3

## Accomplishments

- Replaced dead `kStandardActions[13]` (included `registerPlugin`/`registerPropertyInspector` as
  dead entries) with sizeless `static constexpr std::array kRoutedActions = {...}` (CTAD; count
  derived from literal, cannot drift)
- 41 routed actions total per spec 4.3: 15 standard Elgato (including `setFeedback` as standard)
  - 26 AJAZZ-only (from `setBG` through `sendUserInfo`)
- Genuinely-unknown events still surface via `unhandledEventReceived` (T-17-FWD forward-compat)
- LocalHost-only bind invariant preserved (T-17-LAN); QJsonObject only — no nlohmann (COD-031)
- Updated hpp Doxygen: dropped "26 AJAZZ extensions not implemented" from NOT YET IMPLEMENTED list
- Inverted the obsolete `setBG -> unhandledEventReceived` MVP test; added `setBG -> actionReceived`
  assertion plus a `someEventThatDoesNotExist -> unhandledEventReceived` forward-compat assertion
- Added all-routed-actions case (count derived from test-local name list, not hardcoded), envelope
  round-trip case (all 5 keys verbatim), and loopback re-pin case (PLUGIN-01)
- Full suite green: 448/448 tests pass

## Task Commits

Each task was committed atomically:

1. **Task 1 RED: failing tests for 41-action kRoutedActions routing** - `65d3a77` (test)
1. **Task 1 GREEN: replace kStandardActions[13] with sizeless kRoutedActions (41)** - `b417eca` (feat)
1. **Task 2: invert setBG->unhandled MVP test; add setBG->action assertion** - `9634f4e` (test)

## Files Created/Modified

- `src/app/src/sd_plugin_server.cpp` - Replaced `kStandardActions[13]` with sizeless `kRoutedActions`
  (41 entries per spec 4.3 §4.3); updated dispatch loop and comment block
- `src/app/src/sd_plugin_server.hpp` - Updated Doxygen header: dropped "26 AJAZZ extensions" from
  NOT YET IMPLEMENTED; updated description line to reflect 15+26=41 routing
- `tests/unit/test_sd_plugin_server.cpp` - Added 3 new cases (all-routed-actions, envelope round-trip,
  loopback re-pin); inverted setBG->unhandled to setBG->actionReceived + unknownEvent->unhandled

## Decisions Made

- **CTAD sizeless array:** `static constexpr std::array kRoutedActions = {...}` with no explicit size
  annotation. The count is computed by the compiler from the literal — cannot drift. Satisfies the
  plan's "sizeless" requirement and the `<artifact>` spec.
- **setFeedback in standard group:** Confirmed by spec 4.3 table ("Standard Elgato? yes"). The plan
  explicitly flags this to prevent misclassification into the AJAZZ group.
- **registerPlugin / registerPropertyInspector excluded:** These are handled in the pre-routing branch
  (lines 185-206, early return) — including them in kRoutedActions would be dead/duplicate code.
- **Test count derived from name list size:** `kExpectedCount = sizeof(kAllRoutedNames) / sizeof(...)`.
  This means if kRoutedActions in source ever changes, the test list must be updated to match — the
  test comment documents "must equal kRoutedActions.size() in the source (currently 41)".

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- clang-format reformatted staged files on each commit (pre-commit hook). Standard pattern: re-stage
  after format and retry. Required 2 re-stages total across 3 commits. No impact on deliverable.

## Security Threat Coverage

All T-17 mitigations addressed:

- **T-17-LAN**: `QHostAddress::LocalHost` bind preserved; re-pinned by loopback test case
- **T-17-JSON**: Existing `QJsonDocument::fromJson` guard retained; routed-set scan runs only on valid objects
- **T-17-FWD**: genuinely-unknown events still surface via `unhandledEventReceived` (not silently dropped)
- **T-17-DEVFWD**: `sendToDevice` routes via `actionReceived` only; no raw byte forwarding (Phase 19)
- **T-17-SC**: No external packages installed; COD-031 clean (`nlohmann` absent from `sd_plugin_server.cpp`)

## Threat Flags

None — no new network endpoints, auth paths, or file access patterns introduced.

## Known Stubs

None — this plan only routes actions to `actionReceived`; fulfillment of host-capability actions
(`getScreenshot`, `getSystemAudioVolume`, `getUserInfo`, `getDetectedSensorsData`) is Phase 19.
The routing itself is complete (not stubbed).

## Next Phase Readiness

- 17-02 (host->plugin sendEvent writer) can proceed immediately — `actionReceived` / `unhandledEventReceived` signals unchanged
- 17-03 (passHello/auth) can proceed immediately — the `registerPlugin` branch (lines 185-206) is untouched
- Phase 19 bridge consumes `actionReceived(uuid, msg)` for device-targeting actions

## Self-Check: PASSED

- sd_plugin_server.cpp: FOUND
- sd_plugin_server.hpp: FOUND
- test_sd_plugin_server.cpp: FOUND
- 17-01-SUMMARY.md: FOUND
- 65d3a77 (RED tests): FOUND
- b417eca (GREEN impl): FOUND
- 9634f4e (test inversion): FOUND

______________________________________________________________________

*Phase: 17-plugin-protocol-completion*
*Completed: 2026-05-24*
