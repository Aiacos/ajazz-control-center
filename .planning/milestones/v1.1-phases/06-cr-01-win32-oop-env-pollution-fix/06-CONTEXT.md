---
phase: 6
phase_slug: cr-01-win32-oop-env-pollution-fix
gathered: 2026-05-14
status: Ready for planning
mode: autonomous-interactive
---

# Phase 6: CR-01 Win32 OOP Env Pollution Fix — Context

**Gathered:** 2026-05-14 via `/gsd-autonomous --interactive`
**Source for recommendations:** `.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md` (CR-01 entry — preferred fix path) + `.planning/research/PITFALLS.md` Pitfalls 5 + 6 + REQUIREMENTS WIN32-01..04.

<domain>
## Phase Boundary

A user spawning the OOP plugin host on Windows no longer pollutes the parent process's environment, and Windows CI proves it. The Win32 backend at `src/plugins/src/out_of_process_plugin_host_win32.cpp:451-467` currently calls `_putenv_s` for `PYTHONPATH` / `PYTHONDONTWRITEBYTECODE` / `PYTHONUNBUFFERED` in the **parent** process before spawning the child via `CreateProcessW(lpEnvironment=nullptr)`. This pollutes:

1. The parent process's environment for the rest of its lifetime.
1. Any subsequently-spawned `OutOfProcessPluginHost` instance (cross-instance leak — the v1.0 review's central concern).
1. Any sibling subprocess spawned by the parent — notably the manifest verifier in `manifest_signer_win32.cpp` — which inherits the polluted env.

Phase 6 replaces this with per-spawn UTF-16 env blocks passed to `CreateProcessW` / `CreateProcessAsUserW` with `CREATE_UNICODE_ENVIRONMENT`, deletes the three `_putenv_s` calls atomically, and adds Windows CI coverage that proves parent env is unchanged after a child spawn.

Maps to requirements: WIN32-01 .. WIN32-04 (full text in `.planning/REQUIREMENTS.md`).

</domain>

<decisions>
## Implementation Decisions (locked)

### D-01 — Helper shape: reusable `Win32EnvBlock` class with RAII

A new standalone class `Win32EnvBlock` in `src/plugins/src/win32_env_block.{hpp,cpp}` (Win32-only, guarded by `#ifdef _WIN32`):

- **Constructor:** takes a `std::map<std::wstring, std::wstring>` of overrides.
- Calls `GetEnvironmentStringsW` to snapshot the parent env, walks the `\0`-terminated list, applies override semantics (per D-04 below: case-insensitive key match, override replaces existing, new key appends), preserves `=`-prefixed drive-letter entries verbatim.
- Sorts the merged list case-insensitively (`_wcsicmp` on the key portion up to the first `=`) before serialization — Windows requires sorted env blocks.
- Serializes to a `std::vector<wchar_t> m_block` with each entry `KEY=VALUE\0` and a final `\0\0` block terminator.
- Frees the snapshot via `FreeEnvironmentStringsW` (NOT `delete[]`, NOT `LocalFree`).
- **Destructor:** noop (the snapshot is freed in the constructor; the vector cleans up its own backing on destruction).
- **`.data()` accessor:** returns `LPVOID` pointing at the buffer for `CreateProcessW(lpEnvironment=...)`. Lifetime contract: caller must not let the `Win32EnvBlock` go out of scope before `CreateProcessW` returns.

**Why class instead of inline:** Both the `CreateProcessW` (line 554) and `CreateProcessAsUserW` (line 542) call sites need the same env block. A class avoids duplication and is testable in isolation without spawning a real process.

**Why RAII (snapshot freed in ctor, not via destructor):** The snapshot lifetime is bounded to construction — once we've copied entries into our owned `std::vector<wchar_t>`, the snapshot is no longer needed. Releasing it eagerly avoids holding a Win32 resource for the duration of a potentially-long `CreateProcessW` call.

### D-02 — Atomic `_putenv_s` removal in the same commit

The three `_putenv_s` calls at `out_of_process_plugin_host_win32.cpp:464-467` are deleted in the **same commit** as the `Win32EnvBlock` introduction. Pitfall 6: "the new env-block path is correct **iff** the `_putenv_s` mutations are gone" — keeping them as belt-and-braces makes the new code dead while preserving the bug.

The plan task that introduces `Win32EnvBlock` is the same task that wires it into both spawn call sites and removes the three `_putenv_s` lines. Single atomic commit. PR diff must show:

- Three `-_putenv_s(...)` lines removed
- `Win32EnvBlock` instance constructed in the spawn function before the `CreateProcessW` / `CreateProcessAsUserW` calls
- `CREATE_UNICODE_ENVIRONMENT` flag added to `creationFlags`
- `lpEnvironment = envBlock.data()` (or equivalent) passed to BOTH `CreateProcessW` AND `CreateProcessAsUserW` (the AppContainer/restricted-token branch)

### D-03 — Test scope: unit test (block content) + integration test (full spawn)

**Unit test** (`tests/unit/test_win32_env_block.cpp`, Catch2):

- Build a block from `{L"PYTHONPATH" → L"C:\\foo;C:\\bar", L"KEY" → L"val"}`.
- Assert: block ends in `\0\0` (verify `block[block.size()-1] == 0 && block[block.size()-2] == 0`).
- Assert: entries are sorted case-insensitively by key (parse the block, walk entries, check ordering).
- Assert: `=`-prefixed drive-letter entries from the parent env are preserved at the front.
- Assert: an override of an existing parent env key replaces the value (not appends a duplicate).
- Assert: a new override key appends in sort order.

**Integration test** (`tests/integration/test_oop_plugin_host_win32_env.cpp`, Catch2 + a tiny Python helper script):

- Pre-step: store parent's `_wgetenv(L"PYTHONPATH")`.
- Spawn a small Python child that prints `os.environ.get('PYTHONPATH', '')` to stdout and exits.
- Assert: the captured stdout equals the override value passed to the spawn.
- Assert: parent's `_wgetenv(L"PYTHONPATH")` after the spawn is **unchanged** from the pre-step value (this is the actual CR-01 regression test — fails today on the `_putenv_s` code, must pass after the fix).
- Run the spawn N=2 times concurrently to assert: cross-instance pollution race the v1.0 review specifically flagged is closed (no leak between concurrent constructors).

Both tests run on the existing `windows-2022` GitHub Actions matrix entry in `ci.yml` — no CI infra changes needed.

### D-04 — Override semantics: case-insensitive key match, replace-on-collision

When merging parent env + overrides, use **case-insensitive key matching** (`_wcsicmp` up to the first `=`) — Windows env vars are case-insensitive in lookup but case-preserved in storage. If the override map provides `L"PYTHONPATH"` and the parent env has `L"PythonPath"`, the override **replaces** the parent's value. The replacement entry uses the **override's casing** (`L"PYTHONPATH=..."`).

For the duplicate-key precedence question (WIN32-04, MS docs vs nullprogram disagreement on first-wins / last-wins for inherited PYTHONPATH): the integration test in D-03 will **observe** which one Windows actually uses. The phase artefact records the observed answer. Until then, our merge logic ensures there's only ONE entry per key in the produced block (so the question is moot for our spawn — the answer matters only for understanding what we're protecting against).

