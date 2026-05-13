# Stack Research — v1.1 additions

**Milestone:** v1.1 Device lifecycle hardening + scaffolding-to-functional
**Researched:** 2026-05-13
**Confidence:** HIGH (versions verified via upstream releases; recipes verified against existing source)

## Scope of this document

This is a **delta** stack research. The validated v1.0 stack (C++20, Qt 6.7+, QML 6, hidapi 0.14.0, Python 3.11+, Catch2 v3.7.1, CMake + Ninja, pre-commit + GHA CI) is unchanged and is not re-litigated here. Only **new or changed** stack elements driven by v1.1's five target features are analysed.

The five v1.1 features map to four orthogonal stack questions:

| Feature                                 | Stack question                                                    |
| --------------------------------------- | ----------------------------------------------------------------- |
| Hot-plug hardening (multi-device tests) | Mock/fake HID enumerator + Catch2 multi-device fixture            |
| Time-sync scaffolding                   | None — pure C++ + Qt + Catch2 (already in stack)                  |
| Scaffolded-device wiring                | None — same hidapi backend pattern as existing functional devices |
| CR-01 (Win32 OOP host env pollution)    | Win32 environment-block construction technique (no library)       |
| WR-01 (`loadTrustRoots` parser)         | JSON library choice vs. custom scanner                            |

## Recommended additions

### 1. JSON library for `loadTrustRoots` — **nlohmann::json 3.12.0**

**Decision:** Add `nlohmann::json` v3.12.0 as a **plugins-library-private** dependency, vendored via FetchContent. **Do not** add it to `ajazz_core`. **Do not** propagate it into the public include tree.

|                           | Recommendation                                                                                                  |
| ------------------------- | --------------------------------------------------------------------------------------------------------------- |
| **Library**               | `nlohmann::json`                                                                                                |
| **Version**               | **3.12.0** (released 2025-04-11; backward-compatible with 3.11.x)                                               |
| **License**               | MIT (compatible with GPL-3.0-or-later)                                                                          |
| **Distribution**          | Single-header `json.hpp` (~22k LOC); also available split via FetchContent                                      |
| **Integration point**     | `src/plugins/CMakeLists.txt` only — `target_link_libraries(ajazz_plugins PRIVATE nlohmann_json::nlohmann_json)` |
| **Public-header leakage** | None — only used in `src/plugins/src/manifest_signer.cpp` and `manifest_signer_win32.cpp`, both `.cpp` TUs      |

**Why nlohmann::json and not alternatives** (architectural decision matrix from `.planning/PROJECT.md` v1.1 brief — one of `nlohmann::json` / custom scanner / accept COD-031 break):

| Option                                                              | Verdict    | Reason                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| ------------------------------------------------------------------- | ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **nlohmann::json 3.12.0**                                           | **CHOSEN** | Mature (10+ years), MIT, header-only via FetchContent (mirrors how hidapi is wired in `CMakeLists.txt:62-78`), no Qt entanglement, zero impact on `ajazz_core`. Used as PRIVATE dep of `ajazz_plugins` only — preserves the COD-031 constraint (`ajazz_core` stays Qt-free + dep-light). The header-only model costs ~1-2 sec extra compile per TU that includes `<nlohmann/json.hpp>`; with two `.cpp` TUs this is negligible.                                                                                 |
| simdjson 4.6.4                                                      | Rejected   | Apache-2.0 (compatible) but designed for high-throughput parsing of multi-MB documents. The `trust_roots.json` files in scope are sub-1 KB. simdjson's on-demand API also requires a `padded_string` allocation discipline that's a poor fit for a tiny config file. Optimising for a non-bottleneck.                                                                                                                                                                                                           |
| RapidJSON (last tag 2025-02-26)                                     | Rejected   | MIT, header-only, very fast — but **no signed release tags since 2025-02-26** and the project has been in maintenance-only mode for years. Adding a dep with stalled upstream is a future-tech-debt vector. nlohmann::json has active monthly commits in upstream.                                                                                                                                                                                                                                              |
| RapidYAML                                                           | Rejected   | YAML is a strict superset of JSON syntactically, but pulling in a YAML parser for JSON is gratuitous scope creep and confuses future maintainers about the wire format.                                                                                                                                                                                                                                                                                                                                         |
| Custom 80-LoC scanner                                               | Rejected   | Reviewed the existing `wire::findStringField` mini-parser in `src/plugins/src/wire_protocol.hpp:182` — it took two CVE-class corner-case patches (WR-01 already at partial fix in `1fbb46b`) to handle key-before-name ordering. A purpose-built scanner that handles RFC 8259 escape sequences, nested objects, BOM stripping, and UTF-16 surrogate pairs correctly is closer to 400 LoC and re-introduces the same risk class that put `loadTrustRoots` on the audit list to begin with. Don't reinvent JSON. |
| Accept COD-031 break (link nlohmann::json into `ajazz_core` PUBLIC) | Rejected   | COD-031 specifically tracks **Qt** leakage; nlohmann::json wouldn't cross that constraint *per se*. But the `loadTrustRoots` symbol lives in `ajazz_plugins`, not `ajazz_core`, so this trade-off is a non-sequitur — keep the dep PRIVATE to plugins and the question never arises.                                                                                                                                                                                                                            |

