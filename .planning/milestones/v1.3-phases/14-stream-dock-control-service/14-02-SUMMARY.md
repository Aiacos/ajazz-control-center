---
phase: 14-stream-dock-control-service
plan: 02
subsystem: app-layer device control
tags: [stream-dock, display, profile, qt-timer, hid, dynamic-cast, device-lookup, tdd]

# Dependency graph
requires:
  - phase: 14-stream-dock-control-service/14-01
    provides: akp05e descriptor honesty fix (hasClock==false) + regression test
  - phase: earlier phases
    provides: |
      Akp05Device backend (IDisplayCapable, BAT/LIG/ULEND wire layer),
      ProfileController (profileChanged signal),
      DeviceRegistry (flyweight open()),
      MockTransport + makeAkp05WithTransport DI seam
provides:
  - StreamDockControlService: app-layer device paint path (single held-open handle per session)
  - LIG brightness-ON at setActiveDevice (DISPLAY-06 gap closed)
  - Coalesced QTimer drain: assign -> BAT -> chunks -> ULEND without manual flush (DISPLAY-07, DOCK-02)
  - repaintFromProfile: profileChanged -> iterate active profile keys -> enqueue per-key image (DISPLAY-08)
  - firmwareVersionFor: cached VER string surface (DOCK-01)
  - ProfileController::activeProfile() getter (DISPLAY-08 enabler)
  - Application wired: DeviceLookup lambda + profileChanged connect + hot-plug setActiveDevice
affects:
  - Phase 15 (input/key-press): reuses setActiveDevice held handle and DeviceLookup pattern
  - Phase 16 (controls/persistence): reuses assignKeyImage, adds brightness slider, active-device UI
  - Phase 19 (plugin bridge): reuses repaintFromProfile/assignKeyImage for plugin-side key rendering
  - Phase 25 (hardware smoke): verifies DISPLAY-06/07/08 end-to-end on real AKP05E hardware

# Tech tracking
tech-stack:
  added: []
  patterns:
    - DeviceLookup lambda injection (std::function<shared_ptr<IDevice>(QString)>): mirrors TimeSyncService/LightingService
    - ProfileAccessor lambda seam (std::function<Profile const&()>): injected by Application, testable with lambda fake
    - Single-shot QTimer drain (0ms, Qt::CoarseTimer): coalesces last-write-wins per key without an I/O thread
    - Held shared_ptr<IDevice> session handle (ARCH-03 flyweight): opened once, re-resolved on hot-plug arrival
    - dynamic_cast<IDisplayCapable*> + null-check within 3 lines: Pitfall 1 guard applied consistently

key-files:
  created:
    - src/app/src/stream_dock_control_service.hpp
    - src/app/src/stream_dock_control_service.cpp
    - tests/unit/test_stream_dock_control_service.cpp
  modified:
    - src/app/src/profile_controller.hpp
    - src/app/src/profile_controller.cpp
    - src/app/src/application.hpp
    - src/app/src/application.cpp
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - 'ProfileAccessor seam: std::function<Profile const&()> lambda (not ProfileController*) — keeps service unit-testable with a fake lambda; Application wires m_profileController->activeProfile() as the lambda body'
  - 'Key-index mapping: Profile::keys are 0-based uint16_t; AKP05E backend is 1-based (1..10); service adds +1 in repaintFromProfile()'
  - 'QML exposure deferred: StreamDockControlService is a plain QObject for Phase 14; QML_SINGLETON/UI wiring is Phase 16 scope'
  - 'Default brightness constant: kDefaultBrightnessPercent = 80 (uint8_t); Phase 16 owns the brightness slider (Assumption A4)'
  - 'Active-device selection: first connected Stream Dock wins (Phase 14 simplification); full active-device selection UI is Phase 16'
  - 'Threading: all device writes on GUI thread via QTimer drain (Pitfall 3 / A2); no dedicated I/O thread added'

