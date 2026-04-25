# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Settings page (`src/app/qml/Settings.qml`): Material-styled controls for theme mode (Auto/Light/Dark), launch-on-login, and start-minimised. Bound to existing `ThemeService` / `AutostartService` / `TrayController` properties.
- AJAZZ wordmark visible in `AppHeader.qml`: shipped at `qrc:/qt/qml/AjazzControlCenter/branding/ajazz-logo.png`, rendered with PreserveAspectFit at 32 px height. Falls back to the product-name text label when the image fails to load (e.g. branded builds without a logo asset).
- Brand-aligned application icon: `resources/branding/app.svg` redesigned (clean-room, by Aiacos) to evoke the AJAZZ visual identity — a triangular "A" monogram with an inner droplet element rendered in the brand red (`#E63946`) on a dark rounded square. Replaces the previous 3x3 macropad-grid placeholder. Tray icon (`resources/branding/tray.svg`) updated to a matching stroked silhouette so the brand stays recognisable down to 16 px on light and dark taskbars. All raster derivatives (Windows `.ico`, macOS `.icns`, Linux hicolor PNG fallbacks, tray PNG fallbacks) regenerated from the new SVGs. README and wiki landing page now show the brand mark.
- Plugin SDK foundation: `docs/schemas/plugin_manifest.schema.json` (JSON Schema Draft 2020-12) is the authoritative manifest contract — a strict superset of the Stream Deck SDK-2 manifest, with `CodePathLin` / `CodePaths` for OpenDeck compatibility and an `Ajazz` namespace for sandbox / permissions / Sigstore signing / device-class scoping. Companion architecture document at `docs/architecture/PLUGIN-SDK.md` covers deployment shape, discovery order, lifecycle (negotiation → spawn → register → steady-state → shutdown), wire protocol (line-delimited JSON over WebSocket), per-OS sandboxing (bwrap + seccomp on Linux, `sandbox-exec` on macOS, AppContainer on Windows, `flatpak-spawn --sandbox` inside Flatpak), permission model, Sigstore supply-chain story, signed-catalogue Plugin Store and the `acc plugin scaffold/run/pack/lint` developer CLI.

### CI / automation

- `pre-commit` gains a `check-jsonschema` stage that validates every committed JSON Schema against its Draft 2020-12 metaschema and every plugin `manifest.json` against `docs/schemas/plugin_manifest.schema.json`. Manifest drift is now caught at commit time, not in CI.
- New weekly workflow `precommit-autoupdate.yml`: runs `pre-commit autoupdate --freeze`, re-runs every hook so the resulting auto-fixes ride the same PR, and opens a labelled pull request via `peter-evans/create-pull-request`. Combined with the existing Lint workflow's auto-fix push, the human reviewer sees only a green PR.
- `.typos.toml` now ignores image-format plural acronyms via the regex `^[A-Z]{2,5}s$` (typos was rewriting the literal sequence P‑N‑G‑s into the wrong word) and adds Plugin SDK vocabulary (Sigstore, Fulcio, Rekor, bwrap, OpenDeck, seccomp, AppContainer, QtWebEngine, WebView2, WKWebView).
- `mdformat` exclude list now covers `docs/architecture/*.md` so wide tables in the architecture docs are not reflowed into ping-pong diffs.
- Per-platform desktop / file-explorer icons. Generated from `resources/branding/app.svg`: `resources/icons/app.ico` (multi-size 16/24/32/48/64/128/256 for Windows .exe + WiX installer), `resources/icons/app.icns` (10 entries spanning 16-512 px for macOS bundle), and `resources/icons/hicolor/app-{16,24,32,48,64,128,256,512}.png` (Linux hicolor theme PNG fallbacks). Linux desktop entry installs each PNG to `share/icons/hicolor/<size>x<size>/apps/` so GNOME / KDE / GTK launchers and AppStream metadata viewers all find a crisp raster without depending on the SVG renderer.
- Tray icon refreshed: monochrome `currentColor`-based `resources/branding/tray.svg` plus white-on-transparent PNG raster fallbacks at 16/22/24/32/44/48 px under `resources/icons/tray/`. Renders correctly on light and dark menu-bars / system trays.
- Windows `.exe` now ships the application icon via an auto-generated `.rc` file (`IDI_ICON1 ICON DISCARDABLE app.ico`) so Explorer, the taskbar, and the Start Menu shortcut display the brand icon. macOS bundle is wired through `MACOSX_BUNDLE_ICON_FILE`. Runtime `QApplication::setWindowIcon()` covers Linux/X11/Wayland and serves as a fallback elsewhere.

