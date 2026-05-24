---
phase: 15-stream-dock-input-routing
reviewed: 2026-05-24T00:00:00Z
depth: standard
files_reviewed: 5
files_reviewed_list:
  - src/app/src/stream_dock_input_service.hpp
  - src/app/src/stream_dock_input_service.cpp
  - src/app/src/application.hpp
  - src/app/src/application.cpp
  - tests/unit/test_stream_dock_input_service.cpp
findings:
  critical: 3
  warning: 2
  info: 1
  total: 6
status: issues_found
---

# Phase 15: Code Review Report

**Reviewed:** 2026-05-24T00:00:00Z
**Depth:** standard
**Files Reviewed:** 5
**Status:** issues_found

## Summary

Phase 15 adds `StreamDockInputService` (poll pump, DeviceEvent dispatch, 16 ms
rotation coalescer, encoder-release synthesis, provisional touch-zone map) plus
the `QtExecutor` / `ActionEngine` wiring in `Application`. The dispatch logic,
encoder delta handling, and touch-zone math are sound. Three blockers were
found: two are in the same exception/thread-safety class explicitly flagged as
Phase 14 CR-01/CR-02 regressions (uncaught `poll()` throw from a QTimer slot;
cross-thread `setActiveDevice` call in the Removed hotplug path), and one is a
stale callback that leaves a dangling `this`-capture inside the IDevice backend
after the service releases its device handle.

______________________________________________________________________

## Critical Issues

### CR-01: `pump()` does not guard `poll()` exceptions — `std::terminate` on device yank

**File:** `src/app/src/stream_dock_input_service.cpp:118`

**Issue:**
`pump()` calls `m_device->poll()` without a try/catch. `poll()` calls
`ITransport::read()` internally; `HidTransport::read()` throws
`std::runtime_error` when `hid_read_timeout` returns `-1` (i.e., on a device
yank mid-cycle — `hid_transport.cpp:196`). An uncaught exception propagating
through a `QTimer::timeout` slot into Qt's event loop calls `std::terminate`.

This is the exact Phase 14 CR-01/CR-02 failure class: the control service's
`drainPendingWrites` was fixed with a try/catch at `stream_dock_control_service.cpp:233`.
The input service's read path has the same exposure and is unguarded.

**Fix:**

```cpp
std::size_t StreamDockInputService::pump() {
    if (!m_device) {
        return 0;
    }
    try {
        return m_device->poll();
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("stream-dock-input",
                       "poll() failed (device likely yanked): {}", e.what());
        // Release handle; next hot-plug arrival will call setActiveDevice again.
        m_device->onEvent({});
        m_device.reset();
        m_pollTimer->stop();
        m_coalesceTimer->stop();
        return 0;
    }
}
```

______________________________________________________________________

### CR-02: `onHotplug` Removed path calls `setActiveDevice(nullptr)` from background thread

**File:** `src/app/src/application.cpp:643`

**Issue:**
`Application::onHotplug` is invoked from the `HotplugMonitor`'s dedicated
background thread (documented in `hotplug_monitor.hpp:48` and the comment at
`application.cpp:564`). In the `Removed` branch (lines 634-646), the code calls
`m_streamDockInput->setActiveDevice(nullptr)` **directly** — there is no thread
marshal (no `QMetaObject::invokeMethod`, no `QTimer::singleShot` with a context
object, and no posted event).

`setActiveDevice` is not thread-safe: it calls `m_pollTimer->stop()` and
`m_coalesceTimer->stop()` on `QTimer` objects that are owned by the GUI thread,
and it resets `m_device` (a `std::shared_ptr` with no internal lock). This races
with the GUI thread's `QTimer::timeout -> pump()` which reads `m_device`. The
race can cause a torn read of `m_device`, a double-free, or a QTimer method call
from the wrong thread — all undefined behavior.

By contrast, the `Arrived` path at line 618-629 correctly uses `QTimer::singleShot`
with `m_streamDockControl.get()` as the context object, which marshals execution
to the GUI thread. The `Removed` path has no equivalent marshal.

**Fix:**

```cpp
// In the Removed branch, replace the direct call:
//   m_streamDockInput->setActiveDevice(nullptr);
// with a marshalled call:
QMetaObject::invokeMethod(
    m_streamDockInput.get(),
    [this] { m_streamDockInput->setActiveDevice(nullptr); },
    Qt::QueuedConnection);
```

Or equivalently, use `QTimer::singleShot(0, m_streamDockInput.get(), [this] { ... })`.

______________________________________________________________________

### CR-03: Stale `[this]` capture left in IDevice callback after `setActiveDevice(nullptr)`

**File:** `src/app/src/stream_dock_input_service.cpp:93-95`

**Issue:**
`setActiveDevice(nullptr)` resets `m_device` without first clearing the callback
registered on the previous device:

```cpp
if (!device) {
    m_device.reset();   // <- drops this service's ref; device backend still alive
    return;
}
```

The IDevice backend (`Akp05Device`) retains whatever lambda was last passed to
`onEvent()` — in this case `[this](core::DeviceEvent const& ev) { dispatch(ev); }`,
where `this` is the `StreamDockInputService` pointer. After `m_device.reset()`,
`StreamDockControlService` still holds a `shared_ptr` to the same backend
(ARCH-03 flyweight). If any other code path calls `poll()` on the shared backend
(e.g., if a future service or test drives the same handle), the stale callback
fires into the `StreamDockInputService` object through a raw pointer.

