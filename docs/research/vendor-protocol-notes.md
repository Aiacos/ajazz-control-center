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
- ~~The Windows installer is not an NSIS or InnoSetup wrapper.~~
  **Resolved by Finding 5 below** (2026-04-29 pass): the Windows
  Stream Dock installer is an Advanced Installer / Caphyon EXE
  bootstrap — see `static-2026-04-29-streamdock-win-002`.

### Finding 5 — Stream Dock Win: full Authenticode + version + manifest

> **Capture id**: `static-2026-04-29-streamdock-win-002`. Method:
> recon host = Windows 11 Pro 26200, the same `Stream-Dock-AJAZZ-Installer_Windows_global.exe` artefact (sha256 `005d18fbea74e393560431f167c12737b380687d544f0a48a25e73abda0354b5`,
> Content-MD5 `a182862811703e09582a009a6a9a6a90`, 121 620 400 bytes,
> Last-Modified `2024-01-29`) downloaded fresh from the vendor URL.
> Hash matches `static-2026-04-26-streamdock-win-001` byte-for-byte
> — vendor has not rotated the artefact between probes. New analysis
> dimensions: `Get-AuthenticodeSignature`, PE resource extraction
> via `7z x` (extracts `.rsrc`, `CERTIFICATE`, and the PE-in-PE
> outer-bootstrap payload `[0]`), VersionInfo via `(Get-Item).VersionInfo`.
> No instructions disassembled, no installer executed.

The bootstrap is an **Advanced Installer / Caphyon** EXE wrapping an
opaque, encrypted, 119 852 856-byte PE-in-PE payload (MZ-magic,
listed by 7z as `[0]`). The payload is **not** a recognisable MSI /
CAB / ZIP / 7z archive — Advanced Installer applies its own custom
container format which `7z` 23.01 cannot decode. **Implication**:
the Qt 5 binaries enumerated by Finding 3 (Qt5Core.dll, Qt5Gui.dll,
…, StreamDockAJAZZ.exe, FirmwareUpgradeTool.exe, …) cannot be
extracted by static methods; their contents are reachable only by
running the installer in a VM (see
[`vendor-recon-runbook-windows.md`](vendor-recon-runbook-windows.md) §
"Stream Dock — admin install on disposable VM").

**Authenticode signature (NEW dimension)**:

| Field            | Value                                                                                                     |
| ---------------- | --------------------------------------------------------------------------------------------------------- |
| Status           | `Valid`                                                                                                   |
| Signer subject   | `CN="Shenzhen An Rui Xin Technology Co., Ltd.", O="Shenzhen An Rui Xin Technology Co., Ltd.", S=Guangdong Sheng, C=CN, OID.2.5.4.15=Private Organization, OID.1.3.6.1.4.1.311.60.2.1.3=CN, SERIALNUMBER=91440300057893313F` |
| Signer issuer    | `CN=Sectigo Public Code Signing CA EV R36, O=Sectigo Limited, C=GB`                                      |
| Signer validity  | `2023-10-30` → `2024-10-29`                                                                              |
| Signer thumbprint | `D7ED891C6EE663028E63995051E9C916D15B3E54`                                                              |
| Signer serial    | `409BF6BB5B53D09F16D755DF5D16D2AE`                                                                       |
| Timestamp authority | `CN=Globalsign TSA for MS Authenticode Advanced - G4, O=GlobalSign nv-sa, C=BE`                       |
| Timestamp thumbprint | `31030E176AA4592EAB2C8BADE83299FCB5585DCF`                                                          |

**Cross-vendor entity tree** — three distinct identities surfaced
across the artefacts captured to date:

| Identity                                          | Surfaces in                       | Role                                  |
| ------------------------------------------------- | --------------------------------- | ------------------------------------- |
| `Shenzhen An Rui Xin Technology Co., Ltd.` (深圳安瑞鑫科技) | Stream Dock Authenticode signer   | Legal entity holding the EV cert      |
| `HotSpot`                                         | Stream Dock VersionInfo CompanyName + Aliyun OSS bucket prefix `hotspot-oss-bucket` | Internal team / brand for the desktop app |
| `AJAZZ` / `a-jazz, Inc.` / `黑爵外设 / 深圳市黑爵同创电子科技公司` | Mouse / keyboard driver VersionInfo + AJ199 Max Description.xml | Hardware vendor brand — see Finding 7 |

**VersionInfo block** — extracted from `.rsrc/2052/version.txt`:

```
FILEVERSION    2,9,177,122
PRODUCTVERSION 2,9,177,122
FILEFLAGSMASK  0x3F
FILEFLAGS      VS_FF_DEBUG     ← debug flag set on a release build (vendor build hygiene)
FILETYPE       VFT_DLL          ← anomalous: file is the EXE bootstrap, not a DLL
"CompanyName"      = "HotSpot"
"FileDescription"  = "Stream Dock AJAZZ Global Installer"
"FileVersion"      = "2.9.177.122"
"OriginalFileName" = "Stream-Dock-AJAZZ-Installer_Windows_global.exe"
"ProductName"      = "Stream Dock AJAZZ Global"
"ProductVersion"   = "2.9.177.122"
"LegalCopyright"   = "Copyright (C) 2024 HotSpot"
"Translation"      = 0x804 (zh-CN), 1200 (UTF-16)
```

**Vendor app version is therefore `2.9.177.122`**, primary locale
zh-CN with secondary en-US. The `VS_FF_DEBUG` flag and `VFT_DLL`
filetype on an EXE bootstrap are non-standard — record but do not
block on it.

