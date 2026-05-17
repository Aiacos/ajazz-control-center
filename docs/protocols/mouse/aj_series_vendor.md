# AJ-series mouse vendor app reverse engineering (AJ159 APEX 2.1.94)

> Source: `C:\Users\unilo\reverse-eng-workdir\aj159_apex\extracted\app32\resources\app\`
>
> All line numbers refer to the **beautified** copies the analysis generated
> alongside the originals (run jsbeautifier locally to reproduce):
>
> - `main_dist\main_beautified.js` — Electron main process
> - `dist\static\js\main_beautified.js` — renderer (React/MobX) bundle
> - `iot_driver.exe` — Rust gRPC HID server (raw strings via PowerShell regex)

---

## Inventory

| Item | Value |
|------|-------|
| App name | `AJAZZ Driver（R）` |
| Version | `2.1.94` |
| Stack | Electron + React 18 + MobX 6 + grpc-web; Rust `tonic` HID server |
| Renderer libs | `mobx-react`, `moment`, `axios`, `gifwrap`, `adm-zip`, `uuid`, `react-toastify`, `react-paginate`, `react-router-dom`, `node-machine-id`, `universal-analytics` |
| HID transport | Out-of-process **`iot_driver.exe`** (Rust, hidapi backend) speaking **grpc-web over plaintext HTTP** at `127.0.0.1:3814` (renderer → driver). Build path: `D:\work\dj_hid_sdk_rs\target\i686-pc-windows-msvc\release\…\driver.rs` |
| On-disk DB | **sled 0.34.7** at `%APPDATA%\AJAZZ Driver（R）\iot_db\<table>` |
| Cloud auth | `api.rongyuan.tech:3814/v1` (primary), `api2.qmk.top:3814/v1` (fallback) — share-community login only |
| Bundled vendors visible in branding dir | AJAZZMOUSE, GamingMouse, AQIRYSmousesoftware, rongyuan, VKMS, 炫光, akko |
| AJ159 APEX wired VID:PID | **0x3151:0x5008** (`featureReportByteLength = 65` = 1 report id + 64 payload) |
| AJ159 APEX wireless VID:PID | **0x3151:0x4026** (same envelope, capped at 1 KHz) |
| Companion AJ159 dongle PIDs | 0x4027 (common dangle), 0x4028 (BLE), 0x4011 / 0x4014 (dongle commons) — see `iot_driver` device list |
| Onboard profiles | **8** (`layer: 8` in every AJ-series layout) |
| DPI stages on AJ159 | **8** (`Ki.dpi.count`, `Hi.dpi.count` — line 59578 / 59564) |
| DPI range AJ159 | **50 – 42000 DPI**, step 50 (PAW3950/PAW3395 family) |
| Polling rates AJ159 wired | 125 / 250 / 500 / 1000 / **2000 / 4000 / 8000** Hz |
| Polling rates AJ159 wireless | 125 / 250 / 500 / 1000 Hz (no 2K+ on 2.4 G) |
| RGB on AJ159 | **None on this SKU** (`Ki.light.isRgb = false`) — note `_LightSettingToBuffer` still writes a light block, but the device ignores it for this PID |
| Onboard macros | 20 slots (`MACROMAX = 20`), 256-byte payload each |
| Onboard screen | TFT LCD via `FEA_CMD_SETTFTLCDDATA` (0x25) or 24-bit `FEA_CMD_SET_SCREEN_24BITDATA` (0x29) |

The vendor app is a "universal" driver — the same binary supports dozens of
mouse/keyboard SKUs. Lines 61777 / 61793 / 64045 / 64061 / 64876 each
register an `AJ159 APEX` entry; the layout selection (`Ki` vs `Hi`) picks
the wired-vs-wireless caps.

---

## Architecture diagram

```
  ┌──────────────────────────────┐       ┌─────────────────────────────┐
  │   Electron renderer (JS)     │       │   iot_driver.exe (Rust)     │
  │   React/MobX UI              │  HTTP │   tonic gRPC server :3814   │
  │   grpc-web client            │ ◀───▶ │   hidapi backend            │
  │   no  : 56600  ao = new Jn   │       │   sled KV DB for persistence│
  │   (line 56600)               │       │   sub-streams: watchDevList │
  └──────────────────────────────┘       │       watchVender,          │
                                         │       watchSystemInfo       │
                                         └──────────────┬──────────────┘
                                                        │ /dev/hidraw  (kernel)
                                                        ▼
                                              ┌─────────────────────┐
                                              │  AJ159 APEX mouse   │
                                              │  64-byte feature    │
                                              │  reports (id 0x05)  │
                                              └─────────────────────┘
