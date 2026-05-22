# External Integrations

**Analysis Date:** 2026-05-22

## APIs & External Services

**Plugin Catalogs:**

- Streamdock Space (Chinese vendor ecosystem)

  - Endpoint: `https://space.key123.vip/interface/user/productInfo/list`
  - Method: JSON POST (anonymous, no auth required)
  - Purpose: Fetch plugin listings with metadata (name, description, icon URL, download URL)
  - SDK/Client: Qt6::Network (QNetworkAccessManager)
  - Implementation: `src/app/src/streamdock_catalog_fetcher.cpp`
  - Fallback chain: Live → cached JSON on disk → bundled fixture
  - Cache file: `streamdock-catalog.json` in `QStandardPaths::CacheLocation`
  - Environment override: `ACC_STREAMDOCK_CATALOG_URL`
  - Notes: Tenant ID hardcoded to `"10000000"`, product type `"1"` for plugins

- OpenDeck (open-source Stream Deck alternative ecosystem)

  - Endpoint: `https://plugins.amankhanna.me/catalogue.json`
  - Method: Simple HTTPS GET (anonymous)
  - Purpose: Fetch cross-compatible plugin catalogue as flat JSON array
  - SDK/Client: Qt6::Network
  - Implementation: `src/app/src/opendeck_catalog_fetcher.cpp`
  - Fallback chain: Live → cached JSON → bundled fallback
  - Cache file: `opendeck-catalog.json` in QStandardPaths::CacheLocation
  - Environment override: `ACC_OPENDECK_CATALOG_URL`
  - Dead host filtering: Removes `appstore.elgato.com` (CloudFront retired as of 2026-05-13) to prevent DNS lookups and log spam

**GitHub Releases (Update Checker):**

- Endpoint (stable): `https://api.github.com/repos/Aiacos/ajazz-control-center/releases/latest`
- Endpoint (nightly): `https://api.github.com/repos/Aiacos/ajazz-control-center/releases/tags/nightly`
- Method: HTTPS GET (anonymous, Rate-limited: GitHub API allows unauthenticated requests)
- Purpose: Polling for newer releases; display in-app Material banner if update available
- SDK/Client: Qt6::Network
- Implementation: `src/app/src/app_update_service.cpp`
- Auto-check cadence: Every 24 hours (5s initial delay to avoid blocking splash screen)
- User-Agent: `ajazz-control-center (+https://github.com/Aiacos/ajazz-control-center)`
- Response parsing: Qt's QJsonDocument/QJsonObject (not nlohmann::json per COD-031)
- Storage: QSettings keys `AppUpdate/autoCheck`, `AppUpdate/includeNightly`, `AppUpdate/dismissedTag`
- Notes: Notify-only; no auto-download or in-app installation. Disabled under Flatpak (sandbox constraint).

## Data Storage

**Databases:**

- None (application is stateless regarding external databases)

**File Storage:**

- Local filesystem only
  - User profiles: `~/.config/ajazz-control-center/profiles/` (XDG spec)
  - Plugin cache: `~/.cache/ajazz-control-center/` (streamdock-catalog.json, opendeck-catalog.json)
  - App settings: Qt QSettings (platform-dependent: `~/.config/` on Linux, `~/Library/Preferences/` on macOS, Registry on Windows)

**Caching:**

- In-memory: Plugin catalog entries cached in QAbstractListModel after fetch
- On-disk: Timestamped JSON snapshots to survive app restarts without network

## Authentication & Identity

**Auth Provider:**

- None (application uses no centralized identity system)
- All external APIs accessed anonymously
- Plugin manifests signed with Ed25519 (local verification only; see **Manifest Signing** below)

## Device & Hardware Integration

**HID Devices (USB):**

- Backend: hidapi 0.14.0 (kernel-native `/dev/hidraw*` on Linux only)
- Enumeration: hotplug monitoring via platform-specific listeners
  - Linux: udev via Qt QSystemDeviceNotifier or inotify on `/dev/hidraw*`
  - macOS: IOKit device notifications
  - Windows: WM_DEVICECHANGE window messages
