# Firmware updates — architecture + legal posture

This document records the design decision for how AJAZZ Control Center
exposes firmware-update functionality to users. Status: **DECIDED
2026-05-18**, implementation **deferred** pending the W-06 capture in
[`docs/research/CAPTURE-WISHLIST-2026-05-18.md`](../research/CAPTURE-WISHLIST-2026-05-18.md).

## Decision

**We do not ship vendor firmware blobs. We do not perform the flash.
We display the running firmware version and deep-link to the vendor's
official update tool.**

Pattern adopted: the **OpenRGB / OpenDeck / OpenRazer** posture (FOSS
peripheral controllers that explicitly do not redistribute vendor
firmware), augmented with the **QMK Configurator** detail of clearly
separating "configure" from "flash" so the user always knows which
tool is responsible for the write.

The opposite pattern — bundling firmware blobs + driving the flash
in-app (Razer Synapse, Logitech G HUB, ZSA Wally) — is rejected for
the reasons in [§Why not bundle](#why-not-bundle) below.

## What we ship

### Read-only

- **`IFirmwareUpdateCapable` mixin** (deferred — capture-gated)
  exposing `firmwareVersion()` via the device-specific runtime probe:
  - AKP05 / AKP03 / AKP153: `CRT VER` (capture pending — W-01)
  - AK980 PRO: opcode `0x20 0x01` (already shipped in
    `proprietary_keyboard.cpp:601`)
  - AJ-series: opcode `0x80` `FEA_CMD_GET_REV`
    (`aj_series_opcode_table.md §3.1`)
- **Version display** in device details: `Firmware: X.Y.Z` next to the
  device name.
- **NO "update available" badge** — we have no signed manifest of
  latest versions, so claiming "available" would be either a lie or
  trust-shifting onto an unsigned third-party scrape.

### Vendor-tool deep-link

A single "Update firmware…" button per device that opens a small
modal:

> AJAZZ Control Center is a clean-room reimplementation. We do not
> distribute firmware. Use the official AJAZZ vendor application to
> flash your device.
>
> \[Launch vendor app] · \[Open firmware download page] · \[Close]

- **Launch vendor app**: probe known installation paths
  (`%ProgramFiles%\Stream Dock AJAZZ`,
  `%ProgramFiles%\AJAZZ Driver(R)`,
  `/Applications/Stream Dock AJAZZ.app`, etc.); if found, spawn via
  `QDesktopServices::openUrl(QUrl::fromLocalFile(...))`. **Close our
  HID handle first** so the vendor app's flasher doesn't fight us for
  the device.
- **Open firmware download page**: per-family fallback URL (see table
  below).

### Per-family vendor download URLs

Recorded here so the deep-link doesn't go to a 404 when the vendor
reshuffles their site. Pin the URL in code; if any of these die we
update this doc + the codename's `vendorFirmwareUrl` entry in
`docs/_data/devices.yaml`.

| Family                  | URL                                                              | Source                                                                                                                  |
| ----------------------- | ---------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Stream Dock (AKP03/05/153) | https://stream-dock.com/pages/support (firmware in installer ZIP) | Vendor doesn't publish standalone firmware URLs. The Stream Dock installer bundles `FirmwareUpdateTool` per the agent research. |
| AK980 PRO / AK820 Pro   | https://ajazzstore.com/blogs/firmware                            | Per-model `.zip` (e.g. `AJAZZ_ak820_pro_2.4G_upgrade.zip`); also mirrored at https://ajazz.driveall.cn/                  |
| AK980 PRO (alt portal)  | https://ajazz.driveall.cn/                                       | In-browser WebUSB/WebHID updater                                                                                        |
| AJ159 PRO mouse         | https://epomaker.com/blogs/software/ajazz-aj159-pro-driver-1     | Driver ZIP that includes firmware                                                                                       |
| AJ199 mouse             | https://epomaker.com/blogs/software/ajazz-aj199-driver           | Driver ZIP that includes firmware                                                                                       |

## What we do NOT ship

- **Firmware binary blobs** — under no path, ever (matches our
  CLAUDE.md hard rule against vendor-binary commingling).
- **The flash protocol** — we do not write to the bootloader. The
  vendor's `FirmwareUpdateTool` does. Even when the protocol is fully
  documented (e.g. AK980 PRO is on a Sonix SN32F299 clone and
  [SonixFlasherC](https://github.com/SonixQMK/SonixFlasherC) publishes
  the protocol under GPL), the flash itself is **out of scope for the
  main app**.
- **"Update available" notifications** — we have no authoritative
  manifest of latest versions per codename, and scraping the vendor's
  download page is a fragile cat-and-mouse game. If AJAZZ publishes a
  signed manifest in the future (e.g. via LVFS-style hosting), we
  revisit.

## Future escape hatch (opt-in, separate binary)

For the Sonix SN32F299 family ONLY (AK980 PRO, AK820 Pro), the
protocol is fully public via SonixFlasherC. A long-term option is to
ship a SEPARATE, opt-in `ajazz-firmware-flasher` binary under
`tools/` that:

- is built only when `-DAJAZZ_BUILD_FIRMWARE_FLASHER=ON`
- carries the SonixFlasherC GPL flasher logic (clean reimplementation
  in C++; the protocol is documented enough to reimplement, no need
  to vendor the C source)
- requires the user to provide their own firmware binary (no bundle)
- has a prominent "this WILL void your warranty and may brick the
  device" disclaimer matching OpenRGB's language
- is NOT integrated into the main app's UI — it's a CLI tool you run
  separately

This is parked. We don't build it until at least one of:

- a user explicitly requests it via a GitHub issue
- AJAZZ publishes an LVFS-compatible signed manifest
- a community-tested community firmware (e.g. a QMK port for SN32F299
  AK-keyboards) appears that users want to flash

## Why not bundle

Cost/benefit table for the alternative architectures considered:

| Approach                              | Pros                                       | Cons                                                                                                                                                          | Verdict   |
| ------------------------------------- | ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- |
| **(a) Bundle vendor blob in installer** | One-click update                           | GPL-3.0 commingling with proprietary blob inside same `.deb`/`.rpm`/`.flatpak`; vendor copyright; bricked-device liability; no LVFS-style signed agreement | Reject    |
| **(b) CDN proxy / fetch on demand**   | No bundle, version-pinned                  | Legally identical to bundling (we serve the bytes); no FOSS peer does this without an LVFS-style agreement                                                    | Reject    |
| **(c) Deep-link + vendor tool**       | Zero bundle, zero flash liability, matches OpenRGB / OpenDeck / OpenRazer / Solaar pattern | Worse UX (extra app, extra click)                                                                                                                             | **Adopt** |
| **(d) Instructions-only (no link)**   | Cleanest                                   | UX deadweight — user has to copy URL by hand                                                                                                                  | Skip      |

The decisive factor is **legal**: bundling vendor firmware without a
signed redistribution agreement (the LVFS model) is a copyright risk.
Every FOSS peripheral controller surveyed (OpenRGB, OpenDeck,
OpenRazer, Solaar, Piper, libratbag) makes the same call. ZSA Wally is
the only exception, and they sidestep the issue by shipping ZSA's own
firmware (which they own).

## When this revisits

- **AJAZZ joins LVFS** → we delegate to `fwupd` and become a thin
  notifier (same as Solaar for Logitech today). Estimated effort: ~1
  week to wire the D-Bus client.
- **AJAZZ publishes a signed manifest** (less likely but possible) →
  same architecture as above but talking to a vendor endpoint instead
  of LVFS. Same legal protection from the signed bytes.
- **A community-tested SN32F299 QMK port** for AK980 PRO appears →
  build the opt-in `ajazz-firmware-flasher` per the Future escape
  hatch section.

Until one of those triggers fires, **the current decision stands**.

______________________________________________________________________

## References

Source material for this decision (research agents 2026-05-18):

### Vendor distribution

- [AJAZZ Store firmware blog](https://ajazzstore.com/blogs/firmware)
- [AJAZZ DriveAll portal](https://ajazz.driveall.cn/)
- [Stream Dock support](https://stream-dock.com/pages/support)
- [Epomaker AJ159 PRO driver](https://epomaker.com/blogs/software/ajazz-aj159-pro-driver-1)
- [Epomaker AJ199 driver](https://epomaker.com/blogs/software/ajazz-aj199-driver)

### Chipset DFU protocols (documented but NOT used by us)

- [SonixFlasherC source](https://github.com/SonixQMK/SonixFlasherC/blob/main/sonixflasher.c) — GPL reference for SN32F299 bootloader
- [SonixQMK Docs install](https://sonixqmk.github.io/SonixDocs/install/)
- [SRGBmods QMK binaries](https://github.com/SRGBmods/QMK-Binaries/)

### Clean-room peer projects (pattern source)

- [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB) — "OpenRGB
  does not flash firmware, ever"
- [OpenDeck](https://github.com/nekename/OpenDeck) — no firmware
  update, delegates to Elgato
- [OpenRazer](https://openrazer.github.io/) — same delegation
- [Solaar](https://pwr-solaar.github.io/Solaar/) — Logitech delegate
  to fwupd
- [Piper / libratbag](https://github.com/libratbag/libratbag) — same
- [QMK CLI flashing](https://docs.qmk.fm/flashing)
- [ZSA Flash docs](https://www.zsa.io/flash) — the "in-app flash"
  exception, only works because ZSA owns the firmware

### Legal & licensing

- [LVFS upload terms](https://lvfs.readthedocs.io/en/latest/upload.html) —
  signed-redistribution agreement model
- [QMK license violations doc](https://docs.qmk.fm/license_violations) —
  historical takedown precedents
- [OpenRGB reverse-engineering policy](https://openrgb-wiki.readthedocs.io/en/latest/reverse-engineering/Reverse-Engineering/) —
  EU 2009/24/EC Art. 6 / US DMCA §1201(f) interop exemptions
