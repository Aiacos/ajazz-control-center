# AJAZZ Stream Dock vendor reverse engineering

> Reverse-engineering pass against the official **Stream Dock AJAZZ** Windows
> application (Qt 5.15 + CEF + libusb-1.0 bundle) and its core SDK
> `SDLibrary1.dll`, performed 2026-05-17 using Ghidra 12.1 headless with full
> PDB symbol load (`SDLibrary1.pdb` 15 MB, `Stream Dock AJAZZ.pdb` 69 MB).
> The shipping app version under inspection is from the public
> `Stream-Dock-AJAZZ-Installer_Windows_global.msi`.
>
> **Authoritative artefacts**:
>
> - Ghidra dump (decompiled pseudocode + interesting functions + ASCII
>   strings): `C:\Users\unilo\reverse-eng-workdir\sd-app\ghidra_SDLibrary1_dll.json`
> - PowerShell-extracted ASCII strings: `C:\temp\sdlibrary1_strings.txt`,
>   `C:\temp\streamdock_exe_strings.txt`
> - Java GhidraScript used for the dump:
>   `C:\Users\unilo\reverse-eng-workdir\ExtractStreamDockCalls.java`
>
> Source paths embedded in the binary indicate the upstream project lives at
> `F:\STreamDock\Gitee\11111(1)\11111\src\` on the vendor's developer machine
> (Gitee — the Chinese equivalent of GitHub). Notable source filenames seen
> in QMessageLogger entries: `sddevice.cpp`, `sdgeneraldevice.cpp`,
> `sdwinusb.cpp`, `sddevicewinusb.cpp`, `qtsqlite.cpp`,
> `keychainclass.cpp`, `dataformatconversion.cpp`.

## 1. Inventory

### 1.1 Native components

| Component                       | Stack                              | Role                                     |
| ------------------------------- | ---------------------------------- | ---------------------------------------- |
| `Stream Dock AJAZZ.exe` (42 MB) | Qt 5.15 + CEF (QCefView)           | Main app (window, plugin host, UI, OBS WS) |
| `SDLibrary1.dll` (13 MB)        | Qt 5 Core/Network, libssl, libcurl | Device SDK (HID + WinUSB) — protocol lives here |
| `QCefView.dll` (1.2 MB)         | CEF binding                        | Qt ↔ Chromium Embedded Framework wrapper |
| `FirmwareUpgradeTool.exe`       | Qt 5                               | Separate firmware OTA executable        |
| `ScreenCaptureTool.exe`         | Qt 5 + Win desktop capture         | Helper used by screen-mirror plugins    |
| `StreamDockWatcher.exe`         | Native                             | Hot-plug watcher; launches main app on connect |
| `SplashScreen.exe`              | Native                             | Boot splash window                       |
| `libusb-1.0.dll`                | Bundled                            | NOT used by `SDLibrary1.dll` (no `libusb_*` import sites in Ghidra dump). Almost certainly used only by `FirmwareUpgradeTool.exe` and/or macOS/Linux ports. |
| `libcurl.dll`, `libssl/crypto`  | OpenSSL 1.1                        | HTTPS for cloud version-check + plugin store |
| `Qt5WebSockets.dll`             | Qt                                 | Powers `SDPluginServer` (Elgato-style API) and `OBSWebSocketClient` |

### 1.2 HID transports used by the SDK

Verified via Ghidra import counts on `SDLibrary1.dll`:

| Backend class       | Underlying API                                                    | Notes                                                            |
| ------------------- | ----------------------------------------------------------------- | ---------------------------------------------------------------- |
| `SDDevice`          | **hidapi** (`hid_init`, `hid_open`, `hid_write`, `hid_send_feature_report`, `hid_read_timeout`, `hid_get_input_report`, `hid_enumerate`, `hid_get_feature_report`) | Used for AKP03, AKP153, AKP815, AKP05 (legacy modes), legacy StreamDock[293/295/296/298/321D/0108D/300] models. Two output transports per device: `SDGeneralHidOutputReport` (→ `hid_write`) and `SDGeneralHidFeatureReport` (→ `hid_send_feature_report`). |
| `SDDeviceWinUSB` / `SDWinUSB` | **WinUSB** (`WinUsb_Initialize`, `WinUsb_WritePipe`, `WinUsb_ReadPipe`, `WinUsb_GetDescriptor`, `WinUsb_QueryDeviceInformation`) | Used for higher-bandwidth devices (N4 Pro / N6 / future Stream Dock Plus class with main LCD strips). Endpoint addresses are **discovered at open time**, not hardcoded — the SDK enumerates the descriptor and stores `_pipeIn`, `_pipeOut`, `_pipeControl` (each with `EndpointAddress` + `MaximumPacketSize`). Bulk transfers are auto-chunked to `MaximumPacketSize`. |
| (none — libusb)     | `libusb_*`                                                        | No call sites in `SDLibrary1.dll`. Bundle ships `libusb-1.0.dll` only because `FirmwareUpgradeTool.exe` (separate process) uses it. |

The `SDGeneralReadThread` / `SDGeneralWriteThread` classes wrap the read /
write loops for the hidapi backend; the WinUSB backend has its own
`_writeDataThread` started by `SDDeviceWinUSB::sddeviceWinusbInit`.

### 1.3 Devices recognised by `SDLibrary1.dll`

Full list of device-codename strings (PDB-resolved string constants):

| Family            | Codenames in binary                                                                                                                                  |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| AKP153 family     | `AKP153`, `AKP153E`, `AKP153R`, `TS115`, `HSV293_O`, `HSV293-ARA`, `HSV293S-ARA`, `StreamDock[293]`, `StreamDock[293V2]`, `StreamDock[293S]`, `StreamDock[293SV3]`, `StreamDock[295]` |
| AKP815 family     | `AKP815`, `TS183`                                                                                                                                    |
| AKP05 / N4 family | `AKP05V25`, `AKP05EV25`, `AKP05RV25`, `MBox-N4`, `MBox-N4E`, `MBox-N6`, `N4Pro`, `N4ProE`, `N4V25`, `SD14N4V25`, `TS10N4V25`, `VSDN4`                |
| AKP03 / N3 family | `AKP03`, `AKP03E`, `AKP03R`, `AKP03V25`, `AKP03EV25`, `AKP03RV25`, `MBox-N3`, `MBox-N3E`, `N3-R`, `SD12N3V25`, `TS16N3`, `TS16N3V25`, `VSDN3`        |
| Mirabox N1 family | `MBox-N1`, `MBox-N1E`, `ajazzN1`, `ajazzN1R`, `ajazzN1E`, `SD16N1V25`, `VSDN1`                                                                       |
| Legacy 296/298    | `StreamDock[296]`, `StreamDock[298]`, `HSV298_BE`, `StreamDock2961`, `TS12P8`, `KB8IN1`, `KB8IN1-1`                                                  |
| Legacy 300/321D/0108 | `StreamDock-300`, `StreamDock[321D]`, `StreamDock[0108D]`, `StreamDock0108DCHDevice`                                                              |
| Misc V3 strings   | `TS15B3` (15-key V3 ?), `MBox-N6` (6-key model — possibly new SKU)                                                                                   |

The main exe (`Stream Dock AJAZZ.exe`) loads default profiles for ~50 more
codenames including `OMNI-STREAM`, `OMNIDIAL`, `MSD-ONE`, `MSD-ONEV3`,
`MSD-TWO`, `MSD-TWOV25`, `MSDPRO`, `MSDNEO`, `GK150`, `M18V25`, `M18V3`,
`H1`, `H1V3`, `M3`, `XL`, `D6`, `SS-550`, `SS-551`, `SS-553`, `SS550V3`,
`CX35`, `CX61`, `DK01`, `ZX-554`, `ZX-5539`, `HDKATOV`, `KB-1`, `KB-2`,
`K-992`, `CN001`, `CN002`, `CN003`, `XF-CN001`, `A3506A`, `DUO87`, etc.
These are OEM rebadges sharing the same wire protocol.

### 1.4 PDB-recovered class layout

Most relevant symbols (mangled names in `.rdata`):

- **`SDDevice`** (base) — methods we extracted:
  - `getAllConnectedDevices()`, `findDevice(int vid, int pid)`
  - `openHidDevice(int vid, int pid, QString &serial)`, 2nd overload with
    `(int vid, int pid, QString &serial, int interface, int collection, uchar reportId)`
  - `closeHidDevice()`, `closeHidDeviceOnError(bool, QString)`
  - `startHidReadThread()`, `startHidWriteThread()`, `stopHidReadThread()`, `stopHidWriteThread()`
  - `sendHidFeatureReport()`, `getHidFeatureReport()`, `getHidInputReport()`
  - `getHardwareFirmwareVersion()`, `sendGetHardwareFirmwareVersion()`, `isOld293Version()`
  - **Opcode builders**:
    - `addClearAllCommand()` / `getClearAllCommand()` — emits `CRT CLE … 0xFF`
    - `addClearCommand(uchar idx)` / `getClearCommand(uchar idx)` — emits `CRT CLE … idx`
    - `addFinishCommand()` / `getFinishCommand()` — emits `CRT STP`
    - `getUploadFinishedCommand()` — emits `CRT ULEND`
    - `getQUCMDCommand(uchar, uchar, uchar, uchar, uchar)` — emits `CRT QUCMD p1 p2 p3 p4 p5` (generic 5-byte parameter command)
    - `sendDisconnectCommand()` — emits `CRT CLE … 'D','C'` (StreamDock-300 only)
    - `sendLogoSizeCommand(int size, uchar type)` — emits `CRT LOG <BE32 size> <type>` (firmware boot logo upload)
    - `getSecondaryScreenPicInfo(ImageStruct&)` — emits `CRT DRA …` (touch-strip image header, rect-addressable) or `M_V…` for `location==0x12` (a 1024-byte logo packet)
  - `insertSettings(QList<ImageStruct>&)` — bulk push pipeline
  - `appendSendData(ImageStruct&, bool isLogo)`, `prependSendData(...)` — enqueue to `_writeDataList`
  - `writeData(const uchar*)`, `writeDataToHidDevice()`, `readDataFromHidDevice()`
- **`SDWinUSB`** — WinUSB low-level wrapper, derives some methods to talk via bulk endpoints
- **`SDDeviceWinUSB`** — combines `SDDevice` API with `SDWinUSB` transport. Has the same Clear/Finish/Logo builders, plus `writeToBulkEndpoint(DeviceEndpoint, QByteArray, ulong*)` and `readFromBulkEndpoint(DeviceEndpoint, uchar*, ulong, ulong*)` with `DeviceEndpoint ∈ {ControlEndpoint, InEndpoint, OutEndpoint}`.
- **`SDGeneralReadThread`** / **`SDGeneralWriteThread`** — hidapi I/O threads. The writer enum `SDGeneralHidReport ∈ {SDGeneralHidOutputReport, SDGeneralHidFeatureReport}` selects between `hid_write` and `hid_send_feature_report`. Per-device `_deviceReportId` byte is prepended to each packet (so the on-wire packet is `[reportId] [packet bytes …]`, length `_packetSize + 1`).
- **`SDPluginServer`** — Elgato Stream Deck-compatible WebSocket server (see §5)
- **`SDTcpServer`** / **`SDWebsocketServer`** — both implement `startListen(unsigned short port)`; the plugin host runs both transports.
- **`OBSWebSocketClient`** — built-in OBS Studio integration (`connectToOBSStudioWebSocketServer()`, `disconnectFromOBSStudioWebSocketServer()`)
- **`SimulatedKeyboardEvent`** — host-side keyboard / mouse injection used by HotKey / Hotkey-Sequence plugin actions (`sendKeyDown`, `sendKeyDownEXTENDEDKEY`, `sendKeyDownSCANCODE`, `sendKeyDownUNICODE`, …)
- **`Hook`** — global keyboard hook with `sendKeyDownValue(uint)` / `sendKeyUpValue(uint)`
- **`QtSQLite`** — SQLite wrapper used for plugin settings / profiles persistence
- **`KeyChainClass`** — secure credential store (OS keychain via `QtKeychain`)

### 1.5 The `ImageStruct` write-queue record (per Ghidra)

Verified field layout for `ImageStruct` (used everywhere a packet is queued):

```text
struct ImageStruct {
    DataType dataType;                              // enum tag (see §3.4)
    bool     waitForACK;
    /* padding */
    int32_t  waitForLogoOrScreensaverOrGifFileSizeACKTime;   // = 3000 ms default
    int32_t  waitForLogoOrScreensaverACKTime;                // = 20000 ms
    int32_t  waitForGIFACKTime;                              // = 300000 ms (5 min)
    int32_t  waitForBackgroundImageACKTime;                  // = 20000 ms
    int32_t  waitForVideoACKPacket;                          // = 0x500000 (5 242 880 bytes)
    int32_t  waitForVideoACKTime;                            // = 10000 ms
    int32_t  waitForLastVideoACKTime;                        // = 30000 ms
    int32_t  waitForGIFToJPGACKTime;                         // = 3000 ms
    int32_t  waitForLastGIFToJPGACKTime;                     // = 5000 ms
    int32_t  imageCount;
    QByteArray imageData;
    QRect    imageRect;                              // touch-strip partial-update target
    int32_t  location;                               // == 0x12 → use M_V packet variant
    int32_t  pageNumber;
    QByteArray jsonData;
    QByteArray otherData;                            // used for ULEND trailer
};
```

### 1.6 The `DataType` discriminator

Enumerator values resolved from Ghidra-decompiled assignments:

| Symbol             | Used by                                  |
| ------------------ | ---------------------------------------- |
| `SDClearAll`       | `getClearAllCommand`, `addClearAllCommand` |
| `SDClearOne`       | `getClearCommand`, `addClearCommand`     |
| `SDUploadFinished` | `getFinishCommand`, `addFinishCommand`, `getUploadFinishedCommand` |
| `SDNormalCommand`  | `getQUCMDCommand`, `getLogoSizeCommand`  |
| `SDSleepScreen`    | fallback when `_packetSize < 0x40` (no real packet emitted) |
| `SDImage` / `SDLogo` / `SDBackground` / `SDVideo` / `SDGif` | inferred from `waitFor*ACKTime` field names — these dataTypes drive the write thread's per-type ACK-wait logic. |

## 2. Complete opcode table (verified against `SDLibrary1.dll` Ghidra
       decompilation, 2026-05-17)

Framing for **all** hidapi-backed devices and the AKP05/N3/N4 baseline:

- Byte 0: HID report ID (prepended at `hid_write` time, NOT part of the
  `QByteArray` buffer the builders construct — the `QByteArray` is
  `_packetSize` bytes, then `hid_write` is called with `_packetSize+1`).
- Bytes 1..3 (== buffer offset 0..2): **`'C','R','T'`** prefix (`0x43 0x52 0x54`)
- Bytes 4..5: reserved, zero
- Byte 6 (== buffer offset 5): opcode byte 1
- Byte 7 (== buffer offset 6): opcode byte 2
- Byte 8 (== buffer offset 7): opcode byte 3 (3-byte opcode) OR start of opcode body (5-byte opcodes)
- Byte 9 (== buffer offset 8): payload byte 0 (or opcode byte 4 in 5-byte form)
- Byte 10 (== buffer offset 9): payload byte 1 (or opcode byte 5 in 5-byte form) — **page magic byte on `STP`**
- Byte 11 (== buffer offset 10): payload byte 2
- Byte 12 (== buffer offset 11): payload byte 3
- …  zero pad to `_packetSize`.

> **Offset convention in the table below**: buffer-offset (i.e. NOT counting
> the report-ID byte). This matches both Ghidra's `QByteArray::operator[]`
> indices and our existing in-tree backend code which strips the report ID
> upstream of the protocol layer.

| Opcode (ASCII) | Bytes 5..(7\|9)    | Direction | Payload (offsets after opcode)                                                                                                | UI trigger                                       | Function name (PDB)                                            | Status vs our backend |
| -------------- | ------------------ | --------- | ----------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ | -------------------------------------------------------------- | --------------------- |
| `LIG`          | 5..7 `L I G`       | host→dev  | byte 10 = brightness percent (0..100)                                                                                         | Brightness slider                                | not isolated in dump (likely `getQUCMDCommand`-wrapped per call) but symbol implied | shipping (`buildSetBrightness`) |
| `CLE`          | 5..7 `C L E`       | host→dev  | byte 11 = key index (1..N) or `0xFF` for all keys                                                                              | Clear key icon                                   | `SDDevice::addClearAllCommand`, `addClearCommand`, `getClearAllCommand`, `getClearCommand` | shipping (`buildClearAll`/`buildClearKey`) |
| `STP`          | 5..7 `S T P`       | host→dev  | byte 9 = page-magic (`!`/`"`/`#` = pages 1/2/3) **only** for models `StreamDock[321D]`, `H3`, `StreamDock[0108D]`              | Flush / end of write batch                       | `SDDevice::addFinishCommand`, `getFinishCommand`, `SDWinUSB::*` | shipping (`buildCmdHeader(CmdStop)`) — **gap**: page-magic byte 9 not implemented |
| `VER`          | 5..7 `V E R`       | host→dev  | (no payload)                                                                                                                  | open-time firmware-version probe                 | `SDDevice::sendGetHardwareFirmwareVersion`                     | 🟡 **gap** — we just return "unknown" |
| `LOG`          | 5..7 `L O G`       | host→dev  | bytes 8..11 = big-endian 32-bit total logo size, byte 12 = `0x02` (logo type — splash) or `param2` from caller                | "Custom boot logo / screensaver" upload          | `SDDevice::sendLogoSizeCommand`, `SDWinUSB::getLogoSizeCommand` | 🟡 **gap** — boot logo not exposed |
| `BAT`          | 5..7 `B A T`       | host→dev  | bytes 10..11 = BE16 JPEG byte-length, byte 12 = key index (1-based); followed by raw JPEG split into `_packetSize` chunks    | Set per-key image                                | inferred (per-family builder; e.g. `akp05_protocol.hpp::CmdKeyImage`); the builder for the BAT path lives in app-level code that wraps `appendSendData` | shipping |
| `ENC`          | 5..7 `E N C`       | host→dev  | bytes 10..11 = BE16 size, byte 12 = encoder index (0-based); followed by JPEG chunks                                          | Per-encoder LCD image                            | inferred (akp05 only)                                          | shipping |
| `MAI`          | 5..7 `M A I`       | host→dev  | bytes 10..11 = BE16 size; followed by JPEG chunks                                                                              | Whole-strip main-LCD push (legacy 1-of-N strip)  | inferred (akp05/akp815)                                        | shipping |
| `DRA`          | 5..7 `D R A`       | host→dev  | bytes 8..11 = BE32 size+`0x20`; byte 12 = `location`; bytes 13..14 = BE16 `rect.width`; 15..16 = BE16 `rect.height`; 17..18 = BE16 `rect.x`; 19..20 = BE16 `rect.y`. 32-byte header packet, then JPEG data. | Touch-strip rect-addressable image update (Stream Deck Plus class — partial-update on the strip) | `SDDevice::getSecondaryScreenPicInfo` | 🔴 **gap** — we only do whole-strip `MAI`. **`DRA` enables 4-zone-aligned partial updates without re-uploading the whole 800×480 panel.** This is the *correct* vendor behaviour for AKP05/N4. |
| `M_V` (3-byte) | 0..2 `M _ V` (no `CRT` prefix) | host→dev  | 1024-byte packet, used when `location == 0x12` in `getSecondaryScreenPicInfo`                                                | Boot logo on touch-strip (large image)           | `SDDevice::getSecondaryScreenPicInfo`                          | 🟡 unknown; needs USB capture to confirm full layout (we have header bytes only) |
| `QUCMD` (5-byte) | 5..9 `Q U C M D` | host→dev  | bytes 10..14 = 5 user-supplied bytes                                                                                          | Generic command (one-off device queries — `setBrightness`, page-switch, screen-on/off, etc., all multiplexed through this) | `SDDevice::getQUCMDCommand`                                    | 🟡 partial — `LIG` we have, but the others (`setSleep`, `setIdleTime`, `setRotation`, etc.) are likely `QUCMD` variants we haven't decoded |
| `ULEND` (5-byte) | 5..9 `U L E N D` | host→dev  | no payload                                                                                                                    | After end-of-image-stream commit                 | `SDDevice::getUploadFinishedCommand`                           | 🔴 **gap** — we send `STP` flush but never `ULEND`, which may explain occasional firmware desyncs after large image bursts |
| `GIFVER` (6-byte) | (literal string `GIFVER` found in `.rdata` at `1801967a8`) | host→dev | (TBD)                                                                                                                         | Query firmware GIF-support version              | not yet bound to a Ghidra function — string-only sighting     | 🟡 unknown |
| `CRT CLE … 'D','C'` (sentinel) | 5..7 `C L E`, 11=`'D'`, 12=`'C'` | host→dev  | end-of-session disconnect tag                                                                                                 | App quitting, `closeHidDevice` path             | `SDDevice::sendDisconnectCommand` — **only emitted for `StreamDock-300`** | not currently emitted by us; harmless |

