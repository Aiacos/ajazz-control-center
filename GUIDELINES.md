<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: Aiacos and AJAZZ Control Center contributors
-->

# Project Guidelines

This document captures the **non-negotiable conventions** of the AJAZZ Control
Center project. It is the canonical reference for both maintainers and external
contributors and complements [CONTRIBUTING.md](CONTRIBUTING.md) (which focuses
on the mechanical contribution flow), [SECURITY.md](SECURITY.md) (responsible
disclosure) and the architectural docs under [`docs/architecture/`](docs/architecture/).

If a rule below conflicts with a tool default, the rule below wins — adjust the
tool, not the project.

> 🇮🇹 **Riepilogo in italiano** (linee guida originali). Le sezioni successive
> sono la versione completa in inglese, che è la lingua ufficiale del progetto.
>
> 1. **La compilazione e l'installazione devono essere semplicissime.** Un
>    nuovo utente non deve mai dover leggere più di una pagina di README per
>    avere un binario funzionante.
> 1. **Niente status `planned`/`research` lasciati a metà.** Se uno scaffold
>    entra in repo, va completato: o si implementa, o si rimuove.
> 1. **Linting automation sempre attivo** (pre-commit + CI). Le PR rosse non si
>    mergiano.
> 1. **README e wiki sempre allineati al codice.** I blocchi `AUTOGEN` sono
>    rigenerati da `scripts/generate-docs.py` prima di ogni commit.
> 1. **Tutto il codice è commentato in stile Google** (Doxygen `@brief`,
>    `@param`, `@return`, …) sia C++ che Python.
> 1. **Repo pubblico, licenza GPL-3.0-or-later, documentazione in inglese,
>    proprietà Aiacos, clean-room policy** (vedi sotto).
> 1. **Conventional Commits obbligatorio.** I messaggi di commit sono parte
>    della history pubblica.
> 1. **Branding configurabile, avvio ridotto a icona di default,
>    riconoscimento automation delle periferiche.**

______________________________________________________________________

## Table of contents

