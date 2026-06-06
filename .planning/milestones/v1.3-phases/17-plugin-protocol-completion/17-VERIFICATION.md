---
phase: 17-plugin-protocol-completion
verified: 2026-05-24T15:00:00Z
status: passed
score: 14/14
overrides_applied: 0
---

# Phase 17: Plugin Protocol Completion — Verification Report

**Phase Goal:** The existing SdPluginServer gains the AJAZZ message surface + auth so any Elgato/Mirabox plugin's full protocol is honored.
**Verified:** 2026-05-24T15:00:00Z
**Status:** PASSED
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| #   | Truth                                                                                                                                                                    | Status   | Evidence                                                                                                                                                                                                                                                            |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | WS server binds QHostAddress::LocalHost only, never Any, on a random free port                                                                                           | VERIFIED | `m_server->listen(QHostAddress::LocalHost, port)` at line 92; `bindAddress()` returns hardcoded LocalHost; no setter exists; test #437 and #429 pin the invariant and pass                                                                                          |
| 2   | All 41 plugin->host actions (15 standard + 26 AJAZZ per spec 4.3) route via actionReceived — none reach unhandledEventReceived                                           | VERIFIED | `static constexpr std::array kRoutedActions` (CTAD, 41 entries verified by count) at line 375; linear scan emits `actionReceived` on match; test #435 ("all routed actions route none unhandled") passes with `unhandledSpy.count()==0` and `actionSpy.count()==41` |
| 3   | A genuinely-unknown event name still surfaces via unhandledEventReceived (forward-compat preserved)                                                                      | VERIFIED | Fallthrough at line 459 emits `unhandledEventReceived`; test #434 sends "someEventThatDoesNotExist" and asserts it reaches `unhandledSpy`                                                                                                                           |
| 4   | The verbatim JSON envelope (event/context/device/action/payload) round-trips through actionReceived for every routed action                                              | VERIFIED | Dispatch passes the full `QJsonObject msg` unchanged to `actionReceived`; test #436 ("envelope round trips event context device action payload") asserts all 5 keys verbatim                                                                                        |
| 5   | The host can SEND a JSON-envelope event to a registered plugin's socket addressed by plugin uuid                                                                         | VERIFIED | `sendEvent(targetUuid, eventName, payload)` declared in hpp line 135, implemented in cpp lines 491-518; builds compact JSON envelope and writes via `sendTextMessage`                                                                                               |
| 6   | A host->plugin dialRotate event arrives at the loopback client carrying ticks/pressed/controller in payload                                                              | VERIFIED | Test #440 ("roundTrip dialRotate carries ticks pressed controller") passes; asserts `ticks==2`, `pressed==false`, `controller=="Encoder"`                                                                                                                           |
| 7   | sendEvent returns false (no crash) when the target uuid has no live socket                                                                                               | VERIFIED | `socketForUuid` returns nullptr for unknown uuid; `sendEvent` returns false; test #441 passes with `REQUIRE_FALSE(sent)` and `msgSpy.count()==0`                                                                                                                    |
| 8   | Every §4.4 host->plugin event name can be sent through the same sendEvent seam                                                                                           | VERIFIED | Test #442 ("host to plugin events arrive") iterates 10 representative event names (keyDown, keyUp, dialDown, dialUp, keyDownCord, touchTap, willAppear, deviceDidConnect, titleParametersDidChange, didReceiveSettings); all arrive at loopback client              |
| 9   | After registerPlugin the host sends a passHello frame carrying authentication:{challenge, salt} with a random per-connection salt                                        | VERIFIED | `makeRandomSaltHex()` at line 40 fills 16 bytes from `QRandomGenerator::system()`; `it->salt = makeRandomSaltHex()` at line 271; NESTED payload built at lines 281-289; emitted via `sendEvent(uuid, "passHello", helloPayload)`; test #443 passes                  |
| 10  | With no password configured, the connection is accepted after passHello — no auth round-trip required                                                                    | VERIFIED | `it->authenticated = m_password.isEmpty()` at line 273; no-password path sets `authenticated=true` immediately; `pluginRegistered` emitted at line 306 conditionally                                                                                                |
| 11  | A plugin reply with sha256(password+salt) matching the host computation is accepted; a wrong challenge increments the counter; after 5 bad attempts the socket is closed | VERIFIED | `challengeFor()` helper at lines 60-63; `kMaxAuthAttempts=5` at line 34; accept path at line 340-347; reject+increment at lines 349-363; `connIt->socket->close()` at line 362; tests #444 (accept) and #445 (reject-after-5) pass                                  |
| 12  | CR-01: routed-action dispatch is gated by authenticated check — unauthenticated socket cannot emit actionReceived                                                        | VERIFIED | Pre-auth gate at lines 429-438 checks `!connForAuth->authenticated` and returns with WARN log; test #446 ("unauthenticated socket cannot trigger actionReceived") asserts `actionSpy.count()==0` and passes                                                         |
| 13  | CR-02: pluginRegistered is deferred to authentication-success path when a password is configured                                                                         | VERIFIED | `if (m_password.isEmpty()) emit pluginRegistered(uuid)` at line 305-307; auth-success branch emits at line 347; test #444 asserts `registeredSpy.count()==0` pre-auth and `==1` post-auth                                                                           |
| 14  | CR-03: registerPlugin rejects a UUID already held by another live connection                                                                                             | VERIFIED | UUID collision guard at lines 241-252; `client->close()` on impostor; test #447 ("duplicate UUID registration rejected") asserts impostor disconnected, legitimate still live with `connectedPluginCount()==1`                                                      |