Notes:

1. **Page-magic on `STP`** (`getFinishCommand` at `0x18001d340`): when
   `_currentPage == 2`, byte 9 = `'"'` (0x22); `== 3`, byte 9 = `'#'`
   (0x23); otherwise byte 9 = `'!'` (0x21). This is the multi-page commit
   marker. Only firmware-detected as the old `StreamDock[321D]`, `H3`, or
   `StreamDock[0108D]` model. The N4/N3 families almost certainly do
   **not** use this — they use the proper QUCMD-driven page system instead.
2. **Wait-for-ACK matrix** in `ImageStruct` confirms the SDK expects the
   device to acknowledge logo uploads within ≤ 20 s, GIF within ≤ 5 min,
   video frame within ≤ 10 s. The 5 MiB `waitForVideoACKPacket` constant
   says video-mode uses 5 MiB chunks.

## 3. Profile / page system

- **Multi-page on-device**: confirmed via the `'!'/'"'/'#'` page-magic
  byte on `STP` for the 321D/0108D/H3 generations. The newer V25 families
  use `QUCMD`-routed page commands (exact byte not yet decoded — to verify
  with USB capture).
- **Profile slots on disk**: the SDK provides `ProfileInformation` and
  `UploadProfile` UI strings, plus an `insertSettings(QList<ImageStruct>&)`
  bulk-push API. Profiles are persisted as JSON keyed by device codename
  under `defaultData/defaultProfiles/<codename>/<locale>/` (read-only
  factory defaults).
