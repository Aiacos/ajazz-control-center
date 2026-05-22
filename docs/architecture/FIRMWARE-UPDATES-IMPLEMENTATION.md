# Firmware updates — implementation feasibility study (all 3 device families)

> **Status: EXPLORATORY (2026-05-21).** This document revisits the "Future
> escape hatch" left open by the DECIDED posture in
> [`FIRMWARE-UPDATES.md`](FIRMWARE-UPDATES.md) and asks the next question the
> maintainer raised: *if* we wanted to actually perform firmware updates in-app
> (as a feature) for the keyboard, mouse, and Stream Dock, **how** would we do
> it — grounded in the reverse engineering — and what does each device cost in
> effort, risk, and legal exposure?
>
> Nothing here changes the shipping decision (deep-link + vendor tool). It is a
> design study to decide whether/which path is worth promoting from "parked" to
> "planned". RE source of truth: the per-device dossier
> (`docs/research/reverse-engineering/`, branch `research/reverse-engineering`)
> and the on-main protocol docs.

---

## 0. Non-negotiable constraints (these gate every option below)

1. **No firmware bundling / redistribution.** We never ship, proxy, or fetch
   vendor firmware bytes. Any flash uses a **user-supplied** firmware file. (Same
   legal reasoning as `FIRMWARE-UPDATES.md §Why not bundle` + the CLAUDE.md
   vendor-binary rule.)
2. **No circumvention of firmware protection.** The Stream Dock image is wrapped
   with an encrypted sentinel (an `aKDFU`/AES-GCM construction in the vendor
   tool). We do **not** extract, reproduce, or work around that key — that is a
   DMCA §1201 / EUCD anti-circumvention problem, not a clean-room interop one.
   This alone disqualifies the Stream Dock in-app flash (see §3.3).
3. **No libusb in `ajazz_core`** (COD-031, release-blocker). Any DFU path that
   needs raw bulk/control transfers (Stream Dock Bulk-Only) cannot live in core
   and cannot use the shipped `hidapi_hidraw` transport — it would be a separate,
   optional, libusb-linked module/binary, never linked into the app core.
4. **Honest capability + brick-safety.** A flasher that isn't hardware-verified
   must NOT be exposed as if it were. Mid-flash power loss bricks the device;
   every path needs pre-flight checks, a recovery story, and OpenRGB-grade
   "this may brick / voids warranty" consent.
5. **Opt-in, separate from the main UI.** Per `FIRMWARE-UPDATES.md`, a flasher
   ships (if at all) as a `tools/` binary behind
   `-DAJAZZ_BUILD_FIRMWARE_FLASHER=ON`, not wired into the device list's normal
   flow.

---

## 1. What the RE tells us per family (the raw feasibility input)

| Family | Bootloader / transport | Protocol public? | Needs libusb? | Crypto gate? | RE confidence |
| --- | --- | --- | --- | --- | --- |
| **Keyboard AK980 PRO** | Sonix **SN32F2xx** USB-HID bootloader | **Yes** — SonixFlasherC (GPL) + SonixQMK docs fully document it | No (HID) | No (the bootloader itself is open) | High (chipset public); vendor's own `FirmwareUpdateTool.exe` flow not captured |
| **Mouse AJ-series** | **HID OTA** in-protocol: MLED `0x40/0x41/0xc0/0xc1` (mouse-MCU), OLED `0x30/0x31` (screen-MCU); AJ199 Max `EnterUsbUpdateMode 0x0D`; BLE `upgradeOTAGATT` | Partially — opcodes RE'd from the vendor JS/driver, **not captured/verified** | No (HID) for the wired/2.4G path | No (no encrypted sentinel seen in the HID OTA) | Medium — opcodes known, byte-exact framing + image format unverified |
| **Stream Dock AKP** | Allwinner-SoC **USB Bulk-Only (CBW/CSW)** via `FirmwareUpgradeTool.exe` (the only vendor binary linking `libusb-1.0`) | Allwinner FEL/eGON-style is broadly documented, but the AJAZZ image is **encrypted** (`AIC.FW` + `aKDFU` AES-GCM sentinel) | **Yes** (bulk transfers) | **Yes** (encrypted image) | Low — re-enumerates as a different "UU device"; trigger packet + UU VID:PID uncaptured |

**Reading:** feasibility/safety descends keyboard → mouse → Stream Dock. The
keyboard rides a fully-public chipset bootloader; the mouse rides an in-protocol
HID OTA we already have a transport for; the Stream Dock needs libusb **and**
crosses the anti-circumvention line.

---

