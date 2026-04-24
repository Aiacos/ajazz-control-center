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
2. Document the protocol in `docs/protocols/<family>/<model>.md`.
3. Implement the backend under `src/devices/<family>/<model>/`.
4. Add at least one capture-replay integration test.
5. Update `README.md`'s support matrix.

### 7. Documentation

Any change that alters a public API, configuration file, packaging artifact or user-visible behavior must update the corresponding documentation page.

### 8. Signing off

By submitting a PR you certify that your contribution complies with the [Developer Certificate of Origin](https://developercertificate.org/). Add a `Signed-off-by:` trailer to each commit (`git commit -s`).

## Review process

- Two maintainer approvals required for changes that touch `src/core` or the public plugin API.
- One approval for device backends, UI, docs.
- CI must be green on all three OS runners. Flaky tests are fixed, never retried.

## Release cadence

Release branches are cut from `main` when the support matrix advances. Tags follow [Semantic Versioning 2.0.0](https://semver.org/). Release notes are generated from Conventional Commits history.
