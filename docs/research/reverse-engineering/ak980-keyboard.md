# AJAZZ Proprietary Keyboard Family — Reverse-Engineering Dossier

> **Scope.** The AJAZZ *proprietary* (non-VIA) keyboard wire protocol, anchored
> on the **AK980 PRO** (VID:PID `0x0c45:0x8009`, Sonix SN32F299 MCU) with the
> **AK680 / AK510** legacy lineage as the secondary corpus. Clean-room synthesis:
> findings, byte layouts, opcodes, and references to decompiled function names /
> OSS symbols only — no vendor source reproduced. The VIA/QMK family
> (`0xFF60`/usage `0x61`) is out of scope (see `docs/protocols/keyboard/via.md`).
>
> **Hardware wins.** Proven twice this session: the `0xFF13` (not `0xFF00`)
> control collection; the report-id-`0x00` (not `0x04`) time-sync framing.

**Sources read:** `docs/protocols/keyboard/{proprietary,ak980pro_vendor,ak980pro_tft_protocol,ak980pro_macros_protocol,ak980pro_perkey_rgb_protocol,ak980pro_mui_dll,ak980pro_assets_inventory,via}.md`; `src/devices/keyboard/src/{proprietary_protocol.hpp,proprietary_keyboard.cpp,register.cpp}`; `tests/unit/test_ak980_*.cpp` + `test_proprietary_keyboard_protocol.cpp`; the Ghidra decompiles at `reverse-eng-workdir/ak980pro/decomp_targets/` (39 functions, analysis-only).

---

## 1. Device identity & topology

### 1.1 VID:PID matrix

| Variant | VID | PID | Mode | HID interface |
| --- | --- | --- | --- | --- |
| AK980 PRO (wired USB) | `0x0C45` | `0x8009` | USB | `MI_00` (composite) |
| AK980 PRO (2.4 GHz) | `0x0C45` | `0xFEFE` | dongle | `MI_03` |
| XS75T (re-badge) | `0x05AC` | `0x024F` | USB 75-key | `MI_00` (Apple VID; separate enumerator) |
| AK680 / AK510 (legacy) | `0x3151` | `0x4024`–`0x4029` (provisional) | USB | `usage_page=0xFF00` (provisional) |

`0x0C45` = Microdia (the chip-vendor ID the AK980 PRO enumerates under; Sonix SN32F299 licensee). `register.cpp:55-70` registers it as `ak980pro` with `controlUsagePage=0xFF13`. `0x3151` = SONiX prefix shared with VIA keyboards *and* AJAZZ mice — the AK680/AK510 PID range is provisional/capture-only.

### 1.2 Composite-HID interface map (AK980 PRO)

| Interface | Role | Usage page | Notes |
| --- | --- | --- | --- |
| `MI_00` | Boot keyboard | `0x0001` | What naïve `hid_open` picks; **wrong** for control. |
| `MI_01`/`MI_02` | Other collections | — | Consumer control / mouse-emulation; not control. |
| `MI_03` | **Vendor control collection** | **`0xFF13`** | RTC / battery / RGB / TFT feature reports. Dongle also on `MI_03`. |

> **HARDWARE-PINNED CORRECTION.** `proprietary.md` originally listed `0xFF00`;
> the real AK980 PRO control collection is `0xFF13` (pinned 2026-05-20 — only
> that collection accepts the control SET_FEATURE). `register.cpp` sets
> `.controlUsagePage = 0xFF13`. The transport must filter by usage page or the
> boot-keyboard handle is selected and RTC/battery feature reports silently fail.

### 1.3 Report-ID conventions (the central gotcha)

Two framings, distinguished by transport:
- **OUTPUT-report path (`WriteFile`)** — 33-byte reports. Report ID `0x00`; **opcode at on-wire byte 1**. No `0x04` magic.
- **FEATURE-report path (`HidD_SetFeature`)** — 65-byte reports. Report ID `0x00` at byte 0; fixed frame-magic `0x04` at byte 1; **opcode at byte 2**.

The historically-quoted "byte 0 = 0x04 report id" was wrong: `0x04` is a *data* byte. The codebase keeps `ReportId = 0x04` for the legacy 64-byte builders (AK680/AK510 + mock tests), but the hardware-verified AK980 PRO time-sync/battery builders emit report-id `0x00`, 65-byte (`TimeReportSize = 65`).

---

## 2. Transport layer

