---
phase: 17-plugin-protocol-completion
status: secured
asvs_level: 1
block_on: critical
threats_total: 11
threats_closed: 11
threats_open: 0
unregistered_flags: 0
register_authored_at_plan_time: true
audited: '2026-05-24'
---

# Phase 17 Security Audit — plugin-protocol-completion

**Audit date:** 2026-05-24
**ASVS Level:** 1
**Block on:** critical
**Auditor:** gsd-security-auditor (Claude Sonnet 4.6)

______________________________________________________________________

## Verdict: SECURED

**Threats Closed:** 11/11
**Threats Open:** 0/11
**Unregistered Flags:** 0

______________________________________________________________________

## Threat Verification

| Threat ID                   | Category                    | Disposition | Status | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| --------------------------- | --------------------------- | ----------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| T-17-LAN                    | Info Disclosure / Elevation | mitigate    | CLOSED | `sd_plugin_server.cpp:92` — `m_server->listen(QHostAddress::LocalHost, port)`; `bindAddress()` at :147 returns hardcoded `LocalHost`; no bind-widening setter exists in `sd_plugin_server.hpp` (grep for `setBindAddress`/`AnyIPv4`/`QHostAddress::Any` in hpp returns no setter declaration). Test re-pins invariant at `test_sd_plugin_server.cpp:59-70` and :331-343.                                                                                                                    |
| T-17-JSON                   | DoS                         | mitigate    | CLOSED | `sd_plugin_server.cpp:206-213` — `QJsonDocument::fromJson` with `QJsonParseError` guard; malformed/non-object frames return early before `dispatchClientMessage` is called. The `eventName` string is extracted from the validated object before any routing.                                                                                                                                                                                                                               |
| T-17-UAF                    | Tampering / DoS             | mitigate    | CLOSED | `sd_plugin_server.cpp:472-485` — `socketForUuid` scans `m_connections` on each `sendEvent` call (`sendEvent` at :491-518 calls `socketForUuid` at :495); result is never cached. After `connIt->socket->close()` at :362 the function returns immediately at :365 (`return;`) — `connIt` is never dereferenced again.                                                                                                                                                                       |
| T-17-BRUTE                  | Spoofing                    | mitigate    | CLOSED | `sd_plugin_server.cpp:34` — `constexpr int kMaxAuthAttempts = 5`; counter increment at :349 (`++connIt->authAttempts`); close at :362 when `authAttempts >= kMaxAuthAttempts`. Regression test `PluginAuthTest rejectsAfter5BadAttempts` at `test_sd_plugin_server.cpp:651` asserts socket closed on 5th attempt and stays open at 4.                                                                                                                                                       |
| T-17-REPLAY                 | Spoofing                    | mitigate    | CLOSED | `sd_plugin_server.cpp:40-51` — `makeRandomSaltHex()` fills 16 bytes via `QRandomGenerator::system()->generate()` (not `rand()`); called per-connection at :271 (`it->salt = makeRandomSaltHex()`). Each connection receives a unique salt before the passHello handshake.                                                                                                                                                                                                                   |
| T-17-CRYPTO                 | Info Disclosure             | mitigate    | CLOSED | `sd_plugin_server.cpp:60-63` — `challengeFor()` uses `QCryptographicHash::hash(..., QCryptographicHash::Sha256)` (QtCore, no extra link target); salt sourced from `QRandomGenerator::system()`. No `rand()`, no hand-rolled hash. Zero `nlohmann` references in both `sd_plugin_server.cpp` and `sd_plugin_server.hpp` (COD-031 clean).                                                                                                                                                    |
| T-17-PREAUTH (CR-01)        | Elevation                   | mitigate    | CLOSED | `sd_plugin_server.cpp:422-438` — pre-auth gate at lines 429-438, placed after the `kRoutedActions` array definition (:375) and before the dispatch loop (:441). Checks `connForAuth->authenticated`; returns immediately with `AJAZZ_LOG_WARN` if false. Regression test `PluginAuthTest unauthenticated socket cannot trigger actionReceived` at `test_sd_plugin_server.cpp:697` sends three routed actions without auth and asserts `actionSpy.count() == 0`.                             |
| T-17-PREAUTH (CR-02)        | Elevation                   | mitigate    | CLOSED | `sd_plugin_server.cpp:300-307` — `emit pluginRegistered(uuid)` in `registerPlugin` branch is gated on `m_password.isEmpty()`; when password is set, emission is deferred. Deferred emit at :347 inside the `authentication` success branch (`connIt->authenticated = true` at :341 then `emit pluginRegistered(connIt->uuid)` at :347). Test `PluginAuthTest acceptsCorrectChallenge` at `test_sd_plugin_server.cpp:629` asserts `registeredSpy.count() == 0` before auth and `== 1` after. |
| T-17-PREAUTH (CR-03)        | Spoofing                    | mitigate    | CLOSED | `sd_plugin_server.cpp:236-252` — UUID collision guard via `std::find_if` checking `c.uuid == uuid && c.socket != nullptr && c.socket != client`; if found, calls `client->close()` and returns immediately. Regression test `PluginAuthTest duplicate UUID registration rejected` at `test_sd_plugin_server.cpp:745` verifies impostor is disconnected and legitimate connection remains with `connectedPluginCount() == 1`.                                                                |
| T-17-FWD                    | Tampering                   | accept      | CLOSED | Accepted by design: `sd_plugin_server.cpp:452-459` — genuinely-unknown events fall through to `emit unhandledEventReceived(senderUuid, eventName)` for forward-compat tracing. Test at `test_sd_plugin_server.cpp:175-209` confirms `someEventThatDoesNotExist` reaches `unhandledEventReceived`.                                                                                                                                                                                           |
| T-17-DEVFWD / T-17-DEVTRUST | Tampering                   | accept      | CLOSED | Accepted by design: `sendToDevice` is present in `kRoutedActions` at `sd_plugin_server.cpp:398` and routes only to `emit actionReceived(senderUuid, msg)` at :449. No raw byte forwarding to a HID device occurs anywhere in `sd_plugin_server.cpp`. Device-write trust is deferred to Phase 19.                                                                                                                                                                                            |
| T-17-TIMING                 | Info Disclosure             | accept      | CLOSED | Accepted by design: `sd_plugin_server.cpp:335-337` — plain `QString ==` challenge compare; documented inline as acceptable for the loopback/no-TLS threat model. Comment cites T-17-TIMING and the plan threat register.                                                                                                                                                                                                                                                                    |
| T-17-NOTLS                  | Info Disclosure             | accept      | CLOSED | Accepted by design: `sd_plugin_server.hpp:28` — "No TLS — loopback-only by design (accepted constraint)" documented in Doxygen header; `sd_plugin_server.cpp:88-92` confirms the server is constructed in `NonSecureMode` and binds only loopback.                                                                                                                                                                                                                                          |
| T-17-SC                     | Tampering                   | n/a         | CLOSED | Zero external packages: all crypto (`QCryptographicHash`, `QRandomGenerator`) is QtCore in-tree. No `nlohmann` in `sd_plugin_server.cpp` or `sd_plugin_server.hpp`.                                                                                                                                                                                                                                                                                                                         |

