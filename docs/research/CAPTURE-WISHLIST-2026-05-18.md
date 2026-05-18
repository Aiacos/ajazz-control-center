# Capture wishlist — 2026-05-18 snapshot

This is the operator-facing checklist of USB captures needed to unblock
the feature gaps that **cannot land from RE alone** — every entry below
requires actually triggering a vendor-app action while a Wireshark +
USBPcap (or usbipd + usbmon under WSL2) capture is running.

> **Capture mechanics**: follow [`docs/protocols/CAPTURING.md`](../protocols/CAPTURING.md)
> end-to-end. The Windows USBPcap path is §8; the WSL2 fallback for
> modern xHCI/USB4 hosts is §8.6. Raw `.pcap` files NEVER get committed —
> the pre-commit hook rejects them per CAPTURE-01 / Pitfall 17. The
> deliverable for each entry below is a sanitised hex-array header
> under `tests/integration/fixtures/<codename>/<label>.h` plus an
> entry in `.planning/research/captures/INDEX.md` with a SHA-256 of
> the original capture for provenance.

## How to use this list

For each entry, the columns mean:

- **Feature** — what the capture unblocks in the project (often an open
  GitHub issue or a `scaffolded` device promotion).
- **Vendor app** — which Windows binary to drive.
- **HID filter** — Wireshark display filter to narrow the dump to the
  relevant device + direction.
- **Trigger** — the exact UI action to perform once capture is recording.
- **Witness** — what to look for in the dump that confirms you got the
  right traffic (success criterion).
- **Fixture** — where the sanitised hex array should land in the repo.

Pick entries in priority order (P0 → P2). A single capture session can
cover multiple entries if their triggers are independent (e.g. one
8-min Stream Dock session covers VER + QUCMD + standby image).

______________________________________________________________________

## P0 — closes 3-4 scaffolded → functional promotions

### W-01 · AKP05 `VER` firmware-version response

| Field       | Value                                                                                                                     |
| ----------- | ------------------------------------------------------------------------------------------------------------------------- |
| Feature     | Replace `Akp05Device::firmwareVersion() -> "unknown"` stub (akp05.cpp:379) with real value                                |
| Vendor app  | Stream Dock AJAZZ (`Stream Dock AJAZZ.exe` PID family)                                                                    |
| HID filter  | `usb.idVendor == 0x0300 && usb.idProduct == 0x5001` (or `0x6603:0x1007` for Mirabox N4) + `usb.transfer_type == 0x01`     |
| Trigger     | Launch the vendor app cold (so it does the open-time `VER` probe), wait for the device-info tab to render                 |
| Witness     | One OUT report starting with `CRT VER` (0x43 0x52 0x54 0x56 0x45 0x52), followed by one IN report with the version string |
| Fixture     | `tests/integration/fixtures/akp05/ver_response.h` (just the IN response bytes, ~32-64 bytes typically)                    |
| Code follow | Implement `parseVersionResponse(span)` in `akp05.cpp`, cache + return from `firmwareVersion()`                            |

### W-02 · AKP05 `QUCMD` param catalog

| Field       | Value                                                                                                                                                                |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Feature     | Decode the 5-byte `QUCMD p1 p2 p3 p4 p5` multiplexer used for sleep / idle / rotation / knob-sensitivity (akp05_vendor.md §2 row 194)                                |
| Vendor app  | Stream Dock AJAZZ → Settings dialog                                                                                                                                  |
| HID filter  | Same as W-01                                                                                                                                                         |
| Trigger     | In Settings, toggle each setting ONE AT A TIME: sleep timer (Never / 1 min / 5 min / 30 min), screen rotation (90° / 180° / 270°), knob sensitivity (low / med / hi) |
| Witness     | A `CRT QUCMD <p1> <p2> <p3> <p4> <p5>` OUT report per UI change. Cross-tabulate `p1..p5` against the UI value to decode each param's meaning                         |
| Fixture     | `tests/integration/fixtures/akp05/qucmd_<param>_<value>.h` × ~12 (one per UI option)                                                                                 |
| Code follow | Add `buildQuCmd(p1, p2, p3, p4, p5)` + `IDeviceSettingsCapable` mixin; expose UI                                                                                     |