- **Switch-profile and folder-nav**: implemented as **plugins** rather than
  protocol commands. Three built-in plugin UUIDs do this:
  - `com.hotspot.streamdock.page.previous` (page back)
  - `com.hotspot.streamdock.page.next` (page forward)
  - `com.hotspot.streamdock.page.goto` (jump to page)
  - `com.hotspot.streamdock.page.indicator` (status-only key)
  - `com.hotspot.streamdock.page.change` (Knob controller — turn knob to
    cycle pages)
  - `com.hotspot.streamdock.profile.openchild` (enter folder)
  - `com.hotspot.streamdock.profile.backtoparent` (exit folder)
  - `com.hotspot.streamdock.profile.rotate` (cycle through profiles)
- **Folder-nav on-device**: the `profile.openchild` plugin pushes a new
  "child" page table to the device (rendered as a fresh set of `BAT`
  images). When `backtoparent` is invoked, the previous page state is
  restored from host memory — the device itself does NOT store the
  folder hierarchy.

## 4. Firmware version query and OTA

### 4.1 Version query (always)

`SDDevice::sendGetHardwareFirmwareVersion()` (PDB 0x180023440) emits a
single `CRT VER` packet, then reads back the response via the input-report
path. The result string is cached in `_firmwareVersion` and returned by
`getHardwareFirmwareVersion()`. The response format is a UTF-8 / ASCII
string (Qt `QString::fromAscii_helper` used to decode it), typically with
spaces (`QChar::QChar(' ')` formatter seen). Example log line embedded:
`"Open device: get firmware version, version string = …"`.

