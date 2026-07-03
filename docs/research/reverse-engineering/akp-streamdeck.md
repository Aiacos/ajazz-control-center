# AJAZZ Stream Dock Family — Reverse-Engineering Dossier

> **Scope.** AKP03 / AKP05 / AKP153 / AKP815 + Mirabox N3/N4 siblings. Clean-room:
> byte layouts, command words, and decompiled/OSS symbol references only — no
> vendor source reproduced. Sources resolve in `docs/protocols/streamdeck/_research-sources.md`.
> In-tree truth: `src/devices/streamdeck/src/*`, pinned by `tests/unit/test_akp*.cpp` + `test_image_pipeline.cpp` + `test_factory_reset_and_logo.cpp`.

**Sources read:** all `docs/protocols/streamdeck/*.md`; all `src/devices/streamdeck/src/*.{hpp,cpp}` + `image_pipeline.*` + `register.cpp`; `tests/unit/test_akp*` + `test_image_pipeline` + `test_factory_reset_and_logo`; the streamdeck rows of `docs/_data/devices.yaml`; `akp05_vendor.md` (SDLibrary1.dll Ghidra audit, analysis-only).

______________________________________________________________________

## 1. Identity & topology

### 1.1 Protocol-version taxonomy (`[mirajazz]`)

| Ver | Packet | Press/release      | Example                                |
| --: | -----: | ------------------ | -------------------------------------- |
|   0 |    512 | press-only         | Soomfon 293S                           |
|   1 |    512 | press-only         | AKP153, AKP815                         |
|   2 | 1024\* | press-only         | AKP03, Mirabox N3                      |
|   3 |   1024 | full press+release | AKP03R rev.2, AKP05/AKP05E, Mirabox N4 |

\* `[ajazz-sdk]` `is_v2_api()` implies 1024-byte, but our in-tree AKP03 backend still uses **512** (`akp03_protocol.hpp PacketSize=512`) pending the migration in §7. AKP05 uses **1024** OUT.

### 1.2 AKP03 / Mirabox N3 — 6 LCD keys (2×3) + 3 side buttons + 3 encoders