- Implementation: `src/core/src/hotplug_monitor.cpp`, `src/core/src/hid_transport.cpp`

**Supported Device VID Prefixes (udev rules in `resources/linux/70-ajazz.rules`):**

- `0x0300` - Stream Deck family (Mirabox HSV293S; AKP153 / AKP153E / AKP03 / AKP05)
- `0x3151` - AJAZZ VIA-compatible keyboards (SONiX vendor)
- `0x0c45` - AK980 PRO and Microdia-chipset AK keyboards
- `0x248a` - AJ-series mice (AJ139 / AJ159 / AJ179 wired + dongle)
- `0x249a` - AJ-series mice (2.4GHz dongle alternate VID)
- `0x3554` - AJ199 family mice (AJ199 / AJ199 Max / AJ199 Carbon Fiber)

**Linux udev ACL Setup:**

- Rule file: `resources/linux/70-ajazz.rules`
- Mechanism: `TAG+="uaccess"` for systemd-logind per-user ACLs (no group membership required)
- Rule numbering: **`70-`** (must sort before `73-seat-late.rules` so `uaccess` tag exists before systemd applies it)
- Installation: Post-install script or manual `sudo install -m 644 70-ajazz.rules /etc/udev/rules.d/ && sudo udevadm control --reload-rules`
- Known limitation: systemd 258+ has ACL regression on synthetic re-enumeration (physical replug required for recovery)

## Monitoring & Observability

**Error Tracking:**

