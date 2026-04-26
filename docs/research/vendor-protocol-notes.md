<!--
  vendor-protocol-notes.md — RE task 2 deliverable (scaffold).

  Clean-room observations of the AJAZZ vendor protocols. Every entry
  must trace to a capture (USB / WebSocket / IPC / on-disk artefact)
  produced under controlled conditions, NOT to vendor source.

  Read `docs/research/README.md` for the rules. Critical ones to
  re-read:

    1. The single engineer who reads vendor sources for a given
       module writes the spec but does NOT contribute to the matching
       implementation file in src/. A second "clean" engineer
       implements from the spec. This rule is enforced socially —
       the commit logs of `src/devices/<x>/` and the corresponding
       section here MUST NOT share an author.
    2. Captures live in this repo only as **descriptions** (header
       summaries, byte-level layouts, state-machine notes). Raw
       captures (.pcap, .usbmon trace files, .json IPC dumps)
       belong in the encrypted out-of-repo vault, indexed here by
       capture id.
    3. Every entry carries a capture id like `cap-2026-04-26-akp03-001`
       so the vault index can be looked up after the fact.

  Until captures land, this file holds only the section skeleton +
  the methodology guide. Do not invent protocol details.
-->

# Vendor protocol — clean-room notes

Per-module protocol & feature inventory derived from controlled
captures of the AJAZZ first-party desktop apps. **Read
[`docs/research/README.md`](README.md) before contributing** — there
are hard clean-room and capture-vaulting rules.

> **Status — 2026-04-26**: scaffold + first static-analysis finding
> on the Stream Dock AJAZZ Mac bundle (see "Vendor app architecture"
> below). No USB / WebSocket runtime captures yet. The methodology
> section is canonical; per-device-family sections remain
> placeholders until those runtime captures land.

## Vendor app architecture — Qt 6 (static-analysis finding)

> **Capture id**: `static-2026-04-26-streamdock-mac-001`. Method:
> downloaded the `Stream Dock-AJAZZ-Installer_Mac_global.dmg`
> (sha256 `5e543caa98218187c7022516fda272ea05ef1f3a36b2e82ecefe045b7feeaef9`,
> md5 `dcbd35d954547369c6e6e530eef88dd3`, 282 152 918 bytes,
> Last-Modified `2024-02-01`) into ephemeral `/tmp/` storage on the
> Linux recon host, listed the DMG's top-level bundle layout with
> `7zz l`, recorded the framework names visible in the listing,
> hashed the source file, then deleted both the DMG and its working
> copy. **No application binaries were extracted, run, or
> decompiled** — only filesystem-level packaging metadata was read.

### Finding 1 — the Stream Dock app is a Qt 6 application

The macOS bundle layout shows 25 Qt frameworks under
`Stream Dock AJAZZ.app/Contents/Frameworks/`:

```
QtConcurrent QtCore QtCore5Compat QtDBus QtGui QtMultimedia
QtMultimediaWidgets QtNetwork QtOpenGL QtPdf QtPositioning
QtPrintSupport QtQml QtQmlModels QtQuick QtQuickWidgets
QtSerialPort QtSvg QtVirtualKeyboard QtWebChannel QtWebEngineCore
QtWebEngineWidgets QtWebSockets QtWidgets QtXml
```

This is an architectural confirmation, not a guess. Implications
for our parity strategy:

1. **Same toolkit family**: AJAZZ Control Center's Qt 6 / QML stack
   is structurally aligned with the vendor's. We are not wrapping
   a foreign technology, we are building an open-source
   alternative on the same toolkit. The two implementations should
   converge naturally on plug-points like `QWebChannel` for
   Property Inspectors.
1. **`QtWebChannel` + `QtWebEngineCore` + `QtWebEngineWidgets`**
   confirm the vendor app embeds HTML Property Inspector pages
   over the **exact same `$SD` / `QWebChannel` mechanism** we just
   shipped in `pi_bridge.cpp` (M1–M4). A plugin's PI HTML page
   that targets the vendor app should run in our embedding
   unmodified, modulo the host-side method surface.
