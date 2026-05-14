# AJAZZ AKP05 / AKP05E — Mirabox N4 / N5-family

> **"Stream Dock Plus"-class controller**: 10 LCD keys (2×5) + 4 endless
> rotary encoders + a horizontal touchscreen strip + USB hub.
>
> ⚠️ **In-tree gap (2026-05-14):** the current `akp05.cpp` models *15 keys
> (3×5) + 4 encoders + touch strip*. That is wrong — the AKP05 / N4 has
> only **10 LCD keys arranged 2 rows × 5 columns**. Tracking in `TODO.md`
> under "AKP05 layout reconciliation".

## Hardware

**Sources:** `[mirabox-n4]` `[opendeck-akp05]` `[companion]` `[ajazz-akp05e]` —
see [`_research-sources.md`](./_research-sources.md).

| Property             | Value                                                                                                                                                    |
| -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Form factor          | Desktop controller with built-in USB hub (Mirabox N4)                                                                                                    |
| Connection           | USB-C (1.5 m braided cable bundled)                                                                                                                      |
| Input voltage        | 5 V                                                                                                                                                      |
| **LCD keys**         | **10**, arranged in **2 rows × 5 columns** (≈ 14 × 14 mm each, dynamic GIF support)                                                                      |
| **Rotary encoders**  | **4** endless, 360°, pressable, aluminum-alloy caps                                                                                                      |
| **Touch strip**      | **LCD touchscreen** beneath the encoder row, **110 × 14 mm** (≈ 800 × 480 px panel area on the underlying LCD), 4 touch zones aligned to the 4 encoders  |
| Encoder LCD overlays | None — the per-encoder graphics are rendered inside the touch strip, not on separate LCDs (Stream-Deck-Plus-class)                                       |
| Built-in USB hub     | 2× USB-A (USB 2.0 FS) + 2× USB-C                                                                                                                         |
| Dimensions           | 137 × 125 × 37 mm                                                                                                                                        |
| Weight               | 398 g (bare) / 809 g (boxed)                                                                                                                             |
| Material             | Plastic body + acrylic LCD keycaps + aluminum encoder caps                                                                                               |
| Stand                | Adjustable, integrated                                                                                                                                   |
| OS support           | Windows ≥ 7, macOS ≥ 10.15, Linux (community via `[opendeck-akp05]`)                                                                                     |
| Vendor software      | Mirabox Stream Dock / Nexus, AJAZZ App (rebrand)                                                                                                         |
| Bundled features     | LCD button images and GIFs, knob property auto-display, swipe page-turn on touch strip, 300+ themes, 400+ plugins via Mirabox/AJAZZ "Space" plugin store |

### Variants

| Codename         | Marketing name               | VID          | PID          | Notes                                                                                                                           |
| ---------------- | ---------------------------- | ------------ | ------------ | ------------------------------------------------------------------------------------------------------------------------------- |
| `akp05`          | AJAZZ AKP05                  | unknown      | unknown      | The OpenDeck author of `[opendeck-akp05]` could not confirm the USB ID (no hardware sample). Likely shares a Mirabox vendor ID. |
| `akp05e`         | AJAZZ AKP05E (white / black) | unknown      | unknown      | Sold via `ajazzbrand.com` at USD 99.99. Believed firmware-identical to N4.                                                      |
| `akp05e_pro`     | AJAZZ AKP05E PRO             | unknown      | unknown      | Same form factor; "PRO" SKU mostly trims/material change.                                                                       |
| **`mirabox_n4`** | **Mirabox N4**               | **`0x6603`** | **`0x1007`** | The only known canonical USB ID. Confirmed via `[opendeck-akp05]/40-opendeck-akp05.rules`.                                      |

⚠️ The current `register.cpp` lists `0x0300:0x5001` for `akp05` — there is
**no public source** for that pair. It must have been a placeholder. Until
we capture an AKP05/AKP05E unit and learn its real VID:PID, keep the
placeholder registered but mark the entry `scaffolded` in `devices.yaml`.

## Layout

`[companion]`'s N4 diagram (recreated in ASCII):

```
+-----+-----+-----+-----+-----+
| K1  | K2  | K3  | K4  | K5  |    <- row 1 of 5 LCD keys
+-----+-----+-----+-----+-----+
| K6  | K7  | K8  | K9  | K10 |    <- row 2 of 5 LCD keys
+-----+-----+-----+-----+-----+
| ======== LCD touch strip ====|    <- 110 x 14 mm strip
+-----+-----+-----+-----+-----+
| E1  | E2  | E3  | E4  |     |    <- 4 endless rotary encoders, left-aligned in Companion grid
+-----+-----+-----+-----+-----+
```

Companion notes (which we must implement):

- The 4 encoders are **left-aligned** in the Companion grid even though
  the physical knobs are evenly spaced across the 5-column width.
- The **swipe gesture on the touch strip** is mapped to the **rotary
  actions of the fifth button in the third row** (a Companion convention,
  not a hardware quirk).
- The touch strip supports:
  - **Tap** on one of the four zones → trigger the action assigned to the
    encoder under the zone.
  - **Swipe left / right** → previous / next page.
- Encoders emit only a **press** event, never a release. Companion
  synthesises a press+release pair to keep the API uniform. We must do
  the same.

## Features that must work

