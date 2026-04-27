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
  (Linux) + 3c (macOS) + 3d (Windows port, runtime-validated on CI in
  this cycle) + 3e (legacy backend retired) shipped this cycle. The
  remaining ☐ is slice 3d-ii (`WindowsAppContainerSandbox`) — see the
  bottom of this entry. POSIX
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

  Slice 3d-ii (next, security PR): `WindowsAppContainerSandbox`.
  Windows AppContainer + restricted token are configured at
  `CreateProcessW` time via `STARTUPINFOEX::lpAttributeList`,
  not via a wrapper executable, so the existing
  `Sandbox::decorate(argv)` interface needs a side-channel — likely
  a second virtual method like `applyToProcessAttributes(...)` that
  is a no-op on Linux/macOS and the active path on Windows.

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

- [ ] **Material 3 expressive theming** beyond the basic style switch:
  custom typography ramp, elevation tokens, motion specs (entrance /
  exit transitions), elevated buttons with proper ripples.
- [ ] **Empty state polish** for `DeviceList` when zero devices online
  (illustration + onboarding hint).
- [ ] **Toast notifications upgrade** to `QtQuick.Controls.Material`'s
  Snackbar pattern.
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

## Iceboxed (ideas, no commitment)

- Code signing for macOS / Windows release artifacts (needs publisher
  cert, see `docs/wiki/Release-Process.md#signing--notarization`).
- Sigstore signing of release artifacts (cosign keyless attestation).
- Telemetry (opt-in) for crash reports / anonymized device-mix stats.
- HID protocol fuzzer using the existing `tests/integration/fixtures/`
  as a corpus seed (libfuzzer + ASan, would catch regressions in
  AKP153/AKP05 frame parsers).
- AppImage build path alongside Flatpak (broader Linux distro reach).
