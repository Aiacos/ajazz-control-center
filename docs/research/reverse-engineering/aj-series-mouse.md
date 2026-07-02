# AJAZZ Mouse Family — Reverse-Engineering Dossier

> **Scope.** SONiX-VID `0x3151` family (2.4G 8K `0x5007`; AJ159/AJ179 APEX
> `0x5008` wired + `0x4026` 2.4G + `0x4027`/`0x4028` dongle/BLE), legacy
> AJ139/AJ159/AJ179 on `0x248A`/`0x249A`, AJ199 family on `0x3554`. Clean-room:
> findings + symbol references only; no vendor source reproduced.
>
> **Hardware wins.** The `0x28` OLED clock (report-id `0x00`, `0xD7` marker) and
> the battery status report `0x05` byte 3 are hardware-confirmed (2026-05-21);
> everything else is renderer-decompile confidence, not wire-witnessed.

**Sources read:** `docs/protocols/mouse/*.md`; `src/devices/mouse/src/{aj_series_protocol.hpp,.cpp,aj_series.cpp,register.cpp,aj_series_tft_pipeline.*}`; `tests/unit/test_aj_series_*.cpp`; `docs/research/vendor-protocol-notes.md` Findings 8/11/12/13/14/15; the vendor Electron `main_beautified.js` + native `iot_driver` (analysis-only).

______________________________________________________________________

## 1. Identity & topology

### 1.1 VID:PID inventory

| VID      | Family                                            | Notable PIDs                                                                                                                                                              |
| -------- | ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `0x3151` | **SONiX-VID primary** (AJ159/AJ179 APEX, 2.4G 8K) | `0x5007` (2.4G 8K = `ajazz_24g_8k`), `0x5008` (AJ159 APEX wired / AJ179), `0x4026` (2.4G), `0x4027` (dongle), `0x4028` (BLE, UP `0xff55`); 41 active + 19 bootloader PIDs |
| `0x3554` | **AJ199 family** (V1.0, Max)                      | wired `0xF500`/`0xF566`/`0xF546`; dongle `0xF501`/`0xF564`/`0xF567`/`0xF545`/`0xF547`/`0xF5D5`                                                                            |
| `0x248A` | legacy AJ139/AJ159/AJ179 wired                    | `0x5C2E` (primary), `0x5D2E`/`0x5E2E` (alt), dongle `0x5C2F`                                                                                                              |
| `0x249A` | legacy 2.4G dongle                                | `0x5C2F`                                                                                                                                                                  |

Specific SKU is resolved at runtime by a vendor `dev_id` readback, not by USB PID (all 8 legacy SKUs share `0x248A` tuples). **Do not regress:** the pre-2026-04-29 fictional `0x3554:0xF51A..0xF51D` PIDs exist in no vendor manifest; AJ339/AJ380 remain un-enumerated.

### 1.2 HID interface / collection map

- Legacy `0x248A`: config channel is the **3rd HID interface, `MI_02`**.
- `0x3151`: vendor-config interface uses **usage page `0xFFFF`**, interface 2; bootloader `0xff01`/iface −1; BLE `0xff55` (mouse)/`0xff66` (kbd).
- **Two `0xFFFF` collections.** The control channel is **usage `0x02`**; usage `1` is NOT control. `register.cpp` pins `controlUsagePage=0xFFFF`, `controlUsage=0x02` (commit `69c64a1`); without it the transport opens the boot-mouse interface and every feature/output report silently fails.

### 1.3 Report-id conventions

- **Output/config path: report id `0x05`** (`kReportId`; `featureReportByteLength: 65` = 1 report-id + 64 body).
- **OLED clock path: report id `0x00`** (the `0x28` packet; `buildMouseSetOledClock` sets `pkt[0]=0x00`).
- **Battery read: report id `0x05`** (GET_FEATURE on report 0x05).
- UNCERTAINTY: the report-id value lives inside the native `iot_driver`, not the JS; `0x05`-vs-`0x00` is confirmed only for those two paths.

