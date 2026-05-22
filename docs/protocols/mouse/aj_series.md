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

> Solution narrative (how the read was made to work, root cause, verification):
> [`aj_series_battery.md`](aj_series_battery.md).

```
host  → 05 40 00 00  ... CK
device← 05 40 00 01  BB  ... CK      (BB = percent, 0..100)
```

Offline device returns `BB = 0xFF`.

> **HARDWARE NOTE (2026-05-22, verified on Windows):** the `0x40` query above
> does NOT match the shipping firmware. The working method — what
> `AjSeriesMouse::batteryPercent()` implements and logs as
> `queried ajazz_24g_8k: 100%` — is a **`0xF7` status poll then a GET_FEATURE**:
>
> 1. **SET_FEATURE the `0xF7` status poll** (report-id `0x00`, opcode `0xF7` at
>    body byte 0, zero payload) on the `0xFFFF`/usage-`0x02` control collection.
>    This is the enabler — it makes the basetta bring up its 2.4G telemetry link
>    and mirror the mouse charge into the status report. It replicates the vendor
>    `iot_driver`'s ~1 Hz heartbeat.
> 1. After a **~30 ms settle**, **GET_FEATURE report `0x05`** and read the charge.
>
> hidapi keeps the report-id byte at index 0 on Windows, so the frame is
> `05 00 00 64 01 01 01 02` — **charge at byte 3**; on Linux hidraw the unnumbered
> frame puts it at **byte 2**. `parseBatteryCharge()` auto-detects
> (`frame[0]==0x05 ? 3 : 2`). Status bytes read `01 01 01 02` when the link is up;
> charge `0` / all-zero = link not ready → grey. A non-zero byte 1 is transient
> reconnect garbage → rejected.
>
> **This is cross-platform on hidapi — NOT a libusb gap.** Earlier theories were
> wrong: a passive read with **no `0xF7` poll** leaves the link "not ready" so the
> report is all-zero on every platform (the real cause of the persistent `--%`);
> a `0x83` poke does not enable the link. Confirmed by a live probe across all 6
> HID collections + report ids — only the `0xF7` poll populates the charge, and it
> does so immediately. Full detail + frame table in `aj_series_opcode_table.md` §4.

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
