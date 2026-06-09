---
phase: 34-per-app-profiles-event-parity-audit
plan: 05
subsystem: per-app-profiles-ui
tags: [aprof, aprof-03, ui, qml, settings, applicationHints, capability-chip, wayland, verif-01, debug-channel, graceful-degradation]

# Dependency graph
requires:
  - phase: 34-per-app-profiles-event-parity-audit
    plan: 03
    provides: IActiveWindowWatcher backends + capabilityAvailable() (drives the chip) + factory
  - phase: 34-per-app-profiles-event-parity-audit
    plan: 04
    provides: ProfileController applicationHints reader/writer + resolveProfileForApp + auto-switch wire
provides:
  - ProfileController per-app mapping writer/remover (addAppProfileMapping/removeAppProfileMapping) targeting a SPECIFIC profile id, V5 length-bound app-name token + case-insensitive dup guard
  - ProfileController appProfileMappings() -> {profileId,profileName,deviceCodename,appName} rows for the assign list
  - ProfileController foregroundCapabilityAvailable Q_PROPERTY (NOTIFY) + setter (Q_INVOKABLE seam) + foregroundCapabilityWarning CONSTANT copy for the APROF-03 chip
  - Application injection of the watcher capabilityAvailable() into ProfileController after start()
  - SettingsPage Per-app profiles section (assign surface) + waylandCapabilityWarningChip (amber, visible-on-absent, addressable warningText)
  - window.setCapability debug RPC for live/headless verification of the chip's capability-absent path
affects: [phase-34-close, phase-35]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - QML singleton capability property (NOTIFY) injected by Application from a Qt-free core watcher seam, so the chip binding re-evaluates without a raw watcher pointer in QML
    - Per-profile (by-id) applicationHints writer: active profile mutated in place + saveActiveProfile; non-active profile read/mutate/write-back via the library index path
    - Amber capability-warning chip mirrors the LoadedPluginsPage trust-chip (visible-on-absent inversion: no chip = positive signal; warning family, never error red, for graceful degradation)
    - Debug RPC reaches a QML singleton (not findByName-addressable) via the app.profileController() seam (mirrors profile.*) so VERIF-01 chip verification is headless-drivable on a wlr host

key-files:
  created: []
  modified:
    - src/app/src/profile_controller.hpp
    - src/app/src/profile_controller.cpp
    - src/app/src/application.cpp
    - src/app/qml/SettingsPage.qml
    - src/app/src/debug_control_facade.cpp

key-decisions:
  - The assign-profile UI writes through new by-id Q_INVOKABLEs (addAppProfileMapping/removeAppProfileMapping) rather than the active-only setApplicationHints, so a mapping can target any of the device's profiles without first activating it; active profile is mutated in place, non-active is read/mutate/write-back via the m_library {id,...,path} index
  - Capability is surfaced to QML as a ProfileController Q_PROPERTY injected by Application from the watcher (not a watcher pointer in QML); the chip is visible-on-ABSENT (inverted trusted-chip convention) with an addressable warningText CONSTANT so a headless qml.get asserts non-empty detail without hover
  - Added a window.setCapability debug RPC (Rule 3 — enables the mandated VERIF-01 chip verification) because ProfileController is a QML singleton unreachable by findByName; on a wlr Wayland host the watcher reports capability=true so the absent path is otherwise unreachable headlessly
  - No profile schema change/migration — applicationHints already persists (profile.hpp:192); the wire key applicationHints is schema-correct (PROFILE_SCHEMA.md:52,173)

requirements-completed: [APROF-03]

# Metrics
duration: ~32min
completed: 2026-06-08
---

# Phase 34 Plan 05: APROF-03 Assign-Profile UI + Capability-Warning Chip Summary

**The user-facing half of per-app profiles: a "Per-app profiles" assign surface in `SettingsPage.qml` that maps an application name to any device profile (writing `Profile::applicationHints` via new by-id `ProfileController` Q_INVOKABLEs), plus the amber Wayland/GNOME capability-warning chip driven by a NOTIFY capability property Application injects from `IActiveWindowWatcher::capabilityAvailable()`. Every control is `objectName`-addressable (VERIF-01) and the surface was live-verified headlessly through the debug channel — chip both ways, assign add/remove writing+persisting `applicationHints`, and a rendered screenshot.**

## Performance

- **Duration:** ~32 min
- **Completed:** 2026-06-08
- **Tasks:** 2 code/UI tasks committed atomically + 1 consolidated live checkpoint (HEADLESS portion done here; interactive walk deferred to the orchestrator/HUMAN-UAT)
- **Files modified:** 5 (0 created, 5 modified)