### 4.2 Old vs new device discrimination

`SDDevice::isOld293Version()` (PDB 0x18001ed80) issues a `hid_get_input_report`
with the device's report ID and checks whether the device responds with a
string (new firmware) or returns zero-length (old "293" firmware). Used
to branch the protocol path for the AKP153 family (old 293 firmware does
not support some V25 commands).

### 4.3 OTA / DFU

The shipping app uses a **separate executable** for firmware updates:
`FirmwareUpgradeTool.exe` (1.8 MB). Symbols indicate it bundles
`libusb-1.0.dll` to put the device into a DFU mode (the `aKDFU` 4-byte
signature we see in `SDLibrary1.dll` strings at offset 29251 is likely a
firmware-marker that the OTA tool checks before flashing). Version-check
URLs are stored in the main exe's config blocks:

- `https://cdn1.key123.vip/Custom/Prajnasys/firmware/firmware-version-check.json`
- `https://cdn1.key123.vip/Stock/firmware/firmware-version-check.json`
- `https://cdn1.key123.vip/StreamDock/firmware/firmware-version-check.json`
- `https://cdn1.key123.vip/Custom/Prajnasys/version/app-version-check-Windows.json`

The `_libcef` suffixed URLs serve a separate update channel for the CEF
runtime.

**Action**: do NOT reimplement OTA from this binary. Treat firmware
updates as out-of-scope and let the vendor tool handle them (we just
need to detect "OTA in progress" via VID/PID disappearance, then resume).

## 5. Plugin SDK — Elgato-compatible

The SDK is **Elgato Stream Deck v6-compatible**, almost verbatim. Verified
via `SDPluginServer` symbols and the JavaScript reference plugin shipped at
`defaultData/defaultPlugins/com.hotspot.streamdock.memo.sdPlugin/static/action.js`:

### 5.1 Transport

- **WebSocket** (`QWebSocketServer` via `SDPluginServer::startListen(uint16_t port)`)
- **Also TCP** (`SDTcpServer::startListen` — parallel server). Both listeners
  are on `127.0.0.1`; the port number is dynamic and passed to each plugin
  via `connectElgatoStreamDeckSocket(inPort, …)` (same call name as Elgato's
  SDK).