### 2.1 The five transport helpers (Ghidra references)

| Helper | Win32 primitive | Wire framing | Length |
| --- | --- | --- | --- |
| `FUN_00451220` | `WriteFile` + `GetOverlappedResult` | OUTPUT report; pads to `OutputReportByteLength` | `0x21` (33) or `0x41` (65) |
| `FUN_00451300` | `ReadFile` (overlapped) | INPUT read; **strips a leading `0x00`** if present | — |
| `FUN_004514e0` | `DeviceIoControl 0xB0192` | `IOCTL_HID_GET_INPUT_REPORT` | — |
| `FUN_00451440` | `HidD_SetFeature` | FEATURE report | `0x41` (65) |
| (GET_FEATURE) | `HidD_GetFeature` | feature read | 65 |

### 2.2 Dispatch wrappers

| Wrapper | Role |
| --- | --- |
| `FUN_0044f5f0` | 33-byte OUTPUT write; retry once; ACK-poll via `FUN_00451300` (echo bytes 1–3, ≤20× `Sleep(10)`). Computes checksum at **byte 8** = `sum(bytes) mod 256`. |
| `FUN_0044f790` | As above, fixed opcode `0x02` (SAVE). |
| `FUN_0044eed0` | 65-byte FEATURE write (chunks 0x40-byte slices when `len ≥ 0x42`). Prepends report-id `0x00`, frame-magic `0x04`. The workhorse for config commands. |
| `FUN_0044f0c0` | Like `FUN_0044eed0` + reads back a feature report (per-key RGB `0x20 0x04`). |
| `FUN_0044f2d0` | 0x1001-byte (4097) bulk OUTPUT (DFU / TFT-bulk). |
| `FUN_0044f3a0` | Streaming reader (`timeBeginPeriod(1)`, ~360 ms timeout, ≤9×0x40 chunks) — FW version + battery + per-key RGB read-back. |
| `FUN_00435250` | 200 ms profile-switch poll thread. |

### 2.3 Three report sizes
`0x21` (33) short reports; `0x41` (65) standard config; `0x1001` (4097) bulk/image.

### 2.4 Per-packet handshake — HARDWARE-CONFIRMED (time-sync), 2026-05-21
The vendor `FUN_0044eed0`/`FUN_0044f5f0` do **`Sleep → SET_REPORT → GET_REPORT` per packet**. On a live AK980 PRO (`scripts/ak980_tft_probe.py`):
- Back-to-back `writeFeature()`s with delay 0 are a **silent no-op** — the clock doesn't move.
- A **~30 ms inter-packet settle delay** is required.
- A **`readFeature()` (GET_REPORT) readback** after each write mirrors the vendor; **best-effort** (the GET right after SAVE fails while the RTC commits — must not fail the sync).

`ProprietaryKeyboard::setTime()` implements `sleep(30ms) → writeFeature → best-effort readFeature` per packet + 100 ms SAVE settle. `batteryPercent()` uses the same `sleep(30ms)`+6-attempt poll.

### 2.5 Checksums
- OUTPUT path (`FUN_0044f5f0`): byte 8 = `sum(bytes) mod 256` (slot read as 0 during sum).
- TFT chunked: byte 32 = `sum(bytes[0..31]) mod 256` (`stampTftChecksum()`; pinned header `0x97`, index-0 chunk `0x16`). Firmware validation unknown.
- FEATURE path (`FUN_0044eed0`): no transport checksum; integrity via `0xAA 0x55` trailer.
- (The *mouse* Rust SDK uses a `& 0x7F` BIT7 checksum — a different lineage; do not conflate.)

---

## 3. Complete opcode catalogue

All offsets are on-wire byte indices (report-id = byte 0).

### 3.0 Master opcode table

