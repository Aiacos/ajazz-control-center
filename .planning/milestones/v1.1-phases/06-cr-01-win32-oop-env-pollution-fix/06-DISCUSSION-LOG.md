# Phase 6: CR-01 Win32 OOP Env Pollution Fix - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-14
**Phase:** 06-cr-01-win32-oop-env-pollution-fix
**Areas discussed:** Helper shape, Test scope
**Mode:** `/gsd-autonomous --interactive` (default discuss-phase mode)

______________________________________________________________________

## Helper shape

### Q1 — Per-spawn UTF-16 env block: reusable class, inline static helper, or inline lambda?

| Option                                 | Description                                                                                                                                                                                            | Selected |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------- |
| Reusable Win32EnvBlock class with RAII | Standalone class in src/plugins/src/win32_env_block.{hpp,cpp}. Both CreateProcessW and CreateProcessAsUserW callers construct one. ~80 LoC including tests; cleaner separation; testable in isolation. | ✓        |
| Inline static helper function          | Anonymous-namespace function in out_of_process_plugin_host_win32.cpp. Tighter blast radius; harder to unit-test in isolation.                                                                          |          |
| Inline lambda at the spawn site        | Maximum locality. Must be duplicated across two spawn sites OR factored out anyway.                                                                                                                    |          |

**User's choice:** Reusable Win32EnvBlock class with RAII.
**Notes:** Both CreateProcessW and CreateProcessAsUserW spawn paths need the env block (per FIX-DEFERRED.md). A class avoids duplication and is testable without spawning a real process.

______________________________________________________________________

## Test scope

### Q1 — Windows CI test depth: block-content unit tests only, full-spawn integration only, or both?

| Option                                          | Description                                                                                                                                                                                                                                                    | Selected |
| ----------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Unit (block content) + integration (full spawn) | Two tests: helper class assertions (sort, \\0\\0, =-prefix preserved, override semantics) + actually CreateProcessW a Python child that prints PYTHONPATH; assert child sees override + parent's PYTHONPATH unchanged after spawn. Covers WIN32-03 + WIN32-04. | ✓        |
| Unit only                                       | Cheap; doesn't satisfy WIN32-03 ('exercise the spawn path, not just compile').                                                                                                                                                                                 |          |
| Integration only                                | End-to-end through real spawn; if block builder breaks, failure manifests as 'Python child crashes' rather than 'block builder produced wrong bytes'. Less helpful diagnostics.                                                                                |          |

**User's choice:** Unit (block content) + integration (full spawn).
**Notes:** The integration test's parent-PYTHONPATH-unchanged assertion is the actual CR-01 regression test — fails on pre-Phase-6 code (the \_putenv_s pollutes the parent), passes on post-Phase-6 code. Concurrent N=2 spawn variant catches the cross-instance race the v1.0 review flagged.

______________________________________________________________________

## Claude's Discretion

The following were left for Claude / planner to decide:

- **Override semantics** — case-insensitive key match (Windows env vars are case-insensitive in lookup), override replaces existing value with override's casing. Captured as D-04 in CONTEXT.md.
- **CREATE_UNICODE_ENVIRONMENT flag placement** — added to `creationFlags |= CREATE_UNICODE_ENVIRONMENT` near line 487 alongside CREATE_NO_WINDOW.
- **Test fixture Python script** — small `print_pythonpath.py` in `tests/integration/fixtures/` (~5 lines).
- **Win32EnvBlock placement** — `src/plugins/src/win32_env_block.{hpp,cpp}` (not exported in any public header).
- **bInheritHandles** — leave alone (Pitfall 5 warns against modifying it while landing CR-01).

## Deferred Ideas

- **std::atomic<int> ctor counter** (FIX-DEFERRED path 2) — defense-in-depth not needed because path 1 closes the race directly.
- **manifest_signer_win32.cpp parent-env audit** — verifier sees clean env once parent stays clean; no further verifier work needed.
- **AppVerifier / Windows debug heap integration** — could be added as a separate slower CI job. Defer to v1.2 unless tests flake on this surface.
- **Win32EnvBlock applied to other CreateProcessW sites** — none exist today; track as 'while you're in there' pattern for future Win32 child-spawn sites.