- None (application logs to `stderr` via Qt's logging framework)

**Logs:**

- Qt QLoggingCategory with configurable filtering via environment `QT_LOGGING_RULES`
- Log categories:
  - `ajazz.plugins.streamdock` - Streamdock catalog fetch operations
  - `ajazz.plugins.opendeck` - OpenDeck catalog fetch operations
  - `plugin-server` - WebSocket plugin server lifecycle
  - Other component-specific categories in `src/app/src/` (device_model, time_sync_service, etc.)
- Default suppression: QCDebug messages suppressed unless explicitly enabled

## CI/CD & Deployment

**Hosting:**

- GitHub (primary repository)
- Releases published to GitHub Releases page (downloadable artifacts)

**CI Pipeline:**

- GitHub Actions workflows in `.github/workflows/`:
  - `ci.yml` - Per-PR/per-push matrix (ubuntu-latest, windows-2022, macos-14)
  - `lint.yml` - Clang-tidy static analysis, pre-commit hooks
  - `codeql.yml` - SAST scanning
  - `dependency-review.yml` - Dependency audit on PRs
  - `gitleaks.yml` - Secret scanning
  - `nightly.yml` - Scheduled + manual builds (stricter, both macOS architectures for Universal DMG)
  - `release.yml` - Tag-triggered release builds (.deb, .rpm, .flatpak, .dmg, .msi)
  - `precommit-autoupdate.yml` - Automatic hook updates
  - `wiki.yml` - Documentation auto-generation

**Build Platforms:**

- Linux (GCC/Clang) - `.deb`, `.rpm`, `.flatpak`
- Windows (MSVC) - MSI installer, portable ZIP
- macOS (Apple Clang) - Universal `.dmg` (Intel + Apple Silicon)

## Environment Configuration

**Required environment variables:**

- None (all critical paths have hardcoded defaults or graceful fallbacks)

**Optional environment variables:**

- `ACC_STREAMDOCK_CATALOG_URL` - Override Streamdock Space endpoint (dev/testing)
- `ACC_OPENDECK_CATALOG_URL` - Override OpenDeck endpoint (dev/testing)
- `QT_LOGGING_RULES` - Logging filter (e.g., `"ajazz.plugins.streamdock.debug=true"`)
- `PYTHONPATH` - Set by plugin host when spawning child process (per-spawn isolation)

**Secrets location:**

- None (application does not use API keys, tokens, or authentication credentials)

## Plugin System & Wire Formats

**Plugin Manifest Format:**

- JSON schema: `docs/schemas/plugin_manifest.schema.json`
- Superset of Elgato Stream Deck SDK v2 + OpenDeck extensions
- Required fields: UUID, Name, Version, Author, Description, Icon, CodePath, Actions, OS, SDKVersion, Software
- Platform-specific overrides: CodePathMac, CodePathWin, CodePathLin (Linux preferred over generic CodePath)
- AJAZZ extensions: Nested `Ajazz` object for AJAZZ-specific metadata

**Plugin Archives (`.sdPlugin`):**

- Format: ZIP containers
- Extraction: Qt's QZipReader (private header `<private/qzipreader_p.h>`)
- Extraction logic: `src/app/src/sdplugin_extractor.cpp`
- Manifest signing: Ed25519 signatures verified by `src/plugins/src/manifest_signer.cpp` (POSIX) / `src/plugins/src/manifest_signer_win32.cpp` (Windows)

**Elgato Stream Deck Plugin Protocol (SdPluginServer MVP):**

- Protocol: WebSocket (RFC 6455)
- Server: `src/app/src/sd_plugin_server.cpp` (loopback-only, localhost:\*)
- Server name advertised: `"Stream Dock"` (vendor compatibility)
- Binding: `QHostAddress::LocalHost` (security-critical invariant; never QHostAddress::Any)
- Conditional: Only built if Qt6::WebSockets available; graceful disable at runtime if missing
- Port allocation: Dynamic (returned to caller after `start(port)`)
- WebSocket messages: JSON line-delimited (mirrors vendor protocol)
- Implementation: Qt6::WebSockets (QWebSocketServer, QWebSocket)

## Webhooks & Callbacks

**Incoming:**

- None (application is not a server)

**Outgoing:**

- None (application does not push events to external services)

## Python Plugin Runtime

**Spawning:**

- Method: `fork()` + `execvp("python3", ...)` (POSIX) / `_spawnvp` (Windows)
- No compile-time Python dependency
- Runtime requirement: Python 3.11+
- Sandboxing (opt-in):
  - Linux: `bubblewrap` (bwrap) containerization via `LinuxBwrapSandbox`
  - macOS: `sandbox-exec` (restricted macOS Sandbox) via `MacosSandboxExecSandbox`
  - Windows: Win32 App Container (AppContainer) via `WindowsAppContainerSandbox`
  - Default: `NoOpSandbox` (no isolation)

**IPC Protocol:**

- Medium: Bidirectional pipes (stdin/stdout)
- Format: Single-line JSON objects (not streaming arrays)
- Wire spec: `src/plugins/src/wire_protocol.hpp`
- Operations: `list_plugins`, `add_search_path`, `load_all`, `dispatch`, `shutdown`, `_crash_for_test`
- Handshake: Child sends `{"event":"ready","pid":<int>,"python":"<version>"}` on startup
- Error handling: Parent reads with `poll(2)` / `WaitForMultipleObjects` timeouts (configurable, default reasonable)

**Plugin SDK:**

- Package: `ajazz_plugins` (Python 3.11+ package)
- Location: `python/ajazz_plugins/`
- Public surface: `ajazz_plugins.Plugin` base class, `@ajazz_plugins.action` decorator
- Context object: `ActionContext(device_codename, key_index, settings)`
- Testing: pytest in `python/ajazz_plugins/tests/`

## Third-Party Plugin Ecosystems

**Stream Deck SDK v2 Compatibility:**

- Manifest schema is a strict superset; existing SD plugins load with minimal shims
- WebSocket plugin protocol implemented (SdPluginServer MVP)
- Property Inspector HTML support via Qt WebEngineQuick

**OpenDeck Compatibility:**

- Manifest schema includes CodePathLin extension
- Plugin catalog aggregates OpenDeck entries
- Protocol: Same wire format as proprietary Mirabox SDv2 fork

______________________________________________________________________

*Integration audit: 2026-05-22*