______________________________________________________________________

## 2. Architecture (`0x3151` "universal driver" stack)

Vendor app = **Electron + React + MobX + grpc-web** talking to a native **`iot_driver(_v193).exe`** — a **Rust `tonic` gRPC server** with a hidapi backend at **`127.0.0.1:3814`**, persisting to **sled** at `%APPDATA%\AJAZZ Driver（R）\iot_db\`. Build path: `D:\work\dj_hid_sdk_rs\…\driver.rs`.

**Wire-byte split:** the renderer builds a **64-byte body** (opcode at body byte 0); `iot_driver` **prepends the report id and computes the checksum** (offset 63) per the `CheckSumType` enum. The renderer never touches report id or checksum.

**gRPC `driver.DriverGrpc`:** server-streams `watchDevList` (hot-plug `DeviceList`), `watchVender` (input reports), `watchSystemInfo` (host telemetry — anti-feature); unary `sendMsg`/`readMsg` (interrupt OUT/IN), `sendRawFeature`/`readRawFeature` (SET/GET_REPORT), `setLightType`, `upgradeOTAGATT` (BLE OTA), sled CRUD, `getVersion`, `getWeather` (phone-home — anti-feature).

**Enums:** `DangleDevType {NONE=0, KEYBOARD=1, MOUSE=2}`; `CheckSumType {BIT7=0, BIT8=1, NONE=2}`; `LightType {MUSIC2=0, SCREEN=1, OTHER=2}`.

**Transport:** the mouse-class config path uses **`sendMsg` (interrupt-OUT, `hid_write`)**, not feature reports. Our backend matches (`m_transport->write()`), EXCEPT the `0x28` clock (`writeFeature`/SET_FEATURE) and battery read (`readFeature`/GET_FEATURE).

**Checksum: BIT7** = `sum(body) & 0x7F`. Our `stampBit7Checksum` sums `pkt[1..63]` (vendor bytes 0..62) & `0x7F` at `pkt[64]`. Every AJ-series call site uses BIT7 (98 sites; zero BIT8). The `0x28` clock has **no checksum**. (Exact Rust sum-range `0..=62` vs `1..=62` still pending Ghidra confirmation.)

______________________________________________________________________

## 3. Opcode-by-opcode (`0x3151` "universal" dialect)

Byte positions use the vendor convention (opcode = body byte 0); our buffer maps vendor byte N → `pkt[N+1]`. Checksum BIT7 at `pkt[64]` unless noted.

### 3.1 Core control

| Opcode | Mnemonic                     | Layout                                                                     |
| ------ | ---------------------------- | -------------------------------------------------------------------------- |
| `0x80` | `GET_REV` (firmware version) | resp: uint16-LE at body bytes 1..2 (backend still returns "unknown" — gap) |
| `0x02` | `SET_RESERT` (factory reset) | empty body; destructive                                                    |
| `0x05` | `SET_PROFILE`                | byte 1 = profile 0..7; `0x85`=GET                                          |
| `0x04` | `SET_REPORT` (poll rate)     | byte 1=profile, byte 2 = `_RateToNum` code; `0x84`=GET                     |
| `0x83` | `GET_BATTERY`                | declared, **NOT used on mouse path** (battery via report 0x05 — §4)        |

**`_RateToNum`:** 125→`0x08`, 250→`0x04`, 500→`0x02`, 1000→`0x01`, 2000→`0x84`, 4000→`0x82`, 8000→`0x81` (high bit = high-rate flag; BIT7 masking clears it from the checksum only).

### 3.2 `0x07` — LED param (8-byte block)

byte1=effect(0..10), byte2=`(4 - speed)`, byte3=value/brightness(0..6), byte4=`(option<<4)|mode` (NORMAL=7/DAZZLE=8), byte5/6/7=R/G/B. Effects: 0 Off,1 AlwaysOn,2 Breath,3 Neon,4 Wave,5 Dazzling,6 Laser,7 MusicFollow,8 ScreenColor,9 MusicFollow2,10 UserPicture. **Brightness is NOT a standalone opcode** (byte 3). Pure-white sentinel `0xFFFFFF`→`0xFAFAFA`. `0x87`=GET. AJ159 APEX `light.isRgb=false` (battery-LED only).

### 3.3 `0x50` — `MOUSE_SET_KEYMATRIX` (button rebind)

byte1=profile, byte2=button idx, **action at bytes 8..11 big-endian** (NOT 4..7). `changeArr` type byte: 0 combo/forbidden, 1 mouse-button, 9 macro, {2,3,6,8,10,11,13,14,18,19,20,22} system fns. `0xd0`=GET (16×4-byte = 64-byte matrix).

### 3.4 `0x51` — `MOUSE_SET_FNMATRIX`

Like 0x50, byte1=Fn-layer idx. `0xd1`=GET.

### 3.5 `0x53` — `MOUSE_SET_OPTIONPARAM0` (omnibus)

byte8=profile, byte9=poll-rate code, byte10=debounce(0..10), bytes12-13=uint16-LE flags (bit0 lightOff, bit1 wheelLightOff, bit2 smooth, bit3 ledSelect, bit4 powerSave), byte14=buttonChange(def 1), byte15=wheelToButton(10), byte16=buttonToWheel(10), bytes24-31=main LED block, bytes32-39=logo LED block, bytes40-47=sleep (uint16-LE ×4: BT idle/deep, 2.4G idle/deep, seconds), byte50=xSensitivity, byte51=ySensitivity, byte52=liftCutOff(LOD 0=1mm/1=2mm/2=3mm), byte53=angleSnap, bytes54-56=battery-LED high RGB, bytes57-59=low RGB, byte60=chargingSwitch. **LOD/sensitivity/angle-snap/smoothing/sleep/debounce/battery-LED are ALL fields of this one packet.** `0xd3`=GET.

### 3.6 `0x54` — `MOUSE_SET_OPTIONPARAM1` (DPI table, atomic)

byte1=profile, byte2=active stage(0..7), byte3=stageCount(0..8), bytes8-23=8×uint16-LE DPI, bytes40-63=8×{R,G,B} stage colours. **Edge case:** the 8th stage's B-channel lands on byte 63 = checksum slot → the BIT7 checksum overwrites it (UI must grey the 8th swatch). `0xd4`=GET. DPI 50–42000 step 50 (PAW3950 class). Re-upload full table atomically on every change.

### 3.7 `0x16` — `SET_MACRO_SIMPLE` (chunked, 20 slots)

256-byte payload in **5 chunks × 56 bytes**: byte1=slot(0..19), byte2=chunk idx(0..4), byte3=last-non-zero pos, byte4=final flag, bytes8-63=56 payload bytes (byte63 overwritten by checksum). Payload: bytes0-1=uint16-LE repeat; packed records (delay ≤127=1 byte; >127=uint16-LE; keyboard=HID usage bit7=down; mouse-button=`OF[key][2]` bit7=down; mouse-move=`0xF9`+dx+dy int8). `0x96`=GET. MACROMAX=20.

### 3.8 Screen / LCD — `0x25`/`0x29`

`SETTFTLCDDATA 0x25` (RGB565) / `SET_SCREEN_24BITDATA 0x29` (RGB888), chunked: byte1=currentFrame, byte2=frameNum, byte3=frameDelay(ms), bytes4-5=uint16-LE chunkIndex, byte6=chunkLen(≤56, our cap 55), bytes8-63=pixels. `0xa5`/`0xa9`=GET. GIF: `0x18 USERGIFSTART` + `0x19 USERGIF`. **This render path does NOT set the clock** (use `0x28`); kept for a future custom-image feature.

### 3.9 `0x28` — `SET_OLEDCLOCK` (firmware RTC) — HARDWARE-CONFIRMED 2026-05-21

```
byte 0    : 0x00   (HID report id — NOT 0x05)
byte 1    : 0x28
byte 2..7 : 0x00
byte 8    : 0xD7   (REQUIRED fixed marker — firmware ignores the packet without it)
byte 9..10: year, BIG-ENDIAN  (2026 → 0x07 0xEA)
byte 11   : month   byte 12 : day   byte 13 : hour   byte 14 : minute   byte 15 : second
byte 16.. : 0x00
```

Sent via `HidD_SetFeature` (`writeFeature`). **No checksum.** The `0xD7` marker was the missing piece. (The keyboard RTC uses a single 2000-offset year byte — different.) Pinned in `test_aj_series_tft_clock.cpp`.

### 3.10 Other OLED / boot opcodes (declared; mostly unexplored)

`0x20/0xa0` OLEDPICINDEX, `0x21/0xa1` OLEDPICDATA, `0x22/0xa2` OLEDOPTION, `0x24/0xa4` OLEDGIFDATA, `0x26/0xa6` OLEDGIFINDEX, `0x27` OLEDLANGUAGE, `0x2a` OLEDWEATHER, `0x2b/0xab` OLEDEFFECT, `0x2c/0xac` FLASHCHIPERASE, `0xad` GETOLED_VERSION; screen-MCU boot `0x30/0xb0`, `0x31/0xb1`.

### 3.11 OTA (mouse-MCU "MLED") — declared, unimplemented

`0x40 SET_MLEDBOOTLOADER` (`[0x55,0xAA,0x55,0xAA,0,0,0]`), `0xc0` poll until `resp[1]==1`, `0x41 SET_MLEDBOOTSTART` (uint16-LE chunk count), 64-byte data chunks, `0xc1 GET_MLEDBOOTCHECKSUM` (int32-LE; `resp[1]==0x55`=ok). Both MCU flows skip the first `0x10000` bytes. BLE OTA via gRPC `upgradeOTAGATT`.

### 3.12 Anti-features (DO NOT implement)

`0x55 SET_DOWNCOUNT` (rapid-fire), `0x60 SET_CONTROLRECOIL`/`0xe0` (no-recoil — anti-cheat liability), `0x61` (BLE pairing reset). Plus `getWeather` (leaks typed address), `watchSystemInfo` (host telemetry), cloud login + analytics.

______________________________________________________________________

## 4. Battery telemetry (two paths)

### 4.1 Our path: HID status report `0x05` byte 3 — HARDWARE-CONFIRMED 2026-05-21

GET_FEATURE on report `0x05` (on `0xFFFF`/usage-`0x02`): charge in **byte 3** (0..100; `0x64`=100%). `AjSeriesMouse::batteryPercent()` implements `IBatteryCapable`. hidapi returns the report-id at index 0.

```
byte 0 : 0x05   byte 1 : 0x00   byte 2 : 0x00
byte 3 : charge percent (0 = link up but not yet reported)
byte 4..7 : 01 01 01 02 when link up; all-zero right after reconnect
```

**Frame validation (commit `1f2be0c`):** valid frame iff **bytes 1 and 2 == 0**. Observed: `05 00 00 64 01 01 01 02` (stable 100%), `05 00 00 00 00 00 00 00` (reconnect, not ready), `05 00 00 00 01 01 01 02` (link up, no charge → grey), `05 ad 04 01 00 00 00 00` (**garbage transient → the spurious "1%"**). Fix rejects non-zero byte1/2 + byte3==0→nullopt. `register.cpp` gates `hasBattery` to wireless/dongle codenames.

### 4.2 Vendor path: gRPC `Device.battery` / `Status24`

`watchDevList` → `proto.driver.Device {…, battery=5 (0..100), isonline=6, …}`. Wired: `battery=0`. Dongle pushes battery for both kbd+mouse via `Status24 {battery=1, isOnline=2}`. Linux equivalent: `/sys/class/power_supply/hid-<vid>:<pid>.*/capacity`.

______________________________________________________________________

## 5. Wire-format envelope variants across SKUs (Findings 11/12/13)

| Dialect                       | Frame | Report ID  | Checksum                                      | Used by                                       |
| ----------------------------- | ----- | ---------- | --------------------------------------------- | --------------------------------------------- |
| **OemDrv**                    | 17 B  | `0x08`     | `0x55 − sum_lo − sum_hi − tail` (subtractive) | AJ199 V1.0 (2023)                             |
| **HIDUsb**                    | 20 B  | `0x01`     | `sum(buf[3..18]) & 0xFF` (SIMD)               | AJ199 Max (2024, Beken BK2535 + WCH CH32V305) |
| **Witmod**                    | 64 B  | per-device | per-command (145-case switch)                 | AK820 Max RGB (keyboard)                      |
| **Ours / `0x3151` universal** | 64 B  | `0x05`     | `sum(buf[1..62]) & 0x7F` (BIT7)               | `aj_series.cpp` + iot_driver stack            |

HIDUsb cmd table (AJ199 Max, `buffer[4]`): `0x02 SetPCDriverStatus`, `0x03 ReadOnLine`, `0x04 ReadBatteryLevel`, `0x08 ReadFalshData`, `0x09 SetClearSetting`, `0x0B SetVidPid`, `0x0D EnterUsbUpdateMode`, `0x0E ReadConfig`, `0x0F SetCurrentConfig`, `0x12 ReadVersion`, `0x14 Set4KDongleRGB`, `0x16 SetLongRangeMode`, `0x1A SetDongleIDToMouse`. **Mismatch:** our 64-byte/`0x05`/BIT7 matches the modern `0x3151` SKUs only; the `0x3554:0xF500/0xF501` AJ199 entries in our registry use the WRONG dialect (OemDrv 17B / HIDUsb 20B) and are PID-gated **suspect** until a per-(VID,PID,fw) dialect dispatch lands.

______________________________________________________________________

## 6. RE methods & sources

Beautified Electron JS analysis (`main_beautified.js`; symbols `FEA_CMD_SET_OLEDCLOCK`, `setMouseOption0/1`, `_RateToNum`, `setKeyConfigSimple`, `setMacro`, `getBattery`/`Device.battery`, `mledUpgrade`). Native `iot_driver` Rust gRPC strings. **Frida hook** of `iot_driver` `HidD_SetFeature` (`scripts/aj_mouse_frida_capture.py`) — caught the `0x28` clock packet + the `00 f7` status-poll heartbeat. `scripts/aj_mouse_probe.py` (`--enumerate`/`--clock`/`--battery`/`--battery-watch` — caught the `05 ad 04 01` reconnect garbage). Sensor: tri-mode PAW3950 (PAW3395 family).

## 7. Confidence matrix

| Feature                                                         | Confidence                                       |
| --------------------------------------------------------------- | ------------------------------------------------ |
| `0x28` OLED clock (RID 0x00, 0xD7 marker, BE year, no checksum) | **CONFIRMED (hardware)**                         |
| Battery report `0x05` byte 3 + frame validation                 | **CONFIRMED (hardware)**                         |
| `0xFFFF`/usage-`0x02` control collection                        | **CONFIRMED (hardware)**                         |
| report id `0x05`; BIT7 mask                                     | High (renderer); per-opcode wire-id unverified   |
| `_RateToNum`, `0x53`/`0x54`/`0x50`/`0x07`/`0x16` byte maps      | Medium-High (renderer decompile; no USB capture) |
| OTA, OLED weather/sysinfo/language                              | Low                                              |
| AJ199 family under our backend                                  | **Suspect (dialect mismatch)**                   |

See [`unexplored.md`](unexplored.md) for the full open-questions list (report-id per-opcode, checksum range, wired-vs-dongle, DPI table validation, OTA, AJ199 dialect split, OLED extras, dongle re-addressing, battery frame flag-byte semantics).