1. [Project values](#project-values)
1. [Licensing & ownership](#licensing--ownership)
1. [Clean-room reverse engineering policy](#clean-room-reverse-engineering-policy)
1. [Repository layout](#repository-layout)
1. [Build & install — the "two-command rule"](#build--install--the-two-command-rule)
1. [Coding standards](#coding-standards)
   - [C++20](#c20)
   - [Python 3.11+](#python-311)
   - [QML / Qt 6.7+](#qml--qt-67)
   - [Documentation comments (Google style)](#documentation-comments-google-style)
1. [Linting, formatting & pre-commit](#linting-formatting--pre-commit)
1. [Testing](#testing)
1. [Conventional commits & branching](#conventional-commits--branching)
1. [Documentation discipline](#documentation-discipline)
1. [Branding](#branding)
1. [Default UX expectations](#default-ux-expectations)
1. [Security & supply chain](#security--supply-chain)
1. [CI/CD & release engineering](#cicd--release-engineering)
1. [Adding a new device](#adding-a-new-device)
1. [Issue triage & priority labels](#issue-triage--priority-labels)
1. [Glossary](#glossary)

______________________________________________________________________

## Project values

We optimise for, in priority order:

1. **User trust.** No telemetry by default. No silent network calls. No
   unsigned plugins running with full host privileges. Privacy and security
   are features, not afterthoughts.
1. **Buildability.** Anyone, on any of the three target OSes, must be able to
   go from `git clone` to a running binary in two commands. If you change
   anything that breaks this property, fix the docs and the CI in the same PR.
1. **Honesty about state.** A device that does not work is documented as not
   working. A feature that is a stub is labelled `scaffolded` in the README
   stats table. We never ship a fake green badge.
1. **Long-term maintainability.** Features that cannot be tested or
   maintained without proprietary tooling do not belong in `main`. They go
   behind a CMake option, a runtime flag, or — preferably — an external plugin.
1. **Cross-platform parity.** Linux, Windows and macOS are first-class. The CI
   matrix proves it on every push.

## Licensing & ownership

- **License:** [GPL-3.0-or-later](LICENSE). All source files start with an
  SPDX header:

  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  // SPDX-FileCopyrightText: Aiacos and AJAZZ Control Center contributors
  ```

- **Copyright holder:** "Aiacos and AJAZZ Control Center contributors".
  Individual authors retain copyright on their contributions; by submitting a
  PR you agree to license it under GPL-3.0-or-later.

- **Repo ownership:** the canonical repo is
  [`Aiacos/ajazz-control-center`](https://github.com/Aiacos/ajazz-control-center)
  and it is **public**. Forks are encouraged; do not push private mirrors that
  diverge silently.

- **Third-party code:** any vendored snippet must keep its original SPDX
  identifier and license file. New dependencies must be GPL-compatible
  (preferably MIT, BSD-2/3, Apache-2.0, MPL-2.0, LGPL-2.1+, GPL-2.0+).

## Clean-room reverse engineering policy

AJAZZ Control Center talks to AJAZZ-branded hardware. We support that hardware
without using a single byte of AJAZZ's proprietary software, drivers, or
documentation. This is a **strict** rule, both legally and culturally.

- **Allowed:** USB packet captures from your own device using Wireshark/usbmon,
  black-box experimentation, public protocol notes from third parties (with
  attribution), our own kernel-level tracing.
- **Forbidden:** copying code or constants from decompiled vendor binaries,
  pasting strings or magic numbers from a vendor's installer, reading vendor
  source if it ever leaks. If you have ever seen vendor source code, please
  contribute to a different subsystem.
- **Captures:** raw `.pcap`/`.pcapng` files belong in
  [`docs/protocols/captures/`](docs/protocols/captures/) only after they have
  been **scrubbed of identifying serial numbers**. Document the device, the
  firmware version, the host OS, and the action that produced the capture.
- **Documentation:** every reverse-engineered protocol gets a public Markdown
  document under `docs/protocols/<device>.md` with the frame layout, magic
  numbers, and at least one annotated capture. Code without a published spec
  is reviewed harder.

## Repository layout

```text
src/
  core/          # Pure C++20: HID transport, device registry, profile model, event bus.
  app/           # Qt 6 / QML application + controllers.
  devices/       # One subdir per family: streamdeck/, keyboard/, mouse/.
  plugins/       # Plugin host (Python via pybind11).
docs/
  architecture/  # ADRs and high-level architecture diagrams.
  protocols/     # Per-device protocol specs + captures.
  wiki/          # Wiki source (deployed by .github/workflows/wiki.yml).
  analysis/      # Periodic audits (security, code, UI, feature gaps).
packaging/       # Linux .deb/.rpm/.flatpak, Windows .msi, macOS .dmg.
python/          # Python helpers + plugin SDK (`ajazz_plugin_sdk`).
resources/       # Branding assets, sample profiles, fixtures.
scripts/         # Maintenance scripts (generate-docs.py, …).
tests/           # GoogleTest unit + integration tests.
```

**Rule:** new top-level directories require a README and an entry here.

## Build & install — the "two-command rule"

Any supported OS, from a fresh clone:

```bash
make bootstrap   # installs system deps via the host package manager
make             # configures, builds, runs tests, packages
```

That is the contract. If you change a build-system flag, presets, or a default,
update [`Makefile`](Makefile), the README quickstart and (where relevant) the
[`CMakePresets.json`](CMakePresets.json) aliases (`dev`, `release`, `coverage`)
in the **same PR**. CI will fail PRs that break either path.

End-users on Linux/Windows/macOS should never need CMake or Qt knowledge to run
the app — they can grab the latest installer from the
[nightly release](https://github.com/Aiacos/ajazz-control-center/releases/tag/nightly).

## Coding standards

### C++20

- **Standard:** C++20. We use concepts, ranges, `std::span`, `std::from_chars`.

- **Style:** enforced by `clang-format` (`.clang-format` at repo root, based on
  Google with 4-space indent and a 100-column limit).

- **Static analysis:** `clang-tidy` runs in CI on Linux with Qt 6.7. Common
  suppressions (`bugprone-easily-swappable-parameters`, …) are pinned in
  `.clang-tidy`; do not silence checks inline without a one-line `// NOLINT`
  rationale.

- **Memory & lifetimes:** no raw `new`/`delete`. RAII everywhere. HID handles,
  udev contexts, IOKit notifications all wrapped in dedicated guard types
  (`UdevDeviceGuard`, etc.). Smart pointers spell ownership: `unique_ptr` for
  owning, `shared_ptr` only when shared ownership is genuine, raw `T*` only
  for non-owning observers.

- **Threading:** see [Mutex hygiene](#mutex-hygiene) below. Never run user code
  (callbacks, signals, plugin entry points) under a held lock.

- **Errors:** `std::expected`-style return types where the caller can recover;
  `std::system_error`/`std::runtime_error` only at boundary layers. No silent
  swallowing of errors.

- **Logging:** use `AJAZZ_LOG_*` macros with a module name as the **first**
  argument, e.g.

  ```cpp
  AJAZZ_LOG_WARN("hotplug", "lost device {}", path);
  ```

- **Public headers** live under `src/<lib>/include/ajazz/<lib>/` and are
  installed. Implementation details belong in `.cpp` files or `_impl.hpp`
  headers excluded from install.

#### Mutex hygiene

- Take locks for **state** only. Copy out what callbacks need, release the
  lock, then call.
- Use `mutable std::mutex` for read-mostly accessors (`snapshotCallback()`).
- Forward-declare the `Impl` class **publicly** in the public header when
  multiple translation units need to see it (we do this for `HotplugMonitor`),
  so `friend` declarations are not needed and tests can pass an `Impl&`.

### Python 3.11+

- **Style:** `ruff` with the project's `pyproject.toml` config (formatter +
  linter). Line length 100.
- **Typing:** mandatory. Public APIs use PEP 604 unions (`int | None`),
  `from __future__ import annotations` at the top of every module.
- **Plugin SDK:** the `ajazz_plugin_sdk` package is the **only** stable surface
  exposed to plugins. Anything else is private and may change between minor
  releases without notice.
- **No print statements.** Use the `logging` module with the `ajazz.<module>`
  hierarchy.

### QML / Qt 6.7+

- **Module:** `import AjazzControlCenter`. Singletons (`Theme`,
  `BrandingService`) are exposed as QML singletons via
  `QT_QML_SINGLETON_TYPE` in CMake.
- **No hard-coded colour literals.** Every colour comes from `Theme.*` (e.g.
  `Theme.bgBase`, `Theme.fgMuted`, `Theme.accent`). The canonical fallback
  background is `#14141a`.
- **No `parent.parent`-style traversal.** Use model roles, attached properties,
  or `Loader` with explicit context properties. If you reach for
  `parent.parent`, stop and refactor.
- **No magic device-grid sizes.** Layouts read from device capabilities
  (`device.cells`, `device.encoders`, `device.dpiStages`) — never `model: 15`
  or `count: 4`.
- **Accessibility:** every interactive item sets `Accessible.role`,
  `Accessible.name`, and a meaningful `Accessible.description`. Targets
  ≥ 44×44 px. Focus order matches the visible layout.
- **Internationalisation:** wrap every user-visible string in `qsTr(...)`. The
  i18n CI step fails the build if a literal is detected outside `qsTr`.
- **Responsive layouts:** `Main.qml` chooses sidebar width based on viewport;
  panels stack vertically below 720 px.

### Documentation comments (Google style)

All public functions, classes, structs, files, and non-trivial private helpers
are documented in the **Google C++ comment style**, parsed by Doxygen.

```cpp
/**
 * @file profile.cpp
 * @brief Atomic profile JSON loader with schema validation.
 */

/**
 * @brief Load and validate a profile from disk.
 *
 * Reads @p path, validates against the profile JSON schema and returns the
 * parsed profile. The on-disk file is not modified; writers must use
 * writeProfile() which performs an atomic rename.
 *
 * @param path Absolute path to the profile JSON file.
 * @return The parsed profile.
 * @throws ajazz::core::ProfileLoadError if the file cannot be read,
 *         is not valid JSON, or fails schema validation.
 *
 * @threadsafe Re-entrant; safe to call concurrently from multiple threads.
 */
Profile loadProfile(std::filesystem::path const& path);
```

For Python, use Google-style docstrings (`Args:`, `Returns:`, `Raises:`):

```python
def load_profile(path: pathlib.Path) -> Profile:
    """Load and validate a profile.

    Args:
        path: Path to the profile JSON file.

    Returns:
        Parsed Profile object.

    Raises:
        ProfileLoadError: If the file is missing, invalid JSON, or fails
            schema validation.
    """
```

Doxygen runs in CI (see `Doxyfile`) and warnings are errors on `main`.

## Linting, formatting & pre-commit

`pre-commit` is the gate for every PR. Versions are **pinned** in
`.pre-commit-config.yaml`:

| Hook           | Version   | Purpose                                                                  |
| -------------- | --------- | ------------------------------------------------------------------------ |
| `clang-format` | `v18.1.8` | C++ formatter                                                            |
| `ruff`         | `v0.8.4`  | Python lint + format                                                     |
| `cmake-format` | `v0.6.13` | CMake formatting                                                         |
| `yamllint`     | `v1.35.1` | YAML lint (workflows, configs)                                           |
| `mdformat`     | `0.7.21`  | Markdown formatting (+ `gfm`, `tables`, `frontmatter`)                   |
| `typos`        | latest    | Spelling (allow-list excludes common false positives, e.g. `crate`)      |
| `actionlint`   | latest    | GitHub Actions lint (`SC2016` disabled where shell vars are intentional) |

Local workflow:

```bash
make precommit          # equivalent to: pre-commit run --all-files
```

If a hook auto-fixes files, **stage them and run again**:

```bash
git add -A && pre-commit run --all-files
```

Repeat until clean. The pre-commit-auto-fix loop is deterministic: if it does
not converge in two passes, you have a real issue, not a flaky hook.

## Testing

- **Framework:** GoogleTest for C++, pytest for Python plugin SDK.
- **Coverage target:** core library ≥ 70% line coverage. Track via the
  `coverage` CMake preset.
- **Sanitizers:** ASan, UBSan, TSan run in a CI matrix (issue #41).
- **Device fixtures:** real HID captures live in `tests/fixtures/hid/` as
  `.bin` files with a sibling `.json` annotation. Replay via the
  `HidTransportFake` helper.
- **Dependency injection:** device classes accept a `TransportPtr` in their
  constructor, so tests inject a fake without going through hidapi (issue #42).
- **No network in tests.** Anything that would do I/O is mocked.

## Conventional commits & branching

- **Commits:** [Conventional Commits](https://www.conventionalcommits.org/).
  Allowed types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`,
  `build`, `ci`, `chore`, `revert`. Scope is the affected module
  (`feat(akp05): …`, `fix(core): …`).
- **Branches:** `feat/*`, `fix/*`, `docs/*`, `chore/*`, `refactor/*`. Use
  short imperative names, e.g. `feat/akp03-backend`,
  `fix/qml-profile-drag-crash`.
- **Squash vs merge:** small bug fixes squash; multi-step features keep their
  internal history if each commit compiles, tests pass and tells a coherent
  story. The PR description must include a one-line summary that becomes the
  release-notes entry.
- **No force-push to `main`.** Force-pushes on feature branches are fine until
  the PR opens; once review starts, prefer fixup commits.

## Documentation discipline

- **Single source of truth:** statistics, supported-device tables and feature
  matrices in the README and wiki are generated by
  `python3 scripts/generate-docs.py`. Never hand-edit content between
  `<!-- BEGIN AUTOGEN: … -->` and `<!-- END AUTOGEN: … -->` markers.
- **Pre-commit hook** runs the generator and fails if the working tree is
  dirty after generation — this guarantees code/docs cannot drift.
- **English only** for user-facing docs. Italian comments and PR conversations
  are fine; merged docs are English.
- **Architecture decisions** go in `docs/architecture/ADR-NNN-<title>.md` with
  status (`proposed | accepted | superseded`). Superseding an ADR requires a
  new ADR, not edits in place.
- **Changelog:** `CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/).
  The release workflow promotes the `[Unreleased]` block to a tagged section.

## Branding

The product is fully re-brandable at build time so downstream distributions and
forks can ship under a different name without forking the source tree.

| CMake variable       | Default value                              | Purpose                          |
| -------------------- | ------------------------------------------ | -------------------------------- |
| `AJAZZ_PRODUCT_NAME` | `AJAZZ Control Center`                     | Window title, About dialog, etc. |
| `AJAZZ_VENDOR_NAME`  | `Aiacos`                                   | Org segment in app-id, copyright |
| `AJAZZ_APP_ID`       | `io.github.Aiacos.AjazzControlCenter`      | Reverse-DNS application ID       |
| `AJAZZ_BRANDING_DIR` | `qrc:/qt/qml/AjazzControlCenter/branding/` | Resource root for icons, colours |

The runtime `BrandingService` singleton exposes these to QML
(`Branding.productName`, `Branding.vendorName`). Replace logos by overriding the
QRC root, **not** by patching the source. See
[`docs/architecture/BRANDING.md`](docs/architecture/BRANDING.md) for the full
recipe.

## Default UX expectations

These are user-visible defaults that must not regress without a release-note
entry:

- **Start minimised to tray** — the app launches as a tray icon by default; the
  main window opens on demand. The setting is exposed in Preferences for users
  who prefer to see the window on launch.
- **Automatic device discovery** — every supported VID/PID is detected at
  startup and on hot-plug via the cross-platform `HotplugMonitor`. No "scan"
  button is required.
- **No telemetry** — out of the box, the app makes zero outbound network
  requests. Update checks are opt-in. See [SECURITY.md](SECURITY.md) and the
  privacy section of the wiki.
- **Persisted profiles** — profile changes auto-save to disk through the
  atomic loader, with on-screen Apply/Revert affordances for explicit control.
- **Keyboard accessibility** — every workflow can be completed without a
  mouse. Tab order is meaningful; visible focus rings are mandatory.

## Security & supply chain

See [SECURITY.md](SECURITY.md) for disclosure. Engineering rules:

- **Pinned actions.** Third-party GitHub Actions are pinned to a commit SHA
  (`uses: foo/bar@<sha>  # v1.2.3`). Renovate raises PRs to bump them.
- **Pinned FetchContent.** `FetchContent_Declare(... GIT_TAG <sha>)` only;
  never a moving branch.
- **CodeQL + dependency review + secret scanning** run on every PR
  (`.github/workflows/codeql.yml`).
- **SHA256SUMS** are generated for every release and nightly artefact and
  uploaded alongside the binaries. macOS DMG is notarised; Windows MSI is
  Authenticode-signed (when signing certs are available — otherwise the
  release is marked "unsigned" in the body).
- **Plugins** run with a capability manifest. Until the out-of-process sandbox
  lands (issue #6), only signed plugins from the bundled allow-list are
  loaded. Unsigned plugins surface a "trust this plugin?" dialog.
- **Linux Flatpak** uses `--device=usb` (not `--device=all`).
- **No secrets in the repo, ever.** Pre-commit `gitleaks` will block.

## CI/CD & release engineering

Workflows under `.github/workflows/`:

| Workflow      | Trigger                         | Purpose                                           |
| ------------- | ------------------------------- | ------------------------------------------------- |
| `ci.yml`      | push, pull_request              | Build + test on Linux/Windows/macOS               |
| `lint.yml`    | push, pull_request              | clang-tidy, ruff, mdformat, typos, actionlint     |
| `nightly.yml` | schedule (daily) + push to main | Pre-release rolling installers + SHA256SUMS       |
| `release.yml` | tag `v*`                        | Signed, versioned release with notes + SHA256SUMS |
| `wiki.yml`    | push to main affecting `docs/`  | Sync `docs/wiki/` to the GitHub wiki              |

**Green main is non-negotiable.** A red badge on `main` is a release blocker.

## Adding a new device

1. **Capture.** Take a clean USB capture of the device performing the
   intended operation. Scrub serials. Place under `docs/protocols/captures/`.
1. **Spec.** Write or extend `docs/protocols/<family>.md` with the frame
   layout. Cite the capture filename.
1. **Backend.** Implement the device class in `src/devices/<family>/` against
   the abstract `Device`/`Transport` API. Inject the `TransportPtr`.
1. **Capabilities.** Expose `cellCount`, `encoderCount`, `touchStripRange`,
   `dpiStages`, etc. via the device-capabilities API so QML adapts
   automatically.
1. **Tests.** Unit-test the protocol against a recorded HID fixture. Add at
   least one happy-path test and one error-path test.
1. **Register.** Add the VID/PID to `src/devices/<family>/src/register.cpp`.
   Re-run `python3 scripts/generate-docs.py` to refresh the README stats.
1. **Release notes.** Add an entry under `[Unreleased] / Added` in
   `CHANGELOG.md`.

## Issue triage & priority labels

| Label | Meaning                                                         | Target SLA  |
| ----- | --------------------------------------------------------------- | ----------- |
| `P0`  | Crash, data loss, security incident, broken release on `main`.  | Same day    |
| `P1`  | Significant bug or missing feature blocking a primary use case. | Next minor  |
| `P2`  | Quality, polish, secondary feature.                             | Best effort |
| `P3`  | Nice-to-have, cosmetic, internal cleanup.                       | Whenever    |

Category labels (`security`, `quality`, `ui`, `feature`, `docs`, `testing`,
`plugins`, `akp`, `mouse`, `keyboard`, `branding`, …) are additive and used by
the project board to filter views.

When you open an issue, **always** assign at least one priority label and one
category label.

## Glossary

- **AKP** — internal codename for the AJAZZ stream-deck-class devices
  (`akp03`, `akp05`, `akp153`).
- **Capability** — a runtime-discoverable property of a device (cell count,
  encoder count, RGB depth, …) used by the UI to adapt itself.
- **Profile** — a JSON document mapping device cells/encoders/buttons to
  actions, persisted under the user's config dir.
- **Action** — a unit of behaviour invoked by a device input. Identified by an
  `id` string; serialised settings are an opaque payload owned by the action
  type.
- **AUTOGEN block** — a `<!-- BEGIN AUTOGEN: x -->` … `<!-- END AUTOGEN: x -->`
  pair in a Markdown file whose contents are produced by
  `scripts/generate-docs.py`.

______________________________________________________________________

If something here is unclear, **open a docs issue** rather than asking on
Discord — questions worth asking once are worth answering once and then
documenting forever.
