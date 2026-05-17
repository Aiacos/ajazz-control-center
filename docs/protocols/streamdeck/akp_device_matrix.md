# AJAZZ Stream Dock — full device matrix

> Source-of-truth catalogue cross-referenced against the vendor binaries
> (`Stream Dock AJAZZ.exe` strings + `SDLibrary1.dll` strings) and the
> existing public RE projects (`[ajazz-sdk]`, `[opendeck-akp03]`,
> `[opendeck-akp05]`, `[mirajazz]`, `[companion]`). 2026-05-17.
>
> This document supplements [`akp05_vendor.md`](./akp05_vendor.md) and
> our existing per-family docs (`akp03.md`, `akp05.md`, `akp153.md`,
> `akp815.md`). The vendor recognises **96 distinct codenames** across
> 7 silicon families (V1, V2, V25, V3, N1, N3, N4) — most are OEM
> rebadges sharing one of three wire protocols.

## 1. How to read this matrix

- **VID:PID columns** are populated where a public reference catalogue
  (or live USB capture) has confirmed them. Cells marked `?` mean the
  codename is referenced by the vendor binary but no PID has been
  published; capture pending.
- **Protocol version** column uses the `[mirajazz]` taxonomy:
  - **v1** (legacy 293/300/0108D/321D/H3): 512-byte packets, page-magic
    on `STP`, no firmware-side release events, hidapi only.
  - **v2** (AKP03/N3/AKP153 modern firmware): 512-byte packets,
    proper release events, hidapi.
  - **v3** (AKP03R rev. 2, AKP05/N4 family): 1024-byte packets, native
    press/release, sometimes WinUSB-class on the touch-strip channel.
- **Transport**: `hid` = hidapi-only, `winusb` = WinUSB used for
  high-bandwidth (touch-strip / boot-logo) channels, `hid+winusb` =
  device exposes both interfaces and the SDK uses both.
- **Image format**: per-key JPEG dimensions × rotation × mirror flag
  per `[ajazz-sdk]` `key_image_format()`.
- **Geometry**: keys ÷ encoders ÷ touch-strip dimensions if any.
- **Strip**: pixel resolution of the secondary LCD strip (if any).
- All **codename** values are the canonical strings the vendor SDK
  matches against (from `sdlibrary1_strings.txt` and
  `streamdock_exe_strings.txt`). When the SDK sees a device, it
  matches by `(VID, PID)`, then sets `_usbDevice = "<codename>"` and
  every subsequent codepath branches on the codename string.

## 2. AKP153 family (15 LCD keys, 3×5 grid)

**Wire**: v1 (legacy 293) or v2 (modern firmware) — runtime probe via
`isOld293Version()`. 512-byte packets. JPEG 85×85 with `Rot90` and
mirror, per `[ajazz-sdk]`. Brightness via `LIG`; key clear/all-clear
via `CLE`; flush via `STP`.

