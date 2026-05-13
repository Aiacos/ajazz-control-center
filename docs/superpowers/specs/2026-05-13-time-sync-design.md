# Time Sync — Feature Design

**Date:** 2026-05-13
**Status:** Approved (design A: pre-emptive scaffolding, backend stubs)
**Author:** Brainstorming session 2026-05-13
**Tracking:** TODO.md → "Time Sync feature"

## Critical context (read this first)

The 2026-05-13 vendor-app inventory pass (committed in `d5616ef`)
established that **no AJAZZ Stream Dock device firmware currently
exposes a host-settable RTC over HID**: the public Space Plugin SDK
documents 12 sendable + 19 received events, none time-related, and the
vendor desktop app's "clock widgets" are Vue components that render the
host time onto a button-face image then push that image via the
existing per-key image-upload path. Keyboard backends (AKB980 PRO) have
not yet been verified — vendor driver V1.0.0.6 is a Delphi installer
in `~/MEGAsync/Ajazz/` whose payload requires `wine`/`innoextract` to
extract for static analysis.

This feature therefore implements **pre-emptive scaffolding for a
device-firmware capability that does not exist today**. All backends
return `Result::NotImplemented` from `setTime()`. The user accepted this
in the brainstorming session because:

1. The pattern matches the existing `aj_series.cpp` precedent (wire
   format documented but unconfirmed — see file header `@warning`).
1. If a future firmware revision adds RTC support, only the backend
   `setTime()` body needs to change; UI / service / capability /
   documentation are already in place.
1. An alternative "host-side clock widget" design exists (render
   QImage host-side, push via existing image-upload) and is documented
   as future-work in the Open Questions section. It is NOT this slice.

## Goal

Let users sync the host system clock to AJAZZ devices that expose a
clock / RTC over HID — none today, but the scaffolding lets them do so
the moment a backend implements `IClockCapable::setTime()` for real.
Two modes:

1. **Manual** — a "Sync now" button per device, immediate one-shot push.
1. **Automatic** (toggle) — re-push the system time on each device connection
   event and on host time-zone change.

## Non-goals

- We do NOT manipulate the host's clock from the device (one-way push only).
- We do NOT implement time zones / DST per-device — the host is the
  authoritative wall-clock source; the device receives an absolute Unix
  timestamp (or vendor-specific equivalent).
- We do NOT bundle wire-format implementations for any device family in this
  slice. Backend stubs return `Result::NotImplemented` until a clean-room
  reverse-eng pass produces a byte-level spec per family — see Open Questions.
- We do NOT add UI for displaying the device's current time (read-back is
  out of scope; only set is in scope).

## Architecture

Five layers, each with one well-defined responsibility:

```
┌──────────────────────────────────────────────────────────────────┐
│ QML layer (UI)                                                   │
│  • SettingsPage: "Auto-sync time on device connect" toggle       │
│  • Per-device tile / row: "Sync now" button (when hasClock)      │
└──────────────────────────────────────────────────────────────────┘
                ▲                              │ user click
                │ stateChanged signals         ▼
┌──────────────────────────────────────────────────────────────────┐
│ Application layer (TimeSyncService — new)                        │
│  • setSystemTimeOn(deviceId) → IClockCapable::setTime(now)       │
│  • setAutoSync(bool, intervalSec=0)                              │
│  • Hooks: HotplugMonitor::onArrived → if autoSync, schedule sync │
└──────────────────────────────────────────────────────────────────┘
                ▲                              │ dynamic_cast
                │ deviceConnected events       ▼
┌──────────────────────────────────────────────────────────────────┐
│ Core capability layer (IClockCapable — new)                      │
│  virtual Result setTime(std::chrono::system_clock::time_point);  │
│  Capability::Clock bit flag advertised in IDevice::capabilities()│
└──────────────────────────────────────────────────────────────────┘
                ▲                              │ implementation
                │ inherited                    ▼
┌──────────────────────────────────────────────────────────────────┐
│ Device backend layer (per-device, src/devices/<family>/)         │
│  STUB: each backend that registers Capability::Clock provides    │
│  a setTime() that returns Result::NotImplemented + logs WARN.    │
│  Real wire format is filled in by a separate clean-room pass.    │
└──────────────────────────────────────────────────────────────────┘
                ▲
                │ HID write report
                ▼
            (device firmware)
```

