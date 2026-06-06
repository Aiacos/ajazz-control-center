---
phase: 17-plugin-protocol-completion
plan: 03
subsystem: plugin-protocol
tags: [websocket, elgato, stream-deck, qt6, catch2, tdd, auth, passHello, sha256, security]

# Dependency graph
requires:
  - phase: 17-01
    provides: kRoutedActions (41) set established
  - phase: 17-02
    provides: sendEvent seam + socketForUuid (passHello uses sendEvent to emit)
provides:
  - SdPluginServer::setPasswordForTesting() - test-only password setter
  - passHello emission after registerPlugin with NESTED authentication:{challenge,salt}
  - authentication message handling (before action routing, T-17-PREAUTH)
  - Per-connection brute-force rejection (kMaxAuthAttempts=5, T-17-BRUTE)
  - Per-connection random salt (T-17-REPLAY mitigation)
  - challengeFor() helper (sha256(password+saltHex), mirrors single_instance_guard.cpp)
  - PluginAuthTest passHello-salt, acceptsCorrectChallenge, rejectsAfter5BadAttempts
affects: [18, 19, 20, 21]

# Tech tracking
tech-stack:
  added:
    - QCryptographicHash::Sha256 (QtCore, no new link target)
    - QRandomGenerator::system() (QtCore, no new link target)
  patterns:
    - Per-connection random salt via QRandomGenerator::system() -- T-17-REPLAY guard
    - challengeFor() = SHA-256(password.toUtf8() + saltHex.toUtf8()) -- OUR auth contract
    - authentication branch placed BEFORE kRoutedActions scan -- T-17-PREAUTH
    - Return immediately after socket->close() -- never touch connIt after closure (T-17-UAF)
    - NESTED authentication:{challenge,salt} -- CONTEXT.md locks this; spec §4.5 top-level superseded
    - TDD RED/GREEN: test commit before implementation commit, verified via ctest

key-files:
  created: []
  modified:
    - src/app/src/sd_plugin_server.cpp
    - src/app/src/sd_plugin_server.hpp
    - tests/unit/test_sd_plugin_server.cpp

key-decisions:
  - NESTED authentication:{challenge,salt} shape locked by CONTEXT.md (spec §4.5 top-level is SUPERSEDED)
  - host-side challenge is empty string in passHello when no password; plugin computes sha256(password+salt)
  - authentication handled BEFORE kRoutedActions scan so it is never treated as a routed action
  - Return immediately after socket->close() -- T-17-UAF Pitfall 4 -- connIt is invalid after close
  - Plain QString == comparison for challenge -- T-17-TIMING accepted for loopback/no-TLS threat model
  - kMaxAuthAttempts=5 -- T-17-BRUTE brute-force mitigation
  - setPasswordForTesting() is PUBLIC (test-only) not private; production default is empty=no-password-accept
  - connectedPluginCount() semantics unchanged -- registered=counted regardless of auth state (Pitfall 5)

patterns-established:
  - 'auth gate: authentication branch + kMaxAuthAttempts=5 pattern for loopback plugin servers'

requirements-completed: [PLUGIN-05]

# Metrics
duration: 5min
completed: 2026-05-24
---

# Phase 17 Plan 03: passHello+Salt Auth Handshake Summary

**passHello with per-connection random NESTED salt sent after registerPlugin; sha256(password+salt) challenge accepted; socket closed after 5 bad attempts (kMaxAuthAttempts); 3 new loopback auth tests cover salt presence, correct acceptance, and rejection at attempt 5**

## Performance

- **Duration:** ~5 min
- **Started:** 2026-05-24T11:25:49Z
- **Completed:** 2026-05-24T11:30:57Z
- **Tasks:** 3 (TDD: 2 commits: RED + GREEN)
- **Files modified:** 3

## Accomplishments