| Codename                                                      | VID:PID         | Model name (marketing)            | Notes                                                               |
| ------------------------------------------------------------- | --------------- | --------------------------------- | ------------------------------------------------------------------- |
| `AKP153`                                                      | `0x0300:0x1010` | AJAZZ AKP153                      | International. Per `[ajazz-sdk]` canonical pair is `0x5548:0x6674`. |
| `AKP153E`                                                     | `0x0300:0x1010` | AJAZZ AKP153E                     | China. PID `0x1010` per `[ajazz-sdk]`.                              |
| `AKP153R`                                                     | `0x0300:0x1020` | AJAZZ AKP153R                     | Regional revision; canonical PID per `[ajazz-sdk]`.                 |
| `TS115`                                                       | ?               | Mirabox TS115                     | OEM rebadge                                                         |
| `HSV293_O`                                                    | ?               | Mirabox HSV293 (overseas)         | Old 293 firmware                                                    |
| `HSV293-ARA`                                                  | ?               | Mirabox HSV293 (Arabic)           |                                                                     |
| `HSV293S-ARA`                                                 | ?               | Mirabox HSV293S (Arabic)          |                                                                     |
| `StreamDock[293]`                                             | ?               | Generic StreamDock 293            | Original 293 silicon                                                |
| `StreamDock[293V2]`                                           | ?               | StreamDock 293 (V2 firmware)      |                                                                     |
| `StreamDock[293S]`                                            | ?               | StreamDock 293S                   |                                                                     |
| `StreamDock[293SV3]`                                          | ?               | StreamDock 293S (V3 firmware)     | V3 firmware on 293S silicon                                         |
| `StreamDock[295]`                                             | ?               | StreamDock 295                    | Variant SKU                                                         |
| `MSD-ONE`                                                     | ?               | Mirabox MSD-ONE                   | "Master Stream Dock One"                                            |
| `MSD-ONE293SV3`                                               | ?               | MSD-ONE (293S V3 firmware)        |                                                                     |
| `VSD293SV3`                                                   | ?               | VSD 293S V3                       |                                                                     |
| `VSD293V3`                                                    | ?               | VSD 293 V3                        |                                                                     |
| `OMNI-STREAM`                                                 | ?               | OMNI-STREAM                       | New SKU 2025                                                        |
| `GK150`                                                       | ?               | GK150                             |                                                                     |
| `SS-550` / `SS550V3`                                          | ?               | Shortcut Stream 550 / 550 V3      | "ShortCutsPro" labelling                                            |
| `ShortCutsPro`                                                | ?               | ShortCutsPro                      |                                                                     |
| `ShortCutsProV2`                                              | ?               | ShortCutsPro V2                   |                                                                     |
| `XF-A3503A`                                                   | ?               | XF A3503A OEM                     |                                                                     |
| `A3503A`                                                      | ?               | A3503A                            |                                                                     |
| `293V25` / `293V3`                                            | ?               | 293 V25 / V3 firmware             | Internal product names                                              |
| `Stock293V3`                                                  | ?               | "Stock" 293 V3 reference design   |                                                                     |
| `M18V25` / `M18V25E`/ `M18V3` / `VSDM18` / `M18E`             | ?               | M18 family                        | 15-key keyboard-form SKU                                            |
| `TS15B3`                                                      | ?               | TS15B3                            | 15-key V3                                                           |
| `ControllerDeviceVision02` / `ControllerDeviceVision02_293V3` | ?               | OEM placeholders                  |                                                                     |
| `ControllerDeviceVision01`                                    | ?               | OEM placeholder                   |                                                                     |
| `StreamingDeckOne`                                            | ?               | Streamplify "StreamingDeckOne"    | StreamPlify rebadge                                                 |
| `D6-Pro`                                                      | ?               | D6 Pro                            |                                                                     |
| `CX35`                                                        | ?               | CX35                              |                                                                     |
| `ZX-554`                                                      | ?               | ZX-554                            |                                                                     |
| `APK153_293SV3` / `APK153E_293SV3` / `APK153R_293SV3`         | ?               | OEM labelling for AKP153 variants | Note: `APK153` (typo for AKP153) is the actual vendor string        |
| `XFA3503AV3Device` / `XFA3503A`                               | ?               | XF rebadge                        |                                                                     |
| `CN001V3Device` / `CN001`                                     | ?               | CN001                             | Generic OEM                                                         |
| `KB8IN1` / `KB8IN1-1` / `KB8IN1_1`                            | ?               | "Keyboard 8-in-1"                 | Multi-function keyboard                                             |
| `IYUT_D15` / `Womier_D15`                                     | ?               | Womier D15 (keyboard rebadge)     |                                                                     |
| `KB-1` / `KB-2`                                               | ?               | KB-1 / KB-2                       |                                                                     |
| `K-992` / `K992` / `MKE992`                                   | ?               | K-992 / MK992 keyboard            |                                                                     |
| `DK01`                                                        | ?               | DK01                              |                                                                     |
| `DR21`                                                        | ?               | DR21                              |                                                                     |
| `SMP_V1_2_4`                                                  | ?               | SMP V1.2.4                        |                                                                     |
| `DarkFlash`                                                   | ?               | DarkFlash rebadge                 |                                                                     |
| `LT1102`                                                      | ?               | LT1102                            |                                                                     |
| `BRHub293SV3`                                                 | ?               | BR Hub 293S V3                    | Internal hub variant                                                |
| `293S-R`                                                      | ?               | 293S regional revision            |                                                                     |
| `293V3EN`                                                     | ?               | 293 V3 EN                         |                                                                     |
| `293V3E`                                                      | ?               | 293 V3 E                          |                                                                     |
| `K1Pro` / `K1ProUS` / `K1ProZH`                               | ?               | K1 Pro (US/CN)                    | Keyboard with embedded LCD strip; `Create_Pro` is the same silicon  |
| `Create_Pro`                                                  | ?               | Create Pro                        | Same SDK class as K1Pro                                             |
| `SANWA` / `TOS300`                                            | ?               | SANWA / TOS300                    | OEM keyboards                                                       |
| `MOLA` / `X-Era` / `CX61` / `A3502A`                          | ?               | OEM variants                      |                                                                     |
| `TR-VISION_HOME`                                              | ?               | TR-VISION_HOME                    |                                                                     |
| `MSDPRO`                                                      | ?               | Mirabox MSD Pro                   | Likely shares 153/N4 architecture; codename appears in MSD family   |
| `NoctApotheker`-class fall-throughs                           | ?               | (none directly listed)            | Several other strings (`HSV293N6`, `BRHubN4Pro` etc) are non-153    |

