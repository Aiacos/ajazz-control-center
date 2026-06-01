# AJAZZ AKP05 / AKP05E — Mirabox N4 / N5-family

> **"Stream Dock Plus"-class controller**: 10 LCD keys (2×5) + 4 endless
> rotary encoders + a horizontal touchscreen strip + USB hub.

## ✅ Hardware-confirmed render model (live AKP05E `0x0300:0x3004`, 2026-05-31)

> **This section is the source of truth and supersedes the hypothesised
> "Wire protocol" / "Image upload" sections further down** (kept for history,
> annotated). Confirmed on a live `0x0300:0x3004` "HOTSPOTEKUSB HID DEMO" unit
> (fw `V3.AKP05E.01.007`) by driving the panel and reading off the result; the
> matching code is `src/devices/streamdeck/src/akp05.cpp`.

**Every display surface is addressed through the single `BAT` opcode** (`0x42 0x41 0x54`), distinguished only by the *wire byte* at packet offset 12 (offset
13 on the wire after the POSIX `0x00` report-id prepend). The vendor `ENC`,
`MAI`, and `DRA` opcodes **render nothing on this firmware** — do not use them.

| Wire byte (BAT offset 12) | Physical surface                                     | Image size  | Rotation   |
| ------------------------- | ---------------------------------------------------- | ----------- | ---------- |
| `1..4`                    | the 4 touch-strip zones (aligned to encoders E1..E4) | **192×128** | **Rot180** |
| `5`                       | **no visible surface** — do not use                  | —           | —          |
| `6..10`                   | bottom-row keys K6..K10                              | **112×112** | **Rot180** |
| `11..15`                  | top-row keys K1..K5                                  | **112×112** | **Rot180** |

> **Key size = 112×112, uniform for all 10 keys** (was wrongly 85 here, then
> briefly 120). Confirmed two ways on `0x0300:0x3004` (2026-06-01): (1) the
> authoritative `ambiso/opendeck-akp05` `mappings.rs:166` uses a uniform 112×112
> Rot180 for every protocol-v3 key (AKP05/05E/N4), with no per-key sizing; (2) a
> byte-identical 112×112 buffer painted to all 10 wire bytes fills every key 1:1
> (`scripts/akp05_key_margin_probe.py`). A 120 px image overflows the 112 LCD and
> the firmware's 112-stride framebuffer skews each row, which *looks* like a
> per-key right-margin — that was a stale 120 px build, not a device quirk.
>
> **Touch-zone size = 192×128**, hardware-measured 2026-06-01 with the border
> width-sweep (`akp05_key_margin_probe.py --zsweep`): the strip is one wide LCD
> with a ~192 px zone pitch and the zone fills the full 128 px height. This
> supersedes the earlier eyeballed ~128×128 (too narrow → visible gaps between
> zones) **and** opendeck-akp05 `mappings.rs:174` (176×112) — opendeck's 112 height
> does not fill on real hardware, so its zone values are approximate and the
> measurement wins (same hardware-over-RE rule as the 112 key size). Pitch is ±8 px
> (photo-measured); retail N4 should match (same strip) but re-confirm if on hand.

- **Orientation:** the panel mounts every LCD inverted, so each image is
  **pre-rotated 180°** before encoding (`akp05KeyTransform` / `akp05EncoderTransform`
  both `rotationDegrees=180`). Key *order* is unaffected — rotation is per-image.
- **Logical→wire key map** (`akp05KeyWire`, commit `037bd8d`): logical key
  1..5 → wire 11..15 (top row), 6..10 → wire 6..10 (bottom row).
- **The 4 strip zones ARE the encoder displays** — there is no separate encoder
  LCD. `IEncoderCapable::setEncoderImage(idx 0..3)` renders to BAT wire byte
  `idx+1` (commit `cb00677`). Zone is **192×128** (zone pitch ~192 px; 128 px
  leaves gaps, hardware-measured 2026-06-01 — see the size note up top).
- **Upload framing:** `BAT` header → JPEG payload in 1024-byte chunks → `ULEND`
  commit sentinel. `ULEND` at buffer offset `5..9` (`buildUploadFinished`) is
  hardware-accepted; mirajazz's offset-3 form also works (device is lenient).
- **Brightness:** `LIG` (`0x4C 0x49 0x47`), percent at byte 10 — works.

**Do NOT send `CRT DIS` at open().** It wedges the display: the app
opens→closes(`STP`)→reopens the device at startup, and a `DIS,STP,DIS` churn
leaves the panel backlit-but-black until a physical replug. (`open()` sends only
the `VER` probe; the service owns `LIG`.)

