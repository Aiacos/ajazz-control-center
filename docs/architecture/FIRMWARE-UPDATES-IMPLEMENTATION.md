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
  keyboard, the SN32 bootloader is re-enterable via a key combo even after a
  failed flash (a key safety advantage of that chipset).

---

## 3. Per-device plan

### 3.1 Keyboard AK980 PRO — **best candidate** (Sonix SN32F2xx)

- **Why feasible:** the bootloader protocol is fully public and GPL-documented
  (SonixFlasherC + SonixQMK). It's HID, so our transport works. The bootloader
  is hardware-recoverable (re-enter via key combo), which dramatically lowers
  brick risk.
- **What we'd implement (clean-room from the public protocol, not vendoring the
  GPL C):** enter-bootloader handshake, page erase, page write, CRC verify,
  reboot — against a **user-supplied** firmware binary.
- **Caveats:** (a) the AK980 PRO ships **vendor** firmware, not QMK — flashing a
  user-downloaded vendor image is fine, but there's no community firmware to
  offer; (b) the exact SN32 sub-variant (F299 vs F26x) changes page size /
  protocol details — confirm against the chip; (c) GPL: a clean C++ reimpl of a
  documented protocol is fine, but if we lift SonixFlasherC code, the tool
  inherits GPL-3.0 (acceptable for a `tools/` binary, but a deliberate choice).
- **Effort:** ~1–2 weeks for the opt-in CLI tool + a capture to confirm the
  AK980 PRO actually uses the stock SN32 bootloader (vs a vendor-customised one).

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
  AJ199 family is a *different dialect* entirely (§dossier mouse §5).
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