60×60 JPEG `Rot0` (AKP03R rev.2 → 64×64 `Rot90`); boot logo 240×320 `Rot90`; no touch strip. PIDs: `0x0300:0x1001` (canonical), `0x3002` (E), `0x1003` (R), `0x3003` (R rev.2, v3), legacy `0x3001`; Mirabox `0x6602:0x1002`, `0x6603:0x1002/0x1003`. **`0x0300:0x3004` is NOT here** (it's an AKP05E — §1.3).

### 1.3 AKP05 / AKP05E / Mirabox N4 — 10 LCD keys (2×5) + 4 encoders + touch strip

85×85 JPEG `Rot0`; 4 endless pressable encoders bound to 4 touch zones; LCD strip ≈800×480; built-in USB-2 hub. Proto v3, **1024-byte OUT / 512-byte IN**. PIDs: `0x6603:0x1007` (Mirabox N4, only canonical), `0x0300:0x3004` (AKP05E), `0x0300:0x5001` (placeholder, no public source).

> **The `0x0300:0x3004` correction is load-bearing.** A 2026-05-13 capture surfaced it with USB product string "Ajazz HOTSPOTEKUSB HID DEMO" and it was mis-filed as a 6-key AKP03. A live `CRT VER` handshake on **2026-05-20** returned **`V3.AKP05E.01.007`** — an AKP05E (v3, 10 keys, 4 encoders, touch strip). USB descriptor: IF0 vendor HID (usage page `0xFFA0`, no Report IDs), EP `0x82` IN=512 B, EP `0x03` OUT=**1024 B**; `VER` answered via GET_REPORT. **The white-label "DEMO" string is not a reliable SKU signal — always cross-check `VER`.** `register.cpp` now routes `0x0300:0x3004` to `makeAkp05`.

### 1.4 AKP153 / AKP153E / AKP153R — 15 LCD keys (3×5)

85×85 JPEG `Rot90` + mirror; decorative 854×480 strip; no encoders/strip. PIDs: `0x0300:0x1001` (legacy repo), `0x5548:0x6674` (Mirabox V1, canonical), `0x0300:0x1002` (E legacy), `0x0300:0x1010` (E canonical), `0x0300:0x1020` (R). Proto v1/v2 (runtime-probed `isOld293Version()`). ~80 OEM rebadges map here.

### 1.5 AKP815 — 15 LCD keys (5×3) + 800×480 strip

100×100 JPEG `Rot180`; 800×480 addressable strip; no encoders/strip. PID `0x5548:0x6672` (only known). Proto v1 (512). Backend reuses the AKP153 state machine with a different `DisplayInfo`; dedicated factory + 100×100/Rot180 transform queued.

______________________________________________________________________

## 2. Transport & framing

### 2.1 "CRT" packet framing (buffer offsets, after the report-ID byte)

| Offset | Field                                                             |
| ------ | ----------------------------------------------------------------- |
| 0..2   | `'C','R','T'` = `0x43 0x52 0x54`                                  |
| 3..4   | reserved `0x00 0x00`                                              |
| 5..7   | 3-byte ASCII command word (or start of a 5-byte word)             |
| 8      | 3-byte cmd: payload byte 0; 5-byte cmd: opcode byte 4             |
| 9      | tag/payload byte 1 (page-magic on `STP` for legacy 321D/0108D/H3) |
| 10..N  | payload, zero-padded to `PacketSize`                              |

On hidapi the per-device report-ID byte is prepended at `hid_write` time; the on-wire packet is `[reportId][CRT...]` of length `PacketSize + 1`. The builders construct `PacketSize` bytes with `'C'` at offset 0.

### 2.2 The report-ID-on-Linux issue

Byte 0 of the buffer is `'C'` with **no HID report-ID byte**. On Linux/hidraw the kernel takes `buffer[0]` as the report number, so byte 0 (`'C'`=0x43) is consumed and the panel gets misaligned bytes — the AKP05 first-key icon shows on Windows but not on Fedora. Same class as `via.md:10` ("report id 0x00 on Linux/macOS, omitted on Windows WriteFile"). **Fix on branch `feat/linux-device-support`:** `HidTransport::write()` prepends a `0x00` report-id byte under `#ifndef _WIN32` only, gated by `makeHidTransport(prependReportIdPosix=true)` set by the four streamdeck backends. **Pending Fedora hardware confirmation.** (We are `hidapi_hidraw`-only; the vendor's WinUSB framing — one byte left, dynamic packet size — is not implemented.)

### 2.3 Packet sizes

AKP153/AKP815 = 512 OUT/IN; AKP03 = 512 in-tree (vendor v2 = 1024, migration queued); AKP05 = **1024 OUT / 512 IN**.

### 2.4 Input report parsing

Action/tag byte at **offset 9**; reports \<16 bytes discarded (SEC-008); `'A','C','K'` frames dropped; `tag==0x00` = NOP.

- **AKP153:** byte 9 = key idx 1..15; **no byte-10 polarity** — one frame per transition (backend must diff for release edges; current bug: always `pressed=true`).
- **AKP03:** byte 9 tag — `0x01..0x06` LCD key (byte 10 edge), `0x25/0x30/0x31` side buttons 7/8/9 (release-only), `0x90/0x91`/`0x50/0x51`/`0x60/0x61` encoder 0/1/2 CCW/CW, `0x33/0x35/0x34` encoder 0/1/2 press.
- **AKP05:** `0x01..0x0A` keys, byte 10 = press/release edge — **[CONFIRMED 2026-05-27]**. Encoders + touch: the earlier `0x20..0x2F` / `0x30..0x3F` low-nibble model is **[REFUTED]** — see §2.5.

### 2.5 AKP05E input decode — correction (2026-05-27 Ghidra RE)

A fresh decompile of `SDDevice::readDataFromHidDevice` (SDLibrary1.dll @ RVA `0x180021280`) + `SDActionCanvasWidget::handleKeyEvents` (Stream Dock AJAZZ.exe @ `0x1400d02b0`, symbols from the 69 MB PDB) cross-checked against a live AKP05E corrected the encoder + touch model. Public clean-room write-up: `docs/protocols/streamdeck/akp05_input_corrections.md`. Raw dumps backed up in `../raw-workdir/` (`akp05_input_parse.json`, `akp05_keyevent_slot.json`, `akp05_jumptable.json`).

- **[CONFIRMED] keys.** Input is HID via `hid_read_timeout` (not WinUSB). Key code @ report[9], press/release @ report[10] (vendor `keyCode` / `state`). The in-tree `akp05.cpp` key path is vendor-correct — keep. (Caveat: the EXE runs `keyCode` through `Utilities::mapToSoftwareLocation`; for the simple family the code *is* the 1-based location — confirm `report[9] ∈ {1..10}` in the live capture.)
- **[CONFIRMED WRONG] encoders.** There is **no rotation-delta byte**. Rotation direction is encoded in the **value of report[9]** and routed through a per-family jump table to `KnobActionType ∈ {KnobClockwiseRotation, KnobCounterclockwiseRotation, KnobPressed}` → `SDActionCanvasLabel::performActionKnob`. One report = one detent in a fixed direction. Therefore `frame[10]` is NOT an int8 delta and `frame[11]` is NOT a button edge; the `0x20..0x2F` low-nibble mask was an OSS-corpus guess, not the vendor binary. Decode encoders from a `report[9] → (index, CW/CCW/press)` table; emit `EncoderTurned ±1` / `EncoderPressed` (release synthesised host-side, as AKP03 already does).
- **[CONFIRMED WRONG] touch strip.** Touch X is the **single byte report[10]** (0..255), passed straight to `SDActionTouchBarWidget::getTouchbarLocationFromX(state)` — NOT a BE16 over report[10..11], so the `TouchStripRangeX = 640` clamp is also wrong (one byte cannot encode 0..639). Event type is a **distinct report[9] code** (`0x98` touch-down / `0x99` up / `0x97` move; `0x78/0x79` setCoreX; `0xb1/0xb2` N4-Pro touchbar-mode toggle), NOT a `0x30` low-nibble gesture. **Swipe-left/right is not a firmware event** — the vendor derives tap-vs-swipe host-side from the down→up X delta; X→zone (4 zones aligned to the 4 encoders) is computed by `getTouchbarLocationFromX`.
- **[PROVISIONAL]** The exact `report[9]` codes per (encoder, direction, press) and the touch X scale / X→zone mapping are **NOT pinned** — they need one live native-Linux hidraw capture (corrections doc §7). Do **not** hard-code them as fact. The `0x10→CCW / 0x11→CW` pair was seen in *a* family branch of `handleKeyEvents` but was not proven to be the AKP05E branch.

______________________________________________________________________

## 3. Command words (byte-by-byte)

### 3.1 Family-wide (`akp_common_protocol.hpp`, single source of truth)

| Mnemonic           | Bytes 5..7 (or 5..9)            | ASCII   | Payload                                                                                                     |
| ------------------ | ------------------------------- | ------- | ----------------------------------------------------------------------------------------------------------- |
| **CRT** prefix     | offsets 0..2 = `43 52 54`       | `CRT`   | —                                                                                                           |
| **LIG**            | `4C 49 47`                      | `LIG`   | byte 10 = brightness 0..100                                                                                 |
| **STP**            | `53 54 50`                      | `STP`   | none (legacy page-magic at byte 9 = `0x21/0x22/0x23`)                                                       |
| **CLE**            | `43 4C 45`                      | `CLE`   | byte 10=0, byte 11 = key idx or `0xFF` (all); AKP153 `byte10=0x44 byte11=0x43` ("DC") → restore vendor logo |
| **VER**            | `56 45 52`                      | `VER`   | none; device returns ASCII version string                                                                   |
| **ULEND** (5-byte) | offsets 5..9 = `55 4C 45 4E 44` | `ULEND` | none; end-of-image-burst commit                                                                             |

### 3.2 AKP153 / AKP815 device-specific

- **DIS** (`44 49 53`) display init; **BAT** (`42 41 54`) key image — bytes 10..11 = BE16 JPEG size, byte 12 = 1-based key idx, JPEG follows in 512-byte chunks; **LOG** (`4C 4F 47`) boot logo (854×480 / 800×480); **HAN** (`48 41 4E`) sleep. Upload: BAT header → chunks → device ACKs `41 43 4B 00 00 4F 4B` (`ACK\0\0OK`); serialised per key.

### 3.3 AKP03 device-specific

**BAT** image (JPEG; was mis-named PNG — `CmdImagePng` is a deprecated alias of `CmdImage`): bytes 10..11 = BE16 size, byte 12 = key idx. **DIS** display init, **HAN** sleep, **LOG** boot logo (240×320 `Rot90`). Capture: `43 52 54 00 00 42 41 54 00 00 08 7C 0D ...` (BAT header) then `43 52 54 00 00 53 54 50 ...` (STP flush).

### 3.4 AKP05 device-specific (pinned in tests)

- **BAT** key image: BE16 size @10..11, 1-based key idx @12.
- **ENC** (`45 4E 43`) encoder LCD image: BE16 size @10..11, 0-based encoder idx @12. (May be a misnomer — N4 renders encoder graphics in the strip, not separate LCDs.)
- **MAI** (`4D 41 49`) main/whole-strip image: BE16 size @10..11.
- **LOG** (`4C 4F 47`) boot logo: BE16 size @10..11 (vendor `sendLogoSizeCommand` actually uses BE32 + a type byte — reconcile).
- **DRA** (`44 52 41`) touch-strip rect-addressable: BE32 size @8..11, location/zone @12, BE16 width @13..14, height @15..16, x @17..18, y @19..20; JPEG follows. (Header pinned in `test_akp05_protocol.cpp`.)

Upload discipline (all AKP05 image surfaces): header → JPEG in 1024-byte zero-padded chunks → **ULEND** commit. Payload >65535 B refused (16-bit size field).

### 3.5 Vendor-only opcodes seen in the SDLibrary1.dll audit (not all implemented)

- **QUCMD** (5-byte, bytes 10..14 = 5 user bytes): generic command multiplexer (brightness, sleep, idle-timeout, rotation, screen on/off, page-switch). Only the `LIG` member implemented.
- **M_V** (3-byte, **no CRT prefix**): 1024-byte secondary-screen boot-logo packet when `location==0x12`. Header bytes only; our DRA builder refuses `0x12`.
- **GIFVER** (6-byte literal): query firmware GIF-support version; unbound.
- **CLE + 'D','C'**: end-of-session disconnect tag (StreamDock-300 only); not emitted.

______________________________________________________________________

## 4. Image pipeline (`image_pipeline.cpp`, ARCH-04)

Host-side RGBA8 → JPEG/PNG via Qt6 `QImage::scaled(SmoothTransformation)` + `QImageWriter`. `ImageTransform{w,h,format,rotationDegrees,mirror,jpegQuality=85}`. Per-surface (pinned in `test_image_pipeline.cpp`): AKP153 key 85×85 Rot90+mirror; AKP815 key 100×100 Rot180; AKP03 key 60×60 Rot0; AKP05 key 85×85 Rot0, encoder 100×100, main strip 800×100 (band of 800×480). JPEG SOI/EOI + PNG signature pinned. (SEC-013: AKP153 can brick on logo EXIF metadata — re-encoding from raw RGBA strips it.)

______________________________________________________________________

## 5. RE methods & sources

- **Vendor binary audit** (`akp05_vendor.md`): Ghidra headless on `SDLibrary1.dll` (+ PDB) → `ghidra_SDLibrary1_dll.json` (127 call sites, 81 functions). PDB symbol map (RVAs from base `0x180000000`): `getQUCMDCommand 0x18001de80`, `getSecondaryScreenPicInfo 0x18001e310`, `getClearCommand 0x18001d0f0`, `getFinishCommand 0x18001d340`, `getUploadFinishedCommand 0x18001e6f0`, `sendGetHardwareFirmwareVersion 0x180023440`, `sendLogoSizeCommand 0x180023a70`, `isOld293Version 0x18001ed80`. Source paths indicate upstream Gitee `F:\STreamDock\Gitee\…`.
- **OSS corpora** (read-only, never vendored): `[ajazz-sdk]` mishamyrt (MPL — `info.rs`/`codes.rs`/PIDs), `[opendeck-akp03]`/`[opendeck-akp05]` 4ndv/naerschhersch (GPL — N3/N4 USB IDs + touch-strip), `[mirajazz]` (proto-version taxonomy), `[elgato-rs]` OpenActionAPI (MPL — canonical AKP153/03 v0.10.2), `[pyajazz]` superdeee (MIT — 18-position layout, Rot90, EXIF brick warning), `[ajazz-akp03e-py]` tomekceszke (MIT), `[uriziel-akp153]`/`[zcube]` (raw captures), `[companion]` Bitfocus (press/release-edge synthesis).
- **Input-decode audit (2026-05-27):** a second Ghidra pass over `SDLibrary1.dll` (read path, project `sd_sdk`) + `Stream Dock AJAZZ.exe` + PDB (action-slot layer, project `sd_exe`; both backed up under `../raw-workdir/ghidra_projects/ghidra_sd_proj/`) → dumps `akp05_input_parse.json` / `akp05_keyevent_slot.json` / `akp05_jumptable.json` via scripts `DumpAkp05Input.java` / `DumpKeyEventSlot.java` / `DumpJumpTable.java`. Corrected the encoder + touch wire model (§2.5).
- **Live hardware:** the 2026-05-20 `CRT VER` handshake → `V3.AKP05E.01.007` (the AKP05E reclassification); the 2026-05-27 audit confirmed the *structure* of the input path against the device but the exact encoder/touch codes still await a live hidraw capture.

______________________________________________________________________

## 6. Confidence matrix

| Device                   | VID:PID                           | Maturity   | Witnessed                                                                                                                                                                        |
| ------------------------ | --------------------------------- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| AKP153 / \_v1 / E / E_v2 | `0x0300:0x1001`,`0x5548:0x6674`,… | functional | corpus + capture                                                                                                                                                                 |
| AKP153R                  | `0x0300:0x1020`                   | scaffolded | corpus-only                                                                                                                                                                      |
| AKP815                   | `0x5548:0x6672`                   | **probed** | corpus-only (100×100/Rot180 unconfirmed)                                                                                                                                         |
| AKP03 / E / R            | `0x0300:0x1001/3002/1003`         | functional | corpus + capture                                                                                                                                                                 |
| AKP03R rev.2             | `0x0300:0x3003`                   | scaffolded | corpus-only (v3/64×64)                                                                                                                                                           |
| Mirabox N3               | `0x6602:0x1002`                   | partial    | inherited                                                                                                                                                                        |
| AKP05 (placeholder)      | `0x0300:0x5001`                   | scaffolded | **no public source**                                                                                                                                                             |
| Mirabox N4               | `0x6603:0x1007`                   | scaffolded | corpus-only                                                                                                                                                                      |
| **AKP05E**               | `0x0300:0x3004`                   | scaffolded | **hardware-witnessed** (`V3.AKP05E.01.007`); input **key** path CONFIRMED, encoder/touch *structure* CONFIRMED but codes PROVISIONAL (§2.5); image wire bytes still hypothesised |

Every Stream Dock backend inherits `IClockCapable` but `setTime()` → `NotImplemented` — **no AJAZZ Stream Dock firmware exposes a host-settable RTC** (contrast the AK980 keyboard + AJ mouse, which both have an opcode-0x28 clock).

______________________________________________________________________

## 7. UNEXPLORED / capture-needed (see also `unexplored.md`)

1. **AKP03 v2→1024-byte framing migration** (still 512 in-tree); AKP05 V25 vs V3 firmware boundary.
1. **AKP815** 100×100/Rot180 transform device-unconfirmed (probed); 5×3 vs 3×5 orientation; dedicated factory.
1. **AKP05 placeholder `0x0300:0x5001`** retirement once a real AKP05 surfaces its true PID.
1. **All AKP05 wire bytes (DRA/ENC/MAI/LOG/M_V) are corpus/Ghidra-derived, never confirmed against a live USB capture.** Top capture targets: a partial-update DRA sequence; the M_V `location==0x12` boot-logo layout; the LOG BE16-vs-BE32 size field; whether N4 accepts `ENC` at all.
1. **report-id-on-Linux** fix is on `feat/linux-device-support`, pending Fedora confirmation — does the AKP05 icon render on hidraw with the `0x00` prepend?
1. **AKP153 release-edge synthesis** (no byte-10 polarity).
1. **DFU/firmware update** (`akp_dfu_protocol.md`): Allwinner-style USB-upgrade via `FirmwareUpgradeTool.exe` (the only binary linking libusb) — `AIC.FW` magic, `aKDFU` AES-GCM sentinel, CBW/CSW Bulk-Only, UU-device re-enumeration (conventionally Allwinner BROM `0x1f3a:0xefe8/9`, not captured). **Do NOT reimplement** (COD-031); backend should only detect VID/PID disappearance → DFU → resume.
1. **GIF/animation** (v3): `waitForGIFACKTime=300000ms`, host `gifToBin`; `GIFVER` opcode sighted, unbound.
1. **QUCMD catalogue** (sleep/idle/rotation/screen-off byte combos) undecoded except `LIG`.
1. **AKP05E encoder/touch input codes** (§2.5): the corrected decode *structure* is confirmed, but the exact `report[9]` codes per (encoder, direction, press) and the touch X scale / X→zone mapping are provisional. Needs one native-Linux hidraw capture: per-control `report[9]`/`report[10]` for the 10 keys, the 4 encoders ×{CW, CCW, press}, and touch down/move/up across the 4 zones (corrections doc §7). Until then the in-tree parser keeps the provisional decode, explicitly marked PROVISIONAL.
