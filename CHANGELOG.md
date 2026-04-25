# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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
