---
phase: 06-cr-01-win32-oop-env-pollution-fix
plan: 02
status: complete
date: 2026-05-14
commits:
  - 270c415  # test(06-02): add Win32EnvBlock unit tests
  - 29cb3ce  # test(06-02): add OOP host env pollution integration test (CR-01 regression)
requirements_closed:
  - WIN32-03
---

# Plan 06-02 Summary — Catch2 coverage for CR-01

## Outcome

Two commits per the plan's "atomic commit boundaries" rule:

- `270c415` — Task 1: Win32EnvBlock unit tests (5 TEST_CASEs).
- `29cb3ce` — Task 2: OOP host integration test + Python fixture (3 TEST_CASEs).

## Key files created

- `tests/unit/test_win32_env_block.cpp` (240 lines, `#ifdef _WIN32`-guarded)
  - `TEST_CASE("Win32EnvBlock ends in double-null terminator")`
  - `TEST_CASE("Win32EnvBlock entries are sorted case-insensitively")`
  - `TEST_CASE("Win32EnvBlock preserves =-prefixed drive-letter entries at front")`
  - `TEST_CASE("Win32EnvBlock override replaces existing parent value (case-insensitive key)")`
  - `TEST_CASE("Win32EnvBlock new override key appends in sort order")`
- `tests/integration/test_oop_plugin_host_win32_env.cpp` (301 lines, `#ifdef _WIN32`-guarded)
  - `TEST_CASE("OOP env block delivers PYTHONPATH override to child", "[oop_env][integration]")`
  - `TEST_CASE("OOP env block does not pollute parent PYTHONPATH (CR-01 regression)", "[oop_env][integration][CR-01]")` ← **THIS IS THE CR-01 REGRESSION ASSERT**
  - `TEST_CASE("OOP env block isolates concurrent spawns (cross-instance race closed)", "[oop_env][integration][concurrency]")`
- `tests/integration/fixtures/print_pythonpath.py` (15 lines) — spawn target.

## Key files modified

- `tests/unit/CMakeLists.txt` — `test_win32_env_block.cpp` + `win32_env_block.cpp` added to Win32 branch; `src/plugins/src` added to include path.
- `tests/integration/CMakeLists.txt` — Win32 `if(WIN32)` branch added wiring the integration test source + Win32EnvBlock impl + include path. POSIX build unchanged.

## Design notes

The integration test does NOT instantiate `OutOfProcessPluginHost` (that pulls pybind11 indirectly). Instead it builds the same env-block + CreateProcessW pattern in `spawnPythonCaptureStdout`, including the production override set (`PYTHONPATH`, `PYTHONDONTWRITEBYTECODE`, `PYTHONUNBUFFERED`) and `CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW`. This is the "narrow probe of the env-block path" architecture D-03 stipulates.

Each integration test SKIPs if `python3.exe` is not on PATH (located via `SearchPathW`). The Windows CI matrix already uses `actions/setup-python` (verified in `.github/workflows/ci.yml` line 93), so the SKIP path is dev-box-only — no CI YAML change needed (one of the plan's success criteria).

## Verification

**Local POSIX:** `cmake --build build/linux-debug --target ajazz_integration_tests` succeeds; only `test_capture_replay.cpp` compiles (the new Win32 sources are gated out).

**Windows CI:** the next push triggers the `windows-2022` matrix run. 5 unit-test cases + 3 integration-test cases (8 total) become discoverable via `catch_discover_tests`. The `[CR-01]`-tagged integration test is the load-bearing one — it reads `_wgetenv(L"PYTHONPATH")` BEFORE and AFTER the spawn and `REQUIRE`s equality.

## WIN32-04 evidence

Plan 06-02's three test cases each construct a single-entry override (no duplicate keys), so they do NOT observe the WIN32-04 duplicate-key precedence question. That answer requires the probe test added by Plan 06-03 Task 1.

## CI prerequisite check

The `windows-2022` ci.yml job already has `actions/setup-python@a309ff8b426b58ec0e2a45f0f869d46889d02405` at line 93 (verified). No follow-up CI YAML change needed.

## Deviations from plan

- The integration test's stdout capture converts UTF-8 → UTF-16 via `MultiByteToWideChar` so comparisons against wide-string sentinels are exact. Plan didn't specify the byte-encoding handling; the implementation chose UTF-8 input → UTF-16 conversion as the most defensive (Python writes UTF-8 to a Windows console pipe by default).
- Added a `locatePython()` helper that tries `python3.exe` first then `python.exe`. Plan suggested using `_wsearchenv_s`; `SearchPathW` is the clearer Win32 idiom and is what existing project code uses.

## CI evidence link

Deferred until next Windows CI green build — see Plan 06-03 for the WIN32-04 follow-up.
