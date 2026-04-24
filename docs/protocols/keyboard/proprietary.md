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

| ID     | Name                          | Payload                                |
| ------ | ----------------------------- | -------------------------------------- |
| `0x01` | `GET_FIRMWARE_VERSION`        | —                                      |
| `0x05` | `SET_KEYCODE(layer,row,col)`  | BE16 keycode                           |
| `0x08` | `SET_RGB_STATIC(zone,r,g,b)`  | zone id + 24-bit RGB                   |
| `0x09` | `SET_RGB_EFFECT(zone,fx,spd)` | zone id + effect id + speed            |
| `0x0A` | `SET_RGB_BUFFER(zone,off,n)`  | chunked 60-byte LED buffer             |
| `0x0B` | `SET_RGB_BRIGHTNESS(percent)` | 0..100                                 |
| `0x0C` | `SET_LAYER(layer)`            | layer id 0..3                          |
| `0x0D` | `UPLOAD_MACRO(slot,off,len)`  | chunked 56-byte macro buffer           |
| `0x0E` | `COMMIT_EEPROM`               | —                                      |

## Zones

| ID     | Name       | LED count |
| ------ | ---------- | --------- |
| `0x00` | `keys`     | 104       |
| `0x01` | `sides`    | 18        |
| `0x02` | `logo`     | 4         |

## Layers

Up to 4 layers are supported (fn, fn+shift, etc.). The current active layer is reported in input reports at byte 1 and can be switched from the host with `SET_LAYER`.

## RGB effect ids

These map onto `ajazz::core::RgbEffect`:

| Effect id | Name            |
| --------- | --------------- |
| `0x00`    | `Static`        |
| `0x01`    | `Breathing`     |
| `0x02`    | `Wave`          |
| `0x03`    | `ReactiveRipple`|
| `0x04`    | `ColorCycle`    |
| `0x05`    | `Custom`        |

## Status

| Area                | State      |
| ------------------- | ---------- |
| Backend scaffolding | ✅ present |
| Keymap remap        | ✅ basic   |
| RGB zones           | ✅ basic   |
| Macros              | ✅ basic   |
| Input report parser | 🟠 partial |
| Capture fixtures    | 🟠 missing |
