# Coding Conventions

**Analysis Date:** 2026-06-02

## Naming Patterns

**Files:**

- C++ headers: `.hpp` (no `.h`); implementations: `.cpp`
- Source organization: `src/{module}/include/ajazz/{module}/` for headers, `src/{module}/src/` for implementations
- Test files: `tests/{category}/test_{component}.cpp` (e.g., `tests/unit/test_event_bus.cpp`)
- Python modules: `python/ajazz_plugins/` with standard package structure; test files under `python/ajazz_plugins/tests/`

**Functions:**

- `camelCase` for local functions and methods: `openDevice()`, `setActiveDpiStage()`, `parseInputReport()`
- Factory functions: `make{Type}` pattern: `makeAjSeriesWithTransport()`, `makeSidecarStreamDock()`
- Query methods: no prefix, return type in name: `currentPageId()`, `firmwareVersion()`, `isOpen()`
- Logging macros: `AJAZZ_LOG_{LEVEL}(module, "message", args)` — `AJAZZ_LOG_INFO`, `AJAZZ_LOG_WARN`, `AJAZZ_LOG_ERROR`

**Variables:**

- Member variables: `m_{name}` prefix: `m_handlers`, `m_open`, `m_nextToken`
- Local variables and parameters: `camelCase`: `transport`, `descriptor`, `keyIndex`
- Constants: `camelCase` or `kConstantName` (both used): `kSubscribers`, `kPublishers`, `kEventsPerPublisher`
- Scoped enums (preferred): `enum class` with `PascalCase` values: `enum class Kind { KeyDown, KeyUp, MouseDown }`

**Types:**

- Classes and interfaces: `PascalCase`: `EventBus`, `IDevice`, `MockTransport`
- Interface classes: `I{Name}` prefix: `IDevice`, `ITransport`, `IDisplayCapable`
- Exception types: `{Name}Error`: `ProfileIoError`, `ProfileBundleError`
- Struct/data types: `PascalCase`: `DeviceDescriptor`, `DeviceEvent`, `TransportStats`
- Aliases (type definitions): `camelCase` or `PascalCase` (both used): `using ActionChain = std::vector<Action>`

## Code Style

**Formatting:**

- Tool: `clang-format` (`.clang-format` at repo root)
- Standard: C++20
- Column limit: 100
- Indentation: 4 spaces, never tabs
- Brace style: Attach (opening brace on same line as function/class)
- Namespace indentation: None (blank line before closing brace)
- Pointer/reference alignment: Left (e.g., `Type* ptr` not `Type *ptr`)

**Key settings from `.clang-format`:**

```yaml
BasedOnStyle: LLVM
Standard: c++20
ColumnLimit: 100
IndentWidth: 4
BreakBeforeBraces: Attach
PointerAlignment: Left
ReferenceAlignment: Left
IncludeBlocks: Regroup
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
BinPackParameters: false
BinPackArguments: false
AlwaysBreakTemplateDeclarations: Yes
```

**Linting:**

- Tool: `clang-tidy` (optional, manual/pre-push stage in `.pre-commit-config.yaml`)
- Configuration: `.clang-tidy` at repo root (runs configured checks)
- Pre-commit hooks enforce: trailing whitespace, EOF newlines, mixed line endings, YAML/JSON/TOML validation

**CMake formatting:**

- Tool: `cmake-format` (`.cmake-format.yaml` at repo root)
- Line width: 100
- Indentation: 4 spaces
- Dangle parens: true
- Command case: canonical (uppercase)

## Import Organization

**Order (C++):**

1. SPDX license comment + doxygen file doc
1. Local project headers (quoted): `#include "ajazz/core/event_bus.hpp"`
1. Qt headers (angle brackets): `#include <Qt...>` (grouped)
1. Standard library headers (angle brackets, grouped):
   - `<algorithm>`, `<chrono>`, `<cstdint>`, `<functional>`, `<memory>`, `<mutex>`, `<string>`, `<vector>`, etc.
1. System headers (angle brackets): `<windows.h>`, `<fcntl.h>`, etc. (platform-conditional)

The `clang-format` rule `IncludeBlocks: Regroup` enforces logical grouping and deduplication.

**Qt-specific:**

- `#include <QObject>` before std lib
- `#include <Q...>` before non-Qt standard library
- Example: `#include <QList>` → `#include <algorithm>` → `#include <vector>`

**Python imports:**

```python
from __future__ import annotations

import standard_lib_modules
from standard_lib_modules import names

import third_party_packages

from ajazz_plugins import names
from . import relative_imports
```

