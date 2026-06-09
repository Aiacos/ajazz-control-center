---
phase: 34-per-app-profiles-event-parity-audit
plan: 04
subsystem: per-app-profiles-event-parity
tags: [aprof, event-parity, auto-switch, applicationHints, switchToProfile, systemDidWakeUp, applicationDidLaunch, fan-out, idempotent, security, catch2]

# Dependency graph
requires:
  - phase: 34-per-app-profiles-event-parity-audit
    plan: 01
    provides: IActiveWindowWatcher interface + StubActiveWindowWatcher injectForeground seam + RED app_profile_switch/app_lifecycle_events scaffolds
  - phase: 34-per-app-profiles-event-parity-audit
    plan: 03
    provides: per-OS watcher backends + ActiveWindowDebouncer + app::makeActiveWindowWatcher() wired into Application
provides:
  - ProfileController::resolveProfileForApp (case-insensitive applicationHints match + default fallback) + appIdMatchesHints pure helper
  - ProfileController::applicationHints()/setApplicationHints() reader+writer (backs the Plan-05 assign-profile UI)
  - Application foreground-watcher onChange wire — APROF-02 auto-switch (idempotent guard) + APROF-04 lifecycle fan-out, reusing the profileChanged -> populateContextsForActivePage reconcile
  - switchToProfile inbound host-command handler + resolveSwitchToProfileToken (id/name/device-scoped, bad-token reject)
  - app_event_dispatch.{hpp,cpp} — registered-only outbound fan-out helpers (systemDidWakeUp, applicationDidLaunch/Terminate) + boundApplicationToken (V5 cap) + Application dispatch wrappers
  - PluginDeviceBridge::registeredPlugins() accessor (registered-only fan-out authority)
affects: [34-05, app-profile-auto-switch, event-parity]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Security-relevant outbound fan-out factored as free functions over SdPluginServer* + a registered-uuid set so the registered-only (V4) + length-bound (V5) delivery contract is unit-testable against a live loopback server without constructing the whole Application
    - Untrusted plugin/host tokens (switchToProfile profile, foreground appId) treated purely as lookup keys — V5 length-bound + never eval/shell (T-34-04-03)
    - Auto-switch reuses the existing profileChanged -> populateContextsForActivePage reconcile (no parallel willAppear/willDisappear path) behind an idempotent no-op-on-identical guard (CR WR-01 / T-34-04-01 DoS)
    - Watcher onChange tracks m_lastForegroundApp so a focus change fans out terminate(outgoing)+launch(incoming); no-op when the id is unchanged

key-files:
  created:
    - src/app/src/app_event_dispatch.hpp
    - src/app/src/app_event_dispatch.cpp
    - tests/unit/test_switch_to_profile.cpp
    - tests/unit/test_system_did_wake.cpp
  modified:
    - src/app/src/application.cpp
    - src/app/src/application.hpp
    - src/app/src/profile_controller.hpp
    - src/app/src/profile_controller.cpp
    - src/app/src/plugin_device_bridge.hpp
    - src/app/CMakeLists.txt
    - tests/qml/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_app_profile_switch.cpp
    - tests/unit/test_app_lifecycle_events.cpp

key-decisions:
  - The host-event fan-out (applicationDid*, systemDidWakeUp) is factored into app_event_dispatch free functions (over SdPluginServer* + a QSet<QString> registered set) rather than living only inside Application methods. RATIONALE - the security-relevant delivery contract (registered-only V4, length-bound V5, never-cache-socket Pitfall 4) must be unit-tested, but application.cpp is not compiled into the unit target; the free functions are testable against a live loopback SdPluginServer with a registered plugin. Application's dispatch* methods are thin wrappers (server + bridge->registeredPlugins()).
  - resolveProfileForApp reads candidate profiles off disk per call (the {id,name,device,path} library index does NOT carry applicationHints). This is acceptable because onChange is debounced (~180ms, Plan 03) — the reads gate an actual profile switch, not a per-frame path.
  - The default-profile fallback mirrors activateDeviceProfile - first profile for the device sorted by name. Cross-device isolation - resolveProfileForApp + resolveSwitchToProfileToken both scope to the device codename so an app/token never selects another device's profile.
  - applicationDidTerminate is best-effort - the watcher reports FOCUS, not process exit, so terminate = "the previously-foreground app lost focus". A true process-exit source is out of scope (would need OS process tracking); documented.
  - systemDidWakeUp DISPATCH is implemented + unit-tested via the injectable Application::dispatchSystemWake seam (synthetic wake). The real Linux logind PrepareForSleep D-Bus source is NOT wired in this plan (deferred — see Deferred below); the dispatch path is proven, the OS source is the remaining wiring.

requirements-completed: [APROF-02, APROF-04, EVENT-03]