## Accomplishments

- **ProfileController writer/remover + capability surface (Task 1)** — `addAppProfileMapping(profileId, appName)` / `removeAppProfileMapping(profileId, appName)` target a SPECIFIC profile by id (active: in-memory + `saveActiveProfile`; non-active: read/mutate/write-back via the library path), with a V5 length bound (`kMaxAppNameLength=256`, T-34-05-01) and a case-insensitive duplicate guard. `appProfileMappings()` returns one `{profileId,profileName,deviceCodename,appName}` row per (profile, hint) pair for the list. `foregroundCapabilityAvailable` is a NOTIFY `Q_PROPERTY` (default true = fail-safe) with a Q_INVOKABLE setter; `foregroundCapabilityWarning` is the CONSTANT UI-SPEC detail copy. Application injects `m_activeWindowWatcher->capabilityAvailable()` into the controller after `start()`.
- **SettingsPage section + chip (Task 2)** — a "Per-app profiles" section (section `Label` + `Frame` card, mirroring the existing SettingsPage pattern): helper copy, error-state when zero profiles, `EmptyState` when zero mappings, a `Repeater` of mapping rows with a red destructive `Remove` `SecondaryButton`, and an add-mapping row (`appProfileAppNameField` `TextField` `maximumLength:256` + `appProfileProfileSelector` `ComboBox` + `addAppProfileMappingButton` `PrimaryButton`). The `waylandCapabilityWarningChip` is an amber pill (`Theme.chip*Warning`, radius 12) labelled "Limited on this desktop", visible only when capability is absent, exposing an addressable `warningText` + tooltip. All strings `qsTr()` + ASCII; all tokens `Theme.*`; `Button`/`SecondaryButton` only, no `Switch`.
- **window.setCapability debug RPC** — forces the capability state on the singleton through `app.profileController()` so the chip's absent path is live-verifiable on a wlr host (VERIF-01).
- **Tests** — full `ctest --preset linux-release` = **779/779 green** (incl. 17 qml; the QML smoke gate load-covers the new SettingsPage section). No new ctest cases added (the surface is verified live; the matcher/auto-switch logic it drives is already unit-proven 16/16 in Plan 04).

## Task Commits

1. **Task 1: ProfileController applicationHints writer/remover + foreground-capability property** — `7e2158b` (feat)
1. **Task 2: Per-app profiles section + capability-warning chip in SettingsPage** — `52abf5c` (feat)
1. **window.setCapability debug RPC (live-verification seam, deviation)** — `6e01abd` (feat)

**Plan metadata:** (final commit) `docs(34-05): complete APROF-03 assign-profile UI + chip plan`

## Files Modified

- `src/app/src/profile_controller.{hpp,cpp}` — `addAppProfileMapping`/`removeAppProfileMapping`/`appProfileMappings` + `foregroundCapabilityAvailable` Q_PROPERTY/setter + `foregroundCapabilityWarning` + `foregroundCapabilityChanged` signal + `m_foregroundCapabilityAvailable`.
- `src/app/src/application.cpp` — inject `m_activeWindowWatcher->capabilityAvailable()` into ProfileController after `start()`.
- `src/app/qml/SettingsPage.qml` — Per-app profiles section + `waylandCapabilityWarningChip`.
- `src/app/src/debug_control_facade.cpp` — `window.setCapability` RPC.

## Decisions Made

See `key-decisions` frontmatter. Most load-bearing: the assign UI writes through by-id Q_INVOKABLEs (any profile, not just active); capability is a NOTIFY property injected by Application (no watcher pointer in QML); the chip is amber visible-on-absent with addressable warningText; a debug RPC reaches the singleton for VERIF-01.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking/Verification] window.setCapability debug RPC added**

- **Found during:** Task 3 (headless live gate).
- **Issue:** The plan's success criterion 2 + VERIF-01 require proving the chip's capability-ABSENT path live (chip visible + non-empty warningText). But this niri host's watcher reports `capabilityAvailable()==true` (chip hidden by default), and `ProfileController` is a QML singleton not reachable by the debug channel's `findByName`, so the absent path was otherwise unreachable headlessly.
- **Fix:** Added a `window.setCapability` RPC (mirrors the existing `window.setForeground`) that forces `ProfileController::setForegroundCapabilityAvailable` through the same `app.profileController()` seam `profile.*` uses. This is a verification seam, not new product behavior; the setter it drives is the same one Application calls from the watcher.
- **Files modified:** src/app/src/debug_control_facade.cpp
- **Committed in:** `6e01abd`

______________________________________________________________________

