# Project memory — AJAZZ Control Center

> Load-bearing project conventions and gotchas for new contributors (human
> or AI). Update via PR; **don't** check in personal notes here — those
> belong in your local Claude Code auto-memory.

## What this project is

Modern, open, cross-platform control center for AJAZZ devices (Stream
Decks, keyboards, mice) with Qt 6 / QML UI and a Python out-of-process
plugin system. C++20, Qt 6.7+, CMake, Ninja. Linux primary; Windows and
macOS supported.

For full context: `.planning/PROJECT.md`. For current state: `.planning/STATE.md`.

## Workflow conventions

- **Direct-to-`main` workflow.** No long-lived feature branches. Fetch + rebase
  before every push; expect 3–5 remote commits per session. Never force-push
  to `main`.
- **Atomic commits.** Each commit is one independently-revertable change.
  Don't bundle unrelated fixes.
- **Conventional Commits.** `feat:`, `fix:`, `chore:`, `docs:`, `test:`,
  `refactor:`, with optional scope: `fix(plugins): ...`. Enforced by pre-commit.
- **Never skip pre-commit hooks.** `--no-verify` is only acceptable when the
  hook itself is broken (e.g. stash/restore failure under concurrent agents)
  AND the hook content is verified to pass independently. Document the bypass
  in the commit message body.

## Hard rules

- **No system-level mutations from project tooling.** Code-only fixes inside
  the project repo. Don't write to `/etc/`, `~/.config/niri/`,
  `~/.config/noctalia/`, `/usr/share/`, etc. Even read-only inspection of
  user dotfiles should be sparing and explicitly justified.
- **No `nlohmann::json` in `ajazz_core` or any installed public header.**
  PRIVATE-linked to `ajazz_plugins` only. Verified by grep at audit time
  (`grep -rn nlohmann src/core/include/` must return 0). This is the
  **COD-031 boundary** — crossing it is a release-blocker.
- **Schema doc is the source of truth for JSON wire keys.** When the
  C++ field name and the JSON key differ (e.g. `Profile::deviceCodename` ⇄
  `"device"`), the schema documentation wins. Never align a writer to a C++
  field name without first checking the schema.
- **Cap concurrent execute agents at 2** in autonomous runs. Three or more
  concurrent agents have caused atomic-commit splits and forced `--no-verify`
  workarounds (v1.1 retrospective lesson).

## Qt 6 / QML gotchas (verified, recurring)

- **`QML_SINGLETON` requires `qmlRegisterSingletonInstance`**, NOT the bare
  `QML_SINGLETON` macro. The macro path silently creates a second instance
  per QML import (v1.0 light-theme bug). Co-locate
  `static_assert(!std::is_default_constructible_v<T>)` with the
  `QML_SINGLETON` declaration to make this a build break, not a comment.
- **`WebEngineView` has no `page` Q_PROPERTY.** Use `Qt6::WebChannelQuick`
  (not `WebChannel`). `qt_add_qml_module IMPORTS` is the right wire.
- **`MultiEffect.maskSource` needs a proper Item type.** A raw `Rectangle`
  causes silent SIGABRT on real GPU. Wrap it.
- **Material attached properties don't cross Popup scope.** Set them
  inside the popup contentItem, not on the popup root.

## Cross-platform build strictness

Each platform's compiler catches things the others don't. Land all three.

- **Linux GCC + Linux Clang**: most permissive, smallest set of catches.
- **Apple Clang on macOS (`-Werror`)**: catches `-Wunused-const-variable`
  on `inline constexpr` at file scope. GCC/Linux-Clang don't.
- **MSVC on windows-2022 (`/W4 /WX`)**: deprecation warnings (C4996) are
  hard errors. Prefer the `_s` variants (`_wdupenv_s` not `_wgetenv`,
  `sprintf_s` not `sprintf`).
- **Test names must be ASCII-only.** ctest passes filter args through the
  Win32 CMD codepage; em-dash `—` and right-arrow `→` get mangled to `?`
  and Catch2's filter no longer matches. Use `-` and `->`.
- **Moved-from `std::wstring` size is implementation-defined.** libstdc++
  and libc++ tend to leave SSO contents intact; MSVC clears heap-allocated
  strings on move. Save `entry.size() + 1` BEFORE any `std::move(entry)`
  call when using the value to advance a cursor.

## CMake / CTest

- **Working preset**: `ctest --preset linux-release`. The suite contains
  roughly 286 `TEST_CASE` invocations across `tests/unit/` and
  `tests/integration/` as of 2026-05-18 (was 178 at v1.1 close; grew
  through Phase 9 captures, vendor-RE work, AK980 clock-sync, OOP plugin
  host, SdPluginServer MVP, and the bulk audit follow-up). Run the
  preset and trust the live count; do not hand-edit this figure on
  every push.
- **ctest filter flag is `--tests-regex` / `-R`, NOT `--test-regex`.**
  The latter is a typo that produces "Unknown argument" from CMake.
- **Win32EnvBlock sort order**: env blocks passed to `CreateProcessW` must
  be sorted **by name** (key only), not by full `KEY=VALUE` entry. Use
  `std::wstring_view` to extract the key cheaply for the comparator.

## Linux device access

- udev rules at `resources/linux/99-ajazz.rules` cover VID prefixes
  `0300` (Stream Dock family), `3151` (SONiX-VID AJAZZ keyboards/mice),
  `0c45` (Microdia-VID AK980 PRO), `248a` + `249a` + `3554` (AJ-series mice).
- **Backend**: `hidapi_hidraw` only — kernel-native `/dev/hidraw*`. The
  libusb hidapi backend is NOT used and is explicitly disabled in the
  Flatpak manifest. Don't add libusb-only code paths without a deliberate
  cross-team decision.
- If `udev TAGS=:uaccess:` is present but ACLs are stale (devices plugged
  in before rules installed), replug or run `udevadm trigger --action=change`.

## CI architecture

- **CI** (`.github/workflows/ci.yml`): per-PR/per-push matrix on ubuntu /
  windows-2022 / macos-14. Includes a "Verify Windows hot-plug smoke ran"
  gate that fails if zero hot-plug tests match the regex.
- **Nightly** (`.github/workflows/nightly.yml`): scheduled + manual.
  Builds macOS Universal DMG + Windows MSI/ZIP — STRICTER than CI because
  it builds both architectures of the Universal DMG.
- **Release** (`.github/workflows/release.yml`): triggered by `push: tags: ['v*']` or `workflow_dispatch`. Produces `.deb` + `.rpm` + `.flatpak` +
  `.dmg` + `.msi`. The `workflow_dispatch` trigger lets you re-test
  without retagging.

## Useful references

- `docs/superpowers/plans/` — pre-GSD ad-hoc plans, occasionally inherited
  into formal phases (e.g. v1.1 Phase 5 time-sync adopted
  `2026-05-13-time-sync.md`).
- `.planning/milestones/v1.0-*` and `v1.1-*` — historical milestone
  artifacts, sealed but referenceable.
- `.planning/RETROSPECTIVE.md` — living retrospective with patterns and
  cross-milestone trends. Read before non-trivial new work.

______________________________________________________________________

*This file is project memory. Update via PR. Personal session notes
belong in your Claude Code auto-memory, not here.*
