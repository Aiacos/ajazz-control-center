# Stream Dock — Research Sources & Methodology

This document is the **single index of third-party references** used to spec
the four AKP-class Stream Dock backends in this directory. It exists so that
every fact in `akp153.md`, `akp03.md`, `akp05.md` and `akp815.md` can be
traced to a public source — none of those sources have been re-distributed
or vendored. We only read them, summarised the protocol shape, and
re-implemented from the summary (clean-room policy in
[`docs/research/README.md`](../../research/README.md)).

This file is informational and never overwritten by the docs-generator.

## Citation conventions

When a per-device protocol document states a fact (geometry, USB identifier,
opcode byte, image format…) it must cite one of the entries below using the
short tag in square brackets:

- `[ajazz-sdk]` — `mishamyrt/ajazz-sdk` Rust crate (Stream Dock device catalogue)
- `[opendeck-akp03]` — `4ndv/opendeck-akp03` OpenDeck plugin (N3 family + rebadges)
- `[opendeck-akp05]` — `naerschhersch/opendeck-akp05` OpenDeck plugin (Mirabox N4)
- `[mirajazz]` — `4ndv/mirajazz` Rust crate (shared low-level library, protocol versions)
- `[elgato-rs]` — `OpenActionAPI/rust-elgato-streamdeck` (precursor to mirajazz; v0.10.2 still has Mirabox/Ajazz support)
- `[pyajazz]` — `superdeee/pyajazz` Python module (AKP153E with the 18-position layout)
- `[ajazz-akp03e-py]` — `tomekceszke/ajazz-akp03e` Python SDK (N3 layout + knob mapping)
- `[uriziel-akp153]` — `Uriziel01/Ajazz-AKP153-reverse-engineering` (raw byte captures)
- `[zcube]` — ZCube gist with original AKP153 reverse-engineering notes
- `[companion]` — Bitfocus Companion user guide for Mirabox Stream Dock
- `[mirabox-n3]`, `[mirabox-n4]`, `[ajazz-akp05e]`, `[ajazz-akp153e]` — vendor product pages
- `[capture-2026-05-13]` — internal USB hot-plug capture log (referenced from `TODO.md`)

Any unattributed fact in the wire-protocol tables is one we measured
ourselves during development; protocol headers in `src/devices/streamdeck/`
list those measurements next to the constants they verify.

## Sources

### Primary: clean-room references

#### `[ajazz-sdk]` — `mishamyrt/ajazz-sdk`

- URL: <https://github.com/mishamyrt/ajazz-sdk>
- License: MPL-2.0
- Last consulted: 2026-05-14
- Why it matters: This is the **most complete public catalogue** of AJAZZ
  Stream Dock USB identifiers and geometry. It is a fork of
  `OpenActionAPI/rust-elgato-streamdeck` extended with AKP153/AKP153E/AKP153R,
  AKP815, AKP03/AKP03E/AKP03R/AKP03R-Rev2.
- Files we read (no copy-paste):
  - `src/info.rs` — `Kind` enum, `key_count`, `row_count`, `column_count`,
    `encoder_count`, `lcd_strip_size`, `boot_logo_size`, `key_image_format`,
    `logo_image_format`, `is_v1_api`/`is_v2_api`. Used to cross-check our
    geometry constants in `src/devices/streamdeck/src/*_protocol.hpp`.
  - `src/protocol/codes.rs` — `VENDOR_ID_MIRABOX_V1` (`0x5548`),
    `VENDOR_ID_MIRABOX_V2` (`0x0300`), all `PID_AJAZZ_AKP*` constants, the
    opcode words (`CRT/LIG/BAT/STP/CLE/DIS/LOG/HAN/CONNECT`), the action
    codes for the AKP03's 3 encoders and 3 non-LCD buttons.
  - `40-mirabox.rules` — full canonical udev rule set across both vendor IDs.

#### `[opendeck-akp03]` — `4ndv/opendeck-akp03`

- URL: <https://github.com/4ndv/opendeck-akp03>
- License: GPL-3.0-or-later (same as this project — compatible)
- Last consulted: 2026-05-14
- Why it matters: covers the full AKP03 / Mirabox N3 family **including
  rebadges** that `ajazz-sdk` does not track:
  - Mirabox N3 (`0x6602:0x1002` and `0x6603:0x1002`)
  - Mirabox N3EN (`0x6603:0x1003`)
  - Soomfon Stream Controller SE (`0x1500:0x3001`)
  - Mars Gaming MSD-TWO (`0x0B00:0x1001`)
  - TreasLin N3 (`0x5548:0x1001`)
  - Redragon Skyrider SS-551 (`0x0200:0x2000`)
