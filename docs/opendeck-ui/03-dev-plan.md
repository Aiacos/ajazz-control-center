# OpenDeck UI — Development plan & verification

## Phases

Shared first, then the two UIs in parallel, then cleanup.

### Phase 0 — Foundations (config + seam) — DOING

- `UiModeResolver` (env → `ui.conf` → default `qml`); expose `App.uiMode`;
  add debug-channel `ui.mode get/set`.
- `main.cpp` selects the root surface by mode.
- Vendor the OpenDeck frontend under `src/app/webui/` (+ `LICENSE.md`,
  `NOTICE` with upstream commit); CMake target to `vite build` → stage to qrc.
- **Verify**: app launches in each mode (qml = current UI for now; webui =
  blank host that loads the bundled SPA), `screenshot` RPC works in both.

### Phase 1 — webui seam (Tauri shim + bridge skeleton)

- `tauri-shim.js`: implement `__TAURI_INTERNALS__.invoke` + event internals →
  QWebChannel object; inject before app modules in the hosted `index.html`.
- `OpenDeckBridge` C++: register on QWebChannel; implement the **read** commands
  (`get_devices/get_profiles/get_selected_profile/get_categories/get_settings/ get_localisations/get_build_info/get_fonts/get_port_base/make_info/ list_plugins/get_applications`) against our singletons.
- **Verify**: OpenDeck main screen renders our real AKP05E + Default profile +
  our installed actions; `screenshot` vs `00-research.md` mainmenu reference.

### Phase 2 — webui write-path + rendering + events

- Implement write commands + `update_image`→`set_image`, and emit the 10 events.
- **Verify**: bind/move/remove on the canvas drives the real device (key render
  on the AKP05E via sidecar); profile/plugin/PI flows; live `update_state`.

### Phase 3 — qml native UI

- Build `OpenDeckShell/DeviceCanvas/ActionList/Inspector/ParentActionView/ ProfileManager` to the OpenDeck layout + tokens (`00-research.md`), wired to
  the same singletons (mapping table `01-contract.md`); reuse existing
  PluginStore/Settings/PropertyInspector.
- **Verify**: feature + visual parity with the webui path and the OpenDeck
  reference screenshots, driven through `qml.*` + `screenshot`.

### Phase 4 — Plugin-layer Mirabox additions

- `.sdPlugin` loader: accept `"Nodejs"` + `"Knob"`/`"Information"` manifest
  extensions; add Space/StreamDock-Plugins as a catalog source.
- Cross-check geometry/input vs StreamDock-Device-SDK source.

### Phase 5 — Cleanup

- Retire superseded QML once the native UI reaches parity (keep shared
  services); remove the old ad-hoc layout patches; `ctest` green on 3 compilers;
  docs current.

## Verification system (NOT screenshots alone)

Two independent layers, every change:

1. **Functional (authoritative)** — drive the real backend headless via the
   debug-control channel and assert behaviour:
   - `device.list` / `profile.*` / `plugin.*` to set up state;
   - confirm a bind actually renders on the device (sidecar `set_image`),
     input routes, profile switches, plugin actions list — the gate that has
     historically caught wiring bugs unit tests miss.
   - webui extras: a bridge-level command log + assertions that each `invoke`
     hit the right singleton and returned the right shape.
1. **Visual (corroborating)** — `screenshot` RPC (grabs the QQuickWindow incl.
   the `WebEngineView`) compared against the OpenDeck reference screenshots
   captured in `00-research.md`. Read the screenshot; never trust it alone.

Reference images: OpenDeck `.github/readme/{mainmenu,multiaction,plugins, profiles}.png` (mirrored description in `00-research.md`). The live OpenDeck app
or its built SPA (served locally) can produce more references if needed.

No standalone Chrome is installed; the embedded **QtWebEngine** is the render
target and the `screenshot` RPC is the capture path (more faithful than an
external browser).

## Status log

- 2026-06-22: research complete (4 sub-agent studies); architecture chosen
  (dual impl, config switch); OpenDeck frontend builds to static SPA
  (validated); shim targets + 35-cmd/10-event contract + data types captured;
  docs written.
- 2026-06-22: **Phase 0 config seam DONE + verified.** `UiModeResolver`
  (`src/app/src/ui_mode_resolver.{hpp,cpp}`): env `AJAZZ_UI_MODE` → QSettings
  `ui/mode` → default `qml`; pure `normalizeUiMode()` unit-tested
  (`tests/unit/test_ui_mode_resolver.cpp`, 12 assertions). Wired into `main.cpp`
  (resolve + log `[ui] mode:` + expose `AppUiMode` context property). Live
  end-to-end verified: default→qml, env→{qml,webui}, bogus→qml fallback,
  config-file `[ui] mode=webui`→webui. Full suite green (846 cases / 12698
  assertions), no regression.
  - **Verification constraint flagged for Phase 1-2 (webui):** QtWebEngine needs
    a GPU/display; the headless tool sandbox renders it blank, so the embedded
    OpenDeck UI's *visual* screenshot must be checked on a real display. The
    webui path is therefore verified primarily *functionally* (bridge command
    log + return-shape assertions) in CI/headless, with visual screenshots on a
    live session. The qml path (Phase 3) is fully screenshot-verifiable
    offscreen.