### Changed / Hardened

- Plugin host (`src/plugins/src/plugin_host.cpp`): rejects plugin directories whose basename collides with a Python stdlib module name (SEC-S2 hardening, uses `sys.stdlib_module_names`); deduplicates `sys.path` entries across repeated `loadAll()` calls (SEC-S3).
- Profile IO (`src/core/src/profile_io.cpp`): Linux/macOS write path switched from `std::ofstream` to `::open(O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC, 0600)` with `fsync()` on the original fd and `fsync()` of the parent directory after the atomic rename (SEC-S4 + SEC-S6 hardening). Eliminates symlink-redirect races and a buffered-stdio durability gap.
- Profile IO atomic rename on Windows: now uses `ReplaceFileW` for the existing-destination case (purpose-built for atomic-replace-with-backup, dodges most AV/indexer races) and falls back to `MoveFileExW` only when the target doesn't exist yet. Retry budget widened to also catch `ERROR_LOCK_VIOLATION` and `ERROR_USER_MAPPED_FILE`.

- Multi-action engine (`ActionEngine`) with built-in `Plugin`, `Sleep`, `KeyPress`, `RunCommand`, `OpenUrl`, `OpenFolder`, `BackToParent` action kinds (closes #25).
- Folder navigation: nested `ProfilePage`s on AKP-class devices via `OpenFolder` / `BackToParent` (closes #28).
- Encoder bindings: dedicated `EncoderBinding` with `onCw` / `onCcw` / `onPress` chains (closes #29).
- Macro recorder scaffold with portable `MacroRecorder` interface (closes #30).
- Granular DPI editor: `IMouseCapable::setDpiStage(idx, stage)` + cached `getDpiStages()` (closes #31).
- Profile import / export as `.ajazzprofile` bundles, including `--export-profile` / `--import-profile` CLI flags (closes #32).
- Battery-aware tray tooltip + low-battery notification (closes #34).
- Cross-platform `AutostartService` with launch-on-login + `--minimized` autostart flag (closes #35).
- Cross-platform `NotificationService` (Linux notify-send back-end ships) (closes #36).
- In-app changelog and privacy viewer; embedded `CHANGELOG.md` and `docs/PRIVACY.md` (closes #37).
- `docs/schemas/property_inspector.schema.json` + generic `PropertyInspector.qml` renderer (closes #39).
- Tray menu now exposes Pause/Resume, a per-profile *Switch profile* submenu and Quit, with a live update when the profile library changes (closes #24).
- HID capture fixtures live under `tests/integration/fixtures/<device>/`; new header-only `hex_loader.hpp` parses them and `test_capture_replay.cpp` covers happy-path + malformed (truncated, oversize index, no-key marker) frames (closes #40).
- ASan/UBSan + TSan CI matrix on Linux (`Sanitizers · …` jobs in `ci.yml`); concurrent publish/subscribe and re-entrant subscribe/unsubscribe tests for `EventBus` (closes #41).

- Initial project scaffolding: CMake presets, Qt 6 / QML skeleton UI, C++20 core interfaces.
- Modular device framework with abstract `IDevice`, `ITransport`, `IProtocol` interfaces.
- Python plugin host built on pybind11 with `ajazz` runtime module.
- USB/HID transport backend based on `hidapi`.
- GitHub Actions CI matrix for Linux (Ubuntu 24.04), Windows (2022) and macOS (14, arm64).
- Release workflow producing `.deb`, `.rpm`, `.msi` and `.dmg` artifacts.
- Full documentation set: architecture, reverse-engineering methodology, device onboarding guide, plugin development guide, build guide.
- udev rules template for Linux device permissions.
- `.editorconfig`, `clang-format`, `clang-tidy`, `ruff` and `black` configurations.
- Contributor workflow: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, issue and PR templates.

### Changed

- Architecture documentation refreshed to match the shipped code: clarified the snapshot-based `EventBus` synchronisation, documented the controller-layer exception bridge to QML toasts, removed the unsigned-plugin warning chip claim (deferred to #6) and added a section on the `Theme.qml` singleton (closes #44).

[Unreleased]: https://github.com/Aiacos/ajazz-control-center/compare/HEAD...HEAD
