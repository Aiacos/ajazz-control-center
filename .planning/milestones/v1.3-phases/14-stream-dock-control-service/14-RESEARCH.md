# Phase 14: Stream Dock Control Service - Research

**Researched:** 2026-05-23
**Domain:** Qt 6 / C++20 app-layer device-control service wiring an existing capture-verified HID backend (AKP05E `0300:3004`) to the application
**Confidence:** HIGH (all claims grounded in in-tree source read this session; no external packages)

\<user_constraints>

## User Constraints (from CONTEXT.md)

### Locked Decisions

**Architecture**

- The control service is an **app-layer** component (`src/app/src/`), not core — it owns Qt threading/timers and the held-open device handle. COD-031 boundary preserved (no `nlohmann::json` in `ajazz_core` or installed headers).
- **Single held-open HID handle** for the active device across the session (v1.1 ARCH-03 `weak_ptr` flyweight invariant preserved) — NOT open/close per push.
- The service is the **single device paint path** that Phases 15 (input), 16 (controls/persistence), and 19 (plugin bridge) all reuse. Design it as the reuse surface now.
- Drive image encode through the existing **`image_pipeline.{hpp,cpp}`** (ARCH-04) — do not add a second encode path.

**Wire format (RE is source of truth — do NOT alter)**

- The `BAT` header is **capture-verified** (JPEG size BE16@10-11, key index 1-based@12; real capture `43 52 54 00 00 42 41 54 00 00 08 7C 0D`). Phase 14 is **app-side wiring, not protocol change**.
- Chunked uploads use **1024-byte** packets; last-chunk Transfer-Done flag per the protocol.
- Emit a **`ULEND`** commit after each image burst (DOCK-02 — fixes the vendor "device freezes after rapid setKeyImage" defect; vendor §10 P0). Builders already exist in `akp05_protocol.hpp`.
- Probe firmware with **`VER`** (`CRT…VER`) at open; cache + surface the response (DOCK-01 — vendor returns "unknown" today).

**Honesty**

- `akp05e` advertises `hasClock=false` (DEVICES-11) — the Stream Dock family has **no firmware RTC** per ARCH-05; corrects `register.cpp` and closes Phase-10 UAT #6. (Contrast `ak980pro`, which keeps `hasClock=true` per ARCH-05.1 — do not touch that row.)

**Device facts (verified)**

- AKP05E = `0300:3004`, fw `V3.AKP05E.01.007`, routed to `makeAkp05`; **10 LCD keys (2×5), 4 endless pressable rotary encoders, touch strip**. `register.cpp` descriptor already wires `encoderCount=4` + `hasTouchStrip=true`.

### Claude's Discretion

- Exact class name/shape of the control service (e.g. `StreamDockControlService`) and whether it wraps or composes the existing `lighting_service` / `device_model` / `profile_controller`.
- Write-queue threading model (dedicated write thread vs Qt timer-driven drain) — honor the device ACK/flush semantics from `akp05_init_sequence.md`; coalesce so a burst of key assignments does not stall the UI thread.
- How "active device" selection is represented in the app (reuse existing `device_model` / `application` composition if present).

### Deferred Ideas (OUT OF SCOPE)

- Input routing, brightness slider/clear-all UI, binding persistence, pages → Phases 15-16.
- Plugin SDK (server completion, manifest/spawn, bridge, Property Inspector, store) → Phases 17-22.
- Auxiliary surfaces (encoder overlays / main strip / touch strip / DRA) → Phase 23 (HW-gated).
- Boot logo (`LOG`), `M_V`, `GIFVER` → backlog (vendor §10 P3).
  \</user_constraints>

\<phase_requirements>

## Phase Requirements

| ID                     | Description                                                                                                                        | Research Support                                                                                                                                                                                                                                                                                                                                                                              |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DISPLAY-06             | Persistent control service keeps the active Stream Deck open + brightness ON at open. Single held-open handle (ARCH-03 flyweight). | Flyweight already exists (`DeviceRegistry::open` returns shared backend per VID/PID, weak_ptr cache). `Akp05Device::open()` already opens transport + probes VER but does **NOT** issue a `LIG` brightness packet — that's the gap. App-layer service must hold the `shared_ptr<IDevice>` for the session and call `setBrightness()` at open. See "Architecture Patterns" §Held-open service. |
| DISPLAY-07             | Assigning a key image pushes it within ~1s: encode → `BAT` → 1024 chunks → `ULEND`, no manual flush.                               | `Akp05Device::setKeyImage()` **already** does encode (via `image_pipeline`) → `buildKeyImageHeader` (BAT) → 1024-byte chunked `sendImage()` → `buildUploadFinished()` (ULEND). The full device path is built and byte-tested. Phase 14 is purely the app→`setKeyImage` call + write-queue coalescing.                                                                                         |
| DISPLAY-08             | Loading a profile repaints all keys from saved bindings.                                                                           | `ProfileController` holds the active `Profile` (key `Binding`s with `KeyState.imagePath`/`background`). No code currently iterates a loaded profile to call `setKeyImage`. New service must subscribe to `ProfileController::profileChanged` and repaint.                                                                                                                                     |
| DOCK-01                | `VER` firmware probe at open, cached + surfaced.                                                                                   | `Akp05Device::probeFirmwareVersion()` already runs in `open()` via GET_FEATURE_REPORT report-id 0x01 (mirajazz method), caches in `m_firmwareVersion`, exposed via `IDevice::firmwareVersion()`. Gap is **app-side surfacing** (no QML/service reads it for AKP05). `FirmwareUpdateService` already has a DeviceLookup that reads `firmwareVersion()` — reuse that surface.                   |
| DOCK-02                | `ULEND` commit after each image burst.                                                                                             | Already emitted inside `Akp05Device::sendImage()` after every chunked upload (`buildUploadFinished()`). No new wire work; verification only (assert ULEND follows the chunk burst).                                                                                                                                                                                                           |
| DEVICES-11             | `akp05e` descriptor advertises `hasClock=false`.                                                                                   | **One-line flip** at `register.cpp:305` (`.hasClock = true,` → `false`) on the `0x0300:0x3004` / `akp05e` row only. Do NOT touch `ak980pro`. NOTE: the parallel honesty surface in `docs/_data/devices.yaml:273` still lists `clock` in the akp05e capabilities array (DEVICES-05, a Phase-10 deliverable that has not run) — see Open Questions.                                             |
| \</phase_requirements> |                                                                                                                                    |                                                                                                                                                                                                                                                                                                                                                                                               |