**Stuck-display recovery = physical replug.** On systemd ≥258, a USB
re-enumeration storm can leave the display controller wedged (backlight on,
image layer dead) and drop the `uaccess` ACL; `udevadm trigger` does NOT fix it.
A real unplug/replug does. This is distinct from any software bug — re-run the
known-good `scripts/akp05_color_probe.py` to tell device-state from a code fault.

**Input (key/encoder/touch) is unreachable on this `0x3004` demo unit** —
see [`akp05_input_corrections.md` §7.1](./akp05_input_corrections.md). Output
(above) works fully.

______________________________________________________________________

> ⚠️ **Historical note (2026-05-14):** the sections below were written before
> any live hardware and contain hypotheses now corrected by the model above
> (the "15 keys" gap is fixed; VID:PID is `0x0300:0x3004`; the strip is BAT
> wire 1..4, NOT `ENC`/`MAI`; images are 85×85 Rot180, NOT 60×60 Rot0). Kept
> for provenance; trust the confirmed model when they disagree.

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

| Codename         | Marketing name               | VID          | PID          | Notes                                                                                                                                                         |
| ---------------- | ---------------------------- | ------------ | ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `akp05`          | AJAZZ AKP05                  | `0x0300`     | `0x5001`     | Provisional AJAZZ-branded PID per `[opendeck-akp05]`; no live sample confirmed.                                                                               |
| **`akp05e`**     | AJAZZ AKP05E (white / black) | **`0x0300`** | **`0x3004`** | **Confirmed live 2026-05-21+** (fw `V3.AKP05E.01.007`) on a "HOTSPOTEKUSB HID DEMO" white-label unit. Output fully works; input unreachable on this demo SKU. |
| `akp05e_pro`     | AJAZZ AKP05E PRO             | unknown      | unknown      | Same form factor; "PRO" SKU mostly trims/material change.                                                                                                     |
| **`mirabox_n4`** | **Mirabox N4**               | **`0x6603`** | **`0x1007`** | The only known canonical USB ID. Confirmed via `[opendeck-akp05]/40-opendeck-akp05.rules`.                                                                    |

ℹ️ `register.cpp` registers `0x0300:0x3004` (AKP05E, **live-confirmed**),
`0x6603:0x1007` (Mirabox N4), and `0x0300:0x5001` (provisional AKP05). The
`0x3004` entry is the one validated against physical hardware in this repo.

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

> ❌ **CORRECTED — see the confirmed render model at the top.** On the live
> `0x3004` firmware, `ENC` and `MAI` render **nothing**; the strip zones use
> `BAT` at wire bytes 1..4. The table below is the original hypothesis.

| Command                       | Bytes 5..7                         | Payload                                          |
| ----------------------------- | ---------------------------------- | ------------------------------------------------ |
| `LIG` (brightness)            | `0x4C 0x49 0x47`                   | byte 10 = percent                                |
| `BAT` (key image)             | `0x42 0x41 0x54`                   | size + wire byte (1..4 strip zones, 6..15 keys)  |
| ~~`ENC`~~ (blank on `0x3004`) | `0x45 0x4E 0x43`                   | unused — strip zones use `BAT` wire 1..4 instead |
| ~~`MAI`~~ (blank on `0x3004`) | `0x4D 0x41 0x49`                   | unused — no whole-strip surface on this firmware |
| `STP`, `CLE`, `LOG`, `HAN`    | shared with the rest of the family | unchanged                                        |

### Image upload

> ❌ **CORRECTED — see the confirmed render model at the top.** Live `0x3004`:
> keys are **112×112 JPEG `Rot180`** (NOT 60×60 Rot0, NOT the 85 once recorded
> here); the strip is **4 zones of 192×128 `Rot180`** (hardware-measured 2026-06-01,
> supersedes both the eyeballed 128 and opendeck's 176×112 — see top), each
> addressed by its own `BAT` wire byte
> (1..4) — NOT a single 800×480 split. The original `[opendeck-akp05]` guess below
> was wrong on both size and orientation.

Per `[opendeck-akp05]/[opendeck-akp03]` the image format for the N4 keys
was guessed to be close to AKP03 (60×60 JPEG `Rot0`), with the touch strip a
800×480 JPEG split across the 4 encoder zones (`Rot0`). The live unit disproved
this — see the confirmed render model.

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

## Time sync

**Status:** scaffolded — not yet implemented.

`Akp05Device` inherits `IClockCapable` and returns
`TimeSyncResult::NotImplemented` from `setTime()`, with a WARN-once via
`s_warned_akp05` (Pitfall 14). See
[`docs/superpowers/specs/2026-05-13-time-sync-design.md`](../../superpowers/specs/2026-05-13-time-sync-design.md)
for the design contract.
