<!--
  vendor-software-inventory.md — RE task 1 deliverable.

  Catalogue of the official AJAZZ desktop applications (Stream Dock,
  keyboard / mouse drivers) — what URL serves them, what version is
  current, what they hash to, what OS / arch they target. Updated
  whenever a vendor release ships or whenever a new device family is
  surfaced by recon. Read `docs/research/README.md` for the clean-
  room and no-redistribution rules that govern this directory.

  How to add a new entry:

    1. Find the upstream URL on the relevant vendor download page
       (`ajazz.net`, `ajazzbrand.com`, `mirabox.key123.vip`,
       `support.key123.vip`, …).
    2. HEAD the URL with `curl -sIL --max-time 20`. Record:
         - the full URL,
         - Content-Length,
         - Last-Modified,
         - Content-MD5 (Aliyun OSS provides this; for shopify CDN
           you must download and hash locally — note that as
           "MD5: pending" until a downstream archive run fills it),
         - ETag (often == hex MD5 on Aliyun OSS).
    3. If the artefact is downloaded for full archival (a separate
       out-of-repo encrypted vault — never inside this repo),
       compute SHA-256 with `sha256sum` and update the entry.
    4. Always use ISO-8601 dates. The "Captured" column is the date
       *we* probed the URL, not the vendor's release date.

  Each release added MUST carry: version (where shown), OS, source
  page (so a downstream engineer can re-derive the URL even if the
  CDN URL rotates), Content-MD5, file size in bytes. SHA-256 is
  optional until the archival pass runs.
-->

# Vendor software inventory

Public catalogue of the official AJAZZ desktop applications.
**Read [`docs/research/README.md`](README.md) before contributing** —
this directory operates under hard clean-room and no-redistribution
rules. We do **not** mirror these binaries inside the repo. The
inventory exists so a downstream engineer can verify the artefact
they are looking at matches the one our recon notes describe.

All probe metadata captured **2026-04-26** unless an entry says
otherwise. Rows annotated `(captured 2026-04-29)` were re-probed
or downloaded during the static-analysis pass that produced
Findings 5–10 in [`vendor-protocol-notes.md`](vendor-protocol-notes.md);
SHA-256 hashes recorded there.

## Stream Dock (Mirabox + AJAZZ-branded)

The Stream Dock product line (AKP153, AKP153E, AKP03, AKP05,
AK980 PRO, …) is shared with Mirabox under the same firmware /
desktop-app stack. AJAZZ ships a re-branded installer that drops the
Mirabox chrome but keeps the same WebSocket / SDK protocol. The
shared catalogue API is documented at `https://sdk.key123.vip/en/guide/overview.html`.

| Distribution                       | OS           | Version                                                              | Bytes       | Last-Modified | Content-MD5 (hex)                     | URL                                                                                                                   |
| ---------------------------------- | ------------ | -------------------------------------------------------------------- | ----------- | ------------- | ------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| Stream Dock — AJAZZ-branded global | Windows 7+   | **2.9.177.122** (captured 2026-04-29)                                | 121 620 400 | 2024-01-29    | `a1828628 11703e09 582a009a 6a9a6a90` | `https://hotspot-oss-bucket.oss-cn-shenzhen.aliyuncs.com/custom/AJAZZ/Stream-Dock-AJAZZ-Installer_Windows_global.exe` |
| Stream Dock — AJAZZ-branded global | macOS 10.15+ | unspecified                                                          | 282 152 918 | 2024-02-01    | `dcbd35d9 54547369 c6e6e530 eef88dd3` | `https://hotspot-oss-bucket.oss-cn-shenzhen.aliyuncs.com/custom/AJAZZ/Stream-Dock-AJAZZ-Installer_Mac_global.dmg`     |
| Stream Dock — Mirabox generic      | Windows      | listed on `cdn1.key123.vip/StreamDock/Stream-Dock-Installer_Windows` | pending     | pending       | pending                               | discovery page: `https://mirabox.key123.vip/download`                                                                 |

> **Stream Dock Windows 2.9.177.122 — full hashes** (captured
> 2026-04-29, vendor URL above):
>
> - sha256: `005d18fbea74e393560431f167c12737b380687d544f0a48a25e73abda0354b5`
> - md5: `a182862811703e09582a009a6a9a6a90` (matches Aliyun Content-MD5)
> - VersionInfo: `CompanyName=HotSpot`, `LegalCopyright=Copyright (C) 2024 HotSpot`, `Translation=0x804/1200`
> - Authenticode: signed by `Shenzhen An Rui Xin Technology Co., Ltd.` (Sectigo Public Code Signing CA EV R36, valid 2023-10-30 → 2024-10-29, GlobalSign G4 timestamped). See `static-2026-04-29-streamdock-win-002` in `vendor-protocol-notes.md`.