## Summary

Phase 14 is overwhelmingly **app-layer wiring**, not protocol work. The device backend (`Akp05Device` in `src/devices/streamdeck/src/akp05.cpp`) is already a complete, byte-tested implementation of every wire operation Phase 14 needs: `open()` opens the transport and probes firmware via `VER`/GET_FEATURE; `setKeyImage()` runs the full `image_pipeline` encode → `BAT` header → 1024-byte chunked upload → `ULEND` commit pipeline; `setBrightness()`/`clearKey()`/`flush()` all exist. The capture-verified `BAT` header and the `ULEND` sentinel are already shipping. **No app code calls any of it** — `grep` for `setKeyImage`/`setBrightness`/`->open()` across `src/app/` returns zero hits. This is exactly the Phase-10 UAT gap CONTEXT.md describes.

The work is therefore: (1) build a new app-layer `StreamDockControlService` (QObject) that resolves the active Stream Deck via the established `DeviceLookup`-over-`DeviceRegistry::open` pattern (identical to `TimeSyncService`/`LightingService`/`BatteryService`/`FirmwareUpdateService`), **holds the resulting `shared_ptr<IDevice>` for the session** (single held-open handle — the flyweight already guarantees one backend per VID/PID), issues `setBrightness()` at open so the panel lights, exposes an `assignKeyImage(keyIndex, image)` slot that calls `setKeyImage()` through a coalescing write queue, and repaints all keys when `ProfileController::profileChanged` fires; and (2) the one-line `register.cpp` `hasClock=false` honesty fix. The two behavioral gaps versus "already built" are: the backend's `open()` does **not** currently send a brightness `LIG` (panel stays dark — DISPLAY-06), and **nothing** drives the paint path from the app (DISPLAY-07/08).

Verification is entirely `MockTransport` byte-level wire assertions plus a Qt-offscreen service test — no hardware needed for the Phase 14 gating proof (hardware power-cycle smoke is Phase 25). The `makeAkp05WithTransport` test factory already exists for exactly this. The device is thread-affine (capability methods "must be called from the device's I/O thread"); the existing app services drive everything from the Qt GUI thread, so the discretionary write-queue should stay on the GUI thread (QTimer-driven drain) unless a measured stall justifies a dedicated I/O thread.

**Primary recommendation:** Create `src/app/src/stream_dock_control_service.{hpp,cpp}` as a QObject following the `LightingService`/`BatteryService` DeviceLookup pattern; have it hold the active device's `shared_ptr<IDevice>` across the session, send `setBrightness(<configured>)` immediately after `open()`, coalesce key-image assignments through a GUI-thread `QTimer` drain that calls the existing `Akp05Device::setKeyImage`, repaint on `profileChanged`, and surface the cached `firmwareVersion()`. Flip `register.cpp:305` `hasClock` to `false`. Do NOT add any new wire builder or alter `akp05.cpp`'s `sendImage`/header layout.

## Architectural Responsibility Map

| Capability                                     | Primary Tier                                     | Secondary Tier                                | Rationale                                                                                                                                                                                                |
| ---------------------------------------------- | ------------------------------------------------ | --------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Hold device open for session                   | App service (`StreamDockControlService`)         | Core registry (flyweight cache)               | The held `shared_ptr<IDevice>` keeps the registry weak_ptr alive; the service is the single owner across the session. [VERIFIED: src/core/include/ajazz/core/device_registry.hpp:144 flyweight contract] |
| Brightness-ON at open (`LIG`)                  | App service (calls `setBrightness`)              | Device backend (`buildSetBrightness`)         | Backend has the builder; `open()` does not call it today, so the *decision to light* belongs to the app. [VERIFIED: akp05.cpp:443 open() body has no LIG]                                                |
| Encode + push key image (`BAT`→chunks→`ULEND`) | Device backend (`setKeyImage`)                   | App service (decides *when* + which image)    | Encode/chunk/commit is fully in-backend already; app only triggers it. [VERIFIED: akp05.cpp:565,856]                                                                                                     |
| Image encode (RGBA→JPEG, resize)               | Device backend via `image_pipeline`              | —                                             | ARCH-04 single encode path; do not duplicate. [VERIFIED: akp05.cpp:19 includes image_pipeline.hpp]                                                                                                       |
| Repaint-on-profile-load                        | App service                                      | `ProfileController` (owns active Profile)     | Profile lives in the app layer; service subscribes to `profileChanged`. [VERIFIED: profile_controller.hpp:113]                                                                                           |
| Firmware version probe + cache                 | Device backend (`open()`/`probeFirmwareVersion`) | App service / FirmwareUpdateService (surface) | Probe is in-backend; surfacing is app-layer. [VERIFIED: akp05.cpp:825]                                                                                                                                   |
| `hasClock=false` honesty                       | Core descriptor (`register.cpp`)                 | `devices.yaml` (doc surface)                  | Descriptor is the runtime source of truth; yaml is doc-only (see memory note). [VERIFIED: register.cpp:305]                                                                                              |
| Write-queue coalescing / threading             | App service                                      | —                                             | Discretionary; Qt GUI-thread QTimer drain recommended (see Pitfalls).                                                                                                                                    |

## Standard Stack

This is a C++20 / Qt 6 in-tree phase. **No new external packages.** The "stack" is existing in-tree components to reuse.

### Core (reuse — do not reimplement)

