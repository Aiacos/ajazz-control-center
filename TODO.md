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
- [ ] **README + wiki screenshots** of the Material UI in light and dark
  mode (replace stale Fusion screenshots).

### User actions (out-of-code, one-time)

- [ ] **Enable GitHub Wiki**: visit
  `https://github.com/Aiacos/ajazz-control-center/wiki/_new` and save any
  page once. After that, every push to `main` syncs `docs/wiki/`
  automatically (the workflow already handles the chicken-and-egg case
  gracefully — it just won't actually mirror anything until you click save
  the first time).
- [ ] **Enable Dependency Graph**: Settings → Code security & analysis →
  Dependency graph → Enable. Then `dependency-review.yml` becomes
  effective on dependabot PRs (it currently has `continue-on-error: true`
  while the prereq is disabled).
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
- [ ] **Audit finding S8 — narrow udev rules** (`resources/linux/99-ajazz.rules`):
  drop the three `SUBSYSTEM=="usb"` lines so only `KERNEL=="hidraw*"`
  grants `uaccess`. **Risk**: untested whether hidapi's libusb backend
  fallback is ever hit in practice on Linux. Validate with each backend
  before removing.

### Architecture refactors (multi-day, milestone-level)

> Findings from the architectural review pass earlier in the session.
> Each one is a self-contained milestone; pick by current pain.

- [ ] **A1 — DeviceRegistry singleton → DI**: replace
  `DeviceRegistry::instance()` with constructor-injected ownership in
  `Application`. Keeps the singleton as a transition shim so device
  backends keep compiling. **Why**: per-test isolation, no hidden global
  state. Touches every backend `register*.cpp`.
- [ ] **A2 — ActionEngine threading model**: `ActionEngine::run()`
  currently calls `std::this_thread::sleep_for` on the calling thread
  (typically the HID poll thread or the Qt main thread). Introduce an
  explicit executor (QThreadPool task or dedicated worker queue) and
  drop the default in-place sleep. **Why**: Sleep actions must not
  block HID polling or freeze the UI.
- [x] **A3 — EventBus per-event allocation** ✅ shipped. `event_bus.cpp`
  now holds the subscribers in a `std::atomic<std::shared_ptr<vector<…> const>>`
  swapped via copy-on-write on subscribe / unsubscribe; `publish()` is
  lock-free (atomic-load + iterate immutable snapshot). Writers
  serialise on a write-only mutex; readers don't take a mutex.
  Existing 4/4 EventBus tests still pass under TSan.
- [ ] **A4 — PluginHost out-of-process**: pybind11's
  `scoped_interpreter` runs CPython in-process; a segfault in any
  C-extension imported by a plugin (numpy, opencv, mido) crashes the
  whole app. Move the host out-of-process via subprocess + Unix socket
  (or seccomp-bpf sandbox if staying in-process). **Why**: bake
  isolation in *before* exposing user-installable plugins; retrofitting
  is 10× more expensive.
- [ ] **A5 — Logger global → injectable sink**: `core::log()` is a free
  function with a global `std::atomic<LogLevel>`. Introduce a `LogSink`
  interface and `setLogSink(unique_ptr<LogSink>)`; keep the macros
  unchanged so call sites don't touch. **Why**: per-test log capture,
  routing to journald / syslog without TU patches.
- [ ] **A6 — Application destructor drain** ✅ shipped in `0ca47c6`.

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
- [ ] **Property Inspector embedding** (Qt WebEngine for HTML PI, with
  bridged messages to the plugin process). ≈ 3-5 days.
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
- [x] **AJAZZ Streamdock store integration — schema + UI scaffolding**:
  manifest schema now exposes `Ajazz.Compatibility.Mode = "streamdock"`
  alongside `streamdeck` / `opendeck`, plus an opaque
  `StreamdockProductId` for catalogue-mirror lookups. The Plugin Store
  UI surfaces upstream entries under a first-class “AJAZZ Streamdock”
  tab with a dedicated info banner; `PluginCatalogModel` rows carry a
  `source` field (`local` / `community` / `streamdock`) used by the tab
  filter, and the side-sheet details pane labels the source as “AJAZZ
  Streamdock store” for those rows.
- [ ] **AJAZZ Streamdock store — live catalogue fetch**: reverse-engineer
  the Streamdock desktop app's catalogue endpoint (URL, auth, JSON
  shape), build a cached mirror that translates Streamdock manifests
  into our schema, expose verified / signed metadata where available,
  and let users one-click install Streamdock plugins through the same
  lifecycle manager. Includes a settings toggle to opt out of the
  upstream catalogue for offline / air-gapped builds. ≈ 1-2 weeks once
  the endpoint is documented; depends on **Plugin lifecycle manager**.

**Total realistic estimate**: 6-10 weeks of focused engineering for a
v1; backend catalog and the AJAZZ Streamdock store bridge are parallel
workstreams.

### UI polish (incremental)

- [ ] **Material 3 expressive theming** beyond the basic style switch:
  custom typography ramp, elevation tokens, motion specs (entrance /
  exit transitions), elevated buttons with proper ripples.
- [ ] **Empty state polish** for `DeviceList` when zero devices online
  (illustration + onboarding hint).
- [ ] **Toast notifications upgrade** to `QtQuick.Controls.Material`'s
  Snackbar pattern.
- [x] **Settings page** (`src/app/qml/Settings.qml`) — done. Material-styled
  page exposes `themeService.mode`, `autostart.launchOnLogin`, and
  `autostart.startMinimised` / `tray.startMinimized` via RadioButton +
  SwitchDelegate. Component is registered in the QML module; wiring it
  into the sidebar/StackView is a follow-up (Main.qml is currently a
  fixed 3-pane layout).

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