**Total deviations:** 1 (Rule 3 — enabled the mandated VERIF-01 live verification). No scope creep in the product surface.

## Live verification (headless)

Run on this niri host on an ISOLATED `XDG_RUNTIME_DIR` + `QT_QPA_PLATFORM=offscreen` (so it never collided with the user's running GUI instance; the user's instance was never touched; the headless instance + its runtime dir were torn down after). Drove `scripts/ajazz-debug` against the headless app's debug socket:

- **Chip addressable + non-empty warningText (success criterion 2):** `qml.get waylandCapabilityWarningChip warningText` returned the full UI-SPEC detail copy non-empty WITHOUT hover. Default `visible:false` (capability present on niri).
- **Chip both paths:** `window.setCapability {available:false}` -> with the settings drawer open, `qml.get visible == true`, rendered pill `width 161.6 x height 24`; `window.setCapability {available:true}` -> `visible == false`. The setter logged `foreground capability ABSENT (APROF-03 chip VISIBLE)` / `available (chip hidden)` on each real transition.
- **Assign controls addressable + write `applicationHints`:** `qml.set appProfileAppNameField text="firefox"` (read back OK); `appProfileProfileSelector currentText/currentValue` reflected the newly created "AutoSwitch Test" profile; `qml.click addAppProfileMappingButton` wrote `"applicationHints":["firefox"]` to the profile JSON on disk (schema-correct wire key, verified against `PROFILE_SCHEMA.md`). `qml.click removeAppProfileMappingButton` reduced it to `"applicationHints":[]` (persisted). Test profile cleaned up afterwards.
- **Screenshot read:** captured `/tmp/acc34-settings.png` and confirmed the rendered surface matches the contract — "Per-app profiles" heading with the amber "Limited on this desktop" chip, the "firefox -> AutoSwitch Test" mapping row + red "Remove", and the add-mapping row (Application-name field, AutoSwitch Test selector, accent "Add mapping").

## Pending live verification (HUMAN-UAT)

The consolidated 5-check live gate (plan Task 3) — the interactive parts needing a human at the niri/X11 compositor + a retail device, deferred to the orchestrator's consolidated pass / HUMAN-UAT:

- **APROF-02 synthetic latency + auto-switch end-to-end (checks 1, 4-followup, 5):** `window.setForeground` only injects through the recording STUB's `injectForeground` seam; this full Linux build runs the real per-OS watcher backend (no `injectForeground`), so the synthetic foreground -> auto-switch -> `willDisappear`+`willAppear` in `log.tail` path is NOT reachable headlessly here (the RPC reports "watcher does not support synthetic injection" — same documented limitation as Plans 03/04). The auto-switch matcher + idempotent guard + lifecycle fan-out logic is already unit-proven (16/16, Plan 04). Needs a REAL focus change on niri (focus a mapped app -> device repaints < 500 ms) for the live end-to-end.
- **Wayland live switch on niri (check 2):** focus app A then app B on the real desktop and confirm the watcher emits the `app_id` change + device repaint.
- **EVENT dispatch (check 5):** synthetic `switchToProfile` plugin action -> profile switch; `systemDidWakeUp` reaches a subscribed plugin; `applicationDidLaunch` to a registered plugin — drive via `plugin.simulateAction` / a registered plugin in the orchestrator's pass.
- **Retail-device repaint:** the demo unit (`0x0300:0x3004`) delivers zero input; a retail AKP05E is needed for the physical repaint-on-switch confirmation (HARDWARE-GATED, does not block APROF-03).

## Known Stubs

None introduced. `window.setCapability` is a verification RPC (debug-only), not a product stub. The synthetic-injection gap for the full-build watcher is a pre-existing Plan 03/04 limitation (real backend has no `injectForeground`), not a new stub.

## Threat Flags

None — no new trust-boundary surface beyond the plan's `<threat_model>`. The two anticipated boundaries are mitigated: UI app-name -> `applicationHints` is V5 length-bounded + treated purely as a match token (T-34-05-01); the degraded-capability path drives the amber chip (visible-on-absent, non-empty warning) and manual switching still works (T-34-05-02/03, fail safe).

## Self-Check: PASSED

All 5 modified files exist on disk; all 3 task commits (7e2158b, 52abf5c, 6e01abd) are present in git history. Full `ctest --preset linux-release` = 779/779 (incl. 17 qml). The new SettingsPage section + chip + assign controls were live-verified through the debug channel (chip both ways with non-empty warningText; add/remove wrote+persisted `applicationHints`; screenshot read).

______________________________________________________________________

*Phase: 34-per-app-profiles-event-parity-audit*
*Completed: 2026-06-08*
