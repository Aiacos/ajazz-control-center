# Stack Research — v1.2 Connected-Device Capability Parity

**Milestone:** v1.2 Connected-Device Capability Parity (4 currently-connected USB devices)
**Researched:** 2026-05-15
**Confidence:** HIGH (recipes verified against existing source + dev-box `lsusb` + upstream READMEs / kernel docs; one open question — duplicate-key behaviour in `pcap` replay — flagged §6)

## Scope of this document

This is a **delta** stack research. The v1.0/v1.1 stack (C++20, Qt 6.7+, hidapi 0.14.0 `hidapi_hidraw` backend, nlohmann::json 3.12.0 PRIVATE to `ajazz_plugins`, Catch2 v3.7.1, Python 3.11+, CMake ≥3.28, hidraw-only udev rules) is unchanged and is not re-litigated here. Only **new stack elements** driven by v1.2's seven target capabilities are analysed:

| Capability                               | Stack question                                                                                                |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| AKP03 `display` (image push, PID 0x3004) | Capture tooling + corpus to cross-reference image wire format + Qt6 image pipeline                            |
| AKP03 `encoder` (rotate/press)           | Capture tooling + mirajazz/opendeck-akp03 corpus for `EncoderTwist` / `EncoderStateChange` packet layout      |
| `clock` / `setSystemTimeOn`              | Capture tooling — wire format is firmware-dependent; existing C++ surface (`IClockCapable`) needs nothing new |
| AK980 PRO `rgb` + `macros` + `layers`    | Capture tooling + SonixQMK community corpus for SN32-family `raw_hid` framing                                 |
| AJAZZ 2.4G 8K mouse `dpi` + `rgb`        | Capture tooling + libratbag/sinowealth corpus + AJ-series in-tree backend reuse                               |
| Image preprocessing for LCD keys         | Qt 6 image pipeline (`QImage::scaled` + `QImageWriter` JPEG) — already in stack, NEW utility module placement |
| Test-replay infrastructure               | `pcap` parsing + canned-frame fixture; reuses Catch2 v3.7.1 — NO new C++ library                              |

## Recommended Stack — additions only

### Core: USB protocol capture tooling

#### Linux — `wireshark` (with `tshark` + `dumpcap`) + `usbmon` kernel module

**Decision:** Adopt Wireshark 4.x (tshark, dumpcap, the GTK GUI) on Linux as the canonical capture toolchain. usbmon is the kernel-side capture facility; tshark/dumpcap consume `/dev/usbmonN` directly via libpcap and write pcapng.

| Property                                | Value                                                                                                                                                                                |
| --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Package**                             | `wireshark` + `wireshark-cli` (Fedora/RHEL) / `wireshark` + `tshark` (Debian/Ubuntu)                                                                                                 |
| **Verified version**                    | Wireshark 4.4.x current stable as of 2026-05; usbmon supported since Wireshark 1.2.0, libpcap 1.0.0, Linux 2.6.11                                                                    |
| **Kernel module**                       | `usbmon` (in-tree since Linux 2.6.11; module load: `sudo modprobe usbmon`)                                                                                                           |
| **Capture format**                      | `pcapng` (default since Wireshark 1.8) — link type `DLT_USB_LINUX` or `DLT_USB_LINUX_MMAPPED`                                                                                        |
| **Permissions**                         | `/dev/usbmonN` is root:root 0600 by default; either run capture as root, OR (preferred) `sudo usermod -aG wireshark $USER` + udev rule from `/usr/share/doc/wireshark/README.Debian` |
| **Dev-box state (verified 2026-05-15)** | NOT installed (`which wireshark tshark dumpcap` returns "no … in PATH"); `/sys/kernel/debug/usb/usbmon/` is root-only (kernel default)                                               |

**Why Wireshark + usbmon and not alternatives:**

