---
phase: 16-device-controls-binding-persistence-pages
plan: 02
subsystem: app-layer profile persistence / QML editor bridge
tags: [profile, persistence, round-trip, key-binding, tdd, qml-singleton, sanitization]

# Dependency graph
requires:
  - phase: 14-stream-dock-control-service/14-02
    provides: ProfileController::activeProfile() getter, profileChanged->repaintFromProfile wire
  - phase: 16-01
    provides: StreamDockControlService QML_SINGLETON + Q_INVOKABLE setBrightness/clearAll
provides:
  - ProfileController: defaultProfilePath(id) -> AppDataLocation/profiles/<safe-id>.json
  - ProfileController: Q_INVOKABLE commitKeyBinding (keys map mutation + profileChanged)
  - ProfileController: Q_INVOKABLE saveActiveProfile / loadActiveProfile / resetActiveProfile
  - KeyDesigner.qml: updateSelectedBinding() now calls ProfileController.commitKeyBinding()
  - Main.qml: Apply -> saveActiveProfile, Revert -> loadActiveProfile, RestoreDefaults -> resetActiveProfile
  - test_profile_persistence.cpp: 9 Catch2 TEST_CASEs covering path/traversal/round-trip/repaint
affects:
  - Phase 16-03 (pages navigation): reuses same ProfileController save/load surface
  - Phase 19 (plugin bridge): reuses commitKeyBinding for plugin-authored key rendering
  - Phase 25 (hardware smoke): restart-survives-edit witness on real AKP05E

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Profile id sanitization allowlist [A-Za-z0-9._-]: strips traversal chars inline (T-16b-01); no external helper needed
    - QDir::mkpath() before writeProfileToDisk: Pitfall 5 guard inside saveActiveProfile
    - commitKeyBinding seam: mutate m_profile.keys[uint16_t(index)] in-place; single onPress Action; emit profileChanged (repaint hook)
    - QML commit-per-edit: updateSelectedBinding reads full row via bindings.get() after setProperty, then calls ProfileController.commitKeyBinding()
    - TDD cycle: RED commit (cf675ef) -> GREEN commit (ed05f32)

key-files:
  created:
    - tests/unit/test_profile_persistence.cpp
  modified:
    - src/app/src/profile_controller.hpp
    - src/app/src/profile_controller.cpp
    - src/app/qml/KeyDesigner.qml
    - src/app/qml/Main.qml
    - tests/unit/CMakeLists.txt

key-decisions:
  - 'Default profile path: QStandardPaths::AppDataLocation/profiles/<sanitized-id>.json; sanitizeProfileId inline allowlist [A-Za-z0-9._-] + strip leading dots (T-16b-01 mitigation)'
  - 'Commit seam: per-edit (after every updateSelectedBinding) not Apply-only; Profile stays in sync with editor; disk write deferred to Apply button'
  - 'Active-profile-id convention: m_profile.id is used as the filename stem; if empty, falls back to "default"'
  - 'Restore defaults: resetActiveProfile() clears keys/encoders/mouseButtons + saves; honest behaviour, never silently no-ops (PROFILE-01 plan requirement)'
  - 'Encoder/touch AUTHORING UI deferred (LOCKED decision 3): encoder/touch bindings can be authored programmatically/by tests; full UI is a later phase'
  - 'repaintFromProfile on load: test proves BAT+ULEND per bound key via existing profileChanged->repaintFromProfile wire (already connected in Application by Phase 14); no new Application wiring needed'

requirements-completed: [PROFILE-01]

# Metrics
duration: ~15 min
completed: 2026-05-24
---

# Phase 16 Plan 02: Profile Persistence + KeyDesigner Bridge (PROFILE-01) Summary

**Key/encoder/touch bindings now survive restart: ProfileController gains defaultProfilePath, commitKeyBinding, and saveActiveProfile; KeyDesigner edits commit into the Profile on every field change; Main.qml Apply/Revert call real save/load; a fresh-controller round-trip test and BAT+ULEND repaint-on-load proof close PROFILE-01.**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-05-24T12:30Z
- **Completed:** 2026-05-24T12:45Z
- **Tasks:** 2/2 completed
- **Files modified:** 5 + 1 created

## Accomplishments