| Opcode | Sub | Dir | Vendor fn | Role | Transport | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `0x01` | — | H→D | (FW-version) | Firmware version | OUTPUT/stream | ✅ impl |
| `0x02` | — | H→D | `FUN_0044f790` | CMD_SAVE | FEATURE 65 | ✅ impl |
| `0x05` | `0x10`/`0x01`/— | H→D | `FUN_0044be90`/`FUN_004349f0`/`FUN_0042b240` | Key remap / key-mode | OUTPUT/FEATURE | 🔍 partial |
| `0x07` | `0x10` | H→D | `FUN_00414290` | Settings batch | OUTPUT 33 | ✅ impl |
| `0x08` | zone | H→D | (RgbStatic) | Static RGB | 64-byte | ✅ impl |
| `0x09` | `0x1C` | H→D | `FUN_0042dc10` | Wired macro data | OUTPUT 33 | 🟡 doc |
| `0x0A` | — | H→D | (RgbBuffer) | Per-LED RGB chunked | 64-byte | ✅ impl |
| `0x0B` | `0x1C` | H→D | `FUN_00432be0` | Lighting params | OUTPUT 33 | 🔍 partial |
| `0x0C` | `0x10` | H→D | `FUN_00423a10` | **LCD-aware time alias** (single-packet) | OUTPUT 33 | 🟡 tried on HW, did NOT move clock |
| `0x0C` | — | H→D | (SetLayer) | Layer switch (legacy) | 64-byte | ✅ impl |
| `0x0D` | — | H→D | (UploadMacro legacy) | Macro upload (56-byte chunk) | 64-byte | 🔍 partial |
| `0x0E` | — | H→D | (CommitEeprom) | EEPROM commit | 64-byte | ✅ impl |
| `0x10`/`0x12` | `0x1C` | H→D | `FUN_00418c40` | Key remap chunked (Fn OFF/ON) | OUTPUT 33 | 🟡 doc |
| `0x11`/`0x27` | `0x09` | H→D | `FUN_004183a0` | Alt key remap (rich entry) | OUTPUT 33 | 🟡 doc |
| `0x13` | — | H→D | `FUN_0042b0a0` | **Firmware RGB mode** (20-mode) | FEATURE 65 | ✅ impl |
| `0x14` | `0x1C`/`0x18`/`0x10` | H→D | `FUN_0044be90` | Macro→key assign, chunked | OUTPUT 33 | 🟡 doc |
| `0x15` | `0x04` | H→D | `FUN_0042d690` | Wireless macro chunk-info | FEATURE 65 | 🟡 const |
| `0x17` | — | H→D | `FUN_00414020` | Sleep timer (shares settings pkt) | FEATURE 65 | 🟡 doc |
| `0x18` | — | H→D | `FUN_004238e0` | CMD_START | FEATURE 65 | ✅ impl |
| `0x19` | `0x04` | H→D | `FUN_0042d690` | Wireless macro BEGIN | FEATURE 65 | 🟡 const |
| `0x20` | `0x01` | D→H | `FUN_004358c0` | **Battery query** | OUTPUT 33 + read | ✅ impl |
| `0x20` | `0x04` | H→D | `FUN_00427db0`/`FUN_004329a0` | **Per-key RGB upload** | FEATURE 65 chunked | ✅ builders |
| `0x23` | `0x04` | H→D | `FUN_0044ba20` | Macro record-buffer upload | FEATURE 65 + bulk | 🟡 doc |
| `0x28` | — | H→D | `FUN_004238e0` | CMD_TIME (time-sync preamble) | FEATURE 65 | ✅ impl |
| `0x72` | — | H→D | `FUN_00422920` | **TFT bulk upload BEGIN** | FEATURE 65 | 🟡 scaffold |
| `0x7F` | `0x03` | H→D | `FUN_004231c0` | **TFT chunked image header** | OUTPUT 33 | ✅ builders (PROVISIONAL) |
| `0x80\|n` | — | H→D | `FUN_004231c0` | **TFT chunked image data** | OUTPUT 33 | ✅ builders (PROVISIONAL) |
| `0xF0` | — | H→D | `FUN_0042b0a0` | CMD_FINISH | FEATURE 65 | ✅ impl |
| `0xF5` | `0x03`/`0x09` | H→D | `FUN_0042ae80` | Per-key RGB read-back | OUTPUT + stream | 🟡 builder |

### 3.4 Settings batch (`0x07 0x10`) — IMPLEMENTED
33-byte (`FUN_00414290`). 4-packet envelope `START(0x18) → DATA(0x07 0x10) → SAVE(0x02) → FINISH(0xF0)`. Byte map: `0x01` fixed @5, disableWin @6, disableAltF4 @7, disableAltTab @8 (also checksum slot), fnLayerSwitch @9, sleepTimer @10, keyResponseTime(1..5) @12, trailer `0xAA 0x55` @18/19. (Pinned in `test_ak980_settings_batch.cpp`; response-time 0→default 3, >5→clamp 5.)