### W-03 · AK980 PRO wireless pairing flow

| Field       | Value                                                                                                                                                                                        |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Feature     | New `IWirelessPairingCapable` mixin — the vendor's "Pair new dongle" wizard (ak980pro_vendor.md §2.2 opcode 0x19)                                                                            |
| Vendor app  | AJAZZ Driver(R) → Wireless tab → "Pairing mode"                                                                                                                                              |
| HID filter  | `usb.idVendor == 0x0c45 && usb.idProduct == 0x8009` (wired path) + `usb.transfer_type == 0x01` AND separately capture the dongle (`0x0c45:0x7016`) to see if the dongle also receives a flow |
| Trigger     | Click "Pairing mode" in the vendor app, observe the 60-second timeout countdown                                                                                                              |
| Witness     | A 4-or-5-packet envelope on the wired path starting with `0x18 / 0x19 / DATA / 0x02 / 0xF0` (analogous to the lighting envelope). DATA byte 8 likely carries timeout in seconds              |
| Fixture     | `tests/integration/fixtures/ak980pro/pairing_start.h` + `pairing_cancel.h`                                                                                                                   |
| Code follow | Add `IWirelessPairingCapable::enterPairingMode(int timeoutSec)` to `ProprietaryKeyboard`                                                                                                     |

### W-04 · AKP05 swipe / haptic / standby image

| Field       | Value                                                                                                                                                                                                                |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Feature     | Cover three missing AKP05 features (akp05_vendor.md §6.2 + §7) in one session                                                                                                                                        |
| Vendor app  | Stream Dock AJAZZ                                                                                                                                                                                                    |
| HID filter  | Same as W-01                                                                                                                                                                                                         |
| Trigger     | (a) Swipe finger left + right on the touch strip · (b) press a key the vendor configured with "vibration" feedback · (c) trigger Windows lock screen and capture the standby image upload that fires on session-lock |
| Witness     | (a) IN reports with unique byte signatures — likely opcode `0x07` or `0x08` with direction encoded · (b) OUT report with no obvious LCD payload, likely opcode `0x0A`-ish · (c) DRA upload to a non-key location     |
| Fixture     | `tests/integration/fixtures/akp05/swipe_left.h`, `swipe_right.h`, `haptic_trigger.h`, `standby_image.h`                                                                                                              |
| Code follow | Extend `Akp05Device::poll()` event parser; new `IHapticCapable` mixin; standby-image hook in app session-lock handler                                                                                                |

______________________________________________________________________

## P1 — closes substantial functional gaps

### W-05 · AK980 PRO per-key custom RGB ramps (analog HE)

| Field       | Value                                                                                                                                                                                                      |
| ----------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Feature     | The vendor exposes per-key analog Hall-Effect actuation-point curves (separate from the proprietary RGB at ak980pro_vendor.md §13.4). Wire format completely undocumented in repo                          |
| Vendor app  | AJAZZ Driver(R) → Analog tab (only visible for AK980 PRO HE variant if your unit has the HE switches)                                                                                                      |
| HID filter  | `usb.idVendor == 0x0c45 && usb.idProduct == 0x8009`                                                                                                                                                        |
| Trigger     | Adjust the actuation curve for ONE specific key in the UI, click Apply; repeat for 3-4 distinct keys to get a parameterised sample                                                                         |
| Witness     | Multi-packet envelope NOT matching the existing 0x18/0x13/DATA/0x02/0xF0 lighting envelope. Likely a new opcode in the 0x30-0x40 range. The DATA packet probably carries `{ keyIdx, curve[8] }` or similar |
| Fixture     | `tests/integration/fixtures/ak980pro/he_curve_key<n>.h`                                                                                                                                                    |
| Code follow | New `IAnalogActuationCapable` mixin (only for HE variant); device-side capability advertised based on a firmware-version probe                                                                             |

### W-06 · Firmware OTA bootloader handoff (all 3 families)

