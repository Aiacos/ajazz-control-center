# CI failure remediation

## Goal

Resolve the confirmed Windows compiler failure, remove the Markdown lint
failure at its source, and keep the only Russian project document in English.

## Confirmed findings

- MSVC treats `std::strerror` deprecation warning C4996 as error C2220 under
  `/WX` in `src/core/src/hid_transport.cpp`.
- The docs job runs `pre-commit run mdformat --all-files`, while the main lint
  job already runs the full pre-commit suite including `mdformat`. The failing
  run reports that Markdown files were modified. The later auto-fix commit
  `20fc398` applies those formatting changes. The duplicate docs check can
  independently mark the original commit red even while lint fixes formatting.
- Dependabot runs with `action_required` have no jobs and await GitHub's
  workflow approval; this is a repository security gate, not a code failure.
- `docs/superpowers/plans/2026-10-03-aj159-hid-permissions.md` is the only
  Markdown document containing Russian prose.

## Implementation plan

1. Add a focused regression test for HID open-error formatting and confirm it
   fails before implementation.
1. Replace the deprecated C runtime call with portable `std::error_code`
   message formatting while preserving the permission hint.
1. Remove duplicate Markdown formatting from the docs job; keep the canonical
   pre-commit job responsible for formatting and its existing fix-and-rerun
   behavior.
1. Translate the Russian HID recovery plan into English without changing its
   recorded technical facts.
1. Run the focused test, build, relevant smoke checks, docs check, YAML parse,
   and a diff audit. Do not commit or push.

## Acceptance criteria

- Windows builds compile the HID transport with `/WX` and report a useful
  error message on open failure.
- Tests cover ordinary HID open errors and the access-denied hint.
- Exactly one CI job owns the all-files Markdown format check.
- The project Markdown documentation contains no Russian prose.
- Dependabot approval status is reported separately from code test failures.

## Verification results

- TDD RED was observed at the link stage before the formatter implementation
  existed; the focused suite then passed 3/3 cases after implementation.
- `cmake --build --preset dev --parallel 2` passed, including the application,
  QML tests, integration tests, and unit target.
- `ctest --preset dev --output-on-failure` passed all 879 tests with
  `ASAN_OPTIONS=detect_leaks=0`. The default sanitizer run still reports nine
  pre-existing Qt allocation leaks in `QCollator` and `QWebSocket`; the Catch2
  assertions for those cases pass.
- The GUI smoke stayed alive for the bounded 8-second run, completed bootstrap,
  enumerated the AJ159 device, and opened `VID=3151 PID=5007` through the
  usage/page filtered HID path.
- `make docs-check`, YAML parsing, C++ formatting, and `git diff --check` pass.
- The local environment has no `pre-commit`, `mdformat`, or `actionlint`; the
  equivalent CI hooks must still run on GitHub.
- Windows compilation was not available locally; the code path no longer uses
  the deprecated `std::strerror` call and must be confirmed by the Windows CI
  runner.

## Risks and rollback

Use `std::error_code` with `std::generic_category` to avoid the deprecated
MSVC C runtime interface while retaining a portable error description. Revert
the focused source, test, workflow, and documentation changes to roll back.
