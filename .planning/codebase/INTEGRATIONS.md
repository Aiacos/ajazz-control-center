# External Integrations

**Analysis Date:** 2026-06-02

## APIs & External Services

**Plugin Catalogs:**

- **AJAZZ Streamdock Space Plugin Catalog** - Live mirror of upstream AJAZZ plugin store

  - URL: `ACC_STREAMDOCK_CATALOG_URL` env var (overridable, default internal)
  - Client: `StreamdockCatalogFetcher` (`src/app/src/streamdock_catalog_fetcher.{hpp,cpp}`)
  - Method: HTTP POST pagination (10 s timeout per page)
  - Response: JSON envelope with plugin records translated to `CatalogEntry` shape
  - Cache: `$XDG_CACHE_HOME/ajazz-control-center/streamdock-catalog.json` (atomic write)
  - Fallback: Bundled fixture `qrc:/qt/qml/AjazzControlCenter/streamdock-fallback.json`

- **OpenDeck Legacy Catalog** - Community-scraped archive of Elgato plugins

  - URL: `https://plugins.amankhanna.me/catalogue.json`
  - Client: `OpendeckCatalogFetcher` (`src/app/src/opendeck_catalog_fetcher.{hpp,cpp}`)
  - Method: Single HTTP GET (no pagination)
  - Response: Flat JSON array of plugin metadata (link, download URL, icon URL)
  - Cache: File-based on-disk mirror (same pattern as Streamdock)

**Auto-Update Service:**

- **GitHub Releases API** - In-app update checker
  - URL: `https://api.github.com/repos/Aiacos/ajazz-control-center/releases/latest` (stable)
  - Alt URL: `https://api.github.com/repos/Aiacos/ajazz-control-center/releases/tags/nightly` (rolling)
  - Client: `AppUpdateService` (`src/app/src/app_update_service.{hpp,cpp}`)
  - Auth: None (public repo, no rate-limit gate)
  - Polling: 24-hour auto-check interval (opt-in, enabled by default)
  - First check: 5 s after app launch via `QTimer::singleShot`
  - Response: JSON release envelope (tag, version, body, assets, download_url)
  - Action: Notify-only; user clicks to `Qt.openUrlExternally(releasePageUrl)` — no silent install
  - Self-disable: Flatpak (env var `FLATPAK_ID` present → Status::Disabled)

**Firmware Update Deep-Link:**

- **Vendor Firmware Pages** - Read-only, open-in-browser links
  - Stream Deck: `https://stream-dock.com/pages/support`
  - AJAZZ Keyboards: `https://ajazzstore.com/blogs/firmware`
  - AJAZZ Mice: `https://epomaker.com/blogs/software/ajazz-aj159-pro-driver-1`
  - Implementation: `FirmwareUpdateService` (`src/app/src/firmware_update_service.cpp`) — delegates to platform file associations

## Data Storage

**Databases:**

- None (no server-side database)

**Local Profile Storage:**