**Critical insight (preserving the COD-031 spirit):** The wire-protocol parser in `src/plugins/src/wire_protocol.hpp` was deliberately kept dep-free because `ajazz_plugins` ships into Python child processes and needs a minimal surface (slice-3e cleanup history). The `loadTrustRoots` parser lives in the SAME library but only runs in the host process at startup. Splitting `loadTrustRoots` into its own private TU that uses `nlohmann::json` does NOT pollute the child-process wire path — the IPC parser in `wire_protocol.hpp` stays untouched. State this explicitly in the implementation commit so the next audit doesn't unwind it.

**CMake integration sketch (for the Requirements stage to confirm):**

```cmake
# src/plugins/CMakeLists.txt — add near the top of the target block
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

target_link_libraries(ajazz_plugins PRIVATE nlohmann_json::nlohmann_json)
```

The `JSON_BuildTests OFF` + `JSON_Install OFF` pair prevents nlohmann's own CMake from registering 600+ test targets in our build and from cluttering our install manifest. Confidence: HIGH (verified against [nlohmann/json README integration section](https://json.nlohmann.me/integration/)).

### 2. Mock HID enumerator — **In-tree, hand-rolled, no new library**

**Decision:** Implement a `MockHidEnumerator` test double inside `tests/integration/` and an enumeration-function injection seam in `src/core/include/ajazz/core/device_registry.hpp`. **Do not** add a third-party HID mocking library.

|                             | Recommendation                                                                                                                                                                                                                                                              |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Library**                 | None — in-tree fake                                                                                                                                                                                                                                                         |
| **Seam location**           | `DeviceRegistry::enumerateConnectedHidKeys()` currently calls `::hid_enumerate(0, 0)` directly (`src/core/src/device_registry.cpp:77`). Add a constructor-injectable `std::function<std::set<HidKey>()>` enumerator; default it to the real `hid_enumerate`-based callable. |
| **Fixture file**            | `tests/integration/mock_hid_enumerator.hpp`                                                                                                                                                                                                                                 |
| **Integration with Catch2** | Plain header — no `catch_discover_tests` changes                                                                                                                                                                                                                            |

**Why no third-party library:**

| Option                                       | Verdict                     | Reason                                                                                                                                                                                                                                                                                                                                               |
| -------------------------------------------- | --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **In-tree fake**                             | **CHOSEN**                  | The existing test suite already uses this pattern (`FakeAsyncExecutor` in `tests/unit/test_action_engine.cpp:119`). The hidapi surface that `DeviceRegistry` actually depends on is one function (`hid_enumerate`) returning one list — a function-pointer/`std::function` seam is ~20 LoC and zero dep cost.                                        |
| `hidapi-mock` (no canonical upstream)        | Rejected                    | Surveyed the hidapi ecosystem; there is no widely-used mock library. Bespoke testing wrappers exist in node-hid / Python hid-test but none target C++. Writing one in-tree is strictly cheaper than vetting + vendoring a third-party project of unknown quality.                                                                                    |
| Trompeloeil v49 (header-only, Boost license) | Rejected (for this feature) | Trompeloeil is a generic mocking framework, not HID-specific. For one C function with one signature, framework setup (REQUIRE_CALL macros, sequence specs, etc.) costs more LoC than the hand-rolled fake. See "Considered for the future" below — Trompeloeil may be worth adding LATER when multi-method interface mocking becomes the bottleneck. |
| Mock via `LD_PRELOAD` / linker `--wrap=`     | Rejected                    | Works on Linux but not on macOS (no `--wrap` in lld linking through xcode-stable) and not on Windows. Hot-plug tests must run on all three OSes per project cross-platform constraint.                                                                                                                                                               |

