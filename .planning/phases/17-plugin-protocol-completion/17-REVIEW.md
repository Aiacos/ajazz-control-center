---
phase: 17-plugin-protocol-completion
reviewed: 2026-05-24T12:00:00Z
depth: deep
files_reviewed: 3
files_reviewed_list:
  - src/app/src/sd_plugin_server.hpp
  - src/app/src/sd_plugin_server.cpp
  - tests/unit/test_sd_plugin_server.cpp
findings:
  critical: 3
  warning: 3
  info: 2
  total: 8
status: issues_found
---

# Phase 17: Code Review Report

**Reviewed:** 2026-05-24T12:00:00Z
**Depth:** deep
**Files Reviewed:** 3
**Status:** issues_found

## Summary

Phase 17 implemented a WebSocket plugin protocol server with a passHello/SHA-256 auth handshake. The loopback-only binding invariant is correctly implemented and the brute-force lockout (kMaxAuthAttempts=5) fires correctly. COD-031 is clean (no nlohmann in core headers). No secrets are written to logs.

Three critical security defects survive into the shipped code: the routed-action dispatch path contains no gate on `authenticated`, so any client that has completed `registerPlugin` but has not yet authenticated can inject `actionReceived` signals into the app layer; `pluginRegistered` is emitted unconditionally before authentication completes, potentially wiring device backends to an unauthenticated connection; and there is no guard against a second client registering an already-held UUID, enabling silent impersonation on the loopback interface.

In current production defaults (empty password) the first two defects are dormant because `authenticated` is forced true at `registerPlugin` time. However the code is explicitly designed to support password-protected connections via `setPasswordForTesting`, and the security review mandate treats the password path as in-scope.

______________________________________________________________________

## Critical Issues

### CR-01: Routed Actions Dispatched Without Checking `authenticated`

**File:** `src/app/src/sd_plugin_server.cpp:375-384`

**Issue:** The kRoutedActions dispatch block emits `actionReceived(senderUuid, msg)` with no check on the connection's `authenticated` field. When a password is configured, a client that has completed `registerPlugin` (which sets `authenticated = false`) can immediately send any of the 41 routed actions — `setTitle`, `setImage`, `sendToDevice`, `getSystemAudioVolume`, etc. — and each one reaches the app layer as if fully authenticated. The comment block that introduced T-17-PREAUTH only protects the `authentication` event name from falling through to `unhandledEventReceived`; it does not gate the routed-action path against unauthenticated sockets.

Concrete attack path (loopback only, requires local code execution):

1. Connect to the server.
1. Send `{"event":"registerPlugin","uuid":"com.attacker"}`.
1. Immediately send `{"event":"setTitle","context":"...","payload":{...}}` — no `authentication` reply sent.
1. `actionReceived("com.attacker", msg)` fires on the app layer.

**Fix:**

```cpp
// At the top of the routed-action dispatch section, before the kRoutedActions loop:
// --- Pre-auth gate (T-17-PREAUTH extension) ---
// An authenticated=false slot must not reach the routed-action set.
auto connForAuth = std::find_if(m_connections.begin(), m_connections.end(),
    [client](auto const& c) { return c.socket == client; });
if (connForAuth != m_connections.end() && !connForAuth->authenticated) {
    AJAZZ_LOG_WARN("plugin-server",
                   "unauthenticated client uuid={} sent action '{}'; ignoring",
                   connForAuth->uuid.toStdString(),
                   eventName.toStdString());
    return;
}
// ... existing kRoutedActions loop ...
```

______________________________________________________________________

### CR-02: `pluginRegistered` Emitted Before Authentication Completes

**File:** `src/app/src/sd_plugin_server.cpp:266-270`

**Issue:** `emit pluginRegistered(uuid)` fires at the end of the `registerPlugin` branch, which runs before the plugin has replied to the passHello challenge. When a password is configured, `authenticated` is `false` at this point. The app layer (Phase 19 and beyond) is documented to wire the UUID to device backends on receipt of `pluginRegistered`. This creates a window — between `registerPlugin` and the correct `authentication` reply — during which the app layer may begin forwarding device events to a socket that has not yet proven it knows the password. If the socket then fails auth and is closed, the app layer holds a stale UUID-to-backend binding.

**Fix:** Delay `pluginRegistered` emission until authentication is confirmed. In the `authentication` handler, after setting `connIt->authenticated = true`, emit the signal:

```cpp
// In the registerPlugin branch: do NOT emit pluginRegistered yet when password is set.
if (m_password.isEmpty()) {
    // No-password path: authenticated immediately — emit now.
    emit pluginRegistered(uuid);
}
// (When password is set, pluginRegistered will fire from the authentication handler below.)

// In the authentication success branch:
if (expected == received) {
    connIt->authenticated = true;
    AJAZZ_LOG_INFO("plugin-server", "authentication accepted for uuid={}",
                   connIt->uuid.toStdString());
    emit pluginRegistered(connIt->uuid);  // deferred emit
}
```

Note: this also requires removing the early `emit pluginRegistered` from the registerPlugin branch when `!m_password.isEmpty()`.

______________________________________________________________________

### CR-03: No UUID Collision Guard — Second Client Can Claim an Active Plugin's UUID

**File:** `src/app/src/sd_plugin_server.cpp:223-271`

**Issue:** The `registerPlugin` branch locates the connection slot by **socket pointer** and assigns `it->uuid = uuid` without checking whether that UUID is already held by another live slot. A second client can connect and send `{"event":"registerPlugin","uuid":"com.legitimate.plugin"}` while the legitimate plugin is still connected with that UUID. The result:

1. Two entries in `m_connections` share the same `uuid`.
1. `socketForUuid` returns the first match (the legitimate socket), so `passHello` for the impostor goes to the wrong socket.
1. The impostor's slot has `uuid = "com.legitimate.plugin"` and, absent CR-01 being fixed, can emit `actionReceived("com.legitimate.plugin", ...)` as if it were the real plugin.
1. `pluginRegistered("com.legitimate.plugin")` is emitted a second time, potentially confusing the app layer's UUID-to-backend map.

**Fix:** Reject or close the existing slot when a duplicate UUID registration arrives:

```cpp
// Before it->uuid = uuid (line 236), check for UUID collision:
auto existing = std::find_if(m_connections.begin(), m_connections.end(),
    [&uuid, client](auto const& c) {
        return c.uuid == uuid && c.socket != nullptr && c.socket != client;
    });
if (existing != m_connections.end()) {
    AJAZZ_LOG_WARN("plugin-server",
                   "registerPlugin for uuid={} already held by another socket; "
                   "closing impostor",
                   uuid.toStdString());
    client->close();
    return;  // do not assign UUID, do not emit pluginRegistered
}
```

______________________________________________________________________

## Warnings

### WR-01: `pluginRegistered` Emitted Outside the `if (it != m_connections.end())` Guard

**File:** `src/app/src/sd_plugin_server.cpp:265-270`

**Issue:** The UUID binding, salt generation, and passHello dispatch are all inside the `if (it != m_connections.end()) { ... }` block that closes at line 265. The `AJAZZ_LOG_INFO` and `emit pluginRegistered(uuid)` calls at lines 266-270 are outside this guard and run unconditionally — including in the (currently unreachable in single-threaded Qt) case where the socket is not found in `m_connections`. If that path were ever reached, `pluginRegistered` would fire for a UUID that has no associated socket, no salt, and `authenticated = false` in no slot at all, corrupting the app layer's plugin registry.

**Fix:** Move both the log and the signal emit inside the `if (it != m_connections.end())` block:

```cpp
if (it != m_connections.end()) {
    it->uuid = uuid;
    it->salt = makeRandomSaltHex();
    it->authenticated = m_password.isEmpty();
    // ... passHello build and sendEvent ...
    AJAZZ_LOG_INFO("plugin-server", "plugin registered: uuid={} event={}",
                   uuid.toStdString(), eventName.toStdString());
    emit pluginRegistered(uuid);
} else {
    AJAZZ_LOG_WARN("plugin-server",
                   "registerPlugin for uuid={} but socket not in connection table; ignoring",
                   uuid.toStdString());
}
return;
```

______________________________________________________________________

### WR-02: Re-Registration on an Existing Socket Silently Orphans the Old UUID

**File:** `src/app/src/sd_plugin_server.cpp:236-270`

**Issue:** If a connected, authenticated socket sends `registerPlugin` a second time with a different UUID, `it->uuid` is overwritten and `pluginRegistered(new_uuid)` fires. The old UUID is silently abandoned: no `pluginDisconnected(old_uuid)` signal fires, and `socketForUuid(old_uuid)` now returns `nullptr`. The app layer retains a stale binding for the old UUID with no way to detect it has been abandoned. Any future `sendEvent(old_uuid, ...)` silently drops frames.

