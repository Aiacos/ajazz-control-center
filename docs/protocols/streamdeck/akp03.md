# AJAZZ AKP03 / AKP03E / AKP03R — Mirabox N3 family

> A small desk controller built around **6 LCD keys + 3 rotary encoders + 3
> non-LCD buttons**. The N3 nickname covers a long list of rebadges sold
> under different brand names with identical firmware.
>
> ⚠️ **In-tree gap (2026-05-14):** the current `akp03.cpp` models only
> "6 keys + 1 encoder". That is wrong — the device has **3 encoders and
> 3 non-LCD buttons** in addition to the 6 LCD keys. This document is the
> spec the implementation must converge to; a tracking entry exists in
> `TODO.md` under "AKP03 layout reconciliation".

## Hardware

**Sources:** `[mirabox-n3]` `[ajazz-sdk]` `[opendeck-akp03]` `[companion]` — see
[`_research-sources.md`](./_research-sources.md).

| Property                    | Value                                                                                                                                                                 |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Form factor                 | Desktop macropad with detachable stand                                                                                                                                |
| Connection                  | USB-C, wired                                                                                                                                                          |
| Operating voltage / current | 5 V / 0.6 A (3.0 W typical)                                                                                                                                           |
| LCD keys                    | **6**, arranged in **2 rows × 3 columns**                                                                                                                             |
| LCD key size                | 72 × 72 px nominally, but the **image format on the wire is 60×60 JPEG** (`Rot0`, no mirror) per `[ajazz-sdk]`. The AKP03R rev. 2 uploads 64×64 with `Rot90` instead. |
| Non-LCD buttons             | **3**, positioned in a row underneath the LCD grid                                                                                                                    |
| Rotary encoders             | **3** — one larger primary knob, two smaller secondaries. All three are pressable.                                                                                    |
| Boot logo display           | One small front LCD strip, 320×240 px (uploaded JPEG at `Rot90`)                                                                                                      |
| Touch strip                 | None                                                                                                                                                                  |
| Dimensions                  | 129 × 78 × 59 mm                                                                                                                                                      |
| Weight                      | 148 g (no stand) / 200 g (with stand)                                                                                                                                 |
| Material                    | ABS body + acrylic LCD keycaps                                                                                                                                        |
| OS support                  | Windows ≥ 7, macOS ≥ 10.15, Linux (community)                                                                                                                         |
| Vendor software             | StreamDock (Mirabox), AJAZZ App                                                                                                                                       |
| Bundled features            | Hotkeys, multi-action chains, folders, scene auto-switch, animated GIF icons, drag-and-drop assignment to keys *and* knobs                                            |

### Layout sketch

From `[companion]`'s N3 mapping diagram (recreated in ASCII):

```
+------+------+------+
| LCD1 | LCD2 | LCD3 |    <- 2 rows x 3 cols of 72x72 LCD keys
+------+------+------+
| LCD4 | LCD5 | LCD6 |
+------+------+------+
|  B7  |  B8  |  B9  |    <- 3 non-LCD buttons (action codes 0x25 / 0x30 / 0x31)
+------+------+------+
|  (((( E1 ))))      |    <- "large" primary encoder (Rotary 1)
|        ((E2))((E3))|    <- 2 smaller encoders (Rotary 2 and Rotary 3)
+------+------+------+
```

### Variants and rebadges

`[ajazz-sdk]` knows the AJAZZ-branded SKUs:

| Codename in `[ajazz-sdk]` | Marketing name     | VID      | PID      |
| ------------------------- | ------------------ | -------- | -------- |
| `Akp03`                   | AJAZZ AKP03        | `0x0300` | `0x1001` |
| `Akp03E`                  | AJAZZ AKP03E       | `0x0300` | `0x3002` |
| `Akp03R`                  | AJAZZ AKP03R       | `0x0300` | `0x1003` |
| `Akp03RRev2`              | AJAZZ AKP03R rev 2 | `0x0300` | `0x3003` |

`[opendeck-akp03]` adds the Mirabox-branded and licensee-branded units
sharing the same firmware:

| Marketing name               | VID      | PID      |
| ---------------------------- | -------- | -------- |
| Mirabox N3                   | `0x6602` | `0x1002` |
| Mirabox N3 (rev. 3)          | `0x6603` | `0x1002` |
| Mirabox N3EN                 | `0x6603` | `0x1003` |
| Soomfon Stream Controller SE | `0x1500` | `0x3001` |
| Mars Gaming MSD-TWO          | `0x0B00` | `0x1001` |
| TreasLin N3                  | `0x5548` | `0x1001` |
| Redragon Skyrider SS-551     | `0x0200` | `0x2000` |

⚠️ **The current `register.cpp` lists `0x0300:0x3001` for `akp03` — that is
wrong**: per `[ajazz-sdk]` the canonical AJAZZ AKP03 PID is `0x1001`, not
`0x3001`. The fix is part of the `AKP03 layout reconciliation` TODO. The
hot-plug capture from `[capture-2026-05-13]` surfaced **`0x0300:0x3004`** —
not present in either `[ajazz-sdk]` or `[opendeck-akp03]` — so it is most
likely a new AKP03 sibling or pre-production unit. We register it but
treat it as `scaffolded`.

### USB identifier map (canonical, post-2026-05-14)

```
0x0300:0x1001 AKP03     (was registered as 0x3001 — fix in TODO)
0x0300:0x3002 AKP03E
0x0300:0x1003 AKP03R
0x0300:0x3003 AKP03R rev 2
0x0300:0x3004 Unknown sibling (HOTSPOTEKUSB HID DEMO) — see register.cpp:97
0x6602:0x1002 Mirabox N3
0x6603:0x1002 Mirabox N3 rev 3
0x6603:0x1003 Mirabox N3EN
0x1500:0x3001 Soomfon Stream Controller SE
0x0B00:0x1001 Mars Gaming MSD-TWO
0x5548:0x1001 TreasLin N3
0x0200:0x2000 Redragon Skyrider SS-551
```

Cumulatively this is **12 distinct USB identifiers** that should all open
the same N3 backend.

## Features that must work

| Feature                                                                     | Required for `functional` | Required for `stable` |
| --------------------------------------------------------------------------- | ------------------------- | --------------------- |
| Open / close transport with v2 (1024-byte) framing                          | ✅                        | ✅                    |
| Read **LCD key** press / release events                                     | ✅                        | ✅                    |
| Read **non-LCD button** events (action codes `0x25`/`0x30`/`0x31`)          | ✅                        | ✅                    |
| Read **3 encoder** rotation events (CW + CCW) for all knobs                 | ✅                        | ✅                    |
| Read **3 encoder press / release** events                                   | ✅                        | ✅                    |
| Set per-key image (60×60 JPEG, `Rot0`; or 64×64 / `Rot90` on AKP03R rev. 2) | ✅                        | ✅                    |
| Set global brightness (0..100)                                              | ✅                        | ✅                    |
| Clear single / all keys                                                     | ✅                        | ✅                    |
| Set boot logo (320×240 JPEG, `Rot90`)                                       | —                         | ✅                    |
| Read firmware version (Feature Report ID `0x01`)                            | —                         | ✅                    |
| Forward animated GIF as multi-frame JPEG                                    | —                         | nice-to-have          |
| Probe sibling PIDs (`0x3004`) with capability fallback                      | —                         | ✅                    |

## Wire protocol

The AKP03 reuses the AKP153 framing **prefix** (`CRT` at bytes 0..2 +
3-byte ASCII command word at bytes 5..7) but ships **1024-byte packets**
(`is_v2_api` in `[ajazz-sdk]`). All existing AKP153 opcodes (`LIG`, `STP`,
`CLE`, `BAT`, `LOG`, `HAN`, `CONNECT`) are reused with the larger packet
size; the only new opcodes are the action codes returned in input reports.

### Action codes (input reports byte 9)

Bytes 9 carries the action code:

| Action code  | Meaning                                                                                                                   |
| ------------ | ------------------------------------------------------------------------------------------------------------------------- |
| `0x01..0x06` | LCD key 1..6 — press/release polarity not directly encoded; treat as press event with synthesised release on next reading |
| `0x25`       | Non-LCD button 7                                                                                                          |
| `0x30`       | Non-LCD button 8                                                                                                          |
| `0x31`       | Non-LCD button 9                                                                                                          |
| `0x90`       | Encoder 0 CCW (large knob)                                                                                                |
| `0x91`       | Encoder 0 CW                                                                                                              |
| `0x50`       | Encoder 1 CCW                                                                                                             |
| `0x51`       | Encoder 1 CW                                                                                                              |
| `0x60`       | Encoder 2 CCW                                                                                                             |
| `0x61`       | Encoder 2 CW                                                                                                              |
| `0x33`       | Encoder 0 press                                                                                                           |
| `0x35`       | Encoder 1 press                                                                                                           |
| `0x34`       | Encoder 2 press                                                                                                           |
| `0x00`       | NOP / keep-alive frame                                                                                                    |

⚠️ The current `parseInputReport` in `akp03.cpp` only recognises
`0x01..0x06` (keys) and `tag & 0xF0 == 0x20` (encoder index) — that misses
**every** action code above 0x20 except those that happen to fall in the
0x20..0x2F range. Concretely it drops every event from buttons 7-9 and
all three encoders. The parser table must be rewritten against this table.

### Press / release encoding

The N3 firmware does not always emit both edges of a transition — the
constraint `[companion]` documents:

- Non-LCD buttons (`0x25/0x30/0x31`) emit only **on release**.
- Encoders emit press/release pairs, but **rotation events arrive
  unidirectionally** (one frame per detent, no "release" for a rotation).
- LCD keys emit one frame per transition (same as AKP153).

The backend must synthesise the missing edge so consumers see uniform
`KeyPressed` / `KeyReleased` events.

### Image upload

Identical structure to AKP153 but with 1024-byte chunks and the per-model
image format from `[ajazz-sdk]`:

| Model                   | Encoding | Size    | Rotation | Mirror |
| ----------------------- | -------- | ------- | -------- | ------ |
| AKP03 / AKP03E / AKP03R | JPEG     | 60 × 60 | `Rot0`   | none   |
| AKP03R rev. 2           | JPEG     | 64 × 64 | `Rot90`  | none   |

Boot logo:

| Field    | Value                              |
| -------- | ---------------------------------- |
| Encoding | JPEG                               |
| Size     | 240 × 320 (`Rot90` when displayed) |
| Opcode   | `LOG` (v2 header `4C 4F 47 00 00`) |

## Edge cases and quirks

- **Per-event press/release asymmetry** (above) is the source of the
  current `EncoderReleased → EncoderPressed value=0` workaround in
  `poll()`. The fix is the same as for AKP05: extend `core::DeviceEvent`
  with an `EncoderReleased` kind.
- The AKP03 emits *NOP frames* (`action code 0x00`) at idle for
  keep-alive — they must be silently discarded by the parser.
- Three different USB vendor IDs (`0x0300`, `0x5548`, `0x6602/3`) all map
  to the same firmware behaviour, so the geometry should be selected by
  PID alone after the VID/PID is matched.
- `0x0300:0x3004` (`HOTSPOTEKUSB HID DEMO`) registered via
  `[capture-2026-05-13]` is **not** in any third-party catalogue. Until a
  capture is annotated, treat it conservatively: open with default AKP03
  geometry and log a warning if any byte pattern in the input report
  diverges from the table above.

## Cross-references

- `[ajazz-sdk]` — VID/PID, geometry, opcode table, per-encoder action codes
- `[opendeck-akp03]` — rebadge USB IDs (full list above)
- `[companion]` — N3 layout sketch + press/release edge semantics
- `[mirabox-n3]` — vendor product page (dimensions, weight, materials)
- [`akp153.md`](./akp153.md) — sister 15-key device on the v1 API
- [`akp05.md`](./akp05.md) — full 10-key Plus-class device with touch strip