### Claude's Discretion

- **Test fixture Python script** — small `print_pythonpath.py` lives in `tests/integration/fixtures/` (or equivalent existing fixture dir). Imports `os`, prints `os.environ.get('PYTHONPATH', '')`, exits. ~5 lines.
- **`CREATE_UNICODE_ENVIRONMENT` flag** — added to `creationFlags |= CREATE_UNICODE_ENVIRONMENT;` near line 487 where `CREATE_NO_WINDOW` is already added.
- **`Win32EnvBlock` ctor signature** — `Win32EnvBlock(std::map<std::wstring, std::wstring> overrides)` taking by value. Internal sorted vector for serialization.
- **`Win32EnvBlock` placement under `src/plugins/src/`** — same dir as the host code; not exported in any public header (it's an implementation detail of the Win32 host backend).
- **`bInheritHandles` interaction** — Pitfall 5 warns "do not modify `bInheritHandles` while landing CR-01". The current code passes `TRUE` (line 547, 558); leave it.
- **`std::atomic<int>` ctor counter** (alternative path 2 from FIX-DEFERRED) — NOT added. The per-spawn env block (path 1) handles the cross-instance race directly; the counter would be defense-in-depth that adds API surface (throwing constructor) for a problem already solved. Recorded as deferred in case future review wants belt-and-braces.

</decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Deferred-fix doc (the spec)

- `.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md` — CR-01 entry: preferred fix path (per-spawn env block) and why path 2 (ctor counter) is the alternative. Phase 6 implements path 1.

### Pitfalls research (locked design constraints)

- `.planning/research/PITFALLS.md` Pitfall 5 — Win32 env-block UTF-16 layout. **All four sub-traps** (missing second null, lifetime, sort order, `=`-drive-letter entries, `bInheritHandles` interaction) are mandatory implementation rules.
- `.planning/research/PITFALLS.md` Pitfall 6 — `_putenv_s` left in place "for safety" defeats the entire fix. Drives D-02 atomic-removal rule.
- `.planning/research/PITFALLS.md` Cross-Cutting "Linux-CI-blind Windows breakage" — mandates Windows CI coverage (already satisfied by the existing `windows-2022` matrix entry).

### Requirements & roadmap

- `.planning/REQUIREMENTS.md` — WIN32-01..04 verbatim.
- `.planning/ROADMAP.md` Phase 6 success criteria — four contractual SC1..SC4.

### Existing code (touched by this phase)

- `src/plugins/src/out_of_process_plugin_host_win32.cpp:451-467` — the `_putenv_s` block to delete.
- `src/plugins/src/out_of_process_plugin_host_win32.cpp:541-552` — `CreateProcessAsUserW` branch (AppContainer/restricted-token), needs the same env block.
- `src/plugins/src/out_of_process_plugin_host_win32.cpp:553-570` — `CreateProcessW` branch (plain spawn), needs the same env block.
- `src/plugins/src/out_of_process_plugin_host_win32.cpp:487` — `creationFlags = CREATE_NO_WINDOW;` — add `| CREATE_UNICODE_ENVIRONMENT`.

### CI infrastructure (already in place)

- `.github/workflows/ci.yml` — has `windows-2022` matrix entry with `windows-release` preset. Phase 6's new tests run there; no workflow changes needed (just new test targets in `tests/unit/CMakeLists.txt` and `tests/integration/CMakeLists.txt`).

### Reference docs (verified at research time)

- [CreateProcessW — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw) — `lpEnvironment` block layout, `\0\0` Unicode block terminator, `CREATE_UNICODE_ENVIRONMENT` flag, sort order.
- [Environment Variables — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/procthread/environment-variables) — sort order, `GetEnvironmentStringsW` / `FreeEnvironmentStringsW`.

\</canonical_refs>

\<code_context>

## Existing Code Insights

### Reusable Assets

- **`utf8ToWide` helper** — already used at line 471. The `Win32EnvBlock` ctor can convert UTF-8 override keys/values via this helper if callers prefer to pass `std::map<std::string, std::string>` instead of `std::map<std::wstring, std::wstring>`. (Either signature is fine; the wstring form aligns better with the Win32 API.)
- **`STARTUPINFOEXW` + extended-attribute-list pattern** at lines 475-534 — the AppContainer/restricted-token spawn path is non-trivial. Phase 6 adds env-block plumbing alongside it without disturbing the attribute-list logic.
- **Catch2 unit test conventions** — existing tests in `tests/unit/` use `TEST_CASE` + `REQUIRE`. New `test_win32_env_block.cpp` follows the same pattern.
- **Existing Windows-only test guards** — search for `#ifdef _WIN32` in `tests/unit/` to find the existing pattern for Windows-only test files (or `target_compile_definitions(... _WIN32_TEST)` in CMakeLists, depending on what the codebase uses).

### Established Patterns

- **`#ifdef _WIN32` guarded files** — entire `out_of_process_plugin_host_win32.cpp` is Win32-only. `Win32EnvBlock` follows the same pattern: header + cpp guarded entirely.
- **CMake target conditional sources** — the existing `out_of_process_plugin_host_win32.cpp` is added to the `ajazz_plugins` target conditionally (look for `if(WIN32)` block in `src/plugins/CMakeLists.txt`). `Win32EnvBlock` joins the same conditional.
- **Test file naming** — `test_<unit_under_test>.cpp` for unit tests; `test_<integration_scope>.cpp` for integration tests.

### Integration Points

- **`Win32EnvBlock` instance scope** — declared at the top of the spawn function, lifetime extends through both `CreateProcessW` and `CreateProcessAsUserW` calls. One instance per spawn (the overrides are spawn-local).
- **Both branches' `lpEnvironment` argument** — `CreateProcessAsUserW` line 549 currently passes `nullptr`; `CreateProcessW` line 561 currently passes `nullptr`. Both flip to `envBlock.data()`.
- **`creationFlags |= CREATE_UNICODE_ENVIRONMENT`** added near line 487 alongside `CREATE_NO_WINDOW`.

\</code_context>

<specifics>
## Specific Ideas / Anchor Artefacts

- **Per-decision artefact files under `.planning/phases/06-cr-01-win32-oop-env-pollution-fix/`:**
  - `06-PLAN.md` — gsd-planner output. Single-task plan (the fix is small and atomic).
  - `06-CR-01-RESOLUTION.md` (or similar) — narrative documenting the duplicate-key precedence answer (WIN32-04) once the integration test reveals it.
  - `06-SUMMARY.md` — gsd-executor output.
- **Atomic commit boundaries:** ONE commit for the fix (env block + remove \_putenv_s + wire both spawn branches + CREATE_UNICODE_ENVIRONMENT flag). SECOND commit for the unit + integration tests + CMakeLists updates. THIRD commit for the WIN32-04 documentation answer once observed in CI.
- **Verification anchor:** the CR-01 regression test (parent's PYTHONPATH unchanged after spawn) is the primary verification. Must fail on the pre-Phase-6 code, pass on the post-Phase-6 code.

</specifics>

<deferred>
## Deferred Ideas

- **`std::atomic<int>` ctor counter** — alternative path 2 from FIX-DEFERRED. Defense-in-depth that fails loudly if a second `OutOfProcessPluginHost` is constructed while a first is alive. Not needed because path 1 (per-spawn env block) closes the race directly. Reconsider if Win32 review wants belt-and-braces or if the test surface ever surfaces a related concern.
- **`manifest_signer_win32.cpp` parent-env audit** — the verifier subprocess inherits the parent env. Phase 6 fixes the OOP host's pollution; the verifier was reading the *polluted* env from the parent, but if the parent stays clean, the verifier sees clean env too. No further verifier work needed unless the verifier itself mutates env (it doesn't, per the v1.0 review).
- **Per-spawn env block applied uniformly across other Win32 child-spawning sites** — if the codebase grows other `CreateProcessW` callers (e.g. a future Win32 helper tool), they should adopt the same `Win32EnvBlock` helper. Out of scope for v1.1; tracked as a "while you're in there" pattern.
- **AppVerifier / Windows debug heap integration** — Pitfall 5 lists AppVerifier as a way to catch malformed env blocks at runtime. Could be added to the Windows CI matrix as a separate, slower job. Defer to v1.2 unless v1.1 tests ever flake on this surface.

</deferred>

______________________________________________________________________

*Phase: 06-cr-01-win32-oop-env-pollution-fix*
*Context gathered: 2026-05-14*