- **Profiles:** QSettings (Qt's platform-native storage)

  - Linux: `~/.config/AjazzControlCenter/` (XDG)
  - Windows: HKEY_CURRENT_USER registry
  - macOS: ~/Library/Preferences/io.github.Aiacos.AjazzControlCenter.plist
  - Format: INI (Linux), native (Windows/macOS)
  - Contents: Profile name, device bindings, per-key action UUIDs + settings JSON

- **Plugin Manifests:** Extracted `.sdPlugin` archives (ZIP format)

  - Location: `~/.config/ajazz-control-center/plugins/` (or `%APPDATA%\...` on Windows)
  - Client: `PluginCatalogModel` uses `QZipReader` (`src/app/src/sdplugin_extractor.cpp`)
  - Extract: In-place directory tree per plugin (manifest.json + assets)
  - Verification: Ed25519 signature validation via `manifest_signer.cpp` / `manifest_signer_win32.cpp`

- **Plugin Catalogs Cache:** JSON snapshot

  - Location: `$XDG_CACHE_HOME/ajazz-control-center/streamdock-catalog.json`
  - Format: JSON (flat array or envelope, depending on source)
  - Refresh: On-demand via `StreamdockCatalogFetcher::refresh()` or hourly auto-refresh (TBD)

**File Storage:**

- **Device Profiles** - Exported as `.ajzz` archives (ZIP bundles of profiles)

  - Format: JSON profile + embedded device images + action metadata
  - Client-side generation/import via profile controller

- **Application Cache** - Transient

  - Plugin catalog mirrors: `~/.cache/ajazz-control-center/` (Linux) / platform-equivalent
  - Downloaded plugin archives: Extracted in-place, source archive discarded

**Caching:**

- None (external HTTP caches managed by CDN/HTTP headers; app caches catalogs on disk per above)

## Authentication & Identity

**Auth Provider:**

- None (no remote authentication required)

**Internal Security:**

- **Debug Control Channel** - Unix domain socket (loopback-only, mode 0600)

  - Socket path: `$XDG_RUNTIME_DIR/ajazz-control-center-debug.sock`
  - Activation: `AJAZZ_DEBUG_CONTROL` env var (opt-in)
  - Protocol: Newline-delimited JSON-RPC
  - Wire: `{"id": N, "method": "...", "params": {...}}`
  - Trust model: Same user, file permissions (0600)

- **Plugin WebSocket Server** - Elgato Stream Deck v6 protocol

  - Binding: `127.0.0.1` (loopback-only, no network exposure)
  - Port: Auto-assigned (passed to plugins at launch)
  - Auth: HMAC-SHA256 per `passHello` handshake (optional password)
  - Challenge: Per-connection random salt nested in authentication payload
  - Brute-force mitigation: 5-attempt lockout window, connection close on failure
  - Implementation: `SdPluginServer` (`src/app/src/sd_plugin_server.{hpp,cpp}`)

- **Manifest Signing** - Ed25519 signatures on plugin manifests

  - Public key: Hardcoded in `src/plugins/include/ajazz/plugins/manifest_signer.hpp`
  - Verification: `loadTrustRoots()` → `manifest_signer_common.cpp` (nlohmann::json, PRIVATE)
  - Vendor: AJAZZ publishes signed manifests for official plugins

## Monitoring & Observability

**Error Tracking:**

- None (no remote error reporting; errors logged locally)

**Logs:**

- **Unified Log Tee** - Ring buffer + stderr + file
  - Components: `AJAZZ_LOG_*` (C++ core), Qt `qDebug`/`qCDebug` (app)
  - Sinks: `LogSink` hierarchy (`src/core/src/log_sinks.cpp`)
  - Ring Buffer: Bounded queryable history (exposed via debug channel `log.tail`)
  - File: Append-to-file optional (set at construct time)
  - Debug Channel: `log.tail` RPC exposes live tail (100-line default)

**Device Hot-Plug Monitoring:**

- **Linux:** libudev (via `udev_monitor_*` API for device arrival/removal)
- **Windows:** Raw USB device notifications (WM_DEVICECHANGE)
- **macOS:** IOKit notifications
- Implementation: `HotplugMonitor` (`src/core/src/hotplug_monitor.cpp`) emits device list updates to the registry

## CI/CD & Deployment

**Hosting:**

- GitHub (public repository)
- GitHub Releases (binary distribution)
- Flathub (universal Flatpak distribution)

**CI Pipeline:**

- **Per-PR/per-push (GitHub Actions, `.github/workflows/ci.yml`)**

  - Matrix: Linux (ubuntu-latest), Windows (windows-2022), macOS (macos-14)
  - Build: CMake + Ninja, all three presets (linux-debug/release, windows-debug/release, macos-debug/release)
  - Tests: `ctest` suite (~408 cases), verify Windows hot-plug smoke
  - Lint: pre-commit hooks (gitleaks, typos, formatting)

- **Nightly (scheduled + manual dispatch, `.github/workflows/nightly.yml`)**

  - Builds macOS Universal DMG (Apple Silicon + Intel)
  - Builds Windows MSI/ZIP (stricter than CI — both architectures)

- **Release (triggered by git tags `v*`, `.github/workflows/release.yml`)**

  - Artifacts: .deb + .rpm + .flatpak + .dmg + .msi
  - Also available via workflow_dispatch for re-test without re-tagging

**Deployment Targets:**

- Linux: Direct .deb/.rpm installation, Flatpak (Flathub), AppImage (not yet)
- Windows: MSI (Windows Installer), portable ZIP
- macOS: DMG (Universal), signed + notarized
- Auto-update: GitHub Releases API (24-hour polling, in-app banner)

## Environment Configuration

**Required env vars:**

- `AJAZZ_DEBUG_CONTROL` - Enable debug-control JSON-RPC channel (optional; off by default)
- At runtime: Python 3.11+ must be on `$PATH` (for plugin host spawn)

**Optional env vars (build-time):**

- `FLATPAK_ID` - Detected at runtime to disable auto-update (Flathub manages it)
- `ACC_STREAMDOCK_CATALOG_URL` - Override plugin catalog endpoint
- `AJAZZ_USE_SYSTEM_DEPS=ON` - Use system hidapi/nlohmann_json instead of FetchContent (Flatpak)
- `AJAZZ_BUILD_*` flags - CMake build options (App, Tests, Python Host, Property Inspector, Input Synthesis, Fuzz)

**Secrets location:**

- None embedded (no hardcoded API keys, auth tokens, or credentials)
- Debug-control socket: User-owned file in `XDG_RUNTIME_DIR` (owner-only, mode 0600)
- Plugin WebSocket: Loopback-only, optional password per `setPasswordForTesting()` (test-only setter)

## Webhooks & Callbacks

**Incoming:**

- None (app does not expose HTTP/WebSocket servers for external webhooks)

**Outgoing:**

- **HTTP(S) GETs/POSTs to plugin catalogs** - `StreamdockCatalogFetcher` / `OpendeckCatalogFetcher`
- **HTTP(S) GET to GitHub Releases API** - `AppUpdateService` polling
- **QProcess spawn of plugin processes** - Node.js plugins launched with WebSocket port and auth challenge
- **QProcess spawn of Rust sidecar** - `streamdock-host` binary, newline-delimited JSON over stdio

## Plugin Integration Points

**Plugin Discovery & Installation:**

1. **Discovery:** `PluginManager` scans plugin directories for `manifest.json` (Elgato v6 schema + AJAZZ extensions)
1. **Catalog Download:** User selects plugin from in-app store → `StreamdockCatalogFetcher` downloads `.sdPlugin` archive
1. **Extraction:** `PluginCatalogModel::installPlugin()` uses `QZipReader` to extract archive in-place (`src/app/src/sdplugin_extractor.cpp`)
1. **Verification:** `manifest_signer` validates Ed25519 signature of `manifest.json`
1. **Spawn:** `PluginManager::spawnPlugin()` launches Node.js runner with `QProcess` (WebSocket port, auth salt)
1. **Registration:** Plugin connects to `SdPluginServer` via `registerPlugin` event → `pluginRegistered` signal
1. **Action Dispatch:** App routes key press → `SdPluginServer::sendEvent(uuid, "keyPress", {...})` → plugin handler

**Action & Settings Flow:**

1. User configures key action in UI → selects plugin + action UUID
1. Settings JSON stored in profile (`QSettings` or `.ajzz` export)
1. Key press triggers: `Profile::lookup(keyIndex)` → action binding → `ActionEngine::run()`
1. For plugin actions: `BuiltinActionsService::execute()` short-circuits to `SdPluginServer::sendEvent()`
1. Plugin receives event → parses payload → invokes handler → updates UI

**Property Inspector (HTML):**

1. Plugin ships HTML PI page (Elgato v6 standard)
1. QML PropertyInspector page embeds `WebEngineView` (if WebEngineQuick available)
1. `QQmlWebChannel` bridges QML → JavaScript (`$SD` global in HTML)
1. User adjusts settings in HTML → `$SD.api.sendToPlugin()` → app receives `settingUpdated` event
1. Settings JSON persisted to profile

**Python Plugin Host (OOP):**

1. App spawns child Python process via `execvp` (out-of-process)
1. Child imports `ajazz_plugins` package from `python/ajazz_plugins/`
1. Child loads user's plugin module (e.g. `hello.py`)
1. App sends action dispatch over child's stdout/stdin as JSON
1. Child imports plugin, calls `plugin.dispatch(action_id, settings_json)`
1. Plugin handler executes (may call `ctx.notify()` for desktop notifications)
1. Child's stdout routed to parent's plugin-debug console

## Reverse Engineering & Protocol Documentation

**Authoritative References:**

- `docs/protocols/streamdeck/` - AKP05E / Stream Deck wire format (vendor `.dll` Ghidra audit, device RE)

  - `akp05_vendor.md` - SDLibrary1.dll opcodes, backend implementations
  - `akp05_init_sequence.md` - Handshake sequence (`CRT VER`, `CRT DIS`, `CRT LIG`)
  - `akp05_input_corrections.md` - Input report structure (encoder, touch)
  - `akp_device_matrix.md` - 96 SKUs per-device geometry
  - `akp_plugin_sdk.md` - Elgato Stream Deck v6 WebSocket protocol

- `docs/protocols/keyboard/` - AK-series keyboard protocols (proprietary opcodes)

- `docs/protocols/mouse/` - AJ-series mouse protocols (battery polling, color modes)

**Hardware RE Resources:**

- MEGAsync corpus (`~/MEGAsync/ajazz-reverse-engineering/`) - Ghidra projects, probe scripts, dossiers
- `mirajazz` crate (GitHub: `4ndv/mirajazz`) - Authoritative AKP05/N4 Rust implementation

______________________________________________________________________

*Integration audit: 2026-06-02*