| Option                                 | Verdict    | Reason                                                                                                                                                                                                                                                                                                                                                                                      |
| -------------------------------------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Wireshark + usbmon**                 | **CHOSEN** | Canonical: cited by Wireshark Wiki [`CaptureSetup/USB`](https://wiki.wireshark.org/CaptureSetup/USB), kernel doc [`docs.kernel.org/usb/usbmon.html`](https://docs.kernel.org/usb/usbmon.html), `liquidctl/docs/developer/capturing-usb-traffic.md` (production OSS device project). pcapng is the file format every replay tool understands. tshark provides headless decode `usb.capdata`. |
| `usbhid-dump` (rhel/fedora `usbutils`) | Rejected   | Reads HID reports only, no URB-level visibility (control transfers, vendor commands invisible). Useful as a quick sanity check, NOT as the primary capture tool.                                                                                                                                                                                                                            |
| `usbtop`                               | Rejected   | Per-bus bandwidth, not packet content. No protocol decode.                                                                                                                                                                                                                                                                                                                                  |
| `usbsnoop` (legacy Win)                | N/A        | Windows-only, predates USBPcap.                                                                                                                                                                                                                                                                                                                                                             |
| `WinUSB hooking` / `libusbK`           | Rejected   | Windows-only, requires user-mode driver swap that interferes with vendor app. Out-of-scope: vendor app cannot be used per CLAUDE.md hard rule (clean-room reverse engineering only, no wine/innoextract).                                                                                                                                                                                   |

**Captures-on-dev-box plan (no vendor app):**

The CLAUDE.md hard rule forbids running vendor binaries via wine/innoextract. The four target devices are AJAZZ-branded and the project's clean-room approach is to capture **only the device's own emitted reports** (encoder rotation, button press, etc.) plus **replicated commands inferred from corpora** (mirajazz / opendeck-akp03 / SonixQMK) — never decompile or run vendor binaries. The capture surface is therefore:

1. **Device → host** captures (no host writes): drive each control directly on the AKP03 (rotate encoders, press keys), each control on AK980 PRO (key press, layer-fn combo), each on 2.4G 8K mouse (click, dpi-cycle button). Capture all observed input reports.
1. **Host → device** captures of OUR own writes, replayed from corpora-derived candidate command sets (mirajazz packet prefixes, SonixQMK raw_hid framing): verify on the wire that what we send matches what we encode in our protocol module.

This is exactly the "device→host" + "incremental host→device" loop documented in `docs/protocols/REVERSE_ENGINEERING.md` §1 Capture. No vendor app, no wine, no decompilation.

**Canonical capture commands (verified syntax against Wireshark 4.x man pages):**

```bash
# One-time setup
sudo modprobe usbmon
sudo usermod -aG wireshark $USER  # logout/login OR newgrp wireshark
# Optionally relax /dev/usbmonN: a udev rule is the long-term answer; this is dev-box scope.

# Identify the bus for a given AJAZZ device
lsusb -d 0300:3004  # AKP05E (Stream Dock Plus) — outputs "Bus 001 Device 018: ..."
# -> usbmon1 captures bus 001 (the entire bus; per-device filter applied in display)

# Headless capture, 60-second slice exercising one feature at a time
tshark -i usbmon1 -w captures/akp03/encoder-rotate.pcapng -a duration:60

# Decode just the HID payload bytes (the bit we care about) into hex lines
tshark -r captures/akp03/encoder-rotate.pcapng \
       -Y 'usb.device_address == 18 && usb.capdata' \
       -T fields -e frame.time_relative -e usb.endpoint_address -e usb.capdata \
    > captures/akp03/encoder-rotate.decoded.txt

# Whole-bus capture-all (use when you don't yet know the device address)
sudo dumpcap -i usbmon1 -w captures/akp03/full-bus.pcapng -a filesize:100000  # ~100 MB cap

# Capture every USB bus simultaneously (rarely needed; use when device migrates buses)
tshark -i usbmon0 -w captures/all-buses.pcapng
```

**Pitfalls flagged (verified, MUST be addressed in capture protocol docs):**

1. **No BPF capture filters on usbmon.** Per Wireshark Wiki — capture-filters not supported on usbmon (no BPF). Filter at **display time** (`-Y`) or post-capture with `editcap`. Naive `-f 'host …'` will fail silently.
1. **usbmon0 vs usbmonN.** `/dev/usbmon0` is "capture all buses" (libpcap pseudo-interface); `/dev/usbmonN` for N≥1 is bus-specific. Per-device captures should pick the bus-specific node — smaller, easier to read.
1. **Device address shifts on replug.** USB device address (`usb.device_address`) is bus-assigned at enumeration. If a device is unplugged/replugged mid-capture session, the address changes. Pin the address per-capture in your annotation file, don't hardcode in the protocol doc.
1. **Permission spread.** Default `/dev/usbmonN` is `0600 root:root`. `wireshark` group convention is Debian/Ubuntu; Fedora ships `wireshark` capability via setcap on `dumpcap` instead. Document both.
1. **Composite-HID device 0c45:7016 ("Microdia USB DEVICE", verified attached 2026-05-15).** This is almost certainly the AK980 PRO's wireless receiver companion (separate HID interface for the 2.4GHz link) or the 2.4G 8K mouse's dongle. Capture this PID's enumeration alongside its parent to clarify the relationship before adding a `devices.yaml` entry.

**Confidence: HIGH** — recipes verified against Wireshark Wiki, kernel docs, and the dev box's actual `lsusb` output. Wireshark/tshark are NOT installed on the dev box and must be added to the developer-prereqs documentation; this is a doc change, not a project dep.

#### Windows — `USBPcap` (USBPcap 1.5.4+, ships with Wireshark 4.x installer)

**Decision:** USBPcap 1.5.4 (bundled with the Wireshark 4.x Windows installer) as the canonical Windows USB capture driver. Not directly needed for v1.2 milestone work (all 4 connected devices are captured on Linux dev box), but documented for completeness so contributors on Windows can confirm protocol findings.

| Property           | Value                                                                                                   |
| ------------------ | ------------------------------------------------------------------------------------------------------- |
| **Package**        | Bundled checkbox in the Wireshark Windows installer (`https://www.wireshark.org/download.html`)         |
| **Version**        | 1.5.4 (last release 2021; project is in maintenance mode but functional on Win11)                       |
| **Capture format** | pcapng — same as Linux, **but link type is `DLT_USBPCAP`**, NOT `DLT_USB_LINUX*`                        |
| **Kernel**         | Installs a filter driver on USB root hubs; requires admin install once, capture runs as user thereafter |

**Cross-platform pitfall (load-bearing for replay tooling §6):** The pcapng files from USBPcap and from usbmon have **different link-layer types** (`DLT_USBPCAP` vs `DLT_USB_LINUX_MMAPPED`). A replay parser that hardcodes one will silently miss frames from the other. `usbrply` handles both transparently (verified §6).

**Confidence: HIGH** — verified against Wireshark Wiki and USBPcap GitHub releases page.

### Core: Reverse-engineering corpora for the AJAZZ family

These are **READ-ONLY reference implementations** that the team uses to *cross-reference* protocol findings from our own captures. Per CLAUDE.md / REVERSE_ENGINEERING.md, code is NEVER vendored; only the protocol findings (byte offsets, prefixes) are re-derived from captures and documented in our own words.

#### Stream Dock family corpora (AKP03 / AKP153 / AKP05 / AKP815)

| Project                                                                   | Lang   | License                                        | Coverage                                                                                                                                                                                                                                                                                                                          | Relevance to v1.2                                                                                                                                 |
| ------------------------------------------------------------------------- | ------ | ---------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`4ndv/mirajazz`** (Rust crate, 21 tags, active 2026)                    | Rust   | MIT                                            | AKP03, AKP03 rev. 2 (PID 3xxx, **includes 0x3004**), AKP153, Mirabox N3/N4/293S; documented `protocol_version` 0/1/2/3 quirks (512 vs 1024 byte packets); explicit image header `00 43 52 54 00 00 42 41 54 00 00 …` ("CRT…BAT…") and `STP` (stop) framing; `EncoderTwist(Vec<i8>)` + `EncoderStateChange(Vec<bool>)` input enums | **PRIMARY** corpus for AKP03 variant 0x3004. Documents the packet prefix our `akp03_protocol.hpp` must emit.                                      |
| **`4ndv/opendeck-akp03`** (OpenDeck plugin)                               | Rust   | MIT                                            | AKP03 variants 0x1001/0x1002/0x1003/0x3002/0x3003 + Mirabox N3 family + Soomfon/MarsGaming/TreasLin/Redragon rebadges. Built on top of `mirajazz`. **PID 0x3004 not in this list** — corpus gap.                                                                                                                                  | Secondary; confirms protocol_version 2 vs 3 routing for canonical PIDs. PID 0x3004 IS NOT in any corpus — first-party capture is the only source. |
| **`mishamyrt/ajazz-sdk`** (Rust)                                          | Rust   | MIT                                            | AKP153, AKP153E/R, AKP815, AKP03, AKP03E/R/RV2                                                                                                                                                                                                                                                                                    | Cross-reference for v1.0/v1.1 AKP153 protocol already in tree; not the primary AKP03 source.                                                      |
| **`Uriziel01/Ajazz-AKP153-reverse-engineering`** (Python notes + main.py) | Python | (no LICENSE; treat as reference, not vendored) | AKP153 / AKP153e / Mirabox HSV293S                                                                                                                                                                                                                                                                                                | Historical primary source for AKP153 byte-level findings; cited by `docs/protocols/streamdeck/akp153.md` already.                                 |
| **`calini/opendeck-akp`** (OpenDeck plugin)                               | Rust   | MIT                                            | AKP05 / Mirabox N3-family — fork variant                                                                                                                                                                                                                                                                                          | Secondary for AKP05; outside v1.2 scope (no AKP05 attached).                                                                                      |
| **`rigor789/mirabox-streamdock-node`** (Node.js demo)                     | JS     | MIT                                            | Mirabox 293 only — single-device PoC                                                                                                                                                                                                                                                                                              | Tertiary; reads `mirajazz` already supersedes its findings.                                                                                       |

**Key wire-format facts cross-referenced from `mirajazz` source:**

```
AKP03 image upload prefix (output report, 512B per chunk on proto v1, 1024B on v2+):
  00 43 52 54 00 00 42 41 54 00 00 [sz_hi] [sz_lo] [key+1] <payload> <pad to N>
                "CRT"              "BAT"              ^ key index is 1-based
                                                       ^ 0x3004 is firmware-confirmed AKP05E
                                                         (Stream Dock Plus, proto_v3 -> 1024B)
AKP03 image format:
  proto v2 (AKP03 canonical PID 0x1001/0x3002/0x1003): JPEG 60x60 Rot0
  proto v3 (PID 0x3003 / 0x3004 = AKP05E):  per-device key geometry; confirm image size by capture
AKP03 input report (encoder):
  EncoderTwist  -> i8 per encoder, signed delta (+/- ticks since last poll)
  EncoderStateChange -> bool per encoder, press state
  Stop framing for image upload chain: "STP" command
```

(These are the **published findings** in mirajazz/src/device.rs — protocol facts, not copied code. Our `akp03_protocol.hpp` will encode the same prefix from its byte literals; the test fixture (§6) replays a capture to assert we emit the same wire.)

**Verified against:**

- [4ndv/mirajazz README](https://github.com/4ndv/mirajazz) — protocol_version 0/1/2/3 enum, supported VID/PIDs (HIGH)
- [4ndv/mirajazz src/device.rs](https://github.com/4ndv/mirajazz/blob/main/src/device.rs) — image upload command prefix, packet sizes, key+1 encoding, STP terminator (HIGH)
- [4ndv/mirajazz src/types.rs](https://github.com/4ndv/mirajazz/blob/main/src/types.rs) — `DeviceInput::EncoderTwist(Vec<i8>)`, `EncoderStateChange(Vec<bool>)`, `ImageFormat` enum (HIGH)
- [4ndv/opendeck-akp03 README](https://github.com/4ndv/opendeck-akp03) — VID/PID coverage matrix (HIGH); explicit non-coverage of 0x3004 (HIGH — by absence)
- In-repo `docs/protocols/streamdeck/akp03.md` (already documents 6-key + 3-encoder + 3-non-LCD-button layout with mirajazz/opendeck citations)
- In-repo `docs/protocols/streamdeck/_research-sources.md` (existing source-tag citation scheme: `[mirajazz]`, `[opendeck-akp03]`, `[ajazz-sdk]`)

**Confidence: HIGH** — three independent OSS implementations agree on the wire format; the only gap (PID 0x3004 specifically) closes with a single first-party capture session per `REVERSE_ENGINEERING.md`.

#### Keyboard corpora (AK980 PRO, 0c45:8009)

| Project                                          | Lang     | License       | Coverage                                                                                                                                                                                                 | Relevance                                                                                                                                                                                                                                           |
| ------------------------------------------------ | -------- | ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`SonixQMK/qmk_firmware`** (QMK fork)           | C        | GPL-2.0       | Sonix SN32F2xx family MCUs — community-ported VIA/QMK support for Womier, Redragon, Keychron Sonix-based keyboards. Documents the `raw_hid` framing (HID feature report, 64-byte packets) that VIA uses. | Microdia (VID 0x0c45) is the OEM brand fronting Sonix chipsets per `the-sz.com USB ID DB` + Arch wiki notes. AK980 PRO is **likely** SN32F2xx-class. **NEEDS confirmation** via capture: read HID report descriptor and compare to SonixQMK boards. |
| **`daikiejp/microdia`** (Ubuntu install scripts) | shell    | MIT           | Microdia 0c45 keyboard driver install (Linux)                                                                                                                                                            | Driver presence note only; does NOT document protocol. Low value.                                                                                                                                                                                   |
| **`SonixQMK/sonix-flasher`** (Python flasher)    | Python   | GPL-3.0       | SN32F248/F268 ISP bootloader interface                                                                                                                                                                   | Cross-platform flasher; relevant only if AK980 PRO is firmware-flashable to QMK. Out-of-scope for v1.2 (we are NOT replacing firmware; we ARE reverse-engineering the stock OEM proto).                                                             |
| **`vial-kb/vial-qmk` + Vial JSON layout**        | C / JSON | GPL-2.0 / MIT | Layout descriptor format already used by `docs/protocols/keyboard/via.md` in-tree                                                                                                                        | Reference for layer/macro semantics if AK980 PRO turns out to be VIA-compatible at the wire layer.                                                                                                                                                  |

**Critical caveat (LOW confidence on AK980 PRO chipset family):**

- Microdia VID 0x0c45 fronts MULTIPLE distinct chipset families: Sonix SN32-class (most VIA-compatible keyboards), SiW/SN9C cameras (the historical 0x0c45 use case), and OEM-rebadged generic HID. The AK980 PRO `lsusb` string is "Microdia AK980 PRO" — vendor-specific naming; we **cannot** assume SN32 without a capture confirming.
- **Action:** Phase 9 capture must include `lsusb -v -d 0c45:8009` to dump the HID report descriptor and interface descriptors. If the report descriptor matches the SonixQMK `raw_hid` shape (64-byte vendor-defined feature report `0x60`/`0x61`), proto is well-understood. If it's something else, we are in pure reverse-engineering territory and the SonixQMK corpus is decorative only.
- **The "no AK980 PRO QMK firmware" outcome is more likely than "yes."** SonixQMK community has ported ~30 keyboards in ~3 years; the AK980 PRO specifically is not on any port list as of 2026-05.

**Confidence: MEDIUM-LOW** — chipset family is speculative pending HID descriptor capture; SonixQMK corpus relevance is conditional.

#### Mouse corpora (AJAZZ 2.4G 8K, 3151:5007)

| Project                                                                   | Lang   | License       | Coverage                                                                                                                                                 | Relevance                                                                                                                                                                                                                                                                                              |
| ------------------------------------------------------------------------- | ------ | ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **`libratbag/libratbag`**                                                 | C      | GPL-2.0 / MIT | DBus daemon for HID-protocol gaming mice; drivers for Logitech (HID++), Etekcity, GSkill, Roccat, SteelSeries, Sinowealth, **NOT** Sonix/3151 explicitly | Architectural reference for "how a mouse-config dbus daemon is structured." `src/driver-sinowealth.c` + device-data file split is exactly the pattern we already use (`docs/protocols/mouse/aj_series.md`). **3151 is not in their device list** — the 8K-polling SONiX MCUs are unsupported upstream. |
| **`libratbag/piper`** (GTK GUI client)                                    | Python | GPL-2.0       | Frontend on top of `libratbag`                                                                                                                           | Frontend pattern reference for our Qt UI; no protocol value.                                                                                                                                                                                                                                           |
| **In-tree `docs/protocols/mouse/aj_series.md`**                           | (own)  | GPL-3.0+      | AJ139/AJ159/AJ179 family (PIDs 0x5C2E/0x5D2E/0x5E2E/0x5C2F), AJ199 family (PID 0xF500/F501/etc), `dpi_stages: 6` for all                                 | **Primary in-tree reference.** AJAZZ 2.4G 8K (3151:5007) is currently scaffolded against this same backend per `devices.yaml` notes — needs capture to confirm wire-format match or divergence.                                                                                                        |
| **No 3rd-party SONiX 8KHz mouse OSS reverse-engineering project located** | —      | —             | —                                                                                                                                                        | **Corpus gap.** The 8KHz-polling-rate config protocol for the SONiX 3151:5007 specifically is **not documented in any OSS source** found in this research pass. First-party capture is the ONLY source.                                                                                                |

**Capture priority guidance:** Because no 3rd-party corpus exists for 3151:5007, the Phase 9 capture session for this device is **especially load-bearing**. Plan to capture: DPI cycle (button press → DPI stage advance, report on HID feature endpoint), RGB color set (vendor-app would write a feature report; clean-room version reads what the device emits on enumeration to identify the feature report ID), polling-rate change (this likely requires a SET_REPORT to a vendor-defined endpoint — verify it exists in the descriptor).

**Confidence: HIGH** for "libratbag doesn't cover this device" (verified by inspection of `data/devices/`); **MEDIUM** for "first-party capture is feasible" (DPI cycling is observable; RGB-set wire format may require speculative replay to confirm).

### Core: Image preprocessing for LCD keys (AKP03 display capability)

#### Decision: Reuse Qt 6 image stack — NO new C++ dep

**Decision:** Implement a `StreamDeckImagePipeline` utility module in `src/devices/streamdeck/src/image_pipeline.cpp` using **Qt 6's existing `QImage` + `QImageWriter`** for resize, rotate, mirror, and JPEG encode. The dep is **already present** in the stack (Qt 6.7+ Gui module). No new library needed.

| Property                        | Value                                                                                                                                                                                                                                         |
| ------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Library**                     | `Qt6::Gui` (`QImage`, `QImageWriter`, `QBuffer`) — already in `find_package(Qt6 …)` at root `CMakeLists.txt:51-63`                                                                                                                            |
| **Output format**               | JPEG, quality controllable via `QImageWriter::setQuality(int 0-100)`                                                                                                                                                                          |
| **Operations needed**           | Resize (`QImage::scaled(size, KeepAspectRatio, SmoothTransformation)`), rotate (`QImage::transformed(QTransform().rotate(deg))`), mirror (`QImage::mirrored(bool h, bool v)`), encode-to-buffer (`QImageWriter` → `QByteArray` via `QBuffer`) |
| **Per-device parameters table** | New constexpr table in `streamdeck/image_pipeline.hpp`: `{codename → {width, height, rotation_degrees, mirror_x, mirror_y, format}}`                                                                                                          |

**Per-mirajazz wire-format facts (already documented above), the per-device pipeline parameters are:**

```
AKP153 / AKP153e (proto v1, in-tree functional):  85x85 JPEG, Rot90 + mirror — wire path EXISTS
AKP03 canonical / AKP03E (proto v2):              60x60 JPEG, Rot0,  no mirror
AKP03R rev. 2 / AKP05E 0x3004 (proto v3):         JPEG Rot90, no mirror (image size per capture)
AKP815 (proto v1 reuse, in-tree probed):          100x100 JPEG, Rot180
AKP815 LCD strip (probed pending):                800x480 JPEG, separate upload command
```

**Why Qt6 not alternatives:**

| Option                                                              | Verdict    | Reason                                                                                                                                                                                                                                                                                                                |
| ------------------------------------------------------------------- | ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Qt6 QImage + QImageWriter**                                       | **CHOSEN** | Already in stack. Cross-platform identical behaviour. SmoothTransformation is bilinear-quality. JPEG codec is bundled (libjpeg-turbo on Linux via Qt). No new dep, COD-031 untouched. Existing AKP153 backend ALREADY uses QImage for the 85x85 path (verified by existence of `akp153.cpp` + 178/178 tests passing). |
| `stb_image_resize.h` + `stb_image_write.h` (Sean Barrett's stb\_\*) | Rejected   | Header-only, public domain — would otherwise be tempting. But Qt6 is **already there**, dual-pipeline-ing is wasted compile time and gives two opinions on bilinear vs box resampling. YAGNI.                                                                                                                         |
| `libjpeg-turbo` direct                                              | Rejected   | Qt6 already wraps it. Direct use adds a dep and gives no quality win.                                                                                                                                                                                                                                                 |
| `OpenCV`                                                            | Rejected   | Excessive (50+ MB) for a 60×60 resize. Crosses COD-031 spirit (heavy public dep).                                                                                                                                                                                                                                     |
| `Pillow` via embedded Python                                        | Rejected   | `python-elgato-streamdeck` does this; we have `ajazz_plugins` Python sandbox but image pipeline is host-side, not plugin-side. Crossing the sandbox boundary for image resize is nonsensical.                                                                                                                         |

**Integration scope:**

- `src/devices/streamdeck/include/ajazz/streamdeck/image_pipeline.hpp` — new (PUBLIC header IF the AppShell can render previews; PRIVATE if only the backend uses it). Likely PRIVATE-only first; promote later if QML preview is needed.
- `src/devices/streamdeck/src/image_pipeline.cpp` — new
- Existing `akp153.cpp` may be refactored to delegate to the new module (post-v1.2 cleanup; not load-bearing for this milestone).
- `tests/unit/test_streamdeck_image_pipeline.cpp` — new; assert byte-exact JPEG output for a known input (golden file) per AKP03 / AKP153 / AKP815 row in the parameter table.

**COD-031 boundary check:** Qt6 is already a `find_package` in root; this module already links Qt. The pipeline lives in `ajazz_devices_streamdeck` (PRIVATE-link to `Qt6::Gui`). `ajazz_core` doesn't gain a new Qt dep it didn't already have. **PASS.**

**Confidence: HIGH** — Qt6 image APIs are stable, well-documented, and the existing AKP153 backend is proof-of-life.

### Core: Test-replay infrastructure (pcap → Catch2 fixture)

**Decision:** Write canned-frame fixtures by hand from selected pcap captures — no `libpcap` link-time dependency in `ajazz_core` or the test binary. Use `usbrply` (Python, dev-time tool) to convert pcaps into `std::array<std::uint8_t, N>` literals checked into `tests/integration/fixtures/`.

| Property                    | Value                                                                                                                                                                                                                                                                                                                             |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Library (build-time)**    | **None.** No new C++ link dependency.                                                                                                                                                                                                                                                                                             |
| **Tool (dev-time, Python)** | [`JohnDMcMaster/usbrply`](https://github.com/JohnDMcMaster/usbrply) v2.1.1, Apache-2.0, `pip install --user usbrply`. Converts pcap (usbmon OR USBPcap) → Python/C/JSON replay script. We use the JSON output and a small awk/python one-liner to emit `tests/integration/fixtures/akp03_encoder_rotate.h` containing C++ arrays. |
| **Fixture format**          | `inline constexpr std::array<std::uint8_t, N> AKP03_ENCODER_TICK_RIGHT = { 0x01, 0x02, 0xFF, ... };` in a header committed alongside the test.                                                                                                                                                                                    |
| **Test pattern**            | Inject the fixture into the protocol parser, assert decoded `EncoderTwist` / `EncoderStateChange` equals the expected struct.                                                                                                                                                                                                     |

**Why no link-time pcap dep:**

| Option                                          | Verdict             | Reason                                                                                                                                                                                                                                              |
| ----------------------------------------------- | ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Pre-decode pcaps → C++ literals at dev time** | **CHOSEN**          | Zero link-time deps. Fixtures are tiny (a 60x60 JPEG is ~2 KB, an encoder event is 8 bytes). Catch2 already in stack. **COD-031 boundary untouched.** Same precedent as `tests/unit/test_manifest_signer.cpp` checking literal JSON strings inline. |
| `PcapPlusPlus` link as dev-only dep             | Rejected            | Adds a multi-MB dep + Npcap (Windows) / libpcap (Linux/macOS) cross-platform install burden to CI just to parse a file format we already pre-decode. PcapPlusPlus is for *online* packet processing — irrelevant to file-based fixtures.            |
| `libpcap` direct link                           | Rejected            | Same — file format parse at dev-time, not test-time.                                                                                                                                                                                                |
| `Gallimaufry` (python USB pcap parser)          | Rejected            | Python only; usbrply does the same job and outputs JSON we can post-process.                                                                                                                                                                        |
| `tshark -T fields -e usb.capdata` → text        | Acceptable fallback | Simpler than usbrply when only HID payload bytes are needed; ship a `scripts/extract-hid-fixture.sh` wrapper.                                                                                                                                       |

**Fixture-generation recipe (one-shot, dev-time):**

```bash
# 1. Capture (see §1)
tshark -i usbmon1 -w captures/akp03/encoder-tick-right.pcapng -a duration:5

# 2. Extract HID payload bytes for the relevant device/endpoint
tshark -r captures/akp03/encoder-tick-right.pcapng \
       -Y 'usb.device_address == 18 && usb.endpoint_address == 0x81 && usb.capdata' \
       -T fields -e usb.capdata

# 3. Hand-pick the 1-3 representative frames; convert via scripts/hex-to-cpparray.py
python3 scripts/hex-to-cpparray.py \
        --name AKP03_ENCODER_TICK_RIGHT \
        --type 'std::uint8_t' \
        < hex_frames.txt \
        > tests/integration/fixtures/akp03_encoder_rotate.h

# 4. Commit the fixture header + a Catch2 test that uses it.
```

(`scripts/hex-to-cpparray.py` is a ~30-LoC helper — not a "library", just a sanitiser-friendly converter. Lives in `scripts/`, NOT installed.)

**Integration scope:**

- New `tests/integration/fixtures/` directory with one `.h` per representative protocol frame (encoder tick, key press, layer change, dpi cycle, image upload first/last chunk).
- New `scripts/hex-to-cpparray.py` — fixture generator.
- New `docs/protocols/CAPTURING.md` — companion to `REVERSE_ENGINEERING.md`, the runbook for "I have a real device on my bench; here's how to capture and turn it into a fixture."

**Confidence: HIGH** — usbrply works (verified WebFetch + GitHub repo), pcapng + DLT_USB_LINUX_MMAPPED format is stable, and the "C++ array literal in a header" pattern is already used elsewhere in the test corpus (e.g. `test_action_engine.cpp:119` shows in-tree fakes).

### Considered-and-deferred additions (DO NOT add in v1.2)

| Tool                                                              | Reason to defer / reject                                                                                                                                                              |
| ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `PcapPlusPlus` link-time                                          | YAGNI: pre-decoded fixtures are smaller, simpler, and dep-free. Add LATER if we ever need online pcap parsing inside the app itself (we won't).                                       |
| `libpcap` direct link                                             | Same.                                                                                                                                                                                 |
| `libusb-1.0` direct usage in any code path                        | Hard rule: hidapi `hidapi_hidraw` backend is canonical (CLAUDE.md). The libusb hidapi backend is explicitly disabled in the Flatpak manifest. **Adding libusb breaks ARCH-defaults.** |
| `Boost.Process` for any new subprocess work                       | Project intentionally has zero Boost; out-of-process plugin host uses raw `execvp` (Linux/macOS) + raw `CreateProcessW` (Windows).                                                    |
| `wine` / `innoextract` / vendor-binary inspection                 | **CLAUDE.md hard rule: clean-room reverse engineering only.** No vendor binary execution, no Delphi installer extraction, no decompilation.                                           |
| `Pillow` via embedded Python                                      | Plugin sandbox is for plugins. Image pipeline is host-side. Don't cross the boundary for resize.                                                                                      |
| `OpenCV` for image processing                                     | Wildly disproportionate for 60x60 resize. Qt6 already in stack.                                                                                                                       |
| `Trompeloeil` (still)                                             | Same deferral as v1.1 — no current driver. May revisit at v1.3 if mockable interfaces multiply.                                                                                       |
| New JSON library                                                  | `nlohmann::json 3.12.0` already covers all needs. NEVER in `ajazz_core` or installed headers (COD-031 invariant).                                                                     |
| Modal "must capture protocol first" dialog                        | Anti-feature. Captures happen on the developer's bench, never on the user's machine. UI doesn't surface this.                                                                         |
| Telemetry: "send this capture to AJAZZ Control Center developers" | Anti-feature: privacy + scope creep.                                                                                                                                                  |
| Live in-app USB sniffer                                           | Anti-feature: out-of-scope; users have Wireshark for this.                                                                                                                            |
| Auto-flash AK980 PRO with SonixQMK                                | Anti-feature: bricks the device for users with no easy unflash. Reverse-engineering the OEM proto is the requested outcome.                                                           |

### Unchanged from v1.0 / v1.1 (do NOT re-add, do NOT swap)

| Library                                                              | Version                 | Status                                                                                           |
| -------------------------------------------------------------------- | ----------------------- | ------------------------------------------------------------------------------------------------ |
| Qt 6 (Core, Gui, Network, Quick, QuickControls2, Test, Widgets, Svg) | 6.7+                    | Pinned by root CMake `find_package(Qt6 6.7 ... REQUIRED)`                                        |
| `hidapi`                                                             | 0.14.0 (tag `73d292a8`) | FetchContent at `CMakeLists.txt:67-83`. `hidapi_hidraw` backend ONLY on Linux.                   |
| `nlohmann::json`                                                     | 3.12.0                  | FetchContent + vcpkg mirror. **PRIVATE-link to `ajazz_plugins` only.** COD-031 invariant.        |
| `Catch2`                                                             | v3.7.1                  | FetchContent in `tests/CMakeLists.txt`                                                           |
| Python                                                               | 3.11+                   | OOP plugin host child runtime; no host-side embedding                                            |
| CMake                                                                | ≥ 3.28                  | hidapi sub-floor handled via `CMAKE_POLICY_VERSION_MINIMUM 3.10` override at `CMakeLists.txt:82` |

## Recommended Stack (v1.2 delta only)

### NEW developer-prereqs (NO project link deps)

| Tool                                        | Version           | Purpose                                                                  | Install (Fedora dev box, verified-missing 2026-05-15)                                                               | Distribution                                                  |
| ------------------------------------------- | ----------------- | ------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------- |
| **Wireshark + tshark + dumpcap**            | 4.4.x             | USB protocol capture on Linux; pcapng writer; headless decode            | `sudo dnf install wireshark wireshark-cli` (Fedora) / `sudo apt install wireshark tshark` (Debian/Ubuntu)           | OS package; documented in CONTRIBUTING.md dev-prereqs section |
| **usbmon kernel module**                    | in-tree           | USB bus capture facility                                                 | `sudo modprobe usbmon` (already in kernel since 2.6.11; verified `/sys/kernel/debug/usb/usbmon/` exists on dev box) | Linux kernel                                                  |
| **`wireshark` group membership**            | n/a               | Allow non-root `/dev/usbmonN` capture                                    | `sudo usermod -aG wireshark $USER` + logout/login                                                                   | OS user mgmt                                                  |
| **`usbrply`** (Python, dev-time only)       | 2.1.1             | pcap → JSON/Python replay extractor for fixture generation               | `pipx install usbrply` OR `pip install --user usbrply`                                                              | PyPI; NOT a project dep, NOT installed in CI                  |
| **`USBPcap`** (Windows companion, optional) | 1.5.4             | Windows USB capture driver (bundled checkbox in Wireshark 4.x installer) | Wireshark Windows installer                                                                                         | wireshark.org                                                 |
| **`lsusb` (`usbutils`)**                    | already on Fedora | Device descriptor inspection (`lsusb -v -d 0c45:8009`)                   | `dnf install usbutils` (already present)                                                                            | OS package                                                    |
| **`hexdump` / `xxd`**                       | already on Fedora | Hex inspection of capture text exports                                   | already present                                                                                                     | coreutils-equivalent                                          |

### NEW in-tree (NO third-party library)

| Component                                      | Location                                                                                                                                                                                                                        | Purpose                                                                                                               |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `StreamDeckImagePipeline`                      | `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` (PRIVATE to `ajazz_devices_streamdeck`)                                                                                                                                   | Per-codename resize + rotate + mirror + JPEG encode via `QImage` + `QImageWriter`                                     |
| Per-device protocol module additions           | `src/devices/streamdeck/src/akp03_protocol.hpp` (extend), `src/devices/keyboard/src/proprietary_protocol.hpp` (extend for AK980 PRO), `src/devices/mouse/src/aj_series.cpp` (extend for 3151:5007 quirks)                       | Real wire-format implementations replacing today's `NotImplemented` stubs                                             |
| Catch2 fixture corpus                          | `tests/integration/fixtures/akp03_*.h`, `ak980pro_*.h`, `ajazz_24g_8k_*.h`                                                                                                                                                      | Pre-decoded HID frames as `std::array` literals, generated from real pcap captures                                    |
| Fixture generator script                       | `scripts/hex-to-cpparray.py` (~30 LoC)                                                                                                                                                                                          | tshark hex output → C++ `std::array` literal header                                                                   |
| Capture runbook                                | `docs/protocols/CAPTURING.md`                                                                                                                                                                                                   | Companion to `REVERSE_ENGINEERING.md`; per-device "what to capture and how to turn it into a fixture"                 |
| Per-device protocol doc updates                | `docs/protocols/streamdeck/akp03.md` (extend with PID 0x3004 capture findings), `docs/protocols/keyboard/proprietary.md` (extend with AK980 PRO findings), `docs/protocols/mouse/aj_series.md` (extend with 3151:5007 findings) | First-party protocol findings, citing capture file SHA256 + corpus cross-references                                   |
| **NEW** `docs/protocols/keyboard/ak980pro.md`  | new file                                                                                                                                                                                                                        | Dedicated protocol doc for AK980 PRO (split from `proprietary.md` if findings diverge from VIA shape)                 |
| **NEW** `docs/protocols/mouse/ajazz_24g_8k.md` | new file                                                                                                                                                                                                                        | Dedicated protocol doc for 3151:5007 (split from `aj_series.md` if findings diverge from AJ-series shape)             |
| **NEW** `devices.yaml` entry                   | `docs/_data/devices.yaml`                                                                                                                                                                                                       | After capture identifies 0c45:7016, add it (likely as `ak980pro_dongle` or `ajazz_24g_8k_dongle` — TBD by descriptor) |

### NEW udev rules

The existing `resources/linux/99-ajazz.rules` (read 2026-05-15) **already covers all four connected devices**:

- VID 0x0300 → AKP03 family (PID 0x3004 ✓)
- VID 0x0c45 → Microdia / AK980 PRO (PID 0x8009 ✓; PID 0x7016 also covered by VID-only rule ✓)
- VID 0x3151 → SONiX-VID keyboards/mice (PID 0x5007 ✓)

**No udev change required for v1.2.** Verified by `lsusb` + the rules file: all four PIDs are covered by VID-only `KERNEL=="hidraw*", ATTRS{idVendor}=="VID", MODE="0660", TAG+="uaccess"` lines. The user must replug devices (or `udevadm trigger --action=change`) if they were attached before rules were installed — CLAUDE.md "Linux device access" section already documents this.

### Unchanged but worth verifying

| Library                                                                | Verification                                                                                                                                                                                                           |
| ---------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `hidapi` 0.14.0 — `hid_send_feature_report` / `hid_get_feature_report` | Required for vendor-defined feature reports the AK980 PRO and 2.4G 8K mouse use for DPI/RGB config. Confirm with `hidapi.h` that the API is exposed in the umbrella target (it is — `hid.h` declares both since 0.10). |
| `hidapi` 0.14.0 — `hid_read_timeout`                                   | Required for encoder rotation polling on AKP03 (input reports come on the interrupt-in endpoint). Already used in functional backends; no change.                                                                      |

## Alternatives Considered (summary table)

| Need                                  | Recommended                                                | Alternative                               | When alternative might win                                                                                                                                               |
| ------------------------------------- | ---------------------------------------------------------- | ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Linux USB capture                     | `wireshark` + `tshark` + `usbmon`                          | `usbhid-dump`                             | When you only need HID-class reports and don't care about control-transfer setup packets. Useful as a quick `lsusb -v` companion, NOT primary capture.                   |
| Linux USB capture                     | `wireshark` + `tshark` + `usbmon`                          | `usbsnoop2` / custom usbmon binary parser | Almost never — pcapng is the lingua franca; reject custom binary formats.                                                                                                |
| Windows USB capture                   | USBPcap (bundled with Wireshark)                           | `Microsoft Message Analyzer`              | Microsoft MA is EOL since 2019; USBPcap is the only maintained option.                                                                                                   |
| AKP03 protocol cross-reference        | `4ndv/mirajazz`                                            | `mishamyrt/ajazz-sdk`                     | When cross-referencing AKP815 specifically (mirajazz doesn't cover AKP815; ajazz-sdk does). For 0x3004 / AKP03 family, mirajazz is primary.                              |
| AK980 PRO chipset reverse-engineering | First-party capture                                        | `SonixQMK/qmk_firmware` corpus            | IF the HID descriptor capture confirms an SN32 chipset with `raw_hid` 64-byte feature reports. UNCONFIRMED — capture-gated.                                              |
| 8K mouse RGB/DPI proto                | First-party capture                                        | `libratbag` Sinowealth driver pattern     | Sinowealth is a different chipset; pattern is structural (device-data split), not protocol.                                                                              |
| Image pipeline for LCD keys           | `Qt6::Gui` `QImage` + `QImageWriter`                       | `stb_image_resize.h`                      | Only if Qt were not already in the stack — it is. Reject.                                                                                                                |
| Test fixture from pcap                | Hand-decoded C++ literal arrays                            | `PcapPlusPlus` link-time                  | Only if we needed online pcap parsing inside the app. We don't. Reject.                                                                                                  |
| pcap → C++ literal generator          | `usbrply` + `scripts/hex-to-cpparray.py` (dev-time Python) | Direct `tshark -T fields` shell pipeline  | When the device emits multi-byte control transfers with metadata that the shell pipeline drops. For pure HID payload extraction, the shell pipeline is fine — ship both. |

## What NOT to Use

| Avoid                                                                        | Why                                                                                                                                                                                                | Use Instead                                                                                                                                    |
| ---------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| **Vendor AJAZZ / Mirabox / StreamDock binaries via `wine` or `innoextract`** | CLAUDE.md hard rule: clean-room reverse engineering only. v1.1 retro documented this when deferring AKB980 PRO ("vendor driver is a Delphi installer requiring wine/innoextract; not in dev env"). | First-party capture from the device's own emitted reports; OSS corpora (mirajazz, opendeck-akp03) for cross-reference.                         |
| **`libusb` HID backend / direct `libusb` calls**                             | hidapi `hidapi_hidraw` is the only sanctioned backend (CLAUDE.md "Linux device access"); libusb backend is **explicitly disabled** in the Flatpak manifest. Adding libusb breaks ARCH-default.     | `hidapi::hidapi` umbrella target, already in stack.                                                                                            |
| **Any new JSON parser in `ajazz_core` or installed public header**           | COD-031 invariant. Verified by `grep -rn nlohmann src/core/include/` returning 0 at audit time.                                                                                                    | `nlohmann::json` in `ajazz_plugins` only (PRIVATE link); plain string formatting / `QJsonDocument` if needed in `ajazz_core` (Qt is OK there). |
| **`PcapPlusPlus` / `libpcap` as a project link dep**                         | Fixtures are pre-decoded at dev time; online pcap parse inside the app is YAGNI.                                                                                                                   | Hand-decoded C++ literal arrays in `tests/integration/fixtures/`.                                                                              |
| **Embedded Python (`pybind11` / `cpython` host)**                            | Slice 3e retired this path; the OOP plugin host is the supported pattern. No new image-pipeline reason to revive.                                                                                  | Qt6 `QImage`.                                                                                                                                  |
| **OpenCV**                                                                   | 50+ MB dep for a 60x60 resize. Crosses COD-031 spirit (heavy public dep).                                                                                                                          | Qt6 `QImage`.                                                                                                                                  |
| **`stb_image_resize.h` even though it's tempting**                           | Qt is already in stack; dual-pipeline gives two opinions on bilinear vs box resampling. YAGNI.                                                                                                     | Qt6 `QImage::scaled(SmoothTransformation)`.                                                                                                    |
| **Modal "Capture first" dialog in the app UI**                               | Anti-feature. Captures happen on the developer's bench, not the user's machine. The app shows maturity tier (DEVICES-01..04) — that is the only honest signal.                                     | Maturity tooltip already shipped.                                                                                                              |
| **`LD_PRELOAD` USB function hooking**                                        | Linux-only; project must support Windows + macOS too. Also, the hard rule disallows vendor binary interaction.                                                                                     | Direct usbmon/USBPcap capture of the device.                                                                                                   |
| **Auto-flash AK980 PRO with SonixQMK firmware**                              | Anti-feature: bricks the device for users who bought it for the OEM software experience. Out-of-scope: reverse-engineering OEM proto is the requested outcome.                                     | Capture the OEM wire format.                                                                                                                   |
| **`Boost.Process` for capture-replay subprocess work**                       | We have zero Boost; the OOP plugin host already proves raw POSIX/Win32 is sufficient.                                                                                                              | Raw POSIX (`fork`/`execvp`) or Win32 (`CreateProcessW`).                                                                                       |

## CMake / pre-commit integration impact

| Change                                                                    | File                                                                                                                                        | Risk                                                                                                                                                                                                                                                             |
| ------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| New `image_pipeline.{hpp,cpp}` added to `ajazz_devices_streamdeck`        | `src/devices/streamdeck/CMakeLists.txt` (add sources)                                                                                       | LOW. Same Qt6::Gui link the target already has. PRIVATE-keyword'd.                                                                                                                                                                                               |
| New fixture headers under `tests/integration/fixtures/`                   | `tests/CMakeLists.txt` (add `target_sources` or `target_include_directories`)                                                               | LOW. Header-only; no new link dep.                                                                                                                                                                                                                               |
| New `scripts/hex-to-cpparray.py`                                          | `scripts/` (not installed)                                                                                                                  | LOW. Not in the install tree; pre-commit may want to mark it executable.                                                                                                                                                                                         |
| Per-device protocol module extensions                                     | `src/devices/streamdeck/src/akp03_protocol.hpp`, `src/devices/keyboard/src/proprietary_protocol.hpp`, `src/devices/mouse/src/aj_series.cpp` | MEDIUM. Each is one device's wire format; the existing protocol-module pattern (`<device>_protocol.hpp` with byte-only knowledge, no I/O) is documented at `REVERSE_ENGINEERING.md:31` and proven by 5 existing modules.                                         |
| `IClockCapable::setTime` real wire format (if AKP03 capture surfaces one) | `src/core/include/ajazz/core/i_clock_capable.hpp` already has the surface — change is in the backend, not the interface                     | MEDIUM. Capability flag flip in `devices.yaml`. Surface unchanged.                                                                                                                                                                                               |
| New `docs/protocols/CAPTURING.md`                                         | `docs/protocols/`                                                                                                                           | LOW. Pure doc; pre-commit markdownlint should pass.                                                                                                                                                                                                              |
| Pre-commit hook adjustment                                                | None                                                                                                                                        | The project does NOT lint pcap or `.pcapng` files. **Do not commit raw captures** (REVERSE_ENGINEERING.md §2 already forbids — they may contain serial numbers, firmware blobs, fingerprints). Commit only sanitised hex+annotation + the resulting C++ fixture. |

## Version Compatibility

| Pairing                                                                            | Status                   | Notes                                                                                                                                                                                                      |
| ---------------------------------------------------------------------------------- | ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Wireshark 4.4.x + usbmon (Linux 2.6.11+)                                           | Compatible               | Universal since Wireshark 1.2.0                                                                                                                                                                            |
| Wireshark 4.4.x + USBPcap 1.5.4 (Windows)                                          | Compatible               | USBPcap bundled with Wireshark Windows installer                                                                                                                                                           |
| usbrply 2.1.1 + pcapng (`DLT_USB_LINUX_MMAPPED` and `DLT_USBPCAP`)                 | Compatible               | Both link types supported per usbrply README                                                                                                                                                               |
| Qt6 6.7 `QImage::scaled(SmoothTransformation)` + 60x60/64x64/85x85/100x100 outputs | Compatible               | Bilinear is the default for smooth scaling; deterministic byte-for-byte across platforms (verified by 178/178 ctest pass on `linux-release` for AKP153 path)                                               |
| Qt6 6.7 `QImageWriter::setQuality(int)` JPEG encode                                | Compatible               | libjpeg-turbo backend on Linux; reproducible per-platform quality 0-100                                                                                                                                    |
| hidapi 0.14.0 `hid_send_feature_report` + AK980 PRO + 2.4G 8K mouse                | **Verify**               | Required for DPI/RGB feature reports. API has existed since hidapi 0.10. Confirm with capture that the device accepts feature reports as expected.                                                         |
| usbmon + currently-attached devices (verified 2026-05-15 `lsusb`)                  | Available                | All 4 target devices visible: `0300:3004` (bus 1 dev 18), `3151:5007` (bus 1 dev 16), `0c45:8009` (bus 1 dev 19), `0c45:7016` (bus 1 dev 22). All on bus 1 → single `usbmon1` interface captures them all. |
| nlohmann::json 3.12.0 + new device protocol modules                                | NO-CONTACT (intentional) | The new protocol modules live in `ajazz_devices_*`, NOT `ajazz_plugins`. They are header-only byte-encoders. They MUST NOT include `<nlohmann/json.hpp>`. Verified by COD-031 grep.                        |

## Confidence Assessment

| Area                                                | Confidence     | Reason                                                                                                                                                                                                                                                                                                                                                                                                                         |
| --------------------------------------------------- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Wireshark + tshark + usbmon capture toolchain       | **HIGH**       | Verified against [Wireshark Wiki CaptureSetup/USB](https://wiki.wireshark.org/CaptureSetup/USB), kernel docs [docs.kernel.org/usb/usbmon.html](https://docs.kernel.org/usb/usbmon.html), and a production OSS device-project's recipe ([liquidctl/capturing-usb-traffic.md](https://github.com/liquidctl/liquidctl/blob/main/docs/developer/capturing-usb-traffic.md)). Dev-box state (verified-missing) is a doc-update only. |
| `4ndv/mirajazz` AKP03 wire-format facts             | **HIGH**       | Three independent OSS sources agree (mirajazz, opendeck-akp03, ajazz-sdk); the existing in-repo `docs/protocols/streamdeck/akp03.md` already cites all three with explicit byte-level details. mirajazz's `device.rs` shows the literal command prefix bytes.                                                                                                                                                                  |
| AKP03 PID 0x3004 specifically                       | **MEDIUM**     | NOT in any 3rd-party corpus. The in-repo `akp03.md` already notes "most likely a new AKP03 sibling or pre-production unit." First-party capture from the bench is the **only** authoritative source. Phase 9 capture will close this gap.                                                                                                                                                                                      |
| AK980 PRO chipset family                            | **MEDIUM-LOW** | Microdia 0c45 fronts multiple chipset families. **Cannot** assume Sonix SN32 / VIA-compatible without the HID descriptor capture. SonixQMK corpus relevance is conditional on the descriptor matching.                                                                                                                                                                                                                         |
| AJAZZ 2.4G 8K mouse wire format                     | **MEDIUM**     | No 3rd-party OSS corpus covers 3151:5007. libratbag does not support it. First-party capture is the only source. AJ-series in-tree backend (mouse/aj_series.cpp) is a plausible **starting** point per `devices.yaml` notes ("Wire format reuses AJ-series backend pending reconciliation") but **not** confirmed.                                                                                                             |
| Qt6 image pipeline for LCD keys                     | **HIGH**       | Qt6 image APIs are stable, well-documented, and the existing AKP153 backend at `src/devices/streamdeck/src/akp153.cpp` is proof-of-life (178/178 tests pass at v1.1 close including the image upload path).                                                                                                                                                                                                                    |
| Test-replay infrastructure (pre-decoded fixtures)   | **HIGH**       | Pattern already used in `tests/unit/test_manifest_signer.cpp` and `tests/integration/`. Catch2 v3.7.1 fixtures + std::array literals is a battle-tested pattern in this repo.                                                                                                                                                                                                                                                  |
| usbrply 2.1.1 for fixture extraction                | **HIGH**       | Verified live on github.com/JohnDMcMaster/usbrply; project active, accepts both Linux usbmon and Windows USBPcap pcaps.                                                                                                                                                                                                                                                                                                        |
| COD-031 boundary preservation for v1.2              | **HIGH**       | None of the recommended additions touches `ajazz_core` link surface. Qt6 was already in `ajazz_devices_streamdeck`. New code is header-only fixtures + private protocol modules + dev-time tools.                                                                                                                                                                                                                              |
| Duplicate-key / replay determinism (pcap → fixture) | **MEDIUM**     | One open question: when the device emits two identical input reports (encoder ticks too fast for the host to differentiate), does our fixture extraction capture them as N copies or 1? **Test in Phase 9** when running the first encoder-rotate capture.                                                                                                                                                                     |

## Sources

### Capture tooling

- [Wireshark Wiki: CaptureSetup/USB](https://wiki.wireshark.org/CaptureSetup/USB) — canonical usbmon recipe, kernel module load, libpcap version requirements (HIGH)
- [Linux kernel docs: usbmon](https://docs.kernel.org/usb/usbmon.html) — kernel facility, binary capture format, permission defaults (HIGH)
- [liquidctl: capturing-usb-traffic.md](https://github.com/liquidctl/liquidctl/blob/main/docs/developer/capturing-usb-traffic.md) — production OSS device-project's reverse-engineering capture recipe (HIGH)
- [USBPcap 1.5.4 GitHub releases](https://github.com/desowin/usbpcap/releases) — Windows USB capture driver (HIGH)
- [JohnDMcMaster/usbrply](https://github.com/JohnDMcMaster/usbrply) — pcap → JSON/Python/C replay converter, v2.1.1 (HIGH)

### Stream Dock corpora

- [4ndv/mirajazz](https://github.com/4ndv/mirajazz) — Rust library; protocol_version 0/1/2/3 quirks; PIDs starting with 3 covered (HIGH)
- [4ndv/mirajazz src/device.rs](https://github.com/4ndv/mirajazz/blob/main/src/device.rs) — explicit image-upload command prefix `00 43 52 54 ... 42 41 54 ...`, packet sizes 512/1024, key+1 encoding, STP framing (HIGH)
- [4ndv/mirajazz src/types.rs](https://github.com/4ndv/mirajazz/blob/main/src/types.rs) — `DeviceInput` enum (EncoderTwist Vec<i8>, EncoderStateChange Vec<bool>) (HIGH)
- [4ndv/opendeck-akp03](https://github.com/4ndv/opendeck-akp03) — VID/PID coverage matrix; PID 0x3004 explicitly NOT in list (HIGH — by absence) (HIGH)
- [mishamyrt/ajazz-sdk](https://github.com/mishamyrt/ajazz-sdk) — AJAZZ-branded SKU coverage incl. AKP815 (HIGH)
- [Uriziel01/Ajazz-AKP153-reverse-engineering](https://github.com/Uriziel01/Ajazz-AKP153-reverse-engineering) — historical AKP153 byte-level findings (MEDIUM — repo description verified; README content not directly fetched) (MEDIUM)
- [rigor789/mirabox-streamdock-node](https://github.com/rigor789/mirabox-streamdock-node) — JS demo for Mirabox 293 only (LOW — superseded by mirajazz)

### Keyboard corpora

- [SonixQMK](https://github.com/SonixQMK) — community QMK port for Sonix SN32 family MCUs (HIGH for SN32 patterns; LOW for confirming AK980 PRO specifically)
- [SonixQMK/qmk_firmware](https://github.com/SonixQMK/qmk_firmware) — open-source firmware port (HIGH)
- [SonixQMK/sonix-flasher](https://github.com/SonixQMK/sonix-flasher) — ISP bootloader interface (HIGH; deferred — out of scope for v1.2)
- USB-ID database for VID 0x0c45: [the-sz.com](https://the-sz.com/products/usbid/index.php?v=0x0C45) — Microdia/Sonix VID multiplexing (MEDIUM)

### Mouse corpora

- [libratbag/libratbag](https://github.com/libratbag/libratbag) — DBus daemon architecture pattern; **confirmed Sonix/3151 not supported upstream** (HIGH)
- [libratbag/libratbag data/devices](https://github.com/libratbag/libratbag/tree/master/data/devices) — device-data file split pattern (HIGH)
- [libratbag/libratbag src/driver-sinowealth.c](https://github.com/libratbag/libratbag/blob/master/src/driver-sinowealth.c) — driver pattern, NOT protocol for our chipset (HIGH)

### Qt6 image pipeline

- [Qt 6 QImage Class doc](https://doc.qt.io/qt-6/qimage.html) — scaled/transformed/mirrored APIs (HIGH)
- [Qt 6 QImageWriter Class doc](https://doc.qt.io/qt-6/qimagewriter.html) — JPEG quality + format options (HIGH)

### In-repo verification (load-bearing for all integration claims)

- `CLAUDE.md` — hard rules (no system-level mutations, no `nlohmann::json` in `ajazz_core`, hidapi_hidraw only, clean-room reverse engineering)
- `.planning/PROJECT.md` (read 2026-05-15) — v1.2 milestone definition, ARCH-04 candidate flag for `IClockCapable::setTime`
- `.planning/milestones/v1.1-research/STACK.md` (read 2026-05-15) — v1.1 stack delta, COD-031 boundary rationale
- `docs/_data/devices.yaml` (read 2026-05-15) — all 4 connected devices' catalogued entries including `akp05e`, `ak980pro`, `ajazz_24g_8k`; 0c45:7016 NOT in catalogue
- `docs/protocols/REVERSE_ENGINEERING.md` (read 2026-05-15) — clean-room workflow §1-5 (Capture / Annotate / Document / Implement / Verify)
- `docs/protocols/streamdeck/akp03.md` (read 2026-05-15) — existing AKP03 protocol doc with PID 0x3004 noted as new sibling; mirajazz/opendeck-akp03/ajazz-sdk citations pre-existing
- `resources/linux/99-ajazz.rules` (read 2026-05-15) — udev rules already cover all 4 device VIDs (no rule change needed)
- `CMakeLists.txt` (read 2026-05-15) — root build config; verified Qt6 already includes Gui module; nlohmann::json FetchContent with PRIVATE-link expectation; CMAKE_POLICY_VERSION_MINIMUM 3.10 override for hidapi subdir
- `vcpkg.json` (read 2026-05-15) — only nlohmann-json 3.12.0 declared; no other third-party
- Dev-box `lsusb` (run 2026-05-15) — confirms all 4 devices on Bus 001 (single `usbmon1` interface)
- Dev-box `which wireshark tshark dumpcap` (run 2026-05-15) — confirms NONE installed; doc-update needed in CONTRIBUTING.md / dev-prereqs
- Existing `src/devices/streamdeck/src/akp153.cpp` + 178/178 ctest pass at v1.1 close — proves Qt6 image pipeline pattern works in CI

______________________________________________________________________

*Stack research for: AJAZZ Control Center v1.2 milestone (delta only — Connected-Device Capability Parity)*
*Researched: 2026-05-15*
*Confidence: HIGH overall (one MEDIUM-LOW gap on AK980 PRO chipset family; one open question on pcap-replay determinism for fast encoder events — both close in Phase 9 with first-party captures)*