## Components

### 1. `Capability::Clock` (new bit flag)

**File:** `src/core/include/ajazz/core/capabilities.hpp`

Add to the existing `enum class Capability` next to `PerKeyDisplay`,
`MainDisplay`, etc.:

```cpp
Clock = 1u << <next-free-bit>,   ///< Device has a host-settable RTC / clock.
```

Backends advertise it from `IDevice::capabilities()` if and only if they
also implement `IClockCapable`. The `dynamic_cast` + bit-flag pair lets the
UI decide whether to render the "Sync now" button without instantiating
backends speculatively.

### 2. `IClockCapable` interface (new mix-in)

**File:** `src/core/include/ajazz/core/capabilities.hpp`

```cpp
/**
 * @brief Mix-in for devices that expose a host-settable RTC / clock.
 *
 * Pushes an absolute moment-in-time (system_clock::time_point) to the
 * device. The implementation is responsible for translating into the
 * device's wire format (BCD, Unix epoch, vendor-specific, …) and any
 * time-zone normalisation the firmware expects.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 */
class IClockCapable {
public:
    virtual ~IClockCapable() = default;

    /**
     * @brief Set the device clock to the supplied time-point.
     * @param tp Absolute time-point (monotonic offset from system epoch).
     * @return Result::Ok on confirmed acknowledgement.
     *         Result::NotImplemented while the wire format is being
     *         reverse-engineered (current state for all device families
     *         as of 2026-05-13).
     *         Result::IoError on HID write or ack timeout.
     */
    [[nodiscard]] virtual Result
    setTime(std::chrono::system_clock::time_point tp) = 0;
};
```

### 3. `TimeSyncService` (new application-layer service)

**Files:** `src/app/src/time_sync_service.{hpp,cpp}`

Owned by `Application`; lifetime-tied. Responsibilities:

- **Manual sync** — invoked by QML "Sync now" buttons. Locates the
  `IDevice*` for the supplied `DeviceId`, `dynamic_cast`s to `IClockCapable`,
  calls `setTime(system_clock::now())`, surfaces result via signal
  (`syncSucceeded(deviceId)` / `syncFailed(deviceId, errorMessage)`).
