# Architecture Research — v1.2 Connected-Device Capability Parity

**Milestone:** v1.2 — Connected-Device Capability Parity
**Researched:** 2026-05-15
**Confidence:** HIGH (every claim verified against working-tree HEAD)

## Executive summary

v1.2 lands real wire formats inside **already-existing capability mix-ins**.
This is the architecturally cheapest milestone yet — the v1.1 research call to
make `capabilities.hpp` the single source of capability interfaces held up
perfectly. Every advertised capability for the four connected devices (display,
encoder, rgb, dpi, macros/layers via `IKeyRemappable`, clock) **already has an
interface in `src/core/include/ajazz/core/capabilities.hpp`**, the
factories in `register.cpp` are already wired, and the four connected codenames
already advertise their capability bits in `DeviceDescriptor`.

The work per device reduces to four buckets:

1. **Replace stubs with real wire formats** in the existing backend `.cpp`
   files (Akp03Device, ProprietaryKeyboard, AjSeriesMouse).
1. **Materialise the image pipeline** (`IDisplayCapable::setKeyImage` is the
   only mix-in still passing through a "caller is expected to pass already-PNG
   bytes" no-op; the AKP03 production callers need a resize+encode step).
1. **Add wire-format unit tests** (`tests/unit/test_<codename>_protocol.cpp`)
   and capture-replay integration tests (`tests/integration/fixtures/<codename>/`)
   per the `akp153/key_press_07.hex` precedent.
1. **Honest negative tests** — assert that v1.1's `NotImplemented` sentinel is
   replaced *only* where a real capture confirms a wire format. Don't lie
   about the others.

**Correction relative to milestone_context:** No new `I*Capable` mix-in needs
to be created. Every capability the four connected devices advertise has a
header today. The interfaces are present; the **bodies in the backend `.cpp`
files** are the work.

**No new ARCH-NN decision is strictly required**, but two candidates emerge
for explicit ratification in Phase 9:

- **ARCH-04**: an image-encoding pipeline location decision. The
  `setKeyImage` doc says "Implementations are responsible for any resizing,
  color-space conversion, and codec encoding" but the AKP03 implementation
  ducks this — TODO commentary admits "the caller is expected to pass
  already-PNG-encoded bytes for now". A v1.2 decision should make this
  explicit: either keep the contract and add a Qt-side helper, or move the
  pipeline into a non-core `ajazz_imaging` static lib that the device
  backends PRIVATE-link.
- **ARCH-05**: clock-write feasibility per device family. If Phase 9 USB
  captures reveal AKP03 has a settable RTC, the `Out of Scope` row in
  PROJECT.md for `setTime` wire formats is **invalidated** and ARCH-05
  ratifies the new contract.

## Existing architecture — verified at HEAD (2026-05-15)

```
┌──────────────────────────────────────────────────────────────────────┐
│  src/app/  (libajazz-app — Qt 6 Widgets + QML)                      │
│                                                                      │
│  Application (owns m_deviceRegistry, m_hotplug, m_debouncer,        │
│   m_timeSync, …)                                                    │
│   ├── DeviceModel (with HasClockRole + MaturityRole)                │
│   ├── TimeSyncService (CANONICAL mix-in consumer; QML singleton)    │
│   │   • DeviceLookup → m_registry.enumerate() → registry.open(id)   │
│   │   • doPush(codename) holds shared_ptr<IDevice> in local         │
│   │   • dynamic_cast<IClockCapable*>(dev.get()) + null check        │
│   │   • Pitfall 4 build-break: static_assert(!is_default_…)         │
│   │   • Registered via TimeSyncService::registerInstance(…)         │
│   └── HotplugMonitor + HotplugDebouncer (300ms trailing-edge)       │
└──────────────────────────────────────────────────────────────────────┘
                ▲                                  │
                │ registerAll(registry)            │ onHotplug → debouncer
                │                                  │   → m_timeSync->
                ▼                                  │   onDeviceArrivedDebounced
┌──────────────────────────────────────────────────────────────────────┐
│  src/devices/  (libajazz_streamdeck / _keyboard / _mouse)           │
│                                                                      │
│  registerAll(DeviceRegistry&) per family — all 3 are wired and use  │
│  shared_ptr<IDevice> factories.                                     │
│                                                                      │
│  Streamdock:                                                         │
│    Akp153Device  : IDevice, IDisplayCapable, IFirmwareCapable,      │
│                    IClockCapable                                     │
│    Akp03Device   : IDevice, IDisplayCapable, IEncoderCapable,       │
│                    IClockCapable        ◄── 0x0300:0x3004 routes    │
│                                              here via makeAkp03      │
│    Akp05Device   : IDevice, IDisplayCapable, IEncoderCapable,       │
│                    IClockCapable                                     │
│    Akp815Device  : IDevice, IDisplayCapable, IFirmwareCapable,      │
│                    IClockCapable                                     │
│                                                                      │
│  Keyboard:                                                           │
│    ViaKeyboard         : IDevice, IKeyRemappable, IRgbCapable,      │
│                          IFirmwareCapable  (NO IClockCapable — D-03) │
│    ProprietaryKeyboard : IDevice, IKeyRemappable, IRgbCapable,      │
│                          IFirmwareCapable, IClockCapable            │
│                          ◄── 0x0c45:0x8009 (ak980pro) routes here    │
│                                                                      │
│  Mouse:                                                              │
│    AjSeriesMouse : IDevice, IMouseCapable, IRgbCapable              │
│                    (NO IClockCapable per current register.cpp)       │
│                    ◄── 0x3151:0x5007 (ajazz_24g_8k) routes here      │
└──────────────────────────────────────────────────────────────────────┘
                                  ▲
                                  │ #include
                                  ▼
┌──────────────────────────────────────────────────────────────────────┐
│  src/core/  (libajazz_core — pure C++20, no Qt, no nlohmann)        │
│                                                                      │
│  capabilities.hpp                                                    │
│   • enum Capability + static_assert (Capability::Clock = 1u << 15)  │
│   • Rgb POD                                                          │
│   • DisplayInfo struct + IDisplayCapable (8 methods)                │
│   • RgbZone / RgbEffect + IRgbCapable (5 methods)                   │
│   • EncoderInfo + IEncoderCapable (2 methods)                       │
│   • KeyboardLayout + IKeyRemappable (5 methods incl. setMacro)      │
│   • DpiStage + IMouseCapable (10 methods incl. dpi/poll/lod/btn/bat)│
│   • FirmwareInfo + IFirmwareCapable (3 methods)                     │
│   • TimeSyncResult enum + IClockCapable (1 method)                  │
│                                                                      │
│  device.hpp                                                          │
│   • DeviceId / DeviceFamily / DeviceDescriptor (incl. hasClock,     │
│     hasRgb, hasTouchStrip, dpiStageCount, encoderCount, keyCount)   │
│   • IDevice + DevicePtr (shared_ptr<IDevice>) + DeviceFactory       │
│                                                                      │
│  device_registry.hpp                                                 │
│   • DeviceRegistry (ARCH-02 HidEnumerator seam, ARCH-03 shared_ptr) │
│   • flyweight: weak_ptr<IDevice> cache keyed on (vid, pid)          │
│                                                                      │
│  hotplug_monitor.hpp                                                 │
│   • HotplugMonitor + AJAZZ_TESTING-gated injectEvent shim           │
│   • Win32 parseDevicePathW helper (AJAZZ_TESTING-gated)             │
│                                                                      │
│  transport.hpp + hid_transport.hpp                                   │
│   • ITransport (open/close/write/read/writeFeature/readFeature)     │
│   • TransportPtr = unique_ptr<ITransport>                           │
│   • makeHidTransport(vid, pid, serial) — only HidTransport          │
│     constructor visible; backends inject MockTransport in tests via │
│     the second backend ctor (DI, COD-026 pattern)                   │
└──────────────────────────────────────────────────────────────────────┘
```

**COD-031 boundary verified at HEAD:**
`grep -rn nlohmann src/core/include/ src/core/src/` → 0 hits. The
`nlohmann::json` dep is PRIVATE-linked to `ajazz_plugins` only.

**Anchor files (file:line):**

| Concern                                       | File                                                | Lines       |
| --------------------------------------------- | --------------------------------------------------- | ----------- |
| All capability mix-ins                        | `src/core/include/ajazz/core/capabilities.hpp`      | 31-601      |
| `enum Capability` + `static_assert` lock      | `src/core/include/ajazz/core/capabilities.hpp`      | 31-59       |
| `IDisplayCapable`                             | `src/core/include/ajazz/core/capabilities.hpp`      | 99-197      |
| `IRgbCapable`                                 | `src/core/include/ajazz/core/capabilities.hpp`      | 199-272     |
| `IEncoderCapable`                             | `src/core/include/ajazz/core/capabilities.hpp`      | 274-324     |
| `IKeyRemappable` (layers/macros/keymap)       | `src/core/include/ajazz/core/capabilities.hpp`      | 326-396     |
| `IMouseCapable` (dpi/poll/lod/btn/battery)    | `src/core/include/ajazz/core/capabilities.hpp`      | 398-498     |
| `IFirmwareCapable`                            | `src/core/include/ajazz/core/capabilities.hpp`      | 500-553     |
| `IClockCapable` + `TimeSyncResult`            | `src/core/include/ajazz/core/capabilities.hpp`      | 555-601     |
| `IDevice` + `DevicePtr = shared_ptr<IDevice>` | `src/core/include/ajazz/core/device.hpp`            | 100-201     |
| `DeviceDescriptor` + capability flags         | `src/core/include/ajazz/core/device.hpp`            | 45-72       |
| `DeviceRegistry::open` flyweight              | `src/core/include/ajazz/core/device_registry.hpp`   | 130-196     |
| `HotplugMonitor::injectEvent` test seam       | `src/core/include/ajazz/core/hotplug_monitor.hpp`   | 96-139      |
| Streamdock register (AKP03 family + 0x3004)   | `src/devices/streamdeck/src/register.cpp`           | 238-250     |
| Akp03Device backend (variant_3004 hosts here) | `src/devices/streamdeck/src/akp03.cpp`              | 274-521     |
| Keyboard register (AK980 PRO = 0x0c45:0x8009) | `src/devices/keyboard/src/register.cpp`             | 53-63       |
| ProprietaryKeyboard backend (ak980pro)        | `src/devices/keyboard/src/proprietary_keyboard.cpp` | 204-437     |
| Mouse register (2.4G 8K = 0x3151:0x5007)      | `src/devices/mouse/src/register.cpp`                | 67          |
| AjSeriesMouse backend                         | `src/devices/mouse/src/aj_series.cpp`               | 97-264      |
| TimeSyncService canonical consumer pattern    | `src/app/src/time_sync_service.{hpp,cpp}`           | cpp 149-179 |
| QML singleton registration pattern            | `src/app/src/application.cpp`                       | 188-204     |
| DeviceModel roles                             | `src/app/src/device_model.hpp`                      | 62-78       |
| Catalogue                                     | `docs/_data/devices.yaml`                           | 264-378     |
| Test fixture pattern                          | `tests/integration/fixtures/akp153/`                | —           |
| Unit test pattern (per-codename protocol)     | `tests/unit/test_akp03_protocol.cpp` etc.           | —           |
| Mock-seam pattern (`MockHidEnumerator`)       | `tests/unit/mock_hid_enumerator.hpp`                | —           |
| Test build wiring                             | `tests/unit/CMakeLists.txt`                         | 1-130       |

## Capability mix-in inventory

**Every advertised capability for the four connected devices already exists in
`capabilities.hpp`.** No new headers are required.

### Devices.yaml capability vocabulary → mix-in mapping

| devices.yaml key | Header interface                      | Status today                                                                                       |
| ---------------- | ------------------------------------- | -------------------------------------------------------------------------------------------------- |
| `display`        | `IDisplayCapable`                     | ✓ Header complete; stubbed-but-functional on AKP03/153/05                                          |
| `encoder`        | `IEncoderCapable`                     | ✓ Header complete; functional on AKP03/05                                                          |
| `clock`          | `IClockCapable`                       | ✓ Header complete; **all backends return `NotImplemented`** (v1.1 scaffolding)                     |
| `rgb`            | `IRgbCapable`                         | ✓ Header complete; functional on AjSeries + ProprietaryKb                                          |
| `macros`         | `IKeyRemappable::setMacro`            | ✓ Header method exists; functional on ProprietaryKeyboard                                          |
| `layers`         | `IKeyRemappable::setKeycode(layer,…)` | ✓ Header method exists; functional on ProprietaryKeyboard + ViaKeyboard                            |
| `dpi`            | `IMouseCapable::setDpiStage[s]`       | ✓ Header complete; functional on AjSeries (wire-format unconfirmed)                                |
| `touch`          | (no dedicated mix-in)                 | Surfaced via `DeviceDescriptor.hasTouchStrip` + `IDisplayCapable::setMainImage`; only AKP05 family |
| `firmware`       | `IFirmwareCapable`                    | ✓ Header complete; functional on AKP153, ProprietaryKb, ViaKb                                      |

**Conclusion:** v1.2 needs **zero new mix-in headers**.

The v1.1 retro of capabilities.hpp called for "no new `I*Capable` mix-in needed"
and it still holds. The work is replacing stubs with captures-driven wire
formats inside existing methods.

### Error-reporting pattern (canonical)

The mix-ins use three distinct error patterns today:

1. **`TimeSyncResult` enum sentinel** (`IClockCapable::setTime`) —
   `Ok | NotImplemented | IoError`. The `NotImplemented` value is **load-bearing**
   for the honest-UX rule (Pitfall: "Time synced toast on NotImplemented = lying
   success"). Use this pattern for any new mix-in or method where partial
   backend support is expected.

1. **`std::optional<T>` for queries** (`IMouseCapable::batteryPercent`) —
   absence-is-meaningful. Use for read paths where "wired device, no answer" is
   a valid state.

1. **Exception-throwing for invariants** (`IRgbCapable::setRgbStatic` throws
   `std::invalid_argument` on unknown zone name) — only for caller-side bugs,
   not for hardware fallbacks. Note this conflicts with the "no exceptions on
   the I/O thread" implicit rule — backends that need to surface I/O errors
   should adopt the `TimeSyncResult`-style sentinel.

**For v1.2 captures-driven additions:** when a captured wire format reveals a
new failure mode (e.g. AKP03 RTC write returns a NACK byte), prefer extending
the `TimeSyncResult`-style enum (add `BusyRetry`, `ReadOnly`, etc.) over
throwing. The QML/Settings layer is already wired for the enum (see
`time_sync_service.cpp:170-178`).

### Test-seam consideration

Existing `Akp03Device` already has a **second constructor accepting an injected
`TransportPtr`** (`src/devices/streamdeck/src/akp03.cpp:290-292` — comment
"Test constructor with injected transport (COD-026)"). Same pattern for
`ProprietaryKeyboard` (`proprietary_keyboard.cpp:217-219`) and `AjSeriesMouse`
(`aj_series.cpp:106-108`). v1.2 wire-format tests need **only a MockTransport
implementing `ITransport`** that captures `write()` calls and replays canned
`read()` responses. There is no MockTransport in `tests/` today — a thin one
(~80 LoC) is the first new test infrastructure file. Tracking entry:
`tests/unit/fixtures/mock_transport.hpp` (NEW).

## Integration points per connected device

### 1. AKP03 variant_3004 (Stream Dock 0x0300:0x3004, scaffolded → functional)

**Routes through:** `Akp03Device` factory `makeAkp03`
(`src/devices/streamdeck/src/akp03.cpp:532-534`).

**Inheritance graph today:**

```
Akp03Device : IDevice, IDisplayCapable, IEncoderCapable, IClockCapable
```

This **single class serves every AKP03 sibling** including `akp03`,
`akp03_legacy`, `akp03e`, `akp03r`, `akp03r_rev2`, all Mirabox N3 rebrands,
and **`akp03_variant_3004`**. There is no per-codename subclass — the
descriptor's `model` / `codename` field carries the SKU identity. v1.2 work
on the 0x3004 PID landing real wire formats **automatically applies to every
sibling**, modulo per-revision firmware quirks documented in `akp03.md`.

**Capability work for this codename (per devices.yaml: `[display, encoder, clock]`):**

| Capability | What's there today                                                                    | v1.2 task                                                                                                                                               |
| ---------- | ------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `display`  | `setKeyImage` writes pre-encoded JPEG bytes (akp03.cpp:414-424); `setKeyColor` no-ops | Materialise the image pipeline: resize source `rgba` to 60×60, encode JPEG, then call `sendImage`. `setKeyColor` synthesises a 1-pixel JPEG of `color`. |
| `encoder`  | Read path complete via `poll()` → `parseInputReport()` (akp03.cpp:328-398)            | Verify against real captures; possibly add decimation if 8KHz mouse-like reports show up. Currently no rate-limit; budget = 60fps QML refresh.          |
| `clock`    | `setTime` returns `NotImplemented` + WARN-once (akp03.cpp:474-480)                    | **Phase 9 captures-driven decision** (ARCH-05 candidate). If no RTC, leave `NotImplemented`; if RTC found, replace body with wire-format write.         |

**Wire-format constants already present** in `akp03_protocol.hpp:53-58`:
`DisplayKeyCount=6, KeyCount=9, EncoderCount=3, KeyWidthPx=60, KeyHeightPx=60`.
The 0x3004 variant inherits these (NOT 64×64 like `akp03r_rev2` v3 firmware) —
captures should confirm.

### 2. AK980 PRO (Keyboard 0x0c45:0x8009, scaffolded → functional/partial)

**Routes through:** `ProprietaryKeyboard` factory `makeProprietaryKeyboard`
(`src/devices/keyboard/src/proprietary_keyboard.cpp:441-443`).

**Inheritance graph today:**

```
ProprietaryKeyboard : IDevice, IKeyRemappable, IRgbCapable,
                      IFirmwareCapable, IClockCapable
```

**Critical context:** This is a **single shared keyboard impl** for the whole
"proprietary" family — not per-codename. `register.cpp:53-63` adds AK980 PRO
(0x0c45:0x8009) to the same factory. The current wire format is documented in
`docs/protocols/keyboard/proprietary.md` as "AK680, AK510, and similar" —
**not specifically AK980 PRO**. Phase 9 captures may reveal the AK980 PRO
needs a different command schema (Microdia chipset 0x0c45 vs SONiX 0x3151 used
by AK820 Pro VIA path), in which case **one of two patterns applies**:

- **Pattern A (small divergence):** Add per-codename branches inside the
  existing `ProprietaryKeyboard` methods (e.g. `if (m_descriptor.codename == "ak980pro") { … } else { … }`). Acceptable for one or two commands; gets
  unwieldy past three.
- **Pattern B (significant divergence):** Promote `ProprietaryKeyboard` into
  an abstract base class with a `makeAk980Pro` sibling. The factory in
  `register.cpp` is the dispatch point — no caller code changes. This is
  symmetric with how `makeAkp03` and `makeAkp05` are distinct factories
  despite sharing helper headers.

**Capability work for ak980pro (per devices.yaml: `[rgb, macros, layers, clock]`):**

| Capability | Status today                                                                                | v1.2 task                                                                                           |
| ---------- | ------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| `rgb`      | 3-zone (`keys`, `sides`, `logo`) static/effect/buffer/brightness functional (lines 337-401) | Verify zone names + LED counts against AK980 PRO captures; possibly add per-codename zone overrides |
| `macros`   | `setMacro` chunked upload functional (lines 316-330)                                        | Verify macro slot count and chunk encoding against AK980 PRO firmware                               |
| `layers`   | `setKeycode(layer,row,col,kc)` functional; layout 6×17×4 (line 298)                         | Verify layout dims and `MaxLayers` match AK980 PRO physical keymatrix                               |
| `clock`    | `setTime` returns `NotImplemented` + WARN-once (lines 423-429)                              | Phase 9 captures-driven — likely no RTC on wireless mech keyboard                                   |

**Inter-capability dependency for AK980 PRO:** `layers` shares the per-layer
RGB zone state with `rgb` (layer-indicator backlight typically lights one
zone). Sequence: `rgb` first → `layers` second → revisit `rgb` for layer-aware
extension if captures reveal coupling. This is a soft ordering, not a hard
dependency.

### 3. AJAZZ 2.4G 8K (Mouse 0x3151:0x5007, scaffolded → functional)

**Routes through:** `AjSeriesMouse` factory `makeAjSeries`
(`src/devices/mouse/src/aj_series.cpp:268-270`).

**Inheritance graph today:**

```
AjSeriesMouse : IDevice, IMouseCapable, IRgbCapable
```

**Critical context:** This is a **single shared mouse impl** for the whole
"AJ-series" family. The 2.4G 8K shares the backend with `aj_series_wired_primary`
through `aj199_family_dongle`. The `register.cpp:62-67` comment is explicit:
"Wire format reuses the AJ-series backend; configuration writes may no-op until
reconciled (same caveat as AJ-series, see file header @warning)." Per
`docs/research/vendor-protocol-notes.md` Finding 11.A, this caveat means the
wire format on the wire today **does not match what the vendor driver sends**.

**Capability work for ajazz_24g_8k (per devices.yaml: `[dpi, rgb]`):**

| Capability | Status today                                                        | v1.2 task                                                                                                     |
| ---------- | ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| `dpi`      | 6 stages with checksummed 64-byte feature reports (lines 137-167)   | Capture real vendor-driver writes and reconcile envelope structure (Finding 11.A: report-id, sub-cmd, layout) |
| `rgb`      | 2 zones (`logo`, `scroll`) static/effect/brightness (lines 206-228) | Capture; the SONiX chip on 0x3151 prefix may use a totally different RGB schema vs the 248A/3554 family       |

**Critical risk:** The 8KHz polling distinguishes the SKU. If the vendor driver
uses a separate "report rate" command for the 8KHz path, it likely sits behind
`setPollRateHz` but the current impl writes `kCmdPollRate = 0x22` which may
not be the right command id on the SONiX chip. Phase 9 captures are
mandatory here — there is no "verify against existing OSS corpus" path
because the AJ-series wire format has no public OSS reverse-engineering
corpus (`docs/research/vendor-protocol-notes.md` is the only source).

### 4. Unknown PID 0x0c45:0x7016

**Status:** NOT in `devices.yaml`, NOT in any `register.cpp`. VID 0x0c45 is the
Microdia chipset prefix already used by AK980 PRO (0x0c45:0x8009). This makes
two scenarios likely:

- **Scenario A:** This is **another Microdia-chipset AJAZZ keyboard** —
  v1.2 work is to identify the SKU (AK680 / AK820 Max / unreleased?),
  add a `devices.yaml` entry, and register it in `keyboard/src/register.cpp`
  with `&makeProprietaryKeyboard`. **No new backend** needed.
- **Scenario B:** This is a **secondary HID interface of AK980 PRO** (a
  composite-USB device exposing multiple HID endpoints — common for keyboards
  with separate config / input / consumer-control interfaces). In this case
  it should NOT be registered as a separate device — instead, the
  `DeviceRegistry::open` flyweight needs to deduplicate. This is an **ARCH-06
  candidate**: the registry slot key today is `(vid, pid)` which means two
  HID interfaces of the same physical composite USB device would be opened
  twice (and one of those `hid_open` calls will fail with `EBUSY` on
  Linux).

**Capture mandate for Phase 9:** the very first capture deliverable for
0x0c45:0x7016 is `udevadm info --query=all --name=/dev/hidraw*` and `lsusb -v`
to determine which scenario applies. The ambiguity cannot be resolved from
the PID alone.

## Capability mix-in re-design (NOT needed for v1.2)

The milestone_context asks whether new mix-ins like `IDisplayCapable`,
`IRgbCapable`, etc. need to be created. They do not — **all of them already
exist in `capabilities.hpp`** and are linked into every backend class. The
research question is moot for v1.2. The interface contracts are:

- **`IDisplayCapable`** (8 methods): `displayInfo`, `setKeyImage`,
  `setKeyColor`, `clearKey`, `setMainImage`, `setBrightness`, `flush`.
- **`IEncoderCapable`** (2 methods): `encoderInfo`, `setEncoderImage`. Encoder
  *events* travel through the standard `IDevice` event callback, not via a
  per-encoder `Q_SIGNAL` on the mix-in (see Akp03 `poll()` →
  `DeviceEvent::Kind::EncoderTurned`).
- **`IRgbCapable`** (5 methods): `rgbZones`, `setRgbStatic`, `setRgbEffect`,
  `setRgbBuffer`, `setRgbBrightness`.
- **`IKeyRemappable`** (5 methods incl. `setMacro`): covers both "macros" and
  "layers" devices.yaml capability keys. The "layers" capability advertised by
  AK980 PRO is `setKeycode(layer, row, col, kc)` — no separate `ILayersCapable`.
- **`IMouseCapable`** (10 methods): covers "dpi", "pollRate", LOD, button
  binding, battery — single mix-in for all mouse configuration.
- **`IFirmwareCapable`** (3 methods).
- **`IClockCapable`** (1 method): the v1.1 scaffolding.

**Naming convention conclusion:** the codebase uses `I*Capable` for
**method-bundle interfaces** (display, rgb, encoder, mouse, firmware, key
remap, clock). The devices.yaml capability keys are higher-level user-facing
groupings; the 1:1 mapping in the table above is the authoritative cross-walk.

## Data flow per capability

### Image push (display capability) — AKP03 variant_3004

```
[User selects key 3, drops a PNG file in KeyDesigner.qml]
  ↓ ImagePainter renders to QImage in QML
  ↓ TODO ARCH-04 (Phase 9 decision): where does resize+JPEG-encode live?
  ↓ Two options:
  ↓   Option A: ProfileController synthesises bytes (Qt-side), passes via
  ↓             Q_INVOKABLE setKeyImage(codename, keyIdx, QByteArray jpeg)
  ↓   Option B: keep raw RGBA8 byte path; backend resizes + encodes
  ↓             (currently planned per IDisplayCapable doc lines 119-152
  ↓              but Akp03 ducks this — TODO at akp03.cpp:418-424)
ProfileController::pushKeyImage(codename, keyIdx, …)   [NEW Q_INVOKABLE]
  ↓
DeviceRegistry::open(deviceId) → shared_ptr<IDevice>   [existing flyweight]
  ↓
dynamic_cast<IDisplayCapable*>(dev.get()) + null check  [Pitfall 2 pattern]
  ↓ hold dev as local for the entire push (A-04 / D-01 UAF window close)
disp->setKeyImage(keyIdx, rgba, w, h)                  [existing method]
  ↓ inside Akp03Device:
sendImage(keyIndex, jpeg_bytes)                        [existing helper, line 492]
  ↓ buildImageHeader → ITransport::write (512B header)
  ↓ chunked ITransport::write (512B payload chunks)
[hid_write to /dev/hidraw* on Linux]
```

**Memory ownership across QML→C++:** Qt's QImage refcounts on copy; passing
through `Q_INVOKABLE` with `QByteArray` value parameter is a copy that survives
the call boundary. The shared_ptr<IDevice> held in the local closes the UAF
window on the device side. No raw pointer crosses the boundary.

**Latency budget:** AKP03 6-key full refresh = 6 keys × (1 header + 5-10 chunks)
× 512B writes ≈ ~30 writes × ~1ms hidraw latency = ~30ms. Acceptable for a
non-realtime UX (profile switch). The 60fps animation budget is **out of
reach today** and not part of v1.2 scope.

### Encoder events — AKP03

```
[User rotates encoder 0 clockwise]
  ↓ Hardware emits HID input report with byte 9 = 0x91 (Encoder0Cw)
HotplugMonitor's poll loop OR Akp03Device::poll() (already polling at 60Hz
  from QtTimer in profile_controller / KeyDesigner)
  ↓ akp03::parseInputReport → InputEvent{EncoderTurned, idx=0, delta=+1}
DeviceEvent{Kind::EncoderTurned, index=0, value=+1}
  ↓ EventCallback (registered via IDevice::onEvent)
  ↓ called from I/O thread — already-existing thread-affinity rules apply
QML observer in DeviceCard/KeyDesigner (via Q_SIGNAL emitted on GUI thread
  by ProfileController)
```

**Decimation/coalescing for fast rotation:** Not currently implemented. The
HID stack already throttles to a reasonable rate (typical encoder = ~50
detents/sec max from a human hand; firmware reports at HID poll rate). For
v1.2 leave coalescing out unless a Phase 9 capture shows a degenerate stream.
If needed, the canonical location is the `EventCallback` adapter in
`profile_controller.cpp` (NOT inside the backend `poll()` — keep backends as
thin parsers).

**Latency budget:** ~5ms HID → ~1ms Qt event loop → QML render. Well within
the "feels instant" 100ms budget. No optimisation work for v1.2.

### Time-sync push — canonical pattern (v1.1 reference)

This is the existing pattern, **mirrored by every other one-shot push**:

```
[User clicks "Sync now" in DeviceRow.qml]
  ↓ QML signal → TimeSyncService::setSystemTimeOn(codename)
QString reason = doPush(codename)               [time_sync_service.cpp:149-179]
  ↓
shared_ptr<IDevice> dev = m_lookup(codename);   [closes the UAF window]
  ↓
auto* clk = dynamic_cast<IClockCapable*>(dev.get());
  ↓ null-check within 3 lines (Pitfall 2)
TimeSyncResult result = clk->setTime(now);
  ↓
switch on result → reason text or empty
  ↓
emit syncSucceeded(codename) OR syncFailed(codename, reason)
  ↓ QML toast / glyph reads signals
```

**v1.2 implementations of RGB / Macros / Layers / DPI follow this skeleton
exactly.** The pattern is replicated by:

1. Adding a `Q_INVOKABLE` method to the appropriate controller
   (`ProfileController` for stream-deck/keyboard state, possibly a new
   `MouseTuningService` for the mouse — see ARCH-06 candidate notes below).
1. Implementing `doPushX(codename, args)` that follows the time_sync_service
   pattern: lookup → dynamic_cast → null-check → call → sentinel-or-throw →
   emit signals.

### Auto-sync hook (Hotplug → service) — v1.2 generalisation candidate

The hotplug → service hook in `application.cpp:243-262` is specific to
`TimeSyncService::onDeviceArrivedDebounced`. If v1.2 introduces other
arrival-triggered actions (e.g. "restore last profile" on a known device), the
hook becomes a multi-listener fan-out. Today's wire is a single-listener
direct call. Tracking entry: defer until a second consumer surfaces; the
current `for d in descriptors { if match ev.vid/pid → callback }` loop scales
linearly per consumer and is fine at N=2-3 consumers.

## NEW vs MODIFIED file lists

### New files (per v1.2 scope)

| Type                           | Path                                                   | Purpose                                                                                                                                  |
| ------------------------------ | ------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Test fixture                   | `tests/unit/fixtures/mock_transport.hpp` (NEW)         | Thin ITransport mock that captures `write` calls + replays canned `read` responses. ~80 LoC. Pre-condition for every capability test.    |
| Test fixture dir               | `tests/integration/fixtures/akp03_variant_3004/` (NEW) | hex-format capture fixtures per the `akp153/` precedent.                                                                                 |
| Test fixture dir               | `tests/integration/fixtures/ak980pro/` (NEW)           | hex-format capture fixtures.                                                                                                             |
| Test fixture dir               | `tests/integration/fixtures/ajazz_24g_8k/` (NEW)       | hex-format capture fixtures.                                                                                                             |
| Test fixture dir               | `tests/integration/fixtures/0c45_7016/` (NEW)          | Identification capture (`lsusb -v`, `udevadm`), then either deletion (Scenario B = secondary interface) or wire-format hex (Scenario A). |
| Test (unit)                    | `tests/unit/test_akp03_image_pipeline.cpp` (NEW)       | Drives Akp03Device with MockTransport asserting resize/JPEG-encode byte output for setKeyImage(rgba, w, h).                              |
| Test (unit)                    | `tests/unit/test_ak980pro_capabilities.cpp` (NEW)      | Drives ProprietaryKeyboard with MockTransport asserting AK980-specific overrides (if any per-codename branch lands).                     |
| Test (unit)                    | `tests/unit/test_ajazz_24g_8k_capabilities.cpp` (NEW)  | Drives AjSeriesMouse with MockTransport asserting reconciled wire format for 0x3151:0x5007.                                              |
| Test (integration)             | `tests/integration/test_capture_replay_v1_2.cpp` (NEW) | Add per-device hex-fixture-driven cases extending `test_capture_replay.cpp` precedent.                                                   |
| Possible (Scenario A only)     | `docs/protocols/keyboard/ak980pro.md` (NEW)            | If AK980 PRO needs its own protocol doc.                                                                                                 |
| Possible (Scenario A only)     | `docs/protocols/mouse/ajazz_24g_8k.md` (NEW)           | If 2.4G 8K needs its own protocol doc; otherwise extend `aj_series.md`.                                                                  |
| Possible (Scenario B, ARCH-06) | (NEW infrastructure for composite-HID dedup)           | If 0x0c45:0x7016 is a secondary interface of AK980 PRO — see ARCH-06 candidate below.                                                    |

### Modified files (per v1.2 scope)

| Path                                                         | Change                                                                                                                                                                                            |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/devices/streamdeck/src/akp03.cpp:414-424`               | Replace `setKeyImage` "caller passes pre-encoded PNG" stub with real resize + JPEG-encode pipeline. Implement `setKeyColor` synthesised 1-pixel JPEG. Implement `flush` STP if not already wired. |
| `src/devices/streamdeck/src/akp03.cpp:474-480`               | Phase 9 captures-driven: replace `setTime → NotImplemented` body with real wire format **OR** keep as scaffolded. **Decision: ARCH-05.**                                                          |
| `src/devices/keyboard/src/proprietary_keyboard.cpp:316-401`  | Verify AK980-specific zone-name / LED-count / macro-encoding against captures; possibly inject per-codename branches OR promote to abstract base + ak980pro factory (Pattern B above).            |
| `src/devices/keyboard/src/proprietary_keyboard.cpp:423-429`  | Phase 9 captures-driven: AK980 PRO `setTime` body. Likely no RTC on wireless mech kb — leave `NotImplemented`.                                                                                    |
| `src/devices/keyboard/src/proprietary_keyboard.cpp:298`      | Verify `KeyboardLayout{rows=6, cols=17, layers=MaxLayers}` matches AK980 PRO physical key matrix.                                                                                                 |
| `src/devices/mouse/src/aj_series.cpp:137-228`                | Capture-driven wire-format reconciliation per `vendor-protocol-notes.md` Finding 11.A. Verify 64-byte envelope structure; possibly distinguish 8KHz path with new sub-command discriminator.      |
| `src/devices/mouse/src/register.cpp:67`                      | Add `.hasClock = false` (no-op — default). If wire-format reconciliation reveals 2.4G 8K is structurally different from AJ-series, **split the factory** into `makeAjazz24g8k`.                   |
| `src/devices/keyboard/src/register.cpp` (possibly NEW lines) | If Scenario A: add the 0x0c45:0x7016 descriptor + factory wire. If Scenario B: leave unchanged.                                                                                                   |
| `docs/_data/devices.yaml:264-378`                            | Promote maturity for connected devices: `scaffolded → functional` (or `partial` per capture coverage). Add 0x0c45:0x7016 entry if Scenario A.                                                     |
| `resources/linux/99-ajazz.rules`                             | Possibly add `idVendor=="0c45"` `idProduct=="7016"` rule if Scenario A and a fresh VID prefix is needed (0x0c45 prefix already present — check).                                                  |
| `tests/unit/CMakeLists.txt` lines 1-80                       | Add the new per-codename test sources to `ajazz_unit_tests` executable.                                                                                                                           |
| `tests/integration/CMakeLists.txt`                           | Add per-device fixture-export glob if necessary.                                                                                                                                                  |

**COD-031 boundary check for all modifications:**

- Akp03Device, ProprietaryKeyboard, AjSeriesMouse all live under
  `src/devices/{streamdeck,keyboard,mouse}/src/`, which already INCLUDES
  `ajazz_core` only and does NOT link `nlohmann::json`. JPEG/PNG encoding
  must use a non-nlohmann library — ARCH-04 (Phase 9) decides which
  (libjpeg-turbo, stb_image_write single-header, or Qt's QImage::save going
  via the app layer).

## ARCH-NN candidate decisions (for Phase 9 ratification)

### ARCH-04 — Image-encoding pipeline location

**Question:** Where do `setKeyImage` and `setKeyColor` resize + JPEG-encode
their inputs?

**Why ratify in Phase 9:** Today `Akp03Device::setKeyImage`'s comment is
explicit: "The caller is expected to pass already-PNG-encoded bytes for now.
When the image pipeline lands (phase 2) this method will resize to 72×72 and
encode to PNG itself." This is a deferred decision that Phase 9 must close
before image-push work begins.

**Three sub-options:**

- **A. In the backend `.cpp`:** Each backend embeds its own encoder. Matches
  the `IDisplayCapable` interface doc. Requires picking a non-nlohmann
  image-encoding lib that's PRIVATE-linked to `ajazz_streamdeck` only —
  candidates: stb_image_write (single-header, public domain), libjpeg-turbo
  (mature, fast, larger). Both work without crossing the COD-031 boundary.
- **B. In a new `ajazz_imaging` static lib:** Centralise resize+encode so
  AKP153 / AKP03 / AKP05 / AKP815 / AK980 PRO all use the same code. New
  CMakeLists subdir under `src/imaging/`. Adds one library; reduces backend
  bloat. Could PRIVATE-link to `Qt6::Gui` for QImage.
- **C. In QML/Qt-side controller:** Push the resize+encode out to
  `ProfileController` or a new helper that uses `QImage::save(QBuffer*, "JPEG", quality)`. Backend `setKeyImage` keeps the "caller passes encoded
  bytes" contract honestly. Easiest for v1.2; smallest core-lib delta.

**Roadmapper guidance:** lean toward Option **C** for v1.2 (smallest blast
radius, no new core dep, Qt is already a build dep) with a roadmap note that
B is the right long-term shape if AKP815's 800×480 strip image upload demands
a heavier pipeline.

### ARCH-05 — AKP03 family `setTime` wire format

**Question:** Does the AKP03 family expose a host-settable RTC? If yes, the
`Out of Scope` row in PROJECT.md for "Real `IClockCapable::setTime` wire
formats" is **invalidated**.

**Why ratify in Phase 9:** Captures-driven; binary outcome.

**Decision criteria:** A captured wire format that the vendor software
demonstrates pushing a date/time value to the AKP03 → ratify ARCH-05 +
remove the Out of Scope row + replace the `NotImplemented` stub. Otherwise
keep `NotImplemented` and document the firmware constraint in
`docs/protocols/streamdeck/akp03.md`.

### ARCH-06 — Composite-HID handling (conditional on 0x0c45:0x7016 Scenario B)

**Question:** If 0x0c45:0x7016 turns out to be a secondary HID interface of
the AK980 PRO (composite USB device), how does `DeviceRegistry` deduplicate so
both interfaces don't open two `hid_open` handles to the same physical device?

**Why ratify in Phase 9 (conditional):** Only fires if the 0x0c45:0x7016
identification capture reveals it's a sibling interface, not a separate
device. The decision shape:

- **Sub-decision 1:** Registry slot key. Today: `(vid, pid)`. Composite needs
  one of: `(vid, pid, interface_number)`, OR a `interface_number=0`-only
  filter applied at enumeration time, OR a `primary_interface_vid_pid`
  override in `DeviceDescriptor` so secondary interfaces are silently dropped
  during `hid_enumerate` walks.
- **Sub-decision 2:** UI deduplication. Today: `DeviceModel` reads
  `DeviceRegistry::enumerate()` which yields one row per registered
  `(vid, pid)`. If both 0x8009 and 0x7016 register, the user sees two rows.
  Need to filter at the model layer.

**Decision criteria:** Land ARCH-06 only if real captures prove composite
behaviour for 0x7016. Otherwise treat 0x7016 as a separate device (Scenario A),
which requires zero registry changes — just adding a row to `register.cpp`.

## Test strategy

### New test infrastructure (one-time, Phase 9 or earlier)

`tests/unit/fixtures/mock_transport.hpp` — `class MockTransport : public ajazz::core::ITransport`. Captures every `write` / `writeFeature` call into
a `std::vector<std::vector<uint8_t>>`; replays canned `read` / `readFeature`
responses from a configurable queue. Pattern parallels
`mock_hid_enumerator.hpp`. Approximately 80 LoC + doc comments.

### Per-codename test pattern

For each device codename promoted in v1.2, add:

1. **`tests/unit/test_<codename>_capabilities.cpp`** — Drives the backend
   class with `MockTransport`. Asserts wire-format byte sequences against
   captured ground truth. Example test cases:

   - `"akp03_variant_3004 setKeyImage encodes 60x60 JPEG at offset 12"`
   - `"akp03_variant_3004 setBrightness clamps at 100 (byte 10)"`
   - `"ak980pro setRgbStatic on logo zone emits CmdSetRgbStatic with zone=0x02"`
   - `"ajazz_24g_8k setDpiStage 0 emits envelope with checksum at byte 63"`

1. **`tests/integration/fixtures/<codename>/<event>.hex`** — Real-hardware
   captures stored as hex-format files matching the `akp153/key_press_07.hex`
   precedent.

1. **`tests/integration/test_capture_replay_v1_2.cpp`** — One TEST_CASE per
   fixture file; calls `loadHexFixture` + `parseInputReport` + assertions.

### ctest target additions

The existing `ajazz_unit_tests` and `ajazz_integration_tests` executables
absorb the new per-codename `.cpp` files via the existing
`add_executable(...)` and `target_sources(...)` wires in
`tests/{unit,integration}/CMakeLists.txt`. **No new test executable is
needed.** `catch_discover_tests` picks up new `TEST_CASE` macros
automatically. ctest invocation stays `ctest --preset linux-release`.

### Cross-platform considerations

- **`hidapi_hidraw` Linux-only:** Yes, on Linux. On Windows the keyboard /
  mouse backends use the same `ITransport` interface backed by
  `hidapi_winhid`; on macOS, `hidapi_iohid`. The **backend `.cpp` files are
  identical across platforms** — only the transport implementation differs.
  Wire-format tests using `MockTransport` are **platform-agnostic** by
  construction. The existing Win32 / macOS CI matrix at
  `.github/workflows/ci.yml` runs the same Catch2 binary unchanged.
- **What's NOT cross-platform:** Real-hardware integration tests obviously
  need a real device on the runner. Use the existing `[unit]` /
  `[integration]` Catch2 tag separation — wire-format byte-level tests run
  on every platform; capture-replay tests run on every platform from
  hex-fixtures; live-device smoke is opt-in via a Linux self-hosted runner
  (out of scope for v1.2 CI scope).

### Build-time grep gates (preserve invariants)

Existing v1.1 CI grep gates that v1.2 must not regress:

- `grep -rn nlohmann src/core/include/ src/core/src/` → 0 hits
  (COD-031 boundary).
- `grep -rn 'hid_open\(' src/core/ src/devices/ --include='*.hpp'` →
  must be empty in public headers (D-06 zombie contract — only ITransport
  surface in headers).

v1.2 should add:

- `grep -rn 'IDisplayCapable\|IRgbCapable\|IEncoderCapable\|IClockCapable\|IKeyRemappable\|IMouseCapable\|IFirmwareCapable' src/app/qml/ → 0 hits`
  (capability mix-ins live in `ajazz_core`, never referenced from QML — QML
  only sees the QObject service layer).

## Anti-patterns to avoid

### Anti-pattern A: Inventing a new mix-in per device

**Trap:** Reading the milestone_context "Naming: `IDisplayCapable`,
`IEncoderCapable`, …" and concluding new headers need creating.

**Reality:** Every mix-in already exists. The work is replacing stub bodies
in existing `.cpp` files. Search `capabilities.hpp` before drafting any new
header.

### Anti-pattern B: Per-codename branches inside a shared backend class without an exit ramp

**Trap:** When AK980 PRO captures reveal one or two minor command-byte
differences from the existing `proprietary.md` spec, paper over with
`if (codename == "ak980pro") …` inside every method.

**Reality:** Acceptable for one or two divergences; revisit after three. The
exit ramp is to promote `ProprietaryKeyboard` into a base class + sibling
factory `makeAk980Pro` (Pattern B above), preserving the
`shared_ptr<IDevice>` registry contract.

### Anti-pattern C: Letting a `NotImplemented` become a lie

**Trap:** Partial wire-format implementation lands but doesn't survive every
input. Returns `Ok` on the happy path; throws or hangs on edge cases.

**Reality:** The `TimeSyncResult` precedent is HONEST. If you can't guarantee
the wire-format write succeeds on captured inputs, return `IoError` (or
extend the enum with `BusyRetry` / similar) — never `Ok`. The Pitfall: "Time
synced toast on NotImplemented" / "lying success UX" is project-wide.

### Anti-pattern D: Renumbering Capability bits

**Trap:** Adding a new capability somewhere "more logical" in the enum.

**Reality:** `capabilities.hpp:57` has a `static_assert(static_cast<unsigned>(Capability::Clock) == (1u << 15))` lock. Pitfall 13: never renumber capability bits; only append. v1.2 work is unlikely to add new bits at all (every needed capability has a bit) — but if it does, the new bit goes at the bottom.

### Anti-pattern E: Storing `shared_ptr<IDevice>` across an event-loop turn without re-resolving capability

**Trap:** Phase 5 A-04 amendment 3 made `DeviceLookup` return a
`shared_ptr<IDevice>`. New v1.2 code may be tempted to **cache** that pointer
in a service member to avoid the codename → DeviceId → registry.open
roundtrip on every UI action.

**Reality:** The shared_ptr keeps the IDevice alive, but the **device on the
wire** may have disappeared. The zombie contract (D-06 / ARCH-03) requires
every backend method to gate on `m_transport->isOpen()` — but the
**capability** advertisement also matters: a device that re-enumerates may
have different firmware quirks. Always re-resolve via `m_registry.open(id)`
on each interaction; the registry's `weak_ptr` flyweight makes this O(1)
and returns the SAME instance if it's still alive.

## Build order suggestion for the Roadmapper

The four connected devices have **no cross-device implementation
dependencies**. Their backends are in different libraries, their factories
are independent, their wire-format helpers are independent. The Roadmapper
should treat them as a fan-out **and** group them by capability-vs-device
based on these trade-offs:

### Option A — Device-clustered phases (RECOMMENDED)

- Phase 9: Research (this milestone; 4 parallel researchers)
- Phase 10: AKP03 variant_3004 promotion (display + encoder + clock decision)
- Phase 11: AK980 PRO promotion (rgb + macros + layers + clock decision)
- Phase 12: AJAZZ 2.4G 8K promotion (dpi + rgb)
- Phase 13: 0c45:7016 identification + integration (Scenario A | Scenario B)
- Phase 14: Back-fill visual UI verifies + maturity table promotion

**Why device-clustered wins:**

1. **Each device's wire format is opaque to the others.** No cross-device
   knowledge accelerates Phase 11 if Phase 10 lands first.
1. **A single device's capabilities are tightly coupled in the same backend
   `.cpp` file.** AK980 PRO `rgb` and `macros` are both inside
   `ProprietaryKeyboard` — splitting them into separate phases creates
   atomic-commit headaches (one phase touches lines 337-401 of
   `proprietary_keyboard.cpp`, the next touches lines 316-330; both want to
   re-run the same test_proprietary_keyboard_protocol.cpp).
1. **Risk-per-phase is per-device, not per-capability.** Failure to capture
   AK980 PRO's wire format doesn't block AKP03 work or AjSeries work.
1. **Each device-phase has a natural acceptance gate:** "`devices.yaml`
   `maturity: scaffolded → functional` for this codename" is a single,
   binary, testable condition.

### Option B — Capability-clustered phases (NOT recommended)

- Phase 10: all-device display work
- Phase 11: all-device rgb work
- Phase 12: all-device encoder work
- …

**Why this loses:** Forces synchronous capture-availability across all
devices for each capability. AK980 PRO RGB requires `vid_0c45 captures`;
AjSeries RGB requires `vid_3151 captures`; AKP03 display requires `vid_0300 captures`. Phase scheduling becomes a Gantt of capture session calendars
instead of a parallel fan-out.

### Phase ordering rationale

If the Roadmapper proposes 4 device-clustered phases after Phase 9 research,
order them by **decreasing capture-availability confidence**:

1. **AKP03 variant_3004 first** — Best OSS corpus (`mishamyrt/ajazz-sdk`,
   `4ndv/opendeck-akp03`, `naerschhersch/opendeck-akp05`). Existing Akp03
   backend most mature. Lowest research risk.
1. **AJAZZ 2.4G 8K second** — Zero OSS corpus, but existing AjSeries impl
   gives a starting envelope. High capture-driven risk; gate maturity tier
   on what the captures reveal.
1. **AK980 PRO third** — Microdia chipset 0x0c45 is poorly documented;
   captures will need cross-reference vs the vendor's closed Windows
   utility. Highest research risk.
1. **0x0c45:0x7016 fourth** — Identification first; integration shape
   depends on Scenario A vs B (could be a 30-minute `register.cpp` PR or a
   weeks-long composite-HID architecture decision).

## Sources

All claims verified against working-tree HEAD (`main` branch as of
2026-05-15, commit `2492433` "chore(planning): enable Nyquist validation in
GSD workflow config" or later).

- `src/core/include/ajazz/core/capabilities.hpp` (lines 31-601 — all
  mix-ins)
- `src/core/include/ajazz/core/device.hpp` (lines 45-201 — DeviceDescriptor +
  IDevice + DevicePtr/DeviceFactory)
- `src/core/include/ajazz/core/device_registry.hpp` (lines 130-196 — open()
  flyweight + ARCH-02 HidEnumerator seam)
- `src/core/include/ajazz/core/hotplug_monitor.hpp` (lines 96-139 —
  AJAZZ_TESTING-gated injectEvent / parseDevicePathW)
- `src/core/include/ajazz/core/transport.hpp` (lines 46-118 — ITransport)
- `src/core/include/ajazz/core/hid_transport.hpp` (lines 39-40 —
  makeHidTransport factory)
- `src/devices/streamdeck/src/register.cpp` (lines 238-250 — akp03_variant_3004
  registration)
- `src/devices/streamdeck/src/akp03.cpp` (lines 274-521 — Akp03Device class)
- `src/devices/streamdeck/src/akp03_protocol.hpp` (lines 53-58 — wire-format
  constants)
- `src/devices/keyboard/src/register.cpp` (lines 53-63 — ak980pro
  registration)
- `src/devices/keyboard/src/proprietary_keyboard.cpp` (lines 204-437 —
  ProprietaryKeyboard class)
- `src/devices/mouse/src/register.cpp` (line 67 — ajazz_24g_8k registration)
- `src/devices/mouse/src/aj_series.cpp` (lines 97-264 — AjSeriesMouse class)
- `src/app/src/application.cpp` (lines 71-262 — TimeSyncService construction,
  QML registration, hotplug → service wire)
- `src/app/src/time_sync_service.{hpp,cpp}` (canonical mix-in consumer
  pattern; cpp lines 149-179)
- `src/app/src/device_model.hpp` (lines 62-78 — Roles enum incl. HasClockRole
  and MaturityRole)
- `docs/_data/devices.yaml` (lines 264-378 — connected-device entries +
  capabilities)
- `tests/unit/CMakeLists.txt` (lines 1-130 — ajazz_unit_tests sources)
- `tests/integration/test_capture_replay.cpp` (lines 1-85 — hex-fixture
  pattern)
- `tests/unit/test_hotplug_harness.cpp` (lines 1-90 — ARCH-02 mock seam
  consumption)
- `tests/unit/test_time_sync_service.cpp` (lines 1-100 — canonical IDevice
  mock + capability mix-in test pattern)
- `tests/unit/test_proprietary_keyboard_protocol.cpp` (lines 1-80 —
  per-codename protocol unit test pattern)
- `CMakeLists.txt` (lines 86-98 — nlohmann FetchContent + PRIVATE-link rule)
- `.planning/PROJECT.md` (Out of Scope row for setTime wire formats; Key
  Decisions for ARCH-01..03)
- `.planning/MILESTONES.md` (v1.1 deferred items)
- `.planning/milestones/v1.1-research/ARCHITECTURE.md` (v1.1 architecture
  research — every architectural claim there is reverified at HEAD)

______________________________________________________________________

*Architecture research for: AJAZZ Control Center v1.2 — Connected-Device
Capability Parity*
*Researched: 2026-05-15*
