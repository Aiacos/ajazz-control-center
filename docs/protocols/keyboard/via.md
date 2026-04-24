# VIA / QMK Raw HID Protocol — AJAZZ AK series

Most modern AJAZZ mechanical keyboards (AK820 Pro and siblings) ship with QMK firmware and expose the VIA configuration endpoint on HID interface #1. Our VIA backend speaks the documented raw-HID protocol and re-adds the bits that the stock VIA configurator does not cover.

## Report format

All reports are **32 bytes** over HID interface `RAW` (usage page `0xFF60`, usage `0x61`).

```
byte 0 : report id (0x00 on Linux/macOS, omitted on Windows WriteFile)
byte 1 : command id
byte 2..31 : payload (command-specific)
```

## Supported commands

| Id   | Name                                 | Direction | Notes                                   |
| ---- | ------------------------------------ | --------- | --------------------------------------- |
| 0x01 | `id_get_protocol_version`            | GET       | returns `uint16_t` big-endian           |
| 0x02 | `id_get_keyboard_value`              | GET       | uptime, layout options, layer state     |
| 0x03 | `id_set_keyboard_value`              | SET       | device-state mutator                    |
| 0x04 | `id_dynamic_keymap_get_keycode`      | GET       | returns keycode for `(layer, row, col)` |
| 0x05 | `id_dynamic_keymap_set_keycode`      | SET       | mirrors getter                          |
| 0x07 | `id_dynamic_keymap_reset`            | SET       | restores factory keymap                 |
| 0x08 | `id_custom_set_value`                | SET       | lighting / RGB (channel-addressed)      |
| 0x09 | `id_custom_get_value`                | GET       | read lighting state                     |
| 0x0A | `id_custom_save`                     | SET       | commits to EEPROM                       |
| 0x0D | `id_dynamic_keymap_macro_set_buffer` | SET       | chunked macro upload                    |

## RGB / lighting

`id_custom_set_value` with channel `0x01` (qmk_rgblight) sub-commands:

| Sub  | Name       | Payload                           |
| ---- | ---------- | --------------------------------- |
| 0x02 | brightness | `uint8` (0..255)                  |
| 0x03 | effect     | `uint8` effect id + `uint8` speed |
| 0x04 | color      | RGB 3 bytes                       |

## Macros

Macro buffer uploads use `id_dynamic_keymap_macro_set_buffer`:

```
byte 1 : 0x0D
byte 2..3 : offset (big-endian)
byte 4    : length of payload in this frame (0..28)
byte 5..  : macro bytes (HID usage codes, prefixed with 0x01 for tap, 0x02 for
            down, 0x03 for up, 0x04 for delay)
```

## References

- QMK Firmware, `quantum/via.h` — public source, consulted only as a protocol document (not copied).
- VIA Configurator, `app/src/utils/keyboard-api.ts` — ditto.