- The reference plugin in `memo.sdPlugin/plugin/index.js` literally calls
  the function `connectElgatoStreamDeckSocket(inPort, inPluginUUID,
  inRegisterEvent, inInfo)` exactly as Elgato Stream Deck SDK does.

### 5.2 Events (host → plugin)

`SDPluginServer` emits (resolved from method names in PDB):

```
keyDown, keyUp, willAppear, willDisappear,
titleParametersDidChange, propertyInspectorDidAppear,
applicationDidLaunch, applicationDidTerminate,
systemDidWakeUp,
deviceDidConnect, deviceDidDisconnect,
sendToPlugin, sendToPropertyInspector
```

Plus AJAZZ-specific extensions:

```
keyDownCord, keyUpCord     // Knob press for "Cord" (knob/encoder controller)
```

### 5.3 Actions (plugin → host)

```
registerPlugin, registerPropertyInspector,
setTitle, setImage, showAlert, showOk,
getSettings, setSettings,
getGlobalSettings, setGlobalSettings,
switchToProfile, sendToPropertyInspector, sendToPlugin,
openUrl, logMessage
```

Plus AJAZZ-specific:

```
setBG    // set per-key background colour (used by memo todo for blink animation)
```

### 5.4 Plugin process model

Plugins are spawned as separate `QProcess` children
(`SDPluginManager::loadPlugin`, `LoadPrivatePlugin`, `restartPlugin`).
Each plugin connects back to the local WebSocket. The default plugin
process for JavaScript plugins is `node20.exe` (bundled at `node/node20.exe`)
running the plugin's `plugin/index.js`.

Manifest format is **identical** to Elgato Stream Deck (`manifest.json` with
`Actions`, `States`, `Controllers`, `PropertyInspectorPath`,
`SDKVersion: 1`, `Software.MinimumVersion`, `OS` array, `URL`, `Author`,
`Name`, `Icon`, `CategoryIcon`, `Description`). One small extension:
`"Controllers": ["Knob"]` is an accepted controller class for the
Stream Deck Plus-style encoder bindings.

### 5.5 Bundled built-in plugins (vendor-recognised UUIDs)

From `Stream Dock AJAZZ.exe` strings (selected):

```
com.hotspot.streamdock.browser           com.hotspot.streamdock.device.brightness
com.hotspot.streamdock.device.k1proLED+-  com.hotspot.streamdock.mouse.event
com.hotspot.streamdock.multiactions       com.hotspot.streamdock.multiactions.LunBo
com.hotspot.streamdock.network            com.hotspot.streamdock.obsstudio
com.hotspot.streamdock.page.{previous,next,goto,indicator,change}
com.hotspot.streamdock.plain.text         com.hotspot.streamdock.profile.{openchild,backtoparent,rotate}
com.hotspot.streamdock.system.{hotkey,multimedia,volume}
com.hotspot.streamdock.memo.action{1,2}
com.hotspot.streamdock.myHeadline         com.hotspot.streamdock.system.monitor
com.mirabox.streamdock.{calendar,dateTime,emoji,pictureEmoticons,PR,h1time}
```

## 6. ScreenCapture, SystemMonitor, audio

| Feature                  | Status                                                                                                                                              |
| ------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| Live screen mirror       | `ScreenCaptureTool.exe` is a **separate executable** that captures a region of the desktop and (most likely) pipes JPEG/H.264 frames to a plugin or directly via the `DRA`/`MAI` opcodes. Not exhaustively decompiled in this pass. |
| `SystemMonitor` widget   | Implemented as a JS plugin (`com.hotspot.streamdock.system.monitor`) plus the legacy `SystemMonitor64.dll` referenced in earlier device inventories. Updates per-key images via `setImage` from the plugin sandbox. No new opcode needed. |
| Audio routing            | `com.hotspot.streamdock.system.volume` plugin uses Windows multimedia APIs in-process within the plugin host (no dedicated `streamdockSwitchAudio.exe` was found in this build). |
| Per-key sensitivity / long-press | **Not** implemented in firmware. The HID input report only reports press/release edges; "long-press" UI behaviour is implemented host-side by `SDPluginServer` (debounce timer between `keyDown` and `keyUp`). |
| Brightness auto-dim      | Implemented via `QUCMD` (likely `Q U C M D 0x?? <idleSeconds> 0 0 0`); UI exposes `AutomaticScreenOff_` per locale strings.                          |
| GIF / animation          | First-class: `waitForGIFACKTime = 300000 ms` per `ImageStruct`. Vendor relies on host-side gif decode (see `DataFormatConversion::gifToBin` in `SDLibrary1.dll` PDB). |
| Encoder acceleration     | Not in firmware. Vendor synthesises in host (events arrive as ±1 deltas; software accumulates).                                                     |

## 7. Persistence model

