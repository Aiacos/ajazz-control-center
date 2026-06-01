# Hot-plug detection and auto-discovery

AJAZZ Control Center automatically detects supported devices the moment they are plugged in (or unplugged), without polling and without restarting the app. This page describes the cross-platform implementation.

## Goals

1. **Zero user action** — plug a device, it appears in the sidebar within 200 ms.
1. **Single thread** — one OS event source per process, regardless of how many devices are connected.
1. **No `udev` rule needed** — works out of the box for unprivileged users on Linux.
1. **Race-free** — the device row appears *after* the backend can actually open it (some platforms emit the arrival event before the kernel has set the right permissions).

## Architecture

```
┌─────────────────────────────────────────────────┐
│ HotplugMonitor (one OS thread)                  │
│   Linux:    udev_monitor on subsystem "usb"     │
│   Windows:  RegisterDeviceNotificationW         │
│   macOS:    IOServiceAddMatchingNotification    │
└────────────────────┬────────────────────────────┘
                     │ HotplugEvent { kind, vid, pid, serial }
                     ▼
              EventBus<HotplugEvent>
                     │
        ┌────────────┴────────────┐
        ▼                         ▼
DeviceRegistry              UI / DeviceModel
  matches VID/PID            (sidebar refresh)
  → opens device
  → publishes Connected
```

## Per-platform details

### Linux — `libudev`

- Open a `udev_monitor` filtered on subsystem `usb` (kernel events only — faster than the slower userspace channel).
- Run `epoll_wait` on the monitor's fd in a dedicated thread.
- Convert the `add` / `remove` action and the `idVendor` / `idProduct` properties into `HotplugEvent`.
- **Permissions**: hidapi uses `hidraw` on modern Linux, which requires write access. The packaging step installs a udev rule (`packaging/linux/60-ajazz.rules`) granting `MODE="0664", GROUP="plugdev"` for our VIDs. Users in `plugdev` get hot-plug access without `sudo`.

### Windows — `RegisterDeviceNotificationW`

- Create a hidden message-only window from the monitor thread (`HWND_MESSAGE`).
- Register for `DBT_DEVTYP_DEVICEINTERFACE` notifications under the HID class GUID.
- Pump messages via `GetMessage` / `DispatchMessage`; convert `DBT_DEVICEARRIVAL` / `DBT_DEVICEREMOVECOMPLETE` into events.
- Parse `DBT_DEVTYP_DEVICEINTERFACE.dbcc_name` to extract `VID_xxxx&PID_xxxx`.

### macOS — `IOKit`

- `IOMasterPort` → `IOServiceAddMatchingNotification` for `kIOFirstMatchNotification` and `kIOTerminatedNotification`.
- The matching dictionary is `IOServiceMatching("IOHIDDevice")`.
- Run the notification port on a `CFRunLoop` inside the monitor thread.
- Read `kUSBVendorID` and `kUSBProductID` properties from each matched IOService.

## Race-free open

Some platforms (notably Linux with `hidraw`) emit the arrival event *before* udev has finished applying our rules. To avoid the "permission denied on first try" UX:

- The monitor thread publishes a `HotplugEvent::Arrived` event but does **not** open the device itself.
- `DeviceRegistry::onArrived()` schedules an open attempt with a 50 ms / 100 ms / 250 ms back-off (max 3 retries). The first open that succeeds publishes `DeviceEvent::Connected`.
- If all retries fail, the device row is shown in the UI with a `permission-denied` badge and a "Fix permissions" button that opens the [Linux Permissions wiki page](../wiki/Linux-Permissions.md).

## Device matching

The runtime registry indexes factories by `(vid, pid)`, populated in
`src/devices/streamdeck/src/register.cpp` (and the keyboard / mouse `register.cpp`
siblings). The AKP03 / AKP05-N4 / AKP153 Stream Dock SKUs are registered against
the out-of-process mirajazz sidecar factory; the AKP815 carve-out and the
keyboard / mouse families use their in-tree C++ factories:

```cpp
// app bootstrap (application.cpp): sidecar-backed Stream Dock SKUs
for (auto const& d : streamdeck::streamDockSidecarDescriptors())
    registry.registerDevice(d, &app::makeSidecarStreamDock);

// streamdeck::registerAll(): the AKP815 carve-out (custom C++ backend)
registry.registerDevice(akp815Descriptor, &streamdeck::makeAkp815);
```

A `HotplugEvent::Arrived` triggers a `(vid, pid)` lookup and instantiates the matching factory.

## Disabling auto-discovery

Power users who prefer manual control can set `Devices/AutoDiscover=false` in `QSettings` (UI: **Settings → Devices → Auto-discover devices**). When disabled:

- The hotplug thread still runs but only logs events.
- Devices must be opened explicitly from the sidebar's "Add device" button.
- This is also how integration tests run, replaying captured HID traffic against a `MockTransport` rather than touching real hardware.

## Testing

- Unit tests exercise the matching logic with a `FakeHotplugSource` that scripts arrival/removal events.
- An optional CI job (`hotplug-loopback`) on Linux runs a `usbip`-attached virtual stream deck to verify the udev → registry → backend chain end-to-end. The job is gated behind `[ci hotplug]` in commit messages because it is slow.