- **Auto-sync hook** — connects to `HotplugMonitor::deviceArrived` slot.
  When `autoSync == true` and the arriving device implements
  `IClockCapable`, post a one-shot QTimer (300 ms debounce so the
  device's HID stack has settled) that calls `setTime`.
- **Persistent setting** — `autoSync` flag persisted in
  `QSettings("Time/AutoSync")`, read at construction.

The service is a `QObject` exposed to QML as a singleton via the existing
`QML_NAMED_ELEMENT` + `qmlRegisterSingletonInstance` pattern (with the
shadow-trap-aware static_assert from the recent QML_SINGLETON sweep).

### 4. UI surface

**Settings page** (`src/app/qml/SettingsPage.qml`):
A new `SwitchDelegate` row "Auto-sync time on device connect" bound
two-way to `TimeSyncService.autoSync`. Disabled (greyed) when no
connected device implements `IClockCapable`, with a tooltip explaining
why.

**Per-device row** (`src/app/qml/components/DeviceRow.qml`):
A small "Sync time" `ToolButton` rendered in the row's right-side stack
when the model row's `hasClock` role is true. Click invokes
`TimeSyncService.setSystemTimeOn(codename)`. Visual feedback: spin glyph
during call, checkmark on success, exclamation on `NotImplemented` /
`IoError` with hover-tooltip explaining state.

**`DeviceModel`** gets one new role:

```cpp
HasClockRole,   ///< True when the device advertises Capability::Clock.
```

Computed from the registered descriptor's capability bitset.

### 5. Backend stubs (per device family)

**Files:**

- `src/devices/streamdeck/src/{akp153,akp03,akp05}.cpp` —
  add `IClockCapable` to the inheritance list and emit
  `Capability::Clock` from `capabilities()`. The `setTime()` impl is:
  ```cpp
  Result Akp153Device::setTime(std::chrono::system_clock::time_point) {
      AJAZZ_LOG_WARN("akp153",
          "setTime() called but wire format is not yet implemented — "
          "see docs/protocols/streamdeck/akp153.md § Time sync");
      return Result::NotImplemented;
  }
  ```
- Mouse backends (`src/devices/mouse/src/aj_series.cpp`): we do NOT
  add `IClockCapable` — mice don't render time, no use case.
- Keyboard backend (AKB980 PRO): default to **(a) add stub now**,
  consistent with the stream-deck families. The V1.0.0.6 vendor driver
  is locally archived as a Delphi installer (Win32 PE with `MZP` magic)
  in `~/MEGAsync/Ajazz/`; extracting its payload requires `wine` /
  `innoextract` which the dev environment doesn't currently have. The
  stub keeps the surface uniform — keyboard handles `Capability::Clock`
  the same way the stream-deck families do, and the day a real wire
  format surfaces it's a one-method swap with no UI churn.

## Data flow

### Manual sync

```
[user clicks "Sync now" on AKP153 row]
  ↓ QML
TimeSyncService.setSystemTimeOn("akp153")
  ↓ C++
locate IDevice* via DeviceRegistry::open(deviceId)
  ↓
dynamic_cast to IClockCapable*
  ↓
clk->setTime(system_clock::now())
  ↓
  STUB: returns Result::NotImplemented, logs WARN
  ↓
emit syncFailed("akp153", "Time sync wire format not yet implemented")
  ↓ QML signal
exclamation glyph + tooltip on DeviceRow's button
```

### Auto-sync on connect

```
HotplugMonitor → Application::onHotplug({Arrived, vid, pid, …})
  ↓
TimeSyncService::onDeviceArrived(deviceId)
  ↓ if (autoSync && device->capabilities() & Capability::Clock)
QTimer::singleShot(300 ms, setSystemTimeOn(deviceId))
  ↓
[same stack as manual sync]
```

## Error handling

- **`Result::NotImplemented`** — surfaced as a non-blocking toast/tooltip
  on the device row. Tracked centrally in `TimeSyncService::lastError(id)`
  so re-clicking gets fresh feedback rather than stale state.
- **`Result::IoError`** — same UX as `NotImplemented`, but log level WARN.
  Auto-sync does NOT retry on `IoError` to avoid hammering a flaky link;
  the next `Arrived` event provides the natural retry.
- **No `IClockCapable` device connected** — UI button is hidden
  entirely (driven by the model role, not by a click-time check).

## Testing strategy

Three layers; each uses the existing test conventions:

1. **Unit** — `tests/unit/test_time_sync_service.cpp`:

   - Mock `IDevice` + `IClockCapable` (returns Ok / NotImplemented / IoError).
   - Mock `HotplugMonitor` driving arrivals.
   - Assert: setSystemTimeOn calls clk->setTime exactly once;
     auto-sync re-calls on each Arrived; signals fire; persistent
     setting survives service restart.

1. **Backend stubs** — each device-family unit test gets a single
   case `"setTime returns NotImplemented"` so the contract change
   doesn't accidentally regress to "compiles but doesn't surface".

1. **Integration** — `tests/integration/test_time_sync_e2e.cpp`:
   end-to-end through `Application` with a real `DeviceRegistry`
   populated by `registerAll()`. Verifies the QML model role
   `hasClock` is computed from capability bits correctly.

Coverage target for this slice: 100% of the new application-layer
service + interface + capability bit; 0% of the wire-format paths
(they don't exist yet).

## Documentation deliverables

- `docs/protocols/streamdeck/akp153.md` — new section "§ Time sync"
  with placeholder "TBD: vendor-app capture pending" and the
  expected call shape (`setTime(time_point)`).
- Same section in `akp03.md`, `akp05.md`, and (if (a) is chosen)
  `keyboard/proprietary.md`.
- `docs/_data/devices.yaml` — add `time_sync: scaffolded` capability
  flag to every entry that gets the `Capability::Clock` bit.
- `README.md` — auto-regenerated by the existing pre-commit hook.
- `CHANGELOG.md` — under "Added": "Time Sync infrastructure
  (capability + UI + service); device-side wire format is still
  TBD per family."

## Resolved design decisions (post-recon)

All five open questions were decided during the brainstorming session
on 2026-05-13. None remain blockers for implementation.

| #   | Question                                                                                 | Decision                                              | Rationale                                                                                                                                                                                                                                                                                                    |
| --- | ---------------------------------------------------------------------------------------- | ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1   | Wire format for AKB980 PRO keyboard time-sync                                            | (a) stub now                                          | V1.0.0.6 Delphi installer is locally present in `~/MEGAsync/Ajazz/` but extracting requires `wine`/`innoextract` not installed in the dev env. Stub keeps the surface uniform with stream-deck families.                                                                                                     |
| 2   | Wire format for Stream Dock family (AKP153 / AKP03 / AKP05 / 0x3004)                     | (a) stub now — feature confirmed ABSENT in vendor app | Background recon agent inventoried Mirabox 3.10.191.0421 + AJAZZ-rebranded builds and verified the Space Plugin SDK exposes 12 sendable + 19 received events, NONE time-related. Vendor "clock widget" is a host-side Vue component pushed as image. There is no firmware setTime to implement. See d5616ef. |
| 3   | Should `setTime` accept time-zone offset, or always UTC?                                 | UTC at the interface                                  | Backends are responsible for translating to local / BCD / firmware-specific representation. Avoids two encodings of the same data crossing the API boundary.                                                                                                                                                 |
| 4   | Auto-sync interval beyond per-arrival re-push?                                           | Out of scope                                          | Interval-based re-push adds queue management + power concerns; deferred until a real backend exists to validate the need.                                                                                                                                                                                    |
| 5   | Should the Sync button live in DeviceRow (sidebar) or only in a per-device detail panel? | DeviceRow only (this slice)                           | Per-device detail panel doesn't exist yet in the codebase; introducing one is its own slice. Sidebar button + Settings toggle covers both manual and automatic UX.                                                                                                                                           |

## Future-work alternative (NOT this slice)

If product later wants Stream Dock displays to actually show the
current time on a button face, the right design is **not** an
extension of this slice. It is a separate "Clock Widget" feature that
reuses the existing image-upload path:

- A host-side `ClockWidget` (Qt + QPainter) renders a 85×85 / 72×72
  PNG of the current time at ~1 Hz.
- `ButtonAssignmentService` lets the user assign the widget to a
  specific key on a Stream Dock device (alongside Key Press / Sleep
  / etc. action types).
- The render output is pushed via `IDisplayCapable::setKeyImage`,
  which already exists in `akp153.cpp` / `akp03.cpp` / `akp05.cpp`.

Tracked under TODO.md as a separate item.

## Build / test sequence

1. Add `Capability::Clock` + `IClockCapable` to `capabilities.hpp`.
1. Add stubs to each `streamdeck/<device>.cpp` (and AKB980 if (a)).
1. Add `TimeSyncService` skeleton + unit tests.
1. Wire `Application` to own `TimeSyncService`.
1. Add `HasClockRole` to `DeviceModel` + tests.
1. QML: SettingsPage toggle + DeviceRow button.
1. Update docs/protocols + devices.yaml.
1. Run full ctest suite, expect green; manual smoke-test that
   "Sync now" surfaces NotImplemented toast as designed.

## Out of scope (this slice)

- Real wire-format implementations.
- Device → host time read-back.
- Time-zone widgets, world-clock display, NTP client.
- Per-device sync history / log.