Total in AKP153 family: ~80 codenames (the largest family due to OEM
rebadges).

## 3. AKP815 family (15 LCD keys, 5×3 grid, with optional strip)

**Wire**: v2. 512-byte packets. JPEG 100×100, `Rot180` (NOT Rot90 like
153). Has a main LCD strip (854×480 logo channel; 800×480 user channel
via `MAI`).

| Codename | VID:PID         | Model name    | Notes                       |
| -------- | --------------- | ------------- | --------------------------- |
| `AKP815` | `0x5548:0x6672` | AJAZZ AKP815  | Canonical per `[ajazz-sdk]` |
| `TS183`  | ?               | Mirabox TS183 | OEM rebadge                 |

## 4. AKP05 / Mirabox N4 family (10 LCD keys + 4 encoders + touch strip)

**Wire**: v3 by `[mirajazz]` taxonomy. **1024-byte packets** in modern
firmware (our backend currently sends 512; migration tracked in
TODO.md). JPEG 85×85 per key. Touch strip 800×480 px, partial-update
via `DRA` (akp05_vendor.md §3 row 190). 4 encoders, each binds to
one of 4 strip zones.

| Codename                                      | VID:PID         | Model name                            | Notes                                                                                                                              |
| --------------------------------------------- | --------------- | ------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| `MBox-N4`                                     | `0x6603:0x1007` | Mirabox N4                            | Canonical per `[opendeck-akp05]`. Provisional AJAZZ-branded PID is `0x0300:0x5001`.                                                |
| `MBox-N4 E`                                   | ?               | Mirabox N4E                           | Vendor variant                                                                                                                     |
| `MBox-N4E`                                    | ?               | Mirabox N4E (alt codename)            | Same as above; vendor sometimes uses the without-space variant                                                                     |
| `MBox-N4Pro`                                  | ?               | Mirabox N4 Pro                        | Pro variant; vibration motor (`N4Pro/enableVibration` registry key) and per-key RGB (`N4Pro/lightControlMode`)                     |
| `MBox-N4ProE`                                 | ?               | Mirabox N4 Pro E                      |                                                                                                                                    |
| `MBox-N6`                                     | ?               | Mirabox N6                            | 6-key sibling on N4 silicon (only 6 LCD keys, fewer encoders)                                                                      |
| `N4Pro` / `N4ProE`                            | ?               | N4 Pro (alt codename)                 |                                                                                                                                    |
| `N4V25`                                       | ?               | N4 V25 firmware                       | 2025-revision firmware                                                                                                             |
| `MSDPRO` / `MSDNEO`                           | ?               | MSD Pro / MSD Neo                     | New 2025 SKUs                                                                                                                      |
| `VSDN4` / `VSD_N4` / `VSDN4Pro` / `VSD_N4Pro` | ?               | VSD N4 / N4 Pro                       | OEM rebadge                                                                                                                        |
| `SD14N4V25` / `TS10N4V25`                     | ?               | SD14 / TS10 (N4 V25 silicon)          | OEM rebadges                                                                                                                       |
| `AKP05V25` / `AKP05EV25` / `AKP05RV25`        | ?               | AKP05 V25 / E V25 / R V25             | 2025-revision firmware on AKP05 silicon                                                                                            |
| `AKP05` / `AKP05R`                            | ?               | AJAZZ AKP05 / AKP05R                  | Pre-V25 firmware                                                                                                                   |
| `AR_G100`                                     | ?               | AR G100                               | OEM rebadge                                                                                                                        |
| `ControllerDeviceS3` / `CN003Device`          | ?               | OEM controller-class device           |                                                                                                                                    |
| `MBoxN4+` (registry key prefix)               | n/a             | (registry-only; not a model codename) | `MBoxN4+/SlideSwitchMode`, `MBoxN4/enableTouchSlide`, `MBoxN4/enableTouchClick` are user preferences for the touch strip behaviour |
| `BRHubN4`, `BRHubN4Pro`                       | ?               | BR Hub N4 / N4 Pro                    | Built-in USB hub variants                                                                                                          |