**Application manifest** (extracted from `.rsrc/2052/MANIFEST/1`):
declares Common-Controls v6 dependency, `<dpiAware>true</dpiAware>`,
support for Windows Vista / 7 / 8 / 8.1 / 10 (no Windows 11 GUID
`{8E0F7A12-BFB3-4FE8-B9A5-48FD50A15A9A}` — wait, that IS the Win10
GUID; **no Win11-specific GUID present**, but that is harmless,
the Win10 manifest entry covers Win11 too). UAC level was not
captured fully; runtime install requires elevation but the
`<requestedExecutionLevel>` line is past the read window — re-read
on next pass.

### Finding 6 — Inno Setup wraps every keyboard / mouse driver

> **Capture ids**: `static-2026-04-29-aj199-002` (AJ199 V1.0,
> inner-EXE sha256 `0c8b3a0c4d31922cb0fb9daa14af58211a2939a2afc8113b041de3ecbdc7852c`,
> outer-ZIP sha256 `c9c4e88e9ae14b5e8ffd4ee740c55e99fa7ca169f485fdeab11b28ec87daf8ed`),
> `static-2026-04-29-aj199max-001` (AJ199 Max, inner sha256
> `dc4b27029a660aaf67a956134598631496a867306a8c92eb8b043edfa65e6e4f`),
> `static-2026-04-29-aj159-001` (AJ159, inner sha256
> `c5610da5ddff71df502871e6562dc9d3c93404c969dc70d0bfcefd8030a42345`),
> `static-2026-04-29-ak820max-rgb-001` (AK820 Max RGB, inner sha256
> `d40fdeddce9d7f95c377578304cc9946da568004d764e39e8757a601c291cdd6`).
> Method: download from vendor catalogue → extract outer ZIP with
> 7z → run `innoextract 1.9` (`--list` and `--extract --silent`)
> against the inner `.exe`. **`innoextract` is a clean-room static
> extractor for Inno Setup** — it reads the installer's compiled
> file table without running any installer step. No vendor binary
> was executed; no `.iss` script was decompiled or paraphrased.

**Refinement of Finding 4**: the inner installers are not "Borland-
style Delphi installer wrappers" generically — they are specifically
**Inno Setup** installers. The 5-DLL profile + Variant-API-first
imports + linker version 2.25 + i386 + cdecl + unsigned fingerprint
that Finding 4 surfaced is the canonical Inno Setup compiler stub
(Inno Setup is built with Borland Delphi; the linker stamp is the
Free Pascal Compiler / Delphi linker). The previous `static-2026-04-26-aj199-001`
and `static-2026-04-26-ak820max-001` capture-ids referenced the
inner EXE, not the outer ZIP — re-confirmed by byte-identical SHA-256
against today's downloads.

VersionInfo `Comments` field on every inner installer reads literally
`"This installation was built with Inno Setup."` — the smoking gun.

**Inno-Setup-extracted file inventory**:

| Capture id                            | Files | Total size | Driver-tool EXE                        | Tool size  | Toolkit |
| ------------------------------------- | ----- | ---------- | -------------------------------------- | ---------- | ------- |
| `static-2026-04-29-aj199-002`         | 78    | 3.4 MB     | `app/OemDrv.exe`                       | 2 211 840  | Win32 raw HID + GDI+; linker 9.0 (MSVC 2008); ts 2023-01-06 |
| `static-2026-04-29-aj199max-001`      | 181   | 11.9 MB    | `app/Mouse Drive Beta.exe`             | 5 352 960  | **.NET / CLR** (`mscoree.dll` only); linker 48.0 |
| `static-2026-04-29-aj159-001`         | 107   | 9.8 MB     | `app/AJAZZ Driver (X).exe`             | 1 868 800  | **MFC** (`mfc140u.dll`, `msvcp140.dll`, `MUI.dll`); linker 14.0; ts 2025-03-19 (UTC `0x67da196b`) |
| `static-2026-04-29-ak820max-rgb-001`  | 121   | 58.1 MB    | `app/AK820MAX.exe`                     | 9 444 352  | **Qt 5** (`Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll`, `Qt5Network.dll`, `Qt5Multimedia.dll`, `qwindows.dll`, 34 `.qm` translations); linker 14.0; ts 2024-11-20 |

**Architectural insight**: the vendor maintains **four distinct
driver chassis** in parallel — Win32-raw, .NET, MFC, Qt 5 — across
peer products of similar age. This is a maintenance liability, not
a strategy. AJAZZ Control Center's single Qt 6 chassis is
structurally simpler than the vendor's. The Qt 5 chassis (AK820 Max
RGB) is the one most likely to evolve toward the Stream Dock
desktop app's Qt 6 stack as the vendor consolidates.

**DLL sideload audit** (AJ199 Max bundles `user32.dll` 1.7 MB +
`oleaut32.dll` 832 KB + `kernel32.dll` 773 KB in the install dir):
all three are **genuine Microsoft binaries**, Authenticode-signed by
`CN=Microsoft Windows`, ProductVersion `10.0.19041.x` (Windows 10
2004). They are not trojanized — vendor merely bundles legacy
Windows 10 DLLs to maintain compatibility with stripped / old systems.
Pattern is unusual but benign.

### Finding 7 — Driver wire transport: native Windows HID API + Feature Reports