| Field       | Value                                                                                                                                                                                                                                               |
| ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Feature     | Bridge to vendor firmware-update tooling (FirmwareUpdateTool.exe / OTAUpgrade gRPC service). Wire format incomplete in all 3 vendor docs (akp05_vendor.md §1.2 / ak980pro_vendor.md §1.1 / aj_series_opcode_table.md §3.13)                         |
| Vendor app  | FirmwareUpdateTool.exe (separate binary from the main apps)                                                                                                                                                                                         |
| HID filter  | All 3 VID:PID ranges (you'll do this 3 times, one per device family)                                                                                                                                                                                |
| Trigger     | (1) Launch FirmwareUpdateTool.exe with the device connected · (2) DO NOT actually click "Update" — just capture the **handshake** that puts the device into bootloader mode (the first 1-2 OUT reports). Cancel before any firmware bytes flow      |
| Witness     | A short OUT/IN exchange immediately after device select. The OUT often contains an ASCII magic like `DFU` / `OTA` / `BOOT`. IN response will be a fixed sentinel acknowledging the bootloader-mode entry                                            |
| Fixture     | `tests/integration/fixtures/<codename>/bootloader_handshake.h` × 3                                                                                                                                                                                  |
| Code follow | NEW `IFirmwareUpdateCapable` mixin returning `(currentVersion, latestAvailable, hasUpdate)`; UI hook ONLY shows "Check for updates" link that opens vendor's tool — we do NOT distribute firmware blobs (that's a vendor distribution-rights issue) |

### W-07 · AJ-series wireless dual/tri-mode toggle

| Field       | Value                                                                                                                                                                                                               |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Feature     | Vendor app exposes a 3-way switch (USB wired / 2.4G dongle / Bluetooth) per `aj_series_device_matrix.md §1`. Bluetooth marketing unconfirmed (Finding 8) — capture decides whether BT is real or a vendor-side stub |
| Vendor app  | AJ-series mouse software                                                                                                                                                                                            |
| HID filter  | `usb.idVendor == 0x3151 && usb.idProduct == 0x5007` (8K mouse) + `usb.idVendor == 0x248A && usb.idProduct == 0x5C2F` (2.4G dongle)                                                                                  |
| Trigger     | In the UI, toggle the connection mode from USB to 2.4G dongle (or vice versa); observe the OUT report that informs firmware                                                                                         |
| Witness     | A short OUT report on the wired path (NOT a flood — likely opcode `0x53` byte X or a dedicated 1-byte settings opcode). If a separate OUT lands on the dongle, you have Bluetooth confirmation                      |
| Fixture     | `tests/integration/fixtures/aj_series/mode_usb_to_24g.h`, `mode_24g_to_usb.h`, (if BT exists) `mode_bt.h`                                                                                                           |
| Code follow | Extend the existing `MouseSettings` struct with a `connectionMode` enum field; if BT capture shows zero traffic, document Bluetooth as VENDOR-SIDE-ONLY (no firmware support) and remove the marketing claim        |

______________________________________________________________________

## P2 — nice-to-haves, vendor parity polish

### W-08 · AKP815 + Mirabox N3 promotion

| Field       | Value                                                                                                                                                                    |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Feature     | These two devices are in our catalog as `scaffolded` / `partial` but we have no first-party captures. Vendor apps' image-upload byte streams for these specific PIDs     |
| Vendor app  | Stream Dock AJAZZ (handles both)                                                                                                                                         |
| HID filter  | AKP815: `usb.idVendor == 0x5548 && usb.idProduct == 0x6672` · Mirabox N3 rev. 1: `usb.idVendor == 0x6602 && usb.idProduct == 0x1002`                                     |
| Trigger     | (a) Push an image to a key · (b) toggle brightness · (c) for AKP815 also push a 800×480 LCD strip image                                                                  |
| Witness     | Per-key JPEG upload at expected dimensions (100×100 AKP815, 60×60 Mirabox N3); confirms or refutes the protocol-inheritance assumption already encoded in `register.cpp` |
| Fixture     | `tests/integration/fixtures/akp815/image_upload.h` + `mirabox_n3_v1/image_upload.h`                                                                                      |
| Code follow | Promote `akp815.maturity` and `mirabox_n3.maturity` from current tier (typically `partial` → `functional`) in `docs/_data/devices.yaml`                                  |