- **Registry key**: `HKEY_CURRENT_USER\Software\HotSpot\StreamDock\<deviceCodename>\`
  (verified by string at offset 758247 in `Stream Dock AJAZZ.exe`)
- **App data root**: `%APPDATA%\HotSpot\Stream Dock AJAZZ\` (Qt5 default
  `QStandardPaths::AppDataLocation`)
- **Database**: SQLite (used by `QtSQLite` class — multiple
  `sqlite3_*` symbols imported; embedded `sqlite3` engine compiled in)
- **Plugin per-action settings**: persisted as JSON inside the SQLite DB
  (per Elgato convention — `getSettings`/`setSettings` round-trip)
- **Secret store**: `KeyChainClass` uses `QtKeychain` (OS credential
  manager on Windows / macOS Keychain / kwallet/libsecret on Linux). Used
  for OBS WebSocket password, plugin OAuth tokens.
- **Default profiles** ship under `<install>/defaultData/defaultProfiles/<codename>/<locale>/`.
- **No on-device EEPROM profile storage**: the `UploadProfile` UI string
  refers to uploading the host-defined profile to the device's image
  cache (boot logo / screensaver paths), NOT to flashing profile bindings
  into device storage. The device is a thin display + input panel.

## 8. Anti-features (to document and NOT copy)

| Vector                | Endpoint(s)                                                                                                                                                                                                                                                  | What we should do                              |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| Auto-update phone-home | `https://cdn1.key123.vip/{Custom/Prajnasys,Stock,StreamDock}/version/app-version-check-{Windows,Mac}.json` (+ `_libcef.json` variant), and `https://hotspot-oss-bucket.oss-cn-shenzhen.aliyuncs.com/{BaiJia,LeGe,GORIO}/...-Version_{Windows,Mac}.txt` (per OEM rebrand) | Do NOT implement. Our app is open-source; updates come via Flatpak/MSI repackage cycles. |
| Firmware update CDN   | `https://cdn1.key123.vip/{Custom/Prajnasys,Stock,StreamDock}/firmware/firmware-version-check.json`                                                                                                                                                            | Reference only — when we add OTA support, we'll need to mirror these (or, better, ship vendor blobs bundled in our release). |
| Plugin store / "Mirabox Space" | `https://hotspot-oss-bucket.oss-cn-shenzhen.aliyuncs.com/plugin/Audio/<plugin-name>/<plugin-name>.zip`                                                                                                                                            | Do NOT auto-fetch. We can host our own plugin index. |
| OBS WebSocket plaintext (default) | `OBSWebSocketClient` connects to `ws://localhost:4455` (OBS default) — not necessarily plaintext if user enabled OBS auth                                                                                                                          | OK to replicate; we should default-enable OBS auth checks. |
| Hotkey / global-key hook | `Hook::sendKeyDownValue` / `sendKeyUpValue` install a system-wide low-level keyboard hook (Win32 `SetWindowsHookEx WH_KEYBOARD_LL`) — security-sensitive                                                                                                 | We need our own implementation, but with user-toggleable opt-in. |
| Forced cloud login    | None observed. The app appears to work fully offline for device-local features. The cloud only serves plugin store + version checks.                                                                                                                          | Good — no anti-pattern to avoid here.          |
| Telemetry             | No dedicated telemetry endpoint observed (no `analytics.`, `mixpanel`, `sentry`, etc.). Bug-report path uses `mailto:`.                                                                                                                                       | Good — no anti-pattern to avoid here.          |
| Local plaintext server | `SDPluginServer` (WebSocket) AND `SDTcpServer` both bind to localhost without TLS — by Elgato design                                                                                                                                                          | OK because localhost-only; we should still document the assumption. |

## 9. Gaps vs our current backend (prioritised)

| Priority | Vendor feature                                | Status in `src/devices/streamdeck/`                                                                            | Recommendation                                                                                                                                                                                                            |
| -------- | --------------------------------------------- | -------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **P0**   | `DRA` rect-addressable touch-strip image      | Missing — we only emit whole-strip `MAI`                                                                       | Add `buildSecondaryScreenImageHeader(uint32_t size, uint8_t location, QRect)` to `akp05_protocol.hpp`. Use it for per-encoder zone updates and partial redraws. **Massive bandwidth win** (4×100×480 vs 800×480).         |
| **P0**   | `ULEND` commit-after-image-burst              | Missing — we use `STP` only                                                                                    | Add `CmdUploadFinished{'U','L','E','N','D'}` 5-byte command; emit it after every `setKeyImage`/`setMainImage` sequence in addition to `STP`. Likely fixes the "device freezes after 5–10 rapid setKeyImage calls" class of bugs. |
| **P0**   | `VER` firmware-version query                  | `firmwareVersion()` returns `"unknown"` literal                                                                | Implement `sendGetHardwareFirmwareVersion()` on each backend. Cache result. Use it to gate the rest of the protocol (see `isOld293Version` pattern).                                                                       |
| **P1**   | Page-magic byte 9 on `STP` for V1 protocol    | Missing                                                                                                        | Only relevant for legacy 321D/0108D/H3 (we don't claim to support those). Mark TODO; skip until a user asks.                                                                                                              |
| **P1**   | `LOG` boot-logo / screensaver upload          | Missing                                                                                                        | Add `buildLogoSizeHeader(uint32_t totalBytes, uint8_t logoType=0x02)`. Stream chunks via existing `sendImage`. Expose via new `IBootLogoCapable` capability mix-in.                                                       |
| **P1**   | `QUCMD` generic command catalogue             | Missing — only the `LIG` (brightness) member of this family is implemented                                     | Add `buildQuCmd(uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4, uint8_t p5)` builder. Then experimentally bind: sleep-on/off, idle-timeout, screen-orientation, knob-sensitivity (capture sessions required).            |
| **P1**   | `SDPluginServer` Elgato-compatible WebSocket  | Missing — we have no plugin host today                                                                         | Big feature. Spec: implement `QWebSocketServer` on `127.0.0.1:0` (dynamic port), advertise port to plugins via the standard `connectElgatoStreamDeckSocket(port, uuid, registerEvent, info)` call, implement the 13 standard messages. Make plugins API-compatible with Elgato so users can drop in existing `.sdPlugin` packages. |
| **P2**   | Hot-plug watcher (`StreamDockWatcher.exe`)    | We rely on udev (Linux) / udev-equivalent — already correct                                                    | No change. We use `udev` rules + `IDeviceWatcher` interface; vendor uses `RegisterDeviceNotificationW` polling on Windows.                                                                                                |
| **P2**   | OBS WebSocket built-in client                 | Missing                                                                                                        | Ship as a plugin (`com.ajazz.control.obs.sdPlugin`), not in the core. Use Elgato-style WebSocket events. Out-of-scope for the device backend itself.                                                                     |
| **P2**   | System monitor / multimedia / volume widgets  | Missing                                                                                                        | Ship as plugins; not protocol-level.                                                                                                                                                                                     |
| **P3**   | `GIFVER` firmware GIF-support probe           | Missing                                                                                                        | Capture pending — investigate which model classes need it.                                                                                                                                                              |
| **P3**   | `M_V` 1024-byte secondary-screen logo packet  | Missing; only sighted in header bytes                                                                          | Capture pending. Almost certainly the AKP05/N4 boot logo path.                                                                                                                                                          |
| **P3**   | Encoder-rotation acceleration                 | Out-of-scope                                                                                                   | Host-side (we already deliver ±1 deltas; UI can accumulate).                                                                                                                                                            |

## 10. Modern Qt6 reimplementation recommendations

Cross-references to in-tree code:

- `src/devices/streamdeck/src/akp05_protocol.hpp` — extend with `CmdSecondaryScreen{'D','R','A'}`, `CmdUploadFinished{'U','L','E','N','D'}`, `CmdLogo{'L','O','G'}`, `CmdVersion{'V','E','R'}`, `CmdQuCmd{'Q','U','C','M','D'}` (5-byte). Reuse existing `buildCmdHeader` pattern.
- `src/devices/streamdeck/src/image_pipeline.cpp` (ARCH-04) — already RGBA → JPEG via QImage; no change needed. The `DRA` rect-update path means we should also expose `ImageTransform{cropRect}` so an upstream consumer can render to a specific zone without re-encoding the whole strip.
- `src/core/include/ajazz/streamdeck/streamdeck.hpp` — add an `ISecondaryScreenCapable` mix-in for rect-addressable strip updates. Existing `IDisplayCapable::setMainImage` becomes the "whole-strip" path; add `setSecondaryScreenImage(QRect zone, std::span<uint8_t const> rgba, uint16_t w, uint16_t h)`.
- ARCH-05.1 `setTime()` (Phase 5 time-sync) — the vendor calls `GetSystemTime` and `SystemTimeToFileTime` in `SDLibrary1.dll` (2 + 1 sites, found in our import scan). That's host-side timestamp formatting only, **not** an HID time-sync op. Confirms our `setTime() → NotImplemented` is the correct posture for AKP05.
- `register.cpp` — add the V25 codenames (`AKP03V25`, `AKP03EV25`, `AKP05V25`, `AKP05EV25`, `AKP05RV25`, `AKP153EV3`, `TS10N4V25`, `TS16N3V25`, …) so we recognise the 2025 silicon revisions of the same hardware. The current entry just has `AKP05`/`AKP03`/`AKP153` legacy strings.
- New file `src/host/plugin-host/` (to be created) — Elgato-compatible
  WebSocket plugin host. P1 feature.

## 11. Function name → address index (selected, for follow-up Ghidra
        sessions)