1. **`QtWebSockets`** — the plugin host wire protocol is over a
   `QWebSocketServer` (or client; either direction). This validates
   the design in `docs/architecture/PLUGIN-SDK.md` ("line-delimited
   JSON over WebSocket" is the right abstraction; the underlying
   transport is what the vendor uses).
1. **`QtSerialPort`** — surprise dependency. Possible roles:
   firmware-update flow that reboots a device into USB-CDC
   bootloader mode (common pattern for STM32 / ESP32-class MCUs
   that AJAZZ uses), or non-HID device subclasses. Our codebase
   does NOT link `QtSerialPort` today; this is a parity gap. Track
   when the firmware-update recon (`vendor-techniques.md` § 2)
   captures a CDC handoff.
1. **`QtMultimedia` + `QtMultimediaWidgets`** — audio/video. Likely
   used for "play sound" actions, audio-level mapping (the AKP153
   user-manual mentions "map audio levels per button"), or video
   thumbnail previews in the action-picker UI. We have neither;
   this is a feature gap.
1. **`QtPdf`** — PDF rendering. Lower priority but could be: in-
   app help / changelog, or PDF export of profiles. Confirm
   purpose via runtime capture before duplicating.
1. **`QtVirtualKeyboard`** — embedded-grade soft keyboard. Probably
   bundled by `macdeployqt` defaults rather than actively used,
   but worth confirming.
1. **`QtCore5Compat`** — the codebase migrated from Qt 5 to Qt 6
   without dropping its Qt 5 idioms. Tells us nothing about
   protocol design but explains some style choices the recon may
   surface later.

### Finding 2 — bundle build dates suggest Qt 6.4.x / 6.5.x

Most framework binaries inside the bundle carry `mtime 2023-03-11` to `2023-03-12`, which lines up with the Qt 6.4.2
(2023-01) → 6.5.0 (2023-04) release window. The app shell itself
was rebuilt `2024-02-01` (matching the Last-Modified on the DMG).
Confirms the build is recent enough to assume a modern Qt 6 plug-
point set is available in the vendor app.

### Finding 3 — Windows build is Qt 5 (not Qt 6 like the macOS build)

> **Capture id**: `static-2026-04-26-streamdock-win-001`. Method:
> `curl` of `Stream-Dock-AJAZZ-Installer_Windows_global.exe` (sha256
> `005d18fbea74e393560431f167c12737b380687d544f0a48a25e73abda0354b5`,
> 121 620 400 bytes, Last-Modified `2024-01-29`) into ephemeral
> `/tmp/`, `rz-bin -I/-l/-i/-S` static metadata extraction, no
> instructions disassembled, then deletion. Tool: `rizin 0.7.4`
> (Fedora `dnf install -y rizin`, removed at session end).

The Windows installer is an **Advanced Installer** (Caphyon)
bootstrap (`dbg_file: C:\JobRelease\win\Release\stubs\x86\ExternalUi.pdb`,
GUID `CBF9B5150C964A1592535FCF30CB7B941`) wrapping an embedded MSI.
The bootstrap is i386 PE32, signed (cert chain not verified
yet). Outer DLL imports include `msi.dll`, `wininet.dll`,
`gdiplus.dll` — standard for an MSI bootstrap, no application logic.

The MSI's internal asset table (visible via `strings` on the
bootstrap, since Advanced Installer concatenates the cabinet
file-list into a sequence in the .rsrc section) reveals:

- **Qt is Qt 5, not Qt 6**: bundles `Qt5WinExtras.dll, Qt5Core.dll, Qt5Gui.dll, Qt5Widgets.dll, Qt5Network.dll, Qt5Qml.dll, Qt5Quick.dll, Qt5QuickWidgets.dll, Qt5QmlModels.dll, Qt5Svg.dll, Qt5Multimedia.dll, Qt5MultimediaWidgets.dll, Qt5SerialPort.dll, Qt5WebChannel.dll, Qt5WebEngineCore.dll, Qt5WebEngineWidgets.dll, Qt5WebSockets.dll, Qt5Xml.dll, Qt5PrintSupport.dll, Qt5Positioning.dll`. **`Qt5WinExtras.dll` is the smoking gun** —
  this module does not exist in Qt 6, so the Windows build is firmly
  on Qt 5.x. Implication: the vendor app is mid-migration from
  Qt 5 → Qt 6, with macOS already on Qt 6 (Finding 1) and Windows
  still on Qt 5. Our open-source stack is structurally more modern
  than the vendor's Windows build today.