## 5. AKP03 / Mirabox N3 family (6 LCD keys + 3 side buttons + 3 encoders)

**Wire**: v2 (most firmware). v3 on AKP03R rev. 2. 512-byte packets in
v2, 1024 in v3. JPEG 60×60 `Rot0` (AKP03) or 64×64 `Rot90`
(AKP03R rev. 2). 3 rotary encoders + 3 non-LCD side buttons exposed
through the input report.

| Codename                                                | VID:PID                           | Model name              | Notes                                                                                                              |
| ------------------------------------------------------- | --------------------------------- | ----------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `AKP03`                                                 | `0x0300:0x1001`                   | AJAZZ AKP03             | Canonical per `[ajazz-sdk]`                                                                                        |
| `AKP03E`                                                | `0x0300:0x3002`                   | AJAZZ AKP03E            |                                                                                                                    |
| `AKP03R`                                                | `0x0300:0x1003`                   | AJAZZ AKP03R            |                                                                                                                    |
| `AKP03V25` / `AKP03EV25` / `AKP03RV25`                  | ?                                 | AKP03 V25 variants      | 2025 firmware revision                                                                                             |
| (PID `0x3001`)                                          | `0x0300:0x3001`                   | AKP03 legacy firmware   | Pre-V2 firmware; kept registered for backwards compat                                                              |
| (PID `0x3003`)                                          | `0x0300:0x3003`                   | AKP03R rev. 2           | v3 protocol — 1024-byte packets, 64×64 Rot90 images                                                                |
| (PID `0x3004`)                                          | `0x0300:0x3004`                   | "HOTSPOTEKUSB HID DEMO" | Development firmware sibling; surfaced via 2026-05-13 hot-plug capture                                             |
| `MBox-N3` / `MBox-N3 `                                  | `0x6602:0x1002` / `0x6603:0x1002` | Mirabox N3              | Two VID variants (old=0x6602, new=0x6603) per `[opendeck-akp03]`. Trailing-space variant exists in vendor strings. |
| `MBox-N3E` / `MBox-N3 E` / `MBox-N3E `                  | ?                                 | Mirabox N3E             |                                                                                                                    |
| `MBox-N3V25` / `MBox-N3V25E`                            | ?                                 | Mirabox N3 V25 firmware |                                                                                                                    |
| `MBox-N3 EV25`                                          | ?                                 | Mirabox N3E V25         |                                                                                                                    |
| `MBox N3` / `MBox N3V25` / `MBox N3 EV25` / `MBox N3 E` | ?                                 | "MBox N3" with space    | Marketing-name variants                                                                                            |
| `N3-R`                                                  | ?                                 | Mirabox N3R             |                                                                                                                    |
| `MSD-TWO` / `MSD-TWOV25` / `MSD_TWO`                    | ?                                 | Mirabox MSD-TWO         |                                                                                                                    |
| `VSDN3` / `VSD_N3`                                      | ?                                 | VSD N3                  |                                                                                                                    |
| `SD12N3V25`                                             | ?                                 | SD12 N3 V25             |                                                                                                                    |
| `OMNIDIALV25`                                           | ?                                 | OmniDial V25            |                                                                                                                    |
| `A3506A`                                                | ?                                 | A3506A                  | OEM                                                                                                                |
| `CN002`                                                 | ?                                 | CN002                   |                                                                                                                    |
| `TS16N3` / `TS16N3V25`                                  | ?                                 | TS16 N3 / V25           |                                                                                                                    |
| `SS-551`                                                | ?                                 | ShortCuts 551           |                                                                                                                    |
| `ShortCutsProKB`                                        | ?                                 | ShortCutsPro KB         |                                                                                                                    |
| `ControllerDeviceS2`                                    | ?                                 | OEM placeholder         |                                                                                                                    |

