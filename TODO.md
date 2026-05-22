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

- [ ] Replace all undefined Qt.tint calls in Theme.qml with Qt.lighter/darker or a custom tint helper to avoid runtime errors on all platforms
  — **VALIDATION 2026-05-20: premise looks INVALID.** `Qt.tint(color, tint)` is a documented Qt 6 QML global, used 10× in `Theme.qml`, and the shipped light-theme contrast fix relies on it (it cleared the zero-`qmllint`-warnings bar and the 105/105 test run). No evidence of a runtime error. Close unless a concrete failure is reproduced.

- [ ] Implement a timeout or explicit reset for the syncGlyphState property in DeviceRow.qml after a successful sync to prevent stale success icons persisting in the UI

- [ ] Add proper error handling and logging for sandbox.decorate failures in out_of_process_plugin_host.cpp (POSIX) and out_of_process_plugin_host_win32.cpp (Windows) to abort launch when sandboxing cannot be applied

- [ ] Verify that the Windows AppContainer sandbox destructor always runs and guard the FreeSid call against null pointers to prevent resource leaks or crashes
  — **VALIDATION 2026-05-20: already satisfied.** `~Impl()` in `src/plugins/src/process_attributes_impl_win32.hpp` guards every `FreeSid` (`if (sid != nullptr)`, `if (appContainerSid != nullptr)`) and the token handle (`if (restrictedToken != nullptr)`); it is RAII so it runs deterministically on scope exit. The "verify" is done — keep only as a tracking note, or close.

- [ ] Create integration tests that spawn a child process under each sandbox implementation (AppContainer, bwrap, macOS exec) and assert that the intended isolation constraints are enforced

- [ ] Implement per-LED RGB buffer support in via_keyboard.cpp (currently throws runtime_error) or document the limitation

- [ ] Introduce feature‑flags or compile‑time guards around plugin code that depends on optional vendor libraries (QtSerialPort, OpenCV, libusb) to prevent crashes when those libraries are absent

- [ ] Clamp Theme.surfaceAt level argument to a minimum of zero and add documentation to avoid negative level misuse

### User actions (out-of-code, one-time)

- [ ] **AK 980 ACL — physical replug**: `setfacl` workaround was applied
  this session, but is reset at the next boot. After replugging the
  AK 980 PRO once, systemd-logind picks up the `TAG+="uaccess"` from the
  new udev rule and grants the ACL persistently.

### Medium-effort fixes (1–4 hours)

