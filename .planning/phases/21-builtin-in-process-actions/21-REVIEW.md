---
phase: 21-builtin-in-process-actions
reviewed: 2026-05-24T00:00:00Z
depth: standard
files_reviewed: 6
files_reviewed_list:
  - src/core/src/input_synthesizer_stub.cpp
  - src/core/src/input_synthesizer_linux.cpp
  - src/app/src/obs_client.cpp
  - src/core/src/builtin_action_registry.cpp
  - src/app/src/builtin_actions_service.cpp
  - src/app/src/application.cpp
findings:
  critical: 1
  warning: 2
  info: 2
  total: 5
status: issues_found
---

# Phase 21: Code Review Report

**Reviewed:** 2026-05-24
**Depth:** standard
**Files Reviewed:** 6
**Status:** issues_found

## Summary

Reviewed six files implementing Phase 21 built-in in-process actions: the core registry
dispatch table, the Linux uinput synthesizer and stub, the OBS WebSocket client, the
builtin-actions service, and the application wiring.

COD-031 boundary is clean: both core public headers (`builtin_action_registry.hpp`,
`input_synthesizer.hpp`) contain only standard C++20 headers — no Qt, no nlohmann::json.
The prefix+map double-gate in the registry is correct and the anti-feature opt-in
capture gate (captureHotkeys OFF by default, `m_captureEnabled{false}`) holds.
The OBS password is not logged anywhere in the reviewed files.

One critical finding: the OBS Hello handler has an auth-bypass gap when the server
sends a non-object `d.authentication` value, violating the T-21-obsauth contract.
Two warnings: the OBS auth-failure path silently retries on every subsequent button
press (resource-wasting log spam loop), and `resetLunBoCursors()` is wired to nothing —
LunBo per-key cursors never reset across profile changes despite the documented contract.

______________________________________________________________________

## Critical Issues

### CR-01: OBS auth bypass via non-object `d.authentication` value

**File:** `src/app/src/obs_client.cpp:183`

**Issue:** The Hello handler checks `!authVal.isUndefined() && authVal.isObject()` to
decide whether auth is required. If the server sends `d.authentication` as any non-object
type — `null`, `true`, `"string"` — the condition `authVal.isObject()` is `false`, the
code falls to the `else` branch, logs "auth-disabled server", and sends `Identify` without
an `authentication` field. A server that requires auth but sends a malformed Hello would
be connected to without authentication. This breaks the T-21-obsauth anti-feature
contract: "the client REFUSES to send Identify and emits authFailed when none is set
when OBS demands authentication." The correct discriminant per obs-websocket v5
`protocol.md` is `absent` vs `present` — any defined value for `authentication` must be
treated as auth-required, not only defined+object.

**Fix:**

```cpp
// Before (line 183):
if (!authVal.isUndefined() && authVal.isObject()) {

// After — treat ANY defined authentication value as auth-required:
if (!authVal.isUndefined()) {
    if (!authVal.isObject()) {
        // Malformed Hello: authentication key present but not an object.
        // Treat as auth-required and refuse if no password (T-21-obsauth).
        AJAZZ_LOG_WARN("obs-client",
                       "OBS Hello has d.authentication but it is not an object — "
                       "treating as auth-required (T-21-obsauth)");
        m_state = State::Disconnected;
        m_socket->close();
        emit authFailed(
            QStringLiteral("OBS sent malformed authentication field; treating as auth-required"));
        return;
    }
    // Password check and normal Identify path below...
```

______________________________________________________________________

## Warnings

### WR-01: OBS auth-failure causes unbounded per-button-press retry loop

**File:** `src/app/src/builtin_actions_service.cpp:481-507`

**Issue:** After `ObsClient::authFailed` fires:

1. The `authFailed` handler in `BuiltinActionsService` only logs — it does NOT set any
   "auth failed, stop retrying" flag.
1. `m_obsConnected` was never set to `true` (no `Identified` event arrived), so it
   remains `false`.
1. `ObsClient::m_state` was set to `Disconnected` (line 191 of `obs_client.cpp`) when
   the client refused Identify, so `connectToObs`'s state guard passes.
1. Every subsequent press of an `obsstudio` button re-enters the lazy-connect block
   (`if (!m_obsConnected)`), calls `connectToObs` again, OBS responds with the same
   Hello, auth fails again, and the cycle repeats indefinitely.

Each cycle logs a WARN from the OBS server and another WARN from the auth-failed handler.
On a device where the user repeatedly presses an OBS button (or has a macro loop), this
can spam logs and generate unnecessary network connections.

**Fix:** Track auth-failure state separately so the service stops retrying:

```cpp
// In builtin_actions_service.hpp, add a member:
bool m_obsAuthFailed{false};   // set on authFailed; cleared on successful connection

// In the authFailed connect (builtin_actions_service.cpp ~line 481):
QObject::connect(m_obs.get(), &ObsClient::authFailed, this, [this](QString const& reason) {
    m_obsAuthFailed = true;   // stop retrying until settings change
    AJAZZ_LOG_WARN("builtin", "obsstudio: auth failed -- {}; "
                   "retries suppressed until settings update", reason.toStdString());
});

// Clear authFailed on successful connect:
QObject::connect(m_obs.get(), &ObsClient::connected, this, [this]() {
    m_obsConnected = true;
    m_obsAuthFailed = false;
    AJAZZ_LOG_INFO("builtin", "obsstudio: connected to OBS");
});

// In the lazy-connect block (~line 496):
if (!m_obsConnected && !m_obsAuthFailed) {
    // ... connectToObs(host, port, password);
} else if (m_obsAuthFailed) {
    AJAZZ_LOG_WARN("builtin", "obsstudio: auth previously failed; "
                   "update obs/password in settings to retry");
}
```