**Path aliases:**

- Not used; absolute includes are preferred
- Core library exports: `#include "ajazz/core/..."`, `#include "ajazz/mouse/..."`, etc.

## Error Handling

**Exceptions (C++):**

- Thrown on unrecoverable conditions: file I/O failures, invalid JSON, missing resources
- Custom exceptions inherit `std::runtime_error` with additional context if needed
- Examples: `ProfileIoError`, `ProfileBundleError`
- Never throw in destructors or noexcept functions
- Errors logged at throw site via `AJAZZ_LOG_ERROR` / `AJAZZ_LOG_WARN`

**Try-catch blocks:**

```cpp
try {
    auto profile = ajazz::core::readProfileFromDisk(path);
    // use profile
} catch (ajazz::core::ProfileIoError const& e) {
    AJAZZ_LOG_WARN("profile_io", "read failed: {}", e.what());
    // handle gracefully
}
```

**Optional / Result types:**

- Return `std::optional<T>` for operations that might fail but are not exceptional (e.g., parsing)
- Example: `std::optional<DeviceEvent> parseInputReport(std::span<uint8_t> bytes);`
- Call site: `if (auto ev = parseInputReport(data); ev) { /* use *ev */ }`

**Status booleans:**

- Return `bool` for simple success/failure (no value to return)
- Example: `bool open()` returns true if opened successfully

**noexcept guarantees:**

- All logging is `noexcept` (never throws)
- EventBus::unsubscribe is `noexcept`
- Destructors are implicitly `noexcept`
- Document `noexcept` on public functions that offer the guarantee

## Logging

**Framework:** Custom `Logger` class at `src/core/include/ajazz/core/logger.hpp`

- Direct access via macros: `AJAZZ_LOG_{LEVEL}(module_name, message, args)`
- Levels: Trace, Debug, Info (default minimum), Warn, Error, Critical
- Format: `[<timestamp>] [<LEVEL>] [<module>] <message>`

**When to log:**

- **ERROR**: Failures that require user attention (device open failures, malformed input)
- **WARN**: Recoverable issues (vendor flash close failure before retry, cache misses)
- **INFO**: Normal operational events (device connected, profile loaded, feature detected)
- **DEBUG**: Detailed state changes (subscription added, message dispatched)
- **TRACE**: Fine-grained entry/exit tracing; very verbose

**Examples from codebase:**

```cpp
AJAZZ_LOG_WARN("registry", "open transport failed: {}", e.what());
AJAZZ_LOG_INFO("registry", "cache hit for VID={:04x} PID={:04x}", id.vendorId, id.productId);
```

**Thread safety:**

- All logging is thread-safe (default StderrSink uses internal mutex)
- Safe to call from any thread concurrently

**Testing:**

- Tests can install a custom LogSink via `setLogSink()` to capture log output
- Example: verify a warning was logged when a device fails to open

## Comments

**When to comment:**

- Explain non-obvious algorithm choices: "copy-on-write snapshot for lock-free publish"
- Document invariants: "tokens are never zero; fetch_add starts at 1"
- Explain why (not what): "fsync the parent directory (not the file) for durability across power loss"
- Mark test doubles: `/// Test double: queues continuations instead of running them, ...`
- Flag provisional or hardware-unconfirmed findings: "PROVISIONAL" tag in code comments (see CLAUDE.md)

**Doxygen / Javadoc comments:**

- File headers (every `.hpp`): `/** @file name.hpp ... */` or `///` style
- Class headers: `/** @brief Class description ... */`
- Public method headers: `/** ... @param ... @return ... @threadsafe ... @see ... */`
- Example from codebase:

```cpp
/**
 * @brief Register an event handler.
 *
 * @param handler Callable invoked for every subsequent publish() call.
 * @return Opaque token; pass to unsubscribe() to cancel the subscription.
 */
Subscription subscribe(Handler handler);
```

**Inline comments:**

- Use `//` for short inline remarks
- Use `/* ... */` sparingly (reserved for multi-line doctest blocks)
- Keep comments close to the code they explain

## Function Design

**Size:**

- Aim for single-responsibility: each function does one thing
- Typical range: 5–30 lines for core logic; longer only if the algorithm is inherently sequential
- Test-heavy modules (fixtures, test doubles) may exceed this for readability

**Parameters:**