- [ ] **AKP05 (Stream Dock) image upload silently fails on Linux/hidraw —
  HID Report-ID framing.** 🐧 **Platform-specific; needs Fedora hardware to
  fix+verify.**

  **Symptom (observed 2026-05-21):** on Windows the AKP05E (`0x0300:0x3004`)
  shows our icon on key 1; on Fedora the same build shows nothing. The device
  **IS detected** in the app on Fedora (appears in the device list), so this is
  not enumeration/registration — it is the image *write* not reaching the panel.

  **Root cause (hypothesis, high confidence):** the Stream Dock packets put the
  ASCII `CRT` prefix at **byte 0** (`buildCmdHeader` → `pkt[0]=0x43`), i.e. there
  is **no leading HID Report-ID byte**. hidapi's `hid_write` contract is that
  `data[0]` is the Report ID (0x00 for single-/unnumbered-report devices) on
  *all* platforms. Windows `WriteFile` tolerates the missing report-id byte (the
  device receives `CRT…` from byte 0), but **Linux hidraw is strict**: it takes
  `buffer[0]` (0x43) as the Report ID, so the panel gets misaligned/garbage and
  renders nothing. This exact convention is already documented in
  `docs/protocols/keyboard/via.md:10` — *"byte 0 : report id (0x00 on
  Linux/macOS, omitted on Windows WriteFile)"*.

  **Architectural complication (do NOT do a blanket transport fix):**
  `HidTransport::write()` calls `hid_write` identically on all platforms, but the
  backends DISAGREE on byte 0:

  - **Streamdeck** (`akp03/akp05/akp153/akp815`): byte 0 = `'C'` (no report id).
  - **Keyboard** (`proprietary_keyboard.cpp`): byte 0 = `0x00` report id already
    (time-sync data packet, TFT chunks).
  - **Mouse** (`aj_series`): byte 0 = `kReportId` (0x05) / 0x00 for the clock.

  A blanket "prepend 0x00 on Linux" in `HidTransport::write` would fix streamdeck
  but **double-prefix the keyboard/mouse** (which already carry a report-id byte)
  and break them on Linux. So the fix must be either (a) targeted to the
  streamdeck backends only, or (b) a unification of the byte-0 convention across
  all backends (larger; touches every builder + test + the Windows path).

  **Diagnostic tests to run on Fedora (pin the failure point before coding):**

  1. ✅ Device detected in the app device list — confirmed 2026-05-21.
  1. Run from a terminal capturing stderr and inspect the log:
     `./ajazz-control-center 2> ak.log` then
     `grep -iE "akp05|streamdeck|opened|write|hid|permission|denied" ak.log`.
     - Does it log `opened VID=0300 PID=3004`? (if not → udev/permissions, step 3)
     - Do the image `write()`s return success (>0) or error? A *successful* write
       with no panel change ⇒ confirms the report-id framing hypothesis.
  1. udev / hidraw permissions:
     `ls /etc/udev/rules.d/ | grep -i ajazz` and `ls -l /dev/hidraw*`.
     - Ensure `resources/linux/70-ajazz.rules` is installed (VID `0300` Stream
       Dock family) and the `/dev/hidraw*` node for `0300:3004` is user-accessible
       (`uaccess`); replug or `udevadm trigger --action=change` if ACLs are stale.
  1. Cross-check with the AKP153 (`0x0300:0x1001`) on the SAME Fedora box — it
     uses the identical `CRT`-at-byte-0 framing. If AKP153 image upload ALSO
     fails on Linux, the report-id issue is family-wide (all Stream Dock); if
     AKP153 works, the problem is AKP05-specific (1024-byte packet size, the
     0x3004 firmware, or the secondary-screen path).

  **Candidate fix (pending the diagnostics):** if step 2 confirms write-succeeds-
  but-no-render, give the streamdeck output reports a leading `0x00` report-id
  byte on Linux/macOS only (mirroring `via.md`), e.g. a small platform-guarded
  helper in the streamdeck backends (NOT in shared `HidTransport::write`). Then
  **regression-test BOTH**: re-confirm Windows still shows the key-1 icon, and
  Fedora now does too. Add a unit test pinning the on-wire byte 0 per platform if
  feasible.

  Files: `src/devices/streamdeck/src/akp05.cpp` (`buildCmdHeader`, `sendImage`/
  key-image path), `src/core/src/hid_transport.cpp` (`write`), `docs/protocols/ streamdeck/akp05.md`. Related: the Linux note in `via.md:10`.

  **✅ HARDWARE-CONFIRMED on Fedora 2026-05-22 (branch `feat/linux-device-support`).**
  The fix landed at the transport (not per backend): `makeHidTransport` gained a
  `prependReportIdPosix` flag (default false) that the four streamdeck
  constructors (`akp03/akp05/akp153/akp815.cpp`) set to `true`;
  `HidTransport::write()` prepends a single `0x00` report-id byte **under
  `#ifndef _WIN32` only**, so Windows is byte-for-byte unchanged and only
  Linux/macOS get the report-number byte hidraw expects. Verified: Windows MSVC
  build + tests pass; GCC `-Werror` clean.

  **Fedora confirmation:** a `CRT LIG` brightness probe on the live AKP05E
  (`0x0300:0x3004`, IF0 = `/dev/hidraw14`) with the `0x00` report-id prepend
  (1025 B on the wire) made the panel brightness pulse bright↔dim — every write
  ACKed full-length and the device acted on the packet. That confirms the
  report-id-prepend **OUT write path** (the same path key-image upload rides) is
  byte-aligned on hidraw, closing the root-cause framing question. Remaining (not
  a framing bug): drive an end-to-end key-image render through the app UI (AKP05E
  backend maturity is "scaffolded") and confirm the icon appears on key 1.

  **Fedora 44 test plan (do these on the device):**

  ```bash
  git fetch origin && git checkout feat/linux-device-support
  # one-time device access:
  sudo cp resources/linux/70-ajazz.rules /etc/udev/rules.d/ \
    && sudo udevadm control --reload-rules && sudo udevadm trigger --action=change
  cmake --preset linux-release && cmake --build --preset linux-release
  ctest --preset linux-release            # 365 tests must pass under GCC/-Werror too
  ./build/linux-release/ajazz-control-center 2> akp05.log
  ```

  Then verify and record:

  1. **Does the AKP05 first-key icon now render on Fedora?** (the headline check)
  1. `grep -iE "opened VID=0300|akp05|streamdeck|write|hid" akp05.log` — the
     device should open and the image `write()`s should not error.
  1. If it STILL does not render: try the AKP153 (`0x0300:0x1001`) on the same box
     to see whether the issue is family-wide; capture
     `udevadm info -a /dev/hidrawN` for the `0300:3004` node; and confirm the
     `0x00` prepend is actually firing (Linux build, so `_WIN32` is undefined).
  1. If it renders on Fedora but you later see a regression on Windows, that is
     the platform guard — re-confirm the `#ifndef _WIN32` boundary.

