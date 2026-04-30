# AGENTS.md

Compact instructions for automated agents working in this repo. Read
[`GUIDELINES.md`](GUIDELINES.md) for the full rulebook — this file only
captures what is easy to miss.

## Quality bar (non-negotiable)

- **Zero warnings, every build, every linter.** `-Werror` is on
  (`AJAZZ_ENABLE_WERROR=ON` in every preset). Any warning from
  `clang-format`, `clang-tidy`, `qmllint-qt6`, `ruff`, `mdformat`,
  `cmake-lint`, `typos`, `actionlint` is a build regression — fix, never
  suppress. `make test` running green **with no warnings on the way** is
  the signal that work is ready to commit.
- **Docstring everything new.** Public C++ symbols get Doxygen blocks
  (`@brief`/`@param`/`@return`/`@throws`); Python gets Google-style
  (`Args:`/`Returns:`/`Raises:`); QML files get a file-level `//` header
  and `///` on exported `property …`.
- **Keep the docs/wiki in sync with the code.** The README support matrix
  and wiki tables are AUTOGEN blocks rendered by `scripts/generate-docs.py`
  from `docs/_data/devices.yaml`. Every device / architecture / UX change
  that affects users must land together with the wiki page update under
  `docs/wiki/` — that directory is synced to the GitHub Wiki by
  `.github/workflows/wiki.yml` on every push to `main`. CI runs
  `make docs-check` and fails PRs whose AUTOGEN blocks drifted.

## Build & test

```bash
make build    # cmake --preset dev + ninja
make test     # ctest --preset dev
make lint-all # run every pre-commit hook across the tree
```

Build system is CMake + Ninja via presets (`dev` = Debug + ASan/UBSan,
`release` = RelWithDebInfo, `coverage` = gcov). **Never** invoke `cmake`
directly without a preset — `AJAZZ_ENABLE_WERROR=ON` is set in the
`base` preset and must not be bypassed.

Single-test focus: `ctest --preset dev -R <regex>` or
`./build/dev/tests/unit/ajazz_unit_tests "[tag or case name]"` (Catch2).

Running the GUI: `make run` auto-detects the Wayland socket when
`WAYLAND_DISPLAY`/`DISPLAY` are empty — **do not** hand-invoke the
binary from a bare shell without this or Qt falls back to xcb and aborts
on `xcb-cursor0`.

## Commits, hooks, pushes

- Conventional Commits **strict** — enforced by the `commit-msg` hook.
  Allowed types: `feat fix docs style refactor perf test build ci chore revert`.
  Scope matches the top-level module: `core`, `app`, `devices`, `plugins`,
  `streamdeck`, `keyboard`, `mouse`, `ui`, `ci`, `docs`.
- **Always run `make precommit` once per clone** to install the pre-commit
  - commit-msg + **pre-push** hooks. `clang-tidy` runs at push time (~30 s)
    and catches what per-commit hooks can't.
- When a hook auto-fixes files (`mdformat`, `ruff`, `clang-format`, `typos`,
  `regenerate-docs`), stage the changes and run `pre-commit run --files <list>`
  again until clean. Two passes is the maximum for a valid state; more
  means a real issue.
- **Never** use `--no-verify`. Never force-push to `main`.
- Before `git push`: `git fetch origin && git log HEAD..origin/main` to
  check for remote commits (this repo sees frequent parallel pushes from
  other sessions). If non-empty, `git pull --rebase origin main`.

## Testing gotchas

- Tests must **not** touch the developer's real `QSettings`. The
  `tests/unit/qt_app_fixture.hpp` helper suffixes the app org name with
  the PID for per-process isolation — reuse it instead of building a raw
  `QCoreApplication`.
- Sandbox tests (`test_{linux_bwrap,macos_sandbox_exec,windows_app_container}_sandbox.cpp`)
  are **platform-gated via `#ifdef`**, not CMake. The shared host test
  (`test_out_of_process_plugin_host.cpp`) compiles on all three OSes.
- Windows-only CI validation of the OOP plugin host runs on `windows-2022`;
  locally only the POSIX backends are exercised.

## Structural facts the filenames hide

- **`src/core/`** is intentionally Qt-free and pybind11-free. Do not
  `#include <QObject>` from core code — Qt belongs in `src/app/`.
- **`src/plugins/`** is the out-of-process plugin host. Since A4 slice 3e
  the legacy in-process pybind11 path is **gone**; the child Python
  process is `execvp`'d at runtime, there is no compile-time Python
  dep. Platform-specific sandbox source files (`linux_bwrap_sandbox.cpp`,
  `macos_sandbox_exec_sandbox.cpp`, `windows_app_container_sandbox.cpp`)
  are selected by `CMakeLists.txt` via `if(WIN32)` / `else()`; the
  shared `sandbox.cpp` (pimpl special members) is compiled everywhere.
- **QML module name** is `AjazzControlCenter` (one word). Every singleton
  is registered via `QML_NAMED_ELEMENT(...) + QML_SINGLETON` in the C++
  header and reached from QML as a **type name**, e.g.
  `Theme.accent`, `Branding.productName`, `Autostart.launchOnLogin`.
  No `contextProperty` wiring (the migration is done — `97eb719` +
  `7df853c`). `Theme.qml` is a thin facade over `Branding`; **every QML
  file must read colours from `Theme.*`, not from hard-coded hex literals**.
- **`docs/superpowers/{specs,plans}/`** hosts session-planning docs.
  Before starting a multi-task piece of work check if a `YYYY-MM-DD-*`
  plan already exists — another agent may have queued tasks for you.

## Reverse-engineering / vendor software

Clean-room policy is **enforced**. See
`docs/research/README.md` and `docs/research/vendor-protocol-notes.md`:
the engineer reading vendor binaries/installers writes specs, a *different*
engineer implements from the spec. No vendor binary, decompiled source,
or copyrighted asset lands in version control — only notes, metadata
and hashes.

## TODO discipline

`TODO.md` is the living backlog. Keep source-level `TODO(...)` markers
cross-referenced with a `TODO.md` entry; the "Cleanup backlog" section
tracks known low-risk polish. If you add an inline `TODO` without a
TODO.md counterpart, a future audit will flag it.

## Files worth reading before changing behaviour

- [`GUIDELINES.md`](GUIDELINES.md) — canonical engineering rules.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) §4 — the auto-fix loop in detail.
- [`TODO.md`](TODO.md) — open work + "Cleanup backlog" + "Iceboxed".
- [`CHANGELOG.md`](CHANGELOG.md) "Unreleased" — what ships next.
- [`docs/architecture/`](docs/architecture/) — per-subsystem design docs
  (PLUGIN-SDK, BRANDING, ADRs).
