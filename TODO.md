# TODO

Living checklist of open work for **AJAZZ Control Center**, organized by
expected effort. Items reference concrete files / commits / sessions where
they originated. Closed items move to `CHANGELOG.md`.

For the official release flow see [`docs/wiki/Release-Process.md`](docs/wiki/Release-Process.md).
For the audit findings that produced many of these entries see commit
history of `feat(app): live HID enumeration ...` (`af92ff2`) and the
preceding security/architecture review pass.

______________________________________________________________________

## ✅ Recently shipped (this development cycle)

- AJAZZ Streamdock store — live catalogue fetch with cached on-disk
  mirror and bundled offline fallback (`StreamdockCatalogFetcher`,
  three-layer resolution, live UI status pill). See "Plugin SDK + Store"
  below for the full description.
- AJAZZ AK980 PRO support (vendor `0x0c45`, codename `ak980pro`) — `469e599`
- Single-instance gate + show-existing on relaunch — `f7c4847`
- Default-visible main window (was tray-only on first launch) — `09c104a`
- Live HID enumeration for `ConnectedRole` — `af92ff2`
- Material Design style + theme/accent bound to `ThemeService` — `6072265`
- RPM polish: `.pc` exclude + populated `Description` — `9faf1f9`, `c4c9609`
- CI/CD bulletproofing: concurrency, timeouts, checkout v5, wiki resilience,
  gitleaks PR auth, dep-review tolerance — `87ed454` … `136a25e`, `bc8b8e6`
- Local packaging recipes (Fedora / Debian / Flatpak / Win / macOS) — `9a94ae4`
- AJAZZ wordmark vendored as `resources/branding/ajazz-logo.png` — `3f78afb`

______________________________________________________________________

## 🚧 Open work

### Quality bar (always-on standard)

- [x] **Zero warnings, every build, every linter** — high-professionalism
  baseline set 2026-04-27, achieved this cycle. `cmake --build` and
  every configured linter (`qmllint-qt6`, `clang-tidy`, `ruff`,
  `mdformat`, `cmake-lint`, `typos`) now produce **no warnings** on a
  clean tree. Status:

  - **C++ / make**: ✅ zero warnings (`-Werror` is on; if the C++ ever
    regresses CI fails)

  - **clang-tidy**: ✅ runs at pre-push, catches modernize / readability
    / cppcoreguidelines lints before they reach `main`

  - **ruff / ruff-format / mdformat / typos / cmake-lint**: ✅ zero
    warnings (pre-commit hooks block any regression)

  - **qmllint-qt6**: ✅ zero warnings. The previous 6 residual entries
    were closed by:

    - 4× in `NativePropertyInspector.qml` — replaced the per-row
      `Loader` (whose `item` is typed as `QObject*`) with an inline
      typed `StackLayout` of seven editor controls, so every
      `applyValue` / `committed` member access is statically
      resolvable. Initial values now come from `Component.onCompleted`
      reading `row.currentValue()`; commits hit `row.commit(...)`
      from each editor's native signal. Costs an extra six controls
      per row (the hidden ones); negligible for a property inspector.
    - 2× in `PIWebView.qml` — root cause was that `WebEngineView`
      does **not** expose a `page` property to QML at all (verified
      against `qquickwebengineview_p.h`); the previous
      `page: PropertyInspectorController.activePage` binding was
      silently inert at runtime. Refactored
      `PropertyInspectorController` to use `QQuickWebEngineProfile`
      and `QQmlWebChannel` (the QML-native types) and exposed the
      trio `(activeProfile, activeChannel, activeUrl)` so
      `PIWebView.qml` binds the three properties WebEngineView
      actually accepts. Page-level security flags are now
      configured declaratively on `WebEngineView.settings`.
      `qt_add_qml_module(... IMPORTS QtWebEngine QtWebChannel)` is
      set conditionally on `AJAZZ_HAVE_WEBENGINE` so qmllint can
      resolve cross-module types and minimal builds stay self-
      contained.

    Resolved earlier this cycle (pre-existing categories now closed):

    - `[unqualified]` context properties (8 services × N files) →
      migrated to `QML_NAMED_ELEMENT` + `QML_SINGLETON` in commits
      `97eb719` (Branding) + `7df853c` (the other 7).
    - `[unqualified]` delegate-scope ids/model roles → fixed via
      `pragma ComponentBehavior: Bound` + `required property`
      declarations in 12 QML files.
    - `[duplicated-name]` `settingsChanged` collision in
      NativePropertyInspector / PropertyInspector → renamed to
      `settingsJsonChanged`.
    - 8× unused `import QtQuick.Controls` → removed.
    - `[Quick.layout-positioning]` undefined `width`/`height` on
      Layout-managed items in SettingsPage / ProfileEditor /
      RgbPicker → switched to `Layout.preferredWidth/Height`.

### Quick wins (≤ 1 hour each)

- [x] **Wire AJAZZ banner into AppHeader.qml** — done. The wordmark image is
  rendered with PreserveAspectFit at 32 px height; the redundant text label
  is hidden when the image loads. Tray and app icons stay placeholder until
  a square variant exists.
- [x] **`app.ico` + `app.icns` + multi-size hicolor ONGs** generated from
  `resources/branding/app.svg`. Windows `.exe` now embeds the icon via a
  generated `.rc` file; macOS bundle picks up `app.icns` via
  `MACOSX_BUNDLE_ICON_FILE`; Linux installs PNG + scalable variants under
  `share/icons/hicolor/<size>x<size>/apps/`.
- [x] **README + wiki screenshots** of the Material UI in light and dark
  mode — done in this cycle. Captured via `niri msg action screenshot-window` against the running app with `Appearance/Mode = light` / `dark` preset in `~/.config/Aiacos/AjazzControlCenter.conf`,
  saved at `docs/screenshots/main-{dark,light}.png` (full HiDPI 3792 ×
  2061 each, ~170 KB). README hero block uses a `<picture>` with
  `prefers-color-scheme` so the GitHub README adapts to the visitor's
  theme automatically; `docs/wiki/Home.md` shows both side by side
  under a new `## Screenshots` section.
- [x] **Hicolor PNG install rule** — done in `2486662`. The Linux install
  block in `src/app/CMakeLists.txt` now publishes the brand-aligned SVG
  (sourced from `resources/branding/app.svg`) under
  `share/icons/hicolor/scalable/apps/ajazz-control-center.svg` and a full
  pre-rasterised PNG ladder under `share/icons/hicolor/<sz>/apps/` for
  every size `makeAppIcon()` consumes (16/22/24/32/48/64/128/256/512). The
  legacy 3x3 macropad-grid placeholder at `resources/icons/app.svg` was
  also replaced with a verbatim copy of `resources/branding/app.svg` so
  every fallback path renders the brand mark.
- [x] **Logger `CapturingSink` test fixture** — done in this cycle.
  `tests/unit/test_logger.cpp` ships a reusable `CapturingSink` (records
  every accepted call with level/module/message under an internal mutex
  for thread-safety with the `LogSink::write` contract) and a `ScopedSink`
  RAII helper that installs the sink + restores the previous level on
  destruction. Two new cases pin the contract: `[logger][sink] captures accepted records` exercises both the macro path and the std::format
  substitution at `LogLevel::Trace`; `[logger][sink] respects level filter` verifies that records below the active filter never reach
  `write()` (the filter is enforced in `log()` itself, not delegated to
  the sink).

### User actions (out-of-code, one-time)

- [x] **Enable GitHub Wiki**: done by the maintainer this cycle
  (initial page saved with placeholder content "VOID" — the
  `wiki-sync.yml` workflow overwrites every page from `docs/wiki/` on
  every push to `main`, so the placeholder content is replaced
  automatically on the next push).
- [x] **Enable Dependency Graph**: done by the maintainer this cycle.
  `dependency-review.yml` is now effective on dependabot PRs (the
  `continue-on-error: true` shim can be removed in a follow-up cleanup
  commit once a green run confirms the action works).
- [ ] **AK 980 ACL — physical replug**: `setfacl` workaround was applied
  this session, but is reset at the next boot. After replugging the
  AK 980 PRO once, systemd-logind picks up the `TAG+="uaccess"` from the
  new udev rule and grants the ACL persistently.

### Medium-effort fixes (1–4 hours)

- [ ] **Square brand asset** for tray / app icon. The wordmark in
  `resources/branding/ajazz-logo.png` is a 3:1 banner; a centered crop
  produces mostly whitespace and looks worse than the current
  geometric placeholder. Either ask AJAZZ for a square logo or design a
  custom monogram inspired by the wordmark.
- [x] **`make test` Windows fixture for `concurrent writers`** — switched
  to `ReplaceFileW` for the existing-destination case (the common path
  under contention) and widened the retry set to also catch
  `ERROR_LOCK_VIOLATION` and `ERROR_USER_MAPPED_FILE`. `MoveFileExW` is
  kept as the no-destination-yet fallback. Should eliminate the residual
  ~1/100 flake on windows-2022.