# Metrics
duration: ~85min
completed: 2026-06-08
---

# Phase 34 Plan 04: Auto-switch wire + EVENT-03 dispatch Summary

**Wired the foreground-window watcher into the profile system (APROF-02 per-app auto-switch via case-insensitive `applicationHints` match + default fallback, behind an idempotent guard, reusing the existing `profileChanged -> populateContextsForActivePage` reconcile) and completed the EVENT-03 host dispatch: the `switchToProfile` inbound handler (token validated/bounded, name-or-id resolved, bad-token rejected), `systemDidWakeUp` dispatch, and the APROF-04 `applicationDidLaunch/Terminate` fan-out — all delivered to REGISTERED plugins only (V4) with length-bounded payloads (V5). Turned the two Plan-01 RED scaffolds GREEN and added the two EVENT-03 tests.**

## Performance

- **Duration:** ~85 min
- **Completed:** 2026-06-08
- **Tasks:** 3 (all `auto`, all committed atomically)
- **Files modified/created:** 14 (4 created, 10 modified)

## Accomplishments

- **APROF-02 auto-switch** — `ProfileController::resolveProfileForApp(appId, device)` matches the foreground app id case-insensitively against each candidate profile's `Profile::applicationHints` (reading profiles off disk, scoped to the active device), falling back to the device default. The `Application` watcher `onChange` resolves + applies an **idempotent guard** (skip when `resolved == activeProfileId` — CR WR-01 / T-34-04-01 DoS mitigation) + `loadProfileById`, which drives the **existing** `profileChanged -> populateContextsForActivePage` reconcile. No new `willAppear/willDisappear` path (grep-verified).
- **EVENT-03 switchToProfile** — new branch in the `actionReceived` lambda (captures `[this]`): V5-bounds the `profile`/`device` tokens (256 chars), rejects empty/unresolvable tokens without activation or crash, and resolves via `resolveSwitchToProfileToken` (exact id, then name-or-id scoped to device; cross-device isolation). Appears in `plugin.protocolLog` via the existing inbound-action tap.
- **EVENT-03 systemDidWakeUp** — `Application::dispatchSystemWake()` (injectable synthetic-wake seam) fans out `systemDidWakeUp` to registered plugins via `sendEvent` (no cached socket). Dispatch unit-tested; the real logind source is deferred (see below).
- **APROF-04 applicationDidLaunch/Terminate** — the watcher `onChange` fans out `applicationDidTerminate` (outgoing app) + `applicationDidLaunch` (incoming app) on a foreground change, to **registered plugins only** (V4 / T-34-04-02 — never broadcast) with a **length-bounded** `{application}` payload (V5 / T-34-04-05).
- **Tests** — `ctest -R 'app_profile_switch|app_lifecycle_events|switch_to_profile|system_did_wake'` = **16/16 green** (the two former Plan-01 RED `[!shouldfail]` scaffolds are now real-assertion GREEN). Full suite **779/779 green** (was 768 at phase start).

## Task Commits

1. **Task 1: APROF-02 auto-switch matcher + idempotent guard + hint-resolve helper** — `16d112c` (feat)
1. **Task 2: switchToProfile handler + systemDidWakeUp dispatch (EVENT-03)** — `bf5e417` (feat)
1. **Task 3: applicationDidLaunch/Terminate fan-out to registered plugins (APROF-04)** — `e5a81ef` (feat)

**Plan metadata:** (final commit) `docs(34-04): complete auto-switch + EVENT-03 dispatch plan`

## Files Created/Modified

- `src/app/src/profile_controller.{hpp,cpp}` — `resolveProfileForApp` + `appIdMatchesHints` + `resolveSwitchToProfileToken` + `applicationHints()`/`setApplicationHints()`.
- `src/app/src/application.{hpp,cpp}` — watcher `onChange` wire (auto-switch + lifecycle fan-out), `switchToProfile` branch, `dispatchSystemWake`/`dispatchApplicationLaunch`/`Terminate` wrappers, `m_lastForegroundApp`.
- `src/app/src/app_event_dispatch.{hpp,cpp}` — registered-only fan-out helpers + `boundApplicationToken`.
- `src/app/src/plugin_device_bridge.hpp` — `registeredPlugins()` accessor.
- `src/app/CMakeLists.txt` / `tests/qml/CMakeLists.txt` — compile `app_event_dispatch.cpp` (app + QML smoke targets).
- `tests/unit/CMakeLists.txt` — register `test_switch_to_profile` (main list) + `test_system_did_wake` / `test_app_lifecycle_events` + `app_event_dispatch.cpp` (WS-gated block).
- `tests/unit/test_app_profile_switch.cpp` / `test_app_lifecycle_events.cpp` — RED -> GREEN.
- `tests/unit/test_switch_to_profile.cpp` / `test_system_did_wake.cpp` — new EVENT-03 tests.

