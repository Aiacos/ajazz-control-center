# Contributing to AJAZZ Control Center

Thank you for your interest in improving AJAZZ Control Center. This document describes the conventions, branching model and review workflow for contributions of any kind — code, documentation, device captures, UI/UX, translations.

## Code of conduct

Be respectful, patient and constructive. Hardware reverse engineering is a collaborative, iterative effort: nobody owns a device family, and nobody is expected to know everything. Questions are always welcome.

## How to contribute

### 1. Open an issue first for non-trivial work

For anything beyond a typo fix or a small bug, open a GitHub issue and describe what you want to do. This avoids duplicated effort and gives maintainers a chance to flag architectural concerns early.

### 2. Fork, branch, pull request

- Fork the repository to your account.
- Create a topic branch: `feat/akp03-backend`, `fix/qml-profile-drag-crash`, `docs/adding-a-device`.
- Keep commits focused and reviewable. Squash trivia; preserve logically distinct steps.
- Open a PR against `main`. Link the issue it closes.

### 3. Commit style

We follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(streamdeck): add AKP03 backend scaffolding
fix(core): close HID handle on disconnect race
docs(protocols): document AKP153 set-brightness frame
ci: enable macOS arm64 runner
```

Scopes match top-level module directories (`core`, `app`, `streamdeck`, `keyboard`, `mouse`, `plugins`, `ui`, `ci`, `docs`).

### 4. Coding standards

- **C++20**, `-Wall -Wextra -Wpedantic`, treat-warnings-as-errors in CI.
- `clang-format` + `clang-tidy` configuration lives at the repository root; run `cmake --build --target format` before pushing.
- Prefer `std::` over Qt containers at module boundaries; Qt types inside UI and QObject-owning code only.
- No raw `new`/`delete`; use `std::unique_ptr` or Qt parent ownership.
- **Python 3.11+**, `ruff` for linting, `black` for formatting, full type hints on public plugin API.
- **QML**: one component per file, `PascalCase` filenames, imports sorted, use `required property` where possible.

### 5. Tests

- Every new module ships with unit tests (`tests/unit`) covering protocol encoders/decoders.
- Device backends include integration tests that replay recorded USB captures (no hardware required in CI).
- Target coverage for `src/core` and protocol encoders is **≥ 85 %**.

### 6. Adding a new device

See [`docs/guides/ADDING_A_DEVICE.md`](docs/guides/ADDING_A_DEVICE.md). In short:

1. Capture USB traffic with Wireshark + usbmon (Linux) or USBPcap (Windows).
1. Document the protocol in `docs/protocols/<family>/<model>.md`.
1. Implement the backend under `src/devices/<family>/<model>/`.
1. Add at least one capture-replay integration test.
1. **Add an entry to `docs/_data/devices.yaml`** — the README support matrix
   and every table in the wiki are regenerated from this YAML. Do not edit
   the tables by hand; see §7.2 below.

### 7. Documentation

Any change that alters a public API, configuration file, packaging artifact or user-visible behavior must update the corresponding documentation page.

#### 7.1 Docs live in two places

- `README.md` — repository landing page.
- `docs/wiki/*.md` — published to the GitHub Wiki on every push to `main`
  by the `wiki.yml` workflow.

Keep deep-dive content in the wiki; keep the README short and marketing-y.

#### 7.2 README and wiki are generated — never hand-edit AUTOGEN blocks

Device tables, statistics and legends appear in multiple files. To avoid
drift, they are **generated** from a single source of truth:

- **Source of truth:** [`docs/_data/devices.yaml`](docs/_data/devices.yaml)
- **Generator:** [`scripts/generate-docs.py`](scripts/generate-docs.py)
- **Targets:** `README.md`, `docs/wiki/Supported-Devices.md`,
  `docs/wiki/Home.md`

Inside each target, a block of the form

```markdown
<!-- BEGIN AUTOGEN: devices-by-family -->
   … generated table …
<!-- END AUTOGEN: devices-by-family -->
```

is rewritten in place on every run. Available block names:
`devices-table`, `devices-by-family`, `platform-matrix`, `stats`,
`legend`, `toc-wiki`.

**Workflow:**

```bash
# Edit the source
$EDITOR docs/_data/devices.yaml

# Regenerate all AUTOGEN blocks
make docs

# Commit both the YAML and the regenerated files together
git add docs/_data/devices.yaml README.md docs/wiki/
git commit -m "docs(devices): add AKP815"
```

**Automation keeps this invariant:**

- A pre-commit hook (`regenerate-docs`) runs `make docs` whenever
  `docs/_data/**`, `src/devices/**`, the generator script, the README or
  any wiki page changes — so you (almost) never have to remember `make docs`.
- CI runs `python3 scripts/generate-docs.py --check` and fails a PR if
  any AUTOGEN block is out of date.

If you *really* need custom prose next to a generated table, add it
*outside* the `BEGIN`/`END` markers. Anything between the markers will be
overwritten.

### 8. Signing off

By submitting a PR you certify that your contribution complies with the [Developer Certificate of Origin](https://developercertificate.org/). Add a `Signed-off-by:` trailer to each commit (`git commit -s`).

## Review process

- Two maintainer approvals required for changes that touch `src/core` or the public plugin API.
- One approval for device backends, UI, docs.
- CI must be green on all three OS runners. Flaky tests are fixed, never retried.

## Release cadence

Release branches are cut from `main` when the support matrix advances. Tags follow [Semantic Versioning 2.0.0](https://semver.org/). Release notes are generated from Conventional Commits history.