> **Source pages**: `https://support.key123.vip/faqs/streamVersion.html`
> (version history, gated by JS — direct probe failed 2026-04-26),
> `https://space.key123.vip/StreamDock` (catalogue + plugin store).
> Plugin SDK reference: `https://sdk.key123.vip/en/guide/overview.html`.

> **Linux availability**: none. AJAZZ does not ship a Linux build of
> the Stream Dock app; this is one of the primary motivations for
> AJAZZ Control Center.

## Keyboards

Drivers are mirrored across two CDNs the vendor uses for delivery:
shopify (`cdn.shopify.com/s/files/1/0554/6678/6869/files/...`) and
the Mechlands / Epomaker / WhatGeek partner mirrors. We treat all of
them as canonical because the vendor itself links them from the
official pages — the file hash is the source of truth, not the URL.

> **Source page**: `https://ajazz.net/pages/ajazz-software` (USA),
> `https://ajazz.co.uk/pages/ajazz-software` (UK),
> `https://ajazzbrand.com/pages/drives` (CN-EN bilingual). Some
> mirror via `https://www.a-jazz.com/en/h-col-118.html` (404 at probe
> time, may be region-gated).

| Model                                    | OS            | Version                 | Bytes       | Last-Modified (UTC) | URL                                                                                                                                         |
| ---------------------------------------- | ------------- | ----------------------- | ----------- | ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| AK820 Max RGB                            | Windows       | V2.06.01 (inner FileVersion `2024.11.20.01`, captured 2026-04-29) | 17 983 320  | 2026-04-22          | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK820_MAX_RGB_Installer_V2.06.01.zip`                                         |
| AK820 Max HE (wired+wireless)            | Windows       | 2.1.78                  | pending     | pending             | `https://downloads.mechlands.com/software/AJAZZ_AK820_MAX_HE_setup_2.1.78.zip`                                                              |
| AK820                                    | Windows       | V1.0                    | pending     | pending             | `https://orders.epomaker.com/software/AJAZZ_AK820_Wired_Version_RGB_Keyboard_Driver_V1.0.zip`                                               |
| AK820 Max                                | Windows       | V1.0                    | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK820_MAX_driver_V1.0.zip`                                                    |
| AK820 Pro (Win)                          | Windows       | (n/a)                   | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK820Pro.zip`                                                                 |
| AK820 Pro (online driver)                | Windows (web) | live                    | n/a         | n/a                 | `https://ajazz.driveall.cn/#/`                                                                                                              |
| AK680 Max — 8+1K (tri-mode RGB magnetic) | Windows       | V2.1.87                 | pending     | pending             | `https://downloads.mechlands.com/software/AJAZZ_AK680_MAX__8_1K_tripe_mode_RGB_magnetic_switch_keyboard_drive_V2.1.87.zip`                  |
| AK680 Max — 8+8K (tri-mode RGB magnetic) | Windows       | V2.1.87                 | pending     | pending             | `https://downloads.mechlands.com/software/AJAZZ_AK680_MAX_8_8K_tripe_mode_RGB_magnetic_switch_keyboard_drive_V2.1.87.zip`                   |
| AK832 (tri-mode white-light)             | Windows       | V1.1                    | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK832_Triple_modes_White_Light_Keyboard_Driver_V1.1.zip`                      |
| AK832 Pro (tri-mode RGB)                 | Windows       | V1.0                    | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK832_Pro_Tri-mode_RGB__Keyboard_Driver_V1.0.zip`                             |
| AK870 Wired Magnetic                     | Windows       | V2.05.01                | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK870_single-mode_RGB_magnetic_switch_keyboard_driver_Installer_V2.05.01.zip` |
| AK870 V2 (tri-mode w/ screen)            | Windows       | 1.0.0.0                 | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK870_V2_Tri_mode_RGB_Tri_mode_With_Screen_RGB_Keyboard_Driver-1.0.0.0.zip`   |
| AK870MC                                  | Windows       | V1.0                    | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK870MC_Single_Mode_Mixed_Color_Lighting_Keyboard_Driver_V1.0.zip`            |
| AK650 (tri-mode w/ screen)               | Windows       | V1.0                    | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK650_Tri-mode_with_Screen_RGB_version_Keyboard_Driver_V1.0.zip`              |
| AKS068 Pro                               | Windows       | v2.2                    | pending     | pending             | `https://orders.epomaker.com/software/AJAZZ_AKS068_driver_v2.2.zip`                                                                         |
| AKS075 (tri-mode RGB w/ screen)          | Windows       | V1.0.0.2                | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AKS075_Triple_Modes_RGB_With_Screen_Keyboard_Driver_V1.0.0.2.zip`             |
| AKP815 Screen                            | Windows       | V1.177                  | 115 094 518 | 2024-03-08          | `https://orders.epomaker.com/software/AJAZZ_AKP815_Screen_driver_V1.177.zip`                                                                |
| NK68 (RGB wired)                         | Windows       | v1.0 (2025-06-04 build) | pending     | pending             | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_NACODEX_NK68_RGB_Wired_Version_Keyboard_Driver_v1.0_20250604.zip`             |

> **Maude keyboard**: not surfaced by either the EN or UK official
> pages as of 2026-04-26. The model is referenced in our TODO under
> the reverse-engineering track but the vendor download page does
> not yet expose it. Possibilities: (a) Maude is a regional /
> internal name for one of the AK-series listed above; (b) the page
> is stale; (c) Maude is unreleased. Action item: confirm via
> support@a-jazz.com before recon starts on this device.

## Mice

| Model                         | Sensor      | OS      | Version     | Bytes       | Last-Modified | URL                                                                                                                         |
| ----------------------------- | ----------- | ------- | ----------- | ----------- | ------------- | --------------------------------------------------------------------------------------------------------------------------- |
| AJ199 (no-RGB, dual-mode)     | PAW3395     | Windows | V1.0 (inner build `20231205`, captured 2026-04-29) | 2 237 633   | 2026-04-24    | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AJ199_No_RGB_Dual_Modes_PAW3395_Mouse_Driver_V1.0.zip`        |
| AJ199 Max (tri-mode)          | PAW3395     | Windows | inner V1.15.0.43 (build `2024.12.26`, captured 2026-04-29) | 7 567 973   | 2026-04-25    | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AJ199_MAX_Triple_Modes_PAW3395_Windows_Mouse_Driver.zip`      |
| AJ199 Carbon Fibre (tri-mode) | PAW3311     | Windows | unspecified | pending     | pending       | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AJ199_Carbon_fiber_Tripe_mode_PAW3311_Win_Mouse_Driver.zip`   |
| AJ159 / AJ159 P (dual-mode)   | PAW3395     | Windows | inner V1.0.5.3 (linker ts 2025-03-18, captured 2026-04-29) | 6 020 626   | 2026-04-22    | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJ159_AJ159P_Dual_modes_PAW3395_Win_Mouse_Driver.zip`               |
| AJ159 Pro                     | PAW3395     | Windows | unspecified | 157 121 005 | 2025-04-30    | `https://orders.epomaker.com/software/AJ159_PRO_PAW3395_Win_driver.zip`                                                     |
| AJ159 APEX                    | PAW3950     | Windows | unspecified | pending     | pending       | `https://orders.epomaker.com/software/AJ159_APEX_PAW3950_Win_driver.zip`                                                    |
| AJ179 / AJ179 P (dual-mode)   | PAW3395     | Windows | unspecified | pending     | pending       | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AJ179_AJ179P_Dual-mode_PAW3395_Windows_Only_Mouse_Driver.zip` |
| AJ179 APEX (tri-mode)         | PAW3950     | Windows | unspecified | pending     | pending       | `https://orders.epomaker.com/software/AJAZZ_AJ179_APEX_Tri-mode_PAW3950_Win_Mouse_Driver.zip`                               |
| AJ139 Max (tri-mode)          | PAW3395     | Windows | unspecified | pending     | pending       | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AJ139_MAX_Tripe_Mode_PAW3395_Win_System_Mouse_Driver.zip`     |
| AJ039 (wired)                 | A704F       | Windows | unspecified | pending     | pending       | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AJ039_Wired_Version_A704F_Mouse_Driver.zip`                   |
| AJ129                         | PAW3327     | Windows | unspecified | pending     | pending       | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AJ129_PAW3327_Mouse_Driver.zip`                               |
| AJ358 (gaming)                | unspecified | Windows | unspecified | pending     | pending       | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AJ358_Gaming_Mouse_Driver.zip`                                |
| AJ52 Pro (tri-mode)           | PAW3325     | Windows | unspecified | pending     | pending       | `https://orders.epomaker.com/software/AJAZZ_AJ52PRO%EF%BC%88tri%20mode%EF%BC%89_PAW3325_mouse%20driver.zip`                 |
| AM3 Pro (tri-mode)            | PAW3950     | Windows | unspecified | pending     | pending       | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AM3_PRO_Triple_Modes_PAW3950_Windows_Mouse_Driver.zip`        |
| AM3 Max (tri-mode)            | PAW3950     | Windows | unspecified | pending     | pending       | `https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AM3_MAX_Triple_Modes_PAW3950_Windows_Mouse_Driver.zip`        |

> **AJ339, AJ380**: not present on the consolidated EN download
> page. They appear in our `docs/_data/devices.yaml` (so AJAZZ
> Control Center already enumerates them by VID:PID) but the vendor
> driver download is not currently linked. Either: (a) the device
> uses a generic AJ-series driver listed above, (b) drivers are
> served only via product-page-specific links not yet enumerated,
> (c) the device is firmware-only and ships no Windows tool. Action
> item: HID-capture an AJ339 / AJ380 once we have one and compare
> the report descriptors against AJ199 Max — protocol may already
> be covered by the existing implementation in
> `src/devices/mouse/`.

## Third-party mirrors and references

These are not vendor-published, but the vendor itself links some of
them from the official pages, and we list them so a downstream
engineer can cross-check artefacts.

| Repository                                                     | Contents                                                                | Note                                                                                                                                                                              |
| -------------------------------------------------------------- | ----------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `https://github.com/progzone122/ajazz-aj199-official-software` | Redistribution of the official AJ199 driver (v1.0, 2024-11).            | Pinned reference for AJ199 protocol cross-check. **Not a clean-room source** — readers of this redistribution must NOT contribute to `src/devices/mouse/` for AJAZZ AJ199 family. |
| `https://github.com/4ndv/opendeck-akp03`                       | Independent OSS implementation of the AKP03 protocol.                   | Already cited in `README.md`; clean-room (independent reverse engineering, not a vendor port).                                                                                    |
| `https://github.com/4ndv/opendeck-akp153`                      | Independent OSS implementation of the AKP153 protocol.                  | Already cited in `README.md`; clean-room.                                                                                                                                         |
| `https://github.com/mishamyrt/ajazz-sdk`                       | Generic AJAZZ SDK — useful for cross-checking key codec / image format. | Independent OSS, not a vendor port.                                                                                                                                               |

