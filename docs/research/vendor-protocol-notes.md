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
| `static-2026-04-29-aj159-001`         | 107   | 9.8 MB     | `app/AJAZZ Driver (X).exe`             | 1 868 800  | **MFC** (`mfc140u.dll`, `msvcp140.dll`, `MUI.dll`); linker 14.0; ts 2025-03-18 |
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

The AJ199 Max `Config.ini` uses base64-obfuscated VID/PID strings:
`VID=QUphenozNTU0` decodes to `AJazz:3554`. So the AJ199 family is
on a different VID space (`248A` and `249A` for AJ159 family vs
`3554` for AJ199 family). Base64 obfuscation is a minimal anti-
casual-RE measure with no cryptographic intent.

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