```

The renderer **never** touches HID directly. Every wire byte goes through
the gRPC `sendRawFeature(SendMsg{device_path, msg, check_sum_type,
dangle_dev_type})` call. The checksum byte at offset 63 is computed
inside `iot_driver.exe` according to the `CheckSumType` enum; the
renderer only supplies bytes 0..62.

---

## gRPC service catalogue (`driver.DriverGrpc`)

Extracted from renderer line 56088 onward and confirmed against
`iot_driver.exe` strings.

| Method | Type | Request | Reply | Purpose |
|--------|------|---------|-------|---------|
| `watchDevList` | server-stream | `Empty` | `DeviceList` | Hot-plug events: add/remove/change/init |
| `watchVender` | server-stream | `Empty` | `VenderMsg` | Async device-pushed bytes (HID input reports for "vender" interface) |
| `watchSystemInfo` | server-stream | `Empty` | `SystemInfo` | **Host telemetry**: CPU temp, mem, disk, network IO — fed to the AJ159 LCD widget |
| `sendMsg` | unary | `SendMsg` | `ResSend` | Interrupt-OUT write (raw HID output report) |
| `readMsg` | unary | `ReadMsg{device_path}` | `ResRead{msg}` | Interrupt-IN read |
| `sendRawFeature` | unary | `SendMsg` | `ResSend` | **Feature SET_REPORT** — primary mouse config path |
| `readRawFeature` | unary | `ReadMsg` | `ResRead` | **Feature GET_REPORT** |
| `setLightType` | unary | `SetLight{device_path, light_type, screen_id, dangle_dev_type}` | `Empty` | Tell driver which auxiliary screen-stream is active (`MUSIC2 / SCREEN / OTHER`) |
| `upgradeOTAGATT` | server-stream | `OTAUpgrade{dev_path, file_buf}` | `Progress{percent}` | **BLE-only** OTA upload (used for keyboards over BLE; mouse OTA goes through `FEA_CMD_SET_MLEDBOOTLOADER` 0x40 instead) |
| `muteMicrophone` | unary | `MuteMicrophone{need_mute}` | `ResSend` | Host audio mute (for the mouse's media keys) |
| `toggleMicrophoneMute` | unary | `Empty` | `ResSend` | — |
| `getMicrophoneMute` | unary | `Empty` | `MicrophoneMuteStatus` | — |
| `changeWirelessLoopStatus` | unary | `WirelessLoopStatus{lock}` | `Empty` | Pause/resume the dongle's polling loop while OTA runs |
| `insertDb` | unary | `InsertDb{db_path,key,value}` | `ResSend` | sled write |
| `deleteItemFromDb` | unary | `DeleteItem{db_path,key}` | `ResSend` | sled delete |
| `getItemFromDb` | unary | `GetItem{db_path,key}` | `Item{value}` | sled read |
| `getAllKeysFromDb` | unary | `GetAll{db_path}` | `AllList{data}` | sled scan keys |
| `getAllValuesFromDb` | unary | `GetAll{db_path}` | `AllList{data}` | sled scan values |
| `cleanDev` | unary | `ReadMsg{device_path}` | `ResSend` | Disconnect handle |
| `getVersion` | unary | `Empty` | `Version{base_version, timestamp}` | iot_driver version |
| `getWeather` | unary | `WeatherReq{language,address}` | `WeatherRes{res}` | **Phone-home**: the iot_driver pulls a weather string from a remote API for the mouse LCD widget |

### Helpers in the renderer that wrap those gRPC calls (line 56600)

```text
no(devPath, buf, checksumType, dangleDevType=NONE)  → sendMsg         (interrupt-OUT)
oo(devPath)                                         → readMsg          (interrupt-IN)
uo(devPath, buf, checksumType, dangleDevType=NONE)  → sendRawFeature   (SET_REPORT)
io(devPath)                                         → readRawFeature   (GET_REPORT)
lo(devPath, lightType, screenId=0, dangleDevType=NONE) → setLightType
co(devPath)                                         → cleanDev
so()                                                → getVersion
fo(lang, addr)                                      → getWeather
yo("stop"|"start")                                  → changeWirelessLoopStatus
```

### Enums (renderer line 51241)

```text
DangleDevType:   NONE=0  KEYBOARD=1  MOUSE=2
CheckSumType:    BIT7=0  BIT8=1     NONE=2
DeviceListChangeType: INIT=0  ADD=1  REMOVE=2  CHANGE=3
DeviceType:      YZWKEYBOARD=0  YZWBOOT=1  YZWVENDER=2
LightType:       MUSIC2=0  SCREEN=1  OTHER=2
```

> **CheckSumType note.** Every mouse path the renderer takes passes `BIT7`.
> The Rust server is the one that fills byte 63 of the 64-byte report.
> The enum naming strongly suggests `BIT7 = sum(bytes 1..62) & 0x7f` (or
> the equivalent two's-complement variant where bit-7 is forced to 0).
> Our `aj_series.cpp` currently uses `& 0xff` (BIT8). **This is a
> bug-suspect against the AJ159** — see ACTION items below.

---

## Complete HID opcode table (mouse class)

> All values come from the mouse FEA_CMD enum at renderer line **920839**
> (giant one-liner; ~70 entries). Opcodes follow the convention
> `SET = N, GET = N | 0x80`. Wire layout is always
> `byte0 = opcode, byte1 = profile (or arg), …`. The 65-byte report
> begins with `0x05` report-id, then the 64 bytes shown below.

### Core profile / report control

| Opcode | Mnemonic | Direction | Payload | UI trigger | Status |
|--------|----------|-----------|---------|------------|--------|
| `0x00` | `FEA_CMD_SET_REV` | host→dev feature | none | (vendor unused on mouse path) | gap |
| `0x80` | `FEA_CMD_GET_REV` | host→dev feature, response read via `readRawFeature` | none → uint16 LE @ byte 1 = firmware version | "About" dialog, version banner | **gap** (we hardcode `"unknown"`) |
| `0x8f` | `FEA_CMD_GET_INFOR` | feature | none | initial device probe | gap |
| `0x02` | `FEA_CMD_SET_RESERT` | feature | empty | **"Restore defaults"** button (line 921730) | gap |
| `0x83` | `FEA_CMD_GET_BATTERY` | feature | none | (declared but **NOT** used on mouse path; mouse battery is pushed via `Device.battery` field in `watchDevList` stream) | partial — we use 0x40 instead, which doesn't match vendor |
| `0x01` | `FEA_CMD_SET_WIRELESS_SYNC` | feature | (unknown sub-fields) | wireless re-pair handshake | gap |
| `0x05` | `FEA_CMD_SET_PROFILE` | feature | byte 1 = profile idx (0…7) | profile-switch dropdown | **gap** (we have no profile switching) |
| `0x85` | `FEA_CMD_GET_PROFILE` | feature | empty → byte 1 = current profile | active-profile read on startup | gap |
| `0x06` | `FEA_CMD_SET_KBOPTION` | feature | (keyboard-only on shared base class) | n/a for mouse | n/a |
| `0x86` | `FEA_CMD_GET_KBOPTION` | feature | n/a | n/a | n/a |
| `0x04` | `FEA_CMD_SET_REPORT` (= polling-rate) | feature | byte1=profile, byte2=rate-code (see _RateToNum) | "Polling rate" radio buttons | **partial** — we send raw kHz uint16, vendor sends 1 byte from `_RateToNum` table |
| `0x84` | `FEA_CMD_GET_REPORT` | feature | byte1=profile, → byte2 = rate-code | settings load | gap |

### `_RateToNum` (renderer line 920911)

| UI label | byte-code |
|----------|-----------|
| 125 Hz | `0x08` |
| 250 Hz | `0x04` |
| 500 Hz | `0x02` |
| 1000 Hz | `0x01` |
| 2000 Hz | **`0x84`** (high bit set!) |
| 4000 Hz | **`0x82`** |
| 8000 Hz | **`0x81`** |

> Our backend writes `byte1 = hz >> 8`, `byte2 = hz & 0xff`, i.e. for 8000
> Hz it sends `0x1f, 0x40`. The mouse expects `0x81` at byte 2. **This
> wire format is wrong** for 2/4/8 KHz on the AJ159.

### Lighting (LED params)

Opcode `0x07` (`FEA_CMD_SET_LEDPARAM`) — **payload is 8 bytes at
byte[1..8], not 4** as our backend does.

```
byte0 = 0x07
byte1 = effect type (see table)
byte2 = (MAXSPEED - speed)  where MAXSPEED = 4
byte3 = value (brightness 0..6 or option-encoded value)
byte4 = (option_nibble << 4) | mode_bits     // wave direction, etc.
byte5 = R
byte6 = G
byte7 = B
```

Effect-type enum (renderer line 920977):

| Code | Name | Notes |
|------|------|-------|
| 0 | LightOff | no payload bytes |
| 1 | LightAlwaysOn | value=brightness, rgb=color, dazzle bit |
| 2 | LightBreath | + speed |
| 3 | LightNeon | option 0=Default 1=Random (cycles rainbow) |
| 4 | LightWave | option from `WAVEOP` (up/down/left/right) |
| 5 | LightDazzing | + speed |
| 6 | LightLaser | + speed |
| 7 | LightMusicFollow | option from `MP` enum (upright / sideways) |
| 8 | LightScreenColor | screen-color follower (uses host RGB) |
| 9 | LightMusicFollow2 | second music mode |
| (10) | LightUserPicture | special case — bytes [5,6,7] = `[0, 200, 200]` marker |

`GET_LEDPARAM = 0x87` — response same layout starting at byte 1.

> **Brightness as a separate opcode does NOT exist** in the vendor wire
> format. Our `kCmdRgb` sub-cmd 0x02 (`setRgbBrightness`) is fictitious.
> Brightness rides inside the 8-byte light packet (byte 3 = `value`).

### Mouse-specific (opcodes 0x50–0x60)

> These are the mouse class extensions; `FEA_CMD_MOUSE_*` prefix.

| Opcode | Mnemonic | Direction | Payload | UI trigger | Status |
|--------|----------|-----------|---------|------------|--------|
| `0x50` | `FEA_CMD_MOUSE_SET_KEYMATRIX` | feature | byte1=profile, byte2=button idx, byte8..11 = 4-byte action | per-button rebind | **partial** — our `kCmdButton=0x24` is the wrong opcode and wrong payload offset (we used offset 4, vendor uses offset 8) |
| `0xd0` | `FEA_CMD_MOUSE_GET_KEYMATRIX` | feature | byte1=profile, → 64 bytes = full key-matrix | settings load | gap |
| `0x51` | `FEA_CMD_MOUSE_SET_FNMATRIX` | feature | byte1=fn-layer, byte2=button, byte8..11=action | Fn-layer rebind (some SKUs) | gap |
| `0xd1` | `FEA_CMD_MOUSE_GET_FNMATRIX` | feature | byte1=layer → key-matrix | settings load | gap |
| `0x52` | `FEA_CMD_MOUSE_SET_USERPIC` | feature | screen-frame upload | LCD background editor | gap |
| `0xd2` | `FEA_CMD_MOUSE_GET_USERPIC` | feature | read back picture | LCD preview | gap |
| `0x53` | `FEA_CMD_MOUSE_SET_OPTIONPARAM0` | feature | **omnibus 64-byte mouse settings packet** (see layout below) | "save settings" master button | **gap** — this is the canonical "everything in one packet" command we should use |
| `0xd3` | `FEA_CMD_MOUSE_GET_OPTIONPARAM0` | feature | empty → 64-byte response | settings load | gap |
| `0x54` | `FEA_CMD_MOUSE_SET_OPTIONPARAM1` | feature | DPI stage table + per-stage RGB (see layout) | DPI editor save | **partial** — our `kCmdDpi=0x21` writes one stage at a time; vendor writes all 8 atomically |
| `0xd4` | `FEA_CMD_MOUSE_GET_OPTIONPARAM1` | feature | byte1=profile → DPI table | DPI editor load | gap |
| `0x55` | `FEA_CMD_SET_DOWNCOUNT` | feature | byte 8 = countdown value | recoil-control / rapid-fire countdown | gap |
| `0x60` | `FEA_CMD_SET_CONTROLRECOIL` | feature | chunked recoil pattern upload (56-byte chunks) | "Recoil Control" tab | gap (and we shouldn't implement — see anti-features) |
| `0xe0` | `FEA_CMD_GET_CONTROLRECOIL` | feature | recoil pattern read | — | gap |
| `0xe1` | `FEA_CMD_GET_CLEARBLUEINFRO` | feature | (clear bluetooth pairing info) | "Unpair BLE" button | gap |

### `FEA_CMD_MOUSE_SET_OPTIONPARAM1` byte layout (DPI table — line 921188)

```
byte  0 : 0x54
byte  1 : profile idx (0..7)
byte  2 : current active DPI stage (0..7)
byte  3 : "currentDPIMax" — number of enabled stages
byte 4..7 : zero
byte  8..23 : 8 × uint16-LE DPI values (each stage)
byte 24..39 : reserved / zero
byte 40..63 : 8 × { R, G, B } per-stage indicator colours
```

`GET_OPTIONPARAM1` response (line 921230) deserialised:

```js
currentDpi     = resp[2]            // active stage idx
currentDPIMax  = resp[3]            // enabled-stage count
dpiArr         = uint16LE × 8 in [8..23]   // (vendor reads bytes 8..23, but
                                          //  also reads 24..39 — possibly
                                          //  X-DPI vs Y-DPI split on
                                          //  some SKUs; for AJ159 the
                                          //  X/Y arrays are identical)
