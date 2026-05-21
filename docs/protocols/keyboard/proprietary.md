# AJAZZ proprietary keyboards — Wire Protocol (work in progress)

This document covers AJAZZ keyboards whose vendor software is closed-source and that **do not** ship with the VIA bootloader (e.g. AK680, AK510 and various "gaming" lineups). Their wire protocol is a superset of VIA's dynamic-keymap commands plus manufacturer-specific RGB and macro channels.

The backend in `src/devices/keyboard/src/proprietary_keyboard.cpp` is a clean-room implementation derived from USB captures only — **no vendor firmware, driver, or SDK is disassembled or reused**. Everything below is cited in the capture annotations under `docs/protocols/captures/keyboard/`.

## Identification

| Property   | Value (provisional)                                |
| ---------- | -------------------------------------------------- |
| Vendor ID  | `0x3151`                                           |
| Product ID | `0x4024`–`0x4029`                                  |
| Interface  | `usage_page=0xFF00` (provisional / family default) |
| Report ID  | `0x04` (provisional — see correction below)        |

> **HARDWARE NOTE (AK980 PRO, 2026-05-21):** the provisional values above were
> wrong for the AK980 PRO. Its vendor control collection is usage page **0xFF13**
> (not 0xFF00), and the time-sync feature reports use HID Report ID **0x00**
> (the 0x04 is the first *data* byte), 65 bytes long. Verified by Frida-hooking
> the vendor app + replaying the bytes (TFT clock followed an injected time).
> See `ak980pro_vendor.md` §3.1. The same report-id-0x00 correction probably
> applies to the other proprietary commands too, but only time-sync is
> hardware-verified so far.

## Report layout (host → device)

All output reports are 64 bytes. Byte 0 is the report id (`0x04`), byte 1 is the command id, bytes 2..63 carry the payload. (Exception: the AK980 PRO time-sync reports are 65 bytes with report id `0x00` — see the hardware note above.)

```
byte 0     : 0x04                 (report id)
byte 1     : command id (see below)
byte 2     : channel / sub-id
byte 3..N  : payload
byte N+1.. : zero padding
```

## Command table

| ID     | Name                          | Payload                      |
| ------ | ----------------------------- | ---------------------------- |
| `0x01` | `GET_FIRMWARE_VERSION`        | —                            |
| `0x05` | `SET_KEYCODE(layer,row,col)`  | BE16 keycode                 |
| `0x08` | `SET_RGB_STATIC(zone,r,g,b)`  | zone id + 24-bit RGB         |
| `0x09` | `SET_RGB_EFFECT(zone,fx,spd)` | zone id + effect id + speed  |
| `0x0A` | `SET_RGB_BUFFER(zone,off,n)`  | chunked 60-byte LED buffer   |
| `0x0B` | `SET_RGB_BRIGHTNESS(percent)` | 0..100                       |
| `0x0C` | `SET_LAYER(layer)`            | layer id 0..3                |
| `0x0D` | `UPLOAD_MACRO(slot,off,len)`  | chunked 56-byte macro buffer |
| `0x0E` | `COMMIT_EEPROM`               | —                            |

## Zones

| ID     | Name    | LED count |
| ------ | ------- | --------- |
| `0x00` | `keys`  | 104       |
| `0x01` | `sides` | 18        |
| `0x02` | `logo`  | 4         |

## Layers

Up to 4 layers are supported (fn, fn+shift, etc.). The current active layer is reported in input reports at byte 1 and can be switched from the host with `SET_LAYER`.

## RGB effect ids

These map onto `ajazz::core::RgbEffect`:

| Effect id | Name             |
| --------- | ---------------- |
| `0x00`    | `Static`         |
| `0x01`    | `Breathing`      |
| `0x02`    | `Wave`           |
| `0x03`    | `ReactiveRipple` |
| `0x04`    | `ColorCycle`     |
| `0x05`    | `Custom`         |

## Status

| Area                | State      |
| ------------------- | ---------- |
| Backend scaffolding | ✅ present |
| Keymap remap        | ✅ basic   |
| RGB zones           | ✅ basic   |
| Macros              | ✅ basic   |
| Input report parser | 🟠 partial |
| Capture fixtures    | 🟠 missing |

## Time sync

**Status:** HARDWARE-CONFIRMED on a physical AK980 PRO (2026-05-21) — the
on-device TFT clock follows an injected time, with the per-packet handshake
described below.

`ProprietaryKeyboard::setTime()` writes a **four-packet HID Feature Report**
sequence to the device using the firmware RTC opcodes `0x18` + `0x28` + data

- `0x02`. The wire format is source-level corroborated against two
  independent reverse-engineering corpora targeting the same Sonix SN32F299
  MCU family, plus disassembly of the AJAZZ vendor `DeviceDriver.exe`:

* `gohv/EPOMAKER-Ajazz-AK820-Pro` (Rust, `src/protocol.rs` + `src/usb.rs`)
* `KyleBoyer/TFTTimeSync-node` (TypeScript, `src/packets.ts` + `src/device.ts`)
* `aar-rafi/aks075-linux` README credits gohv for the same wire format
* Vendor `DeviceDriver.exe` imports `HidD_SetFeature` for this code path
  (Agent B static analysis, 2026-05-17) — confirms the Feature Report
  transport, not Output Report