- 2026-06-22: **Phase 1 bridge core DONE + verified.** `OpenDeckBridge`
  (`src/app/src/opendeck_bridge.{hpp,cpp}`) + pure shaping
  (`opendeck_shaping.cpp`, unit-testable) implement the read commands
  (get_build_info/port_base/fonts/settings/devices/categories/profiles/
  selected_profile) mapping our singletons → OpenDeck JSON. QWebChannel surface
  fixed: object `"opendeck"`, slot `invoke(requestId,command,argsJson)`, signals
  `invokeResponse`/`event`, sync `handle()` seam. Wired into `Application`
  (`openDeckBridge()` getter) + debug method `opendeck.invoke`
  (`tests/unit/test_opendeck_bridge.cpp`, 8 cases). Full suite 854 cases /
  12749 assertions, no regression. Independently re-verified live (headless,
  real AKP05E): get_devices→DeviceInfo{rows2,cols5,enc4,touch4,type7},
  get_categories→OpenDeck built-ins, get_selected_profile→keys10/sliders4.
  - Known follow-ups (deferred, documented in code): the 10 events are declared
    but not yet emitted; `get_port_base` is a 57116 stub; profile `id` presented
    as name (uuid stays the internal handle); ActionInstance.action synthesised
    minimally (not catalog-resolved); get_devices lists all connected devices.
- 2026-06-22: **Phase 1 webui HOST DONE + verified — the seam works
  end-to-end.** `resources/opendeck-shim/tauri-shim.js` (Tauri v2 shim →
  QWebChannel `opendeck`), lean vendoring `src/app/webui/opendeck/` (968 K,
  fonts excluded, NOTICE w/ upstream commit 58b59e9), `scripts/build-webui.sh`,
  CMake `AJAZZ_BUILD_WEBUI` (OFF default), `WebUiHost.qml` (WebEngineView +
  WebChannel), `opendeck_scheme_handler.{hpp,cpp}` (custom `opendeck://app/`
  origin — plain `qrc:` 404s under the SvelteKit router + Fetch), `main.cpp`
  root branch on `uiMode`. **OFF-default build green, suite 854 — no
  regression.** ON build (Node 26): SPA 524 K built + shim injected + bundled.
  **WebEngine renders OFFSCREEN with software GL (revises the earlier
  "needs a live display" note → autonomous visual verification IS feasible).**
  Live (webui mode, offscreen): the OpenDeck shell renders with our real
  backend — top bar shows "AJAZZ AKP05E (Stream Dock Plus)" (get_devices),
  Plugins/Settings, action search; bridge log confirms the SPA's shim issuing
  live invokes → shim↔QWebChannel↔bridge↔backend round-trip proven end-to-end.
  Screenshot evidence captured.
  - **Phase 2 diagnosis (canvas + action list render empty — exact shape
    mismatches found by reading the OpenDeck frontend consumers):**
    1. `get_categories` must return `{ [name]: { icon?, actions: Action[] } }`
       (value is an OBJECT with an `actions` field), NOT `{name: Action[]}` —
       `ActionList.svelte` does `.filter(([_, {actions}]) => actions.length>0)`,
       so the bare-array shape throws "reading 'length'" (the observed crash).
    1. `get_devices` must return a MAP `{ [id]: DeviceInfo }` (frontend uses
       `Object.keys(devices)` / `devices[id]`), NOT an array.
    1. `get_selected_profile.keys[]` length must be `rows*cols + touchpoints`
       (DeviceView reads touch slots at `keys[rows*cols + i]`); `sliders[]` =
       encoders. (AKP05E → keys 14, sliders 4.)
    1. shim must locally no-op Tauri `plugin:window|set_size/set_min_size`.