DPIColorArr    = { R,G,B } × 8 in [40..63]
```

### `FEA_CMD_MOUSE_SET_OPTIONPARAM0` byte layout (omnibus — line 921127)

```
byte  0 : 0x53
byte  1..7 : zero
byte  8 : profile idx
byte  9 : polling-rate code (from _RateToNum)
byte 10 : debounce time (ms; 0..10 typical)
byte 11 : zero
byte 12..13 : uint16-LE flags bitfield:
              bit0 = lightOff
              bit1 = wheelLightOff
              bit2 = smooth (motion-smoothing)
              bit3 = ledSelect (battery-LED RGB enable)
              bit4 = powerSaveMode
byte 14 : buttonChange       (default 1)
byte 15 : wheelToButton       (default 10 — wheel-as-keys)
byte 16 : buttonToWheel       (default 10)
byte 17..23 : reserved
byte 24..31 : 8-byte light-setting block (see LightSettingToBuffer above)
byte 32..39 : 8-byte LOGO-light block (separate from main LED zone)
byte 40..47 : 8-byte sleep-time block:
              uint16LE time_bt, deepTime_bt, time_24, deepTime_24
              (BT idle, BT deep-sleep, 2.4G idle, 2.4G deep-sleep, all in s)
byte 48..49 : reserved
byte 50 : xSensitivity   (0..100 %)
byte 51 : ySensitivity   (0..100 %)
byte 52 : liftCutOff     (LOD: 0 = 1 mm, 1 = 2 mm, 2 = 3 mm)
byte 53 : angelSnap      (angle-snap on/off)
byte 54..59 : 2 × { R,G,B }  battery-LED colours (high/low charge indicator)
byte 60 : chargingSwitch (1 = LED on while charging)
byte 61..62 : reserved
byte 63 : checksum (filled by iot_driver)
```

> Our `kCmdLod=0x23` doesn't exist on the mouse. LOD is byte 52 of the
> omnibus packet. Same for sensitivity, angle-snap, sleep-time.

### Macros (`FEA_CMD_SET_MACRO_SIMPLE = 0x16`)

Macro upload is chunked (line 922079 + 921989):

```
For each 56-byte chunk of the 256-byte macro payload:
  byte 0 = 0x16          // FEA_CMD_SET_MACRO_SIMPLE
  byte 1 = macro slot idx (0..19)
  byte 2 = chunk idx (0..4)
  byte 3 = last-non-zero byte position in chunk (for trimming)
  byte 4 = 1 if last chunk
  byte 5..7 = zero
  byte 8..63 = 56 bytes of macro payload