- [x] **Audit finding S8 — narrow udev rules** (`resources/linux/99-ajazz.rules`):
  done this cycle. The four `SUBSYSTEM=="usb"` lines were dropped; only
  the matching `KERNEL=="hidraw*"` rules remain. **Validation**:
  `ajazz_core` links the `hidapi::hidapi` umbrella target, which on
  Linux resolves to `hidapi_hidraw` (the `/dev/hidraw*` kernel-native
  backend) whenever `HIDAPI_WITH_HIDRAW` is built — verified at
  `dev/_deps/hidapi-src/src/CMakeLists.txt:137-156` (EXPORT_ALIAS=hidraw
  takes precedence over libusb when both are available). The libusb
  backend is built but not linked into our binary, so the dropped USB
  rules covered a code path we never enter at runtime.
- [ ] **profile-buttons — wire Apply / Revert / Restore defaults to real
  paths** (surfaced by source-level `TODO(profile-buttons)` at
  `src/app/qml/Main.qml:111`). Today the three ProfileEditor buttons
  toast `"not implemented yet"` because `ProfileController` lacks the
  default-path resolution (`QStandardPaths::AppDataLocation/profile.json`)
  - a "Save as" file dialog for explicit paths. Wire-up needs:
    (1) `ProfileController::saveProfile()` / `loadProfile()` no-arg
    overloads that pick the default path, (2) a `QFileDialog` trigger for
    "Save as" / "Open profile…", (3) bring `PropertyInspector.qml` /
    `NativePropertyInspector.qml` back from dead-QML status (currently not
    instantiated — see the cleanup backlog note below) once the buttons
    actually save something the inspector can re-render. ≈ 1 day.
- [ ] **via_keyboard — per-LED RGB matrix path** (source-level stub
  `throw std::runtime_error` at `src/devices/keyboard/src/via_keyboard.cpp:185`).
  The VIA protocol we speak today only exposes the solid-color / effect
  command set (cmd `0x07` sub-command `0x04`); per-LED keying requires
  the QMK `QMK_RGB_MATRIX` extension command-set (cmd `0x08` + LED-index
  payload) which our firmware detection does not probe yet. Gate plan:
  (a) add a `viaFeatures()` probe at device-open time that enumerates
  the supported VIA command pages via the `GET_PROTOCOL_VERSION` round-
  trip, (b) flip `KeyboardCapabilities::hasRgbMatrix` when the
  `QMK_RGB_MATRIX` page is advertised, (c) implement the write path
  under that gate. Until then `setRgbBuffer()` staying as a hard
  exception is intentional — callers check capabilities first.
  ≈ 1 day.
- [ ] **macOS + Windows AutostartService backends** (source-level stub at
  `src/app/src/autostart_service.cpp:163`). Linux ships via the XDG
  `.desktop` autostart spec. Remaining:
  - **macOS**: write a LaunchAgent plist at
    `~/Library/LaunchAgents/<appId>.plist` with `RunAtLoad = true`
    and (for start-minimised) a `ProgramArguments` array that adds
    `--minimized`. Register via `launchctl load -w`.
  - **Windows**: write the `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
    registry value via `QSettings(QSettings::NativeFormat)` with the
    same `--minimized` suffix. No admin rights needed (HKCU scope).
  - Feature-flag the platforms behind `#ifdef Q_OS_MACOS` / `Q_OS_WIN`
    alongside the existing Linux block. ≈ 0.5 day per platform.
- [ ] **MacroRecorder — macOS + Windows native back-ends** (source-
  level `(TODO)` tags in `src/core/include/ajazz/core/macro_recorder.hpp:14-15`).
  Linux ships an evdev stub gated on `AJAZZ_FEATURE_MACRO_RECORDER`.
  Remaining:
  - **macOS**: `CGEventTap` via Accessibility permission (requires the
    user to grant the app "Input Monitoring" in System Settings →
    Privacy). Translate `CGEventFlags` + keycode into `MacroEvent`.
  - **Windows**: `SetWindowsHookExW(WH_KEYBOARD_LL, ...)` low-level
    hook in a dedicated thread. Translate the `KBDLLHOOKSTRUCT` into
    `MacroEvent`. Beware: DirectInput-consumed events are invisible to
    WH_KEYBOARD_LL; a secondary `SetWindowsHookExW(WH_MOUSE_LL)` hook
    covers the mouse side.
  - Both backends ship disabled by default; the runtime UI prompts the
    user to enable the OS permission before the first capture.
    ≈ 1 day per platform.

### Reverse-engineering & vendor parity (multi-day, research)

> Strategic recon track: the goal is to land at full feature-parity
> with the AJAZZ first-party desktop apps so users have no reason to
> keep the proprietary stack installed alongside ours, and we ship a
> more stable infrastructure on top of what they have already shipped
> and battle-tested. Findings feed every downstream milestone (Plugin
> SDK + Store, Stream Deck / Streamdock compat layers, the live
> Streamdock catalogue fetcher, lifecycle manager, device protocol
> coverage). All work is **clean-room**: notes, captures and decoded
> protocols may live in the repo, but no vendor binary, decompiled
> source, or copyrighted asset ever lands in version control.

- [x] **Acquire the official AJAZZ software for the Maude keyboard and
  the Stream Dock line of products** — first pass landed. Inventory
  shipped at `docs/research/vendor-software-inventory.md` with the
  Stream Dock AJAZZ-branded Windows + macOS installers (Aliyun OSS,
  Content-MD5 verified at HEAD without burning bandwidth on the
  full 121 / 282 MB downloads), 17 keyboard models (AK680/820/870
  families + AKS / AKP / NK SKUs) and 15 mouse models (AJ199/159
  families + AJ039/129/139/179/358/52 + AM3) with URL, file size,
  Last-Modified, and Content-MD5 where the CDN exposes it. Open
  items recorded in the doc: Mirabox-branded Stream Dock builds
  (JS-rendered portal returns 500 on a single-shot fetch — needs a
  real browser session); Maude keyboard (not on either EN or UK
  download page — likely a regional / internal name, action item to
  confirm via `support@a-jazz.com`); AJ339/AJ380 driver download
  (probably folded into AJ199 family — needs report-descriptor
  comparison); SHA-256 archival pass to the encrypted out-of-repo
  vault.

- 🟡 **Decompile / disassemble the AJAZZ desktop apps under a clean-
  room policy**: run the installers in a disposable VM, extract the
  Electron / .NET / Qt payloads, decompile with the appropriate
  toolchain (`asar` + `js-beautify` for Electron, ILSpy / dnSpyEx
  for .NET, Ghidra / IDA for native binaries) and produce a written
  protocol & feature inventory at
  [`docs/research/vendor-protocol-notes.md`](docs/research/vendor-protocol-notes.md).
  **Rules**: only one engineer reads vendor sources; that engineer
  writes specs but does not contribute to the matching module; a
  second "clean" engineer implements from the spec. Capture USB /
  WebSocket / IPC traffic with Wireshark + usbmon to cross-validate
  the static analysis. ≈ 3-5 days.

  **Status — 2026-04-29**: Phase A (host-static analysis without
  installer execution) shipped this cycle. Findings 5-10 in
  `docs/research/vendor-protocol-notes.md` cover: Stream Dock Win
  Authenticode + version 2.9.177.122 + manifest; the four mouse /
  keyboard installers identified as **Inno Setup** (refines the
  earlier "Borland-style" classification — `innoextract 1.9` was
  used for clean-room static decode, no installer execution); each
  device driver chassis fingerprinted (Win32-raw, .NET, MFC, Qt 5);
  driver wire transport confirmed as **HID Feature Reports on
  interface MI_02** (no kernel `.sys` driver, no `winusb.dll`); AJ
  series USB ID space mapped (VID `248A`/`249A`/`3554`); driver UI
  feature surface enumerated from the XML / INI configuration files
  shipped alongside each tool; one inadvertent vendor source-file
  disclosure (`driver_sensor.h`) noted with the structural-only
  policy applied.

  Phase B (runtime VM captures) is queued. The runbook at
  [`docs/research/vendor-recon-runbook-windows.md`](docs/research/vendor-recon-runbook-windows.md)
  documents the disposable-VM workflow, per-device interaction
  scripts, and the cleanup discipline that lets an operator pick
  up Phase B without re-discovering the toolchain. ≈ 2-3 days
  remaining once a person + hardware are scheduled.