- **The Stream Dock app is multi-process**:

  - `StreamDockAJAZZ.exe` — main UI app.
  - `QtWebEngineProcess.exe` — Qt WebEngine helper (standard).
  - `SystemMonitor.exe` — separate process for the system-monitor
    widgets (CPU / GPU / RAM tile data sources).
  - `FirmwareUpgradeTool.exe` — **firmware update is a SEPARATE
    EXECUTABLE**, not in-process. Strongly supports the hypothesis
    in Finding 1 that firmware update reboots the device into a
    USB-CDC bootloader and a dedicated tool (linked against
    `Qt5SerialPort.dll`) drives the flash sequence.
  - `ScreenCaptureTool.exe` — separate process for the
    `screen-capture` action type. May feed image data back into the
    main app via IPC (likely shared memory + WebSocket).
  - `SplashScreen.exe` — splash on launch.

- **Native libraries bundled** (selected — full inventory is the
  Advanced Installer file table inside the MSI):

  - `libusb1.0.dll` — vendor uses **libusb** for device I/O (same
    transport family our `hidapi` ultimately falls back to on
    Linux). Suggests they bypass HID-level abstraction for some
    operations, likely raw bulk transfers for image upload.
  - `OpenHardwareMonitorLib.dll` — feeds the
    `cpuTemperatureIndex.html` widget via the SystemMonitor.exe
    sub-process. We don't have this; if we want CPU/GPU widgets
    we likely need an equivalent abstraction (or use lm-sensors /
    Get-CimInstance / IOKit equivalents per OS).
  - `opencv_core4.dll, opencv_imgproc4.dll, opencv_highgui4.dll, opencv_imgcodecs4.dll, opencv_videoio4.dll` — **OpenCV 4** is
    bundled. Used for image processing (tile thumbnails, screen-
    capture region cropping / scaling, possibly real-time camera
    preview as a tile background). New gap; we use Qt's `QImage`
    today which is cheaper but less capable.
  - `libwebp.dll, libsharpyuv.dll, libwebpdecoder.dll` — WebP
    codec. Tiles can be authored as WebP, not just JPEG / PNG.
  - `libcrypto1_1x64.dll, libssl1_1x64.dll` — OpenSSL 1.1.x. Older
    Qt 5 default; we are on Qt 6 + system OpenSSL 3.x.
  - `D3Dcompiler_47.dll, libGLESv2.dll, libEGL.dll, opengl32sw.dll`
    — Qt Quick rendering on Windows uses ANGLE's GLES backend.
    Standard Qt-on-Windows pattern.

- **Action types — full vendor catalogue** (counted from the
  `btn_*.png/jpg` thumbnails embedded in the MSI):

  - `btn_back.png` — back to parent folder (we have this).
  - `btn_launch.jpg` — launch application (we have this).
  - `btn_custom_folder.png` — open folder (we have this).
  - `btn_custom_open_website.jpg` — open URL (we have this).
  - `btn_custom_trigger_hotkey.jpg` — keyboard hotkey (we have
    this via `KeyPress` action).
  - `btn_goToPage.png` — navigate to a specific profile page (we
    have folders, but not arbitrary page jumps).
  - `btn_media_eject.jpg` — eject media (new).
  - `btn_mouse_event.jpg` — synthesise mouse events (new).
  - `btn_password.png` — store / type a password (new).
  - `btn_play.png` — play sound (new — corroborates the
    QtMultimedia dependency).
  - `btn_switchProfile.jpg` — switch active profile (new — we
    have a single active profile model today).
  - `btn_text.png` — type literal text (new).
  - `btn_youtube_post_message.png` — YouTube live-chat integration
    (new and OEM-specific; low priority for parity).
  - `btn_duration.png` — countdown timer tile (new).
  - `calculatorAction.png` — calculator pop-up (new).
  - `browserBackAction.png` — browser-back hotkey (new).
  - `micAction.png` — microphone mute / unmute (new — useful for
    streamers).