- Extended `PluginConnection` struct with three new fields: `salt` (hex-encoded per-connection random), `authAttempts` (bad-challenge counter), `authenticated` (connection auth state)
- Added `QString m_password` private member to `SdPluginServer` (default empty = no-password-accept)
- Added `void setPasswordForTesting(QString const& password)` public method — test-only setter that enables `PluginAuthTest::rejectsAfter5BadAttempts`; production code never calls this
- `makeRandomSaltHex()`: 16-byte `QRandomGenerator::system()->generate()` fill, `.toHex()` — T-17-REPLAY guard (per-connection salt makes captured challenges useless on reconnect)
- `challengeFor(password, saltHex)`: `QCryptographicHash::hash(password.toUtf8()+saltHex.toUtf8(), Sha256).toHex()` — mirrors `single_instance_guard.cpp:85` idiom exactly; defines OUR auth contract (§17-RESEARCH A3)
- `registerPlugin` branch extended: after binding uuid, generates salt, sets `authenticated = m_password.isEmpty()`, builds the NESTED `authentication:{challenge:"", salt:hexSalt}` payload (CONTEXT.md LOCKED shape; spec §4.5 top-level superseded per §4.4+CONTEXT), emits via the 17-02 `sendEvent` seam
- New `authentication` message branch: placed BEFORE `kRoutedActions` scan (T-17-PREAUTH); verifies `challengeFor(m_password, salt) == msg["challenge"]`; on match sets `authenticated=true`; on mismatch `++authAttempts`; at `authAttempts >= kMaxAuthAttempts` closes socket and returns immediately (never touches `connIt` after `close()` — T-17-UAF / Pitfall 4)
- `constexpr int kMaxAuthAttempts = 5` in anon namespace (T-17-BRUTE brute-force mitigation)
- Added `#include <QByteArray>`, `#include <QCryptographicHash>`, `#include <QRandomGenerator>` to the .cpp (QtCore, no new link target)
- COD-031 clean: `QJsonObject` / `QJsonDocument` only; zero `nlohmann` references in the server
- T-17-TIMING accepted: plain `QString ==` for challenge compare (loopback/no-TLS threat model — documented)
- T-17-NOTLS accepted: no TLS by design — loopback-only constraint (documented)
- 3 new `TEST_CASE`s in `test_sd_plugin_server.cpp`:
  1. `PluginAuthTest passHello carries salt after registerPlugin` — asserts NESTED `payload.authentication.salt` is non-empty; `findFrameByEvent()` helper filters spy by event name (resilient to ordering)
  1. `PluginAuthTest acceptsCorrectChallenge` — calls `setPasswordForTesting("pw")`; registers; extracts salt from passHello; computes `sha256("pw"+salt)` in the test (mirrors `challengeFor()`); sends correct auth reply; asserts socket stays connected
  1. `PluginAuthTest rejectsAfter5BadAttempts` — sends 4 wrong challenges and asserts socket still open after each; 5th wrong challenge triggers `waitForSpy(disconnectedSpy)`; confirms server closes on attempt 5
- Full suite: 454/454 tests pass (was 451 before 17-03; 3 new auth tests + all prior tests green)

## Task Commits

Tasks executed as two TDD commits following RED/GREEN:

1. **RED: failing auth tests** - `9f2ea34` (test)
1. **GREEN: passHello+salt+auth implementation** - `d9fd224` (feat)

## Files Created/Modified

- `src/app/src/sd_plugin_server.hpp` - Extended `PluginConnection` struct (salt/authAttempts/authenticated); added `m_password` member; added `setPasswordForTesting()` public declaration; updated Doxygen header to document Authentication section; removed "passHello/salt/challenge auth handshake (17-03)" from NOT YET IMPLEMENTED list
- `src/app/src/sd_plugin_server.cpp` - Added `makeRandomSaltHex()` + `challengeFor()` anon-namespace helpers; `kMaxAuthAttempts=5` constant; `setPasswordForTesting()` body; passHello emission in registerPlugin branch; `authentication` message branch before kRoutedActions scan; updated push_back initializer to include new PluginConnection fields; updated NOTE comment on kRoutedActions
- `tests/unit/test_sd_plugin_server.cpp` - Added `#include <QCryptographicHash>`; `findFrameByEvent()` helper; 3 new PluginAuthTest TEST_CASEs (passHello-salt, acceptsCorrectChallenge, rejectsAfter5BadAttempts)

## Decisions Made

