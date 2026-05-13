# Project: AJAZZ Control Center

## Core Value

A modern, open, cross-platform control center for AJAZZ devices (stream decks, keyboards, mice) with a Qt 6 / QML UI and a Python out-of-process plugin system for scripting, automation, and third-party integrations.

## Scope

- **In scope:** Device backends (HID, USB), Qt 6.7+ / QML 6 UI, Python 3.11+ plugin SDK, IPC + sandboxing for plugins, packaging across Linux (.deb/.rpm/.flatpak/.snap), Windows (.msi/.zip), macOS (.dmg), CI/release automation.
- **Status:** Early alpha. Scaffolding, architecture and CI in place; device backends under active development. 13 devices catalogued: 6 functional, 7 scaffolded.

## Stack

- C++20, Qt 6.7+ (Widgets, QML, WebEngine, WebChannel)
- Python 3.11+ for plugin host child processes
- CMake build, pre-commit, GitHub Actions CI/lint/nightly
- Cross-platform: Linux primary, Windows + macOS supported

## Planning Bootstrap (2026-05-12)

`.planning/` was retrofitted onto this brownfield repo to enable structured `/gsd-code-review` of work that had already shipped to `main` without phase tracking. Two retro-phases wrap the most recent thematic clusters of commits (SEC-003 plugin host integration; QML_SINGLETON dual-instance sweep). PROJECT.md, ROADMAP.md, and STATE.md were created as stubs sufficient for the SDK to validate phase lookups; this is **not** a full GSD discovery output.

## Milestone History

- **v1.0** (shipped 2026-05-13) — Retro-fit catalogue. 2 phases, 7/7 success criteria, audit `tech_debt` (CR-01 + WR-01 deferred). Archived in `.planning/milestones/`.

## Key Constraints

- No system-level mutations from tooling (no writes to `~/.config/niri/`, `~/.config/noctalia/`, `/usr/share/`, etc.) — see user-memory `feedback_no_system_mutations.md`.
- Direct-to-`main` workflow with frequent fetch+rebase; expect 3-5 remote commits per session — see user-memory `feedback_parallel_workflow.md`.
- Schema doc is the source of truth for JSON wire keys (`Profile::deviceCodename` ⇄ `"device"`).
- Qt 6 QML gotchas catalogued in user-memory `reference_qt_qml_gotchas.md` — `QML_SINGLETON` dual-instance pattern is the most recent endemic bug class.

## Key Decisions

| Date       | Decision                                                                                                                             | Rationale                                                                                                                                                       |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2026-05-02 | Out-of-process Python plugin host (SEC-003)                                                                                          | Sandbox third-party plugin code from main app process; signed manifests gate plugin loading.                                                                    |
| 2026-05-04 | `QML_SINGLETON` services must register a single shared instance via `qmlRegisterSingletonInstance` (not `QML_SINGLETON` macro alone) | Macro path silently created a second instance per QML import; light theme bug (`d7f932f`) surfaced this. Preventive sweep across 5 other services in `e221b21`. |
| 2026-05-12 | Retro-fit GSD planning onto brownfield repo                                                                                          | Enable structured code-review of recent thematic clusters without restructuring git history.                                                                    |