## 2. Proposed architecture (if we build any of it)

### 2.1 Capability surface

- Extend the **read-only `IFirmwareUpdateCapable`** (already planned in
  `FIRMWARE-UPDATES.md` for `firmwareVersion()`) with an OPTIONAL, capability-
  gated write side:
  ```
  struct FirmwareImage { std::span<const std::byte> bytes; std::string declaredVersion; };
  enum class FlashStage { Validating, EnteringBootloader, Erasing, Writing, Verifying, Rebooting, Done, Failed };
  // Implemented ONLY by backends whose flasher is hardware-verified.
  virtual FlashPlan prepareFlash(const FirmwareImage&) = 0;   // dry-run: validates, returns chunking + risks
  virtual void      flash(const FirmwareImage&, ProgressFn) = 0;  // performs it; throws on hard failure
  ```
- A backend that cannot safely flash returns `NotImplemented` from
  `prepareFlash` — never a fake success (Pitfall 12/13).

### 2.2 Where the code lives (COD-031-safe)

- **HID-based flashers (keyboard, mouse):** can use the existing
  `ITransport`/hidapi seam, so they could live in a new
  `ajazz_firmware` module **PRIVATE-linked** to the device libs — still no
  libusb. But per constraint 5 they are exposed only through the opt-in
  `tools/ajazz-firmware-flasher` binary, not the main app UI, until hardware-proven.
- **Bulk-based flasher (Stream Dock):** would require a separate libusb-linked
  binary entirely outside `ajazz_core`. **Recommendation: do not build** (§3.3).

### 2.3 The flash state machine (shared)

```
preflight ─► consent ─► close-all-handles ─► enter-bootloader ─► (device re-enumerates?)
   │                                                                     │
   └─ checks: image magic + size + declared-vs-running version,          ▼
      battery ≥ 50% (wireless), AC/wired preferred, single device,    erase ─► write chunks ─► verify
      no other app holding the handle                                      │
                                                                            ▼
                                                              reboot ─► re-probe VER ─► confirm
```

Hard rules: refuse to flash a **wireless** device below a battery threshold;
warn that disconnecting mid-write bricks it; serialise (never flash two devices
at once); always re-probe the firmware version after reboot to confirm.

### 2.4 Firmware sourcing & recovery

- **User-supplied file only.** The UI accepts a path/drag-drop to a firmware the
  user downloaded from the vendor (we still deep-link the official download page
  from `FIRMWARE-UPDATES.md`). We validate magic/size/checksum but never host it.
- **Recovery:** document the vendor-tool recovery path per family; for the
  keyboard, the SN32 bootloader is re-enterable via a **physical pin-short under
  the spacebar** even after a failed flash (DFU PID `0x0C45:0x7140`) — a real
  safety advantage of that chipset (see §8).

---

## 3. Per-device plan

### 3.1 Keyboard AK980 PRO — **best candidate** (Sonix SN32F2xx)