- **NESTED authentication shape**: `payload.authentication.{challenge, salt}` — locked by CONTEXT.md (spec §4.5 shows top-level `salt` but §4.4 and CONTEXT show nested; CONTEXT wins). A one-line comment in the code cites this contradiction.
- **Host-side challenge empty in passHello**: when no password is configured, the host sends `challenge: ""` (empty). The plugin is the one that computes `sha256(password+salt)` in its reply. This matches §17-RESEARCH open question 2.
- **authentication branch before kRoutedActions**: T-17-PREAUTH — if `authentication` fell through to the routing set it would not match any of the 41 actions and would reach `unhandledEventReceived`. The explicit branch prevents this.
- **Return after socket->close()**: T-17-UAF / Pitfall 4 — `connIt` is an iterator into `m_connections`. After `close()`, `onClientDisconnected` fires synchronously (or on next event loop tick) and erases the entry. Never dereference `connIt` after calling `close()`.
- **setPasswordForTesting public, not private**: the method must be accessible from the test TU. Clearly documented as test-only with a `@warning`.
- **connectedPluginCount semantics unchanged**: counts registered (non-empty-uuid) slots regardless of `authenticated` state. This preserves prior test expectations (Pitfall 5).

## Deviations from Plan

**1. [Rule 1 - Bug] Missing-field initializer in push_back**

- **Found during:** Task 1 (GREEN), first compile attempt
- **Issue:** `m_connections.push_back({QString{}, client})` became a `-Werror=missing-field-initializers` compile failure after adding `salt`, `authAttempts`, `authenticated` fields to `PluginConnection`
- **Fix:** Updated to `m_connections.push_back({QString{}, client, QString{}, 0, false})`
- **Files modified:** `src/app/src/sd_plugin_server.cpp`
- **Commit:** included in `d9fd224`

## Security Threat Coverage

- **T-17-BRUTE**: `kMaxAuthAttempts=5` reject-after-5 path; `PluginAuthTest::rejectsAfter5BadAttempts` pins it with a loopback test
- **T-17-REPLAY**: `makeRandomSaltHex()` via `QRandomGenerator::system()` — per-connection entropy; captured challenge unusable on reconnect
- **T-17-CRYPTO**: `QCryptographicHash::Sha256` — never hand-rolled; salt via `QRandomGenerator` not `rand()`
- **T-17-PREAUTH**: `authentication` branch executes before `kRoutedActions` scan; never reaches `unhandledEventReceived`
- **T-17-UAF**: `return` immediately after `socket->close()` — never re-touches `connIt`; `sendEvent` re-resolves live slot each call (17-02 guard still in place)
- **T-17-TIMING**: plain `==` accepted — loopback/no-TLS threat model; documented as non-blocker
- **T-17-NOTLS**: no TLS — loopback-only accepted constraint; documented in header Doxygen
- **T-17-SC**: zero external packages; COD-031 clean (`nlohmann` absent)

## Threat Flags

None — no new network endpoints, bind changes, or file access patterns. The auth gate is an additive security layer on the existing loopback-bound server.

## Known Stubs

None — passHello emits a real `deviceInfo: {}` empty-object placeholder (Phase 19 fills with real device info). The stub is intentional and non-blocking: the salt/challenge handshake is fully functional without device info. Future plan: Phase 19 wires the real device descriptor into `helloPayload.deviceInfo`.

## TDD Gate Compliance

- RED commit `9f2ea34`: `test(17-03): add RED tests...` — compile failure confirmed (RED gate met)
- GREEN commit `d9fd224`: `feat(17-03): implement passHello+salt...` — 454/454 tests pass (GREEN gate met)

## Self-Check: PASSED

- sd_plugin_server.cpp: FOUND
- sd_plugin_server.hpp: FOUND
- test_sd_plugin_server.cpp: FOUND
- 17-03-SUMMARY.md: FOUND
- 9f2ea34 (RED tests): FOUND
- d9fd224 (GREEN impl): FOUND
- 454/454 ctest pass confirmed

______________________________________________________________________

*Phase: 17-plugin-protocol-completion*
*Completed: 2026-05-24*