- [x] **Vendor feature inventory → gap analysis** — scaffold landed
  this cycle at
  [`docs/research/vendor-feature-matrix.md`](docs/research/vendor-feature-matrix.md).
  Stream Dock / keyboard / mouse / cross-cutting infrastructure
  rows seeded from `docs/_data/devices.yaml` and the public AJAZZ
  feature sheets, with our coverage marked ✅ / 🟡 / ❌ / ❓ where
  evidence is in hand and ❓ where the vendor "claimed" capability
  awaits the recon pass. The doc is goal-backwards: ❓ entries
  flip to ✅ / ❌ only when a `capture-id` from
  `vendor-protocol-notes.md` lands. Open work — populate the
  Vendor column with verified behavior as recon ships.

- 🟡 **Protocol parity backlog** (un-blocked 2026-04-29): the Phase A
  static pass flipped four ❓ rows in `vendor-feature-matrix.md` to
  verified-vendor (mouse polling rate, button remap / macros, lift-
  off distance, sleep / sensor calibration). The first concrete
  parity items from those flips are listed below; each one will
  also be linked back into the **Plugin SDK + Store** and
  **Architecture refactors** sections of this file when an
  implementer picks it up. ≈ 0.5 day per item once the Phase B
  wire-capture confirms byte-level encoding.

  - [ ] **Mouse polling-rate setter** — `IMouseCapable::setReportRate(stage)` with `stage ∈ {0..3}` (vendor exposes 4 stages, not the 5-stage marketing claim). Wire-capture queued in `docs/research/vendor-recon-runbook-windows.md` § 2.5. Blocks: nothing. Implementing engineer must NOT have read the static analysis findings 8 / 9.
  - [ ] **Mouse button remap + macro encoding** — protocol uses Feature Report with `KeyParamN = (x, y, hid_byte_offset, hid_bit_mask, default_value)` shape per AJ199 Max `Config.ini`; macro repeat semantics are `Times (0-3)` + `Speed (3-255)` per AJ199 V1.0 `text.xml`. Re-derive byte-level encoding from a wire capture before implementing.
  - [ ] **Mouse lift-off distance + sleep / move-wakeup** — vendor manifests expose `<sleep_light value="30">`, `<move_wakeup>`, `<move_closelight>`, `show_lod="1"`; supported sensors per `driver_sensor.h` are 3395 / 3370 / 3399 / 3335 / 3950 (LOD calibration), 3395 / 3950 (motion-sync). Implementer reads Finding 9 only, **not** Finding 10's source-file disclosure.
  - [ ] **Mouse RGB effect modes (6)** — flow / breathing / static / neon / rainbow-wave / off, with per-device enable mask. Add an `IMouseCapable::setLightMode(modeId)` API alongside the existing RGB color setter. Re-derive byte mapping from wire capture.
  - [ ] **Mouse 2.4G dongle pairing & enumeration** — vendor exposes USB modes (PIDs `5C2E/5D2E/5E2E` on VID `248A`) plus 2.4G dongle (PID `5C2F` on VID `248A`/`249A`). The dual VID for the dongle is a clue that vendor distinguishes "device-direct USB" vs "via-dongle" stack paths; our enumeration in `src/devices/mouse/` must handle both VID prefixes. Cross-check `docs/_data/devices.yaml`.
  - [ ] **Stream Dock firmware update via QtSerialPort handoff** — `FirmwareUpgradeTool.exe` is a separate process linked against `Qt5SerialPort.dll`, suggesting a USB-CDC bootloader handoff. Wire-capture the boot-into-bootloader command + the subsequent serial flash protocol. Implements the AKP153 / AKP03 / AKP05 firmware-update parity gap.
  - [ ] **Stream Dock Property Inspector HTML compat** — vendor ships ~11 `index.html_*` PI pages bundled in the MSI (per Finding 3); confirm via § 3.2 of the recon runbook (admin-extract). Cross-check what `$SD` events the JS calls; PI compat is required for plugin parity. Implementer is not the same as the runbook operator (clean-room split).
  - [ ] **Audio-reactive RGB on AK820 Max RGB** — vendor driver bundles `fftreal.dll` (real-input FFT). Likely powers an audio-reactive lighting mode that maps mic input spectrum to per-key RGB. New feature surface for our AK820 backend. Wire-capture the FFT mode toggle + observe whether it streams real-time updates over HID Feature Reports or is a fire-and-forget mode-switch.

- 🟢 **AJ-series VID:PID enumeration drift** ✅ shipped 2026-04-29 (data layer): the validation cross-check pass found that `docs/_data/devices.yaml`, `src/devices/mouse/src/register.cpp`, and `resources/linux/99-ajazz.rules` enumerated AJ-series mice under VID `0x3554` with sequential PIDs `0xF51A/B/C/D`. The vendor's own driver `app/config.xml` (Finding 8 in `docs/research/vendor-protocol-notes.md`) maps AJ159 / AJ139 / AJ179 to **VID `0x248A` (USB) and `0x249A` (2.4G dongle)** with PID range `0x5C2E/0x5D2E/0x5E2E/0x5C2F`; AJ199 family is on VID `0x3554` but with PID range `0xF500-0xF5D5` (none of which is the `0xF51A-D` we previously enumerated).

  - [x] Updated `docs/_data/devices.yaml` with 6 AJ-series entries covering both VID spaces.
  - [x] Updated `src/devices/mouse/src/register.cpp` to enumerate the corrected `(VID, PID)` tuples.
  - [x] Widened `resources/linux/99-ajazz.rules` to cover `idVendor==248a`, `249a`, and `3554`.
  - [x] Ran `make docs` (`scripts/generate-docs.py`) — README + wiki + AppStream regenerated from corrected YAML.
  - [x] AJ339 / AJ380 removed from active registry until real-device VID:PID is captured (vendor driver download not located).
  - [x] Clean-room: the data fix is derived from vendor `config.xml` / `Config.ini` (CONFIG-only files, explicitly clean-room safe per `docs/research/README.md` § 1) and from our own code, NOT from disassembly. Apply only the data; the wire-format reconciliation (next entry) is the part that requires a clean engineer.

- 🟢 **Apple VID placeholder in keyboard registration** ✅ shipped 2026-04-29: `src/devices/keyboard/src/register.cpp` no longer registers the Apple-VID placeholder (`0x05AC:0x024F`). The entry was deleted along with a comment block noting why. AK820 / AK820 Pro / AK820 Max keyboards continue to be reachable via the VIA entry (SONiX prefix `0x3151`); the proprietary backend's per-model coverage will follow the device database wire-up.

- 🟥 **AJ-series wire format reconciliation** (filed 2026-04-29; refined 2026-04-30 with full opcode tables; **P0 — likely-broken AJ199 V1.0 impl, possibly-broken AJ199 Max impl**): the 2026-04-30 disassembly pass (Findings 11–14 in `docs/research/vendor-protocol-notes.md`) confirms the AJAZZ vendor stack speaks **three distinct wire-format dialects**, generation-stratified by device-firmware vintage:

  - **OemDrv** (AJ199 V1.0, 2023): 17-byte report, ReportId `0x08`, checksum `0x55 − sum_lo − sum_hi − tail_byte`.
  - **HIDUsb** (AJ199 Max, 2024 — `Mouse Drive Beta.exe` + bundled `HIDUsb.dll`): 20-byte report, ReportId `0x01`, simple SIMD byte-sum over `buffer[3..18]`. **Opcode table extracted in Finding 12.B**: `0x02` SetPCDriverStatus, `0x03` ReadOnLine, `0x04` ReadBatteryLevel, `0x08` ReadFalshData (flash-read primitive — most other Read\* commands thunk through this), `0x09` SetClearSetting, `0x0B` SetVidPid, `0x0D` EnterUsbUpdateMode, `0x0E` ReadConfig, `0x0F` SetCurrentConfig, `0x12` ReadVersion, `0x14` Set4KDongleRGB, `0x16` SetLongRangeMode, `0x1A` SetDongleIDToMouse.
  - **Witmod** (AK820 Max RGB, 2024 — `AK820MAX.exe` + `witmodSdk.dll`): 64-byte report, cmd_id at `byte[1]`, 145-case switch-table dispatch in `CWitmodHid_HidWriteBuff`. Same shape as our existing `aj_series.cpp` envelope. SDK exports 30 `CWitmodHid_*` symbols including per-key RGB, side-bar RGB, ripple/collision reactive lighting, animation timing, and `_FirmwareUpgrade` (over the same HID transport, NOT USB-CDC).

  Our `src/devices/mouse/src/aj_series.cpp` implements a **64-byte / ReportId `0x05` / sum-mod-256** envelope which structurally resembles **Witmod** but does not match either OemDrv or HIDUsb. Fix:

  - [ ] Run `vendor-recon-runbook-windows.md` § 2 against AJ199 V1.0, AJ199 Max, AK820 Max RGB hardware in turn. Capture the USB Feature Report stream while the vendor app exercises every feature (DPI, RGB, polling rate, button remap, firmware update).
  - [ ] Cross-check each capture against the predicted shape from Findings 11.A / 12 / 13.
  - [ ] Refactor `src/devices/mouse/src/aj_series.cpp` to a **per-(vid, pid) dialect dispatch**: AJ199 V1.0 PIDs → OemDrv encoder; AJ199 Max PIDs → HIDUsb encoder; AK820 Max RGB lives in `src/devices/keyboard/` already and may need a witmodSdk-style envelope class.
  - [ ] Update `docs/protocols/mouse/aj_series.md` with the byte-level layout from each capture (current doc is stale on multiple axes; replace with three dialect-specific tables).
  - [ ] Clean-room: the engineer who runs the captures and writes the spec MUST NOT also be the one who modifies `src/devices/{mouse,keyboard}/`. The taint notice in Finding 11 means the 2026-04-30 RE operator is now ineligible for both — a new clean engineer reads the spec excerpts in 11.A / 12 / 13 (NOT the taint notices) and implements from those.