- **Profile bundle format** — visible filenames `Favorite.streamDockProfile`
  and `WINDOWS.streamDockProfile` confirm the vendor bundle
  extension is `.streamDockProfile`. We use `.ajazzprofile` — the
  formats are not interchangeable today; opt-in import / export
  to `.streamDockProfile` is a parity item if we want
  cross-vendor profile portability.

- **Property Inspector HTML pages**: 11 numbered `index.html_*`
  files in the asset table, strongly suggesting one PI HTML page
  per built-in action type, exactly the embedding pattern our
  M1-M4 work supports. Confirms PI architecture compatibility.

- **Localisation**: `.json` per locale (ar / de / …) + `.pak`
  Chromium locale bundles for the WebEngine subprocess.
  Implementation note: our localisation strategy will need to
  cover the same locales when we ship plugin compat.

### Finding 4 — keyboard / mouse driver wrappers are a Delphi-style

toolchain

> **Capture ids**: `static-2026-04-26-aj199-001` (mouse driver,
> sha256 `0c8b3a0c4d31922cb0fb9daa14af58211a2939a2afc8113b041de3ecbdc7852c`,
> 2 334 568 bytes, mtime `2023-12-06`) and
> `static-2026-04-26-ak820max-001` (keyboard driver, sha256
> `d40fdeddce9d7f95c377578304cc9946da568004d764e39e8757a601c291cdd6`,
> 18 106 793 bytes, mtime `2024-11-20`).

Both AJ199 and AK820 Max RGB installers share a near-identical
PE structure:

| Property           | AJ199 Setup                                                                                   | AK820 Max RGB Installer                   |
| ------------------ | --------------------------------------------------------------------------------------------- | ----------------------------------------- |
| Class              | PE32, i386, Windows GUI                                                                       | PE32, i386, Windows GUI                   |
| Base address       | 0x00400000                                                                                    | 0x00400000                                |
| Calling convention | cdecl                                                                                         | cdecl                                     |
| Compiler / runtime | C (Borland-style; linker date 2009-08-15)                                                     | C (Borland-style; linker date 2018-06-14) |
| DLL imports        | `oleaut32, advapi32, user32, kernel32, comctl32` (5 libs)                                     | identical 5 libs                          |
| First imports      | `SysFreeString, SysReAllocStringLen, SysAllocStringLen` (Variant / BSTR APIs)                 | identical                                 |
| Signed             | false                                                                                         | false                                     |
| URLs in strings    | only `http://schemas.microsoft.com/SMI/2005/WindowsSettings` (Windows manifest XML namespace) | identical                                 |

The **5-DLL profile + Variant-API-first imports + i386 + cdecl +
unsigned** is the canonical fingerprint of a **Delphi-compiled
installer wrapper** (Borland C++ Builder 6 / Delphi 7 era). Both
binaries are built with the same toolchain. The "linker date
2009/2018" is not a real build date — it is an artefact of the
toolchain's stub timestamp, recently re-stamped at minimum.

Implication: these are **installer wrappers, not the actual
device-talking driver**. The real driver tool is embedded as a
resource and extracted at install time. Without unpacking the
resource (which crosses into vendor authorial content), this
recon ends here. To go further, a Windows VM is needed:

1. Run the installer in a sandboxed VM, observe what files it
   drops to `C:\Program Files\AJAZZ\` (or similar).
1. Static-analyse the dropped tools — their PE imports will
   show whether they use `hid.dll` (Windows HID API),
   `setupapi.dll`, `winusb.dll` (libusb-equivalent native), or a
   custom kernel driver `.sys`.
1. Capture USB transfers under `tshark + USBPcap` while the
   driver tool is running.

### Open questions surfaced by these findings

- The bundle has a `Contents/PlugIns/` directory at the top level —
  standard `QPluginLoader` location. Are the per-device backends
  (AKP153, AKP03, AKP05, …) shipped as Qt plugins inside this
  folder? If yes, their file names alone (without internals) will
  enumerate the device-family coverage at the binary layer. **Do
  NOT extract** the plugin internals; only enumerate the file names
  in a future capture.
- Mass of `Resources/` and `Frameworks/` is ~580 MB for a 282 MB
  installer → roughly 2× compression. Worth comparing against the
  Mirabox-branded build size (when its URL is recovered) to see
  whether the AJAZZ rebrand is a visual skin or a separate fork.
- The Windows installer (`PE32 executable for MS Windows 5.00 (GUI), Intel i386, 9 sections`, 32-bit) is **not** an NSIS or
  InnoSetup wrapper (`7zz l` cannot list it). Probably a custom
  installer compiled from MFC or similar — common for Chinese OEM
  driver shipping. Out of scope for this recon pass; needs an
  expanded analysis on a disposable Windows VM, with the explicit
  cleanup recorded per the methodology table above.

## Methodology

### Capture environments

Every capture must record the environment so it can be reproduced
or refuted. Required metadata for each capture:

| Field                | Example                                                                                                       | Why                                                                               |
| -------------------- | ------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| `capture-id`         | `cap-2026-04-26-akp03-001`                                                                                    | Stable handle for cross-references; date-prefixed so the timeline is recoverable. |
| `host-os`            | `Windows 11 Pro 23H2 (build 22631.3007)`                                                                      | Vendor app behavior changes with USB-stack version.                               |
| `vendor-app`         | `Stream-Dock-AJAZZ-Installer_Windows_global.exe` Content-MD5 `a1828628…` (see `vendor-software-inventory.md`) | Different build → different protocol.                                             |
| `device`             | `AJAZZ AKP03 / Mirabox N3 — VID 0x0300 PID 0x3001 — firmware fw-string-as-reported`                           | Some commands are firmware-gated.                                                 |
| `usb-stack`          | `Wireshark 4.4 + USBPcap 1.5.4.0` (Windows) / `usbmon` mod + `tshark` (Linux)                                 | Stack-specific framing.                                                           |
| `start-time`         | `2026-04-26T14:00:00Z`                                                                                        | Reorder captures by chronology.                                                   |
| `duration`           | `12 min`                                                                                                      | Sanity-check whether claims about "every event" are exhaustive.                   |
| `interaction-script` | "Boot vendor app, click 4 keys, rotate dial 30°, sleep + wake host."                                          | Without this, the capture cannot be reproduced.                                   |

### Tool checklist (no installations preserved on the host)

The recon host should be a disposable VM or a tools-removed-on-cleanup
workstation. The following toolchain is the recommended baseline; it
is provided as a checklist, NOT as a supply chain — install via OS
package manager, use, then remove with the matching uninstall step
recorded in the capture journal:

| Tool                              | Purpose                                                                                     | Install / remove                                                                                                  |
| --------------------------------- | ------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `wireshark` + `usbpcap` (Windows) | USB capture.                                                                                | `winget install WiresharkFoundation.Wireshark` / matching `winget uninstall …`.                                   |
| `tshark` + `usbmon` (Linux)       | USB capture without GUI.                                                                    | `dnf install wireshark-cli` / `dnf remove …` (Fedora) — `usbmon` is in-tree (`modprobe usbmon` / `rmmod usbmon`). |
| `asar` (npm)                      | Unpack Electron payloads.                                                                   | `npm install -g @electron/asar` / `npm uninstall -g @electron/asar`.                                              |
| `js-beautify` (npm)               | Reformat minified Electron renderer code for *reading* (never re-typing into our codebase). | `npm install -g js-beautify` / `npm uninstall -g js-beautify`.                                                    |
| `ghidra` or `radare2`             | Decompile native binaries (firmware, .dll glue).                                            | Vault-resident — install in disposable VM only.                                                                   |
| `ILSpy` / `dnSpyEx`               | Decompile .NET payloads.                                                                    | Vault-resident — install in disposable VM only.                                                                   |
| `signify` / `gpg`                 | Verify vendor download signatures.                                                          | Standard system tooling.                                                                                          |

For Linux hosts: any `dnf install` / `dnf remove` operation MUST be
recorded in the capture journal so the operator can revert the host
to a known-clean state. Per repo policy
([`feedback_no_system_mutations.md`](../../README.md)), no system-level
mutations should persist past the capture session.

### Protocol description shape

Each section below should follow this shape so downstream
implementers have a uniform contract to work against:

1. **Transport summary** — USB-HID? Custom HID interface? WebSocket
   over localhost? File-system poll?
1. **Endianness, framing, command set** — byte-level, with
   `report-id`, length, opcode, payload, terminator (where
   applicable). Always observed, never re-typed from vendor source.
1. **State machine** — handshake, steady-state, reconnect, sleep /
   wake, error recovery.
1. **Open questions** — list explicitly the things that recon could
   not verify with the captures listed. Implementers MUST treat
   these as defensive boundaries (validate-and-reject, never assume).
1. **Capture references** — link every observation to its
   `capture-id`. A statement without a capture-id is a guess and
   should not be in the doc.

## Stream Dock — `streamDock` IPC + USB-HID

> **Status — 2026-04-26**: not yet captured. Existing OSS work
> (`opendeck-akp03`, `opendeck-akp153`, `mirajazz`) covered the
> AKP03 + AKP153 USB-HID surface from independent recon — those
> independent results inform `src/devices/streamdeck/` already.
> The vendor desktop app's WebSocket layer (`localhost:port`,
> Stream Deck SDK-2 dialect) and its plugin lifecycle have NOT
> been captured under our methodology. The Plugin SDK doc
> (`docs/architecture/PLUGIN-SDK.md`) is our spec; this section
> will track the deltas observed by recon.

Sub-sections to fill once captures land:

- `streamDock` WebSocket — connection handshake, auth (if any),
  event dialect (Stream Deck SDK-2 superset).
- AKP03 / AKP153 / AKP05 USB-HID — already-documented protocol
  cross-checks (see `docs/protocols/streamdeck/*.md`); new entries
  here only when the vendor app uses commands the OSS does not.
- AKP815 (with screen) USB-HID — not yet supported in our backend.
- Firmware update flow — observed delivery of the firmware blob
  - the subset of HID reports used to apply it. **High value**:
    vendor knows how to recover from a half-applied flash; capture
    the retry / rollback path before any of our own DFU work starts.

## Keyboards — proprietary protocol

> **Status — 2026-04-26**: not yet captured. Existing
> `src/devices/keyboard/` proprietary backend already covers
> AK-series RGB zones + macro upload, derived from independent
> recon. Vendor app's macro-record format and HE actuation curve
> (AK820 Max HE / AK680 Max) are unverified by our captures.

Sub-sections to fill:

- AK980 PRO — protocol mapping (currently scaffolded only).
- AK820 / AK820 Pro / AK820 Max RGB — macro encoding, layer
  switching, RGB curve format.
- AK680 Max (HE) — magnetic-switch analog actuation curve format.
- Wireless tri-mode — pairing handshake on the 2.4 GHz dongle.

## Mice — AJ-series proprietary

> **Status — 2026-04-26**: not yet captured. AJ199 / AJ159 / AJ339
> already enumerated and DPI / RGB working via independent recon.

Sub-sections to fill:

- AJ199 family (No-RGB / Max / Carbon Fibre) — DPI stage encoding,
  RGB ramp, polling-rate selector.
- AJ339 / AJ380 — confirm whether protocol matches AJ199 family;
  protocol parity matrix ends up in
  [`vendor-feature-matrix.md`](vendor-feature-matrix.md).
- Battery level + sleep timeout — not yet exposed by our
  `IMouseCapable` interface.

## Plugin SDK — `space.key123.vip/StreamDock/plugins`

The plugin store API surface is documented at
`https://sdk.key123.vip/en/guide/overview.html` and we already
implement the `productInfo/list` catalogue endpoint via
`src/app/src/streamdock_catalog_fetcher.cpp`. This section will host
the deltas a recon pass surfaces (per-plugin signed bundle layout,
Sigstore-equivalent verification, install / uninstall / update
state machine).

## How to add a section

1. Pick a `capture-id` and run captures end-to-end on a disposable
   host. Record the metadata table at the top of the section.
1. Write the protocol summary in your own words, using the shape
   above. Cite every byte-level claim with the capture id.
1. Open a PR. The PR author MUST NOT also be the one who will
   implement the matching `src/devices/...` module — flag this in
   the PR description so the implementer is a different engineer.
1. Once merged, file a TODO entry per gap surfaced (parity backlog,
   per RE task 4 in `TODO.md`).