```
0x180017eb0  SDDevice::addClearAllCommand()
0x180018060  SDDevice::addClearCommand(uchar)
0x180018220  SDDevice::addFinishCommand()
0x18001a580  SDDevice::appendSendData(ImageStruct&, bool)
0x18001a660  SDDevice::appendSendData(QList<ImageStruct>&, bool)
0x18001a9b0  SDDevice::closeHidDevice()
0x18001ada0  SDDevice::closeHidDeviceOnError(bool, QString)
0x18001cfb0  SDDevice::getClearAllCommand()
0x18001d0f0  SDDevice::getClearCommand(uchar)
0x18001d340  SDDevice::getFinishCommand()
0x18001d6c0  SDDevice::getHardwareFirmwareVersion()
0x18001de80  SDDevice::getQUCMDCommand(uchar,uchar,uchar,uchar,uchar)
0x18001e310  SDDevice::getSecondaryScreenPicInfo(ImageStruct&)
0x18001e6f0  SDDevice::getUploadFinishedCommand()
0x18001ed80  SDDevice::isOld293Version()
0x18001f370  SDDevice::openHidDevice(int,int,QString&)
0x180020200  SDDevice::openHidDevice(int,int,QString&,int,int,uchar)
0x180021190  SDDevice::prependSendData(ImageStruct&, bool)
0x180021280  SDDevice::readDataFromHidDevice()
0x180022dc0  SDDevice::sendDisconnectCommand()
0x180023440  SDDevice::sendGetHardwareFirmwareVersion()
0x180023950  SDDevice::sendHidFeatureReport()
0x180023a70  SDDevice::sendLogoSizeCommand(int,uchar)
0x1800265d0  SDDevice::writeData(const uchar*)
0x1800269d0  SDDevice::writeDataToHidDevice()
0x18002c350  SDWinUSB::openDevice(_GUID, QString)
0x180148040  SDWinUSB::writeToBulkEndpoint(DeviceEndpoint, QByteArray, ulong*)
0x180147900  SDWinUSB::readFromBulkEndpoint(DeviceEndpoint, uchar*, ulong, ulong*)
0x1801443b0  SDWinUSB::SDWinUSB()
0x180144f00  SDWinUSB::addClearAllCommand()
0x1801450f0  SDWinUSB::addFinishCommand()
0x180146d20  SDWinUSB::getFinishCommand()
0x180146e40  SDWinUSB::getLogoSizeCommand(int,uchar)
0x180147be0  SDWinUSB::sendLogoSizeCommand(int,uchar)
0x1801482d0  SDDeviceWinUSB::SDDeviceWinUSB()
0x18014a200  SDDeviceWinUSB::getHardwareFirmwareVersion()
0x18014a530  SDDeviceWinUSB::openDevice(_GUID&, QString&)
0x18014ae50  SDDeviceWinUSB::openHidDevice(int,int,QString&)
0x18014b300  SDDeviceWinUSB::sddeviceWinusbInit()
0x18014b6f0  SDDeviceWinUSB::sendLogoSizeCommand(int,uchar)
0x180035cf0  SDTcpServer::onSocketDisconnected(QPointer<QTcpSocket>)
0x1800360c0  SDTcpServer::startListen(uint16_t)
0x180037790  SDWebsocketServer::onSocketDisconnected(QPointer<QWebSocket>)
0x1800379a0  SDWebsocketServer::startListen(uint16_t)
```

(Function offsets are RVA from the `SDLibrary1.dll` image base
`0x180000000`. Absolute = base + offset.)

## 12. Methodology