- 🟧 **Reverse-engineering policy reconciliation** (filed 2026-04-29; LOW — process / docs-only): the project has three documents in conflict on disassembly:

  - `docs/protocols/REVERSE_ENGINEERING.md` § "Legal considerations" forbids it absolutely (`"No disassembly of vendor binaries"`).
  - `TODO.md` § "Reverse-engineering & vendor parity" contemplates it (`"decompile with the appropriate toolchain (asar + js-beautify for Electron, ILSpy / dnSpyEx for .NET, Ghidra / IDA for native binaries)"`).
  - `docs/research/vendor-recon-runbook-windows.md` (this cycle) explicitly forbids it (`"Decompiling vendor binaries... explicitly forbids"`).
    The 2026-04-29 disassembly pass (Finding 11) was authorised on a one-off basis by the repo owner. The policy needs a single decision: allow disassembly under the engineer-split rule, or forbid it entirely (in which case Finding 11 would need to be redacted, leaving only the static-analysis findings 5–10). Recommend updating REVERSE_ENGINEERING.md + the runbook to align with whichever decision is taken. Until reconciled, future disassembly attempts should require explicit written authorisation per-pass, not silent precedent.

- [ ] **Stability & infrastructure cross-pollination**: where the
  vendor app already solves a hard problem better than we do (HID
  reconnect debounce timings, firmware update retry / rollback,
  per-device USB transfer chunk sizes, profile sync conflict
  resolution, telemetry beaconing, crash-safe settings persistence),
  document the technique in
  [`docs/research/vendor-techniques.md`](docs/research/vendor-techniques.md)
  (scaffold + 6 candidate techniques landed this cycle, ranked by
  expected stability impact; details fill in as captures come in)
  and open targeted issues to port the *idea* (never the code) into
  our stack. ≈ ongoing, scoped per technique.

### Architecture refactors (multi-day, milestone-level)

> Findings from the architectural review pass earlier in the session.
> Each one is a self-contained milestone; pick by current pain.

- [x] **A1 — DeviceRegistry singleton → DI** ✅ shipped. `Application`
  now owns a `core::DeviceRegistry m_deviceRegistry` member and threads
  it through `streamdeck::registerAll(reg)` / `keyboard::registerAll(reg)`
  / `mouse::registerAll(reg)` plus the `DeviceModel(registry, parent)`
  constructor. The Meyers-singleton `DeviceRegistry::instance()` is kept
  as a `[[deprecated]]` transition shim (audit-finding label baked into
  the attribute) so any future backend that still needs it keeps
  compiling. `tests/unit/test_device_registry.cpp` builds its own local
  registries and gains an isolation case proving two instances don't
  share state.

- [x] **A2 — ActionEngine threading model** ✅ shipped. `ActionEngine`
  now accepts a pluggable `core::Executor`; on `Sleep` (and on any
  non-zero post-step `delayMs`) it defers the chain continuation via
  `executor->scheduleAfter(delay, ...)` instead of calling
  `std::this_thread::sleep_for` on the caller. Default is `BlockingExecutor`
  (legacy semantics, kept for tests and headless callers); the GUI app
  ships `QtExecutor` (backed by `QTimer::singleShot`) so the HID poll
  thread / Qt main thread is freed immediately. Existing 3 ActionEngine
  tests still pass; 3 new tests cover the executor abstraction.

- [x] **A3 — EventBus per-event allocation** ✅ shipped. `event_bus.cpp`
  now holds the subscribers in a `std::atomic<std::shared_ptr<vector<…> const>>`
  swapped via copy-on-write on subscribe / unsubscribe; `publish()` is
  lock-free (atomic-load + iterate immutable snapshot). Writers
  serialise on a write-only mutex; readers don't take a mutex.
  Existing 4/4 EventBus tests still pass under TSan.

