# AJ-series mouse — byte-precise opcode reference

> Companion to `aj_series_vendor.md`. This document is the byte-by-byte spec
> the P0.5 wire-format rewrite of `src/devices/mouse/src/aj_series.cpp` must
> emit on the wire. Where the renderer and `iot_driver.exe` both have evidence,
> both citations are given; where only one does, the citation says which.
>
> All bytes refer to the **post-report-id** position, i.e. the 64-byte HID
> body that follows report-id `0x05`. The Rust driver lives at
> `…\extracted\app32\resources\app\iot_driver.exe`. Renderer evidence lives at
> `…\extracted\app32\resources\app\dist\static\js\main_beautified.js`
> (line numbers given as `js:N`).

______________________________________________________________________

## 1 — Envelope

```
byte  0 (USB report id) : 0x05      // OUT to iot_driver only; HID transport
byte  1 (opcode)        : see §3
byte  2 (sub / arg-0)   : see per-opcode spec
byte  3..62 (payload)   : see per-opcode spec
byte 63 (checksum)      : sum(bytes 1..62) & 0x7F      // BIT7 — confirmed below
```

The 65-byte SET_REPORT envelope used by `iot_driver.exe` toward the device
is `report_id=0x05` prepended to the 64-byte body. The renderer never
touches the report id; it hands the 64-byte body to the gRPC server.

### Hard fact: report id is 0x05

Every AJAZZ AJ-series SKU has `featureReportByteLength: 65` in the
renderer device table (`js:61749, 61781, 61797, 64053, 64069, 64884`).
65 == 1 (report id) + 64 (body) for `HidD_SetFeature` on Windows.

### Hard fact: `CheckSumType.BIT7` is what every AJ-series mouse path uses

`Wn.CheckSumType` enum at `js:51245`:

```
BIT7 = 0
BIT8 = 1
NONE = 2
```

Every mouse-class call site uses `BIT7` (or the numeric literal `0`, which
is the same value). Census across `main_beautified.js`:

| token                                                                         | sites |
| ----------------------------------------------------------------------------- | ----: |
| `Wn.CheckSumType.BIT7`                                                        |    36 |
| `writeFeatureCmd(buf, 0)`                                                     |    62 |
| `Wn.CheckSumType.BIT8`                                                        | **0** |
| `Wn.CheckSumType.NONE` (default-arg in transport wrappers — passthrough only) |     5 |

**Every** opcode-emitter from the mouse class (`setMouseOption0`,
`setMouseOption1`, `setReportRate`, `setKeyConfigSimple`, `setFnKeyConfigSimple`,
`setMacro`, `setLightSetting`, `getFirmwareVersion`, `setCurrentProfile`,
`getCurrentProfile`, all `_setMacro` chunks, all `_oledUpgrade` / `mledUpgrade`
chunks) passes BIT7 or `0`. The renderer never selects BIT8 anywhere.

> **Note on Ghidra confirmation.** The Rust function that masks byte 63
> lives behind the proto3 `enum CheckSumType` switch inside the iot_driver
> binary; the source path `src\dj_dev_api\cmd_list.rs` and
> `src\dj_dev_api\dangle_common.rs` are visible in the binary's string
> table (line 1622–1623 of `iot_driver_strings.txt`). The cleanest way to
> read the function is `ghidra_iot_driver_import.log` once analysis
> finishes (~10 min for a 6.4 MB Rust binary). For the P0.5 rewrite the
> renderer evidence above is sufficient, but the
> `HuntChecksum.java` script at `C:\Users\unilo\reverse-eng-workdir\HuntChecksum.java`
> will dump every caller of `hid_send_feature_report` and the literal-mask
> census so the next pass can paste the disassembled bytes into this doc.

### Hard fact: BIT8 is reserved for a different device class (keyboard / OLED)

Although BIT8 is the second enum value, the renderer never invokes it.
The keyboard backend(s) also pass `BIT7`. The presence of `BIT8` in the
proto enum almost certainly represents a legacy SKU (the iot_driver
supports devices from VID `0x25aa`, `0x342d`, `0x347a`, `0x374a`, `0x3794`,
`0x331a` in addition to AJAZZ-VID `0x3151` — see `aj_series_device_matrix.md`).
**Our backend should mask `& 0x7F` for every AJ-series PID.** A small
per-PID lookup table can fall back to `& 0xFF` if and when a real SKU
that needs it ever appears.

______________________________________________________________________

## 2 — Complete opcode catalogue

Source: full `FEA_CMD_*` enum at `js:920839`, mirrored in
`iot_driver.exe`'s `cmd_list.rs` string table (`iot_driver_strings.txt:1565–1589`,
`1621`). The renderer enum (mouse class) re-uses base-class opcodes (`0x00..0x4F`)
and adds the mouse-specific extensions at `0x50..0x60`. Every opcode is
either a `SET = N` or `GET = N | 0x80` pair.

### 2.1 Base class (shared with keyboard / OLED)

