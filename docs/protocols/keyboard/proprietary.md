# AJAZZ proprietary keyboards — Wire Protocol (work in progress)

This document covers AJAZZ keyboards whose vendor software is closed-source and that **do not** ship with the VIA bootloader (e.g. AK680, AK510 and various "gaming" lineups). Their wire protocol is a superset of VIA's dynamic-keymap commands plus manufacturer-specific RGB and macro channels.

The backend in `src/devices/keyboard/src/proprietary_keyboard.cpp` is a clean-room implementation derived from USB captures only — **no vendor firmware, driver, or SDK is disassembled or reused**. Everything below is cited in the capture annotations under `docs/protocols/captures/keyboard/`.

## Identification

| Property   | Value (provisional) |
| ---------- | ------------------- |
| Vendor ID  | `0x3151`            |
| Product ID | `0x4024`–`0x4029`   |
| Interface  | `usage_page=0xFF00` |
| Report ID  | `0x04`              |

## Report layout (host → device)

All output reports are 64 bytes. Byte 0 is the report id (`0x04`), byte 1 is the command id, bytes 2..63 carry the payload.

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

**Status:** implemented for AK980 PRO (ARCH-05.1, 2026-05-17) — pending
physical round-trip witness for `functional` promotion.

`ProprietaryKeyboard::setTime()` writes a three-packet HID sequence to the
device using the firmware RTC opcode `0x28`. The wire format is
source-level corroborated against two independent reverse-engineering
corpora targeting the same Sonix SN32F299 MCU family:

- `gohv/EPOMAKER-Ajazz-AK820-Pro` (Rust, `src/protocol.rs`)
- `KyleBoyer/TFTTimeSync-node` (TypeScript, `src/packets.ts`)

Both expose byte-for-byte identical layouts.

### Three-packet sequence

| # | Purpose  | Report ID | Byte 1 | Byte 8 | Notable bytes               |
| - | -------- | --------- | ------ | ------ | --------------------------- |
| 1 | Preamble | `0x04`    | `0x28` | `0x01` | else 0x00                   |
| 2 | Data     | `0x00`    | `0x01` | second | b2=0x5A, b3=year-2000, b62=0xAA b63=0x55 |
| 3 | Save     | `0x04`    | `0x02` | `0x00` | else 0x00                   |

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
2. **Round-trip witness:** physical AK980 PRO accepts the 3-packet
   sequence and the TFT clock widget shows the time we sent —
   **DEFERRED to Phase 9.x physical test**.
3. **Negative witness:** sending a wrong year (e.g. 2099) produces a
   visible-but-wrong display, proving the firmware parses the field —
   **DEFERRED to Phase 9.x physical test**.

VIA-protocol keyboards (`ViaKeyboard`) are still explicitly excluded
per D-03 — they are QMK-style with no vendor clock surface.

See [`ARCH-05.1`](../../../.planning/phases/09-research-captures-hygiene/ARCH-05.1.md)
for the full ADR.
