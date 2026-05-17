# Clean reimplementation roadmap — v1.2.x + v1.3+ derived from vendor RE

**Sources**: 3 vendor RE deep-dive docs landed 2026-05-17 —
[`ak980pro_vendor.md`](../protocols/keyboard/ak980pro_vendor.md) (844 lines,
Ghidra 12.1 on `DeviceDriver.exe`),
[`aj_series_vendor.md`](../protocols/mouse/aj_series_vendor.md) (666 lines,
Electron+Rust gRPC reversal of `AJAZZ Driver(R) 2.1.94`),
[`akp05_vendor.md`](../protocols/streamdeck/akp05_vendor.md) (532 lines,
Ghidra 12.1 with full PDB on `SDLibrary1.dll` 13 MB +
`Stream Dock AJAZZ.exe` 42 MB).
Plus our existing backends:
[`src/devices/streamdeck/src/`](../../src/devices/streamdeck/src/),
[`src/devices/keyboard/src/`](../../src/devices/keyboard/src/),
[`src/devices/mouse/src/`](../../src/devices/mouse/src/),
[`src/core/include/ajazz/core/capabilities.hpp`](../../src/core/include/ajazz/core/capabilities.hpp).

**Scope**: maps every vendor feature → our backend status → modern Qt6
re-implementation recommendation, prioritised by user value + implementation
complexity. Honesty contract: "implemented" = wire format real AND tested;
"scaffolded" = wire format real but untested; "stub" = `NotImplemented`.

______________________________________________________________________

## 0. Executive summary

The 2026-05-17 vendor RE pass uncovered three big-picture truths:

1. **Our `aj_series.cpp` mouse backend is wire-incompatible with the AJ159
   APEX** (and almost certainly with `ajazz_24g_8k`). Nearly every opcode is
   wrong, the checksum mode is wrong, and our "commit" opcode (`0x50`) is
   actually the vendor's `FEA_CMD_MOUSE_SET_KEYMATRIX` — meaning every
   "save" we issue silently corrupts button slot 0. **Mouse is the P0
   safety fire to land first.**
1. **Stream Dock AJAZZ ships an Elgato Stream Deck v6-compatible plugin
   server.** The SDK uses verbatim Elgato API names
   (`connectElgatoStreamDeckSocket`, `setImage`, `keyDown`, `setTitle`, …)
   which means a clean implementation in our QML host lets thousands of
   existing `.sdPlugin` packages drop in. This is the single biggest
   leverage point we have for ecosystem.
1. **AK980 PRO has an auto-profile-switch-by-foreground-app feature** baked
   into its SQLite schema (`t_profile_data.app TEXT` column). Combined
   with 8 profiles on the mouse and 20 macros, this gives us a cross-device
   profile/macro/foreground-app system as a v1.3 killer feature.

Phase 9.x captures are no longer blocking: the byte-level RE evidence is
stronger than what live USB captures would yield for these questions, so
Phase 10+ can start once §11 lands.

______________________________________________________________________

## 1. Critical safety fixes (P0 — land in next commit batch)

### 1.1 `aj_series.cpp kCmdCommit = 0x50` is `FEA_CMD_MOUSE_SET_KEYMATRIX`

- **Severity**: silent data corruption (button binding) on every "save"
- **Root cause**: [`aj_series_vendor.md` line 389 + lines 397-402](../protocols/mouse/aj_series_vendor.md):
  > opcode 0x50 is `FEA_CMD_MOUSE_SET_KEYMATRIX` in vendor speak. Every
  > time our backend "commits" today, it's actually issuing a key-matrix
  > write with garbage payload to whatever button index happens to be in
  > byte 2 of the empty envelope.

  Our code: [`aj_series.cpp:48`](../../src/devices/mouse/src/aj_series.cpp)
  `kCmdCommit = 0x50` — called from every `setDpiStages` / `setDpiStage` at
  lines 145 + 157.
- **Fix**: in the same commit:
  1. Remove the `commit()` helper — vendor has **no** commit step; writes
     persist immediately ([vendor doc line 410](../protocols/mouse/aj_series_vendor.md)).
  1. Remove every `commit()` call site in `aj_series.cpp`.
  1. Demote `ajazz_24g_8k` from `scaffolded` to a new `scaffolded` row with
     `notes:` honestly stating wire-format mismatch (it was scaffolded but
     advertised wrong; downgrade `feature_summary` to mark `dpi` + `rgb` as
     `pending` not `works`).
  1. Mark every `IMouseCapable` setter in `aj_series.cpp` `[[deprecated]]`
     until the §11 rewrite lands.
- **Test**: `tests/unit/devices/mouse/test_aj_series_safety.cpp` —
  construct an `AjSeriesMouse` with a `MockTransport`, call `setDpiStage`,
  assert that **no** packet with opcode `0x50` is emitted.

### 1.2 `aj_series.cpp kCmdButton = 0x24` is wrong number and wrong offset

- **Severity**: every button binding silently no-ops
- **Root cause**: [`aj_series_vendor.md` line 220-221, 384](../protocols/mouse/aj_series_vendor.md) — vendor
  opcode is `0x50` (`FEA_CMD_MOUSE_SET_KEYMATRIX`), byte 1 = profile, byte 2
  = button idx, **bytes 8..11** = 4-byte action. Our code puts opcode at
  `0x24` and payload at offset 4
  ([`aj_series.cpp:184-193`](../../src/devices/mouse/src/aj_series.cpp)).
- **Fix**: blocked behind the §5 rewrite. For now: mark
  `setButtonBinding()` `[[deprecated]]` + throw `NotImplemented` with a
  comment referencing this section.

### 1.3 `aj_series.cpp` checksum is BIT8 (`& 0xff`) — vendor is BIT7 (`& 0x7f`)

- **Severity**: every feature report is rejected by the device firmware
  validator
- **Root cause**: [`aj_series_vendor.md` lines 127-133](../protocols/mouse/aj_series_vendor.md):
  > Every mouse path the renderer takes passes `BIT7` … Our
  > `aj_series.cpp` currently uses `& 0xff` (BIT8). **This is a bug-suspect
  > against the AJ159**.

  Our code:
  [`aj_series.cpp:81-83`](../../src/devices/mouse/src/aj_series.cpp).
- **Fix**: parameterise `makeEnvelope` on a `CheckSumType` enum with values
  `Bit7` (`sum & 0x7f`), `Bit8` (`sum & 0xff`), `None`. Default the AJ
  factory to `Bit7`. Keep `Bit8` available for legacy SKUs and pick per-PID
  in `register.cpp`.
- **Test**: extend `test_aj_series_wire_format` with byte-for-byte
  assertion that checksum byte 63 matches the BIT7 expected value for a
  known DPI-set packet.

### 1.4 AK980 PRO time-sync `wDayOfWeek` (already fixed but document here)

- **Severity**: would have caused wrong day-of-week display on the TFT
  except for the lucky-Thursday alignment of test runs
- **Root cause**: [`ak980pro_vendor.md` lines 244-260](../protocols/keyboard/ak980pro_vendor.md)
  — gohv hard-codes `0x04`; vendor reads real day-of-week.
- **Fix**: already landed in commit `9787962` via
  [`proprietary_keyboard.cpp:539-545`](../../src/devices/keyboard/src/proprietary_keyboard.cpp) — `local.tm_wday`
  passed through.
- **Test**: already present — [`tests/unit/devices/keyboard/`](../../tests/unit/devices/keyboard/)
  `[clock]`-tagged tests.

### 1.5 Stream Dock backend missing `ULEND` commit-after-image-burst

- **Severity**: device freeze after 5-10 rapid `setKeyImage()` calls
- **Root cause**: [`akp05_vendor.md` line 193](../protocols/streamdeck/akp05_vendor.md):
  > we send `STP` flush but never `ULEND`, which may explain occasional
  > firmware desyncs after large image bursts

  Vendor calls `ULEND` after every image-stream sequence;
  [`SDDevice::getUploadFinishedCommand` at 0x18001e6f0](../protocols/streamdeck/akp05_vendor.md).
  Our backends ([`akp05.cpp`](../../src/devices/streamdeck/src/akp05.cpp),
  [`akp153.cpp`](../../src/devices/streamdeck/src/akp153.cpp),
  [`akp815.cpp`](../../src/devices/streamdeck/src/akp815.cpp)) emit `STP`
  only.
- **Fix**: add `CmdUploadFinished = {'U','L','E','N','D'}` (5-byte opcode,
  no payload) to each `*_protocol.hpp`. Emit it **after** `STP` from
  `flush()` in every Stream-Dock-family backend.
- **Test**: extend each backend's wire-format test to assert that a 6-image
  burst ends with both `STP` and `ULEND` packets in that order.

______________________________________________________________________

## 2. P0 missing features (per-device, prioritised by user value)

Ordered by ratio of (user-perceivable feedback)/(implementation hours).

### 2.1 `VER` firmware version query (Stream Dock family)

- **What the vendor does**:
  [`akp05_vendor.md` §4.1](../protocols/streamdeck/akp05_vendor.md) —
  `SDDevice::sendGetHardwareFirmwareVersion()` emits a single `CRT VER`
  packet, reads back the input report, decodes as UTF-8/ASCII. Cached in
  `_firmwareVersion`. Used to gate the rest of the protocol via
  `isOld293Version`.
- **Our current state**: all 4 Stream Dock backends return `"unknown"` —
  search [`akp03.cpp`](../../src/devices/streamdeck/src/akp03.cpp),
  [`akp05.cpp`](../../src/devices/streamdeck/src/akp05.cpp),
  [`akp153.cpp`](../../src/devices/streamdeck/src/akp153.cpp),
  [`akp815.cpp`](../../src/devices/streamdeck/src/akp815.cpp) for
  `firmwareVersion()`.
- **Modern Qt6 reimplementation**: add `CmdVersion = {'V','E','R'}` to
  every `*_protocol.hpp`. New free function `buildVersionRequest()` →
  512/1024-byte packet. Each backend's `firmwareVersion()` writes the
  packet, reads the response with a 200 ms timeout, returns `QString` from
  `QString::fromUtf8(response_bytes).trimmed()`. Cache result behind a
  `mutable std::once_flag` so repeat calls are cheap.
- **Estimated effort**: 4 h (4 backends × ~1 h)
- **Blast radius**: 4 `*_protocol.hpp` + 4 `*.cpp` + 4 unit tests
- **Dependencies**: none — pure additive

### 2.2 `DRA` rect-addressable touch-strip update (AKP05 / Mirabox N4)

- **What the vendor does**:
  [`akp05_vendor.md` line 190 + §1.5 ImageStruct](../protocols/streamdeck/akp05_vendor.md)
  — `SDDevice::getSecondaryScreenPicInfo(ImageStruct&)` emits a `CRT DRA`
  header with `BE32 size+0x20`, `uint8 location`, `BE16 width/height/x/y`
  followed by JPEG chunks. Allows partial updates of the 800×480 strip
  without re-encoding the whole panel.
- **Our current state**: AKP05 only emits whole-strip `MAI` via
  `buildMainImageHeader()`
  ([`akp05_protocol.hpp:154-157`](../../src/devices/streamdeck/src/akp05_protocol.hpp)).
- **Modern Qt6 reimplementation**:
  - Add `CmdSecondaryScreen = {'D','R','A'}` to `akp05_protocol.hpp`.
  - New function `buildSecondaryScreenHeader(QRect zone, uint32_t jpegSize, uint8_t location)`.
  - New capability mix-in `ISecondaryScreenCapable` in `capabilities.hpp`:
    ```cpp
    virtual void setSecondaryScreenImage(QRect zone,
                                          std::span<uint8_t const> rgba,
                                          uint16_t width,
                                          uint16_t height) = 0;
    ```
  - AKP05's `image_pipeline.cpp` gains a `cropAndEncodeJpeg(QRect)` helper.
- **Estimated effort**: 8 h (new opcode + capability + AKP05 wire-up + tests)
- **Blast radius**: `akp05_protocol.{hpp,cpp}`, `akp05.cpp`,
  `image_pipeline.{hpp,cpp}`, `capabilities.hpp`, `streamdeck.hpp`,
  3 new tests