> **Capture ids**: same as Finding 6 — driver-tool EXE imports
> tabulated in the inventory above. Method: `pefile` import-table
> walk on each driver tool.

Every native driver tool (AJ199 V1.0 / AJ159 / AK820 Max RGB)
imports the canonical **Windows native HID API**:

| API                                | Used by                  | Purpose                                                          |
| ---------------------------------- | ------------------------ | ---------------------------------------------------------------- |
| `SetupDiGetClassDevsW/A`           | OemDrv, Driver(X), AK820 | Enumerate device interfaces in HID class                         |
| `SetupDiEnumDeviceInterfaces`      | All                      | Iterate device instances                                         |
| `SetupDiGetDeviceInterfaceDetailW/A` | All                    | Resolve device path                                              |
| `SetupDiDestroyDeviceInfoList`     | All                      | Release enumeration handle                                       |
| `HidD_GetHidGuid`                  | All                      | Get HID class GUID                                               |
| `HidD_GetAttributes`               | OemDrv, Driver(X)        | Get VID/PID/version                                              |
| `HidD_GetPreparsedData`, `HidD_FreePreparsedData` | OemDrv, Driver(X) | Parse report descriptor                                |
| `HidP_GetCaps`                     | OemDrv, Driver(X)        | Read capability ranges                                           |
| `HidP_GetSpecificButtonCaps`, `HidP_GetSpecificValueCaps` | OemDrv | Decode button / axis caps                                  |
| **`HidD_SetFeature`**              | OemDrv (AJ199 V1.0)      | **Send Feature reports — primary command channel**              |

**Implication for our parity strategy**: the driver–device protocol
is **HID Feature Reports on the configuration interface**, NOT
WinUSB/libusb bulk transfers. Our existing `hidapi` dependency is
the correct abstraction; we do not need to ship a custom kernel
driver (`.sys` files were searched for in the extracted Inno trees
— **none present** in any of the four installers). Linux equivalent:
`/dev/hidraw*` or `HIDIOCSFEATURE`/`HIDIOCGFEATURE` ioctls.

Notable absence: **none of the native driver tools imports
`libusb-1.0.dll`, `winusb.dll`, `bthprops.cpl`, or any `.sys`
loader function**. The `setupapi.dll` use is purely for HID
enumeration, not for kernel driver install. Confirms: **all
configuration travels over user-mode HID Feature Reports**, no
kernel-mode component is shipped by the keyboard / mouse driver
installers.

The .NET driver `Mouse Drive Beta.exe` (AJ199 Max) imports only
`mscoree.dll` — its HID interaction is through `System.IO.HidLibrary`
or P/Invoke to the same Win32 APIs at runtime. **Static
analysis of its IL would require ILSpy / dnSpyEx** — clean-room
permissible (read-only IL inspection, never compile or execute) —
deferred to next pass since the wire-level API surface is identical
to the native tools per the import-table absence of any non-CLR
HID dependency.

### Finding 8 — Mouse USB ID space (parity-critical)

> **Capture id**: `static-2026-04-29-aj159-001` — extracted
> `app/config.xml` (a plaintext UTF-8 device manifest, NOT vendor
> source code, but a configuration file shipped in the installer
> intended to be read by the driver tool at runtime). Reading
> configuration files is well within clean-room policy.

The AJ159 driver chassis ships a **complete VID:PID:Interface map**
for **8 mouse models** it supports:

| Mouse model      | device_type | dev_id | USB modes (vid:pid)                                                                        | 2.4G dongle modes                                          |
| ---------------- | ----------- | ------ | ------------------------------------------------------------------------------------------ | ---------------------------------------------------------- |
| AJ139 PRO        | 101         | M129   | `248A:5C2E`, `248A:5D2E`, `248A:5E2E`                                                      | `248A:5C2F`, `249A:5C2F`                                   |
| AJ159            | 102         | M620   | `248A:5C2E`, `248A:5D2E`, `248A:5E2E`                                                      | `248A:5C2F`, `249A:5C2F`                                   |
| AJ159 MC         | 103         | M630   | `248A:5C2E`, `248A:5D2E`, `248A:5E2E`                                                      | `248A:5C2F`, `249A:5C2F`                                   |
| AJ159P MC        | (same chassis) | (same) | (see config.xml)                                                                       | (see config.xml)                                           |
| AJ179            | 105         | M179, M603 | `248A:5C2E`, `248A:5D2E`, `248A:5E2E`                                                  | `248A:5C2F`, `249A:5C2F`                                   |
| AJ179 V2         | (different) | (per config.xml) | (per config.xml)                                                                | (per config.xml)                                           |
| AJ179 V2 MAX     | (different) | (per config.xml) | (per config.xml)                                                                | (per config.xml)                                           |
| AJ139 V2 PRO     | 106         | M139   | `248A:5C2E`, `248A:5E2E`                                                                   | `248A:5C2F`, `249A:5C2F`                                   |

Every mode entry uses **HID interface `MI_02`** — the **3rd HID
interface** of the device is the configuration channel; interfaces
0/1 carry the standard mouse HID reports.

The AJ199 Max `Config.ini` uses base64-obfuscated VID/PID strings.
Full decode (validated 2026-04-29):

