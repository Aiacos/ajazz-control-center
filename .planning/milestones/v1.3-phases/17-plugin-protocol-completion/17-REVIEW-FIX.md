---
phase: 17-plugin-protocol-completion
fixed_at: 2026-05-24T14:00:00Z
review_path: .planning/phases/17-plugin-protocol-completion/17-REVIEW.md
iteration: 1
findings_in_scope: 7
fixed: 7
skipped: 0
status: all_fixed
---

# Phase 17: Code Review Fix Report

**Fixed at:** 2026-05-24T14:00:00Z
**Source review:** `.planning/phases/17-plugin-protocol-completion/17-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 7 (3 Critical + 3 Warning + 1 Info)
- Fixed: 7
- Skipped: 0

## Fixed Issues

### CR-01: Routed Actions Dispatched Without Checking `authenticated`

**Files modified:** `src/app/src/sd_plugin_server.cpp`
**Commit:** 26a689a
**Applied fix:** Added a pre-auth gate immediately before the `kRoutedActions` dispatch loop. The gate performs a `std::find_if` on `m_connections` by socket pointer and checks `connForAuth->authenticated`. If false, the action is silently ignored with a `AJAZZ_LOG_WARN`. In the no-password path, `authenticated` is set to `true` at `registerPlugin` time so the gate is a no-op — open-mode behaviour is fully preserved.

______________________________________________________________________

### CR-02: `pluginRegistered` Emitted Before Authentication Completes

**Files modified:** `src/app/src/sd_plugin_server.cpp`, `tests/unit/test_sd_plugin_server.cpp`
**Commit:** 26a689a (impl), 128ea61 (test adaptation)
**Applied fix:** In the `registerPlugin` branch, `emit pluginRegistered(uuid)` is now conditional on `m_password.isEmpty()`. When a password is configured, the emit is deferred. A new `emit pluginRegistered(connIt->uuid)` was added in the `authentication` success branch (after `connIt->authenticated = true`). The two existing auth tests (`acceptsCorrectChallenge`, `rejectsAfter5BadAttempts`) were updated to use `findFrameByEvent(msgSpy, "passHello")` as the registration-ready signal instead of `waitForSpy(registeredSpy)`, since `pluginRegistered` no longer fires at `registerPlugin` time when a password is set. `acceptsCorrectChallenge` also gained a positive assertion that `registeredSpy.count() == 1` after auth succeeds.

______________________________________________________________________

### CR-03: No UUID Collision Guard

**Files modified:** `src/app/src/sd_plugin_server.cpp`
**Commit:** 26a689a
**Applied fix:** Before the `it->uuid = uuid` assignment in the `registerPlugin` branch, a `std::find_if` scan checks whether any other live slot (`c.socket != nullptr && c.socket != client`) already holds the requested UUID. If found, the impostor socket is closed and the function returns immediately. The legitimate connection's slot is undisturbed.

______________________________________________________________________

### WR-01: `pluginRegistered` Emitted Outside the Guard

**Files modified:** `src/app/src/sd_plugin_server.cpp`
**Commit:** 26a689a
**Applied fix:** The `AJAZZ_LOG_INFO` and `emit pluginRegistered(uuid)` calls were moved inside the `if (it != m_connections.end())` block. An `else` branch was added that logs a warning when the socket is not found in the connection table. This also removes the separate `return` that was after the closing brace.

______________________________________________________________________

### WR-02: Re-Registration Silently Orphans Old UUID

**Files modified:** `src/app/src/sd_plugin_server.cpp`
**Commit:** 26a689a
**Applied fix:** Inside the `if (it != m_connections.end())` block, before assigning `it->uuid = uuid`, a check was added: if `!it->uuid.isEmpty() && it->uuid != uuid`, emit `pluginDisconnected(it->uuid)` with a warning log before overwriting. This ensures the app layer receives a disconnect signal for the stale UUID so it can clean up any backend bindings.

______________________________________________________________________

### WR-03: Stale Test Comment — "13 standard Elgato actions"

**Files modified:** `tests/unit/test_sd_plugin_server.cpp`
**Commit:** 128ea61
**Applied fix:** Changed the comment at line 162 from `// setTitle is one of the 13 standard Elgato actions.` to `// setTitle is one of the 15 standard Elgato actions.` — matching the actual count in `kRoutedActions` (15 standard + 26 AJAZZ-only = 41 total).

______________________________________________________________________

### IN-01: No Test Verifying Routed Actions Blocked When `authenticated=false`

**Files modified:** `tests/unit/test_sd_plugin_server.cpp`
**Commit:** 128ea61
**Applied fix:** Added two new security regression test cases:

1. `"PluginAuthTest unauthenticated socket cannot trigger actionReceived"` — configures a password, sends `registerPlugin` without replying to the `passHello` challenge, then sends three routed actions (`setTitle`, `sendToDevice`, `getSystemAudioVolume`). Asserts `actionSpy.count() == 0` and `registeredSpy.count() == 0`. This is the mechanical regression guard for CR-01 and CR-02.

1. `"PluginAuthTest duplicate UUID registration rejected"` — connects a legitimate socket, registers it with a UUID (no password, so `pluginRegistered` fires immediately), then connects an impostor socket that claims the same UUID. Asserts the impostor is disconnected and the legitimate socket remains live with `connectedPluginCount() == 1`. This is the regression guard for CR-03.

**Test count:** 456 (up from 408 pre-Phase-17 base; +2 new security tests = total 456, 100% passing).

______________________________________________________________________

## Skipped Issues

None — all 7 in-scope findings were fixed.

______________________________________________________________________

_Fixed: 2026-05-24T14:00:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