patterns-established:
  - 'DeviceLookup pattern (5th service to use it): enumerate registry, match codename, return flyweight open()'
  - 'ProfileAccessor lambda: enables sibling service to iterate profile without ProfileController dependency'
  - 'QTimer drain: 0ms single-shot arms on assignKeyImage, fires on next event-loop iteration, clears pending map after draining'

requirements-completed: [DISPLAY-06, DISPLAY-07, DISPLAY-08, DOCK-01, DOCK-02]

# Metrics
duration: ~90min
completed: 2026-05-24
---

# Phase 14 Plan 02: StreamDockControlService Summary

**App-layer device paint service with held AKP05E handle, LIG brightness-ON at open, coalesced BAT/ULEND write queue, profile-repaint on profileChanged, and MockTransport unit tests — closes the Phase-10 UAT gap where the wire layer existed but no app code ever called it.**

## Performance

- **Duration:** ~90 min
- **Started:** 2026-05-24T08:00Z (approx)
- **Completed:** 2026-05-24T09:05Z
- **Tasks:** 3/3 completed
- **Files modified:** 8

## Accomplishments

- Closed the Phase-10 UAT gap: `grep setKeyImage src/app/` now returns a call site (was 0 hits before this plan)
- Built StreamDockControlService as the single device paint path for the AKP05E — holds one `shared_ptr<IDevice>` per session (ARCH-03 flyweight), issues LIG brightness-ON at open (DISPLAY-06), coalesces key image assigns through a single-shot QTimer drain without manual flush (DISPLAY-07 + DOCK-02), repaints all bound keys on profileChanged (DISPLAY-08), and surfaces the cached firmware VER string (DOCK-01)
- 5 MockTransport wire assertions cover all five requirements with exact byte checks (byte[5]=='L' for LIG, bytes[5..7]=='B','A','T' for BAT, bytes[5..9]=='U','L','E','N','D' for ULEND); suite 414/414 green

## Task Commits

1. **Task 1: Add ProfileController::activeProfile() getter** - `705af98` (feat)
1. **Task 2: Implement StreamDockControlService + MockTransport tests** - `0be17b7` (feat)
1. **Task 3: Wire StreamDockControlService into Application** - `6383792` (feat)

## Files Created/Modified