| Key      | Base64 ciphertext                                   | Plaintext                                               |
| -------- | ---------------------------------------------------- | ------------------------------------------------------- |
| `VID`    | `QUphenozNTU0`                                       | `AJazz3554`           — VID `0x3554`                    |
| `M_PID`  | `QUphenozRjUwMCxGNTY2LEY1NDY=`                       | `AJazz3F500,F566,F546` — wired-mode PIDs                |
| `D_PID`  | `QUphenozRjUwMSxGNTY0LEY1NjcsRjU0NSxGNTQ3LEY1RDU=`   | `AJazz3F501,F564,F567,F545,F547,F5D5` — 2.4G dongle PIDs |
| `CID`    | `QUpBWlogRHJpdmVyIChKKTIw`                           | `AJAZZ Driver (J)20`  — driver CID prefix              |
| `Sensor` | `MzMxMQ==`                                           | `3311`                — PixArt PAW3311 sensor flag      |
| `MM`     | `QksyNTM1`                                           | `BK2535`              — **Beken BK2535 master MCU**     |
| `DM`     | `Q1g1MjY1ME4=`                                       | `CX52650N`            — dongle MCU part number          |
| `D4M`    | `Q0gzMlYzMDU=`                                       | `CH32V305`            — **WCH RISC-V CH32V305 MCU** (probably the 2.4G dongle controller) |

So the AJ199 family is on a different VID space (`248A` and `249A`
for AJ159 family vs `3554` for AJ199 family). Base64 obfuscation is
a minimal anti-casual-RE measure with no cryptographic intent.

**MCU silicon identified**: Beken BK2535 (BLE/wireless), an MCU
labelled CX52650N, and WCH CH32V305 (32-bit RISC-V) — all three
are commodity Chinese-market MCUs. The CH32V305 in particular
gives a strong hint about the firmware-update story: WCH RISC-V
parts ship with a documented bootloader at
`0x1FFF0000`, addressable over USB-CDC at `0x1A86:0x55E0` when
booted into ISP mode. The `Qt5SerialPort.dll` dependency in
`FirmwareUpgradeTool.exe` (Finding 1) is consistent with a USB-CDC
handoff into this WCH bootloader.

**Parity gap with our enumeration table**: cross-check
`docs/_data/devices.yaml` against this map. Some AJAZZ mouse
entries in our YAML may have been authored against the old
Mirabox catalogue and might not match the vid/pid the vendor
driver actually probes for. Track as a follow-up issue.

### Finding 9 — Driver UI feature surface (locale XML / INI inspection)

> **Capture ids**: same as Finding 6. Method: read the
> per-locale `text.xml`, `Config.ini`, device manifests
> (`mouse_*.xml`) — these are plaintext configuration / i18n
> files shipped alongside the driver and **not vendor source**.
> Reading them is the same clean-room exercise as reading a
> `.po` translation file.

**AJ199 V1.0 `app/en/text.xml`** surfaces the following user-facing
features (string-key inventory, feature names paraphrased):

- Debounce time tuning with a low-debounce-may-double-click warning
- Firmware version display
- Profile import / export (Apply / Restore / Reset / Cancel / OK)
- Macro repeat-count (`Times (0-3)`) and inter-key delay
  (`Speed (3-255)`); zero count = repeat-while-held