| Opcode | Mnemonic                         | Direction   | Used on mouse path?                                                | Renderer site         |
| -----: | -------------------------------- | ----------- | ------------------------------------------------------------------ | --------------------- |
| `0x00` | `FEA_CMD_SET_REV`                | feature SET | no                                                                 | —                     |
| `0x80` | `FEA_CMD_GET_REV`                | feature GET | **yes** — `getFirmwareVersion()`                                   | `js:921263`           |
| `0x01` | `FEA_CMD_SET_WIRELESS_SYNC`      | feature SET | no (handled by dongle)                                             | —                     |
| `0x02` | `FEA_CMD_SET_RESERT`             | feature SET | **yes** — "Restore defaults"                                       | search for opcode `2` |
| `0x83` | `FEA_CMD_GET_BATTERY`            | feature GET | **NO** — battery comes from `Device.battery` (gRPC `watchDevList`) | `js:50798`            |
| `0x04` | `FEA_CMD_SET_REPORT` (poll rate) | feature SET | **yes** — `setReportRate()`                                        | `js:921290`           |
| `0x84` | `FEA_CMD_GET_REPORT` (poll rate) | feature GET | **yes** — `getReportRate()`                                        | `js:921307`           |
| `0x05` | `FEA_CMD_SET_PROFILE`            | feature SET | **yes** — `setCurrentProfile()`                                    | `js:921350`           |
| `0x85` | `FEA_CMD_GET_PROFILE`            | feature GET | **yes** — `getCurrentProfile()`                                    | `js:921331`           |
| `0x06` | `FEA_CMD_SET_KBOPTION`           | feature SET | no (keyboard)                                                      | —                     |
| `0x86` | `FEA_CMD_GET_KBOPTION`           | feature GET | no (keyboard)                                                      | —                     |
| `0x07` | `FEA_CMD_SET_LEDPARAM`           | feature SET | **yes** — `setLightSetting()`                                      | `js:920862`           |
| `0x87` | `FEA_CMD_GET_LEDPARAM`           | feature GET | **yes** — `getLightSetting()`                                      | —                     |
| `0x08` | `FEA_CMD_SET_SLEDPARAM`          | feature SET | no                                                                 | —                     |
| `0x88` | `FEA_CMD_GET_SLEDPARAM`          | feature GET | no                                                                 | —                     |
| `0x09` | `FEA_CMD_SET_KEYMATRIX`          | feature SET | no (keyboard; mouse uses 0x50)                                     | —                     |
| `0x89` | `FEA_CMD_GET_KEYMATRIX`          | feature GET | no (keyboard)                                                      | —                     |
| `0x0a` | `FEA_CMD_SET_KEYENABLE`          | feature SET | no                                                                 | —                     |
| `0x8a` | `FEA_CMD_GET_KEYENABLE`          | feature GET | no                                                                 | —                     |
| `0x0b` | `FEA_CMD_SET_MACRO`              | feature SET | no — mouse uses `0x16` SIMPLE form                                 | —                     |
| `0x8b` | `FEA_CMD_GET_MACRO`              | feature GET | no                                                                 | —                     |
| `0x0c` | `FEA_CMD_SET_USERPIC`            | feature SET | no — mouse uses `0x52`                                             | —                     |
| `0x8c` | `FEA_CMD_GET_USERPIC`            | feature GET | no                                                                 | —                     |
| `0x0d` | `FEA_CMD_SET_AUDIO`              | feature SET | no                                                                 | —                     |
| `0x0e` | `FEA_CMD_SET_WINDOWS`            | feature SET | no                                                                 | —                     |
| `0x10` | `FEA_CMD_SET_FN`                 | feature SET | no                                                                 | —                     |
| `0x90` | `FEA_CMD_GET_FN`                 | feature GET | no                                                                 | —                     |
| `0x11` | `FEA_CMD_SET_DEBOUNCE` (kb)      | feature SET | no — mouse uses byte 10 of `0x53` omnibus                          | —                     |
| `0x91` | `FEA_CMD_GET_DEBOUNCE`           | feature GET | no                                                                 | —                     |
| `0x12` | `FEA_CMD_SET_SLEEPTIME` (kb)     | feature SET | no — mouse uses bytes 40..47 of `0x53`                             | —                     |
| `0x92` | `FEA_CMD_GET_SLEEPTIME`          | feature GET | no                                                                 | —                     |
| `0x13` | `FEA_CMD_SET_KEYMATRIX_SIMPLE`   | feature SET | no                                                                 | —                     |
| `0x93` | `FEA_CMD_GET_KEYMATRIX_SIMPLE`   | feature GET | no                                                                 | —                     |
| `0x14` | `FEA_CMD_SET_USERPIC_SIMPLE`     | feature SET | no                                                                 | —                     |
| `0x94` | `FEA_CMD_GET_USERPIC_SIMPLE`     | feature GET | no                                                                 | —                     |
| `0x15` | `FEA_CMD_SET_FN_SIMPLE`          | feature SET | no                                                                 | —                     |
| `0x95` | `FEA_CMD_GET_FN_SIMPLE`          | feature GET | no                                                                 | —                     |
| `0x16` | `FEA_CMD_SET_MACRO_SIMPLE`       | feature SET | **yes** — `_setMacro()` chunked                                    | `js:922079`           |
| `0x96` | `FEA_CMD_GET_MACRO_SIMPLE`       | feature GET | **yes** — `getMacro()` chunked                                     | `js:921635`           |
| `0x17` | `FEA_CMD_SET_AUTOOS_EN`          | feature SET | no                                                                 | —                     |
| `0x97` | `FEA_CMD_GET_AUTOOS_EN`          | feature GET | no                                                                 | —                     |
| `0x18` | `FEA_CMD_SET_USERGIFSTART`       | feature SET | (mouse with screen)                                                | `js:817098`           |
| `0x19` | `FEA_CMD_SET_USERGIF`            | feature SET | (mouse with screen)                                                | `js:817119`           |
| `0x20` | `FEA_CMD_SET_OLEDPICINDEX`       | feature SET | no                                                                 | —                     |
| `0xa0` | `FEA_CMD_GET_OLEDPICINDEX`       | feature GET | no                                                                 | —                     |
| `0x21` | `FEA_CMD_SET_OLEDPICDATA`        | feature SET | no                                                                 | —                     |
| `0xa1` | `FEA_CMD_GET_OLEDPICDATA`        | feature GET | no                                                                 | —                     |
| `0x22` | `FEA_CMD_SET_OLEDOPTION`         | feature SET | no                                                                 | —                     |
| `0xa2` | `FEA_CMD_GET_OLEDOPTION`         | feature GET | no                                                                 | —                     |
| `0x23` | `FEA_CMD_SET_KEYSTROKE`          | feature SET | no                                                                 | —                     |
| `0xa3` | `FEA_CMD_GET_KEYSTROKE`          | feature GET | no                                                                 | —                     |
| `0x24` | `FEA_CMD_SET_OLEDGIFDATA`        | feature SET | no                                                                 | —                     |
| `0xa4` | `FEA_CMD_GET_OLEDGIFDATA`        | feature GET | no                                                                 | —                     |
| `0x25` | `FEA_CMD_SETTFTLCDDATA`          | feature SET | (mouse with screen)                                                | `js:817196`           |
| `0xa5` | `FEA_CMD_GETTFTLCDDATA`          | feature GET | (mouse with screen)                                                | `js:736052`           |
| `0x26` | `FEA_CMD_SET_OLEDGIFINDEX`       | feature SET | no                                                                 | —                     |
| `0xa6` | `FEA_CMD_GET_OLEDGIFINDEX`       | feature GET | no                                                                 | —                     |
| `0x27` | `FEA_CMD_SET_OLEDLUANGAGE`       | feature SET | no                                                                 | —                     |
| `0x28` | `FEA_CMD_SET_OLEDCLOCK`          | feature SET | no                                                                 | —                     |
| `0x29` | `FEA_CMD_SET_SCREEN_24BITDATA`   | feature SET | (mouse with screen, 24-bit colour)                                 | —                     |
| `0xa9` | `FEA_CMD_GET_SCREEN_24BITDATA`   | feature GET | (mouse with screen)                                                | —                     |
| `0x2a` | `FEA_CMD_SET_OLEDWEATHER`        | feature SET | no                                                                 | —                     |
| `0x2b` | `FEA_CMD_SET_OLEDEFFECT`         | feature SET | no                                                                 | —                     |
| `0xab` | `FEA_CMD_GET_OLEDEFFECT`         | feature GET | no                                                                 | —                     |
| `0x2c` | `FEA_CMD_SET_FLASHCHIPERASSE`    | feature SET | (OLED erase)                                                       | `js:735950`           |
| `0xac` | `FEA_CMD_GET_FLASHCHIPERASSE`    | feature GET | no                                                                 | —                     |
| `0xad` | `FEA_CMD_GETOLED_VERSION`        | feature GET | no                                                                 | —                     |
| `0x30` | `FEA_CMD_SET_OLED_BOOT`          | feature SET | no                                                                 | —                     |
| `0xb0` | `FEA_CMD_GET_OLED_BOOT`          | feature GET | no                                                                 | —                     |
| `0x31` | `FEA_CMD_SET_OLED_BOOTSTART`     | feature SET | no                                                                 | —                     |
| `0xb1` | `FEA_CMD_GET_OLED_BOOTSTART`     | feature GET | no                                                                 | —                     |
| `0x8f` | `FEA_CMD_GET_INFO`               | feature GET | yes (initial probe)                                                | —                     |