Additionally, when a new device is set via `setActiveDevice(newDevice)` (not
null), the old device similarly retains the callback until the new device's
`onEvent` overwrites it — but only if both are the same backend pointer (flyweight
case). If a different backend is passed, the old backend keeps the stale `this`.

While the current codebase does not reach this through any reachable call path
(only `StreamDockInputService::pump()` drives `poll()`, and `pump()` checks
`m_device` first), the latent ownership violation creates a future UAF as the
architecture expands.

**Fix:**
Clear the callback before resetting the device handle:

```cpp
void StreamDockInputService::setActiveDevice(std::shared_ptr<core::IDevice> device) {
    m_pollTimer->stop();
    m_coalesceTimer->stop();
    m_encAccum.fill(0);

    // Always deregister the callback on the outgoing device before releasing
    // the handle, so no stale `this` capture survives in the backend.
    if (m_device) {
        m_device->onEvent({});
    }

    if (!device) {
        m_device.reset();
        return;
    }

    m_device = std::move(device);
    m_device->onEvent([this](core::DeviceEvent const& ev) { dispatch(ev); });
    m_pollTimer->start();
}
```

______________________________________________________________________

## Warnings

### WR-01: `openUrl` executor uses `QUrl::fromUserInput` — opens arbitrary local files

**File:** `src/app/src/application.cpp:303-306`

**Issue:**
The `openUrl` executor calls `QUrl::fromUserInput(...)`, which converts bare
local-path strings (e.g., `"/etc/passwd"`, `"~/Documents/secret.pdf"`) into
`file://` URLs and opens them in the default handler. Any profile that binds an
`OpenUrl` action to a local path will silently open that file. If profiles can
be loaded from external/untrusted sources (network, plugins), this becomes an
arbitrary-file-open vector.

`QUrl::fromUserInput` is the correct convenience function for human-typed input
(address bars), not for programmatic URL dispatch where scheme enforcement is
expected.

**Fix:**

```cpp
execs.openUrl = [](std::string_view url) {
    QUrl qurl(QString::fromUtf8(url.data(), static_cast<qsizetype>(url.size())));
    // Only dispatch URLs with an explicit http/https scheme.
    if (qurl.scheme() != QStringLiteral("http") &&
        qurl.scheme() != QStringLiteral("https")) {
        AJAZZ_LOG_WARN("input", "openUrl: rejected non-http(s) URL scheme '{}'",
                       qurl.scheme().toStdString());
        return;
    }
    QDesktopServices::openUrl(qurl);
};
```

Alternatively, use `QUrl(str, QUrl::StrictMode)` and validate the scheme before
dispatching.

______________________________________________________________________

### WR-02: `kEncoderCount` defined twice with different types — silent divergence risk

**File:** `src/app/src/stream_dock_input_service.cpp:42` and
`src/app/src/stream_dock_input_service.hpp:191`

**Issue:**
`kEncoderCount` is defined in two places with different types:

- `stream_dock_input_service.hpp:191`: `static constexpr std::size_t kEncoderCount = 4;`
  (class member; used for `m_encAccum` array size and the `drainCoalescedRotation` loop)
- `stream_dock_input_service.cpp:42` (anonymous namespace): `inline constexpr std::uint8_t kEncoderCount = 4;`
  (used by `zoneForX`)

The anonymous-namespace definition shadows the class member inside the `.cpp`
translation unit. There is no compile-time assertion that both equal
`akp05::EncoderCount`. If one is updated without the other, `zoneForX` and
`m_encAccum` silently disagree on the encoder count, producing wrong zone
routing and/or an out-of-bounds accumulator index.

**Fix:**
Remove the anonymous-namespace duplicate. Reference the class constant from
`zoneForX` (it is a `static` member so it is accessible without `this`), or
pull `akp05::EncoderCount` from the protocol header directly:

```cpp
// In the anonymous namespace, remove:
//   inline constexpr std::uint8_t kEncoderCount = 4;
//
// In zoneForX, use the class constant:
std::uint16_t StreamDockInputService::zoneForX(std::uint16_t x) noexcept {
    auto const raw =
        static_cast<std::uint32_t>(x) *
        static_cast<std::uint32_t>(StreamDockInputService::kEncoderCount) /
        static_cast<std::uint32_t>(kTouchStripRangeX);
    auto const capped = std::min<std::uint32_t>(
        raw, static_cast<std::uint32_t>(StreamDockInputService::kEncoderCount) - 1u);
    return static_cast<std::uint16_t>(capped);
}
```

______________________________________________________________________

## Info

### IN-01: No test for `setActiveDevice(nullptr)` / device-removal path

**File:** `tests/unit/test_stream_dock_input_service.cpp`

**Issue:**
The test suite covers key dispatch, encoder CW/CCW coalescing, encoder press +
synthetic release, touch tap zone routing, swipe page-nav, Sleep non-blocking,
and held-handle idempotency. There is no test that calls
`setActiveDevice(nullptr)` (i.e., simulates a device-removal event) and verifies
that the poll timer stops, the coalescer is reset, and no further events dispatch.
This is directly relevant to T-15-05 (UAF on removal) — the mechanism is tested
indirectly by the fact that the service does not crash, but there is no positive
assertion that the removal path is clean.

**Fix:**
Add a test section in the `Held handle` test case (or a new test case) that:

1. Sets an active device and verifies `pump()` dispatches events.
1. Calls `setActiveDevice(nullptr)`.
1. Verifies that a subsequent `pump()` returns 0 and no action fires.
1. Optionally, verifies `obs->isOpen()` is still true (device is not closed by
   the input service — close is the control service's responsibility).

______________________________________________________________________

_Reviewed: 2026-05-24T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
