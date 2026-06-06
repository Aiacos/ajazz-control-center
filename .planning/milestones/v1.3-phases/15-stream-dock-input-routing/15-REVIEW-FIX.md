---
phase: 15-stream-dock-input-routing
fixed_at: 2026-05-24T12:06:00Z
review_path: .planning/phases/15-stream-dock-input-routing/15-REVIEW.md
iteration: 1
findings_in_scope: 5
fixed: 6
skipped: 0
status: all_fixed
---

# Phase 15: Code Review Fix Report

**Fixed at:** 2026-05-24T12:06:00Z
**Source review:** `.planning/phases/15-stream-dock-input-routing/15-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 5 (3 Critical + 2 Warning)
- Fixed: 6 (all 5 in-scope + IN-01 per objective)
- Skipped: 0

**Build result:** Clean -- 424/424 tests passing (+1 from new IN-01 test, 3 SECTIONs), 0 failures, 0 build warnings.

## Fixed Issues

### CR-01: pump() does not guard poll() exceptions -- std::terminate on device yank

**Files modified:** `src/app/src/stream_dock_input_service.cpp`
**Commit:** `6808acb`
**Applied fix:** Wrapped `m_device->poll()` in `try/catch(std::exception const& e)` inside
`StreamDockInputService::pump()`. On catch: logs WARN via AJAZZ_LOG_WARN("stream-dock-input"),
deregisters the `[this]`-capturing callback (`m_device->onEvent({})`), resets `m_device`,
stops both `m_pollTimer` and `m_coalesceTimer`, and returns 0. This is the exact Phase 14
CR-01/CR-02 fix pattern applied to the input service's read path. Added `<stdexcept>` include.

### CR-02: onHotplug Removed path calls setActiveDevice(nullptr) from background thread

**Files modified:** `src/app/src/application.cpp`
**Commit:** `1ac5b37`
**Applied fix:** Replaced the direct `m_streamDockInput->setActiveDevice(nullptr)` call in the
`Removed` branch of `Application::onHotplug` with `QMetaObject::invokeMethod(m_streamDockInput.get(), [this]{ m_streamDockInput->setActiveDevice(nullptr); }, Qt::QueuedConnection)`. This marshals the call to the GUI thread, matching the `Arrived` path's `QTimer::singleShot` marshal and eliminating the data race on `m_device` and the `QTimer` objects.

### CR-03: Stale [this] capture left in IDevice callback after setActiveDevice(nullptr)

**Files modified:** `src/app/src/stream_dock_input_service.cpp`
**Commit:** `8bf5bbe`
**Applied fix:** Added `if (m_device) { m_device->onEvent({}); }` in `setActiveDevice()` before the
`if (!device) { m_device.reset(); return; }` early-return path. The empty `{}` std::function clears
the stored lambda from the backend before the handle is released, so the still-alive backend
(held by `StreamDockControlService` via the ARCH-03 flyweight) cannot fire a dangling
`this`-pointer callback. This fix covers both the null-device path and the device-replacement path.

### WR-01: openUrl executor uses QUrl::fromUserInput -- opens arbitrary local files

**Files modified:** `src/app/src/application.cpp`
**Commit:** `fd86bf1`
**Applied fix:** Replaced `QUrl::fromUserInput(...)` with strict `QUrl(str)` construction and
added an explicit scheme check before dispatching. Any URL whose scheme is not `http` or `https`
is rejected with `AJAZZ_LOG_WARN("input", ...)` and returns early. `QDesktopServices::openUrl`
is only called for approved schemes, eliminating the arbitrary-file-open vector.

### WR-02: kEncoderCount defined twice with different types

**Files modified:** `src/app/src/stream_dock_input_service.cpp`
**Commit:** `dc38744`
**Applied fix:** Removed the anonymous-namespace `inline constexpr std::uint8_t kEncoderCount = 4`
definition that was shadowing the class constant `StreamDockInputService::kEncoderCount`
(`static constexpr std::size_t`, declared in the header at line 191). Updated `zoneForX` to
reference `StreamDockInputService::kEncoderCount` explicitly. The other uses in
`onEncoderTurned` and `drainCoalescedRotation` (non-static member functions) now resolve to the
class constant naturally. Single source of truth; no silent divergence risk.

### IN-01: No test for setActiveDevice(nullptr) / device-removal path (added per objective)

**Files modified:** `tests/unit/test_stream_dock_input_service.cpp`
**Commit:** `92507e3`
**Applied fix:** Added `TEST_CASE "IN-01: setActiveDevice(nullptr) stops poll and subsequent pump() is a no-op"` with three `SECTION`s:

1. `pump() dispatches events while device is active` -- positive baseline assertion.
1. `setActiveDevice(nullptr) then pump() returns 0 and no action fires` -- confirms CR-01/CR-03
   path: pump returns 0 with null device, no spurious dispatches.
1. `setActiveDevice(nullptr) does not close the transport` -- confirms ARCH-03: the input
   service releases the handle but does not call `close()`.

## Skipped Issues

None.

______________________________________________________________________

_Fixed: 2026-05-24T12:06:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