- The PID overlap with the AJAZZ-branded units (`0x0300:0x1001..0x3003`)
  confirms our 0x3004 capture is most likely an AKP03 sibling.

#### `[opendeck-akp05]` — `naerschhersch/opendeck-akp05`

- URL: <https://github.com/naerschhersch/opendeck-akp05> (fork
  `castropaliza/opendeck-akp05` is identical at commit time)
- License: GPL-3.0-or-later
- Last consulted: 2026-05-14
- Why it matters: only public reverse-engineering of the **AKP05 / Mirabox
  N4** Stream-Dock-Plus-class controller. Confirms the **2×5 LCD grid + 4
  rotary encoders + LCD touchscreen strip (110×14 mm, 4 touch zones) + USB
  hub** layout. The N4 USB identifier `0x6603:0x1007` is recorded in
  `40-opendeck-akp05.rules`; the AJAZZ-branded AKP05's USB ID is **not yet
  public** (the author did not have hardware).
- Architecture note: the touchscreen behaves like Elgato Stream Deck Plus —
  4 zones (one per encoder), absolute X for swipes, taps trigger
  per-zone actions, swipes left/right change pages.

#### `[mirajazz]` — `4ndv/mirajazz`

- URL: <https://github.com/4ndv/mirajazz>

- License: MPL-2.0

- Last consulted: 2026-05-14

- Why it matters: the **low-level abstraction layer** that both
  `[opendeck-akp03]` and `[opendeck-akp05]` build on. Crucially defines
  four **protocol versions** that drive feature gating:

  | Version | Packet | Press/release        | Serial                   | Example devices                                |
  | ------: | -----: | -------------------- | ------------------------ | ---------------------------------------------- |
  |       0 |    512 | press-only           | missing                  | Soomfon rebadge of 293S                        |
  |       1 |    512 | press-only           | hardcoded `355499441494` | AKP153 + 293S                                  |
  |       2 |   1024 | press-only           | unique                   | AKP03, Mirabox N3                              |
  |       3 |   1024 | full press + release | unique                   | AKP03 rev.2 (PIDs starting with 3), Mirabox N4 |

  This is the canonical taxonomy our backend should converge to; the
  `is_v1_api` / `is_v2_api` split in `[ajazz-sdk]` is a subset of it.

- Hardcoded quirks worth replicating: missing-serial → hardcoded value,
  broken Windows serial reporting on protocol v1, GIF support gated on v3.

#### `[elgato-rs]` — `OpenActionAPI/rust-elgato-streamdeck`

- URL: <https://github.com/OpenActionAPI/rust-elgato-streamdeck>
- License: MPL-2.0
- Last consulted: 2026-05-14
- Why it matters: **upstream** of both `[mirajazz]` and `[ajazz-sdk]`.
  Mirabox/Ajazz support was removed in v0.11; **v0.10.2 still contains
  the canonical AKP153 / AKP03 wire-format implementations**. The PR #22
  (≈ 2024-09 timeframe) that first added the AKP153 is a useful
  reference for the original ZCube-aligned protocol.

#### `[pyajazz]` — `superdeee/pyajazz`

- URL: <https://github.com/superdeee/pyajazz> (PyPI: `pyajazz`)

- License: MIT

- Last consulted: 2026-05-14

- Why it matters: independent **Python implementation** of the AKP153
  protocol. Confirms three details not visible elsewhere:

  - **18-position layout** (15 buttons + 3 decorative icon slots) matches
    `[ajazz-sdk]/Kind::Akp153::key_count == 15 + 3`. The position grid
    cited in the README is 3 rows × 6 cols, with positions 16/17/18 in
    the last column being the non-interactive logos.
  - **Display rotation**: "The AKP153 display controller expects the
    screen to be rotated 90° clockwise relative to its physical
    orientation. Therefore, images must be rotated 90° counter-clockwise
    before sending to the device" — matches `key_image_format::Rot90`.
  - **Bricking risk**: setting a logo image with extensive metadata
    (EXIF) may permanently hang the device. Our image pipeline must
    strip metadata before upload (security finding worth adding to
    the TODO backlog as SEC-013).