- Pass by const reference for non-POD types: `Profile const& profile`
- Pass by value for POD/small types: `int count`, `uint8_t byte`
- Use `std::span<T>` for array-like ranges (not `const std::vector<T>&`)
- Avoid out-parameters; prefer return values or structured returns (e.g., `std::optional`, tuples via structured bindings)

**Return values:**

- Return by value (rely on NRVO): `std::unique_ptr<IDevice> makeDevice(...)`
- Return by const reference only if the caller doesn't own the lifetime: `DeviceDescriptor const& descriptor()`
- Return `std::optional<T>` for "might not exist" (parse failures, lookups)
- Return `bool` for simple success/failure

**Move semantics:**

- Use `std::move()` when transferring ownership: `std::move(profile)`
- Implement move constructors and assignment for large types
- Use `std::unique_ptr` for exclusive ownership; use `std::shared_ptr` sparingly (prefer unique ownership where possible)

## Module Design

**Exports (public headers in `include/`):**

- Only interface and value types; no implementation details
- Hide internal data structures in `.cpp` files or anonymous namespaces
- Virtual interfaces (e.g., `IDevice`) exported; concrete implementations (e.g., `AjSeriesMouse`) are internal

**Barrel files (index headers):**

- Not used; consumers import specific headers
- Example: `#include "ajazz/core/event_bus.hpp"` not `#include "ajazz/core.hpp"`

**Inline implementations:**

- Fixture headers (test doubles) are header-only: `mock_transport.hpp`, `fake_stream_dock_device.hpp`
- Implementation: define all member functions inline (no `.cpp` translation unit)
- Document design: "Header-only by design (Phase 09 D-04). No `.cpp` translation unit..."

**Namespace structure:**

```cpp
namespace ajazz::core { ... }           // Core library (no Qt/device-specific code)
namespace ajazz::mouse { ... }          // Mouse devices
namespace ajazz::keyboard { ... }       // Keyboard devices
namespace ajazz::streamdeck { ... }     // Stream Deck devices
namespace ajazz::app { ... }            // App layer (Qt, QML)
namespace ajazz::plugins { ... }        // Plugin API / runtime
namespace ajazz::tests { ... }          // Test fixtures and mocks
```

## Hard Rules (from CLAUDE.md)

**COD-031 boundary:**

- No `nlohmann::json` in `src/core/include/` (public headers)
- PRIVATE-linked to `ajazz_plugins` only
- Verified by grep: `grep -rn nlohmann src/core/include/` must return 0
- Violating this is a release-blocker

**Commit conventions:**

- Use Conventional Commits: `feat:`, `fix:`, `chore:`, `docs:`, `test:`, `refactor:`, `perf:`, `build:`, `ci:`, `revert:`
- Optional scope in parentheses: `fix(plugins): ...`, `docs(streamdock): ...`
- Enforced by pre-commit hook (`.pre-commit-config.yaml`, `conventional-pre-commit`)
- Never skip hooks with `--no-verify` unless the hook itself is broken; document the bypass in commit body

**Atomic commits:**

- Each commit is one independently-revertable change
- Don't bundle unrelated fixes; create separate commits

**Test name ASCII-only:**

- TEST_CASE titles must contain ASCII only (no em-dashes `—`, right-arrows `→`)
- Windows CI CMD codepage mangles Unicode; Catch2 filters fail on mangled names
- Use `-` and `->` instead
- Enforced by `check-test-names-ascii` pre-commit hook (Python script)

**Debug-channel verification (mandatory):**

- Every UI/behavioral change must be verified live via the debug-control channel
- Build → launch with `AJAZZ_DEBUG_CONTROL=1` → drive controls via `scripts/ajazz-debug` → screenshot → confirm behavior
- Unit tests + code review are necessary but NOT sufficient
- Every new interactive control must set `objectName:` (for `qml.get/set/invoke/click` accessibility)

**Cross-platform -Werror strictness:**

- Linux GCC + Linux Clang: most permissive
- Apple Clang on macOS: catches `-Wunused-const-variable` (use `[[maybe_unused]]` if intentional)
- MSVC on Windows: C4996 deprecation warnings are hard errors; prefer `_s` variants (`_wdupenv_s`, `sprintf_s`)
- Land changes on all three platforms

**No system-level mutations:**

- Code-only fixes inside the project repo
- Don't write to `/etc/`, `~/.config/niri/`, `~/.config/noctalia/`, `/usr/share/`, etc.
- Even read-only inspection of user dotfiles should be sparing and justified

______________________________________________________________________

*Convention analysis: 2026-06-02*