**Fix:** Emit `pluginDisconnected` for the old UUID before overwriting it:

```cpp
if (it != m_connections.end()) {
    // Clean up old UUID if this socket is re-registering.
    if (!it->uuid.isEmpty() && it->uuid != uuid) {
        AJAZZ_LOG_WARN("plugin-server",
                       "socket re-registering: old uuid={} replaced by uuid={}",
                       it->uuid.toStdString(), uuid.toStdString());
        emit pluginDisconnected(it->uuid);
    }
    it->uuid = uuid;
    // ... rest of registration ...
}
```

______________________________________________________________________

### WR-03: Stale Test Comment — "13 standard Elgato actions" Should Be 15

**File:** `tests/unit/test_sd_plugin_server.cpp:162`

**Issue:** The comment `// setTitle is one of the 13 standard Elgato actions.` is stale. The implementation's `kRoutedActions` list contains 15 standard Elgato entries (matching the spec §4.3 table: `setTitle`, `setImage`, `setState`, `showAlert`, `showOk`, `getSettings`, `setSettings`, `getGlobalSettings`, `setGlobalSettings`, `switchToProfile`, `sendToPropertyInspector`, `sendToPlugin`, `openUrl`, `logMessage`, `setFeedback`). The comment was apparently written before `setFeedback` and one other were added and was never updated.

**Fix:**

```cpp
// setTitle is one of the 15 standard Elgato actions.
```

______________________________________________________________________

## Info

### IN-01: No Test Verifying That Routed Actions Are Blocked When `authenticated=false`

**File:** `tests/unit/test_sd_plugin_server.cpp`

**Issue:** The test suite has no `TEST_CASE` that calls `setPasswordForTesting`, sends `registerPlugin` (leaving `authenticated=false`), then sends a routed action and asserts that `actionReceived` is NOT emitted. This gap allowed CR-01 to ship undetected through the full ctest run. Adding such a test is the mechanical fix that would catch a regression of CR-01.

**Fix:** Add a test case:

```cpp
TEST_CASE("PluginAuthTest unauthenticated socket cannot trigger actionReceived",
          "[plugin-server][auth][security]") {
    ensureQCoreApp();
    SdPluginServer server;
    server.setPasswordForTesting(QStringLiteral("pw"));
    QSignalSpy actionSpy(&server, &SdPluginServer::actionReceived);
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));
    QWebSocket client;
    QSignalSpy connectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(connectedSpy));
    // Register but do NOT send authentication reply.
    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.unauth"})"));
    // Wait for passHello to arrive (ensures server is ready).
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    pump(200);
    // Attempt a routed action without authenticating first.
    client.sendTextMessage(
        QStringLiteral(R"({"event":"setTitle","context":"c","payload":{"title":"x"}})"));
    pump(300);
    // actionReceived must NOT fire — the socket is not yet authenticated.
    REQUIRE(actionSpy.count() == 0);
}
```

______________________________________________________________________

### IN-02: Brute-Force Counter Resets on Reconnect — No Global Rate Limiting

**File:** `src/app/src/sd_plugin_server.cpp:197-198`

**Issue:** `onClientDisconnected` erases the `PluginConnection` entry entirely (by design, to reclaim the slot). A fresh `PluginConnection` with `authAttempts = 0` is created on the next `onNewConnection`. An adversary on the loopback can therefore cycle: connect → send 4 wrong challenges → disconnect → reconnect → repeat, cycling through unlimited guesses at the rate of 4 attempts per TCP handshake. The per-connection lockout at `kMaxAuthAttempts=5` does not bound the total attempt count.

This is a known accepted risk for a loopback-only server (any local process already has full OS privilege), and the planning documents acknowledge it. Flagged here as Info rather than Critical given the threat model, but it warrants a comment in code if not already present, and a global attempt-count per UUID or a connection-rate limit would close it.

**Fix (minimal):** Add a note in the code near `kMaxAuthAttempts` acknowledging the reconnect-reset behavior and why it is acceptable for the loopback trust model:

```cpp
/// NOTE (T-17-BRUTE-RECONNECT): this counter is per-connection. A determined
/// local-process attacker can reconnect and get 5 fresh attempts each time.
/// Acceptable for loopback-only: any local process already has OS-level privilege.
/// A global UUID-scoped counter would close this; defer to future hardening.
constexpr int kMaxAuthAttempts = 5;
```

______________________________________________________________________

_Reviewed: 2026-05-24T12:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: deep_