______________________________________________________________________

## Accepted Risks Log

| Threat ID                   | Rationale                                                                                                                                                                                                                                                | Location                                                |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------- |
| T-17-FWD                    | Unknown events surface via `unhandledEventReceived` (not silently dropped) — explicit forward-compat design choice. The 41 routed actions narrow the unhandled surface; the fallthrough is a debugging/extensibility net, not a security gap at ASVS L1. | `sd_plugin_server.cpp:452-459`                          |
| T-17-DEVFWD / T-17-DEVTRUST | Phase 17 routes `sendToDevice` to `actionReceived` only; actual device writes are Phase 19. No raw HID byte forwarding exists in this phase.                                                                                                             | `sd_plugin_server.cpp:398,449`                          |
| T-17-TIMING                 | Plain `QString ==` for challenge compare. Acceptable for loopback-only, no-TLS server where any local process already has OS privilege. A constant-time compare would not strengthen the threat model materially.                                        | `sd_plugin_server.cpp:335-337`                          |
| T-17-NOTLS                  | No transport encryption. Server is loopback-bound (`ws://127.0.0.1`); JS shim hardcodes this address. Accepted design constraint per CONTEXT.md.                                                                                                         | `sd_plugin_server.hpp:28`, `sd_plugin_server.cpp:88-92` |

______________________________________________________________________

## Unregistered Flags

None. The 17-01, 17-02, and 17-03 SUMMARY.md files each report "Threat Flags: None". No new network endpoints, bind changes, auth paths, or file-access patterns were introduced beyond what the threat register covers.

______________________________________________________________________

## Critical Fix Verification (17-REVIEW.md CR-01/CR-02/CR-03)

The three auth-bypass blockers identified in the deep security review are confirmed PRESENT in the current code, not merely claimed in the fix report:

- **CR-01** (`sd_plugin_server.cpp:422-438`): pre-auth gate — `connForAuth->authenticated` checked; unauthenticated connection drops at `:437 return;` before `emit actionReceived`.
- **CR-02** (`sd_plugin_server.cpp:300-307, 344-347`): `pluginRegistered` emission conditioned on `m_password.isEmpty()` at registration time; deferred `emit pluginRegistered(connIt->uuid)` fires only after `connIt->authenticated = true` in the `authentication` success branch.
- **CR-03** (`sd_plugin_server.cpp:236-252`): UUID collision scan before `it->uuid = uuid`; impostor closed via `client->close()` at `:250`; `return` at `:251` prevents any further processing.

Regression tests for CR-01 and CR-03 are present:

- `test_sd_plugin_server.cpp:697` — `PluginAuthTest unauthenticated socket cannot trigger actionReceived`
- `test_sd_plugin_server.cpp:745` — `PluginAuthTest duplicate UUID registration rejected`

______________________________________________________________________

*Audit performed by gsd-security-auditor. Implementation files were read-only. This file records audit findings only.*