- 2026-06-22: **Phase 2A (read-shape fixes) DONE + verified — the OpenDeck UI
  now renders fully populated 1:1 on our backend.** Applied the 4 fixes:
  (1) `get_categories` → `{name:{actions:Action[]}}` (opendeck_shaping.cpp);
  (2) `get_devices` → MAP `{id:DeviceInfo}` + filter to editable stream
  controllers (skip mice/keyboards with no keys/encoders/zones, so the SPA's
  first-device auto-select can't land on the AJ159 mouse) (opendeck_bridge.cpp);
  (3) `get_selected_profile.keys[]` = keyCount+touchCount, touch zones appended
  after the keypad at controller "Keypad" positions (opendeck_shaping.cpp
  profileJson + new touchInstanceJson); (4) shim no-ops `plugin:window|*`
  (tauri-shim.js). Unit tests updated (+touch-zone case). **Full suite 855
  cases / 12754 assertions — no regression; OFF-default build green.** Headless
  `opendeck.invoke` confirmed: get_devices→map{akp05e}, get_categories values
  have `actions`, get_selected_profile keys=14/sliders=4. **Visual (webui mode,
  offscreen software-GL): the full OpenDeck UI renders with our AKP05E** — top
  bar "AJAZZ AKP05E (Stream Dock Plus)", canvas = 2×5 keys + 4 encoder dials +
  4 touch-strip zones (exact geometry), action list = OpenDeck built-ins +
  System Monitor (CPU/RAM) + Weather. Screenshot captured.
- 2026-06-22: **Phase 2B (write-path + events) DONE + verified.**
  `opendeck_bridge.cpp`: context parsing (`parseCtxString` right-anchored so
  dotted profile names survive; `parseCtxValue` object|string); write commands
  `create_instance` (Keypad key / touch-zone / Encoder → commit\* → returns the
  shaped ActionInstance), `remove_instance`, `update_image` (data-URL → QImage →
  `assignKeyImage`(1-based) / `assignTouchStripZone` / `assignEncoderImage`;
  null = clear), `set_selected_profile` (name→id resolve → loadProfileById),
  `rename_profile`, `delete_profile`; injected `StreamDockControlService` via
  `setStreamDockControl()` (init-order setter). Events wired in Application:
  `ProfileController::profileChanged` → emit `switch_profile` + `rerender_images`;
  `DeviceModel::modelReset` → `devices`. **Full suite 855 cases / 12754
  assertions — no regression; OFF-default build green.** Headless `opendeck.invoke`
  proven on the real AKP05E: create_instance Keypad.0 `opendeck.runcommand` →
  returns the ActionInstance + get_selected_profile shows keys[0] bound;
  update_image (1×1 PNG) → accepted, 0 "unhandled" log lines; remove_instance →
  null again. Visual (webui offscreen) confirms the canvas/action-list still
  render 1:1.
  - **Known nuance:** binding via the backend (`opendeck.invoke`) updates the
    *backend profile* but the SPA's in-memory slot model only re-syncs on the
    right event; a real in-SPA drag-drop updates both (SPA model + create_instance
    commit) so the bound key shows immediately. A `rerender_images`→re-fetch
    refinement (or reload) would make backend-side binds reflect visually too.
  - **TODOs (graceful null no-ops today, not crashes):** `move_instance`,
    `set_state`, `trigger_virtual_press`, `switch_property_inspector`,
    encoder/touch-zone `remove_instance`; events `update_state`/`key_moved`/
    `show_alert`/`show_ok`/`plugin_reloaded` (no single clear backend signal yet).
  - Phase 2 (read+write+events) is functionally complete for the core editing
    loop. Remaining write-path TODOs + Phase 3 (qml native UI) + Phase 4
    (Mirabox manifest extensions) + Phase 5 (cleanup) follow.
  - Superseded plan note: Phase 2B — write-path commands for INTERACTIVITY:
    `create_instance`/`move_instance`/`remove_instance` (bind/move/clear),
    `set_state`, **`update_image`→sidecar `set_image`** (the SPA already streams
    update_image — logged as unhandled today), `trigger_virtual_press`,
    `switch_property_inspector`, profile CRUD (set_selected_profile, rename,
    delete) — and emit the 10 `listen` events (devices/switch_profile/
    rerender_images/update_state/...). Then Phase 3 (qml native UI), Phase 4
    (Mirabox manifest extensions — plugin_manifest.cpp already parses
    Nodejs/Controllers/PUUID), Phase 5 (cleanup).
- 2026-06-22: **Phase 3 (native qml UI) — main screen DONE + verified.** The
  native `qml` (default) UI was already Stream-Deck-shaped; reshaped its header
  toward OpenDeck's main screen: the former left `DeviceList` sidebar is removed
  and replaced by an **OpenDeck-style top-bar device dropdown**
  (`AppHeader.qml` ComboBox `deviceSelectorHeader`, model
  `DeviceModel.connectedDevices()` textRole "name"/valueRole "codename",
  syncs to the active codename, emits `deviceSelected` → `Main.qml` wires it to
  `StreamDockControlService.setActiveDevice` + editor.codename/capabilities,
  mirroring the old sidebar path). The canvas is now full-width; the right
  grouped+searchable action sidebar and Keys/Settings/Firmware editor are
  retained. Verified offscreen via `screenshot`: top-bar device dropdown
  ("AJAZZ AKP05E (Stream Dock Plus)") + Plugins/Settings, full-width canvas,
  grouped Actions sidebar — matches the OpenDeck mainmenu layout. Build green;
  unit suite 855 cases / 12754 assertions (1 pre-existing flaky `input_synth`
  test that needs /dev/uinput — env-related, unaffected by this QML-only change;
  passes on rerun).
- 2026-06-22: **Phase 4 (Mirabox plugin-layer additions) — verified largely
  pre-shipped + geometry cross-check codified.** Methodical audit before touching
  code (CLAUDE.md "GSD planned ≠ unimplemented") found the two implementation
  bullets already landed in earlier phases:
  1. **`.sdPlugin` manifest extensions** — `plugin_manifest.cpp` already parses
     `Nodejs.Version` (l.271-275), `Knob`/`Information`/`SecondaryScreen`
     controllers verbatim (l.76-81; `affordanceMask` maps `Knob`→Dial), `PUUID`
     (with Elgato `UUID` fallback), and the Mirabox `FSize`/`FFamily` font
     synonyms. No change needed.
  1. **Space / StreamDock-Plugins catalog source** — `streamdock_catalog_fetcher`
     is a full paginated fetcher against `space.key123.vip` (device-UUID→AKP
     codename mapping, on-disk cache, bundled offline fallback), wired into
     `plugin_catalog_model` alongside the OpenDeck fetcher. No change needed.
  1. **Geometry/input cross-check (the genuinely-open bullet)** — cross-checked
     the app `DeviceDescriptor`s (`streamDockSidecarDescriptors()` in
     `register.cpp`) against the mirajazz sidecar's authoritative wire model
     (`streamdock-host/src/kind.rs::params_for`). 1:1 SKU correspondence (16
     each). The two `key_count`s measure different layers and reconcile cleanly:
     `encoderCount` matches exactly (4/3/0 for AKP05/AKP03/AKP153); the sidecar
     wire-slot count = app `keyCount` + `touchZoneCount` + per-family non-render
     slots (AKP05 +1 dead BAT slot → 10+4+1=15; AKP03 +3 physical side buttons →
     6+0+3=9; AKP153 +0 → 15). Codified as a regression guard:
     `tests/unit/test_streamdeck_sidecar_geometry_contract.cpp` (mirrors kind.rs;
     fails on any future drift), closing the `device.hpp` "DRIFT WARNING" gap
     across the app↔sidecar boundary. Full unit suite 856 cases / 12803
     assertions green; sidecar `cargo test` 10/10 green.
  - **Phase 4 hardware-gated follow-ups (NOT fixed blind — no live unit):**
    AKP03's 3 physical **side buttons** occupy wire slots 7-9 but the app
    descriptor models only the 6 renderable LCD keys — whether they surface as
    bindable inputs is unverified (no AKP03 hardware). AKP153 input is likewise
    unconfirmed. Both are PROVISIONAL in kind.rs; verify on a retail unit before
    modelling the side buttons (CLAUDE.md hardware-wins rule).
- 2026-06-22: **Phase 5 (cleanup) — partial: orphan retirement + revealed
  regression fixed; wholesale retirement still gated on parity.** The full
  Phase 5 ("retire superseded QML") is gated on the native UI reaching parity,
  which it has NOT (Phase 3 follow-ups below are open), so only the safe subset
  was done now:
  - **Orphan removal:** Phase 3 dropped the left device sidebar, leaving
    `DeviceList.qml` instantiated nowhere and `DeviceRow.qml` instantiated only by
    it. Both retired (git rm) + removed from `src/app/CMakeLists.txt` and the two
    `tests/qml/test_qml_smoke.cpp` registrations. The top-bar `AppHeader` dropdown
    now owns `deviceSelected`.
  - **Latent link gap fixed:** the `ajazz_qml_tests` target has been *unbuildable
    since the webui commit `6bcf8199`* — `application.cpp`/`debug_control_facade.cpp`
    reference `OpenDeckBridge` but `opendeck_bridge.cpp`/`opendeck_shaping.cpp` were
    never added to its source list. Added them (Qt-only, no WebEngine dep). The
    offscreen QML smoke gate builds + runs again on Linux.
  - **Regression surfaced + fixed (the screenshot win):** with the smoke target
    runnable again, `test_under_threshold_is_not_scrollable` went red
    (`contentH 385 > viewH 294`) and the live AKP05E screenshot confirmed it — the
    **2nd key row + dials were clipped** because the docked Inspector and the canvas
    were BOTH `fillHeight`, so the Inspector's larger `preferredHeight` starved the
    canvas below its content height. Fixed in `DeviceView.qml`: when the grid does
    NOT overflow (AKP05/03/153 all do), the canvas sizes to its natural content
    height (`deviceCanvas.implicitHeight + 2·spacingMd`) and the Inspector becomes
    the sole `fillHeight` pane; only an oversized SKU reverts to the shared-flex
    250 px + ScrollView path. Re-verified live: full AKP05E stack (10 keys + 4 strip
    zones + 4 dials) renders above a right-sized Inspector. qml suite 17 cases /
    123 assertions green; unit suite 856 green.
  - **Still gated (not done):** retiring the rest of the superseded QML + removing
    the ad-hoc layout patches waits on the Phase 3 parity follow-ups below; doing
    it now risks the same regression class an earlier single-view rewrite hit.
    `ctest` on macOS/Windows compilers also still to confirm (Linux green).
- 2026-06-22: **Phase 3 follow-up 1 DONE — profile selector to the top bar +
  device-header block removed.** Added an OpenDeck-style profile `ComboBox` to
  `AppHeader` next to the device dropdown (`profileSelectorHeader`, self-sufficient:
  queries `ProfileController.profilesForDevice/activeProfileId` directly and
  re-syncs on `profilesChanged`/`profileChanged`/active-device change, mirroring
  the device combo). `ProfileBar` gained `selectorVisible` (false in the editor) so
  its now-redundant label+combo hide while the New/Rename/Duplicate/Delete/Export/
  Import actions stay; the device-scoped activate/refresh logic is unchanged.
  `ProfileEditor` dropped the large device-photo/name/"Editing" header block (the
  device name is in the top-bar dropdown now) and the stale "on the left"/"from the
  sidebar" copy. Verified live (AKP05E, offscreen): top bar shows device + profile
  selectors side-by-side, the big header is gone, the canvas gains the reclaimed
  height. qml smoke 17/123 green; unit 856 green.
  - **Remaining Phase 3 follow-ups (not done):** the inline inspector is ALREADY
    docked under the canvas (Phase 3 + the Phase-5 canvas-clip fix); restyle the
    action sidebar to OpenDeck's exact neutral tokens (broad — the dark palette is
    `Branding`-derived app-wide, so this is a cosmetic theme alignment, lower value
    / higher blast radius); OpenDeck-shape the Multi/Toggle (ParentActionView),
    Profiles, Plugins, and Settings overlays (a larger chunk — 4 surfaces). The
    Loaded/Debug/search header items are our extras (not in OpenDeck) — keep or move
    behind an overflow later.