## Open items

These need a person + bandwidth to close:

1. **Stream Dock — Mirabox-branded** Windows + macOS installers
   (the non-`AJAZZ`-renamed builds). The discovery page
   (`mirabox.key123.vip/download`) is JS-rendered and a single-shot
   WebFetch still returns empty content (re-probed 2026-04-29).
   Try a real browser session and record the raw URLs, sizes,
   MD5s here — the Mirabox build is a strict superset of the
   AJAZZ build for the AKP-class hardware so its protocol log is
   the more thorough capture target.
1. **Maude keyboard** — confirmed via web search (2026-04-29) NOT
   present on `ajazz.net` or `a-jazz.com`. Most likely (a) an
   internal codename, (b) a regional-only release, or (c)
   unreleased. Vendor support contact is `support@a-jazz.com`.
   No further investigation possible by static recon; gated on a
   support reply or a community sighting.
1. **AJ339, AJ380** — confirmed via web search (2026-04-29) NOT
   on the consolidated EN download page. The product `AJ390` (a
   different number, ultra-light hollow-out optical) IS listed and
   may be confused with `AJ339`/`AJ380`. Action item: HID-capture
   an AJ339 / AJ380 once we have one and compare the report
   descriptors against AJ199 Max — protocol may already be covered
   by the existing implementation in `src/devices/mouse/`. Cross-
   ref Finding 8 in `vendor-protocol-notes.md` for the AJ-series
   USB ID space (`248A:5C2E/5D2E/5E2E` USB, `248A:5C2F` /
   `249A:5C2F` 2.4G dongle).