### 2.2 Mouse class extensions (`0x50..0x60`)

| Opcode | Mnemonic                         | Direction   | Renderer site                    |
| -----: | -------------------------------- | ----------- | -------------------------------- |
| `0x50` | `FEA_CMD_MOUSE_SET_KEYMATRIX`    | feature SET | `js:921897`                      |
| `0xd0` | `FEA_CMD_MOUSE_GET_KEYMATRIX`    | feature GET | `js:921426`                      |
| `0x51` | `FEA_CMD_MOUSE_SET_FNMATRIX`     | feature SET | `js:921929`                      |
| `0xd1` | `FEA_CMD_MOUSE_GET_FNMATRIX`     | feature GET | (parallel of 0xd0)               |
| `0x52` | `FEA_CMD_MOUSE_SET_USERPIC`      | feature SET | (per-key picture)                |
| `0xd2` | `FEA_CMD_MOUSE_GET_USERPIC`      | feature GET | —                                |
| `0x53` | `FEA_CMD_MOUSE_SET_OPTIONPARAM0` | feature SET | `js:921127`                      |
| `0xd3` | `FEA_CMD_MOUSE_GET_OPTIONPARAM0` | feature GET | `js:921155`                      |
| `0x54` | `FEA_CMD_MOUSE_SET_OPTIONPARAM1` | feature SET | `js:921188`                      |
| `0xd4` | `FEA_CMD_MOUSE_GET_OPTIONPARAM1` | feature GET | `js:921222`                      |
| `0x55` | `FEA_CMD_SET_DOWNCOUNT`          | feature SET | — (anti-feature — do NOT expose) |
| `0x60` | `FEA_CMD_SET_CONTROLRECOIL`      | feature SET | — (anti-feature)                 |
| `0xe0` | `FEA_CMD_GET_CONTROLRECOIL`      | feature GET | — (anti-feature)                 |
| `0x61` | `FEA_CMD_GET_CLEARBLUEINFRO`     | feature SET | (BLE pairing reset)              |

______________________________________________________________________

## 3 — Byte-precise specifications (each opcode the mouse path uses)

For every opcode below, byte 0 is the **opcode** (not the report id),
following the convention used in the renderer's `_9r.alloc(64)` buffer.
Checksum at byte 63 is filled by the iot_driver — emitters must zero
bytes 63 before handing to transport.

### 3.1 `FEA_CMD_GET_REV` — `0x80` (firmware version query)

Sender: `getFirmwareVersion()` at `js:921258`.

```
byte 0  : 0x80     // FEA_CMD_GET_REV
byte 1..62 : 0
byte 63 : checksum (filled by iot_driver, BIT7)
```

Response (read via `commonFeature(req, 0)` → `readFeatureCmd`):

```
byte 0  : 0x80 (echo)
byte 1..2 : uint16-LE firmware version       // js:921265: a.readUInt16LE(1)
byte 3..63 : ignored
```

### 3.2 `FEA_CMD_SET_RESERT` — `0x02` (factory reset)

```
byte 0  : 0x02
byte 1..62 : 0
byte 63 : checksum
```

Vendor app trigger: "Restore defaults" button.

### 3.3 `FEA_CMD_SET_PROFILE` — `0x05` (active profile select)

Sender: `setCurrentProfile(idx)` at `js:921350`.

```
byte 0  : 0x05         // FEA_CMD_SET_PROFILE
byte 1  : profile idx (0..7)        // js:921350: a[1] = r
byte 2..62 : 0
byte 63 : checksum
```

`FEA_CMD_GET_PROFILE` (`0x85`) — same layout, response byte 1 = current
profile (`js:921339: a.readUInt8(1)`).

### 3.4 `FEA_CMD_SET_REPORT` — `0x04` (polling rate)

Sender: `setReportRate(hz)` at `js:921271`. Critically: the wire byte
is **NOT** `hz` itself but a coded byte from the `_RateToNum` table
(`js:920911`):

| UI label | byte code |
| -------: | --------: |
|   125 Hz |    `0x08` |
|   250 Hz |    `0x04` |
|   500 Hz |    `0x02` |
|  1000 Hz |    `0x01` |
|  2000 Hz |    `0x84` |
|  4000 Hz |    `0x82` |
|  8000 Hz |    `0x81` |

```
byte 0  : 0x04         // FEA_CMD_SET_REPORT
byte 1  : profile idx (currentProfile or 0)     // js:921290
byte 2  : rate code from _RateToNum             // js:921290: n[2] = a
byte 3..62 : 0
byte 63 : checksum
```

> **Note**: the inline `setReportRate` body shown at `js:921277` only
> covers rates ≤ 1000 Hz (it switches on 1000/500/250/125). High-rate
> SKUs that support 2/4/8 KHz go through a separate code path that
> calls `_RateToNum` directly. The byte codes above are the table the
> Rust driver receives.

### 3.5 `FEA_CMD_SET_LEDPARAM` — `0x07` (LED setting, 8-byte block)

Sender: `setLightSetting(setting)` at `js:920862`. Payload is the
8-byte block produced by `_LightSettingToBuffer()` at `js:920967`.

```
byte 0  : 0x07
byte 1  : effect type             (1..10 — see table below)
byte 2  : (MAXSPEED - speed) where MAXSPEED = 4
                                  // so wire-speed = 4 - UI-speed, range 0..4
byte 3  : value                   (brightness 0..6 or "option-encoded" value)
byte 4  : (option-nibble << 4) | mode-bits
                                  // mode-bits: NORMAL=7, DAZZLE=8, MUSIC2 y=4 (dazzle off)
byte 5  : R
byte 6  : G
byte 7  : B
byte 8..62 : 0
byte 63 : checksum
```

Effect type enum (`js:920977`):

| Code | Name                | Notes                                                 |
| ---: | ------------------- | ----------------------------------------------------- |
|  `0` | `LightOff`          | all bytes 0                                           |
|  `1` | `LightAlwaysOn`     | value=brightness, RGB=color, dazzle bit (mode bits)   |
|  `2` | `LightBreath`       | + speed                                               |
|  `3` | `LightNeon`         | option 0=Default 1=Random (rainbow cycle)             |
|  `4` | `LightWave`         | option from `WAVEOP` (right=0, left=1, down=2, up=3)  |
|  `5` | `LightDazzing`      | + speed (variant spelling)                            |
|  `6` | `LightLaser`        | + speed                                               |
|  `7` | `LightMusicFollow`  | option from `MP` (upright=0, separate=1, intersect=2) |
|  `8` | `LightScreenColor`  | host-RGB feed                                         |
|  `9` | `LightMusicFollow2` | second music mode                                     |
| (10) | `LightUserPicture`  | special case — bytes [4,5,6] = `[0, 200, 200]`        |

