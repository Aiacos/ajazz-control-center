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