#### `[ajazz-akp03e-py]` — `tomekceszke/ajazz-akp03e`

- URL: <https://github.com/tomekceszke/ajazz-akp03e>
- License: MIT
- Last consulted: 2026-05-14
- Why it matters: independent **Python reference for the AKP03E** with
  an explicit ASCII layout diagram that places the **3 encoders at the
  TOP**, the 6 LCD keys in the middle (2 rows × 3 cols), and the **3
  side buttons at the BOTTOM**. The earlier description in `akp03.md`
  ("3 buttons underneath") was correct in spirit but did not specify
  the physical order. Also confirms knob 0=left, 1=middle/top,
  2=right — the same ordering implied by the `0x33/0x35/0x34` action
  code mapping in `[ajazz-sdk]`.

#### `[uriziel-akp153]` — `Uriziel01/Ajazz-AKP153-reverse-engineering`

- URL: <https://github.com/Uriziel01/Ajazz-AKP153-reverse-engineering>

- License: not stated (read-only reference, no code copied)

- Last consulted: 2026-05-14

- Why it matters: contains **raw byte captures** of the `BAT` image
  upload sequence:

  ```
  43 52 54 00 00 42 41 54 00 00 08 7C 0D 00 00 00   <- sent before image
  43 52 54 00 00 53 54 50 00 00 00 00 00 00 00 00   <- sent after image (STP)
  ```

  Confirms our `buildImageHeader` byte layout and that the final
  flush is a plain `STP` packet (no extra trailing payload).

#### `[zcube]` — ZCube AKP153 protocol gist

- URL: <https://gist.github.com/ZCube/430fab6039899eaa0e18367f60d36b3c>
- License: public gist, no explicit licence — used as reading reference,
  never copied.
- Why it matters: first published byte-level description of the AKP153 wire
  protocol. Already cited in `akp153.md`.

### Secondary: vendor / community

#### `[companion]` — Bitfocus Companion

- URL: <https://companion.free/user-guide/v4.2/surfaces/mirabox-streamdock/>
- Last consulted: 2026-05-14
- Why it matters: vendor-independent product-management description of the
  three Mirabox SKUs (293V3 / N3 / N4). Confirms that:
  - **N3** has *3 LCD keys × 2 rows + 3 non-LCD buttons underneath + 3
    rotary encoders* — which contradicts the older "6 keys + 1 knob"
    description in our `akp03.cpp`.
  - **N4** has *5 LCD keys × 2 rows + 4 rotary encoders + LCD strip* (this
    is the actual Stream-Dock-Plus class, not our current AKP05 binding).
  - Some controls fire only press *or* release events, not both, and
    Companion synthesises the missing edge — a constraint we must mirror.

#### `[mirabox-n3]`, `[mirabox-n4]`, `[ajazz-akp05e]`, `[ajazz-akp153e]` — vendor product pages

- <https://mirabox.net/products/mirabox-n3-stream-deck>
- <https://mirabox.net/products/mbox-n4>
- <https://ajazzbrand.com/products/ajazz-akp05e-desk-controller-all-in-one-streaming-media-controller>
- <https://ajazzbrand.com/products/ajazz-akp153-desk-controller>
- <https://shine-tone.com/products/mirabox-stream-dock-n4>

These give the **physical specs** (weight, dimensions, key/screen sizes,
materials, port count) that the protocol layer cannot tell us. They are
the authoritative source for the "Hardware" sections of each protocol
document.

### Internal

#### `[capture-2026-05-13]`

Hot-plug USB capture taken 2026-05-13 against a `0300:3004` "Ajazz
HOTSPOTEKUSB HID DEMO" descriptor. The capture surfaced through the
device-watcher fast path and is referenced from `register.cpp:97-103` and
the `Streamdock 0x0300:0x3004 SKU identification` entry in `TODO.md`.

The capture itself is not committed (binary). A future contributor with the
hardware should annotate it and cite this tag.

## Re-implementation rules

For every fact we cite from one of the references above:

1. We summarise it in **our words** (no direct copy).
1. The constant or routine in our source code carries a comment with the
   citation tag (e.g. `// [ajazz-sdk] info.rs::Kind::Akp815`).
1. We **never** vendor third-party files into this repository, even under a
   compatible licence — those files belong with their upstreams and are
   pulled into reading on a per-task basis.

When in doubt, ask in `docs/research/README.md` before adding a new source.