| Component                | Location                                              | Purpose                                                                                       | Why reuse                                                                 |
| ------------------------ | ----------------------------------------------------- | --------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| `Akp05Device`            | `src/devices/streamdeck/src/akp05.cpp`                | Full AKP05E backend (open/VER/setKeyImage/setBrightness/clearKey/flush)                       | Already byte-tested; capture-verified BAT/ULEND. [VERIFIED: in-tree read] |
| `image_pipeline`         | `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` | ARCH-04 RGBA→resize→JPEG host-side encode                                                     | Single encode path; backend already uses it. [VERIFIED: akp05.cpp:19,361] |
| `DeviceRegistry::open`   | `src/core/include/ajazz/core/device_registry.hpp:144` | Flyweight backend factory (one `shared_ptr<IDevice>` per VID/PID, weak_ptr cache)             | ARCH-03 single-handle invariant; the held-open guarantee. [VERIFIED]      |
| `IDisplayCapable`        | `src/core/include/ajazz/core/capabilities.hpp:124`    | `setKeyImage`/`setKeyColor`/`clearKey`/`setMainImage`/`setBrightness`/`flush` + `DisplayInfo` | The interface the service `dynamic_cast`s to. [VERIFIED]                  |
| DeviceLookup pattern     | `src/app/src/application.cpp:84-198`                  | `codename → shared_ptr<IDevice>` lambda over `enumerate()`+`open()`                           | Established 4× (TimeSync/Lighting/Settings/Battery/Firmware). [VERIFIED]  |
| `ProfileController`      | `src/app/src/profile_controller.{hpp,cpp}`            | Active `Profile` + `profileChanged` signal                                                    | Repaint-on-load trigger source. [VERIFIED]                                |
| `MockTransport`          | `tests/unit/fixtures/mock_transport.hpp`              | Header-only `ITransport` capturing `writes()` + `enqueueReadFeature()`                        | The Phase 14 gating proof without hardware. [VERIFIED]                    |
| `makeAkp05WithTransport` | `src/devices/streamdeck/src/akp05.cpp:928`            | Test factory injecting a `MockTransport` (COD-026 DI)                                         | Lets tests assert exact wire bytes. [VERIFIED]                            |

### Supporting

| Component                            | Location                                | Purpose                                                                      | When to Use                                                                                                                                   |
| ------------------------------------ | --------------------------------------- | ---------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| `QTimer` (Qt::CoarseTimer)           | Qt 6                                    | Coalescing write-queue drain on the GUI thread                               | The recommended write-queue model (see Pitfalls §UI stall). `TimeSyncService` already uses this pattern. [VERIFIED: time_sync_service.cpp:50] |
| `FirmwareUpdateService` DeviceLookup | `src/app/src/firmware_update_service.*` | Already reads `IDevice::firmwareVersion()` per codename                      | Reuse to surface DOCK-01's cached firmware string rather than a new surface. [VERIFIED: application.cpp:183]                                  |
| QML singleton factory pattern        | `BrandingService` precedent             | `create()`/`registerInstance()` + `static_assert(!is_default_constructible)` | If the service is exposed to QML (UI hint = yes). [VERIFIED: lighting_service.hpp:96]                                                         |

### Alternatives Considered

