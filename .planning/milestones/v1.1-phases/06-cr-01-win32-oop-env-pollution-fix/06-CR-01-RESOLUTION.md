# WIN32-04 Resolution: CreateProcessW duplicate `PYTHONPATH` key precedence

**Status:** Awaiting CI observation (probe test added in commit `13565c6`).
**Date:** 2026-05-14
**CI evidence:** TBD — see "Pending CI observation" below. First Windows CI run that includes commit `13565c6` and reaches the `[duplicate-key-probe]`-tagged test produces the evidence. The next Windows CI run URL will be back-filled into this section in a follow-up `docs(06-03):` commit.
**Observed answer:** TBD (one of `first-wins` / `last-wins`). The probe test prints a verbatim CI log line of the form `WIN32-04 result: PYTHONPATH = C:\probe\first` or `... = C:\probe\second`.

## Why the question existed

Microsoft's `CreateProcessW` documentation describes the `lpEnvironment` block layout (a `\0`-terminated list of `KEY=VALUE` wide entries ending in `\0\0`) but does NOT explicitly state behaviour for duplicate keys. The sequential block-walk semantics in the docs imply first-wins. Chris Wellons's nullprogram analysis (2023-08-13) reports empirical observation that Windows uses last-wins — contradicting the implicit first-wins reading of MS docs.

References:

- MS reference: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
- nullprogram analysis: https://nullprogram.com/blog/2023/08/13/

## Resolution method

Plan 06-03 added a `[duplicate-key-probe]`-tagged Catch2 integration test (`tests/integration/test_oop_plugin_host_win32_env.cpp`, commit `13565c6`). The test hand-constructs a malformed env block containing two `PYTHONPATH=` entries (`PYTHONPATH=C:\probe\first` followed by `PYTHONPATH=C:\probe\second`), spawns a Python child via `CreateProcessW` with `CREATE_UNICODE_ENVIRONMENT`, and captures `os.environ.get('PYTHONPATH', '')`.

The test uses `WARN` (not `REQUIRE`) for the captured value — its purpose is observation, not assertion. A `REQUIRE((isFirst || isLast))` sanity check ensures the spawn produced one of the two expected sentinels (catches Win32 contract drift or a buggy hand-built block).

The probe deliberately bypasses `Win32EnvBlock` (which by design collapses to one entry per key — see CONTEXT D-04). It is the ONLY place in the codebase that builds an env block with duplicate keys; production code never does this.

## Pending CI observation

This document was committed before the first Windows CI run with commit `13565c6` was available. The Windows CI run output will include the line:

```
WIN32-04 result: PYTHONPATH = {observed value}
```

The follow-up `docs(06-03): record WIN32-04 CI evidence` commit back-fills:

1. The CI run URL (matching the regex `github\.com/.+/actions/runs/[0-9]+`).
1. The observed answer (one of `first-wins` / `last-wins`).
1. The verbatim WARN line from the CI log.

## Why our runtime doesn't depend on the answer

`Win32EnvBlock` (Plan 06-01, commit `29f87b2`) merges parent env with overrides using case-insensitive key matching (`_wcsicmp`). By construction the resulting block contains EXACTLY ONE entry per key. The duplicate-key precedence question therefore has NO bearing on our runtime — our spawn never produces a block with duplicates.

The answer matters for:

1. Understanding what OTHER `CreateProcessW` callers (third-party DLLs pre-loading env, future ad-hoc Win32 helpers in this codebase, etc.) might experience.
1. Reviewing PRs that touch Win32 child-spawn paths — if a future PR builds an env block with duplicates "for safety" or "as defense-in-depth", it should be flagged with reference to this resolution.
1. Closing requirement WIN32-04 contractually so the milestone v1.1 requirements coverage table can mark it complete.

## Future-touch notes

If a future contributor encounters this question, they should:

- Cite this artefact and (once back-filled) the CI evidence URL.
- NOT re-run the probe unless they suspect Windows behaviour has changed (which would be an ADR-worthy event).
- Use the established `Win32EnvBlock` helper for any new `CreateProcessW` call site, which sidesteps the question entirely.

## Cross-references

- Plan 06-01 (commit `29f87b2`): introduced `Win32EnvBlock` + removed `_putenv_s`.
- Plan 06-02 (commits `270c415` + `29cb3ce`): unit + integration test coverage.
- Plan 06-03 (commit `13565c6`): probe test added.
- CONTEXT D-04: case-insensitive override semantics + duplicate-key question framing.
- PITFALLS Pitfall 5: Win32 env-block UTF-16 layout (four mandatory sub-traps).