## 6. Mirabox N1 family (small format)

**Wire**: v2. 512-byte packets.

| Codename                                  | VID:PID | Model name       | Notes |
| ----------------------------------------- | ------- | ---------------- | ----- |
| `MBox-N1` / `MBox-N1E` / `MBox N1`        | ?       | Mirabox N1 / N1E |       |
| `ajazzN1` / `ajazzN1R` / `ajazzN1E`       | ?       | AJAZZ N1 / R / E |       |
| `SD16N1V25` / `VSDN1` / `VSD_N1` / `SD16` | ?       | SD16 N1 / VSD N1 |       |
| `Flux2`                                   | ?       | Flux2            |       |
| `ControllerDeviceS1`                      | ?       | OEM placeholder  |       |

## 7. Legacy 296/298 family (12-key)

**Wire**: v1 (firmware-specific, varies). 512-byte packets.

| Codename                                               | VID:PID | Model name            | Notes                                                 |
| ------------------------------------------------------ | ------- | --------------------- | ----------------------------------------------------- |
| `StreamDock[296]`                                      | ?       | StreamDock 296        | 12-key variant                                        |
| `StreamDock2961`                                       | ?       | StreamDock 296 v1     |                                                       |
| `StreamDock[298]` / `streamdock298` / `StreamDock2981` | ?       | StreamDock 298 family | 12-key                                                |
| `HSV298_BE` / `HSV298`                                 | ?       | HSV 298 (Belgian?)    |                                                       |
| `2961`                                                 | ?       | (alt codename)        |                                                       |
| `KB-1` (also appears in 153)                           | ?       | KB-1                  | Cross-family — KB-1 is recognised both as 153 and 298 |

## 8. Legacy 300 / 321D / 0108D / H3 (paged "page-magic" devices)

**Wire**: v1 with `STP` page-magic byte (akp05_vendor.md §3 row 200).
512-byte packets. These are the only families whose
`getFinishCommand` writes a `'!'/'"'/'#'` byte at offset 9.