**Score:** 14/14 truths verified

### Deferred Items

None.

### Required Artifacts

| Artifact                               | Expected                                                                                                                                                                 | Status   | Details                                                                                                 |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------- | ------------------------------------------------------------------------------------------------------- |
| `src/app/src/sd_plugin_server.cpp`     | sizeless kRoutedActions (41 entries), authentication branch, passHello emission, challengeFor, makeRandomSaltHex, sendEvent, socketForUuid, CR-01/02/03 fixes            | VERIFIED | 520 lines; all elements present and wired                                                               |
| `src/app/src/sd_plugin_server.hpp`     | PluginConnection extended (salt/authAttempts/authenticated), sendEvent public declaration, setPasswordForTesting                                                         | VERIFIED | 208 lines; struct at lines 192-199; sendEvent at line 135; setPasswordForTesting at line 145            |
| `tests/unit/test_sd_plugin_server.cpp` | All-routed-actions case, envelope round-trip, loopback re-pin, dialRotate round-trip, unknown-uuid false, §4.4 event surface, auth test cases, security regression tests | VERIFIED | 785 lines; 25 test cases covering all required behaviors; 20 SdPlugin\* + 5 PluginAuth\* tests all pass |

### Key Link Verification

| From                        | To                                  | Via                                                                                       | Status | Details                                                                                                                  |
| --------------------------- | ----------------------------------- | ----------------------------------------------------------------------------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------ |
| `dispatchClientMessage`     | `actionReceived`                    | `kRoutedActions` membership check (line 441-446)                                          | WIRED  | Linear scan over 41-entry CTAD array; `isAction=true` path emits `actionReceived(senderUuid, msg)`                       |
| `dispatchClientMessage`     | `authentication` branch             | `eventName == "authentication"` before `kRoutedActions` (line 321)                        | WIRED  | Branch executes at line 321, before `kRoutedActions` scan at line 375; T-17-PREAUTH invariant confirmed by line ordering |
| `registerPlugin` handling   | `sendEvent(uuid, "passHello", ...)` | `makeRandomSaltHex()` + nested payload build (lines 271-290)                              | WIRED  | Salt generated, stored on `it->salt`, NESTED `authentication:{challenge,salt}` built, emitted via `sendEvent`            |
| `SdPluginServer::sendEvent` | `QWebSocket::sendTextMessage`       | `socketForUuid()` live-slot lookup + `QJsonDocument(env).toJson(Compact)` (lines 495-511) | WIRED  | Per-call re-resolution verified; no cached raw pointer                                                                   |
| `test_sd_plugin_server.cpp` | `SdPluginServer::actionReceived`    | loopback QWebSocket + QSignalSpy (tests #433-#436)                                        | WIRED  | All action tests use real loopback WebSocket client                                                                      |
| `test_sd_plugin_server.cpp` | `QWebSocket::textMessageReceived`   | loopback client spy (tests #440-#442, #443-#447)                                          | WIRED  | `QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived)` pattern throughout                                        |

### Data-Flow Trace (Level 4)

Phase 17 produces a protocol server — no dynamic data rendering (no JSX/Vue/QML component rendering state). Data-flow trace is not applicable. The sendEvent seam carries real payloads through loopback WebSocket frames verified by live ctest runs.

### Behavioral Spot-Checks

| Behavior                     | Command                                        | Result                   | Status |
| ---------------------------- | ---------------------------------------------- | ------------------------ | ------ |
| All 5 PluginAuth tests pass  | `ctest --preset linux-release -R "PluginAuth"` | 5/5 passed in 1.36s      | PASS   |
| All 20 SdPlugin\* tests pass | `ctest --preset linux-release -R "SdPlugin"`   | 20/20 passed in 1.02s    | PASS   |
| Full suite                   | `ctest --preset linux-release`                 | 456/456 passed in 13.27s | PASS   |

### Probe Execution

No phase-declared probes. Conventional probe discovery found no `scripts/*/tests/probe-*.sh` for this phase. Behavioral spot-checks above serve as the runnable verification layer.

### Requirements Coverage

| Requirement | Source Plan | Description                                                                                  | Status    | Evidence                                                                                                                                                                          |
| ----------- | ----------- | -------------------------------------------------------------------------------------------- | --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PLUGIN-01   | 17-01-PLAN  | LocalHost-only bind; loopback invariant test-pinned                                          | SATISFIED | `m_server->listen(QHostAddress::LocalHost, port)`; bindAddress() hardcoded LocalHost; tests #429 and #437 pin invariant; REQUIREMENTS.md row 276 marks Complete                   |
| PLUGIN-02   | 17-01-PLAN  | JSON envelope (event/context/device/action/payload) round-trips every supported message type | SATISFIED | Full QJsonObject passed verbatim to actionReceived; test #436 verifies all 5 keys; REQUIREMENTS.md row 277 marks Complete                                                         |
| PLUGIN-03   | 17-01-PLAN  | 26 AJAZZ-only actions implemented on top of standard set                                     | SATISFIED | kRoutedActions contains exactly 26 AJAZZ-only entries (setBG through sendUserInfo) and 15 standard = 41 total; test #435 exercises all 41; REQUIREMENTS.md row 278 marks Complete |
| PLUGIN-04   | 17-02-PLAN  | All host->plugin events wired incl. dialRotate/dialDown/dialUp                               | SATISFIED | sendEvent seam covers full §4.4 surface; test #440 verifies dialRotate ticks/pressed/controller; test #442 covers 10 representative names; REQUIREMENTS.md row 279 marks Complete |
| PLUGIN-05   | 17-03-PLAN  | passHello + salt/challenge auth (sha256(password+salt)); reject after N bad attempts; no TLS | SATISFIED | makeRandomSaltHex (CSPRNG), challengeFor (SHA-256), kMaxAuthAttempts=5, nested salt shape; 3 auth tests pass; REQUIREMENTS.md row 280 marks Complete                              |

**No orphaned requirements.** REQUIREMENTS.md tracking table maps PLUGIN-01..05 to Phase 17; PLUGIN-06..14 map to later phases.

### Anti-Patterns Found

| File                   | Line | Pattern                                   | Severity | Impact                                                                                                                                                                                                                   |
| ---------------------- | ---- | ----------------------------------------- | -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `sd_plugin_server.cpp` | 287  | `"deviceInfo": QJsonObject{}` placeholder | Info     | Intentional stub documented in SUMMARY: Phase 19 fills with real device descriptor. The passHello handshake functions correctly without device info; the placeholder is a wire-format correctness aid, not a logic stub. |

No TBD/FIXME/XXX markers found in any of the three modified files. No nlohmann references in sd_plugin_server.cpp or sd_plugin_server.hpp (COD-031 clean). No unresolved debt markers.

### Security Invariants Verified

| Invariant                                        | Check                                                                                   | Status   |
| ------------------------------------------------ | --------------------------------------------------------------------------------------- | -------- |
| LocalHost-only bind (never Any/AnyIPv4/AnyIPv6)  | `m_server->listen(QHostAddress::LocalHost, ...)` only; no bind setter                   | VERIFIED |
| Salt from CSPRNG (QRandomGenerator::system)      | `QRandomGenerator::system()->generate()` in makeRandomSaltHex                           | VERIFIED |
| SHA-256 challenge                                | `QCryptographicHash::hash(..., QCryptographicHash::Sha256)` in challengeFor             | VERIFIED |
| Brute-force lockout at kMaxAuthAttempts=5        | `if (connIt->authAttempts >= kMaxAuthAttempts) connIt->socket->close()` at line 355-362 | VERIFIED |
| No plaintext secret logging                      | Password never logged; only "password=none\|set" boolean logged                         | VERIFIED |
| CR-01: auth gate before routed-action dispatch   | `connForAuth->authenticated` check at line 432 BEFORE kRoutedActions loop               | VERIFIED |
| CR-02: pluginRegistered deferred to auth-success | Conditional emit at line 305-307; deferred emit at line 347                             | VERIFIED |
| CR-03: UUID collision guard                      | `existing != m_connections.end()` check closes impostor before uuid assignment          | VERIFIED |

### Human Verification Required

None. This phase is hardware-free and exercised entirely over loopback WebSocket with MockDevice. All behaviors are mechanically verifiable via automated tests. All 25 plugin-server + auth tests pass (456/456 full suite).

### Gaps Summary

No gaps. All 14 must-have truths are VERIFIED with direct code evidence and passing ctest confirmation.

______________________________________________________________________

_Verified: 2026-05-24T15:00:00Z_
_Verifier: Claude (gsd-verifier)_