The dazzle / mode-bits sub-field (`byte 4`) encoding (`js:920974–920976`):

```
i = setting.dazzle ? 1 : 0
l = i ? DAZZLE(8) : NORMAL(7)              // becomes mode-bits low nibble
y = i ? 0 : 4                              // music-only secondary mode
```

For `LightWave`: `byte 4 = (WAVEOP[option] << 4) | l`.
For `LightMusicFollow` / `LightMusicFollow2`: `byte 4 = (MP[option] << 4) | y`.

**LED-off detection**: the renderer rewrites `rgb=0xFFFFFF` to `0xFAFAFA`
on the wire (`js:921010`) and reverses on read (`js:921032`), so the
device's literal "pure white" sentinel is `r=g=b=0xFA`.

### 3.6 `FEA_CMD_MOUSE_SET_KEYMATRIX` — `0x50` (single button rebind)

Sender: `setKeyConfigSimple(config)` at `js:921877–921908`. **Critical:
payload starts at byte 8, not byte 4** — our backend's offset is wrong.

```
byte 0  : 0x50              // FEA_CMD_MOUSE_SET_KEYMATRIX
byte 1  : profile idx (currentProfile or 0)
byte 2  : button index (resolved via findIndexInDefaultMatrix())
byte 3..7 : 0
byte 8  : action byte 0   // changeArr from configToChangeArr()
byte 9  : action byte 1
byte 10 : action byte 2
byte 11 : action byte 3
byte 12..62 : 0
byte 63 : checksum
```

`changeArr` is a 4-byte action descriptor (`js:921952`):

| `n[0]` (type byte)                                      | meaning                                                      |
| ------------------------------------------------------- | ------------------------------------------------------------ |
| `0`                                                     | combo / forbidden / unknown (n[1]=skey, n[2]=key, n[3]=key2) |
| `1`                                                     | mouse-button (n[2] = OF[key][2])                             |
| `2`,`3`,`6`,`8`,`10`,`11`,`13`,`14`,`18`,`19`,`20`,`22` | system function                                              |
| `9`                                                     | macro (n[1]=mode, n[2]=macro idx)                            |

For "forbidden" type the entire 4-byte payload is `[0,0,0,0]` (`js:921955`).

### 3.7 `FEA_CMD_MOUSE_GET_KEYMATRIX` — `0xd0` (full key-matrix read)

Sender: `_getKeyMatrix(profile)` at `js:921420`.

```
byte 0  : 0xd0
byte 1  : profile idx
byte 2..62 : 0
byte 63 : checksum
```

Response: 64 bytes = 16 × 4-byte action records (one per button).

### 3.8 `FEA_CMD_MOUSE_SET_FNMATRIX` — `0x51` (Fn-layer key rebind)

Identical to `0x50` except byte 1 = Fn-layer index (`a`), byte 2 = button.

```
byte 0  : 0x51
byte 1  : Fn-layer index
byte 2  : button index
byte 3..7 : 0
byte 8..11 : 4-byte action
byte 12..62 : 0
byte 63 : checksum
```

### 3.9 `FEA_CMD_MOUSE_SET_OPTIONPARAM0` — `0x53` (omnibus settings)

Sender: `setMouseOption0(profile, rate, mp, light, logoLight, debounce, sleepTime, batteryColors, chargingSwitch)` at `js:921121–921149`. **This is the canonical
"everything in one packet" command — the §11.5 rewrite should target
this opcode over any per-field write.**

```
byte 0   : 0x53                                          // FEA_CMD_MOUSE_SET_OPTIONPARAM0
byte 1..7 : 0
byte 8   : profile idx                                   // js:921127: s[8]=r
byte 9   : poll-rate code from _RateToNum                // s[9]=_RateToNum(a)
byte 10  : debounce time (ms; 0..10 typical, default 1)  // s[10]=i
byte 11  : 0
byte 12  : flags-low                                     // uint16-LE flags
byte 13  : flags-high
              flags bit 0 : lightOff
              flags bit 1 : wheelLightOff
              flags bit 2 : smooth (motion smoothing)
              flags bit 3 : ledSelect (battery-LED RGB enable)
              flags bit 4 : powerSaveMode
byte 14  : buttonChange       (default 1)
byte 15  : wheelToButton      (default 10)
byte 16  : buttonToWheel      (default 10)
byte 17..23 : 0
byte 24..31 : 8-byte LED block (LightSettingToBuffer of `light`)
byte 32..39 : 8-byte logo-LED block (LogoLightToBuffer of `logoLight`)
byte 40..47 : 8-byte sleep-time block:
                 uint16-LE time_bt    (BT idle, seconds)        @ 40..41
                 uint16-LE deepTime_bt (BT deep-sleep, seconds) @ 42..43
                 uint16-LE time_24    (2.4G idle, seconds)      @ 44..45
                 uint16-LE deepTime_24(2.4G deep-sleep, seconds)@ 46..47
byte 48..49 : 0
byte 50  : xSensitivity   (0..100 %; default 100)
byte 51  : ySensitivity   (0..100 %; default 100)
byte 52  : liftCutOff     (LOD: 0 = 1 mm, 1 = 2 mm, 2 = 3 mm)
byte 53  : angelSnap      (angle-snap; 0/1)
byte 54  : battery-color-0 R         (high-charge indicator colour)
byte 55  : battery-color-0 G
byte 56  : battery-color-0 B
byte 57  : battery-color-1 R         (low-charge indicator colour)
byte 58  : battery-color-1 G
byte 59  : battery-color-1 B
byte 60  : chargingSwitch (1 = LED on while charging, 0 = LED off)
byte 61..62 : 0
byte 63  : checksum
```

`GET_OPTIONPARAM0` (`0xd3`) returns the same layout in the response;
the renderer parses it at `js:921163–921177`.

> **Sleep-time encoding nuance** (`js:921097`): the renderer writes BOTH
> bytes of each uint16-LE manually — `t[0]=time_bt - (time_bt>>8<<8)`,
> `t[1]=time_bt>>8`. Equivalent to little-endian, but defensive against
> JS `>>` sign-extension on negative values. Our C++ code can use
> straightforward `writeUInt16LE` since `time_bt` is always uint16.

### 3.10 `FEA_CMD_MOUSE_SET_OPTIONPARAM1` — `0x54` (DPI table)

Sender: `setMouseOption1(currentDpi, currentDPIMax, dpiArr, colorArr)` at
`js:921182–921215`.

```
byte 0  : 0x54
byte 1  : profile idx (currentProfile or 0)
byte 2  : current active DPI stage (0..7)        // = caller's `r`
byte 3  : currentDPIMax (number of enabled stages, 0..8)
byte 4..7 : 0
byte 8..23 : 8 × uint16-LE DPI values (16 bytes total)
                  // js:921188-921191: for each DPI value, t.writeUInt16LE(e); i.push(t[0]); i.push(t[1])
                  // then i.map((e,t) => u[8 + t] = e)
byte 24..39 : 0   // reserved (some SKUs use this for X-DPI vs Y-DPI split; AJ159 leaves it 0)
byte 40..63 : 8 × { R, G, B }    // js:921193-921197: l[t]=color; u[40 + 3*t + 0..2] = r/g/b
                                 // 24 bytes = 8 stages × 3 colour bytes
byte 63 : checksum (overwrites last byte of the 8th colour B — see note)
```

