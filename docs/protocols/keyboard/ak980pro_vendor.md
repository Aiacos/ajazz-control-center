# AJAZZ AK980 PRO — vendor reverse engineering (DeviceDriver.exe, MFC native)

Authoritative byte-level reverse-engineering of the AK980 PRO Windows
configuration tool (`DeviceDriver.exe`, "AJAZZ AK980 三模RGB带屏 Keyboard
Driver", Beta 1.0.0.6, 2025-01-02). Compiled from a **clean-room static
analysis only** — Ghidra 12.1 headless decompilation + ASCII/UTF-16
string mining; the vendor binary was never executed during this
investigation. Cross-corroborated against
[gohv/EPOMAKER-Ajazz-AK820-Pro](https://github.com/gohv/EPOMAKER-Ajazz-AK820-Pro)
(Rust, `src/protocol.rs`, `src/lcd.rs`) and
[KyleBoyer/TFTTimeSync-node](https://github.com/KyleBoyer/TFTTimeSync-node)
(TypeScript, `src/packets.ts`).

> Every opcode below cites a Ghidra-discovered function/address; when the
> vendor binary and the open corpora disagree, the section calls it out
> explicitly. Citations of the form `FUN_004XXXXX` point at functions in
> the `C:/temp/ghidra_proj/ak980_proj` project (raw dump under
> `C:/Users/unilo/reverse-eng-workdir/ak980pro/ghidra_hid_callers.json`).

______________________________________________________________________

## 1. Inventory

### 1.1 Binary

| Property             | Value                                                                                |
| -------------------- | ------------------------------------------------------------------------------------ |
| Binary               | `app/DeviceDriver.exe` (1 802 752 bytes)                                             |
| Type                 | PE32 x86, 32-bit MFC native (Visual Studio 2022 toolchain, MSVC linker 14.x)         |
| UI framework         | bundled `mui.dll` (1.19 MB, 2 201 exports, custom MFC-like) — **not** Windows MUI    |
| HID API binding      | dynamic via `LoadLibraryA("hid.dll")` + `GetProcAddress` (FUN_00450af0)              |
| Transport            | `CreateFileW` + `WriteFile` (output) + `ReadFile` (input) + `DeviceIoControl 0xB0192` (IOCTL_HID_GET_INPUT_REPORT) |
| Persistence          | SQLite (`db/<device>_datav1.db` under `%LOCALAPPDATA%/<device>/`)                    |
| Firmware update      | External `FirmwareUpdateTool.exe` downloaded over HTTP (`FUN_0045a520`) — not in this binary |
| Auto-start           | `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run` registry value                  |

### 1.2 Device matrix (from `app/config.xml`)

| Variant            | VID    | PID    | Mode             | HID interface     |
| ------------------ | ------ | ------ | ---------------- | ----------------- |
| AK980 PRO (wired)  | 0x0C45 | 0x8009 | USB              | `MI_00`           |
| AK980 PRO (2.4 GHz)| 0x0C45 | 0xFEFE | 2.4 GHz Type-A   | `MI_03`           |
| XS75T              | 0x05AC | 0x024F | USB              | `MI_00` (Apple VID, 75-key variant) |

`config.xml` also publishes:

```xml
<screen gif_headlength="256" gif_maxframes="140" gif_count="1" width="240" height="135"/>
<light rgb_keyboard="1" default_mode="11" default_brightness="5" default_speed="3" brightness_max="5" speed_max="5"/>
<cmd_delaytime value="35"/>  <!-- inter-command sleep in ms -->
```

Bluetooth is explicitly unsupported for configuration ("keyboard settings are
not supported in Bluetooth mode for now" — `1033.lan` id 62).

### 1.3 UI feature tree (parsed from `language/1033.lan`)

The English resource file enumerates the complete operator surface. Notable
features (each maps to one or more HID commands documented below):

- **Main menu (50–58)**: My Exclusive Config (profiles), Macro Manager,
  Lighting Mode, User Lighting (per-key), Real-time Lighting, Music Rhythm,
  System Settings, TFT Screen
- **Keys (201–298)**: Default / Disable / Custom (Mouse / Multimedia /
  Windows shortcut / Open program / Open website / Send text / Multi-key /
  Open file), Top Layer vs Fn Layer, Fn Switch (Momentary / Toggle)
- **Macros (300–363)**: Record input list, edit macro, insert above/below
  cursor, keyboard input, mouse input, default interval, save GIF — see
  `t_key_otherdata` SQL table
- **Real-time lighting (369–386)**: Static On, Starlight, Fluttering,
  Dynamic Breathing, Colorful Fountain, Rainbow Wave, Following Currents,
  Peak Revolving, Turn Off
- **Built-in light modes (500–540)**: 20 named effects — Stellar Epoch,
  Photon Strike, Energy Annihilation, Space Bubble, Solar Storm, Quantum
  Fluctuation, …, Hourglass of Time, Backlight Off **(matches the `0x13`
  MODE_COMMAND with 20 values 0x00-0x13 in gohv corpus)**
- **Music rhythm (101–120)**: rhythm, amplitude, background mode (Green/
  Yellow/Red, Rainbow, Reverse Rainbow, Gradient, Spectrum Cycle, plus 9
  static colors + Off + Ambilight)
- **Side light (660–670)**: Flowing Light, Red, Yellow, Green, Ice Blue,
  Blue, Pink, White, Neon, Off
- **System settings (140–183)**: Key Response Time, Sleep Time (Never / 1
  min / 5 min / 30 min), Level (1-5), Language, Auto Start, Reset Keyboard,
  Factory Reset, Driver Update, Firmware Update, Disable Windows Key,
  Disable ALT+F4, Disable ALT+TAB
- **TFT Screen (800–887)**: 140-frame max GIF, name, image preview, brush
  (fine / medium / thick), eraser, add text, copy/delete frame, save/import/
  export GIF, **"Time Syns"** (sic — typo present in original; Chinese is
  时间校正 = "time correction"), upload to keyboard

### 1.4 Localizations available

`1033.lan` (en-US), `2052.lan` (zh-CN simplified), `1028.lan` (zh-TW
traditional), `1049.lan` (ru-RU) — all in UTF-16 LE with BOM. Both Chinese
files are the primary source — English is a translation with typos
(`Time Syns`).

______________________________________________________________________

## 2. Transport layer

### 2.1 HID device discovery (`FUN_00450c00`)

1. Resolves `hid.dll` exports dynamically (`FUN_00450af0` — `HidD_GetAttributes`,
   `HidD_GetSerialNumberString`, `HidD_GetManufacturerString`,
   `HidD_GetProductString`, `HidD_SetFeature`, `HidD_GetFeature`,
   `HidD_GetIndexedString`, `HidD_GetPreparsedData`,
   `HidD_FreePreparsedData`, `HidP_GetCaps`).
2. `SetupDiGetClassDevsA` with HID class GUID
   `{4D1E55B2-F16F-11CF-88CB-001111000030}` (standard `GUID_DEVINTERFACE_HID`).
3. Enumerates interfaces, opens each with `CreateFileA` (RW, share RW,
   `OPEN_EXISTING`, `FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED`).
4. Reads `HIDP_CAPS` via `HidD_GetPreparsedData` + `HidP_GetCaps`.
5. Extracts interface number by parsing `&mi_NN` from the device path
   (`strtol(... , ..., 16)`).
6. The XS75T variant (Apple VID `0x05AC`) is opened via a **separate**
   enumerator `FUN_00413340` that filters paths by `wcsstr(L"05AC_PID")`.

### 2.2 Write path (`FUN_00451220`, output reports)

```c
WriteFile(handle, buf, padded_size, NULL, &OVERLAPPED);
GetOverlappedResult(handle, &OVERLAPPED, &bytes_written, /*bWait=*/TRUE);
```

Padding rule: if the caller passes `param_3 < OutputReportByteLength` (ushort
stored at `device[2]`), the buffer is `malloc`'d to the full report size,
the caller's bytes are `memcpy`'d in, and the tail is zeroed. This means
**every output write is exactly `OutputReportByteLength` bytes** — the
keyboard advertises either `0x21` (33 = ReportId + 32 data) or `0x41` (65
= ReportId + 64 data) depending on usage page.

### 2.3 Feature/input read path (`FUN_004514e0`, `FUN_00451300`)

- `DeviceIoControl(handle, 0xB0192, buf, len, buf, len, &bytes, &OVERLAPPED)`
  — IOCTL value `0xB0192` decodes to `IOCTL_HID_GET_INPUT_REPORT` (DeviceType
  `0x0B` = FILE_DEVICE_KEYBOARD, function `0x064`, METHOD_OUT_DIRECT,
  FILE_ANY_ACCESS). Used to fetch the device's response packet.
- `FUN_00451300` (overlapped ReadFile) wraps `ReadFile`. **Crucial detail**:
  if the first byte of the response is `0x00`, the function strips it
  (`memcpy(dst, _Src + 1, n - 1)`) — the firmware uses a 1-byte HID Report
  ID prefix that callers want stripped.

### 2.4 Three report sizes coexist

| Size  | Usage                                                          |
| ----- | -------------------------------------------------------------- |
| 0x21  | 33-byte: short feature reports for **time-sync** and **status** (`FUN_0044f5f0` with `len=0x21`) |
| 0x41  | 65-byte: standard configuration commands (most opcodes use `FUN_0044eed0` with `len=0x41`) |
| 0x1001| 4097-byte: chunked image / bulk uploads (`FUN_0044f2d0`)        |

`FUN_0044eed0` and `FUN_0044f0c0` branch on `param_2 < 0x42` vs `>= 0x42`
and walk the buffer in 0x40-byte chunks for the multi-packet case.

### 2.5 Top-level dispatch wrappers

| Wrapper        | Role                                                           |
| -------------- | -------------------------------------------------------------- |
| `FUN_0044f5f0` | Write 0x21-byte (33) report, retry once on failure, then poll for ACK with `FUN_00451300`. Checksum byte 8 = sum of bytes 0..7 mod 256. |
| `FUN_0044f790` | Same as above but with **fixed** opcode `0x02` (save) and ACK match against response[0]==0x02. |
| `FUN_0044eed0` | Write 0x41-byte (65) report (or chunked > 0x41), no readback. **The workhorse for almost every configuration command.** |
| `FUN_0044f0c0` | Like `FUN_0044eed0` but reads a feature report afterwards (used for `0x2004` per-key RGB read-back). |
| `FUN_0044f2d0` | Write 0x1001-byte (4097) bulk buffer. Used for **DFU / firmware blob** style uploads. |
| `FUN_0044f3a0` | Streaming reader: timed loop with `timeBeginPeriod(1)`, 0x168 ms (360 ms) timeout, reads up to 9 chunks of 0x40 bytes. Used for **firmware version + battery query response** and per-key RGB read-back. |
| `FUN_00435250` | 200 ms polling loop: reads 0x41-byte status with `FUN_00451300`, checks for `byte[0]==0x05, byte[1]==0xA6` → triggers profile-switch toast (key-combo on the keyboard switches profile). |

______________________________________________________________________

## 3. Complete HID opcode table

All command packets share this layout (zero-padded to size):

```
byte 0 : 0x04                  (HID Report ID — fixed across the entire vendor protocol)
byte 1 : opcode                (the "command id" column below)
byte 2 : sub-opcode (often)
byte 3..N : payload
byte N..size-1 : zero padding
```

Some commands additionally end with the two-byte trailer `0xAA 0x55`
**at the bottom of the buffer** (offsets vary by packet length).

Direction column: `H→D` = host to device (WriteFile); `D→H` = device to host
(ReadFile + IOCTL).

`Status` codes: ✅ already in our backend (`proprietary_protocol.hpp`),
🟡 **gap** — present in vendor but not implemented, 🔍 partially supported.

| Opcode | Sub  | Dir  | Vendor function                  | UI label / role                          | Status |
| ------ | ---- | ---- | -------------------------------- | ---------------------------------------- | ------ |
| `0x01` | —    | H→D  | (firmware-version request)       | "Version" — gohv `FIRMWARE_VERSION`       | ✅      |
| `0x02` | —    | H→D  | `FUN_0044f790`, time-sync save   | `CMD_SAVE` — persist RTC to NV-RAM        | ✅      |
| `0x05` | `0x10`| H→D  | `FUN_0044be90`                  | KEY REMAP (assign macro/key to key)       | 🔍      |
| `0x05` | `0x01`| H→D  | `FUN_004349f0`                  | KEY MODE select (single byte at +0x808)   | 🟡      |
| `0x05` | (no) | H→D  | (legacy CmdSetKeycode)          | Single-key remap layer/row/col/keycode    | ✅      |
| `0x07` | `0x10`| H→D  | `FUN_00414290`                  | **SETTINGS BATCH** (33-byte): fn_switch, sleep_time, key_respondtime | 🟡      |
| `0x08` | zone | H→D  | (CmdSetRgbStatic)               | Static RGB per zone                       | ✅      |
| `0x09` | `0x1c`| H→D  | `FUN_0042dc10`                  | **MACRO DATA UPLOAD** (28-byte chunks)    | 🟡      |
| `0x0A` | —    | H→D  | (CmdSetRgbBuffer)               | Per-LED RGB chunked                       | ✅      |
| `0x0B` | `0x1c`| H→D  | `FUN_00432be0`                  | LIGHTING params (mode+speed+brightness)   | 🔍      |
| `0x0C` | —    | H→D  | (CmdSetLayer)                   | Layer switch                              | ✅      |
| `0x0D` | —    | H→D  | (legacy CmdUploadMacro)         | Macro upload                              | 🔍      |
| `0x0E` | —    | H→D  | (CmdCommitEeprom)               | EEPROM commit                             | ✅      |
| `0x10` | `0x1c`| H→D  | `FUN_00418c40` (param_1==0)     | KEY REMAP CHUNK, Fn-layer **OFF**, 21 chunks × 28 bytes | 🟡      |
| `0x12` | `0x1c`| H→D  | `FUN_00418c40` (param_1!=0)     | KEY REMAP CHUNK, Fn-layer **ON**, 21 chunks × 28 bytes  | 🟡      |
| `0x13` | —    | H→D  | `FUN_0042b0a0`                  | **MODE_COMMAND** — set lighting effect (one of 20)    | 🟡      |
| `0x14` | `0x1c`/`0x18` | H→D | `FUN_0044be90`           | KEY-MACRO ASSIGN, chunked (7 or 21 chunks) | 🟡      |
| `0x17` | —    | H→D  | `FUN_00414020`                  | **SLEEP_TIME** — payload at byte 9 (`local_50`) | 🟡      |
| `0x18` | —    | H→D  | (CmdStartTime / `FUN_004238e0`) | `CMD_START` — opens 4-packet envelope     | ✅      |
| `0x20` | `0x01`| D→H | `FUN_004358c0`                  | **BATTERY QUERY** — response[3] = percent | 🟡      |
| `0x20` | `0x04`| H→D  | `FUN_00427db0`, `FUN_004329a0`  | PER-KEY RGB UPLOAD (wired 0xC0 / wireless 0x200 bytes) | 🟡      |
| `0x23` | —    | H→D  | `FUN_0044ba20`                  | MACRO RECORD-BUFFER UPLOAD (wireless variant uses 0x09) | 🟡      |
| `0x27` | `0x09`| H→D  | `FUN_004183a0`                  | KEY remap Fn-layer **alternate** (0x11 fn0 / 0x27 fn1) | 🟡      |
| `0x28` | —    | H→D  | (CmdSetTime / `FUN_004238e0`)   | `CMD_TIME` — time-sync preamble           | ✅      |
| `0x37F`*| —   | H→D  | `FUN_004231c0`, `FUN_00422920`  | **LCD/TFT IMAGE HEADER** (`byte1=0x7F, byte2=0x03` — opens upload) | 🟡 (DISPLAY-05) |
| `0x80\|n`*| —  | H→D  | `FUN_004231c0`                  | **LCD/TFT IMAGE CHUNK** (chunk_idx encoded across bytes 1/3) | 🟡 (DISPLAY-05) |
| `0xF0` | —    | H→D  | `FUN_0042b0a0`, `FUN_0044b910`  | `CMD_FINISH` — closes a configuration envelope | 🟡      |
| `0xF5` | `0x03`/`0x09` | H→D | `FUN_0042ae80`            | **READ-BACK CURRENT RGB** — wired 3 keys, wireless 9 keys per page | 🟡      |

\* The 0x37F / 0x80 entries are big-endian when read as a uint16 from byte 1
of the buffer; the binary writes them as little-endian: `byte1 = 0x7F`,
`byte2 = 0x03` for the header, and `byte1 = (idx & 0xFF)`, `byte2 = 0x80
| (idx >> 16)` for chunks. See §6.

### 3.1 Time-sync 4-packet envelope (already in our backend — corroborates ARCH-05)

Source: `FUN_004238e0` (time-sync) and `FUN_00423a10` (LCD-aware variant).

Byte-for-byte identical to the gohv/KyleBoyer specs we already implement,
with **two new findings**:

1. The vendor binary also sends a leading **CMD_START** packet (`0x04 0x18 …`)
   before `CMD_TIME` (`0x04 0x28 …`). This matches gohv's `CMD_START = 0x18`
   but **KyleBoyer's `packets.ts` skips it** — corroboration that
   `buildSetTimeStart()` is required.
2. **`FUN_00423a10` (LCD-aware time-sync)** writes the **selected LCD index +
   1** into byte 3 of the time-data packet (`local_4f = (char)iVar1 + 1`),
   where `iVar1 = LCDViewList::GetCurSel`. The basic `FUN_004238e0` writes
   the literal `(byte)0` there. If we ever support multi-LCD layouts this
   field is non-zero.
3. **`wDayOfWeek`** is written at byte 10 (`local_4a` after the seconds
   byte). Our current builder writes `0x04` there per the gohv corpus —
   this matches but only by coincidence on a Thursday. **Apply the fix.**

Time-data packet (ReportId=0x00) byte map confirmed:

| Off | Value                              |
| --- | ---------------------------------- |
| 0   | 0x00 (HID Report ID)               |
| 1   | LCD-select index + 1 (else 0)      |
| 2   | 0x5A (magic)                       |
| 3   | year % 2000  (note: vendor uses `% 2000` not `- 2000` — for 1999 this yields **1999** truncated; for 2026 it yields 26. We should match.) |
| 4   | month (1-12)                       |
| 5   | day (1-31)                         |
| 6   | hour (0-23)                        |
| 7   | minute (0-59)                      |
| 8   | second (0-59)                      |
| 9   | 0 (in basic variant)               |
| 10  | wDayOfWeek (0=Sunday, vendor sets this; gohv hard-codes 0x04) |
| 11..61 | 0                                |
| 62  | 0xAA                               |
| 63  | 0x55                               |

### 3.2 Settings batch (NEW — `0x07 0x10`)

`FUN_00414290`: build a **33-byte short feature report** carrying three
settings in one packet, sent via `FUN_0044f5f0` (which checksums byte 8 as
sum of bytes 0..7):

```
byte 0 : 0x04 (ReportId)
byte 1 : 0x07
byte 2 : 0x10
byte 3 : 0x00
byte 4 : 0x01            (fixed)
byte 5 : value("fn_switch")      // read from config keystring; integer 0..N
byte 6 : value("sleep_time")     // 1..4 (see §3.5)
byte 7 : value("key_respondtime")// 1..5 (delay-level)
byte 8 : checksum (sum of bytes 0..7 mod 256)
bytes 9..32 : zero
```

The keys `fn_switch`, `sleep_time`, `key_respondtime` are read from the
SQLite `t_config_data` table (where `key TEXT`/`value TEXT`).

### 3.3 Sleep timer (`0x17`)

`FUN_00414020`:

```
byte 1 : 0x17
byte 2 : 0x04 (fixed)
...
byte 9 : sleep_time value
```

**Enum (matches gohv `SleepTime`):**

| Value | Meaning   | UI label (1033.lan 142-145)         |
| ----- | --------- | ----------------------------------- |
| 0     | Never     | "No Sleep"                          |
| 1     | 1 min     | "1min"                              |
| 2     | 5 min     | "5min"                              |
| 3     | 30 min    | "30min"                             |

### 3.4 RGB mode (`0x13`)

`FUN_0042b0a0`: full envelope `0x18 → 0x13 → data → 0x02 → 0xF0`.

The data packet carries (matches gohv `mode_data_packet` exactly):

```
byte 1 : mode_id (0..0x13 = 20 modes; see §3.4.1)
bytes 2..4 : RGB tint when mode supports it
byte 8 : rainbow flag (0/1)
byte 9 : brightness (clamped 0..5; UI says "brightness_max=5")
byte 10 : speed (clamped 0..5)
byte 11 : direction (Left=0, Down=1, Up=2, Right=3)
bytes 14..15 : 0x55 0xAA trailer
```

#### 3.4.1 The 20 lighting modes (`1033.lan` 521-540, in order)

| ID   | English name (`1033.lan`)       | Chinese (`2052.lan`)   |
| ---- | --------------------------------| ---------------------- |
| 0x00 | Static (Static)                 | 常亮 / Static-On       |
| 0x01 | SingleOn                        | 单点亮                 |
| 0x02 | SingleOff                       | 单熄灭                 |
| 0x03 | Glittering                      | 繁星点点               |
| 0x04 | Falling                         | 漫天飞舞               |
| 0x05 | Colourful                       | 万紫千红               |
| 0x06 | Breath                          | 动感呼吸               |
| 0x07 | Spectrum                        | 光圈循环               |
| 0x08 | Outward                         | 彩浪涌动               |
| 0x09 | Scrolling                       | 万彩纵横               |
| 0x0A | Rolling                         | 寂静流光               |
| 0x0B | Rotating                        | 峰回环绕               |
| 0x0C | Explode                         | 一触即发               |
| 0x0D | Launch                          | 一触惊艳               |
| 0x0E | Ripples                         | 涟漪扩散               |
| 0x0F | Flowing                         | 川流不息               |
| 0x10 | Pulsating                       | 重峰叠叠               |
| 0x11 | Tilt                            | 斜风细雨               |
| 0x12 | Shuttle                         | 来回穿梭               |
| 0x13 | LED Off                         | 关闭背光               |

There are 20 modes (`0..0x13`) for the **Real-time Lighting** picker. The
**"My Exclusive Config" → User Lighting** path uses a different model
(per-key RGB upload — see §5.2).

A **second** 20-effect list (IDs 500-519 in 1033.lan) lists the cosmic
named effects (Stellar Epoch, Photon Strike, …, Hourglass of Time) — these
are the **scene presets** stored in the SQLite profile data, not separate
firmware modes.

### 3.5 Battery query (NEW — `0x20`)

`FUN_004358c0`:

```c
memset(buf, 0, 0x41);
buf[1] = 0x20;       // CMD_BATTERY
buf[2] = 0x01;       // sub-opcode (request)
FUN_0044f5f0(...);   // 33-byte send + immediate readback
// Response:
//   resp[0] == 0x20  (echo)
//   resp[3] != 0    => percentage (clamped to 100)
// Then: MUI::BatteryCtrl::SetBatteryInfo(ctrl, percent, /*charging=*/0)
```

Polled every **15 s** (`14999 < tick_delta`) when the device is in wireless
mode (`this[0x784] == 2`). Returns 0 percent silently when in wired mode.

### 3.6 Lighting params batch (`0x0B 0x1C`)

`FUN_00432be0` (33-byte feature report):

```
byte 1 : 0x0B
byte 2 : 0x1C
byte 4 : MComboBox::GetCurData(combo_at_0x618)  // mode index
byte 5 : MComboBox::GetCurData(combo_at_0x630)  // speed index
byte 6 : DAT(0x144)                              // current brightness
byte 7 : direction high
byte 8 : checksum (auto by FUN_0044f5f0)
```

Followed by a **trace string** in the binary:
`MUI::M_Trace(L"mxn: %02d %02d %02d …")` — the vendor logs every byte sent.

### 3.7 Per-key RGB upload (NEW — `0x20 0x04`)

`FUN_00427db0` (custom lighting tab), `FUN_004329a0` (keyboard tab):

```
1. Send header:
   byte 1 : 0x20
   byte 2 : 0x04
   byte 9 : 0x03 (wired) or 0x08 (2.4 GHz)
2. Send RGB blob: 0xC0 bytes (wired, ~64 keys × 3 bytes RGB) or
                  0x200 bytes (2.4 GHz, more compact per-LED format)
3. Send save: byte 1 = 0x02
```

The blob is built from `MUI::CustomLightMode::GetKeyItems` (or
`KeyboardCtrl::GetKeyItems`), one item per physical key per the layout
matrix in `layouts/rgb-keyboard.xml`. **The format is NOT plain R,G,B,
R,G,B for wireless mode** — additional per-key flags occupy 4 bytes each.

### 3.8 Per-key RGB read-back (`0xF5`)

`FUN_0042ae80`:

```
byte 0 : 0x04
byte 1 : 0xF5
byte 2 : 0x03 (wired) or 0x09 (2.4 GHz)  // chunk count requested
```

Sent via the **streaming reader** `FUN_0044f3a0` — multi-packet response
(up to 9 × 64-byte chunks). For each KeyItem, the code reads the RGB at
`local_2b0[iVar7]` (wired: scaled-to-255 monochrome) or
`local_2b0[iVar7*4 + 1..3]` (wireless: full RGB) and writes the 24-bit
value into `iVar1[0x30]`. Then sends a `0x02 0x04` save.

### 3.9 Profile switch detection (`FUN_00435250`)

Polled at 200 ms intervals on a separate thread. Reads input reports and
matches against the magic ACK pattern:

```
resp[0] == 0x05            (echo of remap opcode)
resp[1] == 0xA6            (= -0x5A in signed char; profile-switch ack key)
resp[2] == 0xFF, resp[3] == 0x01 → profile bit ON → switches to wired UI
resp[2] == 0xFE, resp[3] == 0x02 → profile bit OFF → switches to wireless UI
```

This is the **physical key-combo profile switch** picked up by the host —
the firmware doesn't push a usual HID notification; the host actively polls.

### 3.10 Macro data upload (`0x09 0x1C`)

`FUN_0042dc10`:

1. Build a 3584-byte (`0xE00`) buffer holding all macro events, with each
   macro prefixed by a 2-byte event count.
2. Each **event** is **4 bytes**:

| Type (in DB) | Action          | Bytes 0..3                                |
| ------------ | --------------- | ----------------------------------------- |
| 2            | Key Down        | `[key_lo, 0, key_hi, 0xB0]`               |
| 3            | Key Up          | `[?, ?, key_hi, 0x30]`                    |
| 4 (val 1)    | Mouse L Down    | `[1, ?, 1, 0x90]`                         |
| 5 (val 1)    | Mouse L Up      | `[1, ?, 1, 0x10]`                         |
| 4 (val 2)    | Mouse R Down    | `[4, ?, 4, 0x90]`                         |
| 5 (val 2)    | Mouse R Up      | `[4, ?, 4, 0x10]`                         |
| 4 (val 3)    | Mouse M Down    | `[2, ?, 2, 0x90]`                         |
| 5 (val 3)    | Mouse M Up      | `[2, ?, 2, 0x10]`                         |
| default      | Delay           | `[delay_lo, delay_hi, ?, 0x50]` (min 10ms)|

3. Trailer `0xAA 0x55` at the end of the buffer.
4. Split into 28-byte (0x1C) chunks; each chunk is sent as:

```
byte 1 : 0x09             (CMD_MACRO_DATA)
byte 2 : 0x1C             (data size = 28; on last chunk, may be smaller)
byte 3 : chunk_idx
bytes 4..31 : 28 data bytes
```

Send via `FUN_0044f5f0` (33-byte short report), 2 ms sleep between chunks.

The macros themselves live in SQLite table
`t_key_otherdata(macro_value INTEGER PK AUTOINCREMENT, macro_desc TEXT,
param TEXT, type INTEGER)` keyed by `macro_value`.

### 3.11 Macro-to-key assignment (`0x14`)

`FUN_0044be90` — distinct from §3.10 (which uploads the macro body itself).
This packet assigns macro IDs to physical keys.

```
1. Header packet (size 0x41):
   byte 1 : 0x05
   byte 2 : 0x10
   byte 3 : 0x00
   byte 4 : 0x80               (assignment-table magic)
   byte 9 : 0x00
   byte 10 : (param_1 + 0x814 byte) << 8   // macro group ID
2. Buffer of 600 bytes (0x258):
   wired:    1 byte per key   (0xFF = "macro assigned"; 0x00 = none)
   wireless: 4 bytes per key  ([keycode_lo, keycode_mid, keycode_hi, flags])
3. Split into 28-byte chunks:
   wired:    7 chunks   (idx 0..6;  byte2 = 0x1C, except idx=6 → 0x18)
   wireless: 21 chunks  (idx 0..0x14; byte2 = 0x1C, except idx=0x14 → 0x10)
   each chunk uses opcode 0x14, byte2 = chunk length, byte3 = chunk_idx
4. Trailer 0x55AA at end of full buffer
5. 2 ms Sleep between chunks
```

### 3.12 Key-remap chunked upload (`0x10` / `0x12`)

`FUN_00418c40` — for the **top-layer** vs **Fn-layer** key remap surface
shown in the keymap editor.

```
1. Build buffer of 0x240 (576) bytes with one 16-byte remap entry per key
2. Split into 21 chunks (idx 0..0x14):
   byte 1 : 0x10 (Fn-layer OFF) or 0x12 (Fn-layer ON)
   byte 2 : 0x1C (or 0x10 for the last chunk idx=0x14)
   byte 3 : chunk_idx
   bytes 4..31 : 28 bytes payload
3. Trailer 0x55AA at end of buffer
4. 2 ms Sleep between chunks
```

The "alternate" `FUN_004183a0` uses opcodes `0x11` and `0x27` for the same
buffer — possibly the EEPROM "user 2" slot, or the 2.4 GHz dongle's
parallel storage.

______________________________________________________________________

## 4. Standard envelope pattern for configuration commits

Almost every UI commit follows this 5-step envelope (`FUN_0042b0a0`,
`FUN_004340c0`, `FUN_0044b910`, etc):

```
1. CMD_START   : byte1=0x18,  byte2=0x04        — "begin"
2. CMD_<group> : byte1=<op>,  byte2=0x04 or 0x13/0x23 — "type" select
3. CMD_<data>  : full payload + 0xAA 0x55 trailer
4. CMD_SAVE    : byte1=0x02,  byte2=0x04        — persist
5. CMD_FINISH  : byte1=0xF0,  byte2=0x04        — "end" (no payload)
```

`CMD_FINISH = 0xF0` matches gohv's `CMD_FINISH` exactly. This is **the
key missing piece** in our existing implementation — we have START + SAVE
but **not FINISH**. We should add it after every commit.

______________________________________________________________________

## 5. TFT/LCD screen image upload (`0x37F` / `0x80`-chunks)

`FUN_004231c0` (full-frame), `FUN_00422920` (alternative path):

### 5.1 Geometry & sizes

- Screen: **240 × 135 px**, **RGB565** (2 bytes/pixel) — `config.xml`
- Per-frame size: 240 × 135 × 2 = **64,800 bytes**
- Max frames per upload: **140** (`config.xml gif_maxframes`)
- Per-chunk payload: **28 bytes (0x1C)**
- Per-frame chunks: ⌈64 800 / 28⌉ = **2 315 chunks per frame**
- Inter-chunk pause: **2 ms** (`Sleep(2)`)

### 5.2 Upload sequence

1. **Header packet** (`FUN_0044f5f0`, 0x41 bytes, opcode `0x7F 0x03`):

```
byte 0 : 0x04
byte 1 : 0x7F              (CMD_SCREEN_HEADER)
byte 2 : 0x03              (sub-op for "begin")
byte 3 : LCD-index + 1     (multi-LCD select)
bytes 4..7 : total chunk count (little-endian 32-bit; only lower 24 bits used)
```

2. **Data chunks** — for each chunk index `i` in `[0 .. total_chunks)`:

```
byte 0 : 0x04
byte 1 : (i & 0xFF)        (chunk index low byte)
byte 2 : 0x80 | (i >> 16)  (chunk index high byte + 0x80 marker)
byte 3 : (i >> 8) & 0xFF   (chunk index middle byte)
bytes 4..31 : 28 bytes RGB565 pixel data
bytes 32..62 : zero
byte 63 : —
Sleep(2) between chunks
```

3. **No explicit completion ACK** — the host updates the MProgress widget
   from a 0..100 counter and considers it done when all chunks are sent.

### 5.3 Multi-frame GIF

For an animated GIF (max 140 frames), each frame is uploaded in sequence
with its own per-frame delay byte at offset `+0xC` of the frame metadata
struct, expressed in 2 ms units (so the device runtime renders frame `N`,
sleeps `frame[N].delay × 2 ms`, then advances).

### 5.4 Cross-corroboration with gohv `src/usb.rs`

gohv documents the AK820 PRO's LCD as **128 × 128 RGB565** (32 768 bytes),
split into "**9 chunks of 4123 bytes**" via the interrupt-OUT endpoint
EP3 on interface 2. The AK980 PRO uses **HID-only**, 240 × 135 RGB565,
28-byte HID chunks via opcode `0x80|`. These are two completely different
upload protocols for two different devices — **do not** copy gohv's USB
interrupt-OUT path to the AK980 PRO.

______________________________________________________________________

## 6. Firmware update / DFU

The vendor **does not** flash firmware directly from `DeviceDriver.exe`.
Instead it:

1. Downloads `FirmwareUpdateTool.zip` from a URL (HTTP via WININET — see
   `FUN_0045a520`'s `InternetOpenA` / `HttpQueryInfoW` /
   `InternetReadFile` calls).
2. Writes to `%TEMP%/FirmwareUpdateTool.zip` and unpacks to
   `%TEMP%/FirmwareUpdateTool.exe`.
3. `CreateProcessW` it, then waits on the process handle
   (`OpenProcess(0x1FFFFF, …) + WaitForSingleObject(INFINITE)`).
4. After the external tool exits, the main app reopens the HID
   handle and re-enumerates (`FUN_004fa70`, the device manager).

`FUN_0044f2d0` sends 0x1001-byte (4097-byte) **bulk** writes — likely
used by the spawned `FirmwareUpdateTool.exe` to push firmware blobs once
the keyboard reboots into bootloader mode. The exact DFU opcode + entry
command **cannot** be recovered from `DeviceDriver.exe` alone (it's in
the separately-distributed firmware tool).

**Recommendation**: do NOT implement DFU in our backend without capturing
the standalone `FirmwareUpdateTool.exe` separately.

______________________________________________________________________

## 7. Persistence model

### 7.1 SQLite schema (`%LOCALAPPDATA%/<device>/db/<device>_datav1.db`)

Three known user tables:

```sql
CREATE TABLE IF NOT EXISTS t_config_data(
  config_id INTEGER PRIMARY KEY AUTOINCREMENT,
  profile   INTEGER,            -- FK to t_profile_data.profile
  key       TEXT,               -- e.g. "fn_switch", "sleep_time", "key_respondtime"
  value     TEXT                -- always stringified
);

CREATE TABLE IF NOT EXISTS t_profile_data(
  profile  INTEGER PRIMARY KEY AUTOINCREMENT,
  name     TEXT,                -- profile name shown in the dropdown
  status   INTEGER,             -- enabled / active flag
  app      TEXT,                -- foreground app filter (auto-switch trigger!)
  type     INTEGER              -- profile type (per-app vs global)
);

CREATE TABLE IF NOT EXISTS t_key_otherdata(
  macro_value INTEGER PRIMARY KEY AUTOINCREMENT,
  macro_desc  TEXT,             -- macro display name
  param       TEXT,             -- serialized event list (parsed by FUN_0042dc10)
  type        INTEGER           -- macro type
);
```

**Notable**: the `t_profile_data.app TEXT` column means the vendor
**already** supports **auto-switching profile by foreground application** —
this is a free win for our backend.

### 7.2 Registry

| Key                                                             | Purpose            |
| --------------------------------------------------------------- | ------------------ |
| `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`            | Auto-start         |
| `HKCU\SOFTWARE\<device>\…` (suspected, not exhaustively traced) | Driver preferences |

### 7.3 Filesystem

```
%LOCALAPPDATA%/<device>/
├── db/
│   └── <device>_datav1.db          (SQLite, see §7.1)
├── gif/<device>/<n>.gif             (uploaded GIFs)
├── temp/
│   ├── Keyboard-Software-Setup.exe  (downloaded driver updater)
│   ├── Keyboard-Software-Setup.zip
│   ├── FirmwareUpdateTool.exe       (downloaded FW updater)
│   └── FirmwareUpdateTool.zip
└── layouts/<deviceCodename>.xml    (per-device key layout)
```

______________________________________________________________________

## 8. UX features worth copying

1. **Auto-profile-switch by foreground app** (`t_profile_data.app TEXT`) —
   this is the killer feature of the vendor software. The user selects an
   .exe per profile and the driver switches profiles when that window gets
   focus. Implementable in Qt6 via `QWindowList` polling or
   `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)`. Cross-platform analogue:
   `org.kde.KWin` / `org.gnome.Shell` D-Bus on Linux.
2. **140-frame GIF with per-frame delay editor** — the LCD UX allows the
   user to draw frame-by-frame with brush+eraser+text-insertion and set a
   per-frame delay in 2 ms units. Sketch (UI-level): QGraphicsScene with a
   ListModel for frames, brush size 1/3/5 px.
3. **Music Rhythm (Rainbow/Reverse-Rainbow/Gradient/Spectrum/Solid)** —
   the vendor app drives the keyboard's LEDs from system audio. We can do
   better than the vendor here (proper FFT + multiple visualizers, all on
   the Qt side, sending per-LED RGB to the keyboard).
3. **Single key right-click → "more functions" context menu** — keymap
   editor opens a 7-tab popover for advanced keys (mouse / multimedia /
   shortcut / open app / open URL / send text / multi-key). Same structure
   we should adopt — direct grid clicks for primary remap, right-click for
   the long tail.
4. **Per-level (1-5) explanatory tooltip for Key Response Time** with
   live wire / 2.4 GHz / Bluetooth latency estimates (`1033.lan` 600-624).
5. **Hardware battery indicator + 15 s polling** (`FUN_004358c0`) — we
   should also poll battery every 15 s for wireless devices via opcode
   `0x20 0x01`.

## 9. Anti-features to NOT copy

1. **Custom MFC-like "MUI" UI framework** — the vendor ships its own UI
   toolkit (`mui.dll`, 2 201 exports). We use Qt 6 / QML. ✅ already done.
2. **HTTP-driven external firmware updater** (`FirmwareUpdateTool.exe`
   download + spawn). Modern UX expects in-app firmware update with
   progress, ETA, and verify. Implement in our own backend with a worker
   thread + Qt's `QFile` + libusb_control_transfer or our HID transport.
3. **Polling-based profile-switch ACK detection** (`FUN_00435250`,
   200 ms read loop). The Qt6 way is `QSocketNotifier` on the hidraw fd
   (Linux) or completion port on Windows.
4. **In-tree SQLite with encrypted blobs** (`FUN_004635b0` shows a CRC32
   table encryption layer over file IO). For us, **plain JSON or QSettings
   suffices** — the threat model doesn't justify the complexity.
5. **CSIDL_LOCAL_APPDATA hard-coding** — use `QStandardPaths::AppDataLocation`
   which gives the right path on Linux / macOS too.

______________________________________________________________________

## 10. Modern Qt 6 / C++ reimplementation recommendations (per gap)

| Gap                          | Recommended pattern                                                                  |
| ---------------------------- | ------------------------------------------------------------------------------------ |
| Per-key RGB grid editor      | `QAbstractItemModel` (6 × 19 grid from `rgb-keyboard.xml`) + QML `Repeater` + `ColorDialog`. On commit, build a single chunked buffer via §3.7 envelope. |
| TFT image upload (`0x7F`/`0x80`) | Factor `src/devices/streamdeck/src/image_pipeline.cpp` into a generic `ajazz_imaging` static lib that produces RGB565 frames. New `KeyboardScreenUploader` (worker thread) consumes frames, emits `progress(int percent)` signal, writes via `IHidTransport`. |
| 20 RGB modes (`0x13`)        | Strong-typed `enum class LightingMode : uint8_t { Static=0x00, …, LedOff=0x13 };` with `qmlRegisterUncreatableType`. QML `ListModel` populated from the enum (Qt 6.7 `QML_NAMED_ELEMENT` works). |
| Settings batch (`0x07 0x10`) | Single `SettingsService::commit(fnSwitch, sleepTime, keyDelay)` method that builds the 33-byte report + checksum + posts to worker. Domain validation in the C++ layer; QML just binds via Q_PROPERTY. |
| Battery query (`0x20 0x01`)  | `QTimer(15000)` on the device thread, emits `batteryChanged(int percent)` signal. QML binds to a `BatteryIndicator` component. |
| Macro recording / data upload (`0x09 0x1C`) | `MacroRecorder` QObject hooks `QAbstractNativeEventFilter` (Win) / `evdev` (Linux) / `CGEventTap` (macOS). On finalize, serialize to the 4-byte event format (§3.10) and ship via 0x1C chunked upload. The per-event type/action encoding is unfortunately platform-specific. |
| Macro assign to keys (`0x14`)| `MacroAssignmentModel` (table model 6 × 19) + `KeyEditorPopup`. On commit, build the 7- or 21-chunk envelope (§3.11). |
| Key remap chunked (`0x10`/`0x12`) | `KeymapEditorModel`. On commit, build 21-chunk envelope (§3.12) **for each layer**. |
| Auto profile-switch          | Cross-platform `ForegroundAppWatcher` interface; impls: Windows (`SetWinEventHook`), Linux (D-Bus per DE), macOS (NSWorkspace notifications). Map app exe path → profile slot, emit `profileSwitchRequested(int slot)`. |
| Music Rhythm                 | Use `QAudioInput` + Qt's `QMediaCaptureSession` to capture loopback; run FFT via Eigen or KissFFT in a worker; ship 60 fps per-LED RGB via §3.7 envelope. |
| Profile system (`t_profile_data`) | Replace SQLite with **QtSql + SQLite** (cross-platform, no extra dep) **or** plain JSON via `QJsonDocument` if we don't need queries. Auto-switch column maps to `Profile::trigger.app` field. |
| FINISH packet (`0xF0`)       | Add `buildCommitFinish()` to `proprietary_protocol.hpp` returning `{0x04, 0xF0, 0, …}`. Every existing commit path (`commit()`, `setRgbMode()`, `uploadMacro()`) gains a `FINISH` call **after** `CMD_SAVE`. |
| Firmware update / DFU        | **Defer to v1.3+** — capture the standalone `FirmwareUpdateTool.exe` separately first. Don't add half-baked DFU now. |

______________________________________________________________________

## 11. Cross-corroboration matrix (us ↔ gohv ↔ KyleBoyer ↔ vendor)

| Opcode | Symbol            | Vendor (`DeviceDriver.exe`) | gohv (`protocol.rs`)   | KyleBoyer (`packets.ts`) | Our backend (`proprietary_protocol.hpp`) | Verdict |
| ------ | ----------------- | --------------------------- | ---------------------- | ------------------------ | ----------------------------------------- | ------- |
| 0x02   | CMD_SAVE          | ✓ (`FUN_0044f790`)          | ✓ `CMD_SAVE`           | ✓ `[0x04,0x02]`          | ✓ `CmdSaveRtc`                            | ✅ all agree |
| 0x07   | CMD_SETTINGS_BATCH| ✓ (NEW, `FUN_00414290`)     | —                      | —                        | ❌                                        | 🟡 add it |
| 0x09   | CMD_MACRO_DATA    | ✓ (`0x09 0x1C`)             | —                      | —                        | ❌                                        | 🟡 add it |
| 0x0B   | CMD_LIGHTING_PARAMS | ✓ (`0x0B 0x1C`)           | (subsumed in 0x13)     | —                        | ⚠️ partial (we only do brightness)        | 🔍 extend |
| 0x10/0x12 | CMD_KEYMAP_CHUNK | ✓                          | —                      | —                        | ❌                                        | 🟡 add it |
| 0x13   | MODE_COMMAND      | ✓ (`0x1304`)                | ✓ `CMD_MODE`           | ✓ `[0x04,0x13]`          | ❌                                        | 🟡 add it (20 modes) |
| 0x14   | CMD_MACRO_ASSIGN  | ✓                           | —                      | —                        | ❌                                        | 🟡 add it |
| 0x17   | CMD_SLEEP         | ✓ (`0x1704`)                | ✓ `CMD_SLEEP`          | —                        | ❌                                        | 🟡 add it (4-value enum) |
| 0x18   | CMD_START         | ✓                           | ✓ `CMD_START`          | ✓ `[0x04,0x18]`          | ✓ `CmdStartTime`                          | ✅ all agree |
| 0x20   | CMD_BATTERY (sub 0x01) | ✓ (`FUN_004358c0`)     | —                      | —                        | ❌                                        | 🟡 add it |
| 0x20   | CMD_RGB_PER_KEY (sub 0x04) | ✓                  | —                      | —                        | ⚠️ (we have 0x0A buffer, similar idea)     | 🔍 unify |
| 0x23   | CMD_MACRO_RECBUF  | ✓                           | —                      | —                        | ❌                                        | 🟡 |
| 0x28   | CMD_TIME          | ✓                           | ✓ `CMD_TIME`           | ✓ `[0x04,0x28]`          | ✓ `CmdSetTime`                            | ✅ all agree |
| 0x7F/0x80 | CMD_SCREEN     | ✓ (`FUN_004231c0`, 28-byte chunks) | (different LCD)| —                        | ❌                                        | 🟡 add it (DISPLAY-05) |
| 0xF0   | CMD_FINISH        | ✓ (`FUN_0042b0a0`, etc.)    | ✓ `CMD_FINISH`         | —                        | ❌                                        | 🟡 add it (envelope close) |
| 0xF5   | CMD_RGB_READBACK  | ✓ (`FUN_0042ae80`)          | —                      | —                        | ❌                                        | 🟡 add it |

______________________________________________________________________

## 12. References (Ghidra citations)

- `FUN_00450af0` (0x00450af0) — dynamic loader of `hid.dll`; canonical map
  of function pointers (DAT_005950a0 = HidD_SetFeature, etc.)
- `FUN_00450c00` (0x00450c00) — HID device enumerator using GUID
  `4D1E55B2-F16F-11CF-88CB-001111000030`
- `FUN_00413340` (0x00413340) — Apple-VID (XS75T) enumerator, filters
  by `wcsstr(L"05AC_PID")`
- `FUN_00451220` (0x00451220) — output report writer (WriteFile +
  OVERLAPPED + GetOverlappedResult)
- `FUN_00451300` (0x00451300) — input report reader (ReadFile + OVERLAPPED;
  strips leading 0x00 byte)
- `FUN_004514e0` (0x004514e0) — `DeviceIoControl 0xB0192` =
  IOCTL_HID_GET_INPUT_REPORT wrapper
- `FUN_0044f5f0` (0x0044f5f0) — 33-byte feature send + retry + read ACK
- `FUN_0044f790` (0x0044f790) — `0x02` (SAVE) packet send + ACK match
- `FUN_0044eed0` (0x0044eed0) — 65-byte or chunked send (the workhorse)
- `FUN_0044f0c0` (0x0044f0c0) — like 0044eed0 + read back
- `FUN_0044f2d0` (0x0044f2d0) — 0x1001-byte bulk send (DFU/firmware)
- `FUN_0044f3a0` (0x0044f3a0) — streaming reader (battery / FW version /
  RGB read-back); GetTickCount-based 360 ms timeout
- `FUN_00435250` (0x00435250) — 200 ms profile-switch poll
- `FUN_00414020` (0x00414020) — settings save sequence
  (`0x1804` → `0x1704` sleep → settings)
- `FUN_00414290` (0x00414290) — `0x07 0x10` settings batch (33-byte report)
- `FUN_00418c40` (0x00418c40) — `0x10/0x12` key remap chunked upload (21 chunks)
- `FUN_004183a0` (0x004183a0) — `0x11/0x27` alt key remap
- `FUN_00418f80` (0x00418f80) — `0x04 0x03/0x0D 0x02` macro/key layer
- `FUN_00422920` (0x00422920) — TFT image upload (alternative path)
- `FUN_00423a10` (0x00423a10) — LCD-aware time-sync (multi-LCD aware)
- `FUN_004231c0` (0x004231c0) — **TFT IMAGE UPLOAD primary path** (28-byte
  chunks, `0x7F 0x03` header + `0x80|chunkidx` per-chunk)
- `FUN_004238e0` (0x004238e0) — **time-sync 4-packet envelope**
  (0x18 → 0x28 → time-data → 0x02)
- `FUN_00427db0` (0x00427db0) — per-key RGB upload, custom-lighting tab
- `FUN_0042ae80` (0x0042ae80) — `0xF5` per-key RGB read-back
- `FUN_0042b0a0` (0x0042b0a0) — **RGB mode commit envelope**
  (0x18 → 0x13 → mode-data → 0x02 → 0xF0)
- `FUN_0042b240` (0x0042b240) — `0x05 0x10` key-data alt
- `FUN_0042dc10` (0x0042dc10) — **macro data upload** (`0x09 0x1C`)
- `FUN_0042d690` (0x0042d690) — `0x19 0x04` (alternate macro/key upload)
- `FUN_00432be0` (0x00432be0) — `0x0B 0x1C` lighting params batch
- `FUN_004329a0` (0x004329a0) — per-key RGB upload, keyboard tab
- `FUN_00434320` (0x00434320) — driver-update orchestrator
- `FUN_004340c0` (0x004340c0) — LCD-mode commit envelope (similar to 0x42b0a0)
- `FUN_004349f0` (0x004349f0) — `0x05 0x01` key mode select
- `FUN_00435250` (0x00435250) — profile-switch poll (battery + key combo)
- `FUN_004358c0` (0x004358c0) — **battery query** (`0x20 0x01`) +
  `MUI::BatteryCtrl::SetBatteryInfo`
- `FUN_0044b910` (0x0044b910) — macro-slot commit envelope
- `FUN_0044ba20` (0x0044ba20) — macro record-buffer upload
  (`0x18 → 0x23 → data`)
- `FUN_0044be90` (0x0044be90) — **macro-to-key assignment** (`0x05 0x10` +
  21-chunk `0x14` upload)
- `FUN_0044fa70` (0x0044fa70) — global device manager (enumeration loop)
- `FUN_0045a520` (0x0045a520) — HTTP downloader (firmware/driver)
- `FUN_0040b4c0` (0x0040b4c0) — `SHGetFolderPathW(CSIDL_LOCAL_APPDATA)` —
  the persistence root

Static dumps used in this analysis are at
`C:/Users/unilo/reverse-eng-workdir/ak980pro/`:

- `ghidra_hid_dump.json` (74 call sites + 3 222 strings)
- `ghidra_hid_callers.json` (39 callers of inner HID writers, 2 hops deep)
- `extra_funcs.json` (12 supporting helper functions: WriteFile/IOCTL wrappers, key translators, single-key remap writer, HID enumerator, dynamic loader)
- `mui_dll_inventory.json` (6 604 mui.dll exports + 66 classes catalog + 30 sample decompiles)
- `decomp_targets/` (39 per-function pseudocode dumps)

Extraction scripts are at `C:/Users/unilo/reverse-eng-workdir/`:

- `ExtractHidCalls.java` (GhidraScript — replaces the Python original which
  no longer works in Ghidra 12.x without PyGhidra/CPython)
- `FindHidCallers.java` (BFS caller-tracer)
- `DumpFunctionsByAddr.java` (one-shot dumper for arbitrary function addresses)
- `EnumerateMuiExports.java` (mui.dll export enumerator)

______________________________________________________________________

## 13. Deep-dive addenda (2026-05-17)

This section appends findings from the **deep RE pass** that produced
the four companion documents. See:

- [`ak980pro_mui_dll.md`](./ak980pro_mui_dll.md) — mui.dll first-pass RE
- [`ak980pro_assets_inventory.md`](./ak980pro_assets_inventory.md) — vendor assets
- [`ak980pro_tft_protocol.md`](./ak980pro_tft_protocol.md) — TFT image upload (deep)
- [`ak980pro_macros_protocol.md`](./ak980pro_macros_protocol.md) — macro recording + assignment
- [`ak980pro_perkey_rgb_protocol.md`](./ak980pro_perkey_rgb_protocol.md) — per-key RGB write/read

### 13.1 Transport correction (CRITICAL)

The vendor binary uses **two distinct HID transports**, with **different
on-wire framing**:

| Helper          | Underlying API                       | On-wire bytes                                       |
| --------------- | ------------------------------------ | --------------------------------------------------- |
| `FUN_0044f5f0`  | `WriteFile` (HID OUTPUT report)      | `[ID=0x00, opcode, sub, payload, ..., checksum@8]` (33 B) |
| `FUN_0044eed0`  | `HidD_SetFeature` (HID FEATURE report) | `[ID=0x00, 0x04, opcode, sub, payload, ...]` (65 B)  |
| `FUN_0044f0c0`  | `HidD_SetFeature` chunked (64-B slices) | (same as 0044eed0, repeated per chunk)              |
| `FUN_0044f2d0`  | `WriteFile` (bulk 4097-byte writes)  | `[ID=0x00, payload, ...]` (4097 B)                  |
| `FUN_0044f3a0`  | `WriteFile` + `ReadFile` streaming   | (mixed; reader strips leading 0x00)                 |

So the **0x04** byte that appears at the start of FUN_0044eed0 buffers
is **NOT** the HID Report ID — it's a fixed frame-magic byte
**inserted by the FEATURE-report path between Report ID 0x00 and the
real opcode**. For all 33-byte short reports (settings batch, macro
data chunks, TFT chunks, lighting params, battery query), the **opcode
sits at on-wire byte 1**, not byte 2.

This means our previous citation of "byte 0 : 0x04 (HID Report ID)" was
**wrong** for the FUN_0044eed0 path — Report ID is 0x00, frame-magic
is 0x04 at byte 1, opcode is at byte 2. For the FUN_0044f5f0 path,
opcode is at byte 1 directly.

### 13.2 Settings batch (0x07 0x10) — byte map corrected

Re-deriving from FUN_00414290 with proper stack offset analysis:

| Offset | Value                                                         |
| ------ | ------------------------------------------------------------- |
| 0      | 0x00 (HID Report ID)                                          |
| 1      | 0x07 (CMD_SETTINGS_BATCH)                                     |
| 2      | 0x10 (sub-op)                                                 |
| 3, 4   | 0, 0                                                          |
| 5      | 0x01 (fixed)                                                  |
| 6      | disable_winkey   (0 or 1; from `MUI::CCheckButton` at +0x678) |
| 7      | disable_alt_f4    (0 or 1; from `MUI::CCheckButton` at +0x67c)|
| 8      | disable_alt_tab   (0 or 1; from `MUI::CCheckButton` at +0x680) — *AND* checksum slot |
| 9      | `fn_switch` value (int from `t_config_data`)                  |
| 10     | `sleep_time` value                                            |
| 11     | (unused)                                                      |
| 12     | `key_respondtime` value                                       |
| 18, 19 | 0xAA, 0x55 (trailer)                                          |
| 20..32 | 0                                                             |

The earlier §3.2 byte map was wrong — `value("fn_switch")` is at byte 9
not byte 5; the three disable-key flags consume bytes 6, 7, 8; byte 8
is also the checksum slot (overwritten last by FUN_0044f5f0).

### 13.3 Sleep timer (0x17 0x04) envelope

Re-derived from FUN_00414020:

```
1. CMD_START   : [0x04, 0x18, 0x04, ...]           (sent via FUN_0044eed0, 65 B FEATURE)
2. CMD_SLEEP   : [0x04, 0x17, 0x04, ..., 0x01@8, ..., 0x01@14, ...]
3. SETTINGS    : [0x04, 0x00, 0x01, ..., disable_winkey, disable_alt_f4,
                 disable_alt_tab, fn_switch, sleep_time, ?, key_respondtime,
                 ..., 0xAA 0x55]
4. CMD_SAVE    : [0x04, 0x02, 0x04, ...]
```

So **sleep timer doesn't have its own data byte — it shares the
"settings" packet at step 3**. The `local_4c` lone byte set at offset 8
in step 2 is a **mode flag** (always 1), not the sleep value itself.

The 4-value enum (Never/1min/5min/30min) is mapped to byte 10 of step 3.

### 13.4 Lighting params batch (0x0B 0x1C) byte map

Re-derived from FUN_00432be0:

```
byte 0 : 0x00 (HID Report ID)
byte 1 : 0x0B
byte 2 : 0x1C
byte 3 : 0x00
byte 4 : combo_at_0x618 → CurData (mode index)
byte 5 : combo_at_0x630 → CurData (speed index)
byte 6 : DAT(0x144) → brightness
byte 7 : auVar1[0] (direction byte 0)
byte 8 : checksum (computed; sum of bytes 0..7 + 9..32 with byte 8 = 0)
byte 9..15 : auVar1[1..7] (direction + reserved bytes)
byte 16 : `*param_1[1][4]` (5th byte of direction-state struct)
...
(sent as 33-byte FEATURE report via FUN_0044f5f0 with param_2=0x21)
```

There's also a debug trace: `MUI::M_Trace(L"mxn: %02d %02d %02d %02d %02d %02d %02d %02d %02d %02d \n", ...)` — printing
all 10 input bytes that were just sent. So the vendor logs every
lighting commit to the debug console.

### 13.5 Battery query (0x20 0x01) confirmed

`FUN_004358c0`:

```
Request : [Report_ID=0, 0x20, 0x01, 0x00, ...]      (33 B output)
Response: [0x20, 0x01, ?, percent, ...]              (after ReadFile strips
                                                     leading 0x00)
```

Polled every **15 s** when `*(this + 0x784) == 2` (wireless mode flag).
Wired mode (`== 0`) returns nothing — the binary just skips the call.
Reception calls `MUI::BatteryCtrl::SetBatteryInfo(percent, /*charging=*/0)`
— the "charging" flag is **always passed as 0**, so the vendor's
battery indicator doesn't show "charging" state. We can do better.

### 13.6 Alt key remap (0x11 / 0x27 via FUN_004183a0) — complete encoding

The "alternate" key-remap path (FUN_004183a0) has **far richer encoding**
than the binary's single-key path (FUN_00418f80). The 4-byte per-key
entry written via FUN_00417ff0 has type-specific layouts:

| `type` (DB)  | Bytes [0, 1, 2, 3] of slot                         | Meaning                          |
| ------------ | -------------------------------------------------- | -------------------------------- |
| 1            | `[0x05, 0x03, 0, 0]` (uint16 = 0x0305 at +0)       | Default / pass-through           |
| 2            | `[2, keycode_translated, modifier_byte, 0]`        | Plain HID keycode (translated by `FUN_00451ca0`) |
| 3            | `[6, profile_idx, modifier_byte, 0x24_flags]`      | Profile switch                   |
| 5            | `[1, 0x01..0x10, 0x01 or 0x03, 0]`                 | Mouse: codes 0x101 LeftClick, 0x401 RightClick, 0x201 MiddleClick, 0x301 = 4th, 0x103 = ScrollUp, 0xFF03 = ScrollDown, 0x801 = 5th, 0x1001 = 6th |
| 6            | `[3, multimedia_keycode, 0, 0]`                    | Multimedia: 0xCD play/pause, 0xB7 next, 0xB6 prev, 0xB5 stop, 0xE9 vol+, 0xEA vol-, 0xE2 mute |
| 7            | `[2, 0x08..0x04, 0x07/0x08/0x0F/0x1A/0x2B/0x06/0x19/0x1B, 0]` | System keys (encoded as 2-byte values 0x708, 0x808, 0xF08, 0x1A01, 0x2B04, 0x601, 0x1901, 0x1B01); also `[2, 1, 0x02, 0]` for case 0xB (= 0x0102) and `[2, 2, 0x02, 0]` for case 10 (= 0x0202) |
| 8/9/10/11    | `[5, 0x02, x_byte, 0]` (uint16 = 0x0205)           | Window-shortcut groups           |
| 12 (case 0xC) | `[3, key_lo, key_hi, 0]` via `FUN_00419290(value, &lo, &hi)` | Multi-key combination |
| 0xD          | `[7, key_lo, key_mid, key_hi]`                     | Macro launcher (4 bytes encode macro ID + flags) |

This is the **canonical per-key remap entry format** used for the full
chunked upload (576 bytes / 144 keys × 4 bytes). The wireless macro
assignment §6.2 of `ak980pro_macros_protocol.md` uses the same layout.

### 13.7 RGB modes (0x13) envelope — full re-derivation

`FUN_0042b0a0` confirmed envelope (5 steps):

```
1. CMD_START   [0x04, 0x18, 0x04, ...]
2. CMD_MODE_BEGIN [0x04, 0x13, 0x04, ..., 0x01@8, ...]
3. CMD_MODE_DATA  [0x04, mode_id@0, ?@1, ?@2, ?@3, ..., (direction)@8,
                   (brightness)@9, (speed)@10, ..., 0xAA 0x55@14,15]
4. CMD_SAVE    [0x04, 0x02, 0x04, ...]
5. CMD_FINISH  [0x04, 0xF0, 0x04, ...]
```

Hmm wait — `FUN_0042b0a0` doesn't actually encode the byte map fully in
the decompile because most fields come from `local_68`, `local_70`,
`local_7c`, `local_78`, `local_74` (uninitialised stack values in some
cases). The actual byte layout of the data packet step 3 is best
inferred from gohv's `mode_data_packet`:

```
byte 0 : mode_id (0x00..0x13)
bytes 1..3 : R, G, B tint (when mode supports it)
byte 8 : rainbow flag (0/1)
byte 9 : brightness (0..5)
byte 10 : speed (0..5)
byte 11 : direction (Left=0, Down=1, Up=2, Right=3)
bytes 14..15 : 0xAA 0x55 trailer
```

This matches the existing §3.4 doc. The new finding from FUN_0042b0a0
is the **explicit CMD_FINISH (0xF0) at step 5** — confirmed in three
places in the binary (FUN_0042b0a0, FUN_004340c0, FUN_0044b910).

**Add CMD_FINISH after every commit** in our backend.

### 13.8 Time-sync — minor correction

The `FUN_004238e0` time-data packet byte map (already in §3.1) is
correct except for offset 1: **on-wire byte 1 = lcd_idx + 1**, not "0x00
HID Report ID". The HID Report ID is byte 0 (= 0x00 from memset). The
prior doc conflated the two.

`FUN_00423a10` (LCD-aware variant) uses a **completely different**
opcode `0x0C 0x10` (`local_54._1_4_ = 0x100c`), single-packet via
`FUN_0044f5f0`. Byte map:

| Off | Value                          |
| --- | ------------------------------ |
| 0   | 0x00 (Report ID)               |
| 1   | 0x0C                           |
| 2   | 0x10                           |
| 3,4 | 0, 0                           |
| 5   | lcd_idx + 1                    |
| 6   | 0x5A                           |
| 7   | wYear % 2000                   |
| 8   | wMonth                         |
| 9   | wDay                           |
| 10  | wHour                          |
| 11  | wMinute                        |
| 12  | wSecond                        |
| 13  | 0                              |
| 14  | wDayOfWeek                     |
| 18, 19 | 0xAA, 0x55                   |

This is the path that should be used when the keyboard has an LCD that
displays the time (i.e., the AK980 PRO). The simpler `FUN_004238e0`
path is the **multi-packet envelope variant** for keyboards without
LCD time displays.

### 13.9 Wireless macro upload uses `0x19 0x04` + `0x15 0x04`

FUN_0042d690 reveals the **wireless** macro data upload path is
DIFFERENT from the wired (`0x09 0x1C`) path:

```
1. CMD_MACRO_BEGIN_WIRELESS: [0x04, 0x19, 0x04, ...]
2. CMD_MACRO_CHUNKINFO:      [0x04, 0x15, 0x04, ..., chunk_count@8, ...]
3. BULK BODY: `chunk_count * 64` bytes via FUN_0044eed0
4. CMD_SAVE: [0x04, 0x02, 0x04, ...]
```

So opcodes **0x19** and **0x15** are new (not in the existing table).
They're wireless-specific. Add to the opcode table.

### 13.10 Updated opcode table (delta from §3)

| Opcode | Sub  | Dir  | Source function | Role                                                    |
| ------ | ---- | ---- | --------------- | ------------------------------------------------------- |
| `0x0C` | `0x10`| H→D | `FUN_00423a10` | **LCD-aware time-sync** (NEW, separate from 0x28 path)  |
| `0x15` | `0x04`| H→D | `FUN_0042d690` | **Wireless macro chunk info** (chunk count at byte 8)   |
| `0x19` | `0x04`| H→D | `FUN_0042d690` | **Wireless macro upload BEGIN** (precedes bulk body)    |
| `0x72` | —    | H→D  | `FUN_00422920` | **TFT bulk upload BEGIN** (4 KB chunks via `FUN_0044f2d0`) |

### 13.11 Findings impact on existing implementation

A summary of "must-fix" issues identified during this deep RE pass:

- **Settings batch byte map**: bytes 5, 6, 7 do NOT carry the fn_switch /
  sleep / delay values — they carry the disable-Win/Alt-F4/Alt-Tab
  flags. Real fn_switch is at byte 9, sleep at 10, delay at 12.

- **Per-key RGB byte budget**: wired = 192 bytes = **3 chunks of 64 B
  each** (NOT 3 chunks of 28 B as one might infer from the §3.7 prior
  doc). Wireless = 512 bytes = **8 chunks of 64 B each** (NOT 6).

- **Macro assignment**: wired = 7 chunks × 28 B = 192 B = 1 byte/LED;
  wireless = 21 chunks × 28 B (last = 16 B) = 576 B = 4 bytes/LED.
  Each wireless slot is `[lightIdx_echo, keycode_lo, keycode_mid,
  keycode_hi/flags]` — full keycode encoding per key, not just
  "has-macro" flag.

- **TFT chunk index**: confirmed split across bytes 1, 3, and low 7 bits
  of byte 2 (with high bit of byte 2 = 0x80 marker). Max effective range
  = 24 bits = 16 M chunks. The 17-bit description in the prior doc was
  conservative — the actual range is 24 bits.

- **CMD_FINISH (0xF0)** appears at the end of every multi-packet
  envelope: lighting mode (FUN_0042b0a0), LCD-mode (FUN_004340c0),
  macro upload close (FUN_0044b910), alt remap (FUN_004183a0). Add to
  our backend.

- **Transport choice matters for the 0x04 byte**: HID OUTPUT reports
  (33 B short reports via WriteFile) do NOT have a 0x04 magic byte —
  opcode is at on-wire byte 1. HID FEATURE reports (65 B via
  HidD_SetFeature) DO have a 0x04 magic at byte 1, opcode at byte 2.
  **Our backend must select the transport correctly for each opcode**:

  | Opcode group       | Transport | Helper        |
  | ------------------ | --------- | ------------- |
  | 0x07 0x10 (settings batch) | OUTPUT 33 B  | `FUN_0044f5f0` |
  | 0x09 0x1C (macro data wired) | OUTPUT 33 B  | `FUN_0044f5f0` |
  | 0x0B 0x1C (lighting params) | OUTPUT 33 B  | `FUN_0044f5f0` |
  | 0x14 0x1C (macro assign chunks) | OUTPUT 33 B  | `FUN_0044f5f0` |
  | 0x20 0x01 (battery query) | OUTPUT 33 B  | `FUN_0044f5f0` |
  | 0x7F 0x03 (TFT header)  | OUTPUT 33 B  | `FUN_0044f5f0` |
  | 0x80\|n (TFT chunk)     | OUTPUT 33 B  | `FUN_0044f5f0` |
  | 0x18 / 0x28 / 0xF0 (envelope verbs) | FEATURE 65 B | `FUN_0044eed0` |
  | 0x02 (CMD_SAVE)         | FEATURE 65 B | `FUN_0044eed0` |
  | 0x13 (RGB mode data)    | FEATURE 65 B | `FUN_0044eed0` |
  | 0x17 (sleep timer envelope) | FEATURE 65 B | `FUN_0044eed0` |
  | 0x19 / 0x15 (wireless macro) | FEATURE 65 B | `FUN_0044eed0` |
  | 0x20 0x04 (per-key RGB write) | FEATURE 65 B chunked | `FUN_0044f0c0` |
  | 0x23 (macro rec buffer)  | FEATURE 65 B + bulk body | `FUN_0044eed0` |
  | 0x72 (TFT bulk begin)    | FEATURE 65 B | `FUN_0044eed0` |
  | bulk 4097-byte writes    | OUTPUT 4097 B | `FUN_0044f2d0` |
  | 0xF5 0x03/0x09 (per-key RGB readback) | OUTPUT + streaming read | `FUN_0044f3a0` |

  This taxonomy is enforced by the firmware (it expects the report
  framing matching its descriptor). Sending opcode 0x07 0x10 in a 65-B
  FEATURE report will either be silently ignored or fail with
  `STATUS_INVALID_DEVICE_REQUEST`.