| Instead of                                | Could Use                                                                        | Tradeoff                                                                                                                                                                                                                                                |
| ----------------------------------------- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| New `StreamDockControlService` class      | Extend `LightingService`                                                         | LightingService is firmware-RGB-specific (`IFirmwareLightingCapable`, AK980); display/paint is a different capability surface. A new class keeps the reuse surface clean for Phases 15/16/19. Recommended: new class.                                   |
| GUI-thread QTimer write-queue             | Dedicated I/O `QThread` draining a queue (vendor's `SDGeneralWriteThread` model) | Vendor uses a dedicated write thread + ACK-wait matrix. For 10 keys × ~5-15 KB JPEG that is over-engineering; GUI-thread coalescing avoids cross-thread `shared_ptr<IDevice>` hazards. Revisit only if a measured UI stall appears (Phase 25 hardware). |
| Calling `setKeyImage` directly per assign | Coalesced/debounced queue                                                        | Direct calls risk the vendor "freeze after rapid setKeyImage" defect (DOCK-02 context). ULEND mitigates per-burst; coalescing mitigates burst rate. Use a queue.                                                                                        |

**Installation:** None — no new dependencies. Build via the existing CMake targets (`ajazz_app`, `ajazz_devices_streamdeck`).

## Package Legitimacy Audit

**Not applicable.** Phase 14 installs **no external packages** (npm/PyPI/crates). It is a C++20/Qt6 in-tree change reusing existing first-party components and the existing Catch2 test harness. slopcheck/registry verification steps are inapplicable to a pure in-repo C++ phase. No `[SLOP]`/`[SUS]` exposure.

## Architecture Patterns

### System Architecture Diagram

```
                     ┌──────────────────────────────────────────────┐
   QML UI / assign   │              ajazz::app (GUI thread)          │
   profile load ───► │                                               │
                     │   ProfileController ──profileChanged──►        │
                     │      (active Profile: KeyState.imagePath/bg)   │
                     │                          │                     │
                     │                          ▼                     │
   "make active" ──► │   StreamDockControlService (NEW)              │
   device select     │     • DeviceLookup(codename)─►Registry.open() │
                     │     • holds shared_ptr<IDevice> for session   │
                     │     • setBrightness(N) at open  (DISPLAY-06)  │
                     │     • assignKeyImage(i,img) ─► write-queue    │
                     │     • repaintFromProfile()       (DISPLAY-08) │
                     │     • firmwareVersion() surface  (DOCK-01)    │
                     │                          │                     │
                     │              QTimer-coalesced drain            │
                     └──────────────────────────┼───────────────────┘
                                                 │ dynamic_cast<IDisplayCapable*>
                                                 ▼
                     ┌──────────────────────────────────────────────┐
   core::DeviceRegistry::open(id)  ── flyweight ─►  Akp05Device      │
     (one shared backend per VID/PID, weak_ptr cache, ARCH-03)       │
                     │                                               │
                     │   open(): transport.open() + probeFirmware(VER)│
                     │   setKeyImage(): image_pipeline encode         │
                     │     ─► buildKeyImageHeader(BAT)                 │
                     │     ─► 1024-byte chunked sendImage()           │
                     │     ─► buildUploadFinished(ULEND)  (DOCK-02)   │
                     │   setBrightness(): buildSetBrightness(LIG)     │
                     └──────────────────────────┼───────────────────┘
                                                 ▼
                                    ITransport (hidapi_hidraw)
                                                 ▼
                                AKP05E  0300:3004  (LCD panel)
```

File-to-implementation mapping is in the Component Responsibilities (Standard Stack) tables above; the diagram traces the assign-image and profile-load data flow.

### Recommended Project Structure

```
src/app/src/
├── stream_dock_control_service.hpp   # NEW — QObject, DeviceLookup, held shared_ptr, write queue
├── stream_dock_control_service.cpp   # NEW — open+brightness, assignKeyImage, repaintFromProfile
├── application.{hpp,cpp}             # EDIT — own m_streamDockControl; wire DeviceLookup + profileChanged
src/devices/streamdeck/src/
├── register.cpp                      # EDIT — line 305 hasClock=true → false (akp05e row only)
├── akp05.cpp                         # (optional) open() may send LIG, OR keep brightness app-side
tests/unit/
├── test_stream_dock_control_service.cpp  # NEW — MockTransport wire assertions (offscreen Qt)
docs/_data/devices.yaml               # (see Open Questions: clock capability array, DEVICES-05 overlap)
```

### Pattern 1: DeviceLookup-over-flyweight service (established 5×)

**What:** A QObject service holds a `DeviceLookup = std::function<std::shared_ptr<core::IDevice>(QString const&)>` injected by `Application`; the lambda walks `m_deviceRegistry.enumerate()` for the matching codename and returns `m_deviceRegistry.open({vid,pid,{}})`. `dynamic_cast` to the needed capability happens **inside** the service with a null-check within 3 lines (Pitfall 2).
**When to use:** This is THE app-layer device-access pattern; the control service must follow it.
**Example:**

```cpp
// Source: src/app/src/application.cpp:98 (LightingService construction) — VERIFIED in-tree
m_lighting(std::make_unique<LightingService>(
    [this](QString const& codename) -> std::shared_ptr<core::IDevice> {
        auto const descriptors = m_deviceRegistry.enumerate();
        for (auto const& d : descriptors) {
            if (QString::fromStdString(d.codename) != codename) continue;
            core::DeviceId const id{.vendorId = d.vendorId, .productId = d.productId, .serial = {}};
            return m_deviceRegistry.open(id);   // flyweight: shared backend
        }
        return nullptr;
    }, this)),
```

```cpp
// Source: src/app/src/time_sync_service.cpp:240 (doPush) — VERIFIED: hold shared_ptr across cast
std::shared_ptr<core::IDevice> const dev = m_lookup(codename);   // keeps backend alive
if (!dev) return /* not connected */;
auto* const disp = dynamic_cast<core::IDisplayCapable*>(dev.get());   // Pitfall 2: null-check next
if (disp == nullptr) return /* device has no display surface */;
disp->setBrightness(brightnessPercent);   // panel lights — DISPLAY-06
```

### Pattern 2: Held-open session handle (the DISPLAY-06 distinction)

**What:** Unlike the existing services (which resolve `shared_ptr<IDevice>` *per call* and let it drop), the control service must **retain** the `shared_ptr<IDevice>` for the active device as a member for the whole session. The flyweight cache is weak_ptr-keyed, so retaining the shared_ptr is what keeps the single HID handle open across event-loop turns (ARCH-03 invariant).
**When to use:** On "make device active" / first connect of a Stream Deck.
**Note:** Re-resolve via the lookup on hot-plug *arrival* of the same codename (Pitfall 2: do not assume the old shared_ptr is still valid after a yank+replug — the underlying device honours the zombie contract, but a fresh `open()` re-opens the transport). The `Application` hotplug→service wiring already exists (`onDeviceArrivedDebounced`, 300 ms) — model the control service's re-open on it.

### Pattern 3: Coalesced write-queue drain (DOCK-02 burst mitigation)

**What:** `assignKeyImage(i, img)` records the pending image in a `std::map<int, QImage>` (last-write-wins per key) and arms a single-shot `QTimer`; the timer slot drains all pending keys by calling `setKeyImage` once per key. This bounds the burst rate the vendor freeze defect is sensitive to, on top of the per-burst `ULEND` the backend already emits.
**When to use:** All live key-image pushes (assign + profile repaint).

### Anti-Patterns to Avoid

- **Adding a second image-encode path.** `image_pipeline` (ARCH-04) is the only encode surface; `Akp05Device::setKeyImage` already routes through it. Hand `setKeyImage` an RGBA buffer; never JPEG-encode in the app.
- **Altering the `BAT` header, chunk size, or `ULEND` sentinel.** Capture-verified + CLAUDE.md "RE is source of truth". App-side wiring only.
- **Open/close per push.** Violates the single-held-open-handle invariant; defeats the flyweight and risks the Windows exclusive-open bug (the very reason ARCH-03 exists).
- **Capturing a raw `IDevice*` across an event-loop turn.** Always hold the `shared_ptr` (UAF window closed by Phase 4 D-06). The existing services document this.
- **Driving device writes from a thread other than the one that opened it** without explicit synchronisation. `IDisplayCapable` is thread-affine ("must be called from the device's I/O thread").

## Don't Hand-Roll

| Problem                       | Don't Build                           | Use Instead                                                                                    | Why                                                                                                                                        |
| ----------------------------- | ------------------------------------- | ---------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| RGBA→JPEG encode + resize     | A new encoder in the service          | `image_pipeline` via `Akp05Device::setKeyImage`                                                | ARCH-04 single path; already handles 85×85 resize, JPEG q85, oversized-payload guard. [VERIFIED: akp05.cpp:565-578]                        |
| BAT header / chunking / ULEND | New wire builders                     | `buildKeyImageHeader` + `sendImage` + `buildUploadFinished` (already invoked by `setKeyImage`) | Capture-verified, byte-tested, ULEND already emitted. [VERIFIED: akp05.cpp:856]                                                            |
| Single shared HID handle      | A bespoke handle cache in the service | `DeviceRegistry::open` flyweight + hold the `shared_ptr`                                       | ARCH-03 invariant lives in the registry; reimplementing it re-arms the Windows exclusive-open bug. [VERIFIED: device_registry.hpp:115-144] |
| Codename→device resolution    | A new lookup mechanism                | The `DeviceLookup` lambda Application already injects                                          | 5 services use it; consistency + tested. [VERIFIED: application.cpp:84-198]                                                                |
| Firmware version probe        | A new VER round-trip in the app       | `IDevice::firmwareVersion()` (probed at open)                                                  | Probe already runs in `open()`; just read the cached string. [VERIFIED: akp05.cpp:825,435]                                                 |
| Byte-level wire test harness  | A real-HID test                       | `MockTransport` + `makeAkp05WithTransport`                                                     | Header-only, captures `writes()`, no hardware. [VERIFIED: mock_transport.hpp, akp05.cpp:928]                                               |

**Key insight:** Phase 14's device-side machinery is already complete and tested. The single highest-risk move would be "improving" the wire layer; the lowest-risk path is to treat `Akp05Device` as a finished black box and write *only* app-layer glue + one descriptor edit + tests.

## Runtime State Inventory

> Phase 14 is **not** a rename/refactor/migration phase. It is greenfield app wiring + a single descriptor value flip. The one state-adjacent item:

| Category                         | Items Found                                                                                                                                                                                                                       | Action Required                                                                                                                                                                                                                                |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Stored data                      | None — no datastore keys change.                                                                                                                                                                                                  | None.                                                                                                                                                                                                                                          |
| Live service config              | None.                                                                                                                                                                                                                             | None.                                                                                                                                                                                                                                          |
| OS-registered state              | None.                                                                                                                                                                                                                             | None.                                                                                                                                                                                                                                          |
| Secrets/env vars                 | None.                                                                                                                                                                                                                             | None.                                                                                                                                                                                                                                          |
| Build artifacts                  | None (pure source edits).                                                                                                                                                                                                         | None.                                                                                                                                                                                                                                          |
| Doc/runtime divergence (special) | `register.cpp:305` `hasClock=true` (runtime) vs `docs/_data/devices.yaml:273` still lists `clock` in akp05e `capabilities` (doc). DEVICES-11 flips the runtime; the yaml `clock` removal was DEVICES-05 (Phase 10, not executed). | DEVICES-11 = flip `register.cpp` only. Whether to also prune `clock` from the yaml is an Open Question (see below) — `devices.yaml` is doc-only per project memory; the **runtime** Sync-button visibility reads the descriptor, not the yaml. |

## Common Pitfalls

### Pitfall 1: `dynamic_cast` returns nullptr

**What goes wrong:** `dynamic_cast<IDisplayCapable*>(dev.get())` returns null if the resolved device isn't a Stream Deck (or a future non-display family is selected). Dereferencing crashes.
**Why it happens:** The DeviceLookup resolves *any* codename; only Stream Deck backends implement `IDisplayCapable`.
**How to avoid:** Null-check within 3 lines of every cast (project Pitfall 2 convention — see `time_sync_service.cpp:246`). Return/no-op cleanly.
**Warning signs:** A crash when a keyboard/mouse is the "active" device.

### Pitfall 2: Dropping the `shared_ptr<IDevice>` (UAF / handle close)

**What goes wrong:** If the service resolves the device per-call and lets the `shared_ptr` drop, the flyweight weak_ptr expires and the HID handle closes — the panel goes dark and the next push reopens (open/close churn), violating DISPLAY-06's single-held-open requirement.
**Why it happens:** Copy-pasting the per-call lookup pattern from `TimeSyncService` (which intentionally does NOT retain).
**How to avoid:** Store the active device's `shared_ptr<IDevice>` as a service member for the session; re-resolve only on hot-plug arrival.
**Warning signs:** Panel flickers/dims between assigns; repeated "device opened" log lines.

### Pitfall 3: UI thread stall on a key-assign burst

**What goes wrong:** Encoding + chunked HID writes for 10 keys synchronously on the GUI thread can block the event loop ~hundreds of ms; combined with the vendor "freeze after rapid setKeyImage" defect this both stutters the UI and can desync firmware.
**Why it happens:** Naive `for each key: setKeyImage()` on `profileChanged`.
**How to avoid:** Coalesce through a single-shot `QTimer` drain (Pattern 3); rely on the per-burst `ULEND` (already emitted) plus rate-limiting. Keep all device writes on one thread (the GUI thread is acceptable for 10 small JPEGs; profile if needed).
**Warning signs:** UI jank when switching profiles; device freeze after rapid profile switches.

### Pitfall 4: Panel stays dark despite a "successful" open

**What goes wrong:** `Akp05Device::open()` opens the transport and probes VER but never sends `LIG` — the panel can power on dark. Assigning images may then show on an unlit panel.
**Why it happens:** The brightness decision is intentionally not in the backend `open()` (confirmed: `akp05.cpp:443` has no `setBrightness`).
**How to avoid:** The control service must call `setBrightness(<configured>)` immediately after acquiring the held-open handle (DISPLAY-06). This is the core behavioral gap Phase 14 closes.
**Warning signs:** Phase-10 UAT #6 reproduction — device enumerates, images "sent", but the screen is black.

### Pitfall 5: ASCII-only test names + ctest filter flag

**What goes wrong:** Em-dash/arrow in Catch2 `TEST_CASE` names mangle under the Win32 CMD codepage and break `-R` filtering; `--test-regex` is a typo (correct flag is `--tests-regex`/`-R`).
**How to avoid:** ASCII-only names; `ctest --preset linux-release -R <name>`. (CLAUDE.md hard rules.)

### Pitfall 6: Touching the wrong `hasClock` row

**What goes wrong:** Flipping `ak980pro` (ARCH-05.1 keeps it `true`) or one of the other Stream Dock rows.
**How to avoid:** Edit only the `akp05e` / `0x0300:0x3004` row at `register.cpp:305`. The `akp05` (provisional, line 267) and `mirabox_n4` (line 284) rows are *also* Stream Dock family and *also* carry `hasClock=true` — per ARCH-05 the whole Stream Dock family has no RTC, so the planner should decide whether DEVICES-11 covers only `akp05e` (the requirement text) or all `makeAkp05` rows (the ARCH-05 logic). See Open Questions.

## Code Examples

### Brightness-ON at open (DISPLAY-06)

```cpp
// Service member, set once active device acquired:
m_activeDevice = m_lookup(codename);          // HOLD the shared_ptr for the session
if (!m_activeDevice) return;
m_activeDevice->open();                        // transport open + VER probe (idempotent: isOpen guard)
if (auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get())) {  // Pitfall 1
    disp->setBrightness(m_brightnessPercent);  // LIG — panel lights
}
// firmwareVersion() now returns the cached VER string (DOCK-01)
```

> `Akp05Device::open()` is idempotent (`if (m_transport->isOpen()) return;`, akp05.cpp:444). [VERIFIED]

### Repaint-on-load (DISPLAY-08)

```cpp
// connect(profileController, &ProfileController::profileChanged, service, &Svc::repaintFromProfile);
void StreamDockControlService::repaintFromProfile() {
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    if (disp == nullptr) return;                      // Pitfall 1
    for (auto const& [keyIndex, binding] : m_profile.bindingsForActiveDevice()) {
        // KeyState.imagePath → load RGBA → enqueue; or background → setKeyColor
        enqueueKeyImage(keyIndex, renderKeyState(binding.state));   // coalesced drain
    }
}
```

> `Binding`/`KeyState` carry `imagePath`/`background` (profile.hpp). The exact Profile→active-device key iteration accessor needs confirming against `Profile`'s full API (see Open Questions). [CITED: src/core/include/ajazz/core/profile.hpp]

### MockTransport wire assertion (the gating proof)

```cpp
// Source pattern: tests/unit/fixtures/mock_transport.hpp usage + makeAkp05WithTransport
auto transport = std::make_unique<ajazz::tests::MockTransport>();
auto* obs = transport.get();
transport->open();
obs->enqueueReadFeature({0x00, 'V','3','.','A','K','P','0','5','E','.','0','1','.','0','0','7', 0});
auto dev = ajazz::streamdeck::makeAkp05WithTransport(descriptor, id, std::move(transport));
dev->open();   // expect: VER GET_FEATURE probe recorded; (NO LIG here — backend open has none)
// then drive the service / setBrightness and assert:
auto* disp = dynamic_cast<core::IDisplayCapable*>(dev.get());
disp->setBrightness(80);
CHECK(obs->writes().back()[5] == 0x4C);  // 'L' of LIG
CHECK(obs->writes().back()[10] == 80);   // brightness byte
// key image burst: header(BAT) → chunk(s) → ULEND
std::vector<std::uint8_t> rgba(85*85*4, 0xAA);
disp->setKeyImage(/*1-based*/3, rgba, 85, 85);
// assert a 'BAT' header (writes()[k][5..7] == 'B','A','T'), then ULEND (writes().back()[5..9]=='U','L','E','N','D')
```

> `parseVersionResponse` skips the leading report-id byte and trims (akp05.cpp:320). `setKeyImage` keyIndex is 1-based (akp05.cpp:554 `keyIndexInRange` checks 1..KeyCount). [VERIFIED]

## State of the Art

| Old Approach                                            | Current Approach                                           | When Changed                     | Impact                                                                                         |
| ------------------------------------------------------- | ---------------------------------------------------------- | -------------------------------- | ---------------------------------------------------------------------------------------------- |
| `STP` flush only after image burst                      | `ULEND` commit after each burst                            | 2026-05-17 (vendor RE §10 P0)    | Already implemented in `sendImage`; Phase 14 just verifies it fires. [VERIFIED: akp05.cpp:881] |
| `VER` probe returns "unknown"                           | GET_FEATURE report-id 0x01 probe (mirajazz method), cached | 2026-05-20 (live AKP05E confirm) | DOCK-01 backend side done; app-surface remains. [VERIFIED: akp05.cpp:825]                      |
| AKP05E mis-filed as `akp03_variant_3004` (6 keys/3 enc) | Routed to `makeAkp05` (10 keys/4 enc/strip)                | 2026-05-20                       | `register.cpp:294` correct; descriptor wires encoderCount=4 + touchStrip. [VERIFIED]           |

**Deprecated/outdated:**

- `akp05_protocol.hpp` docstring on `buildVersionRequest` (lines 184-190) still says the VER response "is not yet decoded in our Ghidra dump" — **stale**; `parseVersionResponse` decodes it and a live device confirmed the format. Cosmetic only; do not let it mislead planning.
- The `KeyWidthPx`/`KeyHeightPx` constants are **85×85** (akp05_protocol.hpp:74-75), labelled "(legacy)". CONTEXT.md/ROADMAP Phase-10 text mentions "60×60" for the AKP05E key. The shipping code uses 85×85. See Open Questions — this is a geometry reconciliation, not a Phase 14 blocker (the backend resizes whatever RGBA it's handed to 85×85).

## Assumptions Log

| #   | Claim                                                                                                                    | Section                   | Risk if Wrong                                                                                                     |
| --- | ------------------------------------------------------------------------------------------------------------------------ | ------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| A1  | A new `StreamDockControlService` class (vs extending `LightingService`) is the right shape.                              | Standard Stack / Patterns | Low — discretionary per CONTEXT; either works. Re-shape is mechanical.                                            |
| A2  | GUI-thread QTimer-coalesced write-queue is sufficient (no dedicated I/O thread needed) for 10 small JPEGs.               | Pitfalls §3               | Medium — if real hardware stalls the UI, Phase 25 may need a write thread. Verifiable only on hardware.           |
| A3  | Brightness-ON belongs in the app service, not in `Akp05Device::open()`.                                                  | Responsibility Map        | Low — CONTEXT says app-layer owns the held handle + the light decision; backend `open()` deliberately has no LIG. |
| A4  | The default brightness value to send at open is a service-configured constant (e.g. 80%) until Phase 16 adds the slider. | Code Examples             | Low — any sane non-zero default lights the panel; Phase 16 owns the UI.                                           |

## Open Questions

1. **Does DEVICES-11 flip only the `akp05e` row, or all Stream Dock `makeAkp05` rows?**

   - What we know: the requirement text names `akp05e` (`0x0300:0x3004`, register.cpp:305). But `akp05` (provisional, line 267) and `mirabox_n4` (line 284) are the same backend/family and also carry `hasClock=true`; ARCH-05's "no RTC in the Stream Dock family" logic applies to all of them. The other Stream Dock families (AKP153/AKP03/AKP815 rows) also set `hasClock=true`.
   - What's unclear: scope. Strictly, DEVICES-11 = the `akp05e` row. Honestly (ARCH-05), the whole family should be `false` — but that broadens the diff and may belong to a later honesty sweep.
   - Recommendation: Flip `akp05e` per the literal requirement; surface the `akp05`/`mirabox_n4`/other-family rows as a noted follow-up so the planner can decide explicitly. Do NOT touch `ak980pro` (ARCH-05.1, different file/family).

1. **Should `docs/_data/devices.yaml:273` also drop `clock` from the akp05e capabilities array?**

   - What we know: DEVICES-05 (Phase 10, not executed) owns the yaml `clock` removal; DEVICES-11 (Phase 14) owns the `register.cpp` flip. `devices.yaml` is doc-only (drives README/wiki AUTOGEN) per project memory; runtime Sync-button visibility reads the descriptor, not the yaml.
   - What's unclear: whether Phase 14 should also touch the yaml to keep doc/runtime honest, or leave it for Phase 10/13.
   - Recommendation: Phase 14 flips the runtime descriptor (DEVICES-11). Optionally also prune the yaml `clock` for akp05e in the same honesty-fix commit (non-destructive, doc-only) to avoid a confusing doc/runtime gap — flag for planner.

1. **Key geometry: 85×85 (code) vs 60×60 (some Phase-10 prose).**

   - What we know: `akp05_protocol.hpp:74` sets the key dimensions to 85×85 px (the `KeyWidthPx` / `KeyHeightPx` constants, marked "legacy"); `akp05.cpp` resizes to those. CONTEXT/ROADMAP Phase-10 text mentions 60×60.
   - What's unclear: the true on-device key resolution (pending hardware confirmation — Phase 25).
   - Recommendation: Phase 14 does NOT change geometry — `setKeyImage` resizes any input to the constant. Leave the 60-vs-85 reconciliation to the hardware-gated Phase 25; just don't hardcode a competing size in the app.

1. **The exact `Profile` accessor for "bindings of the active device".**

   - What we know: `Profile` has key `Binding`s and `EncoderBinding`s with `KeyState`; `ProfileController` holds one active `Profile` (`m_profile`) and emits `profileChanged`. The first 120 lines of `profile.hpp` were read; the per-device binding-map accessor / `deviceCodename` field is below that.
   - What's unclear: the precise method to iterate a device's key bindings (and whether `ProfileController` exposes the `Profile` to a sibling service or only via signals).
   - Recommendation: Planner/executor should read the full `profile.hpp` + `profile_controller.cpp` to pin the iteration API before writing `repaintFromProfile`. `ProfileController` currently exposes no `Profile` getter — Phase 14 may need to add one (small, in-scope).

## Environment Availability

| Dependency                    | Required By                                 | Available          | Version                                  | Fallback                                                                                                                                                                 |
| ----------------------------- | ------------------------------------------- | ------------------ | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Qt 6 (Core/Qml)               | App service QObject + QTimer                | ✓                  | 6.7+ (project baseline)                  | —                                                                                                                                                                        |
| CMake + Ninja                 | Build                                       | ✓                  | project presets                          | —                                                                                                                                                                        |
| Catch2                        | Unit tests                                  | ✓                  | in-tree harness                          | —                                                                                                                                                                        |
| `MockTransport`               | Wire-level test (no HW)                     | ✓                  | `tests/unit/fixtures/mock_transport.hpp` | —                                                                                                                                                                        |
| AKP05E hardware (`0300:3004`) | Promotion witness (panel lights, no freeze) | ✗ at research time | —                                        | **Defer to Phase 25** — MockTransport tests are the Phase 14 gating proof per CONTEXT §specifics. systemd ≥258 `uaccess` caveat applies if connected (replug/`setfacl`). |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** AKP05E hardware — the live power-cycle smoke is hardware-gated and explicitly deferred to Phase 25; `MockTransport` byte assertions gate Phase 14.

## Validation Architecture

### Test Framework

| Property           | Value                                                                            |
| ------------------ | -------------------------------------------------------------------------------- |
| Framework          | Catch2 (in-tree), ctest                                                          |
| Config file        | CMake presets (`linux-release`, `linux-debug`, `dev`, `fuzz`)                    |
| Quick run command  | `ctest --preset linux-release -R akp05` (or the new `[stream-dock-control]` tag) |
| Full suite command | `ctest --preset linux-release`                                                   |

### Phase Requirements → Test Map

| Req ID     | Behavior                                                                      | Test Type                               | Automated Command                                     | File Exists?                                           |
| ---------- | ----------------------------------------------------------------------------- | --------------------------------------- | ----------------------------------------------------- | ------------------------------------------------------ |
| DISPLAY-06 | Service sends `LIG` brightness after open; holds one handle                   | unit (MockTransport + offscreen Qt)     | `ctest --preset linux-release -R stream_dock_control` | ❌ Wave 0                                              |
| DISPLAY-07 | `assignKeyImage` produces BAT header → 1024 chunk(s) → ULEND, no manual flush | unit (MockTransport)                    | same                                                  | ❌ Wave 0 (extends `test_akp05_protocol.cpp` patterns) |
| DISPLAY-08 | `profileChanged` repaints every bound key                                     | unit (MockTransport + fake Profile)     | same                                                  | ❌ Wave 0                                              |
| DOCK-01    | `firmwareVersion()` returns parsed VER string after open                      | unit                                    | `ctest ... -R akp05` (extend)                         | ⚠️ partial — backend probe tested; service surface ❌  |
| DOCK-02    | ULEND follows each image burst                                                | unit (MockTransport assert tail packet) | same                                                  | ⚠️ assertable now; add explicit case                   |
| DEVICES-11 | `akp05e` descriptor `hasClock == false`                                       | unit (registry/descriptor assertion)    | `ctest ... -R register` or descriptor test            | ❌ Wave 0                                              |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R stream_dock_control` (+ `-R akp05` when touching the device path).
- **Per wave merge:** `ctest --preset linux-release` (full ~408-case suite; trust the live count per CLAUDE.md, don't hand-edit).
- **Phase gate:** full suite green before `/gsd:verify-work`; MockTransport assertions are the gating proof (hardware smoke deferred to Phase 25).

### Wave 0 Gaps

- [ ] `tests/unit/test_stream_dock_control_service.cpp` — covers DISPLAY-06/07/08, DOCK-01/02 via `MockTransport` + `makeAkp05WithTransport` + an injected fake `DeviceLookup`.
- [ ] A descriptor/registry assertion for DEVICES-11 (`akp05e` `hasClock==false`) — may extend an existing register test.
- [ ] Confirm whether the offscreen QML smoke target (`tests/qml/`) needs a case for the service if it is QML-exposed (UI hint = yes). Likely a C++ unit test suffices for Phase 14; UI wiring is Phase 16.
- [ ] Possible small addition: a `Profile` getter on `ProfileController` for repaint iteration (see Open Question 4).

## Security Domain

`security_enforcement` is not set false in config; default = enabled. Phase 14 has a **narrow** security surface (no network, no plugins, no untrusted input in scope — those are Phases 17+).

### Applicable ASVS Categories

| ASVS Category         | Applies    | Standard Control                                                                                                                                                                                                                                    |
| --------------------- | ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | no         | (plugin auth is Phase 17)                                                                                                                                                                                                                           |
| V3 Session Management | no         | —                                                                                                                                                                                                                                                   |
| V4 Access Control     | no         | —                                                                                                                                                                                                                                                   |
| V5 Input Validation   | yes (mild) | Key index already range-checked in backend (`keyIndexInRange`, 1..KeyCount); JPEG payload size guarded (`sendImage` refuses > 0xFFFF, SEC-008/CWE-190). Service should clamp brightness 0..100 (backend also clamps). [VERIFIED: akp05.cpp:554,861] |
| V6 Cryptography       | no         | —                                                                                                                                                                                                                                                   |

### Known Threat Patterns for this stack

| Pattern                                                          | STRIDE        | Standard Mitigation                                                                                                                                                                                |
| ---------------------------------------------------------------- | ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Use-after-free of `IDevice` across event-loop turn (device yank) | Tampering/DoS | Hold `shared_ptr<IDevice>`; rely on the zombie-contract no-op behavior of the backend after USB removal. [VERIFIED: device_registry.hpp:130]                                                       |
| Oversized/malformed image desyncs firmware                       | DoS           | Backend caps payload at 65535 and emits ULEND; service coalesces burst rate. [VERIFIED: akp05.cpp:861]                                                                                             |
| Untrusted profile path → image load                              | Tampering     | Profile `imagePath` loading should use Qt's safe image loaders; out-of-scope deep handling is Phase 16/19, but don't introduce a raw file read without validation. [ASSUMED — confirm in Phase 16] |

## Sources

### Primary (HIGH confidence — in-tree source read this session)

- `src/devices/streamdeck/src/akp05.cpp` — open()/probeFirmwareVersion/setKeyImage/sendImage/setBrightness/parseVersionResponse, `makeAkp05WithTransport`.
- `src/devices/streamdeck/src/akp05_protocol.hpp` — BAT/ENC/MAI/DRA/LOG opcodes, ULEND/VER, PacketSize=1024, KeyWidthPx=85, geometry constants.
- `src/devices/streamdeck/src/register.cpp` — akp05e descriptor, `hasClock=true` at line 305 (the DEVICES-11 edit), family rows.
- `src/core/include/ajazz/core/capabilities.hpp` — `IDisplayCapable` (line 124), `DisplayInfo`, `IEncoderCapable`, `TimeSyncResult`.
- `src/core/include/ajazz/core/device_registry.hpp` — flyweight `open()` contract (line 115-144), ARCH-03 weak_ptr cache.
- `src/app/src/application.{hpp,cpp}` — service ownership + DeviceLookup wiring (lines 84-198), hotplug→service connection.
- `src/app/src/time_sync_service.cpp` — DeviceLookup `doPush` + hold-shared_ptr + dynamic_cast null-check pattern + QTimer.
- `src/app/src/lighting_service.hpp`, `device_model.hpp`, `profile_controller.hpp` — reuse-candidate roles + QML singleton pattern.
- `src/core/include/ajazz/core/profile.hpp` (1-120) — `Binding`/`KeyState`/`EncoderBinding`/`Action` schema.
- `tests/unit/fixtures/mock_transport.hpp`, `tests/unit/test_akp05_protocol.cpp` — test harness + existing wire-assertion patterns.
- `docs/protocols/streamdeck/akp05_init_sequence.md`, `akp05_vendor.md` — VER-at-open, no-clear-at-open, write/read-thread + ImageStruct ACK-wait model, LIG/CLE/ULEND opcode table.
- `docs/_data/devices.yaml` (akp05e row, line 265-275) — doc-only `clock` capability list.
- `CLAUDE.md` — COD-031, RE-is-source-of-truth, ASCII test names, ctest flag, hidapi_hidraw backend, hooks.

### Secondary (MEDIUM)

- Project memory notes (devices.yaml doc-only vs runtime; mouse battery; ARCH provenance) — context, cross-checked against source.

### Tertiary (LOW)

- None relied upon. No WebSearch/Context7 needed (in-tree C++ phase).

## Project Constraints (from CLAUDE.md)

- **COD-031:** no `nlohmann::json` in `ajazz_core` or any installed public header — the control service is app-layer, so safe; do not introduce json into core.
- **RE is source of truth, do NOT alter wire format:** BAT/LIG/CLE/ULEND/VER are capture-verified — app-side wiring only.
- **hidapi_hidraw backend only** — no libusb-only paths.
- **ASCII-only test names**; ctest filter is `--tests-regex`/`-R` (not `--test-regex`); working preset `ctest --preset linux-release`.
- **Atomic Conventional Commits** (`feat(streamdeck):`/`feat(app):`/`fix(streamdeck):`), direct-to-`main`-style on `feat/streamdock`; never skip pre-commit hooks; **cap concurrent execute agents at 2**.
- **Qt 6 / QML gotchas:** QML_SINGLETON needs `qmlRegisterSingletonInstance` (use the `create()`/`registerInstance()` + `static_assert(!is_default_constructible)` pattern) if the service is QML-exposed.
- **Schema doc is source of truth for JSON wire keys** (`deviceCodename` ⇄ `"device"`) — relevant when Phase 16 persists bindings; Phase 14 doesn't write profile JSON.
- **Cross-platform strictness:** Apple Clang `-Wunused-const-variable`, MSVC `/W4 /WX` C4996 (prefer `_s` variants), moved-from `std::wstring` size implementation-defined.

## Metadata

**Confidence breakdown:**

- Standard stack / reuse surfaces: HIGH — every component read in-tree this session.
- Architecture (held-open service, DeviceLookup, write-queue): HIGH for the pattern (5 in-tree precedents); MEDIUM for the discretionary threading choice (A2, hardware-dependent).
- Pitfalls: HIGH — derived from in-tree comments + ARCH-03/Pitfall-2 conventions + vendor freeze defect.
- Geometry (85 vs 60) + DEVICES-11 row scope + Profile accessor: flagged as Open Questions, MEDIUM.

**Research date:** 2026-05-23
**Valid until:** 2026-06-22 (stable in-tree codebase; re-verify `register.cpp` line numbers and `Profile` API if the branch advances materially before planning).
