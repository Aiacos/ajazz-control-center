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

> **HARDWARE NOTE (2026-05-22, verified):** the `0x40` query above does NOT
> match the shipping firmware. The working method — what
> `AjSeriesMouse::batteryPercent()` implements and logs as
> `queried ajazz_24g_8k: 100%` — is a **two-step handshake** (like the AK980
> keyboard): **SET_FEATURE a `0x83 GET_BATTERY` poke** (`[0x05, 0x83, 0…, BIT7]`)
> on the `0xFFFF`/usage-`0x02` control collection (iface 2), **then GET_FEATURE**
> the status report. That report uses **report-id `0x00`**, so hidapi returns
> `[00, 00, charge, 01 01 01 02]` — **charge at byte 2** (`0x64` = 100%). A valid
> frame has byte 1 == 0; byte 2 == 0 means asleep/not-reported → grey; reject
> frames with non-zero byte 1 (transient reconnect garbage). The earlier
> "passive GET_FEATURE, charge at byte 3" assumed a phantom `0x05` report-id
> prefix (one byte too far → rejected every valid frame → the persistent `--%`);
> the `0x83` OUTPUT+interrupt variant (b9018fc, reverted) read the wrong channel
> (the vendor's gRPC `sendMsg` adds `dangle_dev_type` routing; the reply actually
> surfaces in the GET_FEATURE status report). NOT a libusb blocker. Full detail +
> frame table in `aj_series_opcode_table.md` §4.

## Onboard clock (OLED basetta)

> **HARDWARE NOTE (2026-05-21):** mice with an OLED basetta (e.g. the 2.4G 8K,
> AJ199 family) drive the on-screen clock through a **firmware RTC** set with a
> single opcode `0x28` (`FEA_CMD_SET_OLEDCLOCK`) feature report — a required
> fixed `0xD7` marker at byte 8, big-endian year, no checksum, sent via
> `HidD_SetFeature`. This is **NOT** a host-rendered bitmap; the older
> `0x25 FEA_CMD_SETTFTLCDDATA` RGB565 render path never actually set the clock.
> Full packet layout in `aj_series_opcode_table.md` §3.15.

## References

- [`progzone122/ajazz-aj199-official-software`](https://github.com/progzone122/ajazz-aj199-official-software) — frozen snapshot of the Windows binary, consulted only to *run* the tool during captures. Not disassembled or copied.
- AJAZZ AJ199 user manual (manuals.plus) — physical button layout and LED zone naming.