- Per-key remap pages
- DPI stage configuration (matches AJ159 device manifest's 6 stages)

**AJ159 `app/device/mouse_aj159.xml`** lists per-device runtime
parameters:

- 6 buttons with rect coordinates + `key_value` (HID button index 0–5)
- 6 DPI stages with default stage 1 and per-stage RGB color hex
- Polling rate: `default_value="1"`, `report_max="4"` — 4 stages
- 6 LED effect modes (流光 / 呼吸 / 常亮 / 霓虹 / 七彩波浪 / 关闭 =
  flow / breathing / static / neon / rainbow-wave / off);
  per-device `enable=0/1` mask gates which modes are exposed
- Sleep timer (`sleep_light value="30"`), `move_wakeup`, `move_closelight`

**AJ199 Max `app/Description.xml`** confirms the Chinese trademark
identity: `深圳市黑爵同创电子科技公司` (Shenzhen Black-Knight United-
Creation Electronic Technology Co.) — a fourth vendor-side identity
distinct from `Shenzhen An Rui Xin` (Stream Dock signer), `HotSpot`,
and `AJAZZ / a-jazz, Inc.`. The brand `黑爵外设` (HēiJué Wàishè,
"Black Knight Peripherals") is the Chinese-market name.

**AJ199 Max `Config.ini`** uses `KeyParamN = x_coord, y_coord, hid_byte_offset, hid_bit_mask, default_value`
encoding for per-button HID report layout. **Structural observation
only** — byte-level encoding values to be re-derived from a USB
capture before they enter spec form.

### Finding 10 — Vendor source-file disclosure (clean-room caveat)

> **Capture id**: `static-2026-04-29-aj199max-001` — file
> `app/driver_sensor.h` (22 767 bytes) discovered in the AJ199 Max
> Inno-extracted tree.

The AJ199 Max installer **inadvertently bundles a C header file
from the vendor's own driver source code**: `driver_sensor.h`. The
file contains `#define`-style constants for:

- Sensor support feature flags per supported PixArt model
  (`SUPPORT_SENSOR_MODE_SEL`, `SUPPORT_LOD_CAL`, `SUPPORT_RIPPLE`,
  `SUPPORT_FIXLINE`, `SUPPORT_MOTION_SYNC`, `SUPPORT_MOUSEPAD_CAL`)
- DPI step → byte mapping tables for every supported sensor
  (`SENSOR_3335_DPI_xxxx`, `SENSOR_3395_DPI_xxxx`, `SENSOR_3950_DPI_xxxx`,
  `SENSOR_3370_DPI_xxxx`, `SENSOR_3311_DPI_xxxx`)

**Sensor lineup supported across the AJ199 Max chassis**:
PAW**3395** (flagship), PAW**3311** (entry), PAW**3370** (mid),
PAW**3335** (legacy), PAW**3950** (next-gen). LOD calibration
on 3395/3370/3399/3335/3950; motion-sync only on 3395/3950.

**Clean-room handling**: this file is vendor source. Under our
policy:

1. Its **existence** is recorded here as a fact (file path +
   capture-id + structural description of its contents).
1. Its **byte-level contents are NOT transcribed into this
   document.** Specifically, the literal `#define ...` lines and
   the DPI → byte numeric mappings are NOT reproduced anywhere in
   `docs/research/` or `src/`.
1. The information that the AJ199 Max chassis "encodes DPI as a
   sensor-specific 1-byte step value chosen from a 0x00 … 0x?? range
   that is non-monotonic (some steps are skipped)" is a **structural
   observation** safe to record, because it describes wire format
   shape, not vendor expression. The numeric mapping must be
   re-derived from a USB Feature Report capture before any
   implementer in `src/devices/mouse/` reads it.
1. **The author of this entry MUST NOT contribute to
   `src/devices/mouse/`** for the AJ199 family per the clean-room
   split rule (`docs/research/README.md` § 1).

The file's existence in the installer is most likely a build-system
oversight: the `.iss` Inno Setup script lists the `app/` directory
recursively without an exclusion for `*.h`, so the header was
shipped by accident along with the driver tool that consumes it at
compile time. It does not indicate an intentional vendor disclosure
of the source.

### Vendor-app architecture summary (post-2026-04-29 pass)

| Surface                       | Toolkit                | Transport                          | Capture id (latest)                       |
| ----------------------------- | ---------------------- | ---------------------------------- | ----------------------------------------- |
| Stream Dock (Windows)         | Qt 5.x + WebEngine     | USB-HID + WebSocket localhost      | `static-2026-04-29-streamdock-win-002`    |
| Stream Dock (macOS)           | Qt 6.4-6.5 + WebEngine | USB-HID + WebSocket localhost      | `static-2026-04-26-streamdock-mac-001`    |
| AJ199 V1.0 driver             | Win32 raw + GDI+       | HID Feature Reports                | `static-2026-04-29-aj199-002`             |
| AJ199 Max driver              | .NET / CLR             | HID Feature Reports (assumed)      | `static-2026-04-29-aj199max-001`          |
| AJ159 driver (8 SKUs)         | MFC                    | HID Feature Reports (interface MI_02) | `static-2026-04-29-aj159-001`           |
| AK820 Max RGB driver          | Qt 5.x                 | HID Feature Reports                | `static-2026-04-29-ak820max-rgb-001`      |
| Firmware update tool          | Qt 5.x + QtSerialPort  | USB-CDC (inferred)                 | `static-2026-04-26-streamdock-win-001`    |
| System monitor widget feeder  | Qt 5.x + OpenHardwareMonitorLib | IPC to main app           | `static-2026-04-26-streamdock-win-001`    |

The next recon increment that would unlock implementation work is a
**runtime USB capture** of a vendor app driving each device — see
[`vendor-recon-runbook-windows.md`](vendor-recon-runbook-windows.md).

## Validation findings (2026-04-29 cross-check pass)

After Findings 5–10 landed, a separate cross-check pass re-hashed
every artefact in the vault, re-decoded every base64 ciphertext,
re-walked every PE timestamp into a calendar date, and compared
the documented USB ID space against our own enumeration in
`src/devices/`. Results:

### V1 — Hash integrity ✅

All seven SHA-256 / MD5 hashes cited across Findings 5–10 verify
against the actual files in the recon vault. Stream Dock Win MD5
matches Aliyun's HEAD-reported Content-MD5 byte-for-byte (no CDN
tampering between the 2026-04-26 catalogue probe and the 2026-04-29
download). Inner-EXE SHA-256 hashes match the previous Linux pass's
`static-2026-04-26-aj199-001` and `static-2026-04-26-ak820max-001`
captures verbatim — vendor has not rotated those artefacts.

### V2 — Capture-id linkage ✅

Every `static-2026-04-XX-*` capture-id referenced in
`vendor-feature-matrix.md`, `vendor-software-inventory.md`, and the
`TODO.md` parity backlog resolves to a `> **Capture id**:` block
in this document. No orphan citations.

### V3 — Implementation drift in `src/devices/mouse/` ❌ **Bug**

The AJ-series USB ID space surfaced by Finding 8 contradicts the
values currently in our own
[`docs/_data/devices.yaml`](../_data/devices.yaml),
[`src/devices/mouse/src/register.cpp`](../../src/devices/mouse/src/register.cpp),
and [`resources/linux/99-ajazz.rules`](../../resources/linux/99-ajazz.rules):