All three corpora send the packets via `hid_send_feature_report` (USB
control-endpoint `SET_REPORT`), not via `hid_write` (interrupt OUT).

### Four-packet sequence

| #   | Purpose  | Report ID | Byte 1 | Byte 8 | Notable bytes                            |
| --- | -------- | --------- | ------ | ------ | ---------------------------------------- |
| 1   | Start    | `0x04`    | `0x18` | `0x01` | resets time-sync state machine           |
| 2   | Preamble | `0x04`    | `0x28` | `0x01` | configure-mode marker                    |
| 3   | Data     | `0x00`    | `0x01` | second | b2=0x5A, b3=year-2000, b62=0xAA b63=0x55 |
| 4   | Save     | `0x04`    | `0x02` | `0x00` | persist RTC to NV-RAM                    |

After packet 4, sleep 100ms (gohv `usb.rs` pattern) so the firmware commits
before any subsequent HID I/O can race the SAVE.

### Per-packet handshake (REQUIRED — hardware-confirmed 2026-05-21)

Sending the four `writeFeature()`s back-to-back is a **silent no-op** on a
real AK980 PRO — the firmware clock does not move. The time-sync is a
request/RESPONSE protocol: the vendor's `FUN_0044eed0` does
`Sleep -> SET_REPORT -> GET_REPORT` per packet. Empirically (live device,
0xFF13 collection, `scripts/ak980_tft_probe.py`):

- A **~30 ms inter-packet settle delay** is required. Back-to-back writes
  (delay 0) do not commit, even with the readback present.
- A **`readFeature()` (GET_REPORT) readback** after each `writeFeature()`
  mirrors the vendor handshake. It is **best-effort**: a readback failure
  (notably the GET right after SAVE, while the RTC commits to NV-RAM) must
  NOT fail the sync — the `writeFeature` is the actual command. Treating a
  post-SAVE readback exception as fatal made `setTime` return `IoError` and
  surfaced a red "failed" toast even though the clock had updated.

`ProprietaryKeyboard::setTime()` implements exactly this: per packet,
`sleep(30ms)` -> `writeFeature` -> best-effort `readFeature`, then the 100ms
SAVE settle. The interface is the 0xFF13 vendor collection (MI_03 on the
hardware sample); the device responds on GET_REPORT (e.g. the preamble
readback comes back with byte 4 set to `0x01`, a device-computed value).

The data packet uses HID Report ID `0x00` (NOT the default `0x04` used by
other commands); the firmware's real discriminator is the magic `0x5A` at
byte 2. Year is encoded as a single byte offset from 2000 (saturates at
0 for pre-2000); month is 1-based; the host sends LOCAL time, not UTC
(matches vendor app behaviour per both corpora).

The save opcode `0x02` is distinct from `CmdCommitEeprom = 0x0E` (used
for keymap / RGB / macro state) — the RTC has its own NV-RAM save path.

### What still gates `functional` promotion

Pitfall 19 three-witness rule for `ak980pro.clock` post-amendment:

1. **Capture witness:** source-level corroboration from two independent
   corpora — SATISFIED.
1. **Round-trip witness:** physical AK980 PRO accepts the 3-packet
   sequence and the TFT clock widget shows the time we sent —
   **DEFERRED to Phase 9.x physical test**.
1. **Negative witness:** sending a wrong year (e.g. 2099) produces a
   visible-but-wrong display, proving the firmware parses the field —
   **DEFERRED to Phase 9.x physical test**.

VIA-protocol keyboards (`ViaKeyboard`) are still explicitly excluded
per D-03 — they are QMK-style with no vendor clock surface.

See [`ARCH-05.1`](../../../.planning/phases/09-research-captures-hygiene/ARCH-05.1.md)
for the full ADR.

## Battery charge level

**Status:** HARDWARE-CONFIRMED on a physical AK980 PRO (2026-05-21) — the
wireless battery charge level reads back over HID on the 0xFF13 control
collection.

The query is opcode `0x20` sub `0x01`, sent with HID Report ID `0x00` (NOT
the `0x04` used by the legacy command convention). The reply is read back via
GET_FEATURE into a **65-byte** buffer.

| Field          | Value                                                       |
| -------------- | ----------------------------------------------------------- |
| Query opcode   | `0x20`                                                      |
| Query sub-id   | `0x01`                                                      |
| Report ID      | `0x00`                                                      |
| Read transport | GET_FEATURE (`readFeature`) into a 65-byte buffer           |
| Opcode echo    | `resp[1]`                                                   |
| Charge percent | `resp[4]` (0 = no battery; `0xFF` wired+full clamps to 100) |

Buffer-size pitfall (hardware-confirmed): a **64-byte** GET buffer makes
`hid_get_feature_report` *fail* on Windows — the buffer must be **65 bytes**.
hidapi prepends the report-id byte at index 0 of the returned buffer, which
is why the opcode echo lands at `resp[1]` and the charge percent at `resp[4]`
(one byte further along than the on-wire offsets).