- **Dependencies**: §2.1 first (so we know we're talking the right firmware)

### 2.3 `QUCMD` generic 5-byte command (Stream Dock family)

- **What the vendor does**:
  [`akp05_vendor.md` lines 192, 372](../protocols/streamdeck/akp05_vendor.md)
  — `SDDevice::getQUCMDCommand(uint8 p1, p2, p3, p4, p5)` is the
  vendor's catch-all for sleep-on/off, idle-timeout, screen-orientation,
  knob-sensitivity, etc. Our existing `LIG` brightness path is "probably a
  `QUCMD` variant" we happen to have intercepted.
- **Our current state**: not implemented anywhere.
- **Modern Qt6 reimplementation**: add `CmdQuCmd = {'Q','U','C','M','D'}`
  (5 bytes — note longer than the 3-byte family) plus
  `buildQuCmd(uint8 p1, p2, p3, p4, p5)` builder. **Don't** wire up the
  experimental sub-commands yet — they need USB captures to identify the
  parameter encoding. Just land the builder + a single test.
- **Estimated effort**: 2 h
- **Blast radius**: each `*_protocol.hpp`, 1 new test per backend
- **Dependencies**: none

### 2.4 AK980 PRO firmware version query (`0x01`) — already wired

- **Status**: vendor opcode `0x01` is the firmware version request;
  [`proprietary_protocol.hpp:26-27`](../../src/devices/keyboard/src/proprietary_protocol.hpp)
  has `CmdGetFirmwareVersion = 0x01` and
  [`proprietary_keyboard.cpp:299-313`](../../src/devices/keyboard/src/proprietary_keyboard.cpp)
  decodes the major.minor.patch response. **Verify against AK980 PRO** —
  vendor doc [`ak980pro_vendor.md` line 192](../protocols/keyboard/ak980pro_vendor.md)
  confirms `0x01` is the vendor version request. Bump
  `feature_summary.works` once a real-device round-trip witnesses a
  non-zero version string.

### 2.5 AK980 PRO battery query (`0x20 0x01`)

- **What the vendor does**:
  [`ak980pro_vendor.md` §3.5](../protocols/keyboard/ak980pro_vendor.md) —
  request packet `[0x04, 0x20, 0x01, …]`, response byte 3 = percent
  (clamp 100). Polled every 15 s when wireless. Returns 0 silently when
  wired (`this[0x784] == 2` is the wireless flag).
- **Our current state**: not implemented.
- **Modern Qt6 reimplementation**:
  - Extend `proprietary_protocol.hpp` with
    `inline constexpr std::uint8_t CmdBatteryQuery = 0x20;` +
    `buildBatteryQuery()` builder.
  - Add `IMouseCapable::batteryPercent`-equivalent to `IClockCapable`'s
    siblings — or, better, **lift** the existing
    `IMouseCapable::batteryPercent()` signature out into a standalone
    `IBatteryCapable` mix-in in `capabilities.hpp` so keyboards (AK980 PRO),
    mice (AJ159), and dongles can all implement it.
  - In `ProprietaryKeyboard`, expose `batteryPercent()` that writes the
    request and reads the response in the 15 s `QTimer` poll loop owned by
    `DeviceWorker`.
- **Estimated effort**: 4 h
- **Blast radius**: `proprietary_protocol.{hpp,cpp}`,
  `proprietary_keyboard.cpp`, `capabilities.hpp` (new `IBatteryCapable`
  mix-in), QML `BatteryIndicator` component
- **Dependencies**: cross-cuts with §7 mouse battery refactor

### 2.6 AK980 PRO 20-mode RGB picker (`0x13`)

- **What the vendor does**:
  [`ak980pro_vendor.md` §3.4, §3.4.1](../protocols/keyboard/ak980pro_vendor.md)
  — 20 named modes (Static / SingleOn / Glittering / Breath / Spectrum /
  Outward / Scrolling / Explode / Launch / Ripples / Flowing / Pulsating /
  Tilt / Shuttle / LED Off / …). Mode-data packet at byte 1 carries the
  mode ID `0x00..0x13`, bytes 2-4 tint RGB, byte 8 rainbow flag, byte 9
  brightness (0..5), byte 10 speed (0..5), byte 11 direction. Envelope
  `0x18 → 0x13 → data → 0x02 → 0xF0`.
- **Our current state**: `proprietary_protocol.hpp` exposes
  `CmdSetRgbEffect = 0x09` (a different opcode, 4-byte payload) — not
  compatible with AK980 PRO firmware. Our `RgbEffect` enum has only 6 values
  ([`capabilities.hpp:203-210`](../../src/core/include/ajazz/core/capabilities.hpp)).
- **Modern Qt6 reimplementation**:
  - Add `CmdSetRgbMode = 0x13` to `proprietary_protocol.hpp` + new
    `buildSetRgbMode(uint8 modeId, Rgb tint, bool rainbow, uint8 brightness, uint8 speed, uint8 direction)`.
  - Add `CmdFinish = 0xF0` for envelope closure (used by every commit path
    on AK980 PRO).
  - New strong-typed `enum class AK980Lighting : uint8_t { Static = 0x00, … LedOff = 0x13 };`
    in a new `keyboard/ak980_lighting.hpp`. Use `Q_NAMESPACE` +
    `Q_ENUM_NS` so QML can iterate the values.
  - QML `LightingModePicker.qml` populated from the enum via a
    `RgbModeListModel : QAbstractListModel` (returns localized names + a
    16×16 swatch icon).
  - Refactor `RgbEffect` in `capabilities.hpp` to a richer taxonomy — see §7.
- **Estimated effort**: 8 h
- **Blast radius**: `proprietary_protocol.{hpp,cpp}`, new
  `ak980_lighting.hpp`, `proprietary_keyboard.cpp`,
  `capabilities.hpp`, QML picker
- **Dependencies**: none

### 2.7 AK980 PRO settings batch (`0x07 0x10`) — sleep-time + key-respondtime + fn-switch

- **What the vendor does**:
  [`ak980pro_vendor.md` §3.2](../protocols/keyboard/ak980pro_vendor.md) —
  33-byte feature report carrying three settings in one packet:
  byte 5 = fn_switch, byte 6 = sleep_time (1..4 — see §3.3), byte 7 =
  key_respondtime (1..5), byte 8 = checksum sum(0..7) mod 256.
- **Our current state**: not implemented.
- **Modern Qt6 reimplementation**:
  - Add `CmdSettingsBatch = 0x07` + `SettingsSubBatch = 0x10` + builder.
  - The checksum is per-packet (not per-envelope) — extend the existing
    `makeReport()` helper in `proprietary_protocol.hpp` with an overload
    that computes the byte-8 sum.
  - New `SettingsService` QML singleton (Q_PROPERTY `fnSwitch`,
    `sleepTime`, `keyResponseTime` enums) that gathers all three from the
    UI and commits via a single `commitSettings()` call. Avoids the
    flicker-from-multiple-writes UX seen in §5.3.
- **Estimated effort**: 6 h
- **Blast radius**: `proprietary_protocol.{hpp,cpp}`,
  `proprietary_keyboard.cpp`, new `SettingsService.{hpp,cpp,qml}`
- **Dependencies**: none

### 2.8 AK980 PRO per-key RGB (`0x20 0x04`)

- **What the vendor does**:
  [`ak980pro_vendor.md` §3.7](../protocols/keyboard/ak980pro_vendor.md) —
  header `byte 1 = 0x20, byte 2 = 0x04, byte 9 = 0x03 (wired) / 0x08
  (wireless)`. Then a 0xC0-byte (wired) or 0x200-byte (wireless) RGB blob.
  Then save `byte 1 = 0x02`. Wireless format includes 4 bytes per key
  (flags + RGB).
- **Our current state**: we have `CmdSetRgbBuffer = 0x0a` with a 60-byte
  chunked upload — wrong opcode for AK980 PRO.
- **Modern Qt6 reimplementation**:
  - Add `CmdPerKeyRgb = 0x20`, `PerKeyRgbSubUpload = 0x04` + a builder that
    takes a `std::span<Rgb const>` from a `QML::Repeater`-built 6×19 grid.
  - New `PerKeyRgbModel : QAbstractItemModel` (rows × cols from
    `KeyboardLayout::rows/cols`) for the UI.
  - Branch on `isWireless()` (lifted from §2.5's `IBatteryCapable`
    detection — wireless backends use the 4-byte-per-key format).
  - **Don't** unify with the per-zone path (different opcode, different
    semantics).
- **Estimated effort**: 12 h (largest of the AK980 PRO P0 features)
- **Blast radius**: `proprietary_protocol.{hpp,cpp}`,
  `proprietary_keyboard.cpp`, new `PerKeyRgbEditor.qml`,
  `PerKeyRgbModel.{hpp,cpp}`
- **Dependencies**: §2.6 (RGB mode picker lands first so users
  understand the user-lighting tab)

### 2.9 AK980 PRO TFT image upload (`0x7F` + `0x80|n`)

- **What the vendor does**:
  [`ak980pro_vendor.md` §5](../protocols/keyboard/ak980pro_vendor.md) —
  240×135 RGB565 panel. Header `[0x04, 0x7F, 0x03, lcdIdx+1, BE32
  totalChunks]`. Each chunk `[0x04, idx_lo, 0x80|idx_hi24, idx_mid, 28
  bytes pixel data]`. 2315 chunks per frame, 2 ms sleep between chunks.
  GIF supports up to 140 frames with per-frame delay byte at +0xC.
- **Our current state**: not implemented (DISPLAY-05 deferred per
  [STATE.md:114](../../.planning/STATE.md)).
- **Modern Qt6 reimplementation**:
  - Factor `image_pipeline.cpp` into a generic `ajazz_imaging` private
    static lib that produces both JPEG (Stream Dock) and RGB565 (AK980 PRO
    TFT) frames.
  - New `KeyboardScreenUploader` QObject (worker thread, owned by
    `ProprietaryKeyboard`). Exposes `void uploadGif(QVector<QImage> frames,
    QVector<int> delaysMs)` and emits `progress(int percent)`.
  - QML `TftGifEditor.qml`: a 140-cell `ListView` of frame thumbnails +
    `BrushTool` / `EraserTool` / `TextInsertTool` overlays on a
    `QGraphicsScene`-equivalent QML canvas, per-frame delay stepper.
  - **NOT** v1.2 — defer to v1.2.x per Phase 12 plan.
- **Estimated effort**: 24 h
- **Blast radius**: new `ajazz_imaging` lib, new
  `KeyboardScreenUploader.{hpp,cpp}`, new `TftGifEditor.qml` + ~5
  supporting QML files
- **Dependencies**: §2.6, §2.7 first

### 2.10 AK980 PRO key-remap chunked upload (`0x10` / `0x12`)

- **What the vendor does**:
  [`ak980pro_vendor.md` §3.12](../protocols/keyboard/ak980pro_vendor.md) —
  576-byte buffer of 16-byte remap entries, split into 21 chunks of 28
  bytes. Opcode `0x10` for top-layer, `0x12` for Fn-layer. `0xAA 0x55`
  trailer. 2 ms inter-chunk sleep.
- **Our current state**: we have single-key `CmdSetKeycode = 0x05` at
  `proprietary_protocol.hpp:28-29` which is a different (legacy?) opcode.
  The AK980 PRO firmware almost certainly expects the chunked path.
- **Modern Qt6 reimplementation**:
  - Add `CmdKeyRemapTopLayer = 0x10`, `CmdKeyRemapFnLayer = 0x12` +
    `buildKeyRemapChunks(uint8 layer, std::span<KeyRemapEntry const>)`
    returning a `std::vector<std::array<uint8_t, ReportSize>>` (the 21
    chunks).
  - New value-type `KeyRemapEntry { uint8 type; uint16 keycode; uint8 modifier; …  };` (16-byte struct matching vendor layout).
  - `KeymapEditorModel : QAbstractItemModel` 6×19 grid powering
    `KeymapEditor.qml`.
- **Estimated effort**: 16 h
- **Blast radius**: `proprietary_protocol.{hpp,cpp}`,
  `proprietary_keyboard.cpp`, new `KeymapEditorModel.{hpp,cpp,qml}`
- **Dependencies**: §2.7 (settings batch first so layout/UI patterns
  established)

### 2.11 AK980 PRO macro recording + upload (`0x09 0x1C` + `0x14`)

- **What the vendor does**:
  [`ak980pro_vendor.md` §3.10, §3.11](../protocols/keyboard/ak980pro_vendor.md)
  — macro events are 4 bytes each (key-down / key-up / mouse-button /
  delay), packed in 3584-byte buffer, split into 28-byte chunks via opcode
  `0x09 0x1C` (macro body) then a separate opcode `0x14` 7/21-chunk
  envelope to assign macros to specific keys (`0xFF` = macro-assigned,
  4 bytes [keycode_lo, keycode_mid, keycode_hi, flags] in wireless mode).
- **Our current state**: `CmdUploadMacro = 0x0d`
  ([`proprietary_protocol.hpp:40-41`](../../src/devices/keyboard/src/proprietary_protocol.hpp))
  with a different chunking shape — wrong opcode for AK980 PRO.
- **Modern Qt6 reimplementation**:
  - New `CmdMacroBody = 0x09, MacroSubChunk = 0x1C` + builder.
  - New `CmdMacroAssign = 0x14` + builder.
  - `MacroEvent` value-type union (key-down/key-up/mouse-button/delay).
  - `MacroRecorder` QObject hooks `QAbstractNativeEventFilter` on Windows,
    `evdev` on Linux, `CGEventTap` on macOS to capture the event stream.
    On finalize, serialise to the vendor's 4-byte event format.
  - QML `MacroEditor.qml` shows the event list with reorder / insert-delay
    affordances.
  - Persist macro library in `t_key_otherdata`-equivalent JSON via
    `QJsonDocument` (not SQLite — see §6 anti-features).
- **Estimated effort**: 32 h (largest single keyboard feature)
- **Blast radius**: `proprietary_protocol.{hpp,cpp}`,
  `proprietary_keyboard.cpp`, new `MacroRecorder.{hpp,cpp}` (3
  platform-specific impls), `MacroEditor.qml`, JSON persistence