- [x] **Make the AJ-series mouse battery (+ OLED clock) work on Linux/Fedora.**
  ✅ **HARDWARE-CONFIRMED on Fedora 2026-05-22.**

  **✅ Resolved (2026-05-22, commit `9019682`):** the mouse battery now reads on
  Fedora — the app logs `[battery] queried ajazz_24g_8k: 100%` and the UI shows
  the percent. The shipped read was a **two-step handshake**: SET_FEATURE a
  `0x83` GET_BATTERY poke, then GET_FEATURE the status report. The status report
  uses **report-id `0x00`**, so the frame is `[00, 00, charge, 01 01 01 02]` —
  charge at **byte 2** (the old "byte 3 / report-id 0x05" framing was off by one
  and rejected every valid frame). The OLED clock (`0x28`) and the usage-`0x02`
  control-collection selection were also confirmed live. Historical
  investigation notes below.

  **Status (historical):** on Windows the mouse battery reads correctly (commit `376fb61`:
  vendor status report `0x05`, via GET_FEATURE on the `0xFFFF`/usage-0x02
  control collection) and the OLED clock sets (commit `0a1952e`: opcode `0x28`
  with the 0xD7 marker via SET_FEATURE on the same collection). Both reads/writes
  go through the vendor control collection selected by `controlUsagePage=0xFFFF`

  - `controlUsage=0x02` (commit `69c64a1`). The whole feature must be confirmed
    on Fedora.

  **Why it may not work out-of-the-box on Fedora — two independent gates:**

  1. **udev / hidraw permissions (device access).** The mouse is VID `0x3151`
     (SONiX). The app opens `/dev/hidraw*` directly; without a udev rule granting
     the logged-in user access, `hid_open_path` fails and battery/clock silently
     return nothing.

     - Install the project rules: `sudo cp resources/linux/70-ajazz.rules /etc/udev/rules.d/ && sudo udevadm control --reload-rules && sudo udevadm trigger --action=change` (VID prefix `3151` is covered there).
     - Verify the control node is user-accessible:
       `ls -l /dev/hidraw*` — the `0x3151:0x5007` MI_02 node should carry an ACL
       for your user (`getfacl /dev/hidrawN` shows `user:<you>:rw-` when `uaccess`
       applied). If stale (device was plugged in before the rule landed), replug
       or `sudo udevadm trigger --action=change`.

  1. **HID usage disambiguation on hidraw (the likely code gate).** The mouse
     exposes TWO `0xFFFF` collections (usage 2 = control, usage 1 = not). On
     Windows hidapi reliably reports `usage`, so `controlUsage=0x02` picks the
     right node. On **Linux hidraw, `hid_enumerate`'s `usage`/`usage_page` can be
     0 (unpopulated) for non-primary collections**; then `HidTransport::open`'s
     match (`usage_page==m_usagePage && (m_usage==0 || usage==m_usage)`) finds no
     candidate and **falls back to `hid_open(vid,pid)` = the first interface (the
     boot mouse)**, where the battery/clock feature reports do not exist → silent
     no-op on Fedora.

  **Diagnostic on Fedora (run from a terminal, capture stderr):**

  ```
  ./ajazz-control-center 2> mouse.log
  grep -iE "opened VID=3151|aj_series|battery|queried|hid_open|usage" mouse.log
  ```

  - Expect `opened VID=3151 PID=5007 (usage-page filtered)` + `queried ajazz_24g_8k: NN%`.
  - If you see `opened VID=3151 …` **without** "(usage-page filtered)" or no
    `queried ajazz_24g_8k` line ⇒ it opened the wrong interface ⇒ the hidraw
    usage-unpopulated fallback (gate 2).
  - Independently confirm which hidraw node is the control channel:
    `for n in /dev/hidraw*; do udevadm info -q property $n | grep -E "HID_NAME|HID_PHYS|HID_UNIQ"; echo $n; done` and/or
    a quick `python3 -c "import hid;[print(hex(d['usage_page']),hex(d['usage']),d['path']) for d in hid.enumerate(0x3151,0x5007)]"`
    to see whether `usage` is populated on this kernel (cython-hidapi reads the
    report descriptor; if it shows `0xffff/0x02` then the C++ should match too).

  **Fix (if gate 2 is confirmed):** make `HidTransport::open` resilient to an
  unpopulated `usage` on hidraw — two-pass match: first try `usage_page` **and**
  `usage`; if no candidate matched AND no enumerated entry for this VID:PID
  reported a non-zero `usage` at all, fall back to a `usage_page`-only match
  (still selecting a `0xFFFF` node) before the final `hid_open(vid,pid)` fallback.
  Keep Windows behaviour identical (where `usage` is populated, the strict match
  wins). Add a log line distinguishing "usage+page filtered" vs "page-only
  filtered" vs "first-interface fallback" so future Linux triage is one grep.
  Re-verify on BOTH OSes: Windows still `queried ajazz_24g_8k`, Fedora now does
  too. Files: `src/core/src/hid_transport.cpp` (`open()` match loop).

  Note: the same hidraw `usage`-unpopulated risk applies to the AK980 PRO
  keyboard, but its control collection `0xFF13` is a SINGLE collection
  (`controlUsage=0`, matched by usage page alone) so it is unaffected.

  **🟢 IMPLEMENTED on branch `feat/linux-device-support` (`db21686`) — pending
  Fedora hardware confirmation.** `HidTransport::open()` now does the two-pass
  match described above (pass 1 `usage_page`+`usage`, pass 2 `usage_page`-only
  fallback for hidraw's unpopulated `usage`), and logs which match kind won
  (`usage+page filtered` / `usage-page filtered` / `first-interface` / `default`).
  Windows is unaffected (it reports `usage`, so pass 1 wins). Verified: Windows
  MSVC build + 365 tests; the two-pass logic compiles under
  `g++ -std=c++20 -Wall -Wextra`.

  **Fedora 44 test plan (after the build steps in the AKP05 item above):**

  ```bash
  ./build/linux-release/ajazz-control-center 2> mouse.log
  grep -iE "opened VID=3151|aj_series|battery|queried" mouse.log
  # direct device probe (bypasses the app — pins app-vs-device):
  python3 scripts/aj_mouse_probe.py --enumerate     # is 'usage' populated on hidraw?
  python3 scripts/aj_mouse_probe.py --battery       # reads report 0x05 byte 3
  ```

  Then verify and record:

  1. Log shows `opened VID=3151 PID=5007 (usage+page filtered)` **or**
     `(usage-page filtered)` — either is the control collection (good). If it
     shows `(first-interface)` or `(default)`, the match dropped through ⇒ open a
     follow-up (the hidraw enumeration didn't expose 0xFFFF at all → may need a
     report-descriptor-based selection).
  1. `queried ajazz_24g_8k: NN%` appears (battery works) and the **OLED basetta
     clock** follows a manual "Sync time" from the app (clock 0x28 works).
  1. If `aj_mouse_probe.py --battery` reads a % but the **app** does not, that is
     still an app-side interface-selection gap (report it with the
     `--enumerate` output so the exact `usage`/`usage_page` values are known).
  1. After a wireless replug, the battery should stay grey then jump to the real
     value (no transient 1% — frame validation from commit `1f2be0c`).

- [ ] **AKP05 v3 framing migration**. Per `[mirajazz]`'s protocol-version
  taxonomy (see `docs/protocols/streamdeck/_research-sources.md`), the
  Mirabox N4 / AKP05 family is a **protocol_version 3** device with
  **1024-byte packets** and native press/release events. Our current
  `akp05_protocol.hpp` still hardcodes `PacketSize = 512`. When the
  first real capture lands, gate the packet size on a detected protocol
  version (mirroring `Kind::is_v2_api()` in `[ajazz-sdk]`) and adapt
  `buildCmdHeader` accordingly. Tests must keep covering both 512- and
  1024-byte paths.

- [ ] **AKP03 v2 framing migration**. Same shape as the AKP05 entry
  above: `[ajazz-sdk]/info.rs::Kind::Akp03::is_v2_api()` is true, so
  AKP03 sends 1024-byte packets. Our backend currently still ships 512;
  the change requires bumping `PacketSize` plus widening every chunk
  loop in `akp03.cpp`. Confirm against a USB capture before flipping.

- [ ] **AKP815 dedicated factory + image pipeline**. The newly registered
  AKP815 backend (`src/devices/streamdeck/src/akp815.cpp`) currently
  reuses the AKP153 image-transmit helpers, which means the per-key
  image is sent verbatim without the 100×100 / Rot180 transform
  documented in `docs/protocols/streamdeck/akp815.md`. When the image
  pipeline ("phase 2") lands, branch on `displayInfo()` to apply the
  right resize + rotation per device. Until then, callers must
  pre-transform the JPEG themselves.

- [ ] **AKP05 placeholder VID:PID retirement**. The `0x0300:0x5001`
  pair we shipped before 2026-05-14 was a placeholder with no public
  source. The canonical Mirabox N4 ID (`0x6603:0x1007`) is now also
  registered in parallel. Once a capture confirms the AJAZZ-branded
  AKP05's real VID:PID (vendor pages do not list it), delete the
  placeholder from `register.cpp` and `devices.yaml`.

- [ ] **AKP153 release-edge synthesis**. `akp153::parseInputReport`
  returns `pressed = true` for every transition because the firmware
  emits a single frame per press/release edge with no byte-10 polarity.
  Today the backend silently reports only press events. Diff successive
  frames inside `Akp153Device::poll()` to synthesise the matching
  `KeyReleased` event so consumer code sees uniform paired transitions
  (the AKP03 backend already does this through the v3 polarity bit).

- [ ] **SEC-013 — strip JPEG metadata before logo upload**. `[pyajazz]`
  documents that uploading an EXIF-bearing JPEG to the AKP153 boot-logo
  endpoint can **brick the device**. Our future image pipeline must
  strip every non-essential JFIF/EXIF/IPTC chunk before pushing to
  `LOG` opcodes. Add a regression test that verifies a metadata-heavy
  JPEG is rejected with a clear error.

- [ ] **Streamdock 0x0300:0x3004 SKU identification**. Hot-plug capture
  from 2026-05-13 surfaced this PID as "Ajazz HOTSPOTEKUSB HID DEMO".
  Open issue with vendor support + scan AliExpress / Mirabox stores
  for any SKU that enumerates with this PID. Until then the device
  defaults to the AKP03 backend; a wrong factory match would fail
  silently rather than report a clear error.

- [ ] **Snap packaging — first publish to the edge channel**.
  Recipe scaffold + maintainer guide already shipped:

  - `packaging/snap/snapcraft.yaml` — concrete manifest mirroring
    the Flatpak file (`core24` base, `kde-neon-6` extension, the
    manually-connected `raw-usb` + `hidraw` plugs for the device
    backends).
  - `docs/wiki/Snap-Packaging.md` — full guide covering local
    build, the QtWebEngine sandbox-in-snap workaround, manual-
    review pitfalls (`raw-usb` justification), and the four-channel
    release ladder.

  Open work to actually publish:

  - Run `snapcraft pack --use-lxd packaging/snap` end-to-end on a
    clean Ubuntu 24.04 box (CI doesn't have LXD; see
    `docs/wiki/Snap-Packaging.md#local-build`).
  - Add the `snap-build` job to `.github/workflows/release.yml`
    using `snapcore/action-build@v1` (skeleton in the wiki).
  - Run `snapcraft export-login` once with the
    `Aiacos/ajazz-control-center` Snap Store account, store the
    token as the `SNAP_STORE_TOKEN` repo secret.
  - First push to `edge`; soak for one week then promote to
    `beta` per the wiki's release ladder.
  - File the manual-review request for the `raw-usb` plug with the
    Snap Store team (linking to the device list page on the wiki
    as justification).

- [ ] **Square brand asset** for tray / app icon. The wordmark in
  `resources/branding/ajazz-logo.png` is a 3:1 banner; a centered crop
  produces mostly whitespace and looks worse than the current
  geometric placeholder. Either ask AJAZZ for a square logo or design a
  custom monogram inspired by the wordmark.

- [ ] **profile-buttons — wire Apply / Revert / Restore defaults to real
  paths** (surfaced by source-level `TODO(profile-buttons)` at
  `src/app/qml/Main.qml:111`). Today the three ProfileEditor buttons
  toast `"not implemented yet"` because `ProfileController` lacks the
  default-path resolution (`QStandardPaths::AppDataLocation/profile.json`)

  - a "Save as" file dialog for explicit paths. Wire-up needs:
    (1) `ProfileController::saveProfile()` / `loadProfile()` no-arg
    overloads that pick the default path, (2) a `QFileDialog` trigger for
    "Save as" / "Open profile…", (3) decide what to do about
    `PropertyInspector.qml` / `NativePropertyInspector.qml`, which are
    currently dead QML (no `PropertyInspector{}` or `NativePropertyInspector{}`
    instantiation anywhere — `Main.qml:124` uses only `Inspector {}`).
    Either re-hook them once the buttons actually save something the
    inspector can re-render, or delete them from the QML module. ≈ 1 day.

- [ ] **via_keyboard — per-LED RGB matrix path** (source-level stub
  `throw std::runtime_error` at `src/devices/keyboard/src/via_keyboard.cpp:185`).
  Today we speak the VIA `id_custom_set_value` command (cmd `0x08`)
  against the `qmk_rgblight` channel (`0x01`) — sub-commands `0x02`
  (brightness), `0x03` (effect), `0x04` (solid color). Per-LED keying
  is a different VIA surface: `qmk_rgb_matrix` (channel ID is not
  universal — newer firmwares advertise it as `0x03`, but the numeric
  assignment can vary by build). Gate plan:
  (a) at device-open time probe the supported VIA channels via
  `id_custom_get_value` + `id_dynamic_keymap_get_layer_count` (or the
  newer `id_get_supported_endpoints` when the firmware exposes it) to
  confirm which channel number this particular board uses for RGB
  matrix, (b) flip a `KeyboardCapabilities::hasRgbMatrix` flag based on
  the probe result, (c) implement the write path — per-LED payload is
  `{ led_index, r, g, b }` per LED — behind that gate. Until then the
  hard exception is intentional: callers are expected to check
  capabilities before calling. ≈ 1 day.

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

- [ ] **MacroRecorder — real native back-ends on all three OSes**
  (source-level `(TODO)` tags in
  `src/core/include/ajazz/core/macro_recorder.hpp:14-15` and
  `src/core/src/macro_recorder.cpp:10-12`). Today
  `makeDefaultMacroRecorder()` returns a `StubRecorder` on every
  platform — `start()` / `stop()` just log "stub recorder started /
  stopped" so the UI workflow can be smoke-tested end-to-end, but no
  real events are captured. The header mentions a build-time gate
  `AJAZZ_FEATURE_MACRO_RECORDER`, but the option is **not wired**
  through CMake yet; adding it is the first step. Platform plan:

  - **Linux**: evdev (`/dev/input/eventN`) via a dedicated reader
    thread. Requires membership in the `input` group (rule already
    shipped in `resources/linux/70-ajazz.rules`) or CAP_DAC_READ_SEARCH.
  - **macOS**: `CGEventTap` via Accessibility + "Input Monitoring"
    permissions (System Settings → Privacy). Translate `CGEventFlags`
    - keycode into `MacroEvent`.
  - **Windows**: `SetWindowsHookExW(WH_KEYBOARD_LL, ...)` low-level
    hook in a dedicated thread. Translate the `KBDLLHOOKSTRUCT` into
    `MacroEvent`. Beware: DirectInput-consumed events are invisible to
    WH_KEYBOARD_LL; a secondary `SetWindowsHookExW(WH_MOUSE_LL)` hook
    covers the mouse side.
    All three back-ends ship disabled by default; the runtime UI prompts
    the user to enable the OS permission before the first capture.
    ≈ 1 day per platform + 0.25 day for the CMake option wiring.

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

### Security hardening

- [ ] **S7 — Substring-only profile validator** (LOW, intentional —
  `src/core/src/profile_io.cpp:123-153`): the in-tree `containsKey`
  scanner is documented as a tiny shim; replace with the Qt-side
  `QJsonDocument` parser when the app-layer reader lands. Tracking
  bug only — no behavior change needed yet.

### Plugin SDK + Store (multi-week, milestone-level)

> User-requested in this session. **Not autonomous-feasible** end-to-end;
> document and break down so the work can be parallelized.

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
- [ ] **Catalog backend** (registry, ratings, version pins, Sigstore
  signing). Server-side, ≈ 2-3 weeks; out of repo until protocol stabilises.
- [ ] **Stream Deck plugin compat layer** (translate Elgato manifests
  - WS messages to ours; Property Inspector iframe quirks). ≈ 1-2 weeks.

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