| Device  | Our enumeration                         | Vendor reality (Finding 8)                                                                                  | Verdict   |
| ------- | --------------------------------------- | ------------------------------------------------------------------------------------------------------------ | --------- |
| AJ159   | VID `0x3554` PID `0xf51a`               | VID `0x248A` PIDs `0x5C2E/0x5D2E/0x5E2E` (USB) + VID `0x248A`/`0x249A` PID `0x5C2F` (2.4G dongle); HID interface `MI_02` | **WRONG VID + PID** |
| AJ199   | VID `0x3554` PID `0xf51b`               | VID `0x3554` PIDs `0xF500/0xF566/0xF546` (wired-mode M_PID), `0xF501/0xF564/0xF567/0xF545/0xF547/0xF5D5` (dongle D_PID) | VID OK, **PID wrong** (`0xf51b` not in either set) |
| AJ339   | VID `0x3554` PID `0xf51c`               | Vendor driver download not located (open item — see inventory)                                              | **Unverified** — values are educated guess at best |
| AJ380   | VID `0x3554` PID `0xf51d`               | Vendor driver download not located                                                                          | **Unverified**     |

The four PIDs `0xF51A/B/C/D` in `register.cpp` are **sequential and
fictional**; no equivalent values appear in any vendor manifest /
config file captured to date. The `AJ159` row is doubly wrong:
even the VID is for the wrong product line. Real-world impact:

- **Linux**: `99-ajazz.rules` only tags VID `3554` with `uaccess`,
  so an AJ159 / AJ179 / AJ139 mouse plugged into a Linux host gets
  no ACL and the app cannot open the device. The udev rule needs
  to be widened to `idVendor` ∈ `{248A, 249A, 3554}`.
- **Windows / macOS**: `register.cpp` enumerates the wrong PIDs;
  any AJ-series mouse plugged in is silently invisible to our
  device model.
- **Documentation**: README + wiki + AppStream metadata
  (`io.github.Aiacos.AjazzControlCenter.appdata.xml`) propagate
  the same fictional values from `devices.yaml` via `make docs`.

**Required follow-up** (clean-engineer task — must NOT be the same
operator as the 2026-04-29 recon pass):

1. Read Finding 8 of this document and the AJ159 `app/config.xml`
   structural description in Finding 9.
1. Update `docs/_data/devices.yaml` with the correct VID:PID
   matrix per the table in Finding 8 (8 AJ-series mice, each
   with up to 5 mode entries).
1. Re-generate `register.cpp` to enumerate every (VID, PID,
   model) tuple per the YAML.
1. Widen `99-ajazz.rules` to cover `248A`, `249A`, and `3554`.
1. Re-run `make docs` to refresh the autogen blocks in README +
   wiki.
1. Investigate AJ339 / AJ380 separately — vendor driver download
   was not locatable (inventory open item), so their VID:PID is
   currently a guess. Either confirm via a real-device USB capture
   or remove them from the registry until evidence lands.
1. Tracking entry: see `TODO.md` § "Reverse-engineering & vendor
   parity" → "AJ-series VID:PID enumeration drift" (filed
   2026-04-29).

### V4 — Apple VID placeholder in keyboard registration ⚠️

[`src/devices/keyboard/src/register.cpp`](../../src/devices/keyboard/src/register.cpp)
registers a "proprietary" entry under VID `0x05ac` PID `0x024f`.
**`0x05ac` is Apple's IEEE-assigned vendor ID**, not AJAZZ. The
file's comment says the entry is a placeholder until the device
database lands, but the runtime registry will still match against
this VID:PID, which means an Apple keyboard with PID `0x024f`
would be misclassified as an AJAZZ proprietary keyboard. Action
item: replace with a sentinel `0x0000` / `0xFFFF` pair until a
real VID:PID is sourced, or remove the entry entirely.

### V5 — Base64 obfuscation decode corrected ✅

The original Finding 8 wrote the decoded VID as `AJazz:3554`
(with a colon). The actual decode is `AJazz3554` (no colon — the
table-formatted display in this doc has been corrected and the
full table now shows every observed key in the
`Config.ini` `[Option]` block).

### V6 — No vendor source-leak in committed docs ✅

`grep -E 'SENSOR_\w+_DPI_\d+|SUPPORT_(SENSOR_MODE_SEL|LOD_CAL|RIPPLE|FIXLINE|MOTION_SYNC|MOUSEPAD_CAL)\s*=|#define\s+SENSOR_'`
across `docs/research/` returns zero matches. The structural-only
documentation policy applied to `driver_sensor.h` (Finding 10)
holds — no byte-level mappings or `#define` lines were transcribed.

### V7 — Linker timestamp dates ✅ (one minor adjustment)

All seven cited TimeDateStamp values decode cleanly:

| Hex          | Unix       | UTC                    | Note                                            |
| ------------ | ---------- | ---------------------- | ----------------------------------------------- |
| `0x4a870c9c` | 1250364572 | `2009-08-15 19:29:32` | AJ199 V1.0 wrapper — Borland stub, frozen      |
| `0x5b226d52` | 1528982866 | `2018-06-14 13:27:46` | AK820 Max RGB wrapper — Borland stub, frozen   |
| `0x60b88e27` | 1622707751 | `2021-06-03 08:09:11` | AJ159 wrapper                                   |
| `0x6258476f` | 1649952623 | `2022-04-14 16:10:23` | AJ199 Max wrapper                               |
| `0x63b8027b` | 1673003643 | `2023-01-06 11:14:03` | AJ199 V1.0 OemDrv (real driver build)          |
| `0x67da196b` | 1742346603 | `2025-03-19 01:10:03` | AJ159 driver (real build) — was `2025-03-18` in Finding 6 typo, fixed |
| `0x673d8ae6` | 1732086502 | `2024-11-20 07:08:22` | AK820 Max RGB driver (real build)              |

## Finding 11 — Wire protocol from disassembly (TAINTED — see clean-room note below)