**Required source change** (`device_registry.hpp` + `.cpp`):

```cpp
// Add: a typedef + constructor parameter for enumeration injection.
class DeviceRegistry {
public:
    using HidKey = std::pair<std::uint16_t, std::uint16_t>;
    using HidEnumerator = std::function<std::set<HidKey>()>;

    /// Defaults to real ::hid_enumerate-backed enumeration.
    explicit DeviceRegistry(HidEnumerator enumerator = {});
    // …existing API…
};
```

The default constructor parameter `{}` falls through to the existing `hid_enumerate`-based implementation, so this is a non-breaking ABI change — all current call sites compile unchanged. Tests inject a callable returning a hard-coded `std::set<HidKey>`.

**Multi-device test fixture design** (this slice's first use of the seam):

```cpp
// tests/integration/mock_hid_enumerator.hpp
namespace ajazz::tests {
class MockHidEnumerator {
public:
    void plug(std::uint16_t vid, std::uint16_t pid) { m_present.emplace(vid, pid); }
    void unplug(std::uint16_t vid, std::uint16_t pid) { m_present.erase({vid, pid}); }
    auto operator()() const { return m_present; }   // matches HidEnumerator
private:
    std::set<std::pair<std::uint16_t, std::uint16_t>> m_present;
};
}
```

Hot-plug integration tests drive `plug()` / `unplug()` then poke `DeviceRegistry::enumerateConnectedHidKeys()` and `HotplugMonitor`'s test hooks (the monitor will need a parallel injection seam — flagged below).

### 3. Hot-plug monitor test seam — **Inject `HotplugMonitor::Callback` directly**

**Decision:** No new library. Use the **already-existing** public API `HotplugMonitor::setCallback()` (`src/core/include/ajazz/core/hotplug_monitor.hpp:78`) plus a new test-only `injectEvent()` method behind `#ifdef AJAZZ_TESTING`.

|                                  | Recommendation                                                                                                                                                                                                                                                                                        |
| -------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Library**                      | None                                                                                                                                                                                                                                                                                                  |
| **Approach**                     | Add `void injectEvent(HotplugEvent const&)` to `HotplugMonitor`, gated by `AJAZZ_TESTING` define set by `tests/CMakeLists.txt`                                                                                                                                                                        |
| **Why not a fake monitor class** | The class is already designed for testability — its constructor doc states "Safe to construct with no listener; the monitor simply silences the events. This keeps the API testable on CI runners without USB." Adding `injectEvent` completes the testability story without a second parallel class. |

The CI runners on GHA have no USB hot-plug events, so the monitor's start() is already a no-op there. `injectEvent` lets tests dispatch synthetic Arrived/Removed events through the real callback dispatch path, exercising application-layer onHotplug routing for free.

### 4. Win32 environment block (CR-01) — **Raw UTF-16 buffer, no library**

**Decision:** Replace the three `_putenv_s` calls in `src/plugins/src/out_of_process_plugin_host_win32.cpp:464-467` with a **per-spawn UTF-16 environment block** built from `GetEnvironmentStringsW()`, then pass it to `CreateProcessW` with the `CREATE_UNICODE_ENVIRONMENT` flag. No new library is needed.

|                 | Recommendation                                                                               |
| --------------- | -------------------------------------------------------------------------------------------- |
| **Library**     | None — pure Win32 API (`processthreadsapi.h`, `winbase.h`)                                   |
| **APIs used**   | `GetEnvironmentStringsW`, `FreeEnvironmentStringsW`, `CreateProcessW` with \`dwCreationFlags |
| **Buffer type** | `std::vector<wchar_t>` (RAII, automatic cleanup, no manual `LocalFree`)                      |

**Block layout (verified against [Microsoft Learn — CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw) and Chris Wellons' [Win32 environment blocks](https://nullprogram.com/blog/2023/08/23/)):**

```
NAME1=VALUE1\0NAME2=VALUE2\0...\0NAMEn=VALUEEn\0\0
```

In UTF-16: each `\0` is two zero bytes; the terminating double-null is **four** zero bytes (one wide-char NUL for the last string + one wide-char NUL for the block). An empty environment is still one empty entry, i.e. four zero bytes total.

**Pitfalls flagged by the research, MUST be addressed in implementation:**

1. **`=VAR=…` drive-letter entries** — Windows uses entries beginning with `=` to track per-drive current directories (`=C:=C:\Users\…`). These ARE valid entries that must be preserved in the spawned child's block; the naive "skip entries starting with `=`" filter will break working directory inheritance. Verified in nullprogram blog above.
1. **Sort order** — Windows uses an undocumented UCS-2-era case-folded sort. nullprogram's recipe: don't try to sort. Append our three new vars (`PYTHONPATH`, `PYTHONDONTWRITEBYTECODE`, `PYTHONUNBUFFERED`) at the end of the inherited block — Windows internally re-sorts when the child consumes the block.
1. **`CREATE_UNICODE_ENVIRONMENT` flag is mandatory** — Without it, `CreateProcessW` interprets `lpEnvironment` as an **ANSI** block (single-byte chars, single-NUL separators), which would mangle every Unicode `PATH` component. Source: Microsoft Learn doc above.
1. **Duplicate-key collapsing** — If `PYTHONPATH` is already set in the parent's env, the inherited copy will win unless we filter the parent's entry first or place the override LAST and rely on Windows' last-wins semantics. Microsoft doc says duplicates are forced to match the first occurrence; nullprogram doc says first wins on lookup. **Test in CI to confirm** — write `PYTHONPATH=parent` in CI, override with `PYTHONPATH=child`, assert the child sees `child`. If first-wins, the filter is mandatory.

**Recipe sketch:**

```cpp
// Pseudo-code; details to be elaborated in implementation plan.
std::vector<wchar_t> buildChildEnvBlock(
    std::initializer_list<std::pair<std::wstring, std::wstring>> overrides) {
    LPWCH parentBlock = GetEnvironmentStringsW();
    // Walk parentBlock, copying entries whose names are NOT in `overrides`.
    // (Use _wcsnicmp for case-insensitive compare — env vars on Win32 are
    // case-insensitive.)
    std::vector<wchar_t> child;
    for (LPWCH p = parentBlock; *p; ) {
        std::wstring_view entry{p};
        auto eq = entry.find(L'=');
        std::wstring_view name = (eq == 0) ? L"" : entry.substr(0, eq);
        // …skip if name matches any override key…
        child.insert(child.end(), entry.begin(), entry.end());
        child.push_back(L'\0');
        p += entry.size() + 1;
    }
    FreeEnvironmentStringsW(parentBlock);
    // Append overrides.
    for (auto const& [name, value] : overrides) {
        auto write = [&](std::wstring_view s) {
            child.insert(child.end(), s.begin(), s.end());
        };
        write(name);
        child.push_back(L'=');
        write(value);
        child.push_back(L'\0');
    }
    child.push_back(L'\0'); // block terminator
    return child;
}
```

**Why not a library:** Win32 env-block construction is ~40 LoC of careful pointer arithmetic. Vetted reference (nullprogram blog) confirms there's no idiomatic C++ wrapper anyone trusts; everyone writes this by hand. The CRT helpers (`_putenv_s`, `_wputenv_s`) are the bug we're FIXING — they mutate the parent process. Boost.Process abstracts environment construction but pulling in Boost for one function is gross overkill.

### 5. Considered-and-deferred additions (DO NOT add in v1.1)

| Tool                                                    | Reason to defer                                                                                                                                                                                                                                |
| ------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Trompeloeil v49** (header-only, Boost license, C++14) | Worth adding when 2+ multi-method interfaces need mocking. v1.1 needs **one** function-pointer fake (HID enumerator) — overkill today. Reassess at v1.2 when `IClockCapable` backends start needing real wire-format mocks for protocol tests. |
| **FakeIt / PowerFake**                                  | Same as Trompeloeil; no current driver.                                                                                                                                                                                                        |
| **clang-tidy** as enforced pre-commit hook (COD-027)    | Tech-debt item already in `_issues.json`. Out of v1.1 scope per milestone brief.                                                                                                                                                               |
| **cppcheck / iwyu / trivy / CycloneDX SBOM** (COD-028)  | Same — tracked but deferred.                                                                                                                                                                                                                   |
| Any HTTP client                                         | The project intentionally has no HTTP client (catalog fetchers use `QNetworkAccessManager` already in `Qt6::Network`). Do not add a second one.                                                                                                |
| `Boost.Process`                                         | Avoids one Win32 env-block question but pulls in Boost. Cost > benefit by an order of magnitude.                                                                                                                                               |
| `QJsonDocument` for `loadTrustRoots`                    | Crosses the COD-031 line — `ajazz_plugins` is intentionally Qt-free. Same reasoning that ruled out QJsonDocument in `wire_protocol.hpp` rules it out here.                                                                                     |

## Recommended Stack (v1.1 delta only)

### New library additions

| Library          | Version    | Purpose                                                                                        | Integration scope                                                                                                                                     |
| ---------------- | ---------- | ---------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| `nlohmann::json` | **3.12.0** | RFC-8259 JSON parser for `loadTrustRoots` (`manifest_signer.cpp`, `manifest_signer_win32.cpp`) | PRIVATE link to `ajazz_plugins` only; FetchContent declared in `src/plugins/CMakeLists.txt`; never appears in any header under `src/plugins/include/` |

### New in-tree test infrastructure (no third-party library)

| Component                                      | Location                                                                          | Purpose                                                                                      |
| ---------------------------------------------- | --------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| `DeviceRegistry::HidEnumerator` injection seam | `src/core/include/ajazz/core/device_registry.hpp` + `device_registry.cpp`         | Constructor-injectable `std::function<std::set<HidKey>()>`; defaults to real `hid_enumerate` |
| `HotplugMonitor::injectEvent`                  | `src/core/include/ajazz/core/hotplug_monitor.hpp` (behind `#ifdef AJAZZ_TESTING`) | Synthetic Arrived/Removed dispatch for tests                                                 |
| `MockHidEnumerator`                            | `tests/integration/mock_hid_enumerator.hpp`                                       | Stateful `plug()` / `unplug()` driver, satisfies `HidEnumerator` callable concept            |
| Multi-device baseline fixture                  | `tests/integration/test_hotplug_lifecycle.cpp`                                    | Catch2 `TEST_CASE` exercising 2-device-online → unplug one → assert state                    |

### New OS-API-only addition

| Surface                                               | Why no library                                                                                                                                                  |
| ----------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Win32 UTF-16 env block + `CREATE_UNICODE_ENVIRONMENT` | Pure `processthreadsapi.h` calls; documented recipe; no idiomatic C++ wrapper worth adopting. ~40 LoC self-contained in `out_of_process_plugin_host_win32.cpp`. |

### Unchanged from v1.0 (do NOT re-add, do NOT swap)

| Library                                                 | Version                                                                                           | Status                                                  |
| ------------------------------------------------------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------------- |
| Qt 6 (Core, Widgets, Qml, Quick, WebEngine, WebChannel) | 6.7+                                                                                              | Pinned by `find_package(Qt6 …)` in root CMake           |
| QML 6                                                   | bundled with Qt 6                                                                                 | unchanged                                               |
| `hidapi`                                                | 0.14.0 (tag `73d292a8`)                                                                           | Vendored via FetchContent at `CMakeLists.txt:62-78`     |
| `Catch2`                                                | v3.7.1                                                                                            | Vendored via FetchContent at `tests/CMakeLists.txt:2-5` |
| Python                                                  | 3.11+                                                                                             | Plugin host child runtime; no host-side embedding       |
| CMake                                                   | 3.x (project requires ≥ 3.21 from root CMakeLists; hidapi needs 3.5+ workaround already in place) | unchanged                                               |

## Alternatives Considered (summary table)

| Need                             | Recommended                      | Alternative                | When alternative might win                                                                                                                                                                    |
| -------------------------------- | -------------------------------- | -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| JSON parser for `loadTrustRoots` | nlohmann::json 3.12.0            | simdjson 4.6.4             | Would win if trust_roots.json grew to multi-MB and parse latency mattered. Today: ~1 KB, no contest.                                                                                          |
| JSON parser for `loadTrustRoots` | nlohmann::json 3.12.0            | RapidJSON                  | Would win if compile time vs runtime perf trade favoured RapidJSON's lower compile cost — but RapidJSON upstream is stalled (no signed releases in ~14 months as of 2026-05). Risk > benefit. |
| JSON parser for `loadTrustRoots` | nlohmann::json 3.12.0            | Hand-rolled 80-LoC scanner | Would win in a single-developer hobby project. The WR-01 history (multiple corner-case fixes) shows this is exactly the wrong choice for a security-adjacent path (signature trust).          |
| HID enumerator mock              | In-tree fake                     | Trompeloeil v49            | Will win at v1.2+ when multi-method interface mocking (`IClockCapable` wire-format mocks, multi-call sequences) becomes a real driver.                                                        |
| Win32 env block                  | Raw API + `std::vector<wchar_t>` | Boost.Process              | Would win if we were already on Boost; we're not, and adding Boost for one function is wildly disproportionate.                                                                               |

## What NOT to use

| Avoid                                                                        | Why                                                                                                                                                                                                                                                                                                          | Use instead                                                                       |
| ---------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------- |
| `_putenv_s` / `_wputenv_s` for child-process env setup                       | **This is CR-01 itself.** Mutates the calling (parent) process's environment, leaking PYTHONPATH/PYTHONDONTWRITEBYTECODE/PYTHONUNBUFFERED into every subsequent QProcess call and every plugin spawn after the first. Per-spawn correctness violated.                                                        | Per-spawn UTF-16 block + `CREATE_UNICODE_ENVIRONMENT` (this STACK.md §4)          |
| `QJsonDocument` in `ajazz_plugins`                                           | Crosses the deliberate "plugins library is Qt-free" line that `wire_protocol.hpp` already documents. Same constraint that makes `wire_protocol.hpp` reject `nlohmann::json` for the IPC path applies here — except `loadTrustRoots` is a host-only path so a non-Qt JSON dep IS acceptable; a Qt one is not. | nlohmann::json 3.12.0 (this STACK.md §1)                                          |
| Generic mocking framework (Trompeloeil, FakeIt) added pre-emptively for v1.1 | Adds maintenance + CMake surface for a single, simple use case. YAGNI applies — the in-tree fake is 20 LoC.                                                                                                                                                                                                  | In-tree fake mirroring `FakeAsyncExecutor` (`test_action_engine.cpp:119`) pattern |
| Custom JSON scanner in `manifest_signer.cpp`                                 | Already burned twice (WR-01 partial-fix history). The next corner case (BOM, surrogate pair, embedded NUL) will be the third bug.                                                                                                                                                                            | nlohmann::json 3.12.0                                                             |
| New HTTP client                                                              | `QNetworkAccessManager` already in use for catalog fetchers (`streamdock_catalog_fetcher.cpp`, `opendeck_catalog_fetcher.cpp`).                                                                                                                                                                              | Reuse existing Qt Network                                                         |
| `LD_PRELOAD` / linker `--wrap=hid_enumerate` for tests                       | Linux-only; project must test on Win + macOS too                                                                                                                                                                                                                                                             | Constructor-injectable `HidEnumerator` callable (this STACK.md §2)                |

## CMake / pre-commit integration impact

| Change                                                                                                            | File                                                               | Risk                                                                                                                                                                                                                                                                                               |
| ----------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `FetchContent_Declare(nlohmann_json ...)` block                                                                   | `src/plugins/CMakeLists.txt`                                       | LOW. Mirrors the established hidapi recipe in root `CMakeLists.txt:62-78`.                                                                                                                                                                                                                         |
| `target_link_libraries(ajazz_plugins PRIVATE nlohmann_json::nlohmann_json)`                                       | `src/plugins/CMakeLists.txt`                                       | LOW. PRIVATE keyword ensures no leakage to `ajazz_core` or downstream tests.                                                                                                                                                                                                                       |
| Per-OS package config: drop `nlohmann-json-dev` from `.deb` build-deps if FetchContent stays the canonical source | `CMakeLists.txt` packaging block                                   | LOW — but **confirm** with `dpkg-buildpackage` smoke test in CI; the existing build is already vendor-only for hidapi, so the pattern is established.                                                                                                                                              |
| New test-only `injectEvent()` on `HotplugMonitor`                                                                 | `src/core/include/ajazz/core/hotplug_monitor.hpp`                  | MEDIUM. Behind `#ifdef AJAZZ_TESTING` to keep the production binary surface clean. Define `AJAZZ_TESTING` in `tests/CMakeLists.txt`, NOT globally.                                                                                                                                                 |
| `DeviceRegistry` constructor-arg addition                                                                         | `src/core/include/ajazz/core/device_registry.hpp` + `.cpp`         | LOW. Default-valued parameter; ABI-compatible. All current call sites compile without change.                                                                                                                                                                                                      |
| `loadTrustRoots` rewrite                                                                                          | `src/plugins/src/manifest_signer.cpp`, `manifest_signer_win32.cpp` | MEDIUM. Security-adjacent code. Existing Catch2 unit test (`test_manifest_signer.cpp`) provides regression baseline — keep all existing cases green, add cases for: BOM-prefixed input, escape sequences in publisher names, deeply nested malformed JSON, embedded NUL.                           |
| Win32 env block recipe                                                                                            | `src/plugins/src/out_of_process_plugin_host_win32.cpp`             | HIGH (without proper test). The fix can't be validated on Linux/macOS CI — Windows-specific. Needs a Windows runner case in `test_out_of_process_plugin_host.cpp` (gated by `#ifdef _WIN32`) that spawns a child and verifies the parent's `_wgetenv(L"PYTHONPATH")` is unchanged after the spawn. |

## Version Compatibility

| Pairing                                                                              | Status     | Notes                                                                                                                                             |
| ------------------------------------------------------------------------------------ | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| nlohmann::json 3.12.0 + C++20                                                        | Compatible | Library targets C++11 minimum; works through C++23                                                                                                |
| nlohmann::json 3.12.0 + CMake FetchContent                                           | Compatible | Native CMake support; `JSON_BuildTests OFF` + `JSON_Install OFF` recommended                                                                      |
| nlohmann::json 3.12.0 + GPL-3.0-or-later project                                     | Compatible | MIT → GPL-3.0 absorption is standard, no notice friction                                                                                          |
| nlohmann::json 3.12.0 + ASan/UBSan (project uses sanitizers per `ajazz::sanitizers`) | Compatible | No known sanitizer issues at this version                                                                                                         |
| `CREATE_UNICODE_ENVIRONMENT` + Windows 10/11                                         | Compatible | Documented since Windows NT 4; behaviour stable                                                                                                   |
| `CREATE_UNICODE_ENVIRONMENT` + Wine (dev env Win cross-builds)                       | Verify     | Wine implements the flag, but `=`-prefixed drive-letter entries are handled differently. Test on real Windows in CI before declaring CR-01 fixed. |
| `MockHidEnumerator` fake + existing `FakeAsyncExecutor` style                        | Compatible | Same pattern; no conflict                                                                                                                         |
| Catch2 v3.7.1 + `#ifdef AJAZZ_TESTING` gating in `HotplugMonitor`                    | Compatible | Catch2 doesn't define `AJAZZ_TESTING`; the project sets it explicitly in `tests/CMakeLists.txt`                                                   |

## Confidence assessment

| Area                           | Confidence      | Reason                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| ------------------------------ | --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| nlohmann::json 3.12.0 choice   | **HIGH**        | Version verified against [nlohmann/json releases](https://github.com/nlohmann/json/releases) (3.12.0 released 2025-04-11). License (MIT), header-only status, and CMake FetchContent recipe verified against [official integration docs](https://json.nlohmann.me/integration/). Trade-off rationale verified against the in-repo COD-031 reasoning at `.planning/PROJECT.md:29` and the matching `wire_protocol.hpp:22-31` rationale. |
| In-tree mock HID enumerator    | **HIGH**        | Verified by direct reading of `src/core/src/device_registry.cpp:71-85` (single `hid_enumerate` call site, trivially injectable). Existing fake-class precedent (`test_action_engine.cpp:119`) verified.                                                                                                                                                                                                                                |
| Win32 env block recipe         | **MEDIUM-HIGH** | Recipe details verified against [Microsoft Learn CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw) and [nullprogram Win32 env-block deep dive](https://nullprogram.com/blog/2023/08/23/). Duplicate-key precedence is one open question (first-wins vs last-wins) that needs a CI smoke test — flagged in §4.                                                 |
| Trompeloeil / FakeIt deferral  | **HIGH**        | Survey of `tests/` shows zero existing third-party mocking. Adding one for a single test case is unjustified.                                                                                                                                                                                                                                                                                                                          |
| simdjson / RapidJSON rejection | **HIGH**        | simdjson use-case mismatch is structural (designed for >1 MB streaming); RapidJSON upstream-stall status verified against [Tencent/rapidjson releases](https://github.com/Tencent/rapidjson/releases).                                                                                                                                                                                                                                 |

## Sources

- [nlohmann/json releases](https://github.com/nlohmann/json/releases) — v3.12.0 release date, version verification (HIGH)
- [JSON for Modern C++ integration guide](https://json.nlohmann.me/integration/) — CMake `FetchContent` + `find_package` recipes (HIGH)
- [JSON for Modern C++ license page](https://json.nlohmann.me/home/license/) — MIT license confirmation (HIGH)
- [simdjson releases](https://github.com/simdjson/simdjson/releases) — v4.6.4 (2025-05-06) verification; use-case fit assessment (HIGH)
- [Tencent/rapidjson releases](https://github.com/Tencent/rapidjson/releases) — upstream maintenance-stall status (HIGH)
- [rollbear/trompeloeil releases](https://github.com/rollbear/trompeloeil/releases) — v49 + Boost Software License + Catch2 integration adapter at `<catch2/trompeloeil.hpp>` (HIGH)
- [Microsoft Learn: CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw) — `CREATE_UNICODE_ENVIRONMENT` flag requirement, `lpEnvironment` block format (HIGH)
- [nullprogram: Everything you never wanted to know about Win32 environment blocks](https://nullprogram.com/blog/2023/08/23/) — `=`-prefixed drive-letter entries, sort-order undocumented quirks, duplicate-key handling (MEDIUM — third-party blog, but matches MS docs and is authored by a respected systems engineer with corroborating code samples)
- In-repo verification:
  - `src/plugins/src/manifest_signer.cpp:102-153` — current `loadTrustRoots` parser to be replaced
  - `src/plugins/src/wire_protocol.hpp:22-31` — COD-031 / Qt-free rationale for plugins library
  - `src/plugins/src/out_of_process_plugin_host_win32.cpp:464-467` — the offending `_putenv_s` calls (CR-01)
  - `src/core/src/device_registry.cpp:71-85` — the `hid_enumerate` call site for the injection seam
  - `src/core/include/ajazz/core/hotplug_monitor.hpp:65-110` — existing testability hooks
  - `tests/unit/test_action_engine.cpp:119` — `FakeAsyncExecutor` precedent for in-tree fakes
  - `CMakeLists.txt:62-78` — established hidapi FetchContent pattern to mirror

______________________________________________________________________

*Stack research for: AJAZZ Control Center v1.1 milestone (delta only)*
*Researched: 2026-05-13*