- **Dependencies**: §2.7, §2.10 (settings + keymap UI patterns)

### 2.12 AK980 PRO 8-profile system

- **What the vendor does**:
  [`ak980pro_vendor.md` §7.1 + §8.1](../protocols/keyboard/ak980pro_vendor.md)
  — `t_profile_data` table with `profile INTEGER PK, name TEXT, status
  INTEGER, app TEXT, type INTEGER`. The `app TEXT` column is the
  killer feature: **per-app auto-profile-switch**. Polled at 200 ms
  intervals against `FUN_00435250` for the physical key-combo switch
  ACK pattern.
- **Our current state**: no profile system anywhere; backends are
  stateless.
- **Modern Qt6 reimplementation**: this is the cross-cutting design in
  §7 below.
- **Estimated effort**: see §7
- **Dependencies**: §2.10 + §2.11

### 2.13 AJ159 APEX 8-profile system (`0x05` / `0x85`)

- **What the vendor does**:
  [`aj_series_vendor.md` line 153](../protocols/mouse/aj_series_vendor.md)
  — `FEA_CMD_SET_PROFILE = 0x05`, byte 1 = profile idx (0..7).
- **Our current state**: not implemented.
- **Modern Qt6 reimplementation**: covered by §5 mouse rewrite.

### 2.14 AJ159 APEX 8 DPI stages (currently we say 6)

- **What the vendor does**: 8 stages per
  [`aj_series_vendor.md` line 30, 426](../protocols/mouse/aj_series_vendor.md);
  default `[400, 800, 1200, 1600, 2400, 3200, 0, 0]`, active = 1.
- **Our current state**:
  [`aj_series.cpp:135`](../../src/devices/mouse/src/aj_series.cpp)
  `dpiStageCount() = 6`.
- **Modern Qt6 reimplementation**: bump to 8 as part of §5 rewrite.

### 2.15 AJ159 omnibus packet `FEA_CMD_MOUSE_SET_OPTIONPARAM0 = 0x53`

The single most important opcode in the mouse vendor app:
[`aj_series_vendor.md` §"FEA_CMD_MOUSE_SET_OPTIONPARAM0 byte layout"](../protocols/mouse/aj_series_vendor.md)
carries debounce, sleep-time, LOD, sensitivity, angle-snap, motion-smoothing,
power-save, battery-LED colours and charging-LED toggle all in one packet.
**Vendor has no separate "commit" step — writes persist immediately.**
Wire-up covered by §5.

______________________________________________________________________

## 3. P1 features (v1.3+ candidates)

### 3.1 Elgato Stream Deck v6-compatible plugin host (`SDPluginServer`)

- **What the vendor does**:
  [`akp05_vendor.md` §5](../protocols/streamdeck/akp05_vendor.md) — the
  Stream Dock app implements a **verbatim** Elgato Stream Deck SDK v6
  WebSocket protocol on `127.0.0.1:<dynamic port>` (plus a parallel TCP
  server). The reference plugin in
  `defaultData/defaultPlugins/com.hotspot.streamdock.memo.sdPlugin/plugin/index.js`
  literally calls `connectElgatoStreamDeckSocket(inPort, inPluginUUID,
  inRegisterEvent, inInfo)` exactly as Elgato Stream Deck SDK does.
  Plugins are JS in `node20.exe` subprocesses.
- **Why P1 not P0**: massive feature with its own milestone-worth of work,
  but the leverage is enormous. Any existing `.sdPlugin` package (and there
  are thousands) drops in.
- **Modern Qt6 reimplementation**:
  - New module `src/host/plugin-host/` mirroring our existing
    `ajazz_plugins` PRIVATE-linked architecture.
  - `Qt6::WebSockets` `QWebSocketServer` on `127.0.0.1:0` (dynamic port).
  - 13 standard message types (keyDown/keyUp/willAppear/setImage/setTitle/
    setSettings/getSettings/showAlert/showOk/openUrl/logMessage/etc.) per
    [vendor doc §5.2-5.3](../protocols/streamdeck/akp05_vendor.md).
  - Plugins spawn as separate `QProcess` children (matches our existing
    SEC-003 sandbox pattern). Bundle Node.js (or pyodide for Python).
  - Manifest parsing: PRIVATE-linked `nlohmann::json` in
    `ajazz_plugin_host` (preserves COD-031 boundary at `ajazz_core`).
  - Map Elgato `Controllers: ["Keypad", "Knob"]` to our
    `IDisplayCapable` + `IEncoderCapable` capability mix-ins.
- **Estimated effort**: 80 h (single largest item in the roadmap)
- **Dependencies**: §2.2 (DRA partial updates make `setImage` actually
  efficient)

### 3.2 AK980 PRO firmware DFU

- **What the vendor does**:
  [`ak980pro_vendor.md` §6](../protocols/keyboard/ak980pro_vendor.md) — the
  vendor **does not** flash from `DeviceDriver.exe`. Instead it downloads
  `FirmwareUpdateTool.zip` over HTTP, unpacks to `%TEMP%`,
  `CreateProcessW` it, waits.  `FUN_0044f2d0` sends 0x1001-byte bulk
  writes — likely the DFU path inside the separate tool. **Exact DFU
  opcode + entry command CANNOT be recovered from `DeviceDriver.exe`
  alone**.
- **Recommendation**: defer to v1.3+; needs separate Ghidra session on
  `FirmwareUpdateTool.exe`.
- **Status**: `[needs capture]` — flag in `IFirmwareCapable::beginFirmwareUpdate()`
  with `throw std::runtime_error("DFU pending FirmwareUpdateTool.exe RE")`.

### 3.3 AJ159 BLE OTA via `upgradeOTAGATT`

- **What the vendor does**:
  [`aj_series_vendor.md` line 89, lines 345-355](../protocols/mouse/aj_series_vendor.md)
  — BLE-only OTA upload via gRPC `upgradeOTAGATT`. Mouse-mode OTA uses
  HID-based `MLEDBOOTLOADER` path (`0x40` enter, `0x41` start, 64-byte
  chunks, `0xc1` checksum). Both paths skip first 0x10000 bytes (boot
  header).
- **Recommendation**: defer to v1.3+. BLE-OTA requires Qt6 Bluetooth
  module + per-OS pairing dance; HID OTA is feasible but needs first-party
  capture to confirm chunk size and checksum algorithm.
- **Status**: `[needs capture]` for HID OTA; `[needs Qt6 Bluetooth]` for
  BLE OTA.

### 3.4 BIT7 vs BIT8 checksum auto-detect (mouse)

- See §1.3. After the BIT7 default lands, an auto-detect that flips to
  BIT8 on first response-timeout would future-proof against pre-AJ159 SKUs
  that may use the legacy encoding.
- **Effort**: 4 h — `MouseTransport` wraps `ITransport`, retries with
  alternate checksum on first ACK-timeout, caches the answer.

### 3.5 ScreenCaptureTool live mirror to AKP05 main strip

- **What the vendor does**:
  [`akp05_vendor.md` §6](../protocols/streamdeck/akp05_vendor.md) — separate
  `ScreenCaptureTool.exe` captures a region of the desktop and (most
  likely) pipes JPEG/H.264 frames to a plugin or directly via `DRA`/`MAI`.
  Not fully decompiled in vendor doc.
- **Recommendation**: ship as a built-in plugin once §3.1 plugin host
  lands. Use `Qt6::Multimedia` `QScreenCapture` for cross-platform
  desktop capture. Frame budget: ≤30 FPS, JPEG q=70, max 800×100 zone.

### 3.6 AJ159 TFT LCD widget set (CPU/RAM/disk/weather)

- **What the vendor does**:
  [`aj_series_vendor.md` lines 83, 328-344](../protocols/mouse/aj_series_vendor.md)
  — `FEA_CMD_SETTFTLCDDATA = 0x25` (16-bit colour) /
  `FEA_CMD_SET_SCREEN_24BITDATA = 0x29` (24-bit). 56-byte payload chunks
  with currentFrame / frameNum / frameDelay / chunkIndex / chunkLen
  headers. Host-side host-info widgets render the LCD content (CPU, RAM,
  disk, weather via gRPC `getWeather`).
- **Recommendation**: ship the wire format in v1.3 (chunked upload),
  built-in widgets as plugins post-§3.1. **Do not** implement the weather
  widget — see §4 anti-features.

### 3.7 Stream Dock `LOG` boot-logo / screensaver upload

- **What the vendor does**:
  [`akp05_vendor.md` line 186](../protocols/streamdeck/akp05_vendor.md) —
  `SDDevice::sendLogoSizeCommand(int size, uchar type)` emits `CRT LOG
  <BE32 size> <type>` then streams JPEG chunks via the existing image path.
  `type = 0x02` for splash.
- **Recommendation**: new `IBootLogoCapable` mix-in in `capabilities.hpp`
  + per-backend builder. ~6 h. Ship in v1.3.

### 3.8 Stream Dock `M_V` 1024-byte secondary-screen logo packet

- **What the vendor does**:
  [`akp05_vendor.md` line 191](../protocols/streamdeck/akp05_vendor.md) —
  when `location == 0x12` in `getSecondaryScreenPicInfo`, the SDK emits a
  1024-byte `M_V` packet (no `CRT` prefix). Used for boot logo on
  touch-strip.
- **Status**: `[needs capture]` — header bytes only visible in Ghidra dump.

### 3.9 Stream Dock `GIFVER` opcode

- **Status**: `[needs capture]` — literal string found in `.rdata`
  ([`akp05_vendor.md` line 194](../protocols/streamdeck/akp05_vendor.md))
  but no Ghidra function bound to it.

### 3.10 Stream Dock multi-page system (`STP` page-magic byte 9)

- **What the vendor does**:
  [`akp05_vendor.md` lines 184, 199-204](../protocols/streamdeck/akp05_vendor.md)
  — only the legacy `StreamDock[321D]`, `H3`, `StreamDock[0108D]` use
  `byte 9 = '!'/'"'/#'` (0x21/0x22/0x23) for pages 1/2/3 on `STP`. Newer
  V25 families use `QUCMD`-routed page commands. We don't claim to support
  the legacy models; skip until a user asks.
- **Status**: SKIP

### 3.11 Stream Dock OBS WebSocket client (`OBSWebSocketClient`)

- **What the vendor does**:
  [`akp05_vendor.md` lines 109-110, 402](../protocols/streamdeck/akp05_vendor.md)
  — built-in OBS Studio integration over `ws://localhost:4455`.
- **Recommendation**: ship as a plugin (`com.ajazz.control.obs.sdPlugin`)
  on top of §3.1, NOT in the core backend. Default-enable OBS auth checks.

### 3.12 AK820 Pro family GIF upload via interrupt-OUT EP3 (gohv)

- **Note**: AK820 Pro uses 128×128 RGB565 over interrupt-OUT EP3 in
  9 chunks of 4123 bytes
  ([`ak980pro_vendor.md` §5.4](../protocols/keyboard/ak980pro_vendor.md)).
  **DO NOT** copy this path onto AK980 PRO — AK980 PRO is HID-only with
  28-byte chunks. Two completely different upload protocols for two
  different devices.

______________________________________________________________________

## 4. Anti-features — DELIBERATELY skipped

Captured here so a future contributor can answer "why don't we have X" by
pointing at this section.

### 4.1 AJ159 / `iot_driver.exe` plaintext gRPC localhost listener

- [`aj_series_vendor.md` lines 510-515](../protocols/mouse/aj_series_vendor.md)
  — `127.0.0.1:3814`, no mutual auth, no token, any process on the host can
  issue arbitrary HID feature reports.
- **We do**: keep in-process hidapi via `ITransport`. Strictly safer.

### 4.2 AJ159 cloud login (`api.rongyuan.tech:3814` /
`api2.qmk.top:3814`)

- [`aj_series_vendor.md` lines 24, 494-498, 647](../protocols/mouse/aj_series_vendor.md)
  — mandatory account sign-in to fetch community profiles.
- **We do**: import/export `.ajprofile` JSON via the file system. If we
  add sharing later, self-hostable backend or git-tracked profiles only.

### 4.3 AJ159 `getWeather` phone-home

- [`aj_series_vendor.md` lines 101, 485-489, 652](../protocols/mouse/aj_series_vendor.md)
  — leaks the user's typed `address` to the vendor server for an LCD
  widget.
- **We do**: skip entirely; if we add weather later, opt-in + user-chosen
  provider.

### 4.4 AJ159 `watchSystemInfo` host telemetry

- [`aj_series_vendor.md` lines 83, 490-493](../protocols/mouse/aj_series_vendor.md)
  — pushes CPU temp, mem, disk, network IO over plaintext gRPC.
- **We do**: if we ever implement system-monitor widgets, gate behind
  per-widget opt-in.

### 4.5 AJ159 `universal-analytics` + `node-machine-id`

- [`aj_series_vendor.md` lines 21, 499-505, 648](../protocols/mouse/aj_series_vendor.md)
  — `trackEvent` is a stub today but `node-machine-id` is still imported
  and creates a stable hardware UUID that could be sent later.
- **We do**: zero analytics SDKs in our dependency tree.

### 4.6 AJ159 recoil-control / rapid-fire (`0x60` / `0x55`)