### 3.6 Firmware RGB mode (`0x13`) — IMPLEMENTED
5-packet envelope `START(0x18) → MODE_BEGIN(0x13) → DATA → SAVE(0x02) → FINISH(0xF0)`. DATA: mode_id @1, R/G/B tint @2/3/4, rainbow @8, brightness(0..5) @9, speed(0..5) @10, direction(0..3) @11, trailer `0x55 0xAA` @14/15.
**20 modes** (`availableFirmwareModes()`): 0x00 Static, 0x01 SingleOn, 0x02 SingleOff, 0x03 Glittering, 0x04 Falling, 0x05 Colourful, 0x06 Breath, 0x07 Spectrum, 0x08 Outward, 0x09 Scrolling, 0x0A Rolling, 0x0B Rotating, 0x0C Explode, 0x0D Launch, 0x0E Ripples, 0x0F Flowing, 0x10 Pulsating, 0x11 Tilt, 0x12 Shuttle, 0x13 LedOff. (A separate "cosmic" 500-519 list = SQLite scene presets, NOT firmware modes. A third "Real-time Lighting" picker = 9 host-side effects pushed as cooked per-key RGB via `0x20 0x04`.)

### 3.8 Time-sync (`0x18`/`0x28`/`0x02`) — HARDWARE-CONFIRMED 2026-05-21
Runtime path is the **4-packet envelope** (NOT the `0x0C 0x10` alias, which did NOT move the clock on hardware). 65-byte FEATURE, report-id `0x00`, `0xFF13`. Per-packet handshake per §2.4.
```
START    : 00 04 18 00 ...
PREAMBLE : 00 04 28 00 00 00 00 00 00 01 00 ...     (marker 0x01 @ byte 9)
DATA     : 00 00 01 5A YY MM DD hh mm ss 00 dow ... AA 55
SAVE     : 00 04 02 00 ...
```
DATA byte map: report-id `0x00`@0, `0x00`@1, LCD-select+1 @2, `0x5A` magic @3, year−2000 @4, month @5, day @6, hour @7, minute @8, second @9, `0x00`@10, dayOfWeek(0=Sun) @11, `0xAA`@63, `0x55`@64. Host sends LOCAL time. SAVE `0x02` ≠ `CmdCommitEeprom 0x0E`. 100 ms settle after SAVE.

### 3.9 Battery query (`0x20 0x01`) — HARDWARE-CONFIRMED 2026-05-21
`FUN_004358c0`. Request byte1=`0x20`, byte2=`0x01`, report-id `0x00`. Reply via `readFeature()` into a **65-byte** buffer: opcode echo `resp[1]`, **charge percent `resp[4]`** (0→nullopt; `0xFF` wired+full→clamp 100). **A 64-byte GET buffer makes `hid_get_feature_report` fail on Windows — must be 65.** hidapi returns the reply WITH the report-id at index 0 (does NOT strip it like the vendor `ReadFile`), so the decompile's `resp[3]` lands at `resp[4]`. Vendor polls 15 s while wireless (`this[0x784]==2`); skips wired.

### 3.12 TFT image upload (`0x7F 0x03` + `0x80|n`; bulk `0x72`) — PROVISIONAL
Rebuilt 2026-05-21 from `FUN_004231c0`/`FUN_00422920`; **NOT hardware-confirmed**. Geometry 240×135 RGB565 = 64 800 B/frame, ≤140 frames, 28 B/chunk.
**Header** (OUTPUT 33): `0x00`@0, `0x7F`@1, `0x03`@2, `0x00`@3, lcd+1 @4, 24-bit chunk count @5..7, checksum @32.
**Chunk**: `0x00`@0, `0x80 | ((idx>>16)&0x7F)`@1 (marker on byte 1), `idx&0xFF`@2, `(idx>>8)&0xFF`@3, 28 B pixel @4..31, checksum @32. Decoder: `idx = b2 | b3<<8 | (b1&0x7F)<<16`.
**Bulk** (`0x72`): scaffold only; 4-packet `START(0x18)→BEGIN_BULK(0x72)→4097-byte chunks→SAVE(0x02)`; ~143× faster; not wired; path-selection trigger undetermined.
RGB565 byte order (`mui.dll::GetImageRGB565Data`, not in corpus): impl emits big-endian top-down row-major (`#FF8000`→`0xFC 0x00`); **unverified**.

