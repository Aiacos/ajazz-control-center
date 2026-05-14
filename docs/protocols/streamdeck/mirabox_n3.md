# Mirabox N3 (rev. 1) — Stream Dock with 6 LCD keys + 3 encoders + 3 side buttons

> Mirabox-branded sibling of the AJAZZ AKP03. Same wire protocol, different
> USB identifier. This document is a thin cross-reference to
> [`akp03.md`](./akp03.md) — every protocol fact (framing, opcodes, image
> format, encoder actions, input report layout) is identical to the AJAZZ
> AKP03 because both devices ship the same firmware family.
>
> Phase 8 / DEVICES-04 promotion #2 (Tier 0 Scaffolded → Tier 2 Partial).
> AKP815 is promotion #1 (see [`akp815.md`](./akp815.md)).

## Identification

| Property     | Value                                                            |
| ------------ | ---------------------------------------------------------------- |
| Vendor ID    | `0x6602` (Mirabox V1 vendor ID per `[opendeck-akp03]`)           |
| Product ID   | `0x1002`                                                         |
| HID usage    | Vendor-defined, interface #0 — same as AKP03                     |
| Packet size  | 512 bytes (zero-padded), v1 API (same as `akp03`)                |
| Codename     | `mirabox_n3`                                                     |
| Backend      | `Akp03Device` (via `akp03_descriptor` factory in `register.cpp`) |
| Protocol doc | Defers to [`akp03.md`](./akp03.md) for all wire-level details    |

## Sibling SKUs in the same protocol family

`[opendeck-akp03]` enumerates the Mirabox-branded surface of the AKP03 protocol:

| Codename          | Marketing name      | VID:PID         | Backend       | Status                 |
| ----------------- | ------------------- | --------------- | ------------- | ---------------------- |
| `mirabox_n3`      | Mirabox N3 (rev. 1) | `0x6602:0x1002` | `Akp03Device` | **partial** (this doc) |
| `mirabox_n3_rev3` | Mirabox N3 (rev. 3) | `0x6603:0x1002` | `Akp03Device` | scaffolded             |
| `mirabox_n3en`    | Mirabox N3EN        | `0x6603:0x1003` | `Akp03Device` | scaffolded             |

The AJAZZ-branded `akp03` line is at:

| Codename      | Marketing name      | VID:PID         | Status     |
| ------------- | ------------------- | --------------- | ---------- |
| `akp03`       | AJAZZ AKP03         | `0x0300:0x1001` | functional |
| `akp03e`      | AJAZZ AKP03E        | `0x0300:0x3002` | functional |
| `akp03r`      | AJAZZ AKP03R        | `0x0300:0x1003` | functional |
| `akp03r_rev2` | AJAZZ AKP03R rev. 2 | `0x0300:0x3003` | scaffolded |

See [`akp03.md`](./akp03.md) for the full per-SKU table including the legacy
`0x0300:0x3001` and the `0x0300:0x3004` hot-plug capture variant.

## What "partial" means here

Per Phase 8 D-01's 5-tier vocabulary:

- **Scaffolded** — descriptor + factory exist; backend compiles but does not exercise the device.
- **Probed** — device enumerates and descriptor populated; no protocol writes confirmed.
- **Partial** — some features work end-to-end; advertised capability set incomplete or untested.
- **Functional** — all advertised capabilities work in practice; tested manually or in CI.
- **Verified** — `functional` + automated CI on real hardware OR sustained user-confirmed reliability.

The Mirabox N3 (rev. 1) is promoted to **partial** because:

1. The `Akp03Device` backend it shares with `akp03` is itself `functional` — the protocol is exercised end-to-end (key reads, image upload, brightness, encoders).
1. The N3 surface itself has not been independently captured on real Mirabox-branded hardware; the promotion reflects the inherited protocol coverage, not first-hand verification.
1. Real-hardware confirmation of the N3 (rev. 1) USB identifier (`0x6602:0x1002`) will promote to **functional** once a capture lands.

If a user reports that key input or image upload misbehaves on a real Mirabox N3 (rev. 1) compared to an AJAZZ AKP03, the divergence point will be either:

- Firmware revision differences (handled by separate codename entries —
  `mirabox_n3_rev3`, `mirabox_n3en` — for the newer Mirabox V2 vendor ID
  `0x6603` per the `[opendeck-akp03]` catalogue).
- The Mirabox V1 vendor's own firmware quirks (e.g., USB-suspend handling
  on bus reset) which we'd surface here.

Until such a divergence is observed and captured, this doc stays as a
cross-reference and Mirabox N3 inherits everything from
[`akp03.md`](./akp03.md).

## Backend wiring

The `Akp03Device` factory in `src/devices/streamdeck/src/register.cpp`
(line 223) registers the Mirabox N3 entry:

```cpp
auto d = akp03_descriptor(MiraboxN3VendorOld, 0x1002, "Mirabox N3 (rev. 1)", "mirabox_n3");
d.hasClock = true;  // Phase 5: symmetric IClockCapable coverage across all streamdeck variants.
registry.registerDevice(std::move(d), makeAkp03);
```

The `MiraboxN3VendorOld` constant is `0x6602` (per `[opendeck-akp03]`). The
`akp03_descriptor` helper produces a descriptor with `keyCount = 6`,
`gridColumns = 3`, `encoderCount = 3` — the AKP03 layout per Pitfall 9's
research correction (6 LCD keys + 3 side buttons + 3 pressable encoders,
not the pre-2026-05-14 "6 keys + 1 knob" undercount).

## See also

- [`akp03.md`](./akp03.md) — full protocol details, packet structure, opcodes, image format, encoder actions.
- [`_research-sources.md`](./_research-sources.md) — `[opendeck-akp03]` citation tag.
- [`akp815.md`](./akp815.md) — DEVICES-04 promotion #1.
- `docs/research/vendor-protocol-notes.md` Finding 16 — Stream Dock device catalogue reconciliation (full rebadge taxonomy).
- `register.cpp:223` (production wiring).
- `docs/_data/devices.yaml` — Mirabox N3 entry with `maturity: partial`.
