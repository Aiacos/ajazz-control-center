---
phase: 06-cr-01-win32-oop-env-pollution-fix
plan: 01
status: complete
date: 2026-05-14
commits:
  - 29f87b2  # fix(06-01): replace _putenv_s with per-spawn Win32EnvBlock (CR-01)
requirements_closed:
  - WIN32-01
  - WIN32-02
---

# Plan 06-01 Summary — Atomic CR-01 fix

## Outcome

Single atomic commit `29f87b2` per D-02, four bullets visible in one diff:

1. New `Win32EnvBlock` RAII helper (`src/plugins/src/win32_env_block.{hpp,cpp}`).
1. Three `_putenv_s` calls **deleted** from `out_of_process_plugin_host_win32.cpp`.
1. `creationFlags |= CREATE_UNICODE_ENVIRONMENT` on the spawn path.
1. Both `CreateProcessW` and `CreateProcessAsUserW` receive `envBlock.data()` as `lpEnvironment`.

`bInheritHandles` remains `TRUE` at both spawn sites (Pitfall 5 explicit warning honored).

## Key files created

- `src/plugins/src/win32_env_block.hpp` (110 lines)
- `src/plugins/src/win32_env_block.cpp` (155 lines)

## Key files modified

- `src/plugins/src/out_of_process_plugin_host_win32.cpp` (+51 / -15)
  - Added `#include "win32_env_block.hpp"` + `#include <map>`.
  - Replaced the `_putenv_s` block with stack-local `envOverrides` map + `Win32EnvBlock envBlock{...}` construction.
  - Lifetime spans both spawn branches (single instance declared above the `if/else`).
- `src/plugins/CMakeLists.txt` (+4)
  - `src/win32_env_block.cpp` added inside the existing `if(WIN32)` branch.

## Verification

**Local (POSIX dev box):** `cmake --build build/linux-debug --target ajazz_plugins` succeeds. The new `win32_env_block.cpp` TU is gated by `if(WIN32)` so it does not compile on Linux — verified by ninja log showing only POSIX TUs scanned.

**Windows CI (real verification):** the next `windows-2022` matrix run on push triggers Phase 6's compile coverage. Behavioural verification is Plan 06-02's job.

**Automated invariants checked locally:**

```
$ grep -nE '_putenv_s\s*\(' src/plugins/src/out_of_process_plugin_host_win32.cpp
(no match — atomic removal complete)

$ grep -c 'envBlock\.data()' src/plugins/src/out_of_process_plugin_host_win32.cpp
3   # 2 spawn sites + 1 in a comment

$ grep -n 'CREATE_UNICODE_ENVIRONMENT' src/plugins/src/out_of_process_plugin_host_win32.cpp
508:    DWORD creationFlags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
```

## Deviations from plan

None. The plan's algorithm (Pitfall 5 four sub-traps + D-04 case-insensitive override) was implemented as specified, with one defensive addition: a `m_block.size() == 1` guard in the empty-env path so the `\0\0` block-terminator invariant is unconditional (the loops emit nothing when both `frontEntries` and `mergedEntries` are empty; without the guard the buffer would end at a single `\0`).

## Pre-commit hook interaction

The first commit attempt was rejected because `clang-format` reflowed the new files and `cmake-format` reflowed the new comment in `src/plugins/CMakeLists.txt`. The reformatted files were re-staged (with the formatter's output) and the second commit attempt passed all hooks. No `--amend` used; this was a fresh commit on the formatted content, per project workflow.

## Next plan

Plan 06-02 — Catch2 unit + integration tests for `Win32EnvBlock` and the OOP host spawn path. The integration test contains the actual CR-01 regression assert (`_wgetenv(L"PYTHONPATH")` before == after).

## CI evidence link

To be filled in after the next push triggers the `windows-2022` matrix run. Phase 6 is currently 13+ commits ahead of `origin/main` with parallel work from Phases 4 + 7; the push happens at the end of the milestone-wide flush.