______________________________________________________________________

### WR-02: `resetLunBoCursors()` is never called — LunBo cursors persist across profile changes

**File:** `src/app/src/builtin_actions_service.cpp:228-230`, `src/app/src/application.cpp`

**Issue:** `BuiltinActionsService::resetLunBoCursors()` is declared and implemented but
has **zero callers** in the entire codebase. The header explicitly documents: *"Reset
LunBo per-key cursors (call on profile change). Each profile change invalidates the
round-robin position for every LunBo key."* The method is a dead function.

In `application.cpp` the `profileChanged` signal (from `ProfileController`) is wired
only to `StreamDockControlService::repaintFromProfile` (line 437). The signal is never
connected to `m_builtinActions->resetLunBoCursors()`.

Practical effect: if a user loads a different profile (especially with a shorter `actions`
array on a LunBo key), the stale cursor from the previous profile is carried over. The
modulo guard (`cursor % chainSize`) prevents an out-of-bounds access, but the first
action executed after a profile change is determined by the leftover cursor position
rather than starting at index 0 — violating the documented design invariant T-21-lunbo.

**Fix:** In `application.cpp` after line 437 (inside the Application constructor, after
the `profileChanged -> repaintFromProfile` connection):

```cpp
// Wire resetLunBoCursors to profileChanged so per-key LunBo carousel positions
// start fresh when a new profile loads (T-21-lunbo).
QObject::connect(m_profileController.get(),
                 &ProfileController::profileChanged,
                 m_builtinActions.get(),
                 &BuiltinActionsService::resetLunBoCursors);
```

Note: `m_builtinActions` is constructed after `m_profileController` in the initializer
list, so this connection must be placed in the constructor body after both are initialized.
`m_builtinActions` is a member and is fully constructed by the time the constructor body
runs.

______________________________________________________________________

## Info

### IN-01: `kOpRequestResponse` handler emits `requestSucceeded` without checking `d.requestStatus.result`

**File:** `src/app/src/obs_client.cpp:221-226`

**Issue:** The obs-websocket v5 `RequestResponse` (op 7) frame carries
`d.requestStatus.result` (bool) indicating success/failure and `d.requestStatus.code`
(int) with an error code on failure. The current handler:

```cpp
case kOpRequestResponse: {
    QString const reqId = d.value(QStringLiteral("requestId")).toString();
    if (!reqId.isEmpty()) {
        emit requestSucceeded(reqId);   // always emitted, even on failure
    }
    break;
}
```

`requestSucceeded` is emitted unconditionally for any non-empty `requestId`, even when
`requestStatus.result` is `false` (e.g. scene not found, OBS in a state that rejects the
request). In the current codebase no caller reacts to `requestSucceeded` in a
correctness-affecting way, so this does not cause incorrect behavior now. However the
signal name is misleading and would produce incorrect feedback in a future UI that
displays request outcome.

**Fix:**

```cpp
case kOpRequestResponse: {
    QString const reqId = d.value(QStringLiteral("requestId")).toString();
    QJsonObject const status = d.value(QStringLiteral("requestStatus")).toObject();
    bool const ok = status.value(QStringLiteral("result")).toBool(true); // true = permissive default
    if (!reqId.isEmpty()) {
        if (ok) {
            emit requestSucceeded(reqId);
        } else {
            int const code = status.value(QStringLiteral("code")).toInt(-1);
            AJAZZ_LOG_WARN("obs-client",
                           "OBS request {} failed: status code {}", reqId.toStdString(), code);
        }
    }
    break;
}
```

______________________________________________________________________

### IN-02: `page.change` silently defaults to CW (+1) on any non-"CCW" direction string

**File:** `src/app/src/builtin_actions_service.cpp:294-295`

**Issue:**

```cpp
auto const dir = obj.value(QStringLiteral("direction")).toString();
int const d = (dir == QStringLiteral("CCW")) ? -1 : +1;
```

If `direction` is absent, empty, or any value other than `"CCW"` (including a typo like
`"cw"` or `"CW"`), the action silently navigates forward. No warning is logged. The
schema specifies `"CW"` and `"CCW"` as the only valid values; an unknown value indicates
a misconfigured profile binding that should be surfaced.

**Fix:** Add an explicit check for the known values:

```cpp
auto const dir = obj.value(QStringLiteral("direction")).toString();
int navDir = +1; // default CW
if (dir == QStringLiteral("CCW")) {
    navDir = -1;
} else if (dir != QStringLiteral("CW") && !dir.isEmpty()) {
    AJAZZ_LOG_WARN("builtin",
                   "page.change: unknown direction '{}', defaulting to CW",
                   dir.toStdString());
}
```

______________________________________________________________________

_Reviewed: 2026-05-24_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