## Decisions Made

See `key-decisions` frontmatter. Most load-bearing: the host-event fan-out is factored into `app_event_dispatch` free functions so the registered-only (V4) + length-bound (V5) + never-cache-socket delivery contract is unit-testable against a live loopback server (application.cpp is not in the unit target); `Application::dispatch*` are thin wrappers.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Host-event fan-out factored into a testable free-function TU**

- **Found during:** Task 2 (writing the systemDidWakeUp dispatch test).
- **Issue:** The plan put the fan-out inside `Application::dispatchSystemWake`/`dispatchApplication*`, but `application.cpp` is **not** compiled into `ajazz_unit_tests` (it pulls in the whole app), so the security-relevant delivery contract could not be unit-tested there. Constructing a full `Application` in a unit test is infeasible.
- **Fix:** Added `src/app/src/app_event_dispatch.{hpp,cpp}` (free functions `dispatchSystemWakeTo` / `dispatchApplicationLaunchTo` / `dispatchApplicationTerminateTo` + `boundApplicationToken`) over a `SdPluginServer*` + a `QSet<QString>` registered set. `Application`'s methods delegate to them; the tests drive them against a live loopback `SdPluginServer` with a registered plugin (the same e2e seam `test_plugin_device_bridge.cpp` uses). Compiled into the app, QML-smoke, and unit targets (all WS-gated).
- **Files modified:** src/app/src/app_event_dispatch.{hpp,cpp}, src/app/src/application.{hpp,cpp}, src/app/CMakeLists.txt, tests/qml/CMakeLists.txt, tests/unit/CMakeLists.txt
- **Committed in:** `bf5e417`

**2. [Rule 2 - Missing critical] resolveSwitchToProfileToken cross-device isolation**

- **Found during:** Task 2.
- **Issue:** A `switchToProfile` token resolved by name could match a profile on a *different* device if the name fallback weren't device-scoped — letting a plugin switch another device's profile (EoP, T-34-04-04 spirit).
- **Fix:** The name fallback uses `profilesForDevice(deviceToken)` so a name only resolves within the addressed device; an exact id match is allowed globally (ids are unique). Locked by `switch_to_profile does not match a name from a different device`.
- **Committed in:** `bf5e417`

______________________________________________________________________

**Total deviations:** 2 (1 Rule 3 blocking/testability, 1 Rule 2 security). No scope creep.

## Issues Encountered

- The pre-existing `profile_controller.cpp` GCC `-Wnull-dereference` false-positive on a Qt `QHash` inline (documented in MEMORY/CLAUDE.md) surfaced as a warning during the build; not promoted to an error on that path, out of scope.
- clang-format reformatted the new TUs on first commit (the documented re-add dance); re-staged and re-committed — never `--no-verify`.

## Deferred / Pending

- **systemDidWakeUp real OS source (logind `PrepareForSleep` D-Bus) — DEFERRED.** Per RESEARCH A4 + the plan ("wire the Linux logind source only if cheap"), this plan implements + unit-tests the **dispatch** path via the injectable synthetic-wake seam (`Application::dispatchSystemWake`). The real Linux logind D-Bus listener (and Win/macOS sources behind their guards) is NOT wired here — the dispatch is proven, the OS source is the remaining wiring (a small QDBus connection on Linux; candidate for 34-05 or a follow-up).
- **applicationDidTerminate is best-effort** (focus-loss, not process exit) — documented in code; a true process-exit source is out of scope.
- **Live interactive walk** (focus real app A then B on the niri desktop, confirm auto-switch + lifecycle events via `log.tail`) is part of the 34-05 consolidated live gate (the headless watcher binding was already proven in Plan 03).

## Known Stubs

None introduced. The deferred logind source is a documented wiring gap (dispatch proven), not a stub that blocks the plan goal.

## Threat Flags

None — no new trust-boundary surface beyond the plan's `<threat_model>` (all three boundaries: switchToProfile inbound, watcher->applicationDid\* outbound, OS wake->systemDidWakeUp were anticipated and mitigated: V4 registered-only, V5 length-bound, T-34-04-01 idempotent guard, T-34-04-03 token-as-key, T-34-04-04 device-scoped resolution).

## Self-Check: PASSED

All 4 created files exist on disk (app_event_dispatch.hpp/cpp, test_switch_to_profile.cpp, test_system_did_wake.cpp); all 3 task commits (16d112c, bf5e417, e5a81ef) are present in git history. `ctest -R 'app_profile_switch|app_lifecycle_events|switch_to_profile|system_did_wake'` = 16/16; full suite = 779/779.

______________________________________________________________________

*Phase: 34-per-app-profiles-event-parity-audit*
*Completed: 2026-06-08*
