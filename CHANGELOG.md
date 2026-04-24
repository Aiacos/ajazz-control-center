# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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

[Unreleased]: https://github.com/Aiacos/ajazz-control-center/compare/HEAD...HEAD
