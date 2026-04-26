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
- [ ] **Decompile / disassemble the AJAZZ desktop apps under a clean-
  room policy**: run the installers in a disposable VM, extract the
  Electron / .NET / Qt payloads, decompile with the appropriate
  toolchain (`asar` + `js-beautify` for Electron, ILSpy / dnSpyEx
  for .NET, Ghidra / IDA for native binaries) and produce a written
  protocol & feature inventory at
  [`docs/research/vendor-protocol-notes.md`](docs/research/vendor-protocol-notes.md)
  (scaffold + methodology landed in this cycle; sections per device
  family wait for first capture). **Rules**: only one engineer reads
  vendor sources; that engineer writes specs but does not contribute
  to the matching module; a second "clean" engineer implements from
  the spec. Capture USB / WebSocket / IPC traffic with Wireshark +
  usbmon to cross-validate the static analysis. ≈ 3-5 days.
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
- [ ] **Protocol parity backlog**: file one TODO entry per missing
  feature surfaced by the gap analysis (per-key RGB ramp commands,
  custom macro op-codes, vendor-specific HID reports, firmware-
  upgrade USB DFU sequence, dial haptic patterns, OSD overlay
  triggers, etc.) and link them back into the **Plugin SDK + Store**
  and **Architecture refactors** sections of this file so the
  parity work is scheduled, not forgotten. Blocked on: at least one
  recon-confirmed row of `vendor-feature-matrix.md` flipping from
  ❓ to a verified vendor capability. ≈ 0.5 day once the recon pass
  surfaces material.
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
  (Linux) shipped this cycle. POSIX `OutOfProcessPluginHost`
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

  Slice 3c (next): macOS `sandbox-exec` profile + Windows
  `_spawnvp` port + AppContainer / restricted token + removal of
  the in-process `PluginHost` once every caller has migrated.

- [x] **A5 — Logger global → injectable sink** ✅ shipped. New @c LogSink
  abstract base in `ajazz/core/logger.hpp`; default `StderrSink`
  reproduces the legacy formatting. `setLogSink(shared_ptr<LogSink>)`
  swaps the active sink atomically against concurrent log() calls
  (slot is `std::atomic<std::shared_ptr<LogSink>>`). All AJAZZ_LOG\_\*
  macros unchanged; call sites untouched. Tests can now install a
  capturing sink and assert on what subsystems logged.

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

- [ ] **Material 3 expressive theming** beyond the basic style switch:
  custom typography ramp, elevation tokens, motion specs (entrance /
  exit transitions), elevated buttons with proper ripples.
- [ ] **Empty state polish** for `DeviceList` when zero devices online
  (illustration + onboarding hint).
- [ ] **Toast notifications upgrade** to `QtQuick.Controls.Material`'s
  Snackbar pattern.
- [ ] **Light-theme `DeviceList` tile contrast** — surfaced by the
  README screenshots in this cycle (`docs/screenshots/main-light.png`).
  When `Appearance/Mode = light` resolves a near-white `bgSidebar`
  (`#ebebef`), the device cards in the sidebar render almost-black on
  near-white instead of the expected light-on-light row hover. The
  delegate in `src/app/qml/DeviceList.qml` is likely binding to a
  hardcoded dark fill (or to a Material attached property that does
  not follow `themeService.mode`) instead of the light-theme
  `Theme.bgRowHover` / `Theme.bgBase`. WCAG ratios in
  `resources/branding/theme-light.json` are calibrated assuming the
  card uses `bgRowHover #dedee5` on `bgSidebar #ebebef`; the actual
  rendering proves that's not what's bound. Fix by tracing the device-
  card background binding back to `Theme.qml` and verifying the colors
  swap on every theme transition.
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