> **Correction history:** pre-2026-05-21 the index low byte was on byte 1 and the marker on byte 2, with LCD on header byte 3 + uint32 count at 4..7 — byte-transpositions vs `FUN_004231c0`, now fixed.

### 3.13 Per-key RGB blob layouts
- **Wired** (192 B = 3×64): 1 byte/LED, monochromatic brightness; firmware expands to greyscale.
- **Wireless** (512 B = 8×64): 4 bytes/LED = `[reserved=0, R, G, B]`, 128 LEDs (light_index ≤123). Header mode byte @9 = `0x03` wired / `0x08` wireless.

### 3.14 Macros (`0x09` wired / `0x19`+`0x15` wireless)
3584-byte body: `[0..399]` 100-slot index table (4 B each), `[400..]` bodies (2-B LE event_count + 6 reserved + N×4-B events), `0xAA 0x55` trailer. Per-event byte-3 opcode: `0x10` mouse-up, `0x30` key-up, `0x50` delay, `0x90` mouse-down, `0xB0` key-down. Mouse value remap L/R/M = wire `0x01`/`0x04`/`0x02` (NOT DB 1/2/3). No mouse-move/wheel.

### 3.17 CMD_FINISH (`0xF0`) + standard envelope
Most UI commits: `START(0x18) → group-select → DATA(+0xAA 0x55) → SAVE(0x02) → FINISH(0xF0)`. Implemented for lighting-mode (5-pkt) + settings batch (4-pkt). RTC stays 4-packet (firmware accepts early stop on SAVE for time data).

---

## 4. RE methods & corroborating sources

| Source | Type | Corroborates |
| --- | --- | --- |
| `gohv/EPOMAKER-Ajazz-AK820-Pro` (Rust) | OSS | `0x18/0x28/0x02/0xF0`, `0x13` 20 modes, sleep enum, time-data map, 100 ms settle |
| `KyleBoyer/TFTTimeSync-node` (TS) | OSS | `[0x04,0x02/0x18/0x28]` framings, local-time |
| `aar-rafi/aks075-linux` | OSS | credits gohv for the same format |
| TaxMachine AK820 Pro / `1033.lan` | strings | 20-mode names, settings labels |
| Ghidra decompile of `DeviceDriver.exe` | static | every opcode + transport taxonomy (39 `FUN_*.c`) |
| **Frida hook** (`HidD_SetFeature`/`WriteFile`) on live device | dynamic | **time-sync + battery confirmed**; report-id `0x00`/65-byte/`0xFF13` |
| `scripts/ak980_tft_probe.py` | probe | 30 ms+GET-readback handshake confirmed |

Persistence (vendor): SQLite `t_config_data`/`t_profile_data` (note `app` column = auto-switch-by-foreground-app, worth copying)/`t_key_otherdata`. Firmware update = external `FirmwareUpdateTool.exe` — DFU opcode NOT recoverable from `DeviceDriver.exe`.

---

## 5. Confidence matrix

| Feature | Opcode | Confidence | Witness |
| --- | --- | --- | --- |
| Time-sync envelope + 30 ms/readback handshake | `0x18/0x28/0x02` | **Hardware-confirmed** | Frida + live TFT clock (2026-05-21) |
| `0xFF13` control collection | (topology) | **Hardware-confirmed** | only collection accepting SET_FEATURE |
| Battery query | `0x20 0x01` | **Hardware-confirmed** | live read, 65-B buffer, resp[4] |
| Firmware RGB mode (20-mode) | `0x13` | Decompile + corpus, impl | `FUN_0042b0a0` + gohv; no live RGB witness |
| Settings batch | `0x07 0x10` | Decompile-only, impl | `FUN_00414290` |
| Per-key RGB write/read | `0x20 0x04` / `0xF5` | Decompile-only, impl | wired-monochrome unverified |
| TFT chunked/bulk | `0x7F/0x80/0x72` | **Decompile-only, PROVISIONAL** | NO capture; transport/byte-order/checksum unverified |
| LCD-aware time alias | `0x0C 0x10` | **Decompile-only, NEGATIVE on HW** | tried, did NOT move clock |
| DFU / firmware flash | — | **Unrecoverable** | external `FirmwareUpdateTool.exe` |

See [`unexplored.md`](unexplored.md) for the full open-questions list (TFT validation, report-id-0x00 generalisation, per-key RGB layout, macros/layers field-confirmation, DFU, input-report map).