### W-09 · AK980 PRO TFT GIF multi-frame envelope

| Field       | Value                                                                                                                                                                                                                                  |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Feature     | We ship the still-image chunked path (commit `67f5fea`, opcode `0x7F`). The vendor also supports GIF89a multi-frame uploads up to 140 frames per `ak980pro_tft_protocol.md §3.1`. Per-frame delay encoding documented but not verified |
| Vendor app  | AJAZZ Driver(R) → TFT tab → "Upload animation"                                                                                                                                                                                         |
| HID filter  | `usb.idVendor == 0x0c45 && usb.idProduct == 0x8009`                                                                                                                                                                                    |
| Trigger     | Upload a known small GIF (e.g. 4 frames, 100ms each)                                                                                                                                                                                   |
| Witness     | A long burst of \`0x7F 0x03 / 0x80                                                                                                                                                                                                     |
| Fixture     | `tests/integration/fixtures/ak980pro/tft_gif_4frames.h` (just the header packet, NOT the ~9000 chunk packets)                                                                                                                          |
| Code follow | Extend `ITftDisplayCapable` with `uploadTftAnimation(QVector<QImage> frames, std::vector<int> delaysMs)` — reuse the chunked send path                                                                                                 |

### W-10 · AKP05 bulk image path (`M_V` 1024-byte variant)

| Field       | Value                                                                                                                                                                                                                           |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Feature     | Per akp05_vendor.md §2 row 191, when `location == 0x12` in a DRA header the vendor routes through a separate `M_V` 3-byte opcode + 1024-byte payload shape. Our `setSecondaryScreenImage` explicitly rejects `location == 0x12` |
| Vendor app  | Stream Dock AJAZZ                                                                                                                                                                                                               |
| HID filter  | Same as W-01                                                                                                                                                                                                                    |
| Trigger     | A vendor-app feature that uploads to the "wider" touch-strip overlay. Likely the Streamlabs / Discord widget                                                                                                                    |
| Witness     | An OUT report that doesn't start with `CRT` — bytes 0..2 = "M_V" (`0x4D 0x5F 0x56`) followed by 1024-byte payload                                                                                                               |
| Fixture     | `tests/integration/fixtures/akp05/mv_payload.h`                                                                                                                                                                                 |
| Code follow | Allow `location == 0x12` in `setSecondaryScreenImage`, dispatch to a new `buildMvPayload(span<1024>)` builder                                                                                                                   |

______________________________________________________________________

## Capture-session planning

If you have ~2 hours and one device, do these together:

- **Session A — Stream Dock (1 hr)**: W-01 + W-02 + W-04 + W-10 = 4 entries in one capture window. Drive every option of every Settings UI control. Total ~10 OUT reports expected.
- **Session B — AK980 PRO (45 min)**: W-03 + W-05 (if HE variant) + W-09 = 3 entries.
- **Session C — AJ-series mouse (30 min)**: W-07 only (the rest of the AJ-series wire surface is already captured via `aj_series_opcode_table.md`).
- **Session D — FirmwareUpdateTool (15 min × 3 devices)**: W-06 — just the handshake exchange, never click Update.

## What lands when captures come back

Per CLAUDE.md hard rule, raw `.pcap` files NEVER get committed. The
workflow is:

1. Run capture → save raw to `.planning/research/captures/scratch/`
   (gitignored)
1. Run `scripts/hex-to-cpparray.py` on each isolated frame → produces
   the per-fixture `.h` file
1. SHA-256 the raw capture, log under
   `.planning/research/captures/INDEX.md`
1. Commit ONLY the `.h` files + INDEX.md entry; delete the scratch
   `.pcap`
1. Land the code-side change against the new fixture; bump the device's
   `maturity` tier in `docs/_data/devices.yaml` if the three-witness
   rule (capture + observable state change + negative test) is satisfied

______________________________________________________________________

*Snapshot date: 2026-05-18. Refresh after each capture session by
deleting closed entries; the residue is the next pass's wishlist.*