> **TAINT NOTICE**: This finding is the result of **disassembly** of
> vendor binaries (`Mouse Drive Beta.exe` decompiled with dnSpy 6.5.1
> netframework, embedded `costura64.hidusb.dll` disassembled with
> radare2 5.9.8; `OemDrv.exe` disassembled with radare2 5.9.8).
> Disassembly is **explicitly forbidden** by
> [`docs/protocols/REVERSE_ENGINEERING.md`](../protocols/REVERSE_ENGINEERING.md)
> § "Legal considerations" but was **explicitly authorised by the
> repository owner on 2026-04-29** as an exceptional measure to
> validate that our `src/devices/` implementation matches the
> vendor's actual wire format. The conflicting policy doc still
> needs reconciliation — see TODO § "Reverse-engineering policy
> reconciliation" for the open question.
>
> **Clean-room consequence**: Findings 5–11 in this document have
> now consolidated **both** static-analysis knowledge AND
> disassembly knowledge in a single operator instance (the
> 2026-04-29 recon pass). Per the engineer-split rule in
> [`docs/research/README.md`](README.md) § 1, **no engineer who has
> read this section may contribute to `src/devices/mouse/`,
> `src/devices/keyboard/`, or `src/devices/streamdeck/` for the
> AJAZZ devices in scope (AJ199, AJ199 Max, AJ159, AJ139, AJ179,
> AK820 Max RGB, Stream Dock).** A clean engineer must read only
> the spec excerpts in Finding 11.A / 11.B below — never this
> taint notice block, nor the underlying capture-ids — and
> implement from those.

### 11.A — AJ199 V1.0 / OemDrv.exe wire format (vs our implementation)

> **Capture id**: `disasm-2026-04-29-aj199-v1.0-001`. Method: radare2
> 5.9.8 native i386 disassembly of `OemDrv.exe`
> (sha256 `ac9ca7cf9e072322589324f3027a65b681ccee8b44bf7c74b288627bf9d58846`,
> 2 211 840 bytes, linker MSVC 9.0, ts 2023-01-06 11:14:03 UTC),
> Inno-extracted from the AJ199 V1.0 vendor installer. The function
> `fcn.00450820` (referenced internally as a "SendData" helper —
> error string literal `SendData Err: nSize>PER_PAYLOAD!`) is the
> single canonical Feature-Report sender for the driver's
> configuration commands. Three xrefs total to `HidD_SetFeature`,
> two are device-enumeration probes during `SetupDi*` walk
> (`fcn.0040d6c0`, `fcn.004519f0`); only `fcn.00450820` carries the
> command-and-data payload that configures the device.

**Observed wire format**:

| Aspect              | OemDrv.exe (vendor reality)                                       | Our `src/devices/mouse/src/aj_series.cpp` (claimed)                                                  |
| ------------------- | ----------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| Report length       | **17 bytes** (`push 0x11` immediately before `HidD_SetFeature`)   | 64 bytes (`constexpr std::size_t kReportSize = 64`)                                                   |
| Report ID (byte 0)  | **`0x08`** (`mov byte [var_14h], 8` at `0x00450875`)              | `0x05` (`pkt[0] = 0x05`)                                                                              |
| Header layout       | Multi-field struct starting at `var_14h` (offset 0 of buffer)     | `[0]=ReportId, [1]=cmd, [2]=sub, [3]=payload-length, [4..62]=payload`                                 |
| Checksum byte (last)| **`0x55 − sum_lo − sum_hi − tail_byte`** (constant 0x55 base)     | `sum(bytes 1..62) mod 256` (simple modular sum)                                                       |
| Checksum scope      | Iterates 8 little-endian words (16 bytes) of the body, splitting low/high accumulators in `bl`/`var_13h` | Sums all 62 body bytes flat into a 32-bit accumulator                                                 |
| Send call           | `HidD_SetFeature(handle, buffer, 17)`                             | `m_transport->writeFeature(pkt)` with 64-byte buffer                                                  |

**Conclusion 11.A**: the wire format observed in the vendor's own
`OemDrv.exe` does **not** match the format we send from
`aj_series.cpp`. The discrepancy is on every primary axis (length,
report id, header layout, checksum algorithm, checksum scope). On
real AJ199 V1.0 hardware, our packets will fail the device-side
validation and the configuration writes will silently no-op or be
rejected.

**Caveats** (do not rule out before a real-device wire capture):

1. The driver may also expose a **secondary** Feature-Report path
   we did not yet locate in the disassembly. Only one of the three
   `HidD_SetFeature` xrefs (`fcn.00450820`) is on the
   command-payload path; the other two are during enumeration.
   Other paths (e.g. via `WriteFile` directly to the HID device
   handle) were not searched.
1. The 2023-01 OemDrv.exe build may differ from the 2024-11 build
   that the GitHub redistribution `progzone122/ajazz-aj199-official-software`
   ships, which is what our `aj_series.cpp` header comment cites
   as its reference. That redistribution is itself "Not a clean-
   room source" per the inventory; if our impl was authored from
   a wire-capture against that newer build, the protocol may
   genuinely have changed between 2023-01 and 2024-11, in which
   case our impl is correct for current-firmware AJ199 V1.0
   hardware but does not match the older OemDrv.exe analysed here.

A live USB capture against an AJ199 V1.0 mouse running the **current**
vendor driver, per the runbook in
[`vendor-recon-runbook-windows.md`](vendor-recon-runbook-windows.md)
§ 2.5, is the only way to disambiguate (1) and (2). Until that
runs, the implementation in `src/devices/mouse/src/aj_series.cpp`
is **suspect, not confirmed-broken**.