| Codename                                                                         | VID:PID | Model name                | Notes                                                                                                                                                             |
| -------------------------------------------------------------------------------- | ------- | ------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `StreamDock-300` / `StreamDock300`                                               | ?       | StreamDock 300            | Sends `'D','C'` sentinel on close via `sendDisconnectCommand` (this is the only family that emits the disconnect packet, see `akp05_vendor.md` §2 table footnote) |
| `StreamDock[321D]` / `StreamDock321D`                                            | ?       | StreamDock 321D           | Page-magic on STP                                                                                                                                                 |
| `StreamDock[0108D]` / `StreamDock0108D` / `StreamDock0108DCHDevice` / `0108D_D3` | ?       | StreamDock 0108D          | Page-magic on STP                                                                                                                                                 |
| `StreamDockH3`                                                                   | ?       | StreamDock H3             | Page-magic on STP                                                                                                                                                 |
| `H1V3` / `H1V3-E`                                                                | ?       | H1V3 / H1V3-E             | 1-key (?) variant                                                                                                                                                 |
| `XF-A3501`                                                                       | ?       | XF A3501                  | Re-badge of 293                                                                                                                                                   |
| `H3`                                                                             | ?       | H3 (alias)                | Page-magic                                                                                                                                                        |
| `XF-CN001` / `CN001` (cross-family)                                              | ?       | CN001                     |                                                                                                                                                                   |
| `CN003`                                                                          | ?       | CN003 (also in N4 family) |                                                                                                                                                                   |
| `MSD_TWO` (legacy reference)                                                     | ?       | MSD TWO                   |                                                                                                                                                                   |
| `KB-1` (cross-family)                                                            | ?       | KB-1                      |                                                                                                                                                                   |

## 9. Miscellaneous / unclassified codenames found in binaries

These appear in the vendor strings but their family is ambiguous from
RE alone — most likely OEM/dev units. Listed for completeness so we
catch them in a hot-plug logging path:

```
HDKATOV          DUO87           DUO87Device      X106
ZX-5539          TS12P8          USB_293S         HSV293N6
CyberAxonD3      AICDevice       AIC.FW           dial07
SMP_V1_2_4       DR21            LT1102           DarkFlash
TR-VISION_HOME   BRHubN4         BRHubN4Pro       MOLA
ControllerDeviceS1 / S2 / S3   CN001 / CN002 / CN003
XFA3501          XFA3503AV3Device
```

`AIC.FW` is **not a device codename** — it is the firmware-image
magic header used by `FirmwareUpgradeTool.exe`
(see [`akp_dfu_protocol.md`](./akp_dfu_protocol.md)).

## 10. Comprehensive cross-reference table — per-SKU technical specs

Where we have public confirmation of the technical spec, we include
it; rows with `?` indicate the vendor recognises the codename but the
SKU has not yet been captured in our reference projects.

| Codename            | VID:PID                           | Proto | Transport  | Keys (LCD/non-LCD) | Encoders | Strip   | Per-key JPEG   | Brightness range | Packet size                  | Special features                                                                                |
| ------------------- | --------------------------------- | ----- | ---------- | ------------------ | -------- | ------- | -------------- | ---------------- | ---------------------------- | ----------------------------------------------------------------------------------------------- |
| `AKP153`            | `0x0300:0x1010`                   | v2    | hid        | 15/0               | 0        | —       | 85×85 Rot90+M  | 0..100           | 512                          | —                                                                                               |
| `AKP153` (V1 fw)    | `0x5548:0x6674`                   | v1    | hid        | 15/0               | 0        | —       | 85×85 Rot90+M  | 0..100           | 512                          | Probes `isOld293Version()`                                                                      |
| `AKP153R`           | `0x0300:0x1020`                   | v2    | hid        | 15/0               | 0        | —       | 85×85 Rot90+M  | 0..100           | 512                          | —                                                                                               |
| `AKP815`            | `0x5548:0x6672`                   | v2    | hid        | 15/0               | 0        | 854×480 | 100×100 Rot180 | 0..100           | 512                          | `MAI` whole-strip; logo upload via `LOG`                                                        |
| `AKP03`             | `0x0300:0x1001`                   | v2    | hid        | 6/3                | 3        | —       | 60×60 Rot0     | 0..100           | 512                          | Side buttons emit action bytes 0x25/0x30/0x31                                                   |
| `AKP03E`            | `0x0300:0x3002`                   | v2    | hid        | 6/3                | 3        | —       | 60×60 Rot0     | 0..100           | 512                          | —                                                                                               |
| `AKP03R`            | `0x0300:0x1003`                   | v2    | hid        | 6/3                | 3        | —       | 60×60 Rot0     | 0..100           | 512                          | —                                                                                               |
| `AKP03R rev. 2`     | `0x0300:0x3003`                   | v3    | hid        | 6/3                | 3        | —       | 64×64 Rot90    | 0..100           | 1024                         | Native release events                                                                           |
| `AKP05` / `MBox-N4` | `0x6603:0x1007`                   | v3    | hid+winusb | 10/0               | 4        | 800×480 | 85×85          | 0..100           | 1024 / 512 (current backend) | `DRA` partial-strip, `ENC` per-encoder LCD, touch tap/swipe/long-press, `ULEND` commit sentinel |
| `MBox-N3`           | `0x6602:0x1002` / `0x6603:0x1002` | v2    | hid        | 6/3                | 3        | —       | 60×60 Rot0     | 0..100           | 512                          | Same as AKP03                                                                                   |
| `MBox-N3EN`         | `0x6603:0x1003`                   | v2    | hid        | 6/3                | 3        | —       | 60×60 Rot0     | 0..100           | 512                          | —                                                                                               |
| `MBox-N4Pro`        | `0x6603:?`                        | v3    | hid+winusb | 10/0               | 4        | 800×480 | 85×85          | 0..100           | 1024                         | Per-key RGB; vibration motor; same wire as N4                                                   |

