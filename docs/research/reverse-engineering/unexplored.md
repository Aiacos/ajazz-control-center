# Unexplored areas & capture wishlist (cross-device)

Everything NOT yet hardware-confirmed, consolidated across the three families.
Per-device detail is in the §6/§7/§8 sections of each device file. Ordered by
value to the cross-platform (Linux/macOS) effort and to closing honest-capability
gaps.

## P0 — blocks current cross-platform work

1. **Stream Dock image upload on Linux/hidraw.** The `feat/linux-device-support`
   branch adds a `0x00` report-id prepend (POSIX-only) so the `CRT` packets land
   unshifted. **Pending Fedora confirmation:** does the AKP05 first-key icon
   render? Cross-check AKP153 on the same box to tell family-wide vs AKP05-only.
1. **Mouse interface selection on hidraw.** The two-pass `usage_page`+`usage` →
   `usage_page`-only match. **Pending Fedora confirmation:** does
   `hid_enumerate` populate `usage` on the Fedora kernel? Does the log show
   `(usage+page filtered)` / `(usage-page filtered)`, and do battery (report
   0x05) + clock (0x28) work? Direct check: `aj_mouse_probe.py --enumerate/--battery`.
1. **udev access** for VIDs `0x0c45` (keyboard), `0x3151` (mouse), `0x0300`
   (Stream Dock) on Fedora — `resources/linux/99-ajazz.rules` installed + ACLs.

## P1 — high-value, hardware-witnessable now

4. **AK980 PRO TFT image upload (entire path PROVISIONAL).** `field[0x527c4]`
   pixel-start / 256-byte GIF header (⇒ 2315 vs 2324 chunks); output- vs
   feature-report transport; byte-32 checksum validation; the per-chunk ACK
   readback; RGB565 byte order (test (0,0)=red `0xF800`); bulk-vs-chunked trigger.
1. **AKP05 wire bytes never confirmed against a live USB capture** — DRA partial-
   update sequence; M_V `location==0x12` boot-logo; LOG BE16-vs-BE32 size field;
   whether N4 accepts `ENC` or routes encoder graphics through DRA zones;
   real touch-strip + encoder panel resolution.
1. **Mouse report-id per opcode** — only config (`0x05`) and clock (`0x00`) are
   wire-witnessed; capture every opcode's report id (it lives inside the native
   `iot_driver`). And the exact BIT7 checksum sum-range (`0..=62` vs `1..=62`).
1. **Mouse settings round-trip on hardware** — DPI table `0x54` (8-stage, the
   8th-stage-blue/checksum collision), poll rate `0x04`, LOD/sensitivity/sleep/
   angle-snap/debounce/battery-LED in the `0x53` omnibus, profiles `0x05`,
   key/fn matrix `0x50`/`0x51`, macros `0x16` — implemented but unwitnessed.
1. **Keyboard report-id-0x00 generalisation** — only time-sync + battery are
   Frida-confirmed; the 64-byte `ReportId=0x04` `makeReport` builders (legacy
   zone RGB, single-key remap) are unverified on AK980 PRO. Verify each before
   changing `makeReport`.

## P2 — feature parity / honesty gaps

9. **Keyboard per-key RGB / macros / layers** decompile-only on AK980 PRO: wired
   192-byte blob (monochrome vs packed RGB), wireless 4-byte slot reserved byte,
   mode-byte offset 8-vs-9, `0x14` wired-192-vs-wireless-576 asymmetry.
1. **Mouse OLED extras** — OLEDWEATHER `0x2a`, OLEDLANGUAGE `0x27`, OLEDOPTION
   `0x22` byte layouts; SETTFTLCDDATA `0x25` custom-image panel geometry/chunk
   size (54-vs-56); GIF.
1. **AKP815** 100×100/Rot180 device-unconfirmed (probed); 5×3-vs-3×5 orientation;
   dedicated factory.
1. **AKP153 release-edge synthesis** (no byte-10 polarity → frame-diff needed).
1. **Mouse wired-vs-dongle** — does a wired AJ159 accept `0x28`/battery directly,
   or only via the dongle? Only the 2.4G 8K dongle path is verified.
1. **AJ199 dialect split** — OemDrv (17B) vs HIDUsb (20B) vs our 64B; the
   `0x3554:0xF500/0xF501` registry entries use the wrong dialect (PID-gated
   suspect). Needs per-(VID,PID,fw) dispatch + live AJ199 hardware.

## P3 — out of scope / deferred

15. **DFU / firmware update** (all families) — keyboard external
    `FirmwareUpdateTool.exe` (opcode unrecoverable); Stream Dock Allwinner-style
    `FirmwareUpgradeTool.exe` (`AIC.FW`/`aKDFU`, CBW/CSW, UU-device re-enum); mouse
    MLED `0x40/0x41/0xc0/0xc1` + OLED `0x30/0x31`. **Do NOT reimplement** (COD-031:
    no libusb in core); only detect VID/PID-disappearance → DFU → resume.
01. **Anti-features (DO NOT implement):** mouse `getWeather`/`watchSystemInfo`
    telemetry, `SET_CONTROLRECOIL 0x60` (no-recoil — anti-cheat liability),
    `SET_DOWNCOUNT 0x55` rapid-fire, vendor cloud login/analytics.
01. **AJ339 / AJ380** — no manifest, no device, never enumerated.

## Capture wishlist for Fedora / a fresh capture machine

- AKP05 image render on hidraw (P0.1) + AKP153 cross-check.
- Mouse `--enumerate`/`--battery`/`--clock` on Fedora (P0.2, P1.6/7).
- AK980 TFT test frame round-trip (P1.4) — needs the panel + a willing capture.
- AKP05 DRA/M_V/LOG USB capture (P1.5) — needs usbmon/usbipd on a Linux box, or
  a Frida hook of the Windows vendor app.
- AJ199 V1.0 + Max live capture for the dialect split (P2.14).