- PROFILE-01 delivered: key bindings authored in KeyDesigner now persist to disk and repaint the device on reload after restart
- `defaultProfilePath(id)`: resolves AppDataLocation/profiles/<sanitized-id>.json; inline allowlist sanitization strips path traversal chars (T-16b-01)
- `commitKeyBinding(index, iconPath, label, actionKind, settingsJson)`: mutates m_profile.keys[index], emits profileChanged (repaints immediately)
- `saveActiveProfile()`: creates profiles dir via QDir::mkpath (Pitfall 5 / T-16b-04), then atomic writeProfileToDisk
- `loadActiveProfile()`: reloads from default path; emits profileChanged -> repaint
- `resetActiveProfile()`: clears keys/encoders/mouseButtons, emits profileChanged, saves -- honest "Restore defaults" behaviour
- KeyDesigner.qml: updateSelectedBinding calls ProfileController.commitKeyBinding after each ListModel.setProperty
- Main.qml: Apply/Revert/RestoreDefaults wired to real ProfileController methods (no "not implemented" toasts)
- 9 new TEST_CASEs (test_profile_persistence.cpp): path resolution, traversal sanitization, commitKeyBinding mutation + signal, key/encoder/pages round-trip, saveActiveProfile parent-dir creation, repaint-on-load (BAT+ULEND per bound key)
- 437/437 ctest green; COD-031 intact (0 nlohmann in core/include); wire key "device" unchanged

## Task Commits

1. **Task 1 (RED): Failing tests for PROFILE-01 persistence** - `cf675ef` (test)
1. **Task 1 (GREEN): defaultProfilePath + commitKeyBinding + saveActiveProfile** - `ed05f32` (feat)
1. **Task 2: Bridge KeyDesigner + wire Main.qml Apply/Revert** - `be33d9d` (feat)

## Files Created/Modified

- `tests/unit/test_profile_persistence.cpp` - 9 Catch2 TEST_CASEs tagged [profile-persistence][PROFILE-01]; ASCII-only titles; covers path/sanitization/commit/round-trip/repaint-on-load
- `tests/unit/CMakeLists.txt` - Appended test_profile_persistence.cpp entry (no new source links; profile_controller.cpp already in binary from 14-02)
- `src/app/src/profile_controller.hpp` - Added defaultProfilePath/commitKeyBinding/saveActiveProfile/loadActiveProfile/resetActiveProfile declarations
- `src/app/src/profile_controller.cpp` - Added QDir+QStandardPaths includes; sanitizeProfileId inline helper; implementations of the 5 new methods
- `src/app/qml/KeyDesigner.qml` - updateSelectedBinding now calls ProfileController.commitKeyBinding(selectedIndex, row.iconSource, row.label, row.actionKind, row.actionParams) after ListModel.setProperty
- `src/app/qml/Main.qml` - onApplyRequested -> saveActiveProfile(); onRevertRequested -> loadActiveProfile(); onRestoreDefaultsRequested -> resetActiveProfile()

## Key Design Decisions

### Default Profile Path + ID Sanitization

`defaultProfilePath(id)` resolves `AppDataLocation/profiles/<sanitized>.json`. The sanitization is inlined as `sanitizeProfileId()`: keeps only `[A-Za-z0-9._-]`, strips leading dots (prevents `..` remnant), falls back to `"default"` for empty result. This is the T-16b-01 mitigation for path traversal via the profile id. No external sanitization helper was found in-tree (only the pi_bridge UUID validator uses a different allowlist); the inline approach is appropriate for this narrower use case.

### Commit Seam: Per-Edit, Not Apply-Only

The plan offered a choice. Per-edit was chosen because: (1) it keeps the in-memory Profile always consistent with the editor, so any future code reading `activeProfile()` (e.g. repaintFromProfile, control service, Phase 19 plugin bridge) always sees current data; (2) the disk write is still deferred to Apply, so the user controls persistence. The ListModel remains the editor's view model (live preview); Profile is the persistence source of truth.

### Active Profile ID Convention

`saveActiveProfile()` / `loadActiveProfile()` use `m_profile.id` as the filename stem. If the profile was never given an id (new session, no load), `id` is empty and the fallback `"default"` is used. This ensures first-run saves never fail with an empty path.

### Restore Defaults Behaviour

`resetActiveProfile()` clears `keys`, `encoders`, `mouseButtons` on the active profile, emits `profileChanged` (immediate repaint to blank), then calls `saveActiveProfile()` so the reset is persisted. This is the decision documented in the plan output section: "either reset the active profile to empty defaults + save, or keep an explicit toast if defaults are out of scope -- document the choice." The honest save-on-reset was chosen over a no-op toast.