- **Why feasible:** the SN32F2xx bootloader protocol is public and GPL-documented
  (SonixFlasherC supports SN32F22x/23x/24x/26x/28x/**29x**; HID-based, hidapi —
  so our transport works, no libusb). Online validation (§8) confirms the AK820
  Pro (the AK980 PRO's direct sibling) is an **HFD80CP100, a SONIX SN32F299
  clone** using the **`hfd` OEM bootloader variant** that SonixFlasherC handles
  via `--reboot hfd`. Bootloader entry is a **physical pin-short under the
  spacebar** during USB connect → DFU enumerates as **VID:PID `0x0C45:0x7140`**;
  it is therefore hardware-recoverable (you can re-enter the bootloader manually
  after a bad flash), which lowers brick risk.
- **What we'd implement (clean-room from the public protocol, not vendoring the
  GPL C):** enter-bootloader handshake, page erase, page write, CRC verify,
  reboot — against a **user-supplied** firmware binary, scoped to the **SN32
  main MCU**.
- **Caveats (sharpened by §8 validation):**
  (a) the AK980 PRO ships **vendor** firmware and **no QMK port exists** for this
  family (the community repo is RE-only, "perhaps someone will add QMK") — so we
  can only flash a user-downloaded *vendor* image, there's no community firmware
  to offer;
  (b) it's an HFD-variant SN32F299 **clone**, not the bare stock bootloader —
  confirm the AK980 PRO's exact DFU PID + page size against the real chip before
  trusting SonixFlasherC's defaults. **Caveat (RE re-check 2026-05-22):** the DFU
  PID `0x0C45:0x7140` is **external SonixFlasherC/community lore for the sibling
  AK820 Pro — it does NOT appear anywhere in our own RE corpus.** Our captured/
  decompiled keyboard PIDs are only `0x0C45:0x8009` (wired), `0x0C45:0xFEFE`
  (2.4G dongle), and `0x05AC:0x024F` (XS75T rebrand). Treat `0x7140` as a
  hypothesis to confirm on the physical AK980 PRO, not a fact;
  (c) **the wireless side is a SEPARATE chip** — a **WCH CH582F** BT/2.4G module
  (I2C/UART to the SN32), which SonixFlasher/QMK do **not** touch. Flashing the
  SN32 updates ONLY the wired/main firmware; the 2.4G/BT firmware (the AK980 PRO
  `0xFEFE` dongle path) is a second, undocumented update problem;
  (d) bootloader entry needs a **manual pin-short**, so a one-click software flash
  isn't possible — the UX is "put the board in DFU yourself, then flash";
  (e) GPL: a clean C++ reimpl of the documented protocol is fine; lifting
  SonixFlasherC code makes the `tools/` binary GPL-3.0 (acceptable, deliberate).
- **Effort:** the *flasher itself* is ~1–2 weeks, but it is **gated** on a capture
  confirming the AK980 PRO uses the stock SN32/HFD bootloader (the AK820 Pro does;
  the AK980 PRO is not separately documented) and a decision to scope to the
  wired MCU only.

### 3.2 Mouse AJ-series — **medium** (in-protocol HID OTA)

- **Why interesting:** the OTA is HID, in the same vendor protocol we already
  speak — no libusb, no new transport. The opcodes are RE'd:
  - `0x40 SET_MLEDBOOTLOADER` (payload `55 AA 55 AA 00 00 00`) → enter bootloader
  - poll `0xc0` until `resp[1]==1`
  - `0x41 SET_MLEDBOOTSTART` (uint16-LE chunk count)
  - 64-byte data chunks (image with the first `0x10000` bytes skipped)
  - `0xc1 GET_MLEDBOOTCHECKSUM` (int32-LE; `resp[1]==0x55` = OK)
  - separate `0x30/0x31` flow for the OLED screen-MCU; AJ199 Max uses the
    HIDUsb-dialect `EnterUsbUpdateMode 0x0D`; BLE uses gRPC `upgradeOTAGATT`.
- **Why risky:** every byte above is **decompile-derived, never captured**. The
  image header/skip semantics, the chunk pacing, and the checksum algorithm are
  unverified; a wrong guess bricks the mouse with no key-combo recovery. The
  AJ199 family is a *different dialect* entirely (§dossier mouse §5): the OTA is
  **not unifiable** across AJ-series. Only AJ159 (`0x3151`, 64-byte / report-id
  `0x05` / BIT7 checksum) is the dialect our backend speaks; AJ199 (`0x3554`)
  uses OemDrv 17-byte (`0x08`) or HIDUsb 20-byte (`0x01`) framing, and its
  registry entries are **flagged SUSPECT** pending a per-(VID,PID,fw) dialect
  dispatch (`aj_series.md §5`). A mouse flasher would have to be written
  per-dialect, never once.
- **Gate:** do NOT attempt without (a) a Frida capture of the vendor OTA on a
  sacrificial unit, (b) a verified image-format spec, (c) a confirmed recovery
  path. Until then this stays detect-and-delegate.
- **Effort:** ~2–3 weeks **after** a clean OTA capture; high QA cost.

### 3.3 Stream Dock AKP — **do not build in-app** (libusb + encrypted image)

- Requires raw USB **Bulk-Only** transfers → **libusb**, which COD-031 forbids in
  core and which our `hidapi_hidraw`-only stack doesn't do.
- The firmware image is **encrypted** (`AIC.FW` + `aKDFU` AES-GCM sentinel).
  Performing the flash means defeating that protection → **anti-circumvention
  legal exposure** we will not take. (We document its *existence* in the dossier;
  we do not reproduce the key or unwrap routine.)
- The device re-enumerates as a different Allwinner "UU device" mid-flash; the
  trigger packet and UU VID:PID are uncaptured anyway.
- **Recommendation:** keep the current posture — **detect** the device entering
  DFU (VID/PID disappears → UU device appears), surface a clear "use the vendor
  Stream Dock tool to finish the update" message, and resume normal operation on
  re-enumeration. No flashing.

---

## 4. Recommended phasing

| Phase | Scope | Trigger to start |
| --- | --- | --- |
| **P0 (now, safe)** | Finish the **read-only** `IFirmwareUpdateCapable` + version display + the deep-link modal already specified in `FIRMWARE-UPDATES.md`. Add Stream Dock **DFU-detect-and-inform** (§3.3). | none — already decided |
| **P1 (study → maybe)** | Capture the **keyboard** SN32 bootloader handshake on the real AK980 PRO; if it's the stock Sonix bootloader, build the opt-in `tools/ajazz-firmware-flasher` for the keyboard only (user-supplied image, GPL tool). | maintainer go-ahead + the capture |
| **P2 (gated)** | Capture the **mouse** HID OTA on a sacrificial unit; spec the image format; only then consider adding the mouse to the opt-in flasher. | P1 shipped + a clean mouse OTA capture + a spare unit |
| **never** | Stream Dock in-app flash. | — |

P0 is unconditionally safe and worth doing now. P1 is the only path that's both
technically clean and legally low-risk. P2 is contingent on captures we don't
have. Stream Dock stays delegate-only.

---

## 5. Capture-needed / open questions (the gating unknowns)

- **Keyboard:** does the AK980 PRO use the *stock* SN32F2xx HID bootloader or a
  vendor-customised one? Page size? Bootloader-entry command (vs key combo)?
  CRC/verify algorithm? (1 capture of the vendor tool flashing, + a SonixFlasherC
  probe in `--query` mode.)
- **Mouse:** Frida capture of the vendor OTA — the `0x40→0xc0→0x41→chunks→0xc1`
  sequence on the wire; the exact image header + the `0x10000` skip; chunk pacing;
  the checksum algorithm; the recovery path; the AJ199 (HIDUsb) variant.
- **Stream Dock:** (not pursued) UU-device VID:PID, the DFU-trigger packet — only
  needed for the *detect* feature, not for flashing.
- **Cross-cutting:** is there any community/LVFS firmware for any of these? (If
  AJAZZ ever joins LVFS, all of this collapses into a thin `fwupd` delegate — the
  preferred end state per `FIRMWARE-UPDATES.md §When this revisits`.)

---

## 6. Risk register

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Brick on mid-flash power loss | High | battery/AC pre-flight; serialise; loud consent; prefer wired |
| Wrong RE → bad write bricks unit | High | hardware-capture-gated; dry-run validate; keyboard-first (recoverable bootloader) |
| Firmware redistribution claim | High | user-supplied only; never host/proxy/bundle |
| Anti-circumvention (Stream Dock crypto) | High | don't build it; detect-and-delegate |
| libusb creeping into core | Med | bulk path lives only in a separate opt-in binary, never core (COD-031 grep gate) |
| GPL contamination from SonixFlasherC | Low | clean reimpl of the documented protocol, OR isolate GPL in the `tools/` binary only |
| User flashes wrong-model image | Med | validate magic + declared-vs-running version + model gate before write |

---

## 7. Bottom line

The maintainer's question — "can we also do firmware updates?" — has a nuanced
answer grounded in the RE:

- **Keyboard: yes, plausibly** — via an opt-in, user-supplied-firmware,
  clean-room SN32 flasher (the chipset protocol is public and recoverable). This
  is the one path worth promoting from "parked" to "study/P1".
- **Mouse: maybe, later** — the HID OTA is in-protocol and libusb-free, but every
  byte is unverified and bricking is unrecoverable; gate hard on a capture.
- **Stream Dock: no** — libusb + an encrypted image put it outside both our
  technical constraints and our legal lines; keep detecting + delegating.

Recommended next concrete step: do **P0** now (read-only version + deep-link +
DFU-detect), and schedule the **keyboard SN32 capture** to decide P1. Everything
else waits on hardware captures and an explicit maintainer decision.

---

## 8. Online validation (2026-05-21)

The technical claims above were checked against public sources. Net effect:
the **direction holds** (keyboard best, mouse gated, Stream Dock no), but the
keyboard path is **more constrained** than the first draft implied.

### Confirmed
- **SonixFlasherC is real, HID-based (hidapi, no libusb), and covers SN32F29x.**
  Supported families: SN32F22x/23x/24x/26x/28x/**29x**; firmware is `.bin`;
  jumploader offset `0x200`; `--reboot` has OEM variants **`sonix`, `evision`,
  `hfd`**. This validates the "HID, libusb-free, public protocol" basis of P1.
- **The AK820 Pro (AK980 PRO's direct sibling) is an `HFD80CP100` = a SONIX
  SN32F299 clone**, using the **`hfd`** bootloader variant. Confirms the dossier's
  SN32F299 lineage AND that it's an OEM (HFD) variant, not bare stock — and that
  SonixFlasherC's `--reboot hfd` is the matching path. (Source: the community RE
  repo `fpb/ajazz-ak820-pro` + SonixQMK Mechanical-Keyboard-Database issue #50.)
- **Bootloader entry = physical pin-short under the spacebar** during USB connect;
  DFU enumerates as **`0x0C45:0x7140`** *on the sibling AK820 Pro, per external
  community RE*. Hardware-recoverable, but manual (no software-only trigger).
  **This PID is NOT in our own corpus** (RE re-check 2026-05-22) — see §3.1
  caveat (b); it is a value to confirm on the real AK980 PRO, not yet a fact.

### Refuted / corrected
- **"Recoverable via key combo"** → it's a **physical pin-short**, not a key
  combo. Fixed.
- **"The bootloader is open / low-risk"** is too rosy: the AK820 Pro is an HFD
  *clone*, **no QMK port exists** (the community repo is RE-only with a
  "perhaps someone will add QMK support" TODO), and **only stock vendor firmware
  is archived** — there is no community firmware to flash. So P1 can only flash a
  *user-supplied vendor* image, and the brick mappings are incompletely documented.

### New constraint discovered (material)
- **The wireless side is a separate `WCH CH582F` BT/2.4G chip** (I2C/UART to the
  SN32), which SonixFlasher/QMK do **not** touch. So flashing the SN32 updates
  ONLY the wired/main firmware; the AK980 PRO's **2.4G/BT firmware** (`0xFEFE`
  dongle path) is a **second, undocumented chip** — a full "tri-mode firmware
  update" is a two-MCU problem, and our flasher would honestly cover only the
  wired MCU.

### Could not confirm (stays RE-derived)
- **Stream Dock = Allwinner SoC + encrypted Bulk-Only DFU.** No public source
  corroborated or refuted this; it remains derived from our own RE
  (`FirmwareUpgradeTool.exe` linking libusb + the `AIC.FW`/`aKDFU` strings). The
  "do not build" verdict is unchanged and is anyway driven by the libusb + crypto
  constraints, not by the chip identity.
- **Mouse HID OTA** (`0x40/0x41/0xc0/0xc1`): no public corroboration; stays
  decompile-only/uncaptured. Verdict (gate on a capture) unchanged.

### LVFS / fwupd end-state
- **AJAZZ / A-JAZZ / Mirabox / Microdia are NOT on LVFS** (checked the LVFS
  device list). So the preferred "thin `fwupd` delegate" end-state from
  `FIRMWARE-UPDATES.md §When this revisits` is **not available today** and cannot
  be relied on; it remains a "if AJAZZ ever joins LVFS" hypothetical.

### Official firmware distribution (for the deep-link, confirmed live)
- AK820 Pro / AK-family: per-model upgrade `.zip` on `ajazzstore.com/blogs/firmware`
  and `epomaker.com/blogs/firmware`, mirrored at `ajazz.driveall.cn`.
- Stream Dock: firmware bundled inside the installer from `ajazzstore.com` /
  `ajazz.driveall.cn` (no standalone firmware URL).

### Adjusted bottom line
Keyboard remains the only sane in-app candidate, but realistically scoped to the
**wired SN32/HFD main MCU with a user-supplied vendor image and a manual
pin-short entry** — and gated on a capture confirming the AK980 PRO matches its
AK820 Pro sibling. The wireless WCH chip, the mouse OTA, and the Stream Dock are
all out of reach for now. **P0 stays the right immediate move.**

### Sources
- [SonixFlasherC](https://github.com/SonixQMK/SonixFlasherC) · [README](https://github.com/SonixQMK/SonixFlasherC/blob/main/README.md)
- [SonixQMK compatible keyboards](https://sonixqmk.github.io/SonixDocs/compatible_kb/) · [SonixQMK Mechanical-Keyboard-Database #50 (AK820 Pro)](https://github.com/SonixQMK/Mechanical-Keyboard-Database/issues/50)
- [fpb/ajazz-ak820-pro (community RE of the sibling)](https://github.com/fpb/ajazz-ak820-pro)
- [LVFS device list](https://fwupd.org/lvfs/devices/) · [fwupd](https://github.com/fwupd/fwupd)
- [AJAZZ Store firmware](https://ajazzstore.com/blogs/firmware) · [Epomaker AK820 Pro 2.4G upgrade](https://epomaker.com/blogs/firmware/ajazz-ak820-pro-2-4g-upgrade) · [AJAZZ DriveAll portal](https://ajazz.driveall.cn/)