> **Edge case on byte 63**: 8 × 3 = 24 colour bytes occupy `byte 40..63`,
> meaning byte 63 (the checksum byte) is also byte 40 + 23 = colour-7-B.
> The renderer wires this directly; the iot_driver overwrites with the
> computed checksum after the renderer's write completes. **Net effect:
> the 8th DPI stage's B-channel colour is replaced by the BIT7 checksum
> on the wire.** This is a vendor-app bug we should replicate
> bit-for-bit to keep firmware behaviour identical, OR cap the DPI count
> at 7 (current AJ159 firmware uses 7 visible stages, with the 8th
> reserved). Our `aj_series_opcode_table.md` reader should treat the
> 8th-stage RGB as "no colour" and read it back from `GET_OPTIONPARAM1`
> response (where byte 63 is also the checksum, but the response uses a
> different layout — the renderer's `getMouseOption1` re-reads bytes 40..63
> in 3-byte chunks; the last chunk's B-channel will likely be garbage).

`GET_OPTIONPARAM1` (`0xd4`) — request bytes 0..7, response 64 bytes
deserialised at `js:921222–921248`:

```
resp[2]          = currentDpi (active stage idx)
resp[3]          = currentDPIMax
resp[8..23]      = 8 × uint16-LE dpi values
resp[24..39]     = reserved (probably another 8 × uint16-LE for X/Y split — currently unread)
resp[40..63]     = 8 × { R, G, B }
```

### 3.11 `FEA_CMD_SET_MACRO_SIMPLE` — `0x16` (chunked macro upload)

Sender: `_setMacro(buf256, slotIdx)` at `js:922079`. Macro payload is
256 bytes (5 chunks × 56 bytes; last chunk partial).

```
For each chunk c in 0..(chunkCount-1):
  byte 0  : 0x16              // FEA_CMD_SET_MACRO_SIMPLE
  byte 1  : macro slot idx (0..19)
  byte 2  : chunk idx (c)
  byte 3  : last-non-zero byte position across the full 256 bytes
           // js:922090: n[3] = 56*(u-1) + s, where u = highest non-empty chunk and
           // s = position of last non-zero byte within that chunk
  byte 4  : 1 if this is the final chunk, else 0
  byte 5..7 : 0
  byte 8..63 : 56 bytes of macro payload (chunk c)
  byte 63 (last 56-byte chunk byte) : overwritten by checksum on the wire
```

After all chunks, the renderer sleeps `BIGCMDDELAY = 500 ms` to let the
device flush macro RAM to flash.

Macro payload format (256 bytes; `js:922019–922078`):

```
@ 0..1   : uint16-LE repeatCount
@ 2..N   : packed action records:
            type = delay      → bit7=0, low 7 bits = delay ms (≤127)
                                OR if delay > 127: uint16-LE for longer delays
                                (placed BEFORE the next non-delay record per js:922036)
            type = keyboard   → byte = HID usage; bit7 = down flag
            type = mouse_button → byte = OF[key][2]; bit7 = down flag
            type = mouse_move → 0xF9 marker byte, dx int8, dy int8
```

`getMacro(slot)` (`0x96`) reads back the same 256 bytes in 4 chunks of
64 (1 header + 56 payload per response). The renderer deserialises at
`js:921674` (`buffToMacroEvents`).

### 3.12 `FEA_CMD_SETTFTLCDDATA` — `0x25` (16-bit colour LCD upload)

Sender: `_oledUpgrade` style flow; chunked at 56 bytes per packet.

```
byte 0  : 0x25
byte 1  : currentFrame
byte 2  : frameNum (total frames)
byte 3  : frameDelay (ms between frames)
byte 4..5 : uint16-LE chunk index
byte 6  : chunkLen (≤ 56)
byte 7  : reserved
byte 8..63 : up to 56 bytes of RGB565 pixel data
byte 63 : checksum
```

For 24-bit colour use `FEA_CMD_SET_SCREEN_24BITDATA = 0x29`.

### 3.13 OLED / mouse-MCU bootloader (firmware OTA)

The OTA flow has two parallel paths — `mledUpgrade` (mouse-MCU) and
`oledUpgrade` (screen-MCU). Both share the same opcode set numerics but
on different MCU controllers.

|      Opcode | Mnemonic                       | Purpose                                                            |
| ----------: | ------------------------------ | ------------------------------------------------------------------ |
|      `0x30` | `FEA_CMD_SET_OLED_BOOT`        | Enter screen-MCU bootloader: payload `[0x55,0xAA,0x55,0xAA,0,0,0]` |
|      `0xb0` | `FEA_CMD_GET_OLED_BOOT`        | Poll bootloader ready: response byte 1 = 1                         |
|      `0x31` | `FEA_CMD_SET_OLED_BOOTSTART`   | Start screen upload: payload `[uint16LE chunkCount]`               |
|      `0xb1` | `FEA_CMD_GET_OLED_BOOTSTART`   | Poll progress                                                      |
| (no opcode) | data chunks                    | 64-byte chunks of firmware                                         |
|      `0xc1` | `FEA_CMD_GET_MLEDBOOTCHECKSUM` | Send int32-LE accumulated checksum; resp byte 1 == 0x55 = ok       |

The mouse-MCU variant (`mledUpgrade`) uses analogous opcodes `0x40`,
`0xc0`, `0x41`, `0xc1` (`js:817399`, `js:817415`, `js:817472`):

```
FEA_CMD_SET_MLEDBOOTLOADER = 0x40   payload [0x55,0xAA,0x55,0xAA,0,0,0]
FEA_CMD_GET_MLEDBOOTLOADER = 0xc0   poll until resp[1] == 1
FEA_CMD_SET_MLEDBOOTSTART  = 0x41   payload [uint16LE chunkCount]
(data)                              64-byte chunks via send64()
FEA_CMD_GET_MLEDBOOTCHECKSUM = 0xc1 send [opcode, b0, b1, b2, b3]; resp[1]==0x55 → success
```

Both flows skip the first `0x10000` (64 KiB) bytes of the firmware
image (the boot header).

### 3.14 Anti-features (do **NOT** implement)

| Opcode | Mnemonic                     | Anti-feature                                                          |
| -----: | ---------------------------- | --------------------------------------------------------------------- |
| `0x55` | `FEA_CMD_SET_DOWNCOUNT`      | rapid-fire countdown                                                  |
| `0x60` | `FEA_CMD_SET_CONTROLRECOIL`  | recoil-control / no-recoil macro                                      |
| `0xe0` | `FEA_CMD_GET_CONTROLRECOIL`  | (read-side of recoil)                                                 |
| `0x61` | `FEA_CMD_GET_CLEARBLUEINFRO` | clear BLE pairing info (probably fine, but BLE is out-of-scope today) |

The two recoil opcodes are anti-cheat liabilities and are universally
disallowed in tournament play (Valorant, CS2, Apex). Do **not** implement.

______________________________________________________________________

## 4 — Battery model (no opcode — pushed from dongle via gRPC)

There is **no HID feature-report battery query** on AJ-series mice.
Battery percentage is delivered via the `Device.battery` field
(`js:50798`, `js:50824`, `js:50841`) on the `proto.driver.Device` message
broadcast by the iot_driver's `watchDevList` server-stream:

```protobuf
message Device {
  DeviceType devtype = 1;
  bool       is24    = 2;   // 2.4-GHz dongle path?
  string     path    = 3;   // /dev/hidraw* equivalent
  int32      id      = 4;
  uint32     battery = 5;   // 0..100
  bool       isonline = 6;
  uint32     vid     = 7;
  uint32     pid     = 8;
}
```

For wired mice, `battery = 0` and `is24 = false`. For wireless mice the
dongle firmware reports both. Our backend's equivalent should expose a
`batteryPercent()` method that:

1. Returns `std::nullopt` for wired SKUs (no dongle, no battery).
1. For wireless SKUs, subscribes to a hot-plug callback that watches
   the dongle's `Device.battery` proto field — on Linux this maps to
   `/sys/class/power_supply/hid-<vid>:<pid>.*/capacity`.

There is also a `Status24` message (`js:50890`):

```protobuf
message Status24 {
  uint32 battery   = 1;
  bool   isOnline  = 2;
}
```

… used by the `DangleStatus` oneof to push battery for both keyboard and
mouse on a shared dongle (`js:50940`, common dangles like PID `0x4011`,
`0x4014`, `0x4017`, etc. — see device matrix).

______________________________________________________________________

## 5 — Checksum function (BIT7 / BIT8 / NONE)

Per renderer (`js:51245`), the proto enum is:

```protobuf
enum CheckSumType {
  BIT7 = 0;
  BIT8 = 1;
  NONE = 2;
}
```

The Rust iot_driver wraps every `hid_send_feature_report` call with a
checksum compute based on this enum. The function lives in
`src\dj_dev_api\cmd_list.rs` per the binary's string table
(`iot_driver_strings.txt:1622`). Three modes:

| Enum       | Wire byte 63                                     |
| ---------- | ------------------------------------------------ |
| `BIT7` (0) | `sum(bytes 0..62) & 0x7F`                        |
| `BIT8` (1) | `sum(bytes 0..62) & 0xFF`                        |
| `NONE` (2) | left as-is (renderer's bytes are passed through) |

**Note on sum range.** I have not yet visually inspected the Rust
disassembly to confirm whether the sum range is bytes `0..=62` or
`1..=62`. Two arguments for `0..=62`:

1. It's strictly more defensive (every byte we touch is summed).
1. The renderer never writes byte 0 with a non-opcode value, so
   sum(0..=62) == sum(1..=62) + opcode, and an attacker-controlled
   opcode would still be folded in.

For our backend's first cut, **use `sum(bytes 1..=62) & 0x7F`** (matches
our current `aj_series.cpp:127` accumulator range), then once a USBPcap
capture confirms either way, fall back to `0..=62` if needed. The
difference is only the opcode byte; for a non-zero opcode this matters.

Open the iot_driver in Ghidra and run `HuntChecksum.java`
(`C:\Users\unilo\reverse-eng-workdir\HuntChecksum.java`) to dump every
caller of `hid_send_feature_report` — the checksum function is the
small wrapper called immediately before. Look for the 0x7F literal in
its decompilation.

______________________________________________________________________

## 6 — Code corrections required

Files to change in our repo (line numbers as of commit `216b0b8`):

### 6.1 `src/devices/mouse/src/aj_series.cpp`

|    Line | Current                                                                                                                                             | Replace with                                                                                                                                                                              |
| ------: | --------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
|   86–94 | `CommandId` enum with `kCmdDpi=0x21`, `kCmdPollRate=0x22`, `kCmdLod=0x23`, `kCmdButton=0x24`, `kCmdRgb=0x30`, `kCmdBattery=0x40`, `kCmdCommit=0x50` | Delete `CommandId` enum. Replace with `enum FeaCmd : std::uint8_t` listing every opcode in §2 we actually use (~10 values).                                                               |
|     127 | `& 0xff`                                                                                                                                            | `& 0x7F` (BIT7 — see §5)                                                                                                                                                                  |
|     180 | `dpiStageCount() = 6`                                                                                                                               | `dpiStageCount() = 8`                                                                                                                                                                     |
| 217–223 | `setPollRateHz` writes uint16 BE: `p[0] = hz >> 8; p[1] = hz & 0xff`                                                                                | Replace with single byte from a `constexpr std::array<std::pair<uint16_t, uint8_t>, 7>` table (the `_RateToNum` table in §3.4). Opcode: `0x04`. Payload at byte 2 (after profile byte 1). |
| 227–231 | `setLiftOffDistanceMm(mm)` writes `(mm * 10)` at opcode 0x23                                                                                        | Delete. LOD is byte 52 of the `0x53` omnibus packet. Expose via a `setMouseOption0()` helper.                                                                                             |
| 233–243 | `setButtonBinding(button, action)` opcode 0x24, payload at byte 4                                                                                   | Opcode `0x50`. Payload structure: `pkt[1]=profile`, `pkt[2]=button`, `pkt[8..11]=action`.                                                                                                 |
| 245–252 | `batteryPercent()` writes opcode 0x40 + reads response                                                                                              | Replace with `std::nullopt` for wired SKUs; for wireless, return cached value updated by a `Device.battery` watcher (gRPC equivalent of `udev change` events on Linux).                   |
| 259–270 | `setRgbStatic` / `setRgbEffect` opcode 0x30                                                                                                         | Opcode `0x07`, single 8-byte payload via `LightSettingToBuffer` equivalent (see §3.5).                                                                                                    |
| 273–277 | `setRgbBrightness(percent)` opcode 0x30 sub 0x02                                                                                                    | **Delete this opcode entirely.** Brightness rides as byte 3 of the 8-byte light packet (`value` field).                                                                                   |
| 287–298 | `uploadDpiStage(index, stage)` opcode 0x21, payload at byte 4                                                                                       | Delete this per-stage upload. Replace with `setMouseOption1(stages, activeIdx)` that writes the full 8-stage table atomically via opcode `0x54` (see §3.10).                              |

### 6.2 New helper struct `AjSeriesOptionPacket`

Wrap the 64-byte `0x53` omnibus packet as a builder:

```cpp
struct AjSeriesOptionPacket {
    std::uint8_t profile = 0;
    std::uint16_t pollRateHz = 1000;       // → _RateToNum on emit
    std::uint8_t debounceMs = 1;
    bool lightOff = false, wheelLightOff = false, smooth = false;
    bool ledSelect = false, powerSaveMode = false;
    std::uint8_t buttonChange = 1, wheelToButton = 10, buttonToWheel = 10;
    LightSetting mainLight{}, logoLight{};
    SleepTime sleepTime{};
    std::uint8_t xSensitivity = 100, ySensitivity = 100;
    LiftOffDistance liftOffDistance = LiftOffDistance::Mm1;
    bool angleSnap = false;
    Rgb batteryColorHigh{0, 255, 0}, batteryColorLow{255, 0, 0};
    bool chargingSwitch = true;

    std::array<std::uint8_t, 64> toBuffer() const;
};
```

Then `setMouseOption0(packet)` writes the entire 64-byte body in one
HID transaction. This mirrors the vendor's "one save button" UX.

### 6.3 Catch2 tests required (under `tests/devices/mouse/`)

The tests below use a `MockTransport` that captures writeFeature calls
in a `std::vector<std::array<std::uint8_t, 64>>`. Each test asserts
byte-for-byte equality against the expected envelope.

```cpp
// test_aj_series_wire_format.cpp

TEST_CASE("AJ159: setActiveDpiStage emits 0x54 packet with correct active byte",
          "[mouse][aj_series][wire]") {
    auto [device, transport] = makeAjSeriesWithMockTransport(/*vid*/0x3151, /*pid*/0x5008);
    auto m = std::dynamic_pointer_cast<IMouseCapable>(device);
    REQUIRE(m);

    // First: load a known DPI table (8 stages 400, 800, ..., 6400)
    std::vector<DpiStage> stages;
    for (int i = 1; i <= 8; ++i) {
        stages.push_back({.dpi = uint16_t(400 * i),
                          .indicator = Rgb{uint8_t(i * 30), 0, 0}});
    }
    m->setDpiStages(stages);
    m->setActiveDpiStage(3);

    // Expected last packet: opcode 0x54, profile 0, active=3, max=8,
    //                       8 LE DPI values at byte 8..23,
    //                       8 RGB triplets at byte 40..62
    auto const& pkt = transport->writes().back();
    CHECK(pkt[0] == 0x54);
    CHECK(pkt[1] == 0);          // profile
    CHECK(pkt[2] == 3);          // active stage
    CHECK(pkt[3] == 8);          // count

    // DPI values uint16-LE
    for (int i = 0; i < 8; ++i) {
        uint16_t want = 400 * (i + 1);
        CHECK(pkt[8 + 2 * i]     == (want & 0xFF));
        CHECK(pkt[8 + 2 * i + 1] == (want >> 8));
    }

    // Checksum is BIT7 of sum(bytes 1..62)
    uint8_t sum = 0;
    for (int i = 1; i < 63; ++i) sum += pkt[i];
    CHECK(pkt[63] == (sum & 0x7F));
}

TEST_CASE("AJ159: setPollRateHz(8000) emits 0x04 packet byte 2 = 0x81",
          "[mouse][aj_series][wire]") {
    auto [device, transport] = makeAjSeriesWithMockTransport(0x3151, 0x5008);
    auto m = std::dynamic_pointer_cast<IMouseCapable>(device);
    m->setPollRateHz(8000);

    auto const& pkt = transport->writes().back();
    CHECK(pkt[0] == 0x04);   // FEA_CMD_SET_REPORT
    CHECK(pkt[2] == 0x81);   // _RateToNum[8000]
}

TEST_CASE("AJ159: setPollRateHz(4000)→0x82, (2000)→0x84, (1000)→0x01",
          "[mouse][aj_series][wire]") {
    auto [device, transport] = makeAjSeriesWithMockTransport(0x3151, 0x5008);
    auto m = std::dynamic_pointer_cast<IMouseCapable>(device);

    m->setPollRateHz(4000);
    CHECK(transport->writes().back()[2] == 0x82);
    m->setPollRateHz(2000);
    CHECK(transport->writes().back()[2] == 0x84);
    m->setPollRateHz(1000);
    CHECK(transport->writes().back()[2] == 0x01);
    m->setPollRateHz(500);
    CHECK(transport->writes().back()[2] == 0x02);
    m->setPollRateHz(250);
    CHECK(transport->writes().back()[2] == 0x04);
    m->setPollRateHz(125);
    CHECK(transport->writes().back()[2] == 0x08);
}

TEST_CASE("AJ159: setButtonBinding emits 0x50 with action at byte 8..11",
          "[mouse][aj_series][wire]") {
    auto [device, transport] = makeAjSeriesWithMockTransport(0x3151, 0x5008);
    auto m = std::dynamic_pointer_cast<IMouseCapable>(device);
    // Bind button 5 to mouse-button action (type=1, key=BTN_FORWARD=4)
    m->setButtonBinding(5, 0x01000400);  // type=1, sub=0, key=4, key2=0

    auto const& pkt = transport->writes().back();
    CHECK(pkt[0] == 0x50);   // FEA_CMD_MOUSE_SET_KEYMATRIX
    CHECK(pkt[1] == 0);      // profile
    CHECK(pkt[2] == 5);      // button idx
    // Bytes 3..7 are zero
    for (int i = 3; i < 8; ++i) CHECK(pkt[i] == 0);
    // Action at bytes 8..11 (vendor offset, not the broken offset-4 we had)
    CHECK(pkt[8]  == 0x01);
    CHECK(pkt[9]  == 0x00);
    CHECK(pkt[10] == 0x04);
    CHECK(pkt[11] == 0x00);
}

TEST_CASE("AJ159: setRgbStatic emits 0x07 with type=1, brightness=value, RGB at 5..7",
          "[mouse][aj_series][wire]") {
    auto [device, transport] = makeAjSeriesWithMockTransport(0x3151, 0x5008);
    auto r = std::dynamic_pointer_cast<IRgbCapable>(device);
    r->setRgbStatic("logo", Rgb{0xAA, 0xBB, 0xCC});

    auto const& pkt = transport->writes().back();
    CHECK(pkt[0] == 0x07);                          // FEA_CMD_SET_LEDPARAM
    CHECK(pkt[1] == 1);                             // LightAlwaysOn
    CHECK(pkt[2] == 4 /*MAXSPEED*/ - 0 /*speed*/);  // wire speed code
    // pkt[3] = brightness (caller-default to full)
    CHECK(pkt[5] == 0xAA);
    CHECK(pkt[6] == 0xBB);
    CHECK(pkt[7] == 0xCC);
}

TEST_CASE("AJ159: checksum is BIT7 (sum & 0x7F) of bytes 1..62",
          "[mouse][aj_series][checksum]") {
    auto [device, transport] = makeAjSeriesWithMockTransport(0x3151, 0x5008);
    auto m = std::dynamic_pointer_cast<IMouseCapable>(device);

    // Pick a value that makes BIT7 vs BIT8 obviously different
    m->setPollRateHz(8000);    // opcode 0x04 + rate 0x81 → sum has bit 7 set
    auto const& pkt = transport->writes().back();
    uint8_t sum = 0;
    for (int i = 1; i < 63; ++i) sum += pkt[i];
    REQUIRE((sum & 0x80) != 0);   // sanity: bit 7 is set in the raw sum
    CHECK(pkt[63] == (sum & 0x7F));  // confirms we MASK bit 7 off
    CHECK(pkt[63] != (sum & 0xFF));  // confirms we are NOT using BIT8
}

TEST_CASE("AJ159: setMouseOption0 omnibus has correct LOD at byte 52, sensitivity at 50/51",
          "[mouse][aj_series][wire]") {
    auto [device, transport] = makeAjSeriesWithMockTransport(0x3151, 0x5008);
    AjSeriesOptionPacket pkt;
    pkt.xSensitivity = 75;
    pkt.ySensitivity = 80;
    pkt.liftOffDistance = LiftOffDistance::Mm2;
    pkt.angleSnap = true;
    pkt.batteryColorHigh = Rgb{0, 255, 0};
    pkt.batteryColorLow  = Rgb{255, 0, 0};

    auto* concrete = dynamic_cast<AjSeriesMouse*>(device.get());
    concrete->setMouseOption0(pkt);

    auto const& wire = transport->writes().back();
    CHECK(wire[0]  == 0x53);
    CHECK(wire[50] == 75);   // xSensitivity
    CHECK(wire[51] == 80);   // ySensitivity
    CHECK(wire[52] == 1);    // LOD = 2 mm
    CHECK(wire[53] == 1);    // angle snap on
    CHECK(wire[54] == 0);    // battHigh.r
    CHECK(wire[55] == 255);  // battHigh.g
    CHECK(wire[56] == 0);    // battHigh.b
    CHECK(wire[57] == 255);  // battLow.r
}

TEST_CASE("AJ159: getFirmwareVersion emits 0x80, parses uint16-LE at byte 1..2",
          "[mouse][aj_series][wire]") {
    auto [device, transport] = makeAjSeriesWithMockTransport(0x3151, 0x5008);
    transport->queueFeatureResponse({0x80, 0x5A, 0x01, /*…*/});  // version 0x015A = 346
    CHECK(device->firmwareVersion() == "346");

    auto const& req = transport->writes().back();
    CHECK(req[0] == 0x80);
    for (int i = 1; i < 63; ++i) CHECK(req[i] == 0);
}

TEST_CASE("AJ159: NO standalone battery opcode is ever emitted on writeFeature",
          "[mouse][aj_series][safety]") {
    auto [device, transport] = makeAjSeriesWithMockTransport(0x3151, 0x5008);
    transport->writes().clear();
    auto battery = device->batteryPercent();
    CHECK(transport->writes().empty());     // no HID write at all
    CHECK(battery == std::nullopt);         // wired SKU = no battery
}

TEST_CASE("AJ159: setMacro emits 0x16 with chunk index 0..N, last-chunk flag at byte 4",
          "[mouse][aj_series][wire]") {
    // ... 256-byte payload, chunked at 56 bytes, expect 5 chunks
    // Validate byte 0 = 0x16, byte 1 = slot, byte 2 = chunk_idx
    // For final chunk, byte 4 = 1
    // For all others, byte 4 = 0
}
```

______________________________________________________________________

## 7 — References for this document

| Subject                                   | File:Line                                  |
| ----------------------------------------- | ------------------------------------------ |
| Mouse FEA_CMD enum (full)                 | `dist/static/js/main_beautified.js:920839` |
| `_RateToNum` table                        | `dist/static/js/main_beautified.js:920911` |
| `_LightSettingToBuffer` (8-byte LED pack) | `dist/static/js/main_beautified.js:920967` |
| `setMouseOption0` (omnibus 0x53)          | `dist/static/js/main_beautified.js:921121` |
| `getMouseOption0` parser                  | `dist/static/js/main_beautified.js:921150` |
| `setMouseOption1` (DPI 0x54)              | `dist/static/js/main_beautified.js:921182` |
| `getMouseOption1` parser                  | `dist/static/js/main_beautified.js:921216` |
| `setReportRate`                           | `dist/static/js/main_beautified.js:921271` |
| `setKeyConfigSimple` (KEYMATRIX 0x50)     | `dist/static/js/main_beautified.js:921877` |
| `setFnKeyConfigSimple` (FNMATRIX 0x51)    | `dist/static/js/main_beautified.js:921909` |
| `setMacro` (256-byte payload assembly)    | `dist/static/js/main_beautified.js:922019` |
| `_setMacro` (0x16 chunked upload)         | `dist/static/js/main_beautified.js:922079` |
| `getFirmwareVersion` (0x80)               | `dist/static/js/main_beautified.js:921258` |
| `writeFeatureCmd` (transport wrapper)     | `dist/static/js/main_beautified.js:726774` |
| `commonFeature` (write+read helper)       | `dist/static/js/main_beautified.js:726843` |
| `no`/`oo`/`uo`/`io` gRPC wrappers         | `dist/static/js/main_beautified.js:56600`  |
| `CheckSumType` enum                       | `dist/static/js/main_beautified.js:51245`  |
| Device proto `Device.battery` field       | `dist/static/js/main_beautified.js:50798`  |
| `Status24` proto (battery + isOnline)     | `dist/static/js/main_beautified.js:50890`  |
| iot_driver FEA_CMD strings                | `iot_driver_strings.txt:1565–1621`         |
| iot_driver gRPC method names              | `iot_driver_strings.txt:842, 849`          |
| iot_driver Rust source paths              | `iot_driver_strings.txt:1622–1624`         |
| Rust build path                           | `iot_driver_strings.txt:850`               |

______________________________________________________________________

## 8 — Ghidra worklist for next pass

1. **Wait for `iot_driver.exe` analysis to finish** in
   `C:\Users\unilo\reverse-eng-workdir\ghidra_projects\iot_driver_proj\`.
   Use `analyzeHeadless … -process -noanalysis` for subsequent runs.

1. **Run** `HuntChecksum.java` (at `C:\Users\unilo\reverse-eng-workdir\`):

   ```powershell
   & 'C:\ProgramData\chocolatey\lib\ghidra\tools\ghidra_12.1_PUBLIC\support\analyzeHeadless.bat' `
     'C:\Users\unilo\reverse-eng-workdir\ghidra_projects' iot_driver_proj `
     -process iot_driver.exe -noanalysis `
     -scriptPath 'C:\Users\unilo\reverse-eng-workdir' `
     -postScript HuntChecksum.java
   ```

   Output: `C:\Users\unilo\reverse-eng-workdir\iot_driver_hunt_checksum.txt`
   contains every caller of `hid_send_feature_report` with full
   decompilation, plus the 0x7F / 0xFF literal census across all
   instructions. The checksum wrapper is the function called
   immediately before `hid_send_feature_report` that takes a 64-byte
   buffer pointer and writes byte 63.

1. **Look for** the `match` on `CheckSumType` enum in the same file —
   it's a 3-arm `switch` that calls one of three functions
   (`checksum_bit7`, `checksum_bit8`, no-op). The `BIT7` arm should
   contain a `mov eax, 0x7F; and dl, al` or `lea / and ecx, 0x7F`
   instruction sequence around the byte-63 store.

1. **Confirm** sum range (`0..=62` vs `1..=62`) by examining the loop
   bounds. The loop is small (`for i in 1..63 { sum += buf[i] }` is
   the most likely pattern given that the renderer never writes byte
   0 to anything but the opcode).