### 11.B — AJ199 Max wire format is different from AJ199 V1.0

> **Capture id**: `disasm-2026-04-29-aj199max-001`. Method: ILSpy/dnSpy
> decompilation of `Mouse Drive Beta.exe` (.NET assembly,
> sha256 `f59265cd11e6c816627007b8151a038e8851aef559e96bd9d98dcbecba3e5832`,
> 5 352 960 bytes), then radare2 disassembly of the embedded
> `costura64.hidusb.dll` (Costura.Fody-bundled native dependency,
> sha256 `abbc0a8175fe91471ae6e082abacd69137ac725f22079104478e6ff0b7a9e6e1`,
> 145 408 bytes, x86_64).

The AJ199 Max ".NET wrapper + native DLL" architecture exposes a
**rich command vocabulary** on the C ABI of `HIDUsb.dll`. Functions
exported with `CS_UsbServer_*` prefix are 5-byte trampolines into
the matching internal `UsbServer_*` symbol. The full export list
recovered (47 symbols) covers: device pairing, MTK / USB-update
mode entry, current-DPI / report-rate / DPI-LED / LED-bar reads,
4K-dongle RGB control, long-range mode toggle, slave version,
battery optimisation parameters, flash data (read at address /
read all / write via `ProtocolDataCompareUpdate`), config read,
clear-setting, VID/PID rebind, descriptor-string set, dongle ID
rebinding (`SetDongleIDToMouse`), etc.

**Observed buffer layout** in `UsbServer_ReadVersion`
(`0x180010fa0`, 221 bytes, native x64) and peer functions:

The driver uses a **stack-local struct** at offsets `var_40h…var_53h`
with multi-byte writes:

- `mov word [var_43h], 0x1208`
- `mov word [var_40h], 0x101`
- `or byte [var_48h], 0x80`
- subsequent `lea rcx, [var_43h]` / `r8d, [rdx+0x10]` / call into
  `fcn.18000f060` (the central send dispatcher, common to all
  UsbServer_* functions of size ~221 bytes)

**This is structurally different from both** our 64-byte report
format **and** OemDrv.exe's 17-byte format. The AJ199 Max driver:

- builds a `<= 16-byte` header struct on the stack with
  field-by-field MOVs at known offsets (`var_40`, `var_43`,
  `var_48`, `var_50`, `var_53`),
- ORs flag bits at specific byte offsets (`or byte [var_48h], 0x80`
  is a "completion" or "request" bit),
- delegates the actual transport via `fcn.18000f060` which we did
  not deeply trace in this pass; it likely splits short commands
  vs long commands and chooses between `WriteFile`-style HID OUT
  endpoint vs `HidD_SetFeature`.

**Conclusion 11.B**: AJ199 V1.0 (Win32 raw / OemDrv.exe) and AJ199
Max (.NET + HIDUsb.dll) speak **different wire dialects**. They
share the same VID space (`0x3554`) but the protocol-layer
encoding diverges. Our implementation cannot be a single
`aj_series.cpp` for both — at minimum the report length and
checksum strategy need a per-PID branch, and probably a
per-firmware lookup. The MCU footprint (Beken BK2535 master,
WCH CH32V305 dongle, per Finding 8 base64 decode) confirms the
hardware lineage difference: AJ199 Max has a more capable
controller than AJ199 V1.0 and the firmware authors evolved the
host protocol to match.

### 11.C — Our `aj_series.cpp` is wrong about the disassembly basis

The header comment in
[`src/devices/mouse/src/aj_series.cpp`](../../src/devices/mouse/src/aj_series.cpp:5)
claims the protocol was

> "Reverse-engineered from the official Windows utility (Wireshark
> + USBPcap captures of `ajazz-aj199-official-software`)"

— with a referenced byte-level spec at
`docs/protocols/mouse/aj_series.md`. The disassembly in 11.A shows
the AJ199 V1.0 driver's wire format is **incompatible** with
`aj_series.cpp`'s 64-byte / report-id-0x05 / sum-mod-256 envelope.
Either:

1. The original USB capture that informed `aj_series.cpp` was
   against a **different** (likely newer) firmware version that
   uses a 64-byte report format the older OemDrv.exe does not
   send, and our implementation is correct for current-firmware
   AJ199 V1.0 hardware.
1. `aj_series.cpp` was authored from **a guess** (or an
   unrelated reference) and never wire-validated against any
   AJAZZ mouse, in which case the 64-byte / 0x05 / sum-mod-256
   protocol is fictional.
1. There is a **second wire-format path** in OemDrv.exe (the
   non-`HidD_SetFeature`-based path) that uses our format, and
   the `fcn.00450820` SendData helper covers a different feature
   set.

The only way to disambiguate is the runtime USB capture in
[`vendor-recon-runbook-windows.md`](vendor-recon-runbook-windows.md).
Until that lands, the existing `tests/integration` capture-replay
suite (per
[`docs/protocols/REVERSE_ENGINEERING.md`](../protocols/REVERSE_ENGINEERING.md)
§ "5. Verify") is the canonical correctness check; if it passes,
the impl is correct against the captures it was authored from,
even if those captures pre-date this disassembly pass.

**Action item (P0, clean-engineer)**: see TODO § "AJ-series wire
format reconciliation" filed 2026-04-29.

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