- 2026-06-22: **Mirabox plugin install — investigation + GitHub source increment 1
  (browse).** User goal clarified: make Mirabox plugins INSTALLABLE in-app (not a
  Mirabox UI). Live RE of the Space store proved the full 469-plugin catalogue is
  NOT installable — `download` is a relative path resolvable only by the
  proprietary desktop app's secret CDN base (0/50 absolute; the web SPA's
  `window.open(download)` returns the SPA index, not a file; the OSS object keys
  aren't derivable from the list API or the icon path; `productInfo/download` is
  auth-gated). Captured in the plugin-install memory. Chosen path (open,
  license-clean): a **Mirabox (GitHub) catalog source** backed by
  `github.com/MiraboxSpace/StreamDock-Plugins` (GPL-3.0, ~21 plugin SOURCE dirs;
  no releases/archives → needs a multi-file fetch-and-assemble install path).
  - **Increment 1 DONE (browse):** `MiraboxGithubCatalogFetcher`
    (`mirabox_github_catalog_fetcher.{hpp,cpp}`) hits the GitHub Contents API for
    `Plugins/` (one call → immediate child dirs, no recursive-tree truncation),
    derives one `CatalogEntry{source:"mirabox-github"}` per dir (dir-name
    humanised → display name; repo-relative path stashed in `streamdockProductId`
    for the future install fetch; `downloadUrl` EMPTY → browse-only for now).
    Wired into `PluginCatalogModel` (member + ctor + reload + refreshOnline +
    `replaceMiraboxGithubRows`, symmetric with the streamdock/opendeck fetchers),
    a `MiraboxGithubTab = 5` proxy filter, and a "Mirabox (GitHub)" PluginStore tab.
    **Verified:** live GitHub fetch returned **21 plugins** (+ cache written,
    log-confirmed); fetcher parse + humaniser + cached round-trip unit-tested
    (`test_mirabox_github_catalog_fetcher.cpp`, 4 cases); proxy filter locked
    (`test_plugin_catalog_proxy_model.cpp` MiraboxGithubTab case); unit 861 /
    qml smoke 123 green. **Screenshot of the rendered tab is blocked by the
    headless modal-Drawer harness gap** (can't open a modal Popup via the debug
    channel) — verification is the live fetch log + the two unit tests.
  - **Increment 2 DONE (installable) — live-verified.** `MiraboxGithubInstaller`
    (`mirabox_github_installer.{hpp,cpp}`) fetches the plugin's subtree (GitHub
    Contents → tree SHA → `git/trees/<sha>?recursive=1`, no whole-repo truncation)
    and downloads each blob from raw.githubusercontent.com into a staging dir at
    its path relative to the **bundle root** (the dir holding the shallowest
    `manifest.json` — handles both root-manifest and nested `*.sdPlugin/` layouts).
    Security: per-file path-safety (`isSafeRelPath` — no `..`/absolute/drive) +
    a post-resolve staging-escape check + file-count (512) and total-byte (64 MB)
    caps. `PluginCatalogModel::install()` branches on `source=="mirabox-github"`,
    stages into a dot-prefixed dir inside the scan dir (scanner ignores it), then
    `finalizeAssembledInstall()` resolves the install dir name from the manifest
    UUID, runs the SAME `verifyStagedPlugin` gate (only Refused quarantined,
    mirroring the network path), and atomically renames staging → `<uuid>.sdPlugin`.
    `entryInstallableInApp` now accepts mirabox-github rows. Source-only dirs
    (no manifest.json anywhere, e.g. WorldWeather's un-built Vite project) fail
    with a clear "distributed as source" message.
    - **Verified LIVE (debug channel `plugin.installFromCatalog`):** installing
      `com.mirabox.streamdock.weather` assembled **129 files** → verified
      (Unsigned→allowed) → promoted to `Weather.sdPlugin`; its action
      "Weather query" then appeared in `plugin.installedActions` (count 1) with a
      working Property Inspector + icon, no staging leftover. The source-only
      negative path (`com.mirabox.github.worldweather`) was rejected gracefully.
      Pure helpers (`findBundleRoot`, `isSafeRelPath`) unit-tested
      (`test_mirabox_github_installer.cpp`). Unit 863 / qml smoke 123 green.
- 2026-06-22: **Left device sidebar RESTORED (user feedback).** The user
  rejected the OpenDeck top-bar device dropdown ("la barra laterale di sinistra
  con i dispositivi … non va bene, ripristinala"). Recovered `DeviceList.qml` +
  `components/DeviceRow.qml` byte-identical from commit 229353de, re-added to the
  app QML module + the qml smoke registrations, restored the `DeviceList`
  sidebar in `Main.qml` (drives `setActiveDevice` + editor codename/capabilities),
  and removed the now-redundant top-bar device `ComboBox` from `AppHeader`. KEPT:
  the top-bar **profile** selector (separate follow-up, not objected to) — it now
  reflects the active device chosen in the sidebar. So the layout is: left device
  sidebar + top-bar profile selector + full-device canvas + docked inspector +
  right action sidebar. Verified live (offscreen screenshot): sidebar lists the
  connected AKP05E + AJ159 (battery chip), editor shows the AKP05E canvas; qml
  smoke 17 cases / 127 assertions green. (This partially reverts the Phase 3
  device-selector reshape; the OpenDeck-parity epic's device-selection model is
  now sidebar-based per user preference.)
- 2026-06-26: **OpenDeck vendoring → git submodule.** Replaced the lean copied-in
  `src/app/webui/opendeck/` tree (57 tracked files, fonts-stripped copy of upstream
  `58b59e9`) with a git submodule pointing at `nekename/OpenDeck`, pinned to the same
  verified commit `58b59e9`. Rationale: easy upstream updates
  (`git submodule update --remote src/app/webui/opendeck`). Safe because all our
  customizations live OUTSIDE the submodule — the Tauri shim
  (`resources/opendeck-shim/tauri-shim.js`), the C++ `OpenDeckBridge` +
  `opendeck_scheme_handler` + `opendeck_shaping`, and `scripts/build-webui.sh` — so
  the submodule stays pristine. Build wiring unchanged: same path, `build-webui.sh`
  falls back to `npm install` (upstream ships no `package-lock.json`) then
  `vite build`; upstream `.gitignore` covers `build/`/`node_modules/`/`.svelte-kit/`
  so building never dirties the submodule. The submodule gitlink now *is* the
  upstream-commit record (supersedes the prior NOTICE-with-commit).
- 2026-06-26: **webui is now the DEFAULT UI mode (direction: go webui-only).**
  `ui_mode_resolver` default flipped `qml`->`webui` (`UiMode::WebUi`; env/config still
  override so `qml` stays reachable during the transition); `test_ui_mode_resolver`
  default case updated. `AJAZZ_BUILD_WEBUI` default `OFF`->`ON`, but now degrades
  GRACEFULLY — a missing Qt WebEngine or Node/npm toolchain is a WARNING + skip (was
  FATAL_ERROR), and `main.cpp` falls back to the native qml UI when the SPA bundle
  isn't compiled (no more blank WebUiHost placeholder); a real build failure (npm
  present but `build-webui.sh` errors) stays FATAL. **Verified:** `[ui_mode]` unit
  green (12 assertions); full app builds with `-DAJAZZ_BUILD_WEBUI=ON` (OpenDeck SPA
  bundled from the submodule via vite); headless launch with no override resolves
  `[ui] mode: webui`, the SPA boots and drives `OpenDeckBridge` (catalog 325 rows),
  no crash. Deferred Phase-2B commands (`set_settings`/`set_application_profiles`)
  still no-op as documented. **Next:** close the Phase-2B webui gaps, THEN retire the
  native QML editor (Phase 5, now webui-canonical instead of qml-canonical).
- 2026-06-26: **Phase-2B webui gaps — 3 closed + verified live.** Got the headless
  verification harness working again first: `AJAZZ_DEBUG_CONTROL=1` (a bare value →
  the socket lands in `$XDG_RUNTIME_DIR`, short enough for AF_UNIX; an explicit path
  under the long scratchpad dir hits "AF_UNIX path too long"), and `opendeck.invoke`
  expects `args` as a JSON **object** (the facade does `params.value("args").toObject()`),
  not a string. With that, drove `opendeck.invoke` on the real AKP05E:
  1. **`get_settings`/`set_settings`** now persist via QSettings (`opendeck/settings`)
     over the {language,rotation,brightness} defaults (pure `settingsWithDefaults()`
     helper, unit-tested). Round-trip verified live: set {brightness:77,language:it}
     → get returns 77/it, rotation default 0.
  1. **`set_application_profiles`** accepted as a graceful no-op (no backing yet) —
     removes the per-launch unhandled-command warning (verified: 0 warnings on boot).
  1. **`move_instance`** wired to swap{Key,Encoder,TouchZone}Bindings (swap onto the
     always-empty dst = full-fidelity move); returns the dst ActionInstance + emits
     rerender_images; retain=true (copy) deferred. Verified live: bind brightness at
     Keypad.0 → move 0→3 → key0 null, key3 bound.
     Commits: `e3021878` (settings + app-profiles), `61506478` (move_instance).
  - **Still no-op (deferred — each needs a new service injected into OpenDeckBridge +
    Application wiring + nuanced verification):**
    - `trigger_virtual_press` — `StreamDockInputService::injectSyntheticEvent` exists
      but is not injected into the bridge; needs a member + a constructed `DeviceEvent`.
    - `switch_property_inspector` — in webui the PI is the SPA's own iframe ("PI for
      free"), so it's unclear the bridge must act at all; investigate before wiring
      `PropertyInspectorController::loadInspector/closeInspector`.
    - `set_state` — multi-state edit; map to `commitToggleStates`/`cycleInstanceState`
      (semantics need care).
- 2026-06-30: **DIRECTION CORRECTED → HYBRID (not webui-only); OpenDeck embedded
  as the streamdeck editor.** User: integrate OpenDeck only when a Stream Dock is
  selected; mouse + keyboard KEEP their native panels. The whole-app `webui` root
  flip (1fb6d5a5) is superseded. Implemented (commit `c456a277`):
  - `main.cpp` always loads the native shell `Main.qml`; the OpenDeck bridge +
    bundle flag + `opendeck://` scheme are now wired UNCONDITIONALLY (WebEngine-
    gated). Dropped the UiMode root branch / AppUiMode.
  - `qml/OpenDeckPane.qml` (new) — embeddable Item form of WebUiHost (WebEngineView
    - QWebChannel → OpenDeckBridge), WebEngine-gated in CMake like PIWebView.
  - `ProfileEditor.qml` — `_isStreamController` (keyCount|encoderCount|touchZoneCount
    > 0, the exact get_devices filter) hides the native tabs and mounts OpenDeckPane
    > (string-source Loader) for stream controllers; mouse (MousePanel) + keyboard
    > (RGB/Settings/Firmware) keep their native tabs.
  - **Verified live (real hardware, offscreen screenshot):** our sidebar lists BOTH
    the AKP05E and the AJ159 mouse; selecting the AKP05E renders the full OpenDeck
    SPA in the editor area; the mouse is excluded by the identical filter → native
    panel. qml+opendeck+ui_mode tests 15/15 green.
  - **Next (cleanup, task pending):** the native STREAMDECK editor QML
    (DeviceView/DeviceCanvas/KeyCell/Inspector/ActionLibraryPane/Encoder\*/TouchStrip/
    KeyBindingList) is now DEAD (unreached) but still compiled — delete it + its
    qml-smoke tests + any now-dead C++ image providers/models, and retire
    WebUiHost/ui_mode_resolver. Keep mouse/keyboard + shared components. Risky
    (dead-C++ identification) → its own focused pass.
- 2026-06-30: **Native streamdeck editor QML DELETED (commit `5eb206d0`).** Removed
  13 grep-verified self-contained QML files (DeviceView, DeviceCanvas, KeyCell,
  KeyBindingList, TouchStripLane, EncoderCard, EncoderDial, EncoderPanel,
  ActionLibraryPane, Inspector, PropertyInspector, NativePropertyInspector,
  PIWebView) + the DeviceView-only qml tests; trimmed ProfileEditor (no Keys tab /
  deviceViewComp / dead props) + CMake QML_FILES + the smoke-test lists. The QML
  module compiling with all 13 gone proves nothing surviving referenced them.
  Verified live (offscreen, real hardware): app launches clean (no QML errors),
  streamdeck → OpenDeck pane (openDeckWebView visible), AJ159 mouse still in the
  sidebar, screenshot identical to pre-deletion. Clean build + ctest 876/876 green.
  - **Still-dead follow-ups (separate pass, harmless if left):** C++ that only fed
    the deleted QML (the live-key image providers, encoder-layout renderer, the PI
    controllers/bridges now unused by any QML), and `ui_mode_resolver.{hpp,cpp}` +
    its unit test (the UiMode root branch was dropped in c456a277). Dead C++/test
    compiles fine; retire in a focused C++ cleanup.
- 2026-06-30: **Embedded OpenDeck Plugins tab wired to the catalog (commit
  `45fb5166`) + the `remove_plugin` no-op FIXED + live-verified.** The embedded
  SPA's Plugins tab was non-functional: `list_plugins` returned an empty stub and
  `install/remove_plugin` hit the unhandled-command warning. `45fb5166` wired all
  three to `PluginCatalogModel` (list groups `installedActions()` by `pluginUuid`
  into OpenDeck's `{id,name,icon}`; install handles `file://`/local sync + http(s)
  async download; remove called `uninstall(id)`).
  - **Bug caught by live debug-channel verification (constitution V):** the
    `list_plugins` `id` it hands the SPA is the disk-backed install-DIR name (the
    `pluginUuid`, e.g. `20250730000814.sdPlugin`), but `remove_plugin` fed that to
    `PluginCatalogModel::uninstall()`, which keys off the *catalogue* uuid
    (`com.streamdock.battery.…`) AND only flips row state without deleting the dir.
    Result: `findRow` missed → silent no-op; even with the right uuid the
    disk-backed `list_plugins`/`installedActions` would never reflect removal.
  - **Fix:** new `PluginCatalogModel::removeInstalledPlugin(installDirName)` —
    sanitises the SPA-supplied name against path traversal, `removeRecursively()`s
    `<pluginsDir>/<name>/`, clears bindings via `pluginUninstalled` (owner uuid =
    PUUID, else the actions' longest reverse-DNS prefix — what
    `clearBindingsForPlugin` matches), and best-effort flips the matching
    catalogue row to not-installed. `remove_plugin` now routes here.
  - **Live-verified end-to-end (offscreen + debug channel):** install Battery from
    the catalog → `list_plugins` shows it + dir on disk → bridge
    `remove_plugin {id:"20250730000814.sdPlugin"}` → `list_plugins` `[]`,
    `installedActions` 0, dir gone from disk. Unit test
    `CatalogOffline removeInstalledPlugin …` (path-traversal reject + non-existent
    reject + real delete + `pluginUninstalled` carries the manifest-prefix uuid);
    catalog tag suite 131 assertions / 17 cases green.
  - **Known minor gap (cosmetic):** `list_plugins` `icon` is empty — the disk-backed
    `installedActions()` rows do not carry a resolved plugin-level icon, so the
    SPA's PluginManager shows a blank glyph. Functional, low priority.
- 2026-06-30: **Dead-C++ cleanup: live-image providers retired (focused pass).**
  The native streamdeck QML deletion (`5eb206d0`) left the QML-editor "live render
  mirror" plumbing dead. Grep-verified (constitution VI/VIII — no deletion off a
  guess) and removed: `live_key_image_provider.hpp` + `live_encoder_image_provider.hpp`
  (LiveKey/LiveEncoder ImageStore+Provider), their `application.cpp` `addImageProvider`
  registration, and on `StreamDockControlService` the now-dead surface —
  `setLive{Key,Encoder}ImageStore`, `live{Key,Encoder}Revision`, the
  `keyImageAssigned`/`keyImageCleared`/`encoderImageAssigned` signals, the
  `m_live*Images`/`m_*Revision` members, and the store-mirroring blocks inside
  `assignKeyImage`/`assignEncoderImage`/`clearKeyImage`. No QML referenced
  `image://livekey`/`image://liveencoder` or any of these methods/signals (proven
  by grep). `assignKeyImage`/`assignEncoderImage`/`clearKeyImage` themselves STAY —
  they are the live device render path (called by `plugin_device_bridge` +
  `opendeck_bridge`); only the dead editor-mirror side-effect was excised.
  - **CORRECTION to the earlier "still-dead" note:** `encoder_layout_renderer` is
    NOT dead — it is `#include`d + used by `plugin_device_bridge.cpp` (the live
    plugin→device encoder-feedback composite) and has its own 6-case
    `[encoder-layout]` test suite. KEPT. (Exactly the constitution's "grep callers
    before deleting" rule paying off.)
  - Removed the two `[delta-b]` mirror unit tests; fixed the stale
    `tests/unit/CMakeLists.txt` comment that cited the deleted provider header.
  - **Verified:** clean 3-target build; full `ctest --preset linux-release`
    **869/869** (incl. QML smoke loads — they instantiate every surviving QML);
    live offscreen launch clean (no image-provider/QML errors), `device.renderTest`
    paints 6 keys + main + encoders (assignKeyImage/assignEncoderImage path intact),
    OpenDeck bridge still responds.
  - **STILL pending (separate decision, NOT touched):** the PI controllers/bridges
    (`PropertyInspectorController` + `PIBridge`) — these are plugin-RUNTIME (the
    PI↔plugin WS relay), not merely QML-fed, so "no QML loads them" ≠ dead. Needs a
    determination of whether the embedded OpenDeck path (PI = the SPA's own iframe)
    bypasses them before any removal. `PluginDeviceBridge` is firmly live (keep).
