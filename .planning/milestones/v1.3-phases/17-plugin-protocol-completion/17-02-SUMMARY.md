---
phase: 17-plugin-protocol-completion
plan: 02
subsystem: plugin-protocol
tags: [websocket, elgato, stream-deck, qt6, catch2, tdd, host-to-plugin, sendEvent]

# Dependency graph
requires:
  - phase: 17-01
    provides: sizeless kRoutedActions (41), SdPluginServer m_connections vector established
provides:
  - SdPluginServer::sendEvent(uuid, eventName, payload) - public host->plugin seam
  - SdPluginServer::socketForUuid(uuid) - private forward lookup helper
  - dialRotate loopback round-trip test with ticks/pressed/controller verification
  - unknown-uuid false return test (T-17-UAF Pitfall 4 coverage)
  - 10-event §4.4 surface coverage test
affects: [17-03, 19, 20, 21]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Re-resolve live socket on every sendEvent call (never cache raw QWebSocket* - T-17-UAF / Pitfall 4)
    - 'TDD RED/GREEN: test commit before implementation commit, verified via ctest'

key-files:
  created: []
  modified:
    - src/app/src/sd_plugin_server.cpp
    - src/app/src/sd_plugin_server.hpp
    - tests/unit/test_sd_plugin_server.cpp

key-decisions:
  - sendEvent re-resolves live slot via socketForUuid on every call; raw QWebSocket* never cached (T-17-UAF / Pitfall 4)
  - payload key omitted from envelope when QJsonObject is empty (keeps wire minimal)
  - socketForUuid filters on uuid == targetUuid && socket != nullptr (live-slot-only invariant)
  - Three new TEST_CASEs cover dialRotate round-trip, unknown-uuid false path, and 10-event surface

patterns-established:
  - 'host->plugin sendEvent seam: socketForUuid + sendTextMessage(QJsonDocument::Compact) pattern'

requirements-completed: [PLUGIN-04]

# Metrics
duration: 3min
completed: 2026-05-24
---

# Phase 17 Plan 02: Host-to-Plugin sendEvent Seam Summary

**sendEvent(uuid, eventName, payload) wires the host->plugin direction with a per-call live-socket lookup (T-17-UAF guard); 3 new loopback tests cover dialRotate round-trip, unknown-uuid false path, and a 10-event §4.4 surface sweep**

## Performance

- **Duration:** ~3 min
- **Started:** 2026-05-24T11:20:03Z
- **Completed:** 2026-05-24T11:23:00Z
- **Tasks:** 2 (TDD: 2 commits)
- **Files modified:** 3

## Accomplishments

- Added `bool sendEvent(QString const& targetUuid, QString const& eventName, QJsonObject const& payload = {})` to the public API of `SdPluginServer`
- Added private `QWebSocket* socketForUuid(QString const& uuid) const` — forward-direction mirror of the existing `uuidForClient` reverse lookup
- `socketForUuid` filters on `uuid == targetUuid && socket != nullptr` so a disconnected-but-not-erased slot never produces a dangling pointer
- `sendEvent` re-resolves the live socket on EVERY call (T-17-UAF / Pitfall 4 guard: if a future auth rejection in 17-03 closes the socket, the next sendEvent call returns false instead of writing to freed memory)
- Envelope: `{"event": eventName}` + optional `"payload"` key (omitted when `QJsonObject::isEmpty()`) — correct per spec §4.2
- `AJAZZ_LOG_DEBUG` on both the send path and the no-socket path
- Updated hpp Doxygen: `**Host-to-plugin event sender**` section documents the seam; dropped `Host-to-plugin sendEvent writer (17-02)` from the NOT YET IMPLEMENTED list
- COD-031 clean: `QJsonObject` / `QJsonDocument` only throughout; zero `nlohmann` references
- 3 new TEST_CASEs added to `test_sd_plugin_server.cpp`:
  1. `SdPluginProtocolTest roundTrip dialRotate carries ticks pressed controller` — loopback round-trip; asserts `event=="dialRotate"`, `payload.ticks==2`, `payload.controller=="Encoder"`, `payload.pressed==false`
  1. `SdPluginProtocolTest sendEvent returns false for unknown uuid` — asserts `sendEvent` returns false and no frame arrives at the client
  1. `SdPluginProtocolTest host to plugin events arrive` — data-driven sweep of 10 representative §4.4 event names; each must arrive with the correct `event` field