All other codenames in §§2–9 share the wire spec of the family
they're grouped under unless documented otherwise. Capture queue is
tracked in `TODO.md` → "Stream Dock SKU live captures".

## 11. Codename ↔ vendor-internal grouping (for future filtering)

Per the vendor's `setSettings` UI strings the codenames are bucketed
into 4 product lines:

| Bucket                   | Codename pattern                                                    | Notes                                       |
| ------------------------ | ------------------------------------------------------------------- | ------------------------------------------- |
| **Stream Dock 1**        | `*N1*`, `*0108D*`, `*N6*`, single-row strips                        | "Mini"                                      |
| **Stream Dock**          | `293*`, `295*`, `296*`, `298*`, `321*`, `H3`, `0108*`               | Original Stream Deck-class                  |
| **Stream Dock Pro/Knob** | `MBox-N3*`, `AKP03*`, `MSD-TWO*`, `N3-R`, `OMNIDIAL*`               | 6-key + encoders                            |
| **Stream Dock Plus**     | `MBox-N4*`, `AKP05*`, `MSDPRO`, `MSDNEO`, `MBox-N6`                 | 10-key + touch strip + encoders             |
| **Keyboard MKey**        | `K1Pro*`, `K-992`, `MK*`, `KB-*`, `MOLA`, `SANWA`, `TOS300`, `M18*` | Mechanical keyboards w/ Stream Dock LCD bar |
| **Other**                | OEM-only rebadges (`CN001`, `XF-*`, `DK01`, etc.)                   |                                             |

## 12. Registry per-device settings (vendor)