- [`aj_series_vendor.md` lines 231-234, 507-509](../protocols/mouse/aj_series_vendor.md)
  — anti-cheat liability. Many competitive games ban this.
- **We do**: not implementing. Document in our protocol doc that the
  opcode space exists but we deliberately skip it.

### 4.7 AK980 PRO file-encrypted SQLite blob layer

- [`ak980pro_vendor.md` §9 anti-feature 4](../protocols/keyboard/ak980pro_vendor.md)
  — `FUN_004635b0` CRC32 table encryption layer over file IO.
- **We do**: plain JSON via `QJsonDocument`. Threat model doesn't justify
  the complexity.

### 4.8 AK980 PRO HTTP-spawn external `FirmwareUpdateTool.exe`

- [`ak980pro_vendor.md` §6, §9 anti-feature 2](../protocols/keyboard/ak980pro_vendor.md)
  — downloads firmware updater over HTTP, spawns external process.
- **We do**: in-app DFU with progress + verify (when we eventually ship
  it — §3.2).

### 4.9 AK980 PRO 200 ms polling for foreground-app detection
(`FUN_00435250`)

- [`ak980pro_vendor.md` §9 anti-feature 3](../protocols/keyboard/ak980pro_vendor.md)
  — busy-loop polling.
- **We do**: event-driven `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` on
  Windows, D-Bus subscription on Linux, NSWorkspace notifications on
  macOS. See §7 `ForegroundAppMonitor`.

### 4.10 Stream Dock Alibaba Cloud OSS auto-update CDN

- [`akp05_vendor.md` §8 first row + lines 399-400](../protocols/streamdeck/akp05_vendor.md)
  — `cdn1.key123.vip` + `hotspot-oss-bucket.oss-cn-shenzhen.aliyuncs.com`.
- **We do**: updates via Flatpak/MSI/dnf/apt repositories.

### 4.11 Stream Dock Alibaba plugin store

- [`akp05_vendor.md` §8 row 3](../protocols/streamdeck/akp05_vendor.md)
  — `hotspot-oss-bucket.oss-cn-shenzhen.aliyuncs.com/plugin/...`.
- **We do**: ship a self-hostable plugin index. Optional: GitHub releases
  metadata for vendor-published plugins.

### 4.12 Stream Dock global low-level keyboard hook (`Hook::sendKeyDownValue`)

- [`akp05_vendor.md` §1.4 + §8 row 6](../protocols/streamdeck/akp05_vendor.md)
  — Win32 `SetWindowsHookEx WH_KEYBOARD_LL` is security-sensitive (can be
  used for keylogging).
- **We do**: per-plugin opt-in, user-toggleable, with a clear permission
  prompt the first time a plugin requests it. Default OFF.

### 4.13 AK980 PRO custom "MUI" UI framework (`mui.dll`)

- [`ak980pro_vendor.md` §9 anti-feature 1](../protocols/keyboard/ak980pro_vendor.md)
  — vendor ships its own UI toolkit (2 201 exports).
- **We do**: Qt 6 / QML.

### 4.14 AK980 PRO dynamic `LoadLibraryA("hid.dll")` + `GetProcAddress`

- [`ak980pro_vendor.md` §1.1, §2.1](../protocols/keyboard/ak980pro_vendor.md)
  — vendor resolves HID API functions dynamically at runtime.
- **We do**: link `hidapi_hidraw` normally (already done — see
  CLAUDE.md Linux device access section).

### 4.15 AJ159 universal mouse driver supporting dozens of unrelated SKUs

- [`aj_series_vendor.md` line 40](../protocols/mouse/aj_series_vendor.md)
  — vendor binary supports AJAZZMOUSE / GamingMouse / AQIRYSmousesoftware /
  rongyuan / VKMS / 炫光 / akko brands.
- **We do**: AJAZZ-only. Don't grow into a competitor to OpenRGB /
  Piper / libratbag.

______________________________________________________________________

## 5. Architectural patterns to ADOPT (verified worth copying)

### 5.1 Elgato Stream Deck v6 plugin SDK (AKP05 vendor)

Industry-standard, thousands of existing plugins, well-documented. See
§3.1 — adopt verbatim. Reference plugin code in
`defaultData/defaultPlugins/com.hotspot.streamdock.memo.sdPlugin/plugin/index.js`.

### 5.2 Auto-profile-switch by foreground app (AK980 PRO)

Killer feature, low implementation cost (host-only logic over our existing
profile JSON). See §7 design.

### 5.3 Per-level explanatory tooltip for Key Response Time (AK980 PRO)

[`ak980pro_vendor.md` §8 row 4](../protocols/keyboard/ak980pro_vendor.md):
1033.lan 600-624 gives a 1-5 scale with live wired / 2.4 GHz / Bluetooth
latency estimates. Lifts a confusing slider into a self-explaining UX.
**Pattern**: any time we expose a 0..N raw firmware scale to the user,
attach a `tooltip:` string that explains what each level *means*.

### 5.4 140-frame GIF editor (AK980 PRO TFT)

[`ak980pro_vendor.md` §8 row 2](../protocols/keyboard/ak980pro_vendor.md):
brush (fine/medium/thick) + eraser + text-insert + per-frame delay editor.
Adopt for any TFT display we support (AK980 PRO 240×135, future AJ159
LCD, future Stream Dock encoder LCDs). Sketch in §2.9.

### 5.5 Single "Save" button per tab, not per input (AJ159 omnibus packet)

[`aj_series_vendor.md` line 532-534](../protocols/mouse/aj_series_vendor.md):
the omnibus `0x53` packet maps cleanly to a single "save" UX button. This
reduces RGB flicker on the device and reduces NVM wear. **Pattern**: any
"settings tab" UI should batch its writes to a single
`SettingsService::commit()` call rather than writing on every
`onValueChanged`.

### 5.6 Side bar nav structure (AJ159)

[`aj_series_vendor.md` line 545-546](../protocols/mouse/aj_series_vendor.md):
Profile selector at top, then tabs `Buttons`, `DPI`, `Polling`,
`Sensor (LOD + sensitivity + smooth + angle-snap)`, `Lighting`, `Macros`,
`Sleep`, `About / Firmware`. Battle-tested mouse-config UX shape.

______________________________________________________________________

## 6. Architectural patterns to AVOID

### 6.1 2-process gRPC localhost daemon (AJ159 `iot_driver.exe`)

See §4.1. Security regression vs our in-process `ITransport`. Also adds
deployment complexity (extra binary to ship, version-skew between
renderer and driver, on-exit cleanup hazards — see
[`aj_series_vendor.md` line 516-519](../protocols/mouse/aj_series_vendor.md)
`changeWirelessLoopStatus` not cleaned up on exit).

### 6.2 Custom UI framework (`mui.dll`)

See §4.13. Qt6/QML is the right answer.

### 6.3 Dynamic `LoadLibraryA + GetProcAddress` for system libraries

See §4.14. Link normally.

### 6.4 Embedded `sled` KV store (AJ159) or in-tree SQLite (AK980 PRO,
Stream Dock)

See §4.7. Both vendor apps over-engineer persistence. We use:
- `QStandardPaths::AppLocalDataLocation` + `QJsonDocument` for profile /
  macro / settings data.
- `QSettings` (INI format on Linux/macOS, registry on Windows — but use
  IniFormat **everywhere** for portability per cross-platform principles)
  for small key/value (last-known-DPI, window geometry).
- The **only** scenarios where SQLite earns its keep are full-text search
  and large relational queries — neither applies here.

### 6.5 Polling for foreground-app detection

See §4.9.

### 6.6 Hardcoded `CSIDL_LOCAL_APPDATA`

[`ak980pro_vendor.md` §9 anti-feature 5](../protocols/keyboard/ak980pro_vendor.md).
Use `QStandardPaths::AppLocalDataLocation` — works on all 3 platforms.

### 6.7 Background polling thread for hot-plug events
(Stream Dock `StreamDockWatcher.exe`)

[`akp05_vendor.md` §9 row 6](../protocols/streamdeck/akp05_vendor.md). Our
udev (Linux) / `RegisterDeviceNotificationW` (Windows) / IOKit (macOS)
event-driven hot-plug already does this right
([`HotplugMonitor`](../../src/core/include/ajazz/core/hotplug.hpp)).

______________________________________________________________________

## 7. Cross-device infrastructure (new C++ classes needed)

### 7.1 `IProfileCapable` mix-in + `ProfileManager` QML singleton

New mix-in in `capabilities.hpp`:

```cpp
class IProfileCapable {
public:
    virtual ~IProfileCapable() = default;
    [[nodiscard]] virtual std::uint8_t profileCount() const noexcept = 0;
    [[nodiscard]] virtual std::uint8_t activeProfile() const noexcept = 0;
    virtual void switchProfile(std::uint8_t index) = 0;
    [[nodiscard]] virtual QString profileName(std::uint8_t index) const = 0;
    virtual void renameProfile(std::uint8_t index, QString name) = 0;
};
```

- AJ159 → 8 profiles via `FEA_CMD_SET_PROFILE = 0x05`
- AK980 PRO → 8 profiles via the SQLite table schema (host-driven; device
  has no in-firmware profile register, just a per-key remap layer)
- AKP05 / Stream Dock → host-only profiles (no on-device storage; profiles
  are JSON in `QStandardPaths::AppLocalDataLocation`)

QML singleton `ProfileManager` (registered via
`qmlRegisterSingletonInstance` per the v1.0 light-theme bug lesson +
A-01 Pitfall 13 lock — see CLAUDE.md Qt6 gotchas) owns profile metadata,
emits `Q_SIGNAL profileSwitched(int oldIdx, int newIdx)` for UI binding.

### 7.2 `ForegroundAppMonitor` (cross-platform)

Header-only interface in `src/core/include/ajazz/core/foreground_app.hpp`:

```cpp
class IForegroundAppMonitor : public QObject {
    Q_OBJECT
public:
    [[nodiscard]] virtual QString currentApp() const = 0;
signals:
    void foregroundAppChanged(QString exePath, QString windowTitle);
};
```

Three implementations (factory in `register.cpp`):
- **Windows**: `SetWinEventHook(EVENT_SYSTEM_FOREGROUND, …, EVENT_OBJECT_NAMECHANGE, …)`
  + `GetWindowThreadProcessId` + `QueryFullProcessImageNameW`.
- **Linux**: D-Bus subscription on the user session bus
  (`org.kde.KWin.activeWindow` or `org.gnome.Shell.Eval` — pick one, expose
  as a backend choice).
- **macOS**: `[[NSWorkspace sharedWorkspace] notificationCenter]
  addObserver:NSWorkspaceDidActivateApplicationNotification`.

`ProfileManager` listens on `foregroundAppChanged`, looks up the exe path
in the per-profile `triggerApp` field, calls
`IProfileCapable::switchProfile` on every device that declares
`IProfileCapable`.

### 7.3 `IMacroCapable` mix-in + `MacroRecorder` QML service

New mix-in:

```cpp
class IMacroCapable {
public:
    virtual ~IMacroCapable() = default;
    [[nodiscard]] virtual std::uint8_t macroSlotCount() const noexcept = 0;
    virtual void uploadMacro(std::uint8_t slot, std::span<MacroEvent const>) = 0;
    [[nodiscard]] virtual std::vector<MacroEvent> readMacro(std::uint8_t slot) const = 0;
};

struct MacroEvent {
    enum class Kind { KeyDown, KeyUp, MouseButton, MouseMove, Delay };
    Kind kind;
    std::uint32_t value;     // HID usage code or mouse button
    std::int16_t dx, dy;     // for MouseMove
    std::uint16_t delayMs;   // for Delay
};
```

- AK980 PRO → 20+ slots via opcode `0x09 0x1C` (§2.11)
- AJ159 → 20 slots via opcode `0x16` (see §5 mouse rewrite)

`MacroRecorder` QObject (3 platform impls per §7.2 pattern) captures the
host event stream and produces a `std::vector<MacroEvent>`. QML
`MacroEditor.qml` consumes it.

### 7.4 `IBatteryCapable` mix-in

Lift the existing `IMouseCapable::batteryPercent()` signature into a
standalone mix-in:

```cpp
class IBatteryCapable {
public:
    virtual ~IBatteryCapable() = default;
    [[nodiscard]] virtual std::optional<std::uint8_t> batteryPercent() const = 0;
    [[nodiscard]] virtual bool isCharging() const = 0;
};
```

Allows AK980 PRO keyboard, AJ159 mouse (when wireless), and the AK980 PRO
2.4 GHz dongle to all implement the same interface. QML
`BatteryIndicator.qml` polls a single signal regardless of family.

### 7.5 `IFirmwareUpdatable` (separate from `IFirmwareCapable`)

`IFirmwareCapable` (existing) is non-destructive query only. Add a
**separate** `IFirmwareUpdatable` mix-in for DFU because DFU is
destructive and we want UI to gate it behind an additional consent
dialog:

```cpp
class IFirmwareUpdatable {
public:
    virtual ~IFirmwareUpdatable() = default;
    [[nodiscard]] virtual bool canUpdateFirmware() const = 0;
    virtual std::uint32_t beginFirmwareUpdate(std::span<std::uint8_t const> image) = 0;
    [[nodiscard]] virtual std::uint8_t firmwareUpdateProgress(std::uint32_t token) const = 0;
};
```