- Full suite green: 451/451 tests pass (was 448 before 17-02)

## Task Commits

Each task was committed atomically following TDD RED/GREEN:

1. **Task 1+2 RED: failing tests for sendEvent seam** - `a32a488` (test)
1. **Task 1+2 GREEN: implement sendEvent + socketForUuid** - `9ac2bdc` (feat)

## Files Created/Modified

- `src/app/src/sd_plugin_server.hpp` - Added `sendEvent` public declaration + `socketForUuid` private declaration; updated Doxygen header to document host->plugin event sender seam
- `src/app/src/sd_plugin_server.cpp` - Implemented `socketForUuid` (forward uuid->socket lookup) and `sendEvent` (compact-JSON envelope writer with per-call live-slot re-resolution)
- `tests/unit/test_sd_plugin_server.cpp` - Added 3 new TEST_CASEs for dialRotate round-trip, unknown-uuid false, and 10-event §4.4 surface coverage; added `QJsonDocument`, `QJsonObject`, `QSet` includes

## Decisions Made

- **Per-call socket re-resolution**: `sendEvent` calls `socketForUuid` on every invocation rather than caching a `QWebSocket*` between calls. This is the T-17-UAF mitigation from Pitfall 4 — if the 17-03 auth rejection closes a socket, no dangling pointer can persist.
- **Empty payload omitted**: `"payload"` key is only inserted in the envelope when `!payload.isEmpty()`. This keeps the wire format minimal for no-payload events like `systemDidWakeUp` and matches the Elgato v6 pattern.
- **socketForUuid filters live slots only**: The predicate is `uuid == targetUuid && socket != nullptr`. An erased slot (WR-07 pattern from 17-01) never lingers in `m_connections` so this guard is belt-and-suspenders, but the null check is kept for safety.
- **Tests filter by event name in msgSpy**: `msgSpy.last()` alone would be fragile once 17-03 adds a `passHello` frame. The tests iterate `msgSpy` and match by `event` field — resilient to future protocol additions.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- clang-format reformatted staged files on both commits (pre-commit hook). Standard pattern: re-stage after format and retry. Required 2 re-stages total across 2 commits. No impact on deliverable.

## Security Threat Coverage

- **T-17-UAF**: `sendEvent` re-resolves the live slot via `socketForUuid` on every call; raw pointer never cached. Guards the use-after-free that 17-03's reject-after-5 socket close would otherwise expose.
- **T-17-LAN**: LocalHost-only bind preserved and untouched; this plan adds no listener and no bind setter.
- **T-17-SC**: Zero external packages; COD-031 clean (`nlohmann` absent from `sd_plugin_server.cpp`).

## Threat Flags

None - no new network endpoints, auth paths, or file access patterns introduced.

## Known Stubs

None - `sendEvent` writes real frames to live sockets. No placeholder data. Phase 19 wires real device input to this seam.

## Next Phase Readiness

- 17-03 (passHello/auth) can proceed immediately - `sendEvent` is the seam it uses to send `passHello` after `registerPlugin`
- Phase 19 bridge calls `sendEvent(uuid, "dialRotate", {...})` for real encoder input; seam is ready

## Self-Check: PASSED

- sd_plugin_server.cpp: FOUND
- sd_plugin_server.hpp: FOUND
- test_sd_plugin_server.cpp: FOUND
- 17-02-SUMMARY.md: FOUND
- a32a488 (RED tests): FOUND
- 9ac2bdc (GREEN impl): FOUND

______________________________________________________________________

*Phase: 17-plugin-protocol-completion*
*Completed: 2026-05-24*