Each codename gets a key under `HKCU\Software\HotSpot\StreamDock\<codename>\`.
Common subkeys observed:

```
StreamDockCurrentDevice/Device      - currently selected device codename
StreamDockCurrentDevice/SerialNumber- serial of currently selected device
<codename>/<deviceId>/ActionState   - JSON action bindings
<codename>/<deviceId>/SerialNumber  - this device's serial
SkinCap/index                       - currently selected skin index
RotationAngle/angle                 - 0 / 90 / 180 / 270 device-rotation
ScreenOffTime/time                  - inactivity sleep timeout (sec)
ScreenDeviceState/ScreenDeviceState - current power state
ResetDataState/resetData            - "true" to force factory reset
```

Device-family-specific:

```
MBoxN4/enableTouchSlide             - enable swipe gestures
MBoxN4/enableTouchClick             - enable taps
MBoxN4+/SlideSwitchMode             - slide-to-switch behaviour
N4Pro/lightControlMode              - RGB control mode
N4Pro/enableVibration               - vibration motor on/off
```

These are preferences only; we do NOT need to mirror this exact
registry layout. Our equivalent should live under
`org.ajazz.control-center` per Qt6's `QStandardPaths::AppConfigLocation`.

## 13. Code corrections required

| File                                                             | Change                                                                                                                                                                                                                                              | Breaking?    | Tests needed                                                     |
| ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------ | ---------------------------------------------------------------- |
| `src/devices/streamdeck/src/register.cpp:138`                    | Add `(0x6603, 0x1007)` Mirabox N4 — **already present** at line 273-287. Re-verify with new findings; confirmed.                                                                                                                                    | n/a          | n/a                                                              |
| `src/devices/streamdeck/src/register.cpp:227`                    | Add `(0x6602, 0x1003)` for MBox-N3E (rev 1). Currently only `0x1002` is registered for the old vendor pair.                                                                                                                                         | additive     | `RegisterTest::miraboxN3E_oldVendor_0x1003_resolves`             |
| `src/devices/streamdeck/src/register.cpp:227`                    | Add codenames `AKP03V25`, `AKP03EV25`, `AKP03RV25` as descriptors pointing at the existing `AKP03` family — same wire protocol, just different vendor codename for log/UI purposes. PIDs unknown; register under existing PIDs with codename hints. | additive     | `RegisterTest::akp03V25_codename_branch`                         |
| `src/devices/streamdeck/src/register.cpp:270`                    | Add `MBox-N4Pro`, `MBox-N4ProE`, `MBox-N6` descriptors. Same backend factory `makeAkp05`. PIDs unknown — leave provisional and add hot-plug logging to capture.                                                                                     | additive     | `RegisterTest::miraboxN4Pro_resolves`                            |
| `src/devices/streamdeck/src/register.cpp` (end of registerAll()) | Add `AKP815` rebadge codename `TS183` — already shares factory.                                                                                                                                                                                     | additive     | `RegisterTest::ts183_codename_branch_to_akp815`                  |
| `src/devices/streamdeck/src/akp05_protocol.hpp:75`               | Change `PacketSize` from `512` to `1024` for v3 firmware. Gate at runtime via firmware-version probe (see akp05_init_sequence.md §3.2). Breaking change for the AKP05 backend; behind a feature flag for one release.                               | **breaking** | `Akp05ProtocolV3Test::usesLargerPackets_whenFirmwareV3`          |
| `src/devices/streamdeck/src/akp03_protocol.hpp:65`               | Same as above for AKP03R rev. 2 — change to 1024 when `_pid == 0x3003`.                                                                                                                                                                             | **breaking** | `Akp03Rev2Test::usesV3PacketSize`                                |
| `src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp` | Add `enum class ProtocolVersion { V1, V2, V3 }` and a `ProtocolVersion protocolVersion() const` on `IDevice`. Backends report based on `_pid + firmwareVersion`.                                                                                    | additive     | `IDeviceTest::protocolVersion_reflectsFirmware`                  |
| `docs/protocols/streamdeck/_research-sources.md`                 | Add a new citation tag `[vendor-strings-2026-05-17]` pointing at `C:\temp\sdlibrary1_strings.txt` and `C:\temp\streamdock_exe_strings.txt`.                                                                                                         | additive     | n/a                                                              |
| `src/devices/streamdeck/src/akp05.cpp` (touch-strip path)        | Implement `setSecondaryScreenImage(QRect, bytes)` using `buildSecondaryScreenHeader(...)` already prototyped in `akp05_protocol.hpp:223`. Wire it into the device interface mix-in.                                                                 | additive     | `Akp05TouchStripTest::partialUpdate_emitsDRA_thenJPEG_thenULEND` |

**Capture queue** (live-USB work, not code changes):

1. AKP05 / Mirabox N4 — partial-update touch-strip DRA byte sequence.
1. AKP03R rev. 2 — confirm 1024-byte packet size on v3 firmware.
1. MBox-N4 Pro — capture per-key RGB control packets.
1. MBox-N6 — confirm key count (6? or 10 with some non-LCD?).
1. K1Pro / Create_Pro keyboards — capture the LCD-bar protocol.