Move `beginFirmwareUpdate` / `firmwareUpdateProgress` out of
`IFirmwareCapable`
([`capabilities.hpp:541-552`](../../src/core/include/ajazz/core/capabilities.hpp)).
Add backward-compat default implementations on `IFirmwareCapable` that
delegate to the new interface via `dynamic_cast<IFirmwareUpdatable*>(this)`.

### 7.6 Lighting effects taxonomy

Current `RgbEffect` enum
([`capabilities.hpp:203-210`](../../src/core/include/ajazz/core/capabilities.hpp))
has 6 values. Vendor reality:
- AK980 PRO: 20 modes (§2.6)
- AJ159 APEX: 10 effect types (`LightOff/LightAlwaysOn/LightBreath/
  LightNeon/LightWave/LightDazzing/LightLaser/LightMusicFollow/
  LightScreenColor/LightMusicFollow2` —
  [`aj_series_vendor.md` lines 195-208](../protocols/mouse/aj_series_vendor.md))

Approach: **semantic mapping rather than 1:1 enum copy**. The shared
`RgbEffect` exposes a common-denominator taxonomy:

```cpp
enum class RgbEffect : std::uint8_t {
    Off,
    Static,
    Breathing,
    ColorCycle,
    Wave,
    ReactiveRipple,
    MusicFollow,
    ScreenColor,
    Custom,      // per-LED via setRgbBuffer
};
```

Each device-specific RGB header (`ak980_lighting.hpp`,
`aj_lighting.hpp`) keeps its own native enum with the firmware ID values
and provides a mapping table `static constexpr std::array<RgbEffect, N> kNativeToCommon`.
Backends use the native enum on the wire; UI uses the common enum for
the picker.

______________________________________________________________________

## 8. Maturity tier adjustments (devices.yaml changes implied)

These are advisory — actual edits should land via amendment commits to
`devices.yaml`, not inside this synthesis run.

| codename | Current | Proposed | Justification |
|----------|---------|----------|---------------|
| `ajazz_24g_8k` | scaffolded | **scaffolded with WARN note** | §1.1-§1.3 surface wire-format mismatch. Bump `notes:` to honestly say "advertised dpi+rgb capabilities are NOT wire-correct vs AJ159 vendor; see `clean-reimplementation-roadmap.md` §1.1-§1.3; rewrite tracked as ATM-XX". Demote `feature_summary.works` items to `pending`. |
| `aj_series_wired_primary` (and the 5 sibling rows) | scaffolded | unchanged | Same backend, same bug surface. After §5 rewrite they all promote together. |
| `ak980pro` | partial | **partial** (unchanged) | ARCH-05.1 promoted scaffolded → partial. Per [STATE.md §2026-05-17](../../.planning/STATE.md) commit `9787962` already records the 18-opcode gap as `pending` items; this synthesis adds 8 more opcodes (per-key RGB, settings batch, sleep timer, battery query, RGB mode, key remap chunked, macro upload, macro assign) that should be appended to `feature_summary.pending`. Full promotion to `functional` requires §2.6-§2.11 to land. |
| `akp05` / `mirabox_n4` | scaffolded | scaffolded | Now document the missing V25 device codename variants from vendor binary: `AKP05V25`, `AKP05EV25`, `AKP05RV25`, `N4Pro`, `N4ProE`, `N4V25`, `SD14N4V25`, `TS10N4V25`, `VSDN4`. Add a `feature_summary.pending` line: "register V25 sibling PIDs once AKP05 backend reaches partial — vendor SDK recognises 9 sibling codenames at the same wire protocol". |
| `akp03_variant_3004` | scaffolded | scaffolded | Add sibling V25 codenames: `AKP03V25`, `AKP03EV25`, `AKP03RV25`, `SD12N3V25`, `TS16N3V25`, `VSDN3`. Note that vendor strings index from [`akp05_vendor.md` §1.3](../protocols/streamdeck/akp05_vendor.md). |

______________________________________________________________________

## 9. Phase 9.x finalization implications

The three vendor RE docs change the answer to "do we still need Phase 9.x
captures before Phase 10 starts?".

### 9.1 ARCH-04 (image pipeline) — FINAL

Vendor RE confirms host-side JPEG encode + chunked hidapi write is the
right shape. [`akp05_vendor.md` §2 + §6](../protocols/streamdeck/akp05_vendor.md):
- `SDGeneralHidOutputReport` → `hid_write`
- per-device `_deviceReportId` byte prepended at write time
- per-packet `_packetSize` (matches our 512 vs 1024 split per family)
- `DataFormatConversion::gifToBin` in SDK PDB confirms host-side decode

ARCH-04 default verdict can be **promoted from DEFAULT to FINAL** without
captures — the vendor's own SDK has the same architecture. **Recommendation**:
amend [`ARCH-04.md`](../../.planning/phases/09-research-captures-hygiene/ARCH-04.md)
with this corroborating evidence in a separate `.planning/` commit.

### 9.2 ARCH-05.1 (AK980 PRO clock sync) — FINAL

Triple-source corroboration (gohv + KyleBoyer + DeviceDriver.exe Ghidra
in `ak980pro_vendor.md` §3.1) plus the `wDayOfWeek` fix already applied
in commit `9787962`. Promotion from `scaffolded → partial` already landed;
promotion to `functional` requires the round-trip witness Phase 9.x test
(TFT clock widget shows the time we sent).

The vendor doc also reveals **two new findings** that should be amended
to ARCH-05.1:
1. The vendor sends a leading **CMD_START** (`0x18`) before CMD_TIME
   (`0x28`). KyleBoyer skips this; gohv does it; **we do it** (commit
   `9787962` per `buildSetTimeStart`). Corroboration that our 4-packet
   envelope is correct.
1. The vendor's LCD-aware variant `FUN_00423a10` writes
   `LCDViewList::GetCurSel + 1` into byte 3 of the time-data packet. We
   currently write `0x01` (since we have no multi-LCD UI). When we add
   multi-LCD support, this field becomes meaningful.

### 9.3 ARCH-06 (composite-HID dedup) — STILL FINAL

Stream Dock RE shows no evidence of composite enumeration changing the
picture. [`akp05_vendor.md` §1.2](../protocols/streamdeck/akp05_vendor.md):
`SDDevice::openHidDevice(int vid, int pid, QString &serial, int interface,
int collection, uchar reportId)` — composite **handling** exists (the
`interface` + `collection` parameters) but no dedup logic. The default
verdict "NOT firing — topology proves `0c45:7016` is a separate dongle"
stands.

### 9.4 Recommendation