| Feature                                                          | Required for `functional` | Required for `stable` |
| ---------------------------------------------------------------- | ------------------------- | --------------------- |
| Open / close transport                                           | ✅                        | ✅                    |
| Read 10 LCD key press / release events                           | ✅                        | ✅                    |
| Read 4 encoder rotation (CW / CCW) events                        | ✅                        | ✅                    |
| Read 4 encoder press events (synthesise release)                 | ✅                        | ✅                    |
| Read touch-strip tap events with zone index (0..3)               | ✅                        | ✅                    |
| Read touch-strip swipe left / right gestures                     | ✅                        | ✅                    |
| Set per-key JPEG image                                           | ✅                        | ✅                    |
| Set touch-strip image / GIF                                      | ✅                        | ✅                    |
| Set global brightness (0..100)                                   | ✅                        | ✅                    |
| Clear single / all keys                                          | ✅                        | ✅                    |
| Detect bundled USB hub child devices and pass through (no claim) | —                         | ✅                    |
| Set boot logo (854×480 JPEG suspected — to confirm)              | —                         | ✅                    |
| Animated GIFs on keys and strip                                  | —                         | nice-to-have          |

## Wire protocol

⚠️ **Unverified from a real capture.** All bytes in this section are
**hypothesised** based on the AKP03 protocol family + the public
description in `[opendeck-akp05]`. They are listed here so the
implementation has a starting point; every constant must be confirmed
against a USB capture before the device moves out of `scaffolded`.

### Framing

Same `CRT` prefix + 3-byte command word as the rest of the AKP family.
`[opendeck-akp05]` is a fork of `[opendeck-akp03]` and uses the same v2
(1024-byte) packet structure, so we expect the AKP05 to also be a v2-API
device. The legacy `akp05_protocol.hpp` assumes 512-byte packets — that
is almost certainly **wrong** and needs reconciliation with a capture.

### Tag byte at offset 9 (input reports)

| Tag range    | Meaning                            | Notes                                                            |
| ------------ | ---------------------------------- | ---------------------------------------------------------------- |
| `0x01..0x0A` | LCD key 1..10 press/release        | Mirroring AKP03 (1-based key index)                              |
| `0x25..0x2D` | Encoder press / release / rotation | Exact mapping unknown without capture                            |
| `0x30..0x3F` | Touch-strip gestures               | Lower nibble = gesture code (tap=0, swipe-left=1, swipe-right=2) |
| `0x00`       | NOP / keep-alive                   | Discard silently                                                 |

Suggested gesture-code mapping (from prior reverse-engineering of similar
Mirabox devices — to verify):

| Gesture code | Meaning     | Payload                                     |
| ------------ | ----------- | ------------------------------------------- |
| `0x0`        | Tap         | Bytes 10..11 = touch zone index (0..3)      |
| `0x1`        | Swipe left  | Bytes 10..11 = start-X (big-endian, 0..639) |
| `0x2`        | Swipe right | Bytes 10..11 = start-X (big-endian, 0..639) |
| `0x3`        | Long-press  | Bytes 10..11 = absolute X                   |

### Output reports (host → device)

| Command                                   | Bytes 5..7                         | Payload                                                      |
| ----------------------------------------- | ---------------------------------- | ------------------------------------------------------------ |
| `LIG` (brightness)                        | `0x4C 0x49 0x47`                   | byte 10 = percent                                            |
| `BAT` (key image)                         | `0x42 0x41 0x54`                   | size + 1-based key index                                     |
| **`ENC`** (encoder LCD area in the strip) | `0x45 0x4E 0x43`                   | size + 0-based encoder index — confirm vs `[opendeck-akp05]` |
| **`MAI`** (full-width touch strip)        | `0x4D 0x41 0x49`                   | size only (target is implicit)                               |
| `STP`, `CLE`, `LOG`, `HAN`                | shared with the rest of the family | unchanged                                                    |

These opcode words match what's in `src/devices/streamdeck/src/akp05_protocol.hpp`
but **are not yet attested in any third-party reference** — when the
real capture lands, they may turn out to be misnamed. Treat as
hypothetical until then.

### Image upload

Per `[opendeck-akp05]/[opendeck-akp03]` the image format for the N4 keys
should be very close to AKP03 (60×60 JPEG `Rot0`). For the touch strip we
expect a 800×480 JPEG split across the 4 encoder zones (`Rot0`). To
confirm with a capture.

## Edge cases and quirks

- The N4 ships with its own USB hub (2× USB-A + 2× USB-C). When the device
  is enumerated, the hub appears as a separate USB device and **must not
  be claimed by our backend** — only the HID interface matters.
- Encoder events have no release. The backend must synthesise it (see
  also AKP03, same issue).
- Touch-strip taps are per-zone (4 zones), but Companion left-aligns the
  encoders in its 5-column grid; we should expose the **zone index** in
  the event so consumers don't have to derive it from X.
- The "5th column third row" placement for swipe is a Companion
  convention, not a hardware fact — when we expose touch events we keep
  zone + gesture + X in the payload and let the UI decide where to wire
  swipe.
- Some product photos show a small "Hub LED" — it is decorative, not
  controllable.

## Cross-references

- `[opendeck-akp05]` — Stream-Deck-Plus-style touch-strip semantics, USB ID for N4
- `[ajazz-sdk]` — does *not* support AKP05; this device is outside its scope today
- `[companion]` — N4 layout, encoder release synthesis convention
- `[mirabox-n4]` — vendor product page (hub, dimensions, weight, materials)
- `[ajazz-akp05e]` — AJAZZ-branded SKU product page (white / black variants)
- [`akp03.md`](./akp03.md) — same v2 framing, smaller form factor
- [`akp815.md`](./akp815.md) — sister 15-key device (LCD strip but no touchscreen)