1. **SHA-256 archival pass — partial** (captured 2026-04-29):
   five priority installers downloaded with full hashes recorded.
   Stream Dock Win sha256 + md5 are documented in the table-
   adjacent block above and matched between Aliyun's HEAD-reported
   Content-MD5 and the artefact's local hash (no CDN tampering).
   Mouse / keyboard outer-ZIP and inner-EXE sha256 hashes captured
   in the recon journal at `<vault>/journal/downloads-2026-04-29.json`
   (out of repo); referenced from `vendor-protocol-notes.md`
   Findings 5–10 by capture-id. **Remaining**: every artefact in
   the table whose row says `pending` for Bytes / Last-Modified —
   re-probe and add hashes once they are downloaded.
1. **Version strings — partial** (captured 2026-04-29):
   AK820 Max RGB inner-EXE FileVersion `2024.11.20.01`, AJ199 V1.0
   inner build `20231205`, AJ199 Max inner-EXE FileVersion
   `1.15.0.43` build `2024.12.26`, AJ159 inner V1.0.5.3 (linker
   timestamp 2025-03-18). Stream Dock Windows app version
   `2.9.177.122`. Remaining "unspecified" rows still need a
   download + `innoextract --info` pass for the Inno-Setup-based
   ones, or `(Get-Item).VersionInfo` for the Advanced-Installer-
   based ones (Stream Dock variant).