- 🟡 **A4 — PluginHost out-of-process** — slices 1 + 2 + 2.5 + 3a + 3b
  (Linux) + 3c (macOS) + 3d (Windows port, runtime-validated on CI in
  this cycle) + 3d-ii (Windows AppContainer sandbox) + 3e (legacy
  backend retired) shipped. The remaining ☐ is the CI end-to-end run
  of the AppContainer path on a `windows-2022` runner (see "Slice
  3d-ii" at the bottom of this entry). POSIX
  `OutOfProcessPluginHost`
  (`src/plugins/include/ajazz/plugins/out_of_process_plugin_host.hpp`)
  spawns a child Python process via `fork()` + `execvp()` and talks
  to it over line-delimited JSON on a pair of pipes. The child
  (`python/ajazz_plugins/_host_child.py`) speaks the wire protocol:
  `ready` handshake, `add_search_path`, `load_all`,
  `list_plugins` (now populated from real loaded plugins),
  `dispatch` (routes to the matching `Plugin` instance, catches
  Python exceptions so a bad handler returns `dispatch_error`
  instead of taking the child down), `shutdown`, plus a
  `_crash_for_test` op for the safety proof. Five unit tests in
  `tests/unit/test_out_of_process_plugin_host.cpp` cover spawn
  round-trip, the crash-isolation claim, end-to-end discovery
  against the `hello` example plugin, dispatch routing, and
  bad-config rejection. The slice-2 wire uses one flat-JSON
  object per line (no nested arrays), so the host's mini-parser
  stays adequate without an extra JSON dep.

  Slice 2.5 (this cycle): the `IPluginHost` abstract base in
  `src/plugins/include/ajazz/plugins/i_plugin_host.hpp` is now the
  shared contract — `addSearchPath`, `loadAll`, `plugins`, `dispatch`
  with the richer signatures (counts, `bool` returns) — and both
  `PluginHost` (`final : public IPluginHost`) and
  `OutOfProcessPluginHost` (`final : public IPluginHost`)
  implement it via virtual dispatch. The in-process `PluginHost`'s
  `dispatch` was migrated from `(core::Action const&)` to the
  unified `(plugin_id, action_id, settings_json)` shape — it returns
  `bool` now too. A new test case
  (`OOP plugin host: drives the full lifecycle through an IPluginHost pointer`)
  exercises the OOP backend through an `IPluginHost&` reference and
  proves virtual dispatch works end-to-end. Future callers can hold
  a `std::unique_ptr<IPluginHost>` slot and switch backends with no
  source-level changes.

  Slice 3a (this cycle): foundation for permission-based sandboxing.
  `PluginInfo` now carries a `permissions: vector<string>` field that
  both backends populate from the Plugin subclass's
  `permissions: ClassVar[list[str]]` attribute (validated against the
  `Ajazz.Permissions` enum in
  `docs/schemas/plugin_manifest.schema.json`). The OOP wire protocol
  emits the array verbatim in `plugin_loaded` / `plugin` events; a new
  `findStringArrayField` host-side parser handles string arrays
  without an extra JSON dep. The hello example declares
  `["notifications"]`. No enforcement yet — slice 3b adds the
  per-OS sandbox that consumes this list.

  Slice 3b — Linux (this cycle): `Sandbox` ABC at
  `src/plugins/include/ajazz/plugins/sandbox.hpp` plus
  `LinuxBwrapSandbox` (`linux_bwrap_sandbox.{hpp,cpp}`) wrap the
  child invocation in `bwrap(1)` with a default-deny profile —
  `--ro-bind / /` + `--tmpfs /tmp` + fresh `--proc`/`--dev` + the
  full unshare set (pid/ipc/uts/cgroup-try/net) +
  `--die-with-parent`/`--new-session`. Permission-driven
  relaxations: granting any of `obs-websocket`/`spotify`/`discord-rpc`
  drops `--unshare-net`; granting any of
  `notifications`/`media-control`/`system-power` bind-mounts the
  user session DBus socket (`$XDG_RUNTIME_DIR/bus`) when present.
  `OutOfProcessHostConfig` grew a `unique_ptr<Sandbox> sandbox`
  field (null defaults to `NoOpSandbox`, preserving prior
  behaviour). Falls back to passthrough when `bwrap` is missing
  from `PATH` so minimal containers / non-bubblewrap distros stay
  functional. 5 unit tests + 1 end-to-end "spawn through
  LinuxBwrapSandbox round-trips" cover the decoration logic and
  prove the actual sandboxed child completes ready / load_all /
  dispatch under the most-restrictive profile.

  Slice 3e (this cycle): retired the legacy in-process pybind11
  `PluginHost`. The `ajazz_plugins` library is now Qt-free,
  pybind11-free, and Python-compile-time-dep-free; the OOP child
  invokes the system `python3` via execvp at runtime. Deleted
  `plugin_host.{hpp,cpp}` and `python_bindings.cpp` (the embedded
  `ajazz` Python module — its Rgb/DeviceFamily types will be
  re-exposed as plain Python in `ajazz_plugins/__init__.py` if a
  plugin ever needs them). Top-level CMake dropped the
  `find_package(Python3)` + `find_package(pybind11)` block;
  `AJAZZ_BUILD_PYTHON_HOST` survives as a thin gate over the
  plugins subsystem so existing CI matrices (`-D…=OFF`) keep working.

  Slice 3c (this cycle): macOS counterpart of slice 3b. New
  `MacosSandboxExecSandbox` (`macos_sandbox_exec_sandbox.{hpp,cpp}`)
  wraps the OOP child in `/usr/bin/sandbox-exec -p '<profile>'` with
  the same default-deny posture: `(version 1) (deny default)` plus
  the minimum allow rules CPython needs to start (process-fork,
  process-exec\*, signal target self, sysctl-read, file-read\*,
  file-write\* under `/private/var/folders` + `/tmp`, narrow
  bootstrap-server `mach-lookup`). Permission-driven relaxations
  parallel the Linux ones: any of
  `obs-websocket`/`spotify`/`discord-rpc` adds `(allow network*)`;
  any of `notifications`/`media-control`/`system-power` adds the
  broad `(allow mach-lookup)` so the child can reach
  `com.apple.usernotificationsd` / MediaRemote / IOPower mach
  services. Fall-back: passthrough if `/usr/bin/sandbox-exec` is
  missing (Linux dev boxes, stripped systems). 6 unit tests pin the
  policy text + argv shape; the policy generator is exercised on
  Linux runners via a `/bin/sh` shim. End-to-end runtime test is
  pending a macOS CI runner — same posture as bwrap on Linux until
  the matrix expands.

  Slice 3d (this cycle, runtime-validated on CI):
  `out_of_process_plugin_host_win32.cpp` mirrors the POSIX backend
  using `_spawnvp(_P_NOWAIT, ...)` for the spawn and `_pipe` for the
  IPC channel. `PeekNamedPipe` provides the timeout-read semantics
  `poll(2)` gives on POSIX. The wire-protocol helpers (jsonEscape,
  buildOp, parsers) were extracted into `src/plugins/src/wire_protocol.hpp`
  so both backends share the exact same encoding logic — fewer
  divergence opportunities. Windows compiles this file via the
  CMake gate (`if(WIN32)`) and Linux compiles the POSIX file; the
  public header is now platform-agnostic. **Validation**: commit
  `82779d9` removed the `if(NOT WIN32)` gate around
  `test_out_of_process_plugin_host.cpp` (the Linux-only piece was the
  bwrap end-to-end test; the rest exercises the `IPluginHost`
  contract, which is platform-agnostic). The cross-platform fixture
  uses a `findPython()` helper that picks `python3` (POSIX) or
  `python` (Windows). The first push after the unblock ran the full
  6-test OOP suite on `windows-2022`, all green in 4.75 s — proving
  the win32 backend's `_spawnvp` + `_pipe` + `PeekNamedPipe` flow,
  the wire protocol, dispatch routing, the crash-isolation claim
  (`os.kill(self, SIGSEGV)` → `TerminateProcess(handle, 11)` plus
  the `ctypes.string_at(0)` fallback for an actual access violation),
  and virtual dispatch through an `IPluginHost&` reference all hold
  end-to-end on Windows.

  Slice 3d-ii (this cycle): `WindowsAppContainerSandbox` shipped at
  `src/plugins/{include/ajazz/plugins,src}/windows_app_container_sandbox.{hpp,cpp}`.
  AppContainer + restricted token are configured at `CreateProcessW`
  time via `STARTUPINFOEX::lpAttributeList`, not via a wrapper
  executable, so the `Sandbox` interface grew a second virtual
  method, `configureProcessAttributes(ProcessAttributes&)`, that is
  a no-op on POSIX (bwrap / sandbox-exec already express the full
  sandbox through argv decoration) and the active path on Windows.
  `ProcessAttributes` is a pimpl opaque type defined in
  `src/plugins/src/sandbox.cpp` — the POSIX pimpl is empty, the
  Windows pimpl owns the `PSID appContainerSid`,
  `std::vector<SID_AND_ATTRIBUTES> capabilities`,
  `std::vector<PSID> capabilitySids` (mirror for `FreeSid`) and
  `HANDLE restrictedToken`; the destructor runs `FreeSid` on every
  SID and `CloseHandle` on the token, so the sandbox is
  RAII-clean end-to-end. `WindowsAppContainerSandbox::configureProcessAttributes`
  calls `DeriveAppContainerSidFromAppContainerName` (via
  `userenv.dll` delay-loaded with `LoadLibraryW` + `GetProcAddress`
  so the static link surface stays minimal and the class gracefully
  degrades to passthrough on Windows builds where AppContainer has
  been disabled by group policy), allocates capability SIDs via
  `AllocateAndInitializeSid(SECURITY_APP_PACKAGE_AUTHORITY, SECURITY_CAPABILITY_BASE_RID, rid, ...)`, and produces a
  restricted token via `CreateRestrictedToken(primary, DISABLE_MAX_PRIVILEGE, ...)` to drop every privilege. Permission
  mapping mirrors the Linux / macOS rules exactly so plugin authors
  see one consistent model across OSes: `obs-websocket` / `spotify`
  / `discord-rpc` grant the `SECURITY_CAPABILITY_INTERNET_CLIENT`
  capability; `notifications` / `media-control` / `system-power`
  additionally grant `SECURITY_CAPABILITY_INTERNET_CLIENT_SERVER`
  so the child can reach the Windows notification broker /
  MediaRemote / IOPower equivalents. The win32 host backend
  (`out_of_process_plugin_host_win32.cpp`) was rewritten off
  `_spawnvp` onto `CreateProcessW` / `CreateProcessAsUserW` with a
  proper STDIO pipe pair (`CreatePipe` + `SetHandleInformation` to
  mark the parent-end non-inheritable), and when the sandbox
  populated the pimpl it now builds a `STARTUPINFOEXW` carrying a
  `SECURITY_CAPABILITIES` blob via
  `UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES)`

  - the `EXTENDED_STARTUPINFO_PRESENT` creation flag. Parent-end
    pipe HANDLEs are wrapped in CRT fds via `_open_osfhandle` so the
    existing `readLine` / `writeLine` code paths keep using the
    shared POSIX-shaped `_read` / `_write` helpers (only the spawn
    changed; IPC is unchanged). 5 unit tests in
    `tests/unit/test_windows_app_container_sandbox.cpp` cover the
    policy surface (decorate passthrough, default container name,
    custom container name, permission preservation including
    "unknown" strings, `configureProcessAttributes` exception-safety)
    — they run on the `windows-2022` CI slot only. **Pending work**:
    E2E runtime validation on `windows-2022` (the existing
    `test_out_of_process_plugin_host.cpp` fixture would need a
    `SandboxedSpawn` case that constructs a `WindowsAppContainerSandbox`
    and drives the OOP round-trip through it), and a later narrowing
    pass that replaces the coarse `internetClientServer` capability
    with named broker-surface capabilities once we have measurements
    of which mach-equivalent services each Windows permission
    actually contacts.

- [x] **A5 — Logger global → injectable sink** ✅ shipped. New @c LogSink
  abstract base in `ajazz/core/logger.hpp`; default `StderrSink`
  reproduces the legacy formatting. `setLogSink(shared_ptr<LogSink>)`
  swaps the active sink atomically against concurrent log() calls
  (slot is `std::atomic<std::shared_ptr<LogSink>>`). All AJAZZ_LOG\_\*
  macros unchanged; call sites untouched. Tests can now install a
  capturing sink and assert on what subsystems logged.

- [x] **A6 — Application destructor drain** ✅ shipped in `0ca47c6`.

### Security hardening

- [x] **S2 — Plugin module name shadowing** — done. `loadAll()` calls
  `sys.stdlib_module_names` (Python 3.10+) before importing any plugin
  directory and rejects directories whose basename collides with a stdlib
  module name. Pre-3.10 best-effort: relies on `isSafePluginId`.
- [x] **S3 — `sys.path` accumulation** — done. `loadAll()` snapshots the
  current `sys.path` into an unordered_set and appends only paths not yet
  present.
- [x] **S4 — Profile-IO TOCTOU / symlink follow / parent-dir fsync** —
  done. Switched the Linux/macOS write path from `std::ofstream` to POSIX
  `::open(O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC, 0600)`, fsync the
  parent directory after the rename, and let `O_EXCL` reject any racing
  symlink.
- [x] **S6 — `syncFile` reopen** — done. The new POSIX write path
  fsyncs the *original* fd before close; no reopen, no buffered-stdio
  loss window.
- [ ] **S7 — Substring-only profile validator** (LOW, intentional —
  `src/core/src/profile_io.cpp:123-153`): the in-tree `containsKey`
  scanner is documented as a tiny shim; replace with the Qt-side
  `QJsonDocument` parser when the app-layer reader lands. Tracking
  bug only — no behavior change needed yet.

### Plugin SDK + Store (multi-week, milestone-level)

> User-requested in this session. **Not autonomous-feasible** end-to-end;
> document and break down so the work can be parallelized.

- [x] **Manifest schema** (`docs/schemas/plugin_manifest.schema.json`):
  JSON Schema Draft 2020-12 describing `UUID`, `Name`, `Version`,
  `SDKVersion`, `Actions[]`, `PropertyInspectorPath`, `OS`, `Software`,
  `CodePath` / `CodePathLin` / `CodePaths` (OpenDeck-compatible) plus
  the `Ajazz` namespace (`Sandbox`, `Permissions`, `Signing`,
  `Compatibility`, `SupportedDevices`). Hooked into pre-commit via
  check-jsonschema so every committed `manifest.json` is validated
  automatically.
- [x] **Architecture doc** (`docs/architecture/PLUGIN-SDK.md`):
  language-agnostic out-of-process SDK; WebSocket JSON protocol that is
  a strict superset of the Stream Deck SDK-2 wire format and accepts
  OpenDeck plugins unchanged; lifecycle (discovery → negotiation →
  spawn → register → steady-state → shutdown); per-OS sandbox
  strategy (bwrap + seccomp / sandbox-exec / AppContainer /
  flatpak-spawn); permission model; Sigstore-based signing; Plugin
  Store catalogue shape; CLI workflow (`acc plugin scaffold/run/pack/lint`).
- [ ] **Plugin process spawner** (sandboxed sub-processes, stdio or
  Unix-socket transport). ≈ 3-4 days.
- [ ] **WebSocket protocol bridge** (Stream Deck-compatible JSON event
  router, plugin → app and app → plugin). ≈ 5-7 days.
- [ ] **Plugin lifecycle manager** (install / load / unload / state
  persistence). ≈ 5-7 days.
- **Property Inspector embedding** (Qt WebEngine for HTML PI, with
  bridged messages to the plugin process) — five-step roadmap:
  - [x] **M1** — controller stub + CMake gating (`AJAZZ_BUILD_PROPERTY_INSPECTOR`).
  - [x] **M2** — Qt WebEngine surface, per-plugin `QWebEngineProfile`
    isolation, conservative `QWebEngineSettings` baseline,
    `QtWebEngineQuick::initialize()` in main.cpp, `PIWebView.qml`
    behind a Loader switcher.
  - [x] **M3** — `PIBridge` QObject exposing the Stream Deck SDK-2
    `\$SD` API surface via `QWebChannel` (registered as `"$SD"` on the
    page's channel). Method bodies are logging stubs.
  - [x] **M4** — settings persistence for `setSettings` / `getSettings`
    / `setGlobalSettings` / `getGlobalSettings` to per-context JSON
    files under `QStandardPaths::AppDataLocation/plugins/<plugin>/`.
    Atomic writes via `QSaveFile`, path-traversal validation on uuid
    components, 1 MiB size cap. Per-context settings land at
    `settings/<contextUuid>.json`; plugin-wide at `global.json`. Getter
    methods stay async (return-via-signal contract preserved). All
    M4-side code is gated by `AJAZZ_HAVE_WEBENGINE` and lives in
    `src/app/src/pi_bridge.cpp`.
  - [ ] **M5** — bridge `\$SD.sendToPlugin` and `sendToPropertyInspector`
    over the plugin-host WebSocket. Depends on **Plugin process spawner**
    - **WebSocket protocol bridge** below.
- [x] **Property Inspector security hardening pass** — shipped. New
  `PIUrlRequestInterceptor` (`src/app/src/pi_url_request_interceptor.{hpp,cpp}`)
  is installed on every per-plugin `QWebEngineProfile` at the moment
  `loadInspector` constructs the profile, scoped to the PI directory
  (parent of `htmlAbsPath`); the allow/deny policy lives in pure C++ at
  `src/app/src/pi_url_policy.{hpp,cpp}` so it is unit-testable without
  booting QtWebEngine. The decision tree allows file:// only inside the
  PI dir (with traversal blocked via cleaned-path comparison), https://
  only to the Phase 1 CDN allowlist (`cdn.jsdelivr.net`, `unpkg.com` —
  pinned in a `static constexpr std::array` so it is grep-able), qrc /
  blob / data for Qt-internals, and rejects http:// + every other scheme
  with a logged `AJAZZ_LOG_WARN("plugin-pi", ...)`. `\$SD.openUrl(url)`
  is now wired through `QDesktopServices::openUrl` after an https-only
  validation (rejects `javascript:`, `file:`, `mailto:`, `http:`); the
  per-plugin first-call confirmation prompt is flagged with
  `// TODO(pi-prompt)` for the next pass. Manual smoke test page at
  `resources/dev/pi/index.html`. 12 unit tests under
  `tests/unit/test_pi_bridge.cpp` cover the load policy + the openUrl
  policy. M5 plugin-host wiring remains the next milestone.
- [x] **Plugin Store UI** (`src/app/qml/PluginStore.qml`,
  Material-styled, virtualized `GridView`, live search/filter, install /
  uninstall / enable-toggle per-plugin, side-sheet details pane). Mounted
  in a right-edge modal `Drawer` opened from a new “Plugins” button in
  `AppHeader`. Source-switcher tabs scope the catalogue to All /
  Installed / AJAZZ Streamdock / Community; an info banner on the
  Streamdock tab clarifies the upstream origin. Backed by the
  `PluginCatalogModel` C++ list model exposed as the `pluginCatalog` QML
  context property. Networked catalogue fetch + Sigstore verification
  still mocked — next milestone.
- [ ] **Catalog backend** (registry, ratings, version pins, Sigstore
  signing). Server-side, ≈ 2-3 weeks; out of repo until protocol stabilises.
- [ ] **Stream Deck plugin compat layer** (translate Elgato manifests
  - WS messages to ours; Property Inspector iframe quirks). ≈ 1-2 weeks.
- [x] **OpenDeck store integration — first-class tab** (user request,
  2026-04-26). Done. New `OpenDeckCatalogFetcher`
  (`src/app/src/opendeck_catalog_fetcher.{hpp,cpp}`) GETs
  `https://plugins.amankhanna.me/catalogue.json` (the OpenDeck
  community mirror of the archived Elgato Stream Deck App Store —
  per the OpenDeck Wiki "0. Elgato Marketplace" page), parses the
  flat JSON array of plugin entries, translates each to a
  `CatalogEntry` with `source = "opendeck"` and
  `compatibility = "opendeck"`. Same three-layer (live → cache →
  bundled fallback) resolution as the Streamdock fetcher, atomic
  `QSaveFile` mirror at
  `<XDG_CACHE_HOME>/ajazz-control-center/opendeck-catalog.json`,
  endpoint overridable via `ACC_OPENDECK_CATALOG_URL=` (set to
  `disabled` to skip live fetch on air-gapped builds). Plumbed
  through `PluginCatalogModel` with parallel `replaceOpendeckRows`
  plus three new Q_PROPERTY entries (`opendeckState`,
  `opendeckFetchedAtUnixMs`, `opendeckCount`) and a matching
  `opendeckStateChanged` signal. New "OpenDeck" tab in
  `PluginStore.qml` (index 3,
  between Streamdock and Community) with its own status-pill banner
  attributing the upstream and explaining the SDK-2 compat shim.
  `resources/opendeck-fallback.json` ships 3 representative entries
  so the tab is never empty in dev / CI / air-gapped builds.
  One-click install through the shared lifecycle manager still
  depends on the **Plugin lifecycle manager** milestone.
- [x] **AJAZZ Streamdock as the default Plugin Store tab** (user
  request, 2026-04-26). Done — `src/app/qml/PluginStore.qml`
  initialises `property int activeTab: 2` so first-time users land
  on the AJAZZ-curated catalogue (live mirror of
  `https://space.key123.vip/interface/user/productInfo/list`,
  confirmed via web search to be the official AJAZZ Streamdock
  plugin store) rather than the noisy union under "All".
- [x] **QSettings-backed Plugin Store tab persistence** — done.
  `src/app/qml/PluginStore.qml` now imports `QtCore` and wires
  `activeTab` through a `Settings { category: "PluginStore"; property alias activeTab: root.activeTab }` block. The AJAZZ
  Streamdock default (index 2) still applies on the very first
  launch (when QSettings has no stored value yet); subsequent
  launches restore whatever tab the user last selected. Stored
  under `[PluginStore]/activeTab` in the existing
  `Aiacos/AjazzControlCenter.conf` — the `category` keeps it
  scoped so future page-specific persistence (search query, sort
  order, etc.) can co-exist without key collisions.
- [x] **AJAZZ Streamdock store integration — schema + UI scaffolding**:
  manifest schema now exposes `Ajazz.Compatibility.Mode = "streamdock"`
  alongside `streamdeck` / `opendeck`, plus an opaque
  `StreamdockProductId` for catalogue-mirror lookups. The Plugin Store
  UI surfaces upstream entries under a first-class “AJAZZ Streamdock”
  tab with a dedicated info banner; `PluginCatalogModel` rows carry a
  `source` field (`local` / `community` / `streamdock`) used by the tab
  filter, and the side-sheet details pane labels the source as “AJAZZ
  Streamdock store” for those rows.
- [x] **AJAZZ Streamdock store — live catalogue fetch**: shipped. New
  `StreamdockCatalogFetcher` (`src/app/src/streamdock_catalog_fetcher.{hpp,cpp}`)
  POSTs to `https://space.key123.vip/interface/user/productInfo/list`
  page-by-page, accumulates plugin records, translates upstream
  `deviceUuid`s (293/293E/N3/N4/D92…) to AKP codenames via a curated
  table, and writes an atomic `QSaveFile` mirror to
  `<XDG_CACHE_HOME>/ajazz-control-center/streamdock-catalog.json`. Three
  resolution layers (live → cached → bundled `streamdock-fallback.json`)
  ensure the AJAZZ Streamdock tab is never empty. The fetcher is wired
  into `PluginCatalogModel` and surfaces `streamdockState`
  (`loading`/`online`/`cached`/`offline`) + `streamdockFetchedAtUnixMs`
  to QML, which renders a live status pill on the tab banner with a
  rolling relative-time label. Endpoint is overridable via
  `ACC_STREAMDOCK_CATALOG_URL=` (set to `disabled` to skip the network
  fetch entirely on air-gapped builds). One-click install through the
  shared lifecycle manager still depends on **Plugin lifecycle
  manager**.

**Total realistic estimate**: 6-10 weeks of focused engineering for a
v1; backend catalog and the AJAZZ Streamdock store bridge are parallel
workstreams.

### UI polish (incremental)

- 🟡 **Material 3 expressive theming** beyond the basic style switch.
  Foundation slice landed this cycle:

  - Theme.qml grew an M3 typography ramp (`typeHeadlineLarge` /
    `Medium`, `typeTitleLarge` / `Medium` / `Small`, `typeBodyLarge` /
    `Medium` / `Small`, `typeLabelLarge` / `Medium` / `Small`) — each a
    `{ pixelSize, weight, letterSpacing }` bag mirroring the M3 type-
    scale tokens.
  - Motion tokens (`durationShort`/`Medium`/`Long` ≈ 150/280/500 ms,
    `easingStandard`/`Decelerate`/`Accelerate` mapped to the closest
    QtQuick easing curves).
  - Elevation tokens (`elevation0..3` `{ offsetY, blur, opacity }` bags
    plus `elevationOf(level)` and `elevationShadowColor`) driving M3-
    spec drop shadows via `QtQuick.Effects.MultiEffect.layer.effect`.
  - `components/Toast.qml` rewritten as an M3 Snackbar — width clamped
    to 344-672 px, optional trailing action button, motion-token-bound
    animations, elevation-3 shadow.
  - `components/Card.qml` exposes an opt-in `elevation: int` (default
    0 preserves the prior flat look; 1-2 lifts).

  Still open: M3 expressive *full kit* — large numerals, font-weight-
  motion (variable axis), responsive container queries, surface-tinted
  primary palette, ripple effects on `PrimaryButton` /
  `SecondaryButton`. Pick by visible win when the typography pass
  reaches a page-by-page audit.

- [x] **Empty state polish** for `DeviceList` when zero devices online
  — done. `src/app/qml/DeviceList.qml` now hides the empty `ListView`
  and renders the existing `components/EmptyState.qml` centered in the
  sidebar with a "No devices yet" title and a one-line onboarding hint
  ("Plug in an AJAZZ device — keyboard, Stream Dock, or mouse — and it
  will show up here automatically."). Suppressed in the collapsed
  64 px icon-only layout (`root.width < 200`) so the title doesn't
  clip on narrow windows. Illustration is the existing typography-
  only treatment from the EmptyState component; richer artwork
  belongs to a future Material 3 polish pass.

- [x] **Toast notifications upgrade** to the Material 3 Snackbar
  pattern — done in this cycle. `components/Toast.qml` is now M3-spec:
  geometry width clamped 344-672 px (vs. the previous 480-cap), height
  switches between 48 (text-only) and 56 (with action), corner radius
  on `Theme.radiusMd`, drop shadow at elevation 3 via
  `MultiEffect.layer.effect`, motion bound to the new Theme tokens
  (`durationMedium` + `easingStandard`). API gained an optional
  trailing action button: `toast.show(message, variant, "Undo", cb)`.
  Existing 6 call-sites in `Main.qml` keep working unchanged.

- [x] **Light-theme `DeviceList` tile contrast** — fixed in this cycle.
  Root cause was *not* the `DeviceList`/`DeviceRow` binding (it already
  read from `Theme.tile`), but `Theme.tile` itself: it (and
  `Theme.tileHover`/`Theme.borderSubtle`) was a hardcoded dark literal
  (`#24242a` / `#2a2a32` / `#3a3a44`) that ignored the active branding
  palette. Every tile/card/border across the app (DeviceRow, KeyCell,
  EncoderCard, AppHeader search field, Card.qml, PluginStore tiles,
  Settings page, etc.) painted dark over a light `bgSidebar` when light
  theme was selected.
  Fix: derive these tokens from the branding palette via
  `Qt.tint(bgSidebar, Qt.rgba(fgPrimary.r, fgPrimary.g, fgPrimary.b, α))`
  with α=0.03/0.06/0.13. The tint is polarity-agnostic — `fgPrimary` is
  near-white in dark mode (lightens `bgSidebar`) and near-black in light
  mode (darkens `bgSidebar`), so elevation always points the right way.
  α factors were chosen by working backward from the prior dark
  literals so the dark-theme look is preserved within < 1 %/channel.
  No QML call-site changes; no `BrandingService` contract changes;
  no `themeService.mode` plumbing — the `themeChanged` signal already
  re-emits when the user switches modes and Qt re-evaluates every
  property that reads a `branding.*` color. Visual regression check
  against `docs/screenshots/main-light.png` waits for the next
  screenshots refresh; build + test (105/105) green.

- [x] **Settings page** (`src/app/qml/SettingsPage.qml`) — page +
  entry-point both shipped. Material-styled page exposes
  `themeService.mode`, `autostart.launchOnLogin`, and
  `autostart.startMinimised` / `tray.startMinimized` via RadioButton +
  SwitchDelegate. AppHeader now hosts a "Settings" ToolButton next to
  "Plugins" that emits `settingsRequested()`; Main.qml opens a right-edge
  modal `Drawer` (clamped 360–560 px wide) hosting the page —
  same pattern as the Plugin Store drawer. The page was renamed from
  the original `Settings.qml` to `SettingsPage.qml` because
  `QtCore.Settings` is the QSettings binding type used by PluginStore
  for tab persistence; sharing the unqualified name caused
  `[missing-property] category` resolution errors when both modules were
  imported in the same file.

______________________________________________________________________

## Cleanup backlog (low-risk polish, opportunistic)

Surfaced by an exhaustive code-quality audit on 2026-04-30. None of
these are blocking; they are worth bundling into a rainy-day cleanup
PR when a block of uninterrupted time is available.

### Dead code

- [ ] Remove `StreamdockCatalogFetcher::setCatalogUrlOverride` and
  `OpenDeckCatalogFetcher::setCatalogUrlOverride` (defined but never
  called from `src/` or `tests/`; cache-dir override is the only one
  exercised by test fixtures). Header + source, ≈ 4 lines net.
- [ ] Remove `akp153::buildShowLogo` (declared in
  `akp153_protocol.hpp:87`, defined in `akp153.cpp:130`; no callers in
  the tree — capability exists in firmware but is not wired to any
  device method or UI surface).
- [ ] Remove `akp153::emptyPacket()` helper — single caller
  (`buildCmdHeader` one line below) and `std::array<...>{}` is already
  zero-initialised; net loss of clarity.
- [ ] Resolve the status of `src/app/qml/PropertyInspector.qml` +
  `src/app/qml/NativePropertyInspector.qml`: no instantiation of
  either `PropertyInspector { ... }` or `NativePropertyInspector { ... }`
  exists anywhere in `src/app/qml/`. They are dead WIP files waiting
  on the `profile-buttons` wire-up above. Once that lands — keep them;
  until then, they bloat the QML module without runtime benefit.
- [ ] Retire `scripts/_create_issues.sh` + `scripts/_issues.json` —
  one-shot GitHub-issue bootstrap, no Makefile / CI reference. Move to
  `docs/dev/archive/` or delete outright.

### Stale comments / inline docs

- [ ] `src/app/src/pi_bridge.hpp:23-25` claims "Compiled only when
  `AJAZZ_HAVE_WEBENGINE` is defined" — FALSE. The file is compiled
  unconditionally per `src/app/CMakeLists.txt:52` and contains no
  `#ifdef AJAZZ_HAVE_WEBENGINE` guards. Either add the guard (matching
  intent: PIBridge is useless without WebEngine) or correct the claim.
- [ ] `src/plugins/src/out_of_process_plugin_host_win32.cpp:37-48` —
  the "This file ships untested on Windows" paragraph is outdated. The
  file IS now exercised on `windows-2022` via `test_out_of_process_plugin_host.cpp`
  (commit `82779d9`). Strike the disclaimer.

### Duplication (low-effort refactoring)

- [ ] Extract `src/plugins/src/permission_gating.hpp` with shared
  `grantsNetwork` / `grantsBroker` + the permission-string constants.
  Today they are copy-pasted across
  `linux_bwrap_sandbox.cpp` / `macos_sandbox_exec_sandbox.cpp` /
  `windows_app_container_sandbox.cpp` — a drift hazard (the three
  network-permission lists must stay in sync or user plugins get
  different capabilities on different OSes). Zero behaviour change.
- [ ] Extract `src/plugins/src/windows_utf8.hpp` with a single UTF-8
  →wide helper. Today `utf8ToWide` (`out_of_process_plugin_host_win32.cpp:277`)
  and `toWide` (`windows_app_container_sandbox.cpp:119`) are
  byte-identical modulo the parameter type (`std::string const&` vs
  `std::string_view`). Consolidate to `std::string_view`.
- [ ] Extract a `CatalogFetcherBase` CRTP base (or a shared plain
  helper) for `StreamdockCatalogFetcher` + `OpenDeckCatalogFetcher`.
  Today `setCacheDirOverride` / `effectiveCatalogUrl` / `cacheFilePath`
  / `loadFromCache` / `writeCache` / `loadBundledFallback` /
  `emitSnapshot` are structurally identical — the only genuine
  difference is the upstream wire-format translator. Adding a third
  upstream (e.g. a future AJAZZ native plugin directory) would
  triple the boilerplate. Estimated saving: ~150 lines.
- [ ] Extract `src/devices/streamdeck/src/akp_framing.hpp` with a
  templated `buildCmdHeader<CmdPrefix>(cmd)` + the
  `buildSetBrightness` / `buildClearAll` / `buildClearKey` common
  bodies. Today these are copy-pasted across `akp03.cpp` / `akp05.cpp`
  / `akp153.cpp`; a fourth AKP SKU would quadruple them.
- [ ] QML: extract `BaseButton.qml` from `PrimaryButton.qml` +
  `SecondaryButton.qml` (identical `accessibleName`/`Description`,
  identical `implicitHeight`, identical `leftPadding`/`rightPadding`,
  identical `Connections` block on `pressedChanged` → `ripple.trigger`,
  identical `Accessible.role`). Derived classes override only
  `background:` colors/borders and `contentItem:` text weight.
- [ ] CRTP-ify the QML singleton factory boilerplate shared by 8
  classes (`s_xxxInstance` static + `create(QQmlEngine*, QJSEngine*)`
  - `registerInstance(T*)`). Pattern duplicated in
    `branding_service.cpp`, `theme_service.cpp`, `autostart_service.cpp`,
    `tray_controller.cpp`, `device_model.cpp`, `profile_controller.cpp`,
    `plugin_catalog_model.cpp`, `property_inspector_controller.cpp`.
    A `template<class Derived> class QmlSingletonBridge` would collapse
    ~15 lines per class to ~3.
- [ ] Factor `tests/unit/sandbox_test_helpers.hpp` with the shared
  `argvContains(argv, needle)` + `makeSandbox<T>(perms)` helpers
  currently copy-pasted across `test_linux_bwrap_sandbox.cpp` /
  `test_macos_sandbox_exec_sandbox.cpp` / `test_windows_app_container_sandbox.cpp`.

### Magic numbers

- [ ] Replace the `for (int i = 0; i < 8; ++i)` poll-drain cap in
  `akp03.cpp:259` / `akp05.cpp:323` / `akp153.cpp:266` with a named
  `constexpr std::size_t kMaxReportsPerPoll = 8;` in a shared header
  (candidate: the new `akp_framing.hpp` above).
- [ ] `src/devices/streamdeck/src/register.cpp:71-95` uses hex
  literals (`0x0300`, `0x3001`, `0x5001`) for USB IDs that are already
  defined as `akp03::VendorId` / `akp05::VendorId` /
  `akp153::ProductId` in the protocol headers in scope. Replace the
  literals with the named constants so a VID/PID correction propagates
  via one edit instead of five.
- [ ] Move hard-coded QML tile sizes (`EncoderCard.qml` 120×120,
  `KeyCell.qml` 96×96, `PrimaryButton.qml` min-height 36) into
  `Theme.qml` so the spacing / sizing tokens live in one place and
  device-specific layouts can vary them declaratively.

### Consistency

- [ ] **QSettings double-key for "start minimised"** — the same
  concept is persisted under TWO keys by two different services:

  - `tray_controller.cpp:56` writes `"Window/StartMinimized"`
    (CamelCase group, US spelling).
  - `autostart_service.cpp:115-116` writes `"autostart/startMinimised"`
    (lowerCamel group, UK spelling).

  Today only `SettingsPage.qml:161-162` writes BOTH at once. A user
  toggling the tray menu updates only the first; the next auto-launch
  reads only the second → desync. Pick one key (recommend
  `"Window/StartMinimized"` per the `TrayController` docstring), make
  the other a read-on-startup migration shim, and delete after one
  release cycle.

- [ ] **C++ member-field prefix unified to `m_`** — the codebase is
  split 50/50 between `m_foo` (core + devices + ~7 app files) and
  `foo_` (8 app files: branding_service, theme_service,
  autostart_service, pi_bridge, pi_url_request_interceptor,
  single_instance_guard, tray_controller, property_inspector_controller).
  Pick `m_` and run a global rename.

- [ ] Align QSettings group naming — `"Appearance/Mode"` +
  `"Window/StartMinimized"` (CamelCase) vs `"autostart/startMinimised"`

  - `"Branding/ThemeOverride"` (mixed). Standardise on CamelCase
    groups.

- [ ] Unify shell-script naming under `scripts/` — kebab-case
  dominates (`bootstrap-dev.sh`, `doctor.sh`, `run-clang-tidy.sh`);
  `_create_issues.sh` + `_issues.json` use snake_case with underscore
  prefix. Either rename the underscored ones (if kept) or retire them
  per the dead-code bullet above.

### Docstring polish (cosmetic)

- [ ] Add `///` Doxygen blocks for the 12 trivial Q_PROPERTY readers
  in `branding_service.hpp:60-74` (accent, accent2, bgBase, bgSidebar,
  bgRowHover, fgPrimary, fgMuted — all one-liners returning a
  pre-populated member). Current coverage is 95%+; this closes the
  last 5%.
- [ ] Add one-liner docs for the two signals
  `launchOnLoginChanged(bool)` and `startMinimisedChanged(bool)` in
  `autostart_service.hpp:72-73`.

______________________________________________________________________

## Iceboxed (ideas, no commitment)

- Code signing for macOS / Windows release artifacts (needs publisher
  cert, see `docs/wiki/Release-Process.md#signing--notarization`).
- Sigstore signing of release artifacts (cosign keyless attestation).
- Telemetry (opt-in) for crash reports / anonymized device-mix stats.
- HID protocol fuzzer using the existing `tests/integration/fixtures/`
  as a corpus seed (libfuzzer + ASan, would catch regressions in
  AKP153/AKP05 frame parsers).
- AppImage build path alongside Flatpak (broader Linux distro reach).
