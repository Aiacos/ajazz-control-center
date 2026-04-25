# Threading and synchronization

This page documents which threads exist in AJAZZ Control Center, what they own, and how they communicate. The model is intentionally minimal: one Qt main thread, one I/O thread per open device, one Python interpreter, and a snapshot-based event bus that bridges them.

## Thread inventory

| Thread              | Owner                 | Lifetime            | Cardinality            |
| ------------------- | --------------------- | ------------------- | ---------------------- |
| **Main / UI**       | Qt event loop         | Process             | 1                      |
| **Device I/O**      | `IDevice::open()`     | Device open → close | 1 per *open* device    |
| **Hotplug**         | `HotplugMonitor`      | Process             | 1                      |
| **Plugin / Python** | `PluginHost`          | Action dispatch     | 1 (GIL serialized)     |
| **Logger**          | `ajazz::core::Logger` | Process             | 1 (background flusher) |

Background workers spawned by plugins (`asyncio` loops, `concurrent.futures` pools, etc.) are not counted here — they're the plugin's responsibility.

## Main / UI thread

Runs `QGuiApplication::exec()`. Owns:

- The QML scene graph and all `Q_OBJECT` controllers (`DeviceModel`, `ProfileController`, `BrandingService`, `TrayController`).
- `QSettings` reads / writes — `QSettings` is documented thread-safe but we keep all access here for simplicity.
- The window hide/show transitions (including tray-minimize-on-startup).

It **must never block on USB I/O**. All transport calls happen on device I/O threads; results return through `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.

## Device I/O threads

Each open device starts a reader thread inside `IDevice::open()`. Concretely:

```cpp
void Akp03Device::open() {
    transport_->open(...);
    reader_ = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested()) {
            if (auto ev = poll(); ev) eventCb_(*ev);
        }
    });
}
```

Invariants:

- Only the reader thread calls `transport_->read()`.

- Only the main thread (or the plugin thread) calls `transport_->write()`. Writes are serialized by the OS HID layer; we additionally hold a per-device `std::mutex` around the write to keep multi-packet image transfers atomic.

- `EventCallback` is invoked **from the reader thread**. It must be cheap and non-blocking. The default callback in `Application` re-posts to the main thread:

  ```cpp
  QMetaObject::invokeMethod(deviceModel_,
                            [=]{ deviceModel_->onEvent(ev); },
                            Qt::QueuedConnection);
  ```

- A reader thread may *only* end via the `std::stop_token`. The destructor of `IDevice` requests stop, joins, and only then closes the transport handle.

## Hotplug thread

`HotplugMonitor` (`src/core/include/ajazz/core/hotplug_monitor.hpp`) opens a platform handle and pumps device-arrival/removal events:

- Linux: `udev_monitor` on the `usb` subsystem.
- Windows: `RegisterDeviceNotificationW` against a hidden message-only window.
- macOS: `IOServiceAddMatchingNotification`.

Events are published into the same `EventBus` as device input. The main thread subscribes; backends subscribe too so that a removed device cleanly tears down its reader.

The monitor thread is **single-shot per OS**: one thread serves the whole process, regardless of how many devices are connected.

## Plugin thread (Python)

`PluginHost` initialises the embedded interpreter once at app startup and serializes action dispatch behind the GIL:

```cpp
void PluginHost::dispatch(ActionId id, ActionContext ctx) {
    pybind11::gil_scoped_acquire gil;
    handlers_[id](ctx);
}
```

Long-running Python code (HTTP calls, OBS websockets, etc.) is the plugin's responsibility — they should spawn a `threading.Thread` or use `asyncio.run_in_executor`. The host does **not** silently offload work, because doing so would change the perceived ordering of side-effects.

The host catches exceptions at the boundary, logs them, and surfaces a non-blocking notification so a misbehaving plugin cannot crash the app.

## Event bus

`EventBus` is a tiny publish/subscribe utility (`src/core/include/ajazz/core/event_bus.hpp`):

- Subscribers register a `Handler = std::function<void(DeviceId const&, DeviceEvent const&)>` and receive a `Subscription` token to unsubscribe.
- `publish()` takes a `std::lock_guard<std::mutex>` to copy the handler map into a local snapshot, releases the lock, then invokes each handler outside the critical section. This means a subscriber can `subscribe()` / `unsubscribe()` from inside a callback without deadlocking.
- The single mutex is preferred over `std::shared_mutex` here because subscriptions are rare relative to publishes and the snapshot is small; the simpler primitive is easier to reason about. If profiling later shows lock contention on the publish path, switching to `std::shared_mutex` is a drop-in change.
- The bus does not promise FIFO ordering across publishers — only within a single publisher's call sequence.

## Timing-sensitive paths

| Path                              | Hard deadline | Soft target |
| --------------------------------- | ------------- | ----------- |
| Key press → tray notification     | < 50 ms       | < 10 ms     |
| Encoder turn → screen redraw      | < 33 ms       | < 16 ms     |
| Image push to AKP153 (72×72 JPEG) | < 100 ms      | < 50 ms     |
| Hotplug detect → UI row appears   | < 500 ms      | < 200 ms    |

These targets are *informally* validated by the integration test suite (`tests/integration/test_capture_replay.cpp`), which replays canned HID traffic and asserts a wall-clock budget.

## Dos and don'ts

✅ Do call `IDevice::open()` from the main thread; the reader thread is created internally.
✅ Do post UI-touching work back to the main thread with `QMetaObject::invokeMethod`.
✅ Do release the GIL before doing any blocking C++ work in pybind11 wrappers (we use `pybind11::call_guard<pybind11::gil_scoped_release>`).

❌ Don't call any QObject method from a reader thread.
❌ Don't share a `hid_device*` between threads. The OS HID layer accepts concurrent reads and writes, but our code base assumes the read/write split above.
❌ Don't capture `this` raw in lambdas posted to other threads — use `QPointer<T>` (Qt) or weak ownership.