- `src/app/src/stream_dock_control_service.hpp` - QObject with DeviceLookup/ProfileAccessor ctors, setActiveDevice/assignKeyImage/repaintFromProfile/firmwareVersionFor public API, kDefaultBrightnessPercent=80 constant
- `src/app/src/stream_dock_control_service.cpp` - Full implementation: held shared_ptr session handle, Pitfall-1-compliant dynamic_casts, QTimer drain, 0-to-1-based key-index translation in repaintFromProfile
- `tests/unit/test_stream_dock_control_service.cpp` - 5 Catch2 TEST_CASEs tagged [stream-dock-control], ASCII-only titles, MockTransport wire assertions
- `src/app/src/profile_controller.hpp` - Added `activeProfile() const noexcept` const-ref getter
- `src/app/src/profile_controller.cpp` - Added implementation returning m_profile
- `src/app/src/application.hpp` - Added m_streamDockControl member (after m_firmwareUpdate, -Wreorder safe)
- `src/app/src/application.cpp` - Constructed with DeviceLookup + ProfileAccessor lambdas; connected profileChanged->repaintFromProfile; wired hot-plug arrival->setActiveDevice for StreamDeck family
- `src/app/CMakeLists.txt` - Added service sources to qt_add_executable + ACC_QML_MODULE_SOURCES
- `tests/unit/CMakeLists.txt` - Appended test + service + profile_controller sources (did not clobber 14-01's entry)

## Key Design Decisions

### ProfileAccessor Seam (Open Question 4 resolved)

Chose `std::function<core::Profile const&()>` over passing a `ProfileController*` pointer. The lambda seam keeps StreamDockControlService testable without a real ProfileController: the unit test passes a lambda returning a stack-allocated fake Profile. Application wires it as `[this]() -> core::Profile const& { return m_profileController->activeProfile(); }`.

### Key-Index Mapping

Profile::keys uses 0-based `std::uint16_t` indices (confirmed from `profile.hpp`). The AKP05E backend's `keyIndexInRange` checks `1..KeyCount=10` — 0 is rejected. `repaintFromProfile()` therefore adds +1: `deviceKeyIndex = static_cast<uint8_t>(profileKeyIndex + 1)`.

### QML Exposure Deferred

StreamDockControlService is a plain `QObject` for Phase 14; no `QML_SINGLETON` / `qmlRegisterSingletonInstance`. The brightness slider, active-device selector, and key-image assignment UI are Phase 16 scope.

### Active-Device Selection (Phase 14 Simplification)

On hot-plug arrival for `DeviceFamily::StreamDeck`, the service calls `setActiveDevice(codename)` immediately (after the existing 300ms debounce). If multiple Stream Decks are plugged in, the last one to arrive wins. Full active-device selection UI is Phase 16.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed sign-conversion warning in drainPendingWrites()**

- **Found during:** Task 2 build
- **Issue:** `rgba.height()` returns `int`; multiplying with `static_cast<std::size_t>(rgba.width())` triggered `-Werror=sign-conversion` (Apple Clang / MSVC strict path)
- **Fix:** Cast both width and height explicitly: `static_cast<std::size_t>(rgba.width()) * static_cast<std::size_t>(rgba.height()) * 4u`
- **Files modified:** `src/app/src/stream_dock_control_service.cpp`
- **Commit:** included in `0be17b7`

**2. [Rule 1 - Bug] Removed unused `obs` variable in held-handle test**

- **Found during:** Task 2 build
- **Issue:** `auto* obs = fx.transport` declared but never referenced in the test body, triggering `-Werror=unused-variable`
- **Fix:** Removed the variable; test logic uses only `firmwareVersionFor()` return value
- **Files modified:** `tests/unit/test_stream_dock_control_service.cpp`
- **Commit:** included in `0be17b7`

None — plan executed with only compiler-warning auto-fixes; no architectural deviations.

## Follow-up Items (Not Phase 14 Scope)

| Item                                | Phase | Reason deferred                                                                   |
| ----------------------------------- | ----- | --------------------------------------------------------------------------------- |
| Brightness slider UI                | 16    | Phase 14 hardcodes kDefaultBrightnessPercent=80; Phase 16 owns device settings UI |
| Active-device selector UI           | 16    | Phase 14 first-arrival wins; Phase 16 adds device picker                          |
| clearAll on device close            | 16    | No explicit clear at session end; Phase 16 adds panel-off on disconnect           |
| Hardware smoke test (real AKP05E)   | 25    | Phase 14 proof is MockTransport-only; Phase 25 does live power-cycle verification |
| Untrusted imagePath deep validation | 16/19 | Phase 14 uses Qt safe image decoders; deep path/sandboxing is Phase 16/19         |

## Threat Mitigations Applied

| Threat                                               | Mitigation                                                                            |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------- |
| T-14b-01: held shared_ptr device yank                | Held in m_activeDevice member; zombie-contract no-op; re-resolved on hot-plug arrival |
| T-14b-02: rapid setKeyImage burst                    | QTimer drain (0ms single-shot, last-write-wins per key); ULEND per burst              |
| T-14b-03: untrusted imagePath                        | Qt safe image decoders only (accepted for Phase 14)                                   |
| T-14b-04: dynamic_cast nullptr on non-display device | Null-check within 3 lines at every cast site (3 sites in service)                     |

## Self-Check: PASSED

Files confirmed present:

- `src/app/src/stream_dock_control_service.hpp` - FOUND
- `src/app/src/stream_dock_control_service.cpp` - FOUND
- `tests/unit/test_stream_dock_control_service.cpp` - FOUND
- `src/app/src/profile_controller.hpp` (with activeProfile()) - FOUND

Commits confirmed:

- `705af98` (Task 1) - FOUND
- `0be17b7` (Task 2) - FOUND
- `6383792` (Task 3) - FOUND

ctest suite: 414/414 green.
