# AJAZZ AJ-series Mouse Protocol

The AJ-series mice (AJ159, AJ199, AJ339 Pro, AJ380) share a common configuration protocol over a vendor-defined HID interface (usage page `0xFF00`, usage `0x01`). The official Windows utility sends 64-byte feature reports with the envelope below.

## Envelope

```
byte 0 : report id             (0x05)
byte 1 : command id            (see table)
byte 2 : sub-command
byte 3 : payload length (N)
byte 4..4+N-1 : payload
byte 63 : checksum = (sum of bytes 1..62) mod 256
```

## Command ids

| Id   | Name        | Sub-commands                                     |
| ---- | ----------- | ------------------------------------------------ |
| 0x21 | DPI         | 0x00 set-stage, 0x01 set-active, 0x02 get-stages |
| 0x22 | Poll rate   | 0x00 set, 0x01 get                               |
| 0x23 | Lift-off    | 0x00 set (deci-mm), 0x01 get                     |
| 0x24 | Button bind | 0x00 set-binding, 0x01 set-macro                 |
| 0x30 | RGB         | 0x00 static color, 0x01 effect, 0x02 brightness  |
| 0x40 | Battery     | 0x00 status (wireless only)                      |
| 0x50 | Commit      | 0x00 save to EEPROM                              |

## Example frames

### Set DPI stage 2 to 1600 DPI with blue indicator

```
05 21 00 06  02 06 40 00 00 FF   00 00 ... 00   CK
```

### Set polling rate to 1000 Hz

```
05 22 00 02  03 E8 00 00 00 00   ...           CK
```

## Battery (wireless models)

```
host  → 05 40 00 00  ... CK
device← 05 40 00 01  BB  ... CK      (BB = percent, 0..100)
```

Offline device returns `BB = 0xFF`.

## References

- [`progzone122/ajazz-aj199-official-software`](https://github.com/progzone122/ajazz-aj199-official-software) — frozen snapshot of the Windows binary, consulted only to *run* the tool during captures. Not disassembled or copied.
- AJAZZ AJ199 user manual (manuals.plus) — physical button layout and LED zone naming.