1. **Ghidra 12.1 headless** on `SDLibrary1.dll` with PDB symbol load.
   - Project: `C:\temp\ghidra_sd_proj\sd_sdk`
   - JDK 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.9.10-hotspot`) — required because Ghidra 12 dropped Jython, so the post-script was rewritten in Java (`ExtractStreamDockCalls.java`).
   - Analysis pass: full (~99 s wall-clock). Decomp pass: ~60 s for the 81 interesting functions matched.
2. **Output**: `ghidra_SDLibrary1_dll.json` (1.6 MB) containing:
   - 127 call sites for HID/WinUSB/network imports
   - 81 interesting function decompilations
   - 5408 ASCII data strings from `.rdata` / `.data`
3. **Cross-reference**: full ASCII-strings dump of `SDLibrary1.dll`
   (173 602 strings) and `Stream Dock AJAZZ.exe` (774 312 strings) via a
   PowerShell extractor (`C:\temp\sdlibrary1_strings.txt`,
   `C:\temp\streamdock_exe_strings.txt`).
4. **Clean-room policy**: no vendor source has been incorporated. We used
   the Ghidra-decompiled pseudocode and PDB-resolved symbol names purely to
   document the wire shape; new code is written from scratch against the
   `akp05_protocol.hpp` builder pattern that already exists.
5. **Not yet run** (parking for future passes):
   - Ghidra on `Stream Dock AJAZZ.exe` (42 MB) — would yield UI flow,
     plugin-host details, `SDPluginManager`. Estimated 25–40 min headless.
   - Ghidra on `FirmwareUpgradeTool.exe` (1.8 MB) — would reveal the
     DFU entry protocol, chunk-and-ACK pattern, recovery handshake.
   - Ghidra on `ScreenCaptureTool.exe` (2.1 MB) — would reveal how live
     screen mirror frames are forwarded to the strip (via shared memory,
     pipe, or another WebSocket).
   - USB capture (USBPcap or usbipd-win + Wireshark) against a live
     AKP05/N4 unit to confirm the `DRA`/`ULEND`/`M_V` byte layouts.

## 13. References

- `[ghidra-2026-05-17]` — internal Ghidra dump
  `C:\Users\unilo\reverse-eng-workdir\sd-app\ghidra_SDLibrary1_dll.json`
  + the source `SDLibrary1.dll` (13.1 MB, MD5 unverified) and
  `SDLibrary1.pdb` (15.9 MB).
- See [`_research-sources.md`](./_research-sources.md) for the existing
  third-party citation index used throughout the device-specific docs.
- Cross-link: [`akp05.md`](./akp05.md) (existing device spec — gaps
  identified above feed back into the in-tree implementation).
- Cross-link: [`akp03.md`](./akp03.md) — shares the same wire framing.
- Cross-link: [`akp815.md`](./akp815.md) — shares the `MAI` strip
  protocol.

## 14. Companion documents (deep RE pass 2 — 2026-05-17 evening)

The first pass (§§1–13 above) catalogued the SDK's HID surface,
opcode table, and gross plugin SDK architecture. A follow-up pass
added four companion documents that drill deeper into specific
subsystems:

- **[`akp05_init_sequence.md`](./akp05_init_sequence.md)** — covers
  the `WinMain → main() → bootstrap → device handshake` chain,
  including the previously-undocumented WinUSB framing difference
  (no report-ID byte; opcode at offset 5 instead of 6), the
  `QLocalServer`-based single-instance enforcement, the registry
  layout for per-device prefs, and the `QHostAddress::Any` listener
  binding posture (security-sensitive — do NOT replicate verbatim).
- **[`akp_plugin_sdk.md`](./akp_plugin_sdk.md)** — exhaustive plugin
  SDK reference: 12 default plugins enumerated (we had said "11"
  before; the `mkey.*` variant is the 12th), full manifest schema
  including the AJAZZ-only fields (`IsK1Pro`, `RunAsAdministrator`,
  `FSize`/`FFamily`, `Nodejs.Version`, `PUUID`), all 26+ AJAZZ-only
  WebSocket actions (cross §3 from this file's §5.3), the
  challenge/salt authentication handshake (`hello` → `passHello`
  with salt → SHA-256 challenge), the plugin store URL pattern
  and metadata fields, the QCefView JS-to-C++ bridge API surface
  (`invokeMethod`, `executeJavascriptWithResult`, `cefQueryRequest`),
  and the recommended Qt 6 / QWebChannel replacement pattern.
- **[`akp_device_matrix.md`](./akp_device_matrix.md)** — full
  per-SKU table: 96 codenames recognised by the vendor (we had
  ~50 in §1.3 of this file). Each row includes VID:PID where
  publicly confirmed, protocol version (v1/v2/v3 per `[mirajazz]`),
  transport (hid vs winusb vs both), key/encoder/strip geometry,
  per-key JPEG dimensions + rotation, brightness range, packet
  size, and family-specific features. Notable additions:
  AKP05 V25 codenames (`AKP05V25`, `AKP05EV25`, `AKP05RV25`),
  Mirabox N4 Pro family (vibration + per-key RGB), keyboard-form
  SKUs with embedded LCD (`K1Pro`, `K-992`, `MOLA`), and ~30 OEM
  rebadges (`Streamplify`, `DarkFlash`, `IYUT`, `Womier`, `SANWA`,
  `TOS300`, …).
- **[`akp_dfu_protocol.md`](./akp_dfu_protocol.md)** — the firmware
  upgrade protocol. The `aKDFU` 4-byte sentinel mentioned in §4.3
  is the in-binary marker; the actual on-wire format is an
  Allwinner SoC USB-upgrade protocol over libusb bulk endpoints
  with CBW/CSW handshake, AIC.FW image header, per-FWC CRC
  verification, and a multi-stage burn/verify/run flow. The doc
  spells out why we MUST NOT reimplement this and what our backend
  should do instead (detect DFU transition, suspend, resume on
  re-enumeration). Includes captured strings from
  `FirmwareUpgradeTool.exe`.

### 14.1 Corrections to this document from the deeper pass

- **§1.2 Backends table — WinUSB framing**: the framing diagram in
  §2 of this file describes the **hidapi** packet layout. The
  WinUSB layout is **one byte further left** (no report-ID byte at
  offset 0 — the `'C','R','T'` prefix starts at offset 0; opcode
  starts at offset 5 instead of 6; payload starts at offset 8
  instead of 9). See `akp05_init_sequence.md` §3.4 for the table.
  This affects every WinUSB-class device — currently mainly the
  AKP05/N4 family for the touch-strip channel.
- **§5.1 Transport — listener binding**: the WebSocket listener
  binds to `QHostAddress::Any`, **not** `127.0.0.1` as previously
  stated. The browser-side JS shim hardcodes `ws://127.0.0.1:<port>`
  so the intended attack surface is local-only, but the C++
  listener does accept LAN connections. We must NOT replicate this
  in our implementation — see `akp_plugin_sdk.md` §9 row 1.
- **§5.3 Actions table — completeness**: §5.3 of this file lists
  ~16 actions; the actual vendor surface has 39 (verified in
  `akp_plugin_sdk.md` §4.3 from `streamdock_exe_strings.txt`).
- **§5.5 Built-in plugins**: §5.5 of this file lists ~17 built-in
  UUIDs; we found additional ones (`com.hotspot.streamdock.vmix`,
  `com.hotspot.streamdock.youtube`,
  `com.hotspot.streamdock.pageindicatororgoto`,
  `com.hotspot.streamdock.pagebackorforword`) — see
  `akp_plugin_sdk.md` §1 paragraph after the table.
- **`ProductIdAkp03Demo` PID `0x3004`** is already in our
  `register.cpp` as `Akp03DemoPid`. The "HOTSPOTEKUSB HID DEMO"
  string label confirmed the device is an AKP03 sibling on
  development firmware.

### 14.2 Items moved to follow-up captures (still gaps)

- DFU-trigger HID packet shape (which `QUCMD` byte combo enters
  DFU mode?).
- One `.aic` firmware hex-dump to confirm AIC.FW header.
- AKP05 V25 vs pre-V25 protocol-version differences (firmware
  reports `293V25` vs `293V3`-class strings — runtime detection
  needs a `VER` response sample).
- AKP05 main-strip JPEG dimensions for non-rect-update path
  (whole-strip `MAI` vs rect-update `DRA` — both exist but use
  different geometry).
- M_V boot-logo 1024-byte packet full layout (per §2 row 191 of
  this file — header bytes only verified).