**Phase 9.x captures no longer block Phase 10**. The RE evidence is
stronger than what live captures would produce for these specific
questions. Captures still have residual value for:
- Negative tests (year 2099 timestamp produces visible-but-wrong display
  → §9.2's witness 3).
- USB-side latency profiling (how slow IS chunked upload on a real
  device?).
- Per-byte JPEG quality tuning for the AKP03 0x3004 variant.

But none of these block the next 5 commits in §11.

______________________________________________________________________

## 10. Effort estimate per priority bucket

Hours are senior-engineer estimates assuming established Qt6 patterns
already in the codebase + no concurrent rework.

| Bucket | Items | Hours |
|--------|-------|-------|
| **P0 safety fixes** (§1) | 5 items | 6 + 0 (deferred) + 4 + 0 (done) + 6 = **16 h** |
| **P0 missing features** (§2.1-§2.8) | 8 items | 4 + 8 + 2 + (already wired) + 4 + 8 + 6 + 12 = **44 h** |
| **P0 missing features deferred to v1.2.x** (§2.9-§2.11) | 3 items | 24 + 16 + 32 = **72 h** |
| **P0 cross-device (§7)** | profile + foreground + macro mix-ins + battery + DFU split | ~24 h |
| **P0 total** | | **156 h** |
| **P1 plugin host** (§3.1) | Elgato-compatible WebSocket | **80 h** |
| **P1 DFU / OTA** (§3.2-§3.3) | AK980 PRO + AJ159 HID OTA + BLE OTA | **40 h** (HID only; BLE deferred) |
| **P1 misc** (§3.4-§3.12, excluding skips/deferrals) | checksum auto-detect + screen mirror + AJ TFT widgets + LOG boot logo + M_V + GIFVER | **48 h** |
| **P1 total** | | **168 h** |
| **P2 / v1.4+ wishlist** | OBS plugin, system-monitor plugins, calendar plugin, multi-page UI for legacy Stream Decks | **~120 h** (mostly plugins on top of §3.1) |

**Total roadmap**: ~444 h = ~11 person-weeks of focused work.

Realistic milestone shape:
- **v1.2** (current): §1 (P0 safety) + §2.1-§2.8 (P0 missing on-deck) +
  §7.4 (battery mix-in) = ~64 h ≈ 2 weeks
- **v1.2.x**: §2.9-§2.11 (AK980 PRO TFT + remap + macros) + §7
  (profile + foreground + macro) = ~96 h ≈ 3 weeks
- **v1.3**: §3.1 plugin host + §3.7 (LOG boot logo) = ~86 h ≈ 2.5 weeks
- **v1.4+**: §3.5-§3.6 (live screen mirror + AJ TFT widgets), P2 wishlist

______________________________________________________________________

## 11. Recommended next 5 atomic commits

After this synthesis lands as a research artefact, the next 5 commits
should be — in order, with concrete scope:

### 11.1 `fix(mouse): safety guard against aj_series.cpp wire-format mismatch`

- Files touched:
  - [`src/devices/mouse/src/aj_series.cpp`](../../src/devices/mouse/src/aj_series.cpp)
    — add `[[deprecated]]` to every public setter; remove `commit()`
    helper and its call sites at lines 145+157; add a WARN-once log in
    the constructor citing this roadmap §1.1-§1.3.
  - [`docs/_data/devices.yaml`](../../docs/_data/devices.yaml) — downgrade
    `ajazz_24g_8k.feature_summary.works` items to `pending`; bump `notes:`
    with mismatch citation.
  - `tests/unit/devices/mouse/test_aj_series_safety.cpp` — NEW; assert no
    opcode-0x50 packet is emitted from any setter call.
- Honesty contract: explicitly bumps the device row to acknowledge the
  bug we documented.
- Estimated diff size: ~80 lines code, ~40 lines test, ~10 lines yaml.

### 11.2 `feat(keyboard): implement AK980 PRO battery query (opcode 0x20 0x01)`

- Files touched:
  - [`src/devices/keyboard/src/proprietary_protocol.hpp`](../../src/devices/keyboard/src/proprietary_protocol.hpp)
    — add `CmdBatteryQuery = 0x20`, `BatteryQuerySub = 0x01` +
    `buildBatteryQuery()` declaration.
  - [`src/devices/keyboard/src/proprietary_keyboard.cpp`](../../src/devices/keyboard/src/proprietary_keyboard.cpp)
    — implement builder; add `batteryPercent()` method behind a new
    `IBatteryCapable` mix-in.
  - [`src/core/include/ajazz/core/capabilities.hpp`](../../src/core/include/ajazz/core/capabilities.hpp)
    — new `IBatteryCapable` mix-in (lifted from `IMouseCapable`); leave
    `IMouseCapable::batteryPercent()` as a default-implemented alias for
    backward compat.
  - `tests/unit/devices/keyboard/test_ak980pro_battery.cpp` — NEW;
    byte-level assertion against `MockTransport` for the request packet
    + decoding of a synthetic 33-byte response.
  - [`docs/_data/devices.yaml`](../../docs/_data/devices.yaml) — append
    `battery` capability to `ak980pro.capabilities` list (and define it
    in the `capabilities:` legend if not yet present).
- Confirmation gate: real-hardware witness on a wireless AK980 PRO showing
  the QML `BatteryIndicator` updates from 0 → real percentage.
- Estimated diff: ~120 lines code, ~80 lines test.

### 11.3 `feat(streamdeck): add VER firmware version query + ULEND commit sentinel`

- Files touched:
  - All 4 `*_protocol.hpp`
    ([`akp03`](../../src/devices/streamdeck/src/akp03_protocol.hpp),
    [`akp05`](../../src/devices/streamdeck/src/akp05_protocol.hpp),
    [`akp153`](../../src/devices/streamdeck/src/akp153_protocol.hpp),
    [`akp815`](../../src/devices/streamdeck/src/akp815_protocol.hpp))
    — add `CmdVersion = {'V','E','R'}` (3-byte) and
    `CmdUploadFinished = {'U','L','E','N','D'}` (5-byte); declare
    `buildVersionRequest()` + `buildUploadFinished()`.
  - All 4 `*.cpp` — implement the builders; replace `"unknown"`
    `firmwareVersion()` with the real query path; emit `ULEND` after
    `STP` from `flush()`.
  - 4 new unit tests, one per backend.
- Estimated diff: ~200 lines code, ~120 lines test.

### 11.4 `feat(streamdeck): add DRA rect-addressable strip update for AKP05/Mirabox N4`

- Files touched:
  - [`src/devices/streamdeck/src/akp05_protocol.hpp`](../../src/devices/streamdeck/src/akp05_protocol.hpp)
    — add `CmdSecondaryScreen = {'D','R','A'}`,
    `buildSecondaryScreenHeader(QRect zone, uint32_t jpegSize, uint8_t location)`.
  - [`src/devices/streamdeck/src/akp05.cpp`](../../src/devices/streamdeck/src/akp05.cpp)
    — implement `setSecondaryScreenImage(QRect zone, std::span<uint8_t const> rgba, uint16_t width, uint16_t height)`;
    branch from `setMainImage` for partial updates.
  - [`src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`](../../src/devices/streamdeck/src/)
    — add `cropAndEncodeJpeg(QImage source, QRect zone, int quality) → QByteArray`.
  - [`src/core/include/ajazz/core/capabilities.hpp`](../../src/core/include/ajazz/core/capabilities.hpp)
    — new `ISecondaryScreenCapable` mix-in.
  - `src/core/include/ajazz/streamdeck/streamdeck.hpp` — wire the mix-in
    into the AKP05 type.
  - 3 new unit tests (rect → JPEG, header builder, full-encode → wire).
- Estimated diff: ~300 lines code, ~200 lines test.

### 11.5 `refactor(mouse): rewrite aj_series.cpp wire format per AJ159 vendor RE`

- Files touched:
  - [`src/devices/mouse/src/aj_series.cpp`](../../src/devices/mouse/src/aj_series.cpp)
    — wholesale rewrite. Drop `kCmdDpi/Lod/Button/Rgb/Battery/Commit`
    enum; replace with vendor-correct opcodes
    `kCmdProfile = 0x05, kCmdPollRate = 0x04, kCmdLed = 0x07,
    kCmdMouseKeyMatrix = 0x50, kCmdMouseFnMatrix = 0x51,
    kCmdMouseOption0 = 0x53, kCmdMouseOption1 = 0x54,
    kCmdMacroBody = 0x16, kCmdGetVersion = 0x80`.
  - Implement `makeEnvelope` with parameterised `CheckSumType { Bit7, Bit8, None }`;
    default to `Bit7` for AJ159; expose factory choice.
  - Bump `dpiStageCount()` 6 → 8.
  - Build the **omnibus 0x53 packet** for setLOD/setSensitivity/setSleepTime/
    setBatteryLed/setLogoLed all going through a single
    `AjSeriesOptionPacket` helper struct.
  - Implement `firmwareVersion()` via opcode `0x80`.
  - Implement `setActiveDpiStage()` via opcode `0x54` (with the full DPI
    table re-uploaded atomically — vendor pattern).
  - Implement `setPollRateHz()` via opcode `0x04` with `_RateToNum` lookup
    (`{125→0x08, 250→0x04, 500→0x02, 1000→0x01, 2000→0x84, 4000→0x82, 8000→0x81}`).
  - Implement `setRgbStatic`/`setRgbEffect` via opcode `0x07` 8-byte
    packet (effect type at byte 1, brightness at byte 3, RGB at bytes 5-7).
  - Remove `setRgbBrightness()` (no standalone opcode — folds into
    byte 3 of the 8-byte light packet).
  - `setButtonBinding` via opcode `0x50`, payload at byte 8 (not 4).
- Files touched:
  - `tests/unit/devices/mouse/test_aj_series_wire_format.cpp` —
    byte-level assertion suite. NEW; ~400 lines.
  - [`docs/_data/devices.yaml`](../../docs/_data/devices.yaml) — bump
    `ajazz_24g_8k`, `aj_series_wired_primary` (× 4 siblings),
    `aj199_family` (× 2 siblings) to `scaffolded` with `feature_summary.works`
    populated honestly.
- This is the largest single commit in the roadmap. Estimated diff:
  ~600 lines code, ~400 lines test.
- Honesty gate: commit message body explicitly states "wire format is now
  vendor-correct per `aj_series_vendor.md` but untested against real
  hardware; promotion `scaffolded → partial` requires real-device witness".

______________________________________________________________________

## Appendix A — Vendor opcode index

Quick-reference cross-index of every vendor opcode referenced above, with
the file:line citation back to the RE doc and our backend gap status.

### Stream Dock (AKP family)

| Op | Bytes | Vendor doc | Our status |
|----|-------|-----------|-----------|
| `LIG` | `4C 49 47` | [akp05_vendor.md:182](../protocols/streamdeck/akp05_vendor.md) | ✅ shipping |
| `CLE` | `43 4C 45` | [akp05_vendor.md:183](../protocols/streamdeck/akp05_vendor.md) | ✅ shipping |
| `STP` | `53 54 50` | [akp05_vendor.md:184](../protocols/streamdeck/akp05_vendor.md) | ✅ shipping (without page-magic byte 9 — only matters for legacy 321D/0108D/H3) |
| `VER` | `56 45 52` | [akp05_vendor.md:185](../protocols/streamdeck/akp05_vendor.md) | 🟡 §2.1 |
| `LOG` | `4C 4F 47` | [akp05_vendor.md:186](../protocols/streamdeck/akp05_vendor.md) | 🟡 §3.7 |
| `BAT` | `42 41 54` | [akp05_vendor.md:187](../protocols/streamdeck/akp05_vendor.md) | ✅ shipping |
| `ENC` | `45 4E 43` | [akp05_vendor.md:188](../protocols/streamdeck/akp05_vendor.md) | ✅ shipping |
| `MAI` | `4D 41 49` | [akp05_vendor.md:189](../protocols/streamdeck/akp05_vendor.md) | ✅ shipping |
| `DRA` | `44 52 41` | [akp05_vendor.md:190](../protocols/streamdeck/akp05_vendor.md) | 🔴 §2.2 |
| `M_V` | `4D 5F 56` | [akp05_vendor.md:191](../protocols/streamdeck/akp05_vendor.md) | 🟡 §3.8 (needs capture) |
| `QUCMD` | `51 55 43 4D 44` | [akp05_vendor.md:192](../protocols/streamdeck/akp05_vendor.md) | 🟡 §2.3 |
| `ULEND` | `55 4C 45 4E 44` | [akp05_vendor.md:193](../protocols/streamdeck/akp05_vendor.md) | 🔴 §1.5 |
| `GIFVER` | `47 49 46 56 45 52` | [akp05_vendor.md:194](../protocols/streamdeck/akp05_vendor.md) | 🟡 §3.9 (needs capture) |
| `CRT CLE DC` | sentinel | [akp05_vendor.md:195](../protocols/streamdeck/akp05_vendor.md) | not needed (StreamDock-300 only) |

### AK980 PRO

| Op | Sub | Vendor doc | Our status |
|----|-----|-----------|-----------|
| `0x01` | — | [ak980pro_vendor.md:192](../protocols/keyboard/ak980pro_vendor.md) | ✅ shipping |
| `0x02` | — | [ak980pro_vendor.md:193](../protocols/keyboard/ak980pro_vendor.md) | ✅ shipping (RTC save) |
| `0x07 0x10` | settings batch | [ak980pro_vendor.md:198](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.7 |
| `0x09 0x1C` | macro body | [ak980pro_vendor.md:199](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.11 |
| `0x0B 0x1C` | lighting params | [ak980pro_vendor.md:201](../protocols/keyboard/ak980pro_vendor.md) | 🔍 partial (we have brightness) |
| `0x10 / 0x12` | keymap chunked | [ak980pro_vendor.md:205-206](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.10 |
| `0x13` | RGB mode | [ak980pro_vendor.md:207](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.6 |
| `0x14` | macro assign | [ak980pro_vendor.md:208](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.11 |
| `0x17` | sleep timer | [ak980pro_vendor.md:209](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.7 |
| `0x18` | CMD_START | [ak980pro_vendor.md:210](../protocols/keyboard/ak980pro_vendor.md) | ✅ shipping |
| `0x20 0x01` | battery | [ak980pro_vendor.md:211](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.5 / commit §11.2 |
| `0x20 0x04` | per-key RGB | [ak980pro_vendor.md:212](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.8 |
| `0x23` | macro record buffer | [ak980pro_vendor.md:213](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.11 |
| `0x28` | CMD_TIME | [ak980pro_vendor.md:215](../protocols/keyboard/ak980pro_vendor.md) | ✅ shipping (ARCH-05.1) |
| `0x7F` + `0x80\|n` | TFT image | [ak980pro_vendor.md:216-217](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.9 (DISPLAY-05) |
| `0xF0` | CMD_FINISH | [ak980pro_vendor.md:218](../protocols/keyboard/ak980pro_vendor.md) | 🟡 §2.6 (envelope closer) |
| `0xF5` | RGB read-back | [ak980pro_vendor.md:219](../protocols/keyboard/ak980pro_vendor.md) | 🟡 (deferred — read-back is low UX priority) |

### AJ159 APEX

| Op | Vendor doc | Our status |
|----|-----------|-----------|
| `0x04` poll rate | [aj_series_vendor.md:159, 168-176](../protocols/mouse/aj_series_vendor.md) | 🟡 §5 / §11.5 (wrong byte encoding today) |
| `0x05` profile | [aj_series_vendor.md:153, 154](../protocols/mouse/aj_series_vendor.md) | 🟡 §2.13 / §5 |
| `0x07` LED params 8-byte | [aj_series_vendor.md:179-209](../protocols/mouse/aj_series_vendor.md) | 🔴 §5 / §11.5 (wrong opcode + wrong layout today) |
| `0x16` macro chunked | [aj_series_vendor.md:300-322](../protocols/mouse/aj_series_vendor.md) | 🟡 §2.11 |
| `0x25` / `0x29` TFT LCD | [aj_series_vendor.md:328-344](../protocols/mouse/aj_series_vendor.md) | 🟡 §3.6 |
| `0x40-0x41 / 0xC0-0xC1` mouse-MCU OTA | [aj_series_vendor.md:345-355](../protocols/mouse/aj_series_vendor.md) | 🟡 §3.3 |
| `0x50` `MOUSE_SET_KEYMATRIX` | [aj_series_vendor.md:220-221](../protocols/mouse/aj_series_vendor.md) | 🔴 §1.1 / §5 / §11.5 (we currently call this "commit" — corrupts data) |
| `0x53` omnibus | [aj_series_vendor.md:227-228, 262-295](../protocols/mouse/aj_series_vendor.md) | 🔴 §2.15 / §5 / §11.5 (we use 4 separate wrong opcodes today) |
| `0x54` DPI table | [aj_series_vendor.md:229-230, 236-261](../protocols/mouse/aj_series_vendor.md) | 🔴 §5 / §11.5 (wrong opcode + wrong endian today) |
| `0x60` / `0x55` recoil | [aj_series_vendor.md:231-233](../protocols/mouse/aj_series_vendor.md) | ⛔ §4.6 anti-feature |
| `0x80` GET_REV | [aj_series_vendor.md:149](../protocols/mouse/aj_series_vendor.md) | 🟡 §11.5 |

______________________________________________________________________

## Appendix B — Anti-feature checklist (for the security-conscious reader)

When reviewing future PRs that touch device backends or settings, this
section is the canonical "should we add this?" reference. Match the
proposed change against the list:

- [ ] Network call to a vendor cloud → §4.2, §4.3, §4.10, §4.11
- [ ] Background telemetry / analytics → §4.4, §4.5
- [ ] System-wide keyboard hook without opt-in → §4.12
- [ ] In-process gRPC / TCP listener exposing device control → §4.1
- [ ] Spawning external binaries downloaded over HTTP → §4.8
- [ ] Embedded KV/SQL store for trivial settings → §6.4
- [ ] Recoil-control / rapid-fire mouse macro → §4.6
- [ ] Custom UI toolkit / `LoadLibrary` tricks → §4.13, §4.14
- [ ] Polling loops where event subscriptions exist → §4.9, §6.5

If any box is checked, the PR description must cite the deviation and
explain why this case is different. Anti-feature exceptions require
maintainer review (CLAUDE.md "Hard rules" section).

______________________________________________________________________

*This document is a research synthesis, not a planning artefact. It will
not be amended via `/gsd-*` workflows; it should be re-generated when a
substantive new vendor RE doc lands. The next 5 commits in §11 carry the
actionable next steps; everything else feeds into v1.2.x and v1.3
milestone planning.*

______________________________________________________________________

## 12. Deep RE pass 2026-05-17 — additional findings

A second, **byte-level** RE pass landed 13 new docs on 2026-05-17 (3
commits: `fdd5e48` Stream Dock, `bf869ac` AK980 PRO, `37bea86` AJ159
mouse). Of these, **5 contradict** the original §11.1–§11.5 commit
sequence in this document and force a re-ordering. The corrections, new
opportunities, and contradictions are catalogued below. The concrete
execution plan that results from this section lives in
[`docs/research/phase3-patch-sequence.md`](./phase3-patch-sequence.md).

### 12.1 New P0 corrections beyond the original §11.1–§11.5

#### 12.1.1 AK980 PRO settings batch (0x07 0x10) byte map was WRONG

**Source**: [`ak980pro_vendor.md` §13.2](../protocols/keyboard/ak980pro_vendor.md).

Re-deriving from `FUN_00414290` with proper stack-offset analysis, the
prior §2.7 proposal had bytes 5/6/7 carrying `fn_switch`/`sleep_time`/
`key_respondtime` — that was wrong. **Bytes 5/6/7 carry the disable
Win-key / Alt-F4 / Alt-Tab flags**; the real `fn_switch` is at byte 9,
`sleep_time` at byte 10, `key_respondtime` at byte 12. Byte 8 is BOTH a
disable-key flag (Alt-Tab) **and** the checksum slot (overwritten last).
Bytes 18–19 are the `0xAA 0x55` trailer.

If we had landed §2.7 / §11.x with the prior byte map, every settings
write would have silently set Win-key / Alt-F4 / Alt-Tab disable flags
that the UI never asked for. **Containment via documentation only** —
no in-tree code yet implements this opcode.

#### 12.1.2 AK980 PRO per-key RGB (0x20 0x04): 192 B wired (3 chunks of 64), 512 B wireless (8 chunks of 64) — NOT 6 chunks

**Source**: [`ak980pro_perkey_rgb_protocol.md` §3](../protocols/keyboard/ak980pro_perkey_rgb_protocol.md) +
[`ak980pro_vendor.md` §13.11](../protocols/keyboard/ak980pro_vendor.md).

The prior §2.8 said "0xC0 (wired) / 0x200 (wireless) RGB blob". Both
sizes are correct. The chunk count was implicit ("60-byte chunked
upload" inherited from `CmdSetRgbBuffer = 0x0a`) and would have produced
**wrong chunking**. Vendor uses **64-byte FEATURE-report slices** via
`FUN_0044f0c0`: wired = **3 chunks × 64** = 192 B, wireless = **8 chunks
× 64** = 512 B. Each on-wire chunk has NO opcode prefix — header packet
(step 1) sets context, raw blob (step 2) follows, CMD_SAVE 0x02
(step 3) closes.

**Wired path is monochromatic** (1 byte per LED, indexed by
`light_index`), not full RGB. To get arbitrary per-key colors on the
wired interface we must layer `0x08` zone-color + `0x20 0x04`
intensity. **Wireless is full 4-byte-per-LED [reserved=0, R, G, B]
slots**, 128 LEDs addressable (5 slots unused beyond light_index 123).

#### 12.1.3 AK980 PRO TFT bulk-upload opcode 0x72 is 143× faster than 0x7F path — change priority

**Source**: [`ak980pro_tft_protocol.md` §4-§5](../protocols/keyboard/ak980pro_tft_protocol.md) +
[`ak980pro_vendor.md` §13.10](../protocols/keyboard/ak980pro_vendor.md).

The prior §2.9 proposed the chunked `0x7F 0x03 + 0x80|n` path, costing
**~648 s (10.8 min)** for a 140-frame GIF. The deep pass found
`FUN_00422920` uses an alternate **bulk path** with **opcode 0x72**
(`CMD_SCREEN_BEGIN_BULK`) plus 4097-byte chunks via `FUN_0044f2d0` —
**~4.5 s for the same 140-frame GIF**.

Promote TFT image upload from `v1.2.x` deferred to **P0 v1.2** in
phase3 sequence. Implement both paths: bulk by default, chunked as
fallback when firmware rejects 0x72.

#### 12.1.4 AK980 PRO Report ID 0x00 vs 0x04 transport — defer change

**Source**: [`ak980pro_vendor.md` §13.1](../protocols/keyboard/ak980pro_vendor.md).

The deep pass shows the vendor uses **two distinct HID transports**:
- `FUN_0044f5f0` → `WriteFile` (HID OUTPUT, 33 B reports), framing
  `[ReportID=0x00, opcode, sub, payload, …, checksum@8]` — opcode at
  on-wire byte 1.
- `FUN_0044eed0` → `HidD_SetFeature` (HID FEATURE, 65 B reports),
  framing `[ReportID=0x00, 0x04, opcode, sub, payload, …]` — opcode at
  on-wire byte 2; the **0x04 byte is a frame-magic, NOT the Report ID**.

Our `proprietary_protocol.hpp:23` defines `ReportId = 0x04`. Empirically
the AK980 PRO setTime path against gohv + KyleBoyer works with 0x04 as
the first byte. Either interpretation is consistent on the wire — our
HID stack prepends a Report ID byte; either 0x00 (correct) or 0x04
(incorrect but the firmware tolerates it) lands the rest of the bytes
in the right places.

**DECISION**: defer the change pending hardware test against the
**FEATURE-report** opcodes (0x13, 0x17, 0x18, 0x02, 0xF0). No observed
bug; risk of changing what works > value of theoretical correctness.
Captured as `[needs capture]` for negative validation.

#### 12.1.5 AK980 PRO transport selection per opcode (TRANSPORT MATRIX)

**Source**: [`ak980pro_vendor.md` §13.11 last table](../protocols/keyboard/ak980pro_vendor.md).

Each opcode must use the matching transport — firmware enforces this:

| Opcode group | Transport | Helper |
| ------------ | --------- | ------ |
| 0x07 0x10 (settings), 0x09 0x1C (macro wired), 0x0B 0x1C (lighting), 0x14 0x1C (macro assign), 0x20 0x01 (battery), 0x7F 0x03 + 0x80\|n (TFT chunks) | OUTPUT 33 B | `FUN_0044f5f0` |
| 0x18 / 0x28 / 0xF0 (envelope verbs), 0x02 (SAVE), 0x13 (RGB mode), 0x17 (sleep timer), 0x19 / 0x15 (wireless macro), 0x72 (TFT bulk begin) | FEATURE 65 B | `FUN_0044eed0` |
| 0x20 0x04 (per-key RGB write) | FEATURE 65 B chunked | `FUN_0044f0c0` |
| bulk 4097-byte writes | OUTPUT 4097 B | `FUN_0044f2d0` |
| 0xF5 0x03 / 0xF5 0x09 (per-key RGB readback) | OUTPUT + streaming read | `FUN_0044f3a0` |

For our backend, this means `ITransport` needs a clear distinction
between `write()` (OUTPUT path, hidapi `hid_write`) and `writeFeature()`
(`hid_send_feature_report`). Audit existing usages in
`proprietary_keyboard.cpp` to ensure each opcode goes through the right
side.

#### 12.1.6 AK980 PRO CMD_FINISH 0xF0 missing from every commit envelope

**Source**: [`ak980pro_vendor.md` §13.7, §13.11](../protocols/keyboard/ak980pro_vendor.md).

Every multi-packet envelope ends with **opcode 0xF0 (CMD_FINISH)**.
Confirmed in three places: `FUN_0042b0a0` (RGB mode commit),
`FUN_004340c0` (LCD mode commit), `FUN_0044b910` (macro upload close).
Our ARCH-05.1 `setTime` envelope omits this — the firmware tolerates
it but the vendor pattern is to always close with 0xF0. Pure additive
fix; no behaviour change expected.

#### 12.1.7 AK980 PRO LCD-aware time-sync uses 0x0C 0x10 (alternate opcode)

**Source**: [`ak980pro_vendor.md` §13.8](../protocols/keyboard/ak980pro_vendor.md).

`FUN_00423a10` (LCD-aware variant) uses opcode `0x0C 0x10` — a single
33-byte FEATURE packet via `FUN_0044f5f0`, NOT the 4-packet 0x28
envelope. This is the path that **should** drive the on-keyboard TFT
time display on AK980 PRO (which HAS an LCD). Our ARCH-05.1 currently
uses the multi-packet 0x28 path — that works (the firmware accepts
both), but the 0x0C 0x10 single-packet form is the canonical vendor
choice. Defer; document as future improvement.

#### 12.1.8 AK980 PRO wireless macro upload uses 0x19 0x04 + 0x15 0x04 (NEW opcodes)

**Source**: [`ak980pro_vendor.md` §13.9](../protocols/keyboard/ak980pro_vendor.md) +
[`ak980pro_macros_protocol.md` §5](../protocols/keyboard/ak980pro_macros_protocol.md).

Wired macro upload uses `0x09 0x1C` (28-byte chunks via OUTPUT 33 B).
Wireless uses a **different envelope**: `0x19 0x04` (begin) + `0x15 0x04`
(chunk info, byte 8 = chunk_count) + bulk body via `FUN_0044eed0` (auto-
chunks at 64-byte boundaries) + `0x02 0x04` (save). Add to opcode table.

#### 12.1.9 AK980 PRO macro mouse-remap footgun

**Source**: [`ak980pro_macros_protocol.md` §3.2](../protocols/keyboard/ak980pro_macros_protocol.md).

The DB-side `t_key_otherdata.value` field uses values `1=Left, 2=Right,
3=Middle`. The on-wire byte uses **HID button bitmask** values
`0x01=Left, 0x02=Middle, 0x04=Right`. **2 ↔ 3 swap.** A naïve port that
forwards `value` straight to the wire byte breaks Right + Middle click
recording. Must implement a `macroMouseDbToWire()` remap (one-line
constexpr per macros_protocol.md §9.1) and write a dedicated test.

#### 12.1.10 AJ-series mouse: `hid_write` (interrupt-OUT), NOT `writeFeature()`

**Source**: [`aj_series_vendor.md` "Other corrections" §1](../protocols/mouse/aj_series_vendor.md).

The renderer's `writeFeatureCmd` wrapper at `js:726774` calls `no()`
(`sendMsg` wrapper at `js:56601`) — which uses **interrupt-OUT** (HID
output report). `sendRawFeature` (= `hid_send_feature_report`) is only
used by `writeRawFeatureCmd`, which the mouse path doesn't call.

The prior §11.5 rewrite proposal said "writeFeature". **Use `write()`
(interrupt-OUT) instead.** Concrete: `ITransport::write()` not
`writeFeature()`. The `ITransport` abstraction in our tree maps this to
`hid_write` on the hidapi backend. Apply correction in P0.5.

#### 12.1.11 AJ-series mouse: BIT7 checksum confirmed by 98 call-site census

**Source**: [`aj_series_vendor.md` "deep-dive appendices" §Critical finding](../protocols/mouse/aj_series_vendor.md) +
[`aj_series_opcode_table.md` §1 "Hard fact: CheckSumType.BIT7"](../protocols/mouse/aj_series_opcode_table.md).

The renderer census found `Wn.CheckSumType.BIT7` (or numeric `0` =
BIT7) in **98 distinct call sites** and **zero** BIT8 call sites for
mouse-class opcodes. The `BIT8` enum value is reserved for legacy non-
AJAZZ VID SKUs (VKMS / akko / rongyuan).

**UNBLOCKS P0.5 rewrite** — we no longer need a USBPcap capture to
confirm checksum mode for the wire-format rewrite. `& 0x7F` is the
right answer for every AJ-series PID.

#### 12.1.12 AJ-series mouse: 8th DPI stage colour byte is wire-quirk (collides with checksum)

**Source**: [`aj_series_vendor.md` "Other corrections" §3](../protocols/mouse/aj_series_vendor.md) +
[`aj_series_opcode_table.md` §3.10 "Edge case on byte 63"](../protocols/mouse/aj_series_opcode_table.md).

The 0x54 DPI-table packet has `byte 40..63 = 8 stages × 3 RGB bytes`.
Byte 63 (= 40 + 23 = 8th-stage B-channel) **also is the checksum slot
overwritten by the iot_driver after the renderer writes its bytes**.
Net effect: the 8th DPI stage's B-channel colour is destroyed on the
wire. The QML editor should grey out the 8th-stage colour swatch (or
hide the 8th stage entirely, since firmware likely only uses 7
visible stages anyway).

#### 12.1.13 Stream Dock WinUSB framing — one byte further left than hidapi (no Report ID)

**Source**: [`akp05_init_sequence.md` §3.4](../protocols/streamdeck/akp05_init_sequence.md) +
[`akp05_vendor.md` §14.1](../protocols/streamdeck/akp05_vendor.md).

The framing diagram in `akp05_vendor.md` §2 describes the **hidapi**
packet layout (Report ID at byte 0, `CRT` at bytes 1..3, opcode at byte
6). The **WinUSB** layout has **no report-ID byte** — `CRT` at bytes
0..2, opcode at byte 5, payload at byte 8.

This only matters for the WinUSB-class touch-strip channel on AKP05 /
Mirabox N4 family (and the N4 Pro). Our current AKP05 backend uses
hidapi for the touch-strip; if/when we move to WinUSB for higher
bandwidth, we must shift opcode offsets one byte left. **Containment
flag**: add a comment in `akp05_protocol.hpp` documenting the offset
delta before WinUSB lands.

#### 12.1.14 Stream Dock SDPluginServer binds 0.0.0.0 (security)

**Source**: [`akp05_init_sequence.md` §5](../protocols/streamdeck/akp05_init_sequence.md) +
[`akp_plugin_sdk.md` §4.1 + §9](../protocols/streamdeck/akp_plugin_sdk.md) +
[`akp05_vendor.md` §14.1](../protocols/streamdeck/akp05_vendor.md).

Vendor binds `QHostAddress::Any` on both WebSocket and TCP servers —
all interfaces, including LAN-facing. Browser-side JS shim hardcodes
`ws://127.0.0.1:<port>`, so the *intended* attack surface is local-only,
but the C++ listener accepts LAN connections.

**For our implementation: bind to `QHostAddress::LocalHost` only.**
This is an anti-feature (§4.x) we must NOT replicate. SDPluginServer
spec (§3.1) updates to require LocalHost binding + the salt/challenge
auth (`hello` → `passHello` with salt, SHA-256 challenge) per
`akp_plugin_sdk.md` §4.5.

### 12.2 New P1/P2 features unlocked by deep RE

#### 12.2.1 12 default plugins (not 11)

**Source**: [`akp_plugin_sdk.md` §1](../protocols/streamdeck/akp_plugin_sdk.md).

Prior pass said "11 default plugins"; deep pass found **12**. The 12th
is `mkey.com.mirabox.streamdock.calendar.sdPlugin` — a K1Pro-keyboard-
specific variant gated by `IsK1Pro: true` on the action.

Plus ~50 built-in UUIDs handled in-process (page nav, profile nav,
brightness, hotkey, multimedia, OBS, YouTube, vMix, system monitor,
plain text, etc.) — covered by the SDPluginServer in §3.1.

#### 12.2.2 39-action WebSocket surface (Elgato 13 + AJAZZ 26 extensions)

**Source**: [`akp_plugin_sdk.md` §4.3 + §4.4](../protocols/streamdeck/akp_plugin_sdk.md).

Prior §5.3 of `akp05_vendor.md` listed ~16 actions; deep pass enumerated
**39 distinct plugin→host actions**, of which 26 are AJAZZ-only
extensions (`setBG`, `openTouchbarSecondaryMenu`, `enterGatheringEvent`,
`sendToDevice`, `setBackground`, `clearIcon`, `getScreenshot`,
`getSystemAudioVolume`, `setAcImgTop`, `lockScreen`/`unLockScreen`,
`getUserInfo`/`sendUserInfo`, `startAudioCapture`/`stopAudioCapture`,
`onSwitchToFolderProfile`, `exitFullScreen`, `touchTap`,
`getDetectedSensorsData`, `deleteAction`, `stopBackground`,
`registrationScreenSaverEvent`, `setText`, etc.).

Plus host→plugin events: 13 Elgato-standard + ~6 AJAZZ-specific
(`passHello` with auth challenge, `keyDownCord`/`keyUpCord`,
`deleteAction`, `lockScreen`/`unLockScreen` mirror).

Update §3.1 SDPluginServer spec: 39-action surface, not 13.

#### 12.2.3 96 Stream Dock codenames (vs 50 prior — register.cpp expansion)

**Source**: [`akp_device_matrix.md` §§2-11](../protocols/streamdeck/akp_device_matrix.md).

Prior pass had ~50 codenames in `akp05_vendor.md` §1.3. Deep pass
catalogued **96 distinct codenames** across 7 silicon families (V1, V2,
V25, V3, N1, N3, N4). Notable additions:

- AKP05 V25 variants: `AKP05V25`, `AKP05EV25`, `AKP05RV25`
- Mirabox N4 Pro family (vibration + per-key RGB): `MBox-N4Pro`,
  `MBox-N4ProE`, `N4Pro`, `N4ProE`, `MSDPRO`, `MSDNEO`, `VSDN4Pro`
- Mirabox N6 (6-key sibling): `MBox-N6`
- ~30 OEM rebadges: `Streamplify`, `DarkFlash`, `IYUT_D15`, `Womier_D15`,
  `SANWA`, `TOS300`, `MOLA`, `BRHubN4`, `BRHubN4Pro`, etc.
- Keyboard-form SKUs with embedded LCD: `K1Pro`, `K1ProUS`, `K1ProZH`,
  `Create_Pro`, `K-992`, `M18*` family

Expansion target for `src/devices/streamdeck/src/register.cpp` — purely
additive (new codename strings + sibling PIDs pointed at existing
backend factories).

#### 12.2.4 41 mouse PIDs + 6 partner VIDs (vs ~8 prior — register.cpp expansion)

**Source**: [`aj_series_device_matrix.md` §1 + §7](../protocols/mouse/aj_series_device_matrix.md).

Prior pass had ~8 PIDs; deep pass found **41 active AJAZZ-VID (0x3151)
PIDs + 19 bootloader PIDs**, plus **6 partner VIDs** that ship the same
iot_driver (Sino Wealth `0x25aa`, akko `0x347a`, VKMS `0x374a`,
MagneticJade `0x3794`, rongyuan `0x342d`, Mad Catz `0x0738`, plus AK980
PRO's Microdia `0x0c45`).

Priority promotions for `src/devices/mouse/src/register.cpp`:
- `aj159_apex_wired` = `0x3151:0x5008`
- `aj159_apex_24g` = `0x3151:0x4026`
- `aj159_apex_dongle` = `0x3151:0x4027` (dongle_common — exposes BOTH
  keyboard + mouse children)
- AJ179 APEX shares PIDs (`0x5008` + `0x4026`) — same wire format

DO NOT promote partner-VID SKUs without explicit consent (trademark
risk per `aj_series_device_matrix.md` §7.1 note).

#### 12.2.5 AK980 PRO host-side 9 lighting effects (CustomLightMode)

**Source**: [`ak980pro_mui_dll.md` §3](../protocols/keyboard/ak980pro_mui_dll.md).

The vendor's `CustomLightMode` class (67 methods, largest app class)
implements **9 host-side lighting effects** entirely in C++ on the host
and then pushes cooked per-key RGB via opcode `0x20 0x04`:
`InitLightStar` (Starlight), `InitLightRain` (Fluttering),
`InitFlowerEffect` (Colorful Fountain), `InitBreathEffect` (Dynamic
Breathing), `InitSpringEffect` (Rainbow Wave), `InitVerticalEffect`
(Following Currents), `InitRollEffect` (Peak Revolving),
`InitLightRotate` (Static On variants).

Distinct from the **20 firmware-rendered modes** (opcode `0x13`). The
keyboard MCU does NOT run the 9 host-side effects — host must compute
60 fps frames and stream via `0x20 0x04` chunks.

Cross-cutting design implication: a new `LightingEffectsService` Qt6
service that owns 9 effect compositors + a per-key 144-slot
`LightingMatrix` model + the upload worker. This is a non-trivial new
subsystem (~1500 LOC) deferred to v1.3.

#### 12.2.6 AK980 PRO TFT bulk 0x72 path (143× faster — see §12.1.3)

Covered above. Top priority for `IDisplayCapable` extension to the
keyboard family.

### 12.3 Contradictions to resolve

| # | Contradiction | DECISION | RATIONALE |
| - | ------------- | -------- | --------- |
| C-1 | Settings-batch byte map: §2.7 wrong vs §13.2 right | USE §13.2 byte map | Deep RE has stack-offset evidence; §2.7 was inferred from incomplete decompile. No in-tree implementation yet → no migration risk |
| C-2 | Per-key RGB chunk count: "60-byte chunked" (§2.8) vs "64-byte chunked" (§13.11) | USE 64-byte chunks via `FUN_0044f0c0` | Deep RE explicitly traced `FUN_0044f0c0` chunking loop; "60-byte" was inherited from a different opcode (0x0a CmdSetRgbBuffer, NOT used on AK980 PRO) |
| C-3 | Report ID 0x00 vs 0x04 (§13.1 says 0x00 + frame magic 0x04; our `proprietary_protocol.hpp:23` says ReportId=0x04) | **DEFER** | gohv/KyleBoyer/our impl all empirically work with 0x04 prefix. Either interpretation lands the same bytes if the HID stack-prepend behaves consistently. No observed bug. Capture-validate before changing |
| C-4 | Mouse transport: `writeFeature` (§11.5) vs `hid_write` (§12.1.10 / aj_series_vendor.md §1) | USE `hid_write` (interrupt-OUT, `ITransport::write()`) | Renderer-side wrapper census proves mouse path uses `sendMsg` not `sendRawFeature`. Apply in P0.5 rewrite |
| C-5 | DPI stage count: §2.14 says 8 stages; §12.1.12 says 8th stage colour B-byte is destroyed by checksum | EXPOSE 8 stages, GREY OUT 8th colour swatch | Firmware accepts 8 stages but truncates 8th-stage RGB byte; UI must reflect this |
| C-6 | Stream Dock listener binding: §3.1 / §5.1 said `127.0.0.1`; deep pass §14.1 says `QHostAddress::Any` | OUR IMPL: bind LocalHost ONLY | Anti-feature §4.x — vendor footgun; we must NOT replicate. Add to anti-feature checklist |
| C-7 | Stream Dock framing for WinUSB: §2 hidapi layout vs §14.1 WinUSB layout (one byte left, no ReportID) | DOCUMENT NOW, IMPLEMENT LATER | Only affects WinUSB touch-strip channel which we don't yet wire. Add comment to `akp05_protocol.hpp` so the next implementer doesn't miss it |
| C-8 | TFT upload path priority: §2.9 (`v1.2.x`, 0x7F) vs §12.1.3 (P0 v1.2, 0x72 bulk) | PROMOTE TO P0 v1.2 via 0x72 bulk | 143× speedup justifies promotion; chunked 0x7F stays as fallback |
| C-9 | LCD-aware time-sync uses 0x0C 0x10 (§13.8) vs 4-packet 0x28 envelope (our current impl) | KEEP 0x28, DOCUMENT 0x0C 0x10 AS FUTURE | Our 0x28 envelope works; switching opcodes is high-risk for zero user-visible benefit |
| C-10 | "11 default plugins" (prior planning) vs "12 default plugins" (§12.2.1) | UPDATE to 12 | Off-by-one corrected; the 12th is the `mkey.*` K1Pro variant |
| C-11 | "Default plugins" vs "built-in UUIDs": ~50 built-in UUIDs handled in-process | DOCUMENT BOTH; SDPluginServer surfaces both | The ~50 built-in UUIDs are the page-nav/profile-nav/brightness/etc. actions handled by the host without spawning a plugin process — they don't need .sdPlugin packaging |

### 12.4 Amended §11 commit sequence preview

The original §11 sequence (5 commits, P0 safety + features) was
ordered before the deep RE pass. With the §12.1 corrections, the
sequencing for the **next batch** of commits changes substantially. The
authoritative new plan is
[`docs/research/phase3-patch-sequence.md`](./phase3-patch-sequence.md) —
which targets ~20 commits organised by risk tier (additive →
medium-blast → high-risk rewrites) and defers P1 plugin-host work to a
dedicated milestone.

Quick changes vs §11:

- **§11.1** (mouse safety guard) — **landed** (`00acf5e`)
- **§11.2** (AK980 PRO battery 0x20 0x01) — **landed** (`eea50a5`)
- **§11.3** (Stream Dock VER + ULEND for AKP03/AKP05) — **landed**
  (`24c0965`); AKP153 + AKP815 propagation still queued (P3.7 in
  phase3 sequence)
- **§11.4** (DRA rect-addressable strip) — **landed** (`ef9597b`)
- **§11.5** (AJ-series wire-format rewrite) — **unblocked** by §12.1.11
  (BIT7 confirmed by 98-site census); also requires §12.1.10 transport
  correction (`write()` not `writeFeature()`) + §12.1.12 8th-stage UX
  caveat. Scheduled as **P3.12 (HIGH RISK)** in phase3 sequence

### 12.5 New anti-features identified by deep RE pass

To append to §4:

- **§4.16 Stream Dock WebSocket binding to `QHostAddress::Any`**
  ([`akp05_init_sequence.md` §5](../protocols/streamdeck/akp05_init_sequence.md)).
  Vendor binds all interfaces despite the JS shim only ever connecting
  via 127.0.0.1. We MUST bind `QHostAddress::LocalHost` only.
- **§4.17 Stream Dock plugin-zip auto-unpack without signature
  verification** ([`akp_plugin_sdk.md` §6](../protocols/streamdeck/akp_plugin_sdk.md)).
  Vendor host extracts ZIPs from the plugin store and trusts the
  contents. We MUST require a signature (Ed25519 over the manifest +
  code) before extraction.
- **§4.18 AK980 PRO custom MUI toolkit (mui.dll, 6 604 exports)**
  ([`ak980pro_mui_dll.md` §1-§5](../protocols/keyboard/ak980pro_mui_dll.md)).
  Already covered by §4.13 (anti-feature: custom UI framework). Deep
  pass corroborates: mui.dll reimplements a subset of Qt 6 / QML in
  1.2 MB of MFC-derived code.
- **§4.19 Allwinner SoC USB-upgrade protocol (DFU)** —
  [`akp_dfu_protocol.md`](../protocols/streamdeck/akp_dfu_protocol.md).
  Already covered by §3.2 (defer DFU). Deep pass confirms: the DFU is
  an Allwinner BROM upgrade with AIC.FW + CBW/CSW transport, NOT
  Stream-Deck-protocol code. We MUST detect DFU transition + suspend
  but NEVER reimplement.
- **§4.20 Stream Dock AES-GCM-encrypted firmware blob with hardcoded
  key** ([`akp_dfu_protocol.md` §4](../protocols/streamdeck/akp_dfu_protocol.md)).
  Vendor decrypts downloaded firmware in-app with key
  `a7e61c373e219033c21091fa607bf3b8`. We do not download or decrypt
  firmware — see §4.10 (auto-update CDN).