### Encoder/Touch AUTHORING UI Deferred (LOCKED Decision 3)

Per the plan, full encoder/touch authoring UI is deferred. The round-trip tests prove the serializer supports them (encoder binding + KeyState round-trips field-by-field equal). The authoring bridge in KeyDesigner is for keys only. A future UI pass adds the encoder/touch editor.

### Repaint-on-Load Proof

The test `ProfileController: loaded profile with 2 bound keys repaints via StreamDockControlService` builds a MockTransport-backed StreamDockControlService, connects profileChanged -> repaintFromProfile (mirrors Application wiring from Phase 14), loads a saved profile, and asserts >= 2 BAT headers + >= 2 ULEND commits in the write stream. This proves the Phase 14 wire (already connected in Application) repaints on load without any new Application wiring.

## Deviations from Plan

None -- plan executed exactly as written. The `resetActiveProfile()` addition for `onRestoreDefaultsRequested` was explicitly required by the plan's "document the choice" instruction.

## TDD Gate Compliance

- RED gate: commit `cf675ef` (test(16-02)) -- 9 failing tests for PROFILE-01 persistence
- GREEN gate: commit `ed05f32` (feat(16-02)) -- implementation; all 9 tests pass

## Follow-up Items (Not Phase 16-02 Scope)

| Item                                                         | Phase         | Reason deferred                                                                    |
| ------------------------------------------------------------ | ------------- | ---------------------------------------------------------------------------------- |
| Encoder/touch AUTHORING UI (3-chain editor, touch config)    | later UI pass | Authoring UI deferred per LOCKED decision 3; round-trip is tested                  |
| Live hardware witness: restart-survives-edit on real AKP05E  | 25            | Phase 16 proof is MockTransport-only; Phase 25 does physical UAT                   |
| Profile library / id:path index for tray submenu (issue #24) | future        | loadProfileById stub kept; full id:path index is follow-up to profile library work |
| KeyDesigner populates bindings from loaded Profile on init   | 16-03/later   | On restart, ListModel is re-initialized from scratch; not seeded from m_profile    |

## Threat Mitigations Applied

| Threat                                               | Mitigation                                                                                               |
| ---------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| T-16b-01: profile id -> save path traversal          | sanitizeProfileId allowlist [A-Za-z0-9.\_-] + strip leading dots; tested with "../etc/passwd" input      |
| T-16b-03: second JSON serializer drifting            | No app-layer JSON writer; all paths through profileToJson/profileFromJson + writeProfileToDisk (COD-031) |
| T-16b-04: first-run save fails (profiles dir absent) | QDir::mkpath() inside saveActiveProfile before writeProfileToDisk call (Pitfall 5)                       |

## Known Stubs

None that prevent the plan's goal. The KeyDesigner ListModel still initializes with empty defaults on startup (not seeded from Profile), but this is an explicit follow-up item (see above), not a stub preventing persistence. Bindings committed via commitKeyBinding do persist.

## Self-Check: PASSED

Files confirmed present:

- `src/app/src/profile_controller.hpp` - FOUND (defaultProfilePath + commitKeyBinding + saveActiveProfile + loadActiveProfile + resetActiveProfile)
- `src/app/src/profile_controller.cpp` - FOUND (sanitizeProfileId + all 5 method bodies)
- `src/app/qml/KeyDesigner.qml` - FOUND (ProfileController.commitKeyBinding call in updateSelectedBinding)
- `src/app/qml/Main.qml` - FOUND (saveActiveProfile / loadActiveProfile / resetActiveProfile)
- `tests/unit/test_profile_persistence.cpp` - FOUND (9 TEST_CASEs)

Commits confirmed:

- `cf675ef` (RED) - FOUND
- `ed05f32` (GREEN) - FOUND
- `be33d9d` (QML/Main) - FOUND

Invariants confirmed:

- `grep -c nlohmann src/core/include/` == 0 (COD-031 intact)
- `grep -n '"device"' src/core/src/profile.cpp` -- wire key unchanged at line 792
- `static_assert(!std::is_default_constructible_v<ProfileController>)` -- still present in profile_controller.hpp
- `git diff --stat` excludes akp05.cpp / akp05_protocol.hpp

ctest suite: 437/437 green.