```

Macro payload format (line 922025 — `setMacro`):

```
uint16LE @ 0   : repeatCount
byte sequence  : packed action records
                 type=delay      → bit7=0, low 7 bits = delay ms (≤127)
                                   OR uint16LE for longer delays
                 type=keyboard   → byte = HID usage; bit7=down flag
                 type=mouse_button → byte = OF[key][2]; bit7=down flag
                 type=mouse_move → 0xF9, dx int8, dy int8
```

### Screen / LCD upload (AJ159 has a small TFT)

`FEA_CMD_SETTFTLCDDATA = 0x25` (16-bit colour) / `FEA_CMD_SET_SCREEN_24BITDATA = 0x29` (24-bit).

```
byte 0 : 0x25 or 0x29
byte 1 : currentFrame
byte 2 : frameNum (total frames)
byte 3 : frameDelay (ms between frames)
byte 4..5 : uint16-LE chunk index
byte 6 : chunkLen (≤ 56)
byte 7 : reserved
byte 8..63 : up to 56 bytes of pixel data
```

GIF-style USERGIF upload uses `FEA_CMD_SET_USERGIFSTART=0x18` + `FEA_CMD_SET_USERGIF=0x19`.

### OLED / boot-loader paths (firmware OTA over HID, no BLE)

The mouse uses the "MLED" (mouse-LED MCU) bootloader path:

| Opcode | Mnemonic | Use |
|--------|----------|-----|
| `0x40` | `FEA_CMD_SET_MLEDBOOTLOADER` | Enter bootloader: payload `[0x55,0xAA,0x55,0xAA,0,0,0]` |
| `0xc0` | `FEA_CMD_GET_MLEDBOOTLOADER` | Poll until response byte 1 == 1 (bootloader ready) |
| `0x41` | `FEA_CMD_SET_MLEDBOOTSTART` | Begin upload: payload `[uint16LE chunkCount]` |
| (no-op opcode) | data chunks | 64-byte chunks of firmware (sent with `send64(chunk, 2, devPath)`) |
| `0xc1` | `FEA_CMD_GET_MLEDBOOTCHECKSUM` | Send accumulated int32-LE checksum; response byte 1 == 0x55 = success |

The screen-MCU has a parallel `FEA_CMD_SET_OLED_BOOT=0x30`,
`FEA_CMD_SET_OLED_BOOTSTART=0x31` flow (line 817268). Both flows skip
the first 0x10000 bytes of the firmware image (boot header).

### Misc

| Opcode | Mnemonic | Purpose |
|--------|----------|---------|
| `0x23` | `FEA_CMD_SET_AUTOOS_EN` | Autodetect Mac/Windows layout switch |
| `0xa3` | `FEA_CMD_GET_AUTOOS_EN` | — |
| `0x14` | `FEA_CMD_SET_USERPIC_SIMPLE` | per-key picture (n/a for mouse) |
| `0x13` | `FEA_CMD_SET_AUDIO` | media-key audio settings (n/a here) |
| `0x0e` | `FEA_CMD_SET_AUDIO` (alt class) | |
| `0x0f` | `FEA_CMD_SET_WINDOS` | Windows-specific quirks |
| `0x11` | `FEA_CMD_SET_DEBOUNCE` (keyboard) | mouse uses byte 10 of OPTIONPARAM0 instead |
| `0x12` | `FEA_CMD_SET_SLEEPTIME` (keyboard) | mouse uses bytes 40..47 of OPTIONPARAM0 |
| `0x7a..0x7d` | `G_CMD_TEST_MODE / CHECK_STUTAS / GET_READY / SET_BOOTLOATER` | factory commands; do NOT expose |

---

## Wire-format summary table (vendor app vs. our backend today)

| Feature | Vendor opcode | Vendor payload | Our opcode | Match? |
|---------|--------------|----------------|-----------|--------|
| DPI table | `0x54` | 8 stages atomic, uint16-LE, RGB at 40..63 | `0x21` sub `0x00` | **NO** (wrong opcode, wrong endian — we use BE) |
| Active DPI stage | `0x54` byte 2 (sent with the table) | — | `0x21` sub `0x01` | **NO** |
| Polling rate | `0x04` byte 2 = code from `_RateToNum` | 1 byte coded | `0x22` payload = uint16 BE | **NO** |
| LOD | byte 52 of `0x53` omnibus | enum 0/1/2 mm | `0x23` payload = (mm × 10) | **NO** |
| Button binding | `0x50` byte 2 = btn, bytes 8..11 = action | — | `0x24` payload at byte 4 | **NO** (offset 4 vs 8 → corrupts request) |
| RGB static | `0x07` 8-byte light packet | type=1 (LightAlwaysOn) | `0x30` sub `0x00` | **NO** |
| RGB effect | `0x07` 8-byte light packet | type=2..9 | `0x30` sub `0x01` | **NO** |
| RGB brightness | (inside the 8-byte light packet, byte 3) | — | `0x30` sub `0x02` | **NO** (no such standalone opcode) |
| Battery query | NOT a feature-report on mouse — comes from `Device.battery` field of `watchDevList` gRPC stream | — | `0x40` | **NO** (opcode doesn't exist) |
| Commit / save to EEPROM | NOT a separate opcode — every `0x53`/`0x54` writes persist immediately | — | `0x50` | **NO** (the opcode 0x50 we use is actually `MOUSE_SET_KEYMATRIX` — we've been corrupting button slot 0!) |
| Firmware version | `0x80` (`FEA_CMD_GET_REV`), response uint16-LE @ byte 1 | — | `"unknown"` literal | gap |
| Profile select | `0x05` byte 1 = idx | — | absent | gap |
| Macro upload | `0x16` chunked | 256-byte format | absent | gap |
| Recoil pattern | `0x60` chunked | (anti-feature) | absent | (intentional gap) |
| Screen / LCD | `0x25` / `0x29` chunked | image frames | absent | gap (defer to a future v1.x) |
| Firmware OTA | `0x40` / `0x41` / `0xc0` / `0xc1` | mouse-MCU bootloader | absent | gap |
| LED MUTE / mic | gRPC `muteMicrophone`, not HID | — | absent | n/a |

> **The most damaging finding is our `kCmdCommit = 0x50`** — opcode 0x50
> is `FEA_CMD_MOUSE_SET_KEYMATRIX` in vendor speak. Every time our
> backend "commits" today, it's actually issuing a key-matrix write with
> garbage payload to whatever button index happens to be in byte 2 of
> the empty envelope. This is the kind of thing the CAPTURE-04 mock
> transport would catch, but it would slip past anyone who only
> exercises the test harness.

---

## Features in our backend NOT in vendor (suspect / remove)

1. **`kCmdCommit = 0x50`** — vendor has no separate "commit" step. Writes
   persist immediately. Action: delete the helper, audit every caller.
2. **`kCmdBattery = 0x40` HID query** — vendor reads battery from the
   gRPC `Device.battery` field broadcast by the dongle. The wired-USB
   mouse has no battery to report. Replace with a no-op (return
   `std::nullopt` on wired SKUs) and a dongle-status hook on wireless.
3. **`kCmdRgb sub 0x02` (standalone brightness)** — does not exist.
   Brightness must ride inside the 8-byte light packet (byte 3).

---

## Features the vendor implements (gaps in our backend)

1. **8 onboard profiles** with switch via `FEA_CMD_SET_PROFILE` (0x05).
   Today we treat the mouse as stateless.
2. **8 DPI stages** (not 6). Default table for AJ159 APEX:
   `[400, 800, 1200, 1600, 2400, 3200, 0, 0]`, active stage = 1 (800 DPI).
3. **Per-stage RGB indicator colour** (we have it but with wrong opcode).
4. **2K / 4K / 8K polling rate** — needs the special 0x84/0x82/0x81
   single-byte code rather than our uint16 BE write.
5. **LOD as a 3-step enum** (1 / 2 / 3 mm), inside the omnibus packet.
6. **X / Y sensitivity multipliers** (0..100 %).
7. **Angle-snap toggle**.
8. **Motion-smoothing toggle** (`smooth` bit).
9. **Power-save mode toggle** + **sleep-time** (4 × uint16 seconds —
   BT-idle, BT-deep, 24G-idle, 24G-deep).
10. **Battery-LED dual-colour** (high-charge colour, low-charge colour) +
    "charging" indicator toggle.
11. **Logo / wheel LED separate** from main RGB (separate 8-byte block).
12. **Wheel-to-button / button-to-wheel mapping** (bytes 15/16).
13. **Light-off / wheel-light-off bits** (independent of brightness).
14. **20 onboard macros**, 256 bytes each, with packed delay/keyboard/
    mouse-button/mouse-move events.
15. **Firmware version query** (`FEA_CMD_GET_REV = 0x80`).
16. **Firmware OTA** over HID — two-stage `MLEDBOOTLOADER` / `OLEDBOOTLOADER`
    handshake + 64-byte chunk upload + int32-LE checksum verify.
17. **TFT LCD** image / GIF upload (AJ159 has a tiny screen).
18. **Factory reset** (`FEA_CMD_SET_RESERT = 0x02`).
19. **Fn-layer key-matrix** (`0x51` / `0xd1`).
20. **Audio / "WINDOS" platform-quirk flags** (n/a for our scope).
21. **Light effect catalogue** (10 effect types beyond just "static" and
    a single "effect" enum we expose).
22. **Wireless re-pair / clear-BLE-info** (`0x01`, `0xe1`).
23. **Dongle pairing**: detected via `iot_driver`'s built-in supported
    device list; the "common dangle" flag (`dongle_common = true` for
    PIDs ending in even+1) marks dongles that pair both keyboard +
    mouse — we discover them automatically by reading the `Device.is24`
    flag from `watchDevList`.

---

## Persistence model

- **Storage engine**: `sled 0.34.7` embedded KV store, opened by the
  Rust driver on demand. Each "table" is a separate sled tree at:
  - Windows: `%APPDATA%\AJAZZ Driver（R）\iot_db\<table>\`
  - Browser fallback (web-driver): `web_driver/iot_db/<table>` (cwd)
- **Tables** (renderer line 57534):
  `device_type`, `CONFIG`, `macro`, `screen`, `screen_image`, `user`,
  `custom_light`, `db_custom_light_image`, `db_img_share`, `db_light_share`,
  `audio`, `gun`.
- **Encoding** (line 57514): JSON.stringify → UTF-8 bytes; binary blobs
  (images, macro buffers) are stored raw as `Uint8Array`. Reads run
  through `JSON.parse(base64Decode(value))`.
- **Sync direction**: host is the source of truth. The renderer reads
  the on-device state once at startup (via `GET_OPTIONPARAM0` /
  `GET_OPTIONPARAM1` etc.), caches it in MobX stores, and writes any
  changes immediately both to the device AND to sled. There is **no
  conflict resolution**: re-plugging a mouse with locally-cached
  settings will silently overwrite whatever was on the device.

---

## Anti-features (do NOT copy)

1. **`getWeather` gRPC call** — the iot_driver pulls weather data from a
   remote API to display on the AJ159's mini-LCD. The host's request
   leaks the user's typed `address` field to the vendor server. Skip
   the feature entirely or use a configurable, opt-in weather provider.
2. **`watchSystemInfo` stream** — pushes CPU temp, disk usage, mem,
   network throughput to the renderer (purportedly so the LCD widget
   can show them). For our backend we have no reason to expose host
   telemetry; if we ever support the LCD, gate the data feed behind an
   explicit opt-in toggle.
3. **`api.rongyuan.tech:3814` / `api2.qmk.top:3814` / `api3.qmk.top:3816`
    cloud endpoints** — login, password recovery, "shared configurations"
    download. **Mandatory account sign-in** to fetch community profiles.
   We have no need for this; if we add profile-sharing later, it should
   be over a self-hostable backend or git-tracked JSON files.
4. **`universal-analytics` + `node-machine-id`** dependencies — the
    package declares `universal-analytics` (Google Analytics over HTTP).
    The actual `trackEvent` / `trackPage` functions in the shipped
    renderer are empty stubs (line 970474 — `L7a.trackEvent = function() {}`
    body is `void 0`), so the binary as shipped is *probably* not
    phoning home. **But `node-machine-id` is still imported and creates
    a stable, hardware-derived UUID** that could be sent later.
    Do not depend on either in our reimplementation.
5. **Recoil-control / rapid-fire macros (`FEA_CMD_SET_CONTROLRECOIL`
    0x60 + `FEA_CMD_SET_DOWNCOUNT` 0x55)** — anti-cheat liability.
    Many competitive games ban this. We should not expose it.
6. **iot_driver runs at 127.0.0.1:3814 in plaintext gRPC** — any
    process on the host can issue arbitrary HID feature reports through
    the gRPC API while the vendor app is running. Even the loopback
    binding doesn't help: there's no mutual auth, no token. **Our
    architecture (in-process hidapi via `ITransport`) is strictly
    safer.**
7. **`changeWirelessLoopStatus` semantics** — pauses the dongle's HID
    polling loop. Useful for OTA. Make sure we never leave it `lock=true`
    on app exit (the vendor app has no on-exit cleanup hook visible).

---

## UX patterns worth copying

1. **Per-profile DPI table** — 8 rows (one per stage), each row has a
    DPI numeric input, a "current" radio button, and a colour swatch.
    Empty stages (DPI = 0) render greyed-out. Our QML `DpiStageModel :
    QAbstractListModel` can drive a `TableView` 1:1 with this.
2. **Polling-rate radio group** — 7 buttons, disabled-with-tooltip for
    rates above the wireless cap. Pull the list from a `reportRate`
    array per-SKU (vendor does this with the `layout.reportRate` array
    we saw at lines 59565 / 59578).
3. **One omnibus "save" button** instead of saving on every input
    change — maps cleanly to the vendor's single-packet `0x53`
    write. Reduces flicker on the AJ159's RGB.
4. **Macro recorder** with timestamped event list, record button,
    insert-delay button, and a "repeat count" stepper at the top.
    Vendor splits delay records as `≤127 ms` packed into the previous
    keyboard byte, `>127 ms` as a uint16 — replicate this packing in
    our Qt model so the on-device byte count is identical.
5. **Light-effect picker** as a dropdown with type, then a contextual
    options row (colour picker for static, speed slider for animated,
    direction enum for wave). This is exactly what
    `_LightSettingToBuffer` does — keep the same data shape.
6. **Side bar nav** — Profile selector, then tabs: `Buttons`, `DPI`,
    `Polling`, `Sensor (LOD + sensitivity + smooth + angle-snap)`,
    `Lighting`, `Macros`, `Sleep`, `About / Firmware`.

---

## Modern Qt6 reimplementation recommendations

| Vendor concept | Our Qt6 / C++ replacement |
|----------------|---------------------------|
| Out-of-process Rust gRPC daemon | Keep `ITransport` in-process (we already use hidapi from the same binary). No external daemon. |
| `_RateToNum` byte map | `constexpr std::array<std::pair<std::uint16_t, std::uint8_t>, 7>` lookup in `aj_series.cpp`. |
| Per-SKU `layout` config (DPI count, max, reportRate list) | Promote `SkuCapabilities` struct in `capabilities.hpp` and load from a `static constexpr` table keyed by PID. |
| `OPTIONPARAM0` omnibus packet | Wrap the 64-byte buffer in an `AjSeriesOptionPacket` struct with setter helpers (`setSleepTimes`, `setSensitivity`, `setLightBlock`, …); marshall once on save. |
| 8 onboard profiles | Add `uint8_t profileIndex` to every `IMouseCapable` setter; default to `currentProfile` cache. |
| Macro 256-byte payload | New `MacroBuffer` value-type class in `mouse_capable.hpp`, with `append(KeyboardEvent)`, `append(MouseButtonEvent)`, `append(MouseMoveEvent{dx,dy})`, `setRepeatCount(u16)`. |
| GIF / LCD stream | Defer to v1.x — file a follow-up issue `mouse-lcd-uploader.md`. The chunked 56-byte protocol is simple but the image-encode step (16-bit RGB565 vs 24-bit RGB) needs a dedicated converter. |
| sled KV store | Use Qt's `QStandardPaths::AppLocalDataLocation` + `QSettings(IniFormat)` for simple key/value (profile cache, last-known-DPI). Avoid embedding a full KV engine. |
| Cloud share / weather | Drop. If profile-sharing demand emerges, export/import `.ajprofile` JSON files via the existing profile system. |
| `watchVender` / `watchSystemInfo` streams | Drop entirely. |
| Battery via gRPC `Device.battery` | On Linux, read `/sys/class/power_supply/hid-<vid>:<pid>.*/capacity` or query the dongle's input-report ringer; gate behind `isWireless()`. |
| Light effect enum (10 types) | Promote our `RgbEffect` enum to match: `Off`, `Static`, `Breathe`, `Neon`, `Wave`, `Dazzling`, `Laser`, `MusicFollow1`, `ScreenColor`, `MusicFollow2`. |
| Checksum mode | **Switch `aj_series.cpp` makeEnvelope to compute `sum & 0x7f`** (BIT7) for AJ-series mice. Verify with USBPcap once a real capture path is available (see CAPTURING.md). Keep BIT8 (`& 0xff`) as a fallback for legacy SKUs and pick per-PID. |

---

## Action items for our backend (priority order)

1. **CRITICAL**: change `kCmdCommit = 0x50` — it is colliding with
   `FEA_CMD_MOUSE_SET_KEYMATRIX`. Either remove the commit step
   entirely (vendor does no commit) OR rename and re-purpose.
2. **CRITICAL**: opcode `kCmdButton = 0x24` is the wrong number AND
   the payload starts at offset 4 instead of offset 8. Replace with
   `0x50` and the 8-byte offset.
3. **CRITICAL**: `kCmdDpi = 0x21` should be `0x54`, payload is
   uint16-**LE** (we use BE), 8 stages atomic (we do one-at-a-time),
   RGB block at byte 40 not byte 4.
4. **HIGH**: `kCmdPollRate = 0x22` should be `0x04`, single byte
   from `_RateToNum` table (not uint16 BE).
5. **HIGH**: `kCmdLod = 0x23` does not exist standalone; LOD is byte
   52 of the omnibus `0x53` write. Fold into a future
   `setMouseOption0()` helper.
6. **HIGH**: `kCmdRgb = 0x30` should be `0x07`, 8-byte payload as
   described above. Brightness sub-cmd 0x02 must be removed.
7. **HIGH**: `kCmdBattery = 0x40` — replace with a dongle-status
   callback for wireless SKUs; return `std::nullopt` for wired AJ159
   APEX.
8. **MEDIUM**: verify the checksum mode (BIT7 vs BIT8). Without a
   live USBPcap capture (blocked on xHCI/USB4 — see CAPTURING.md
   §8.6 usbipd-win workaround), the only definitive way is to
   reverse-engineer `iot_driver.exe` further. Provisional
   recommendation: implement BIT7 first (`sum & 0x7f`), keep BIT8
   as a config flag.
9. **MEDIUM**: add 8-profile support to `IMouseCapable`.
10. **MEDIUM**: bump `dpiStageCount()` from 6 to 8.
11. **MEDIUM**: add `firmwareVersion()` via `FEA_CMD_GET_REV (0x80)`.
12. **LOW**: macro upload (256-byte format, 20 slots).
13. **LOW**: factory-reset opcode `0x02`.
14. **DEFER**: LCD upload, OTA, recoil-control (last is anti-feature).

---

## References (file:line citations)

All paths relative to
`C:\Users\unilo\reverse-eng-workdir\aj159_apex\extracted\app32\resources\app\`.

| Subject | File:Line |
|---------|-----------|
| AJ159 APEX SKU entry (wired) | `dist/static/js/main_beautified.js`:61771–61785 |
| AJ159 APEX SKU entry (wireless) | …:61787–61801 |
| AJ159 layout `Ki` (8K-capable) | …:59571–59583 |
| AJ159 layout `Hi` (1K wireless) | …:59558–59570 |
| AJ159 other-settings `_l` | …:60105–60125 |
| Default mouse key-matrix `N9r` | …:920363 |
| Mouse FEA_CMD enum (full, ~70 entries) | …:920839 |
| `setMouseOption0` (omnibus 0x53) | …:921121–921149 |
| `getMouseOption0` parser | …:921150–921182 |
| `setMouseOption1` (DPI table 0x54) | …:921182–921215 |
| `getMouseOption1` parser | …:921216–921248 |
| `setReportRate` | …:921271–921301 |
| `_RateToNum` / `_NumToRate` | …:920911–920967 |
| `_LightSettingToBuffer` | …:920967–921015 |
| `_BufferToLightSetting` | …:921015–921094 |
| `_SleepTimeToBuffer` | …:921097–921099 |
| `setLightSetting` (LED 0x07) | …:920856–920876 |
| `setKeyConfigSimple` (KEYMATRIX 0x50) | …:921870–921908 |
| `setFnKeyConfigSimple` (FNMATRIX 0x51) | …:921909–921940 |
| `setMacro` (256-byte payload assembly) | …:922019–922078 |
| `_setMacro` (0x16 chunked upload) | …:922079–922119 |
| `setProfile` (0x05) | …:921345–921362 |
| `getProfile` (0x85) | …:921326–921344 |
| `getFirmwareVersion` (0x80) | …:921258–921270 |
| `oledUpgrade` (firmware OTA flow) | …:817237–817367 |
| `mledUpgrade` (firmware OTA flow) | …:817368–~817500 |
| TFT LCD upload (0x25 / 0x29) | …:817181 |
| `writeFeatureCmd` (transport wrapper) | …:726774–726816 |
| `commomFeature` (write+read helper) | …:726843–726874 |
| `writeRawFeatureCmd` | …:726899–726929 |
| `readRawFeatureCmd` | …:726930+ |
| gRPC client construction (host = `http://127.0.0.1:3814`) | …:56600 |
| gRPC wrappers (`no`, `oo`, `uo`, `io`, `lo`, …) | …:56601–56683 |
| `proto.driver.CheckSumType` / `DangleDevType` / `LightType` enums | …:51241–51262 |
| Cloud endpoints (`api.rongyuan.tech:3814` etc.) | …:53299–53302 |
| Analytics stubs (`L7a.trackEvent = function() {}`) | …:970474–970476 |
| sled DB path + table names + DB wrappers | …:57509–57642 |
| Telemetry stream `SystemInfo` proto | …:51085–51140 |
| `Device.battery` field | …:50769, 50841–50844 |
| `getWeather` gRPC method | …:56128, 56236, 56760–56770 |
| Rust binary VID/PID device list | `iot_driver.exe` (strings) |
| Rust binary gRPC method names | `iot_driver.exe` (`/driver.DriverGrpc/...` strings) |
| Rust binary build path | `iot_driver.exe`: `D:\work\dj_hid_sdk_rs\target\i686-pc-windows-msvc\release\…\driver.rs` |
| Rust HID transport modules | `iot_driver.exe`: `src\dj_hid_device\dj_hid\hid_normal.rs`, `…\hid_ble.rs`, `…\vender_dev.rs` |
| Rust persistence engine | `iot_driver.exe`: `sled-0.34.7` (`…\sled-0.34.7\src\tree.rs`) |

---

## Cross-references

- Our clean-room doc: `docs/protocols/mouse/aj_series.md`
- Our current backend: `src/devices/mouse/src/aj_series.cpp`
- Capture workflow (blocked on xHCI): `docs/capturing/CAPTURING.md` §8.6
- Related: `docs/research/builtin-plugin-categories.md`, `docs/research/feature-matrix.md`
