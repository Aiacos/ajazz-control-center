# Coding Conventions

**Analysis Date:** 2026-05-22

## Naming Patterns

**Files:**

- C++ header: `snake_case.hpp` (e.g., `pi_bridge.hpp`, `mock_transport.hpp`)
- C++ implementation: `snake_case.cpp`
- Python files: `snake_case.py`
- Test files: `test_<component>.cpp` or `test_<component>.py`
- Mock/fixture files: `mock_<interface>.hpp`, `<component>_fixture.hpp`

**Functions & Methods:**

- Member functions: `camelCase` (e.g., `setProfile()`, `firmwareVersion()`)
- Free functions: `camelCase` (e.g., `makeAppIcon()`, `pluginDir()`)
- Getters: Use bare name, not `get` prefix; mark with `[[nodiscard]]` (e.g., `QString const& name() const [[nodiscard]]`)
- Setters: `setXyz()` (e.g., `setSettings()`, `setRgbBrightness()`)
- Action method names for Qt signals: past tense when emitted as event (e.g., `connected()` signal fired on connection)

**Variables & Constants:**

- Member variables: `m_camelCase` (e.g., `m_transport`, `m_writes`, `m_readFeatures`)
- Static constants: `kCamelCase` (e.g., `kMaxSettingsBytes`, `kReportSize`, `kMaxTrustRootsBytes`)
- Template parameters: `PascalCase` (e.g., `typename T`, `typename LogLevel`)
- Enums: values in UPPER_CASE (e.g., `Warn`, `Error`, `Info`)
- Type aliases: `PascalCase` (e.g., `DeviceId`, `TransportStats`)

**Types & Structs:**

- Classes, structs, interfaces: `PascalCase` (e.g., `ITransport`, `PIBridge`, `MockTransport`)
- Interface classes: prefix with `I` (e.g., `IDevice`, `ITransport`, `ISettingsCapable`, `IBatteryCapable`)
- Capability mixin interfaces: `I<Capability>Capable` (e.g., `IRgbCapable`, `IMacroCapable`, `IClockCapable`)

**Namespaces:**

- Using qualified names: `ajazz::core`, `ajazz::app`, `ajazz::devices`, `ajazz::keyboard`, `ajazz::mouse`, `ajazz::tests`
- Anonymous namespaces (`namespace { }`) used for static-linkage helpers in .cpp files

## Code Style

**Formatting:**

- Tool: `clang-format` (enforced by pre-commit hook)
- Config file: `.clang-format`
- LLVM-based style with C++20 standard
- Column limit: 100 characters
- Indent: 4 spaces (no tabs)
- Brace style: Attach (opening brace on same line)
- Pointer/reference alignment: Left (e.g., `QString const& s`, not `QString const &s`)

**Linting:**

- Tool: `clang-tidy` (static analysis; runs on pre-push stage, not per-commit)
- Config: `.clang-tidy` at repo root
- Pre-commit auto-runs clang-format for auto-fix; clang-tidy is "hook-stage manual" and "pre-push"
- Python: `ruff` (format + lint), configured in `pyproject.toml`

**Line-ending & whitespace:**

- Pre-commit enforces LF line endings across all files
- Trailing whitespace is removed automatically (exception: markdown line-break syntax `  ` at EOL)

## Import Organization

**Order (C++):**

- Group 1 (priority 1): Local project headers in quotes (e.g., `#include "pi_bridge.hpp"`)
- Group 2 (priority 3): Qt headers (e.g., `#include <QObject>`, `#include <QString>`)
- Group 3 (priority 4): Standard library (e.g., `#include <memory>`, `#include <string>`)
- Special: nlohmann::json is PRIVATE-linked only; appears in `.cpp` files, never in installed public headers (COD-031 boundary)

**Path Aliases:**

- Not detected — full qualified paths used throughout

## Error Handling

**Pattern — Transport I/O Errors:**

- Device backends use `try/catch` around `m_transport->write()` and `m_transport->read()` calls that can throw `std::runtime_error`
- On error, catch and log via `AJAZZ_LOG_ERROR()` with module name and message
- Example: `proprietary_keyboard.cpp:530-534` wraps write/read in try-catch with error logging
- For fire-and-forget commands (no response expected), the return value is cast to `(void)` with no error handling (e.g., `(void)m_transport->write(pkt);`)

**Exception Contract:**

- `ITransport::open()` throws `std::runtime_error` on failure
- `ITransport::write()` and `ITransport::read()` throw `std::runtime_error` on transport failure
- Device capability methods throw `std::runtime_error` for unimplemented features (e.g., `"per-LED RGB buffer: TODO"`)
- Logging functions (`AJAZZ_LOG_*`) are marked `noexcept` — never throw

**Qt Signal/Slot Errors:**

- Errors in Q_INVOKABLE methods (bridged to QML) are logged via `AJAZZ_LOG_ERROR()`; QML receives a log record, not an exception

## Logging

**Framework:** Custom `ajazz::core::Logger` with pluggable `LogSink` interface

**Macros:**

- `AJAZZ_LOG_TRACE()` — fine-grained tracing
- `AJAZZ_LOG_DEBUG()` — developer debug
- `AJAZZ_LOG_INFO()` — normal operational messages (default min level)
- `AJAZZ_LOG_WARN()` — recoverable abnormal conditions
- `AJAZZ_LOG_ERROR()` — non-fatal errors requiring attention
- `AJAZZ_LOG_CRITICAL()` — fatal conditions

**Usage Pattern:**

```cpp
#include "ajazz/core/logger.hpp"

AJAZZ_LOG_INFO("module-name", "message with {} format", value);
AJAZZ_LOG_ERROR("pi-bridge", "failed to load settings: {}", error);
```

Module names (first arg) are conventionally short, kebab-cased (e.g., `"pi-bridge"`, `"device-model"`, `"macro_recorder"`).

**Sink Contract:**

- Implementations must be thread-safe (all log calls are serialized internally via mutex)
- Tests can install a capturing sink via `setLogSink()` to assert on logged messages (see `test_logger.cpp`)
- Default sink writes timestamped lines to stderr

## Comments

**When to Comment:**

- Document WHY a design choice or workaround exists, not WHAT the code does
- "Pitfall N" cross-references (e.g., "Pitfall 13 lock", "Pitfall 11 invariant") point to load-bearing constraints in `CLAUDE.md` or `RETROSPECTIVE.md`
- Use `TODO:`, `FIXME:`, `HACK:`, `XXX:` for incomplete/broken code
- Multi-file implications get a cross-reference block (e.g., device backend protocol changes should mention `docs/protocols/`)

**JSDoc/Doxygen:**

- Used extensively in public headers (`.hpp` files under `src/core/include/`, `src/app/src/`, etc.)
- Format: `/** @brief ... */` with `@param`, `@return`, `@throws`, `@post`, `@see`
- Example: `pi_bridge.hpp:2-29` shows full doxygen doc for the bridge class
- Enforced by convention, not by automated check

**Implementation Comments:**

- C++ .cpp files use inline doxygen comments (e.g., line-level `///` comments) to explain non-obvious logic
- Python docstrings follow Google convention (enforced by ruff rule D102-D107)

## Function Design

**Size:**

- No hard limit, but keep functions focused on a single responsibility
- Long `switch`/`if` chains indicate refactoring opportunity (e.g., register plugin handlers as polymorphic table)

**Parameters:**

- Passed by value: small scalars, enums
- Passed by const reference: large objects, strings (e.g., `QString const&`, `std::span<std::uint8_t const>`)
- Mutable reference: rare; reserved for out-parameters (e.g., `bool& oversize` in `manifest_signer_common.cpp:59`)
- Ownership transfer: use `std::unique_ptr` or `std::move()` semantics

**Return Values:**

- Bare values, `std::optional<T>`, `std::vector<T>`, or ownership via `std::unique_ptr<T>`
- All query methods marked `[[nodiscard]]` to prevent accidental ignoring of return values

## Module Design

**Exports:**

- Public API lives in `src/*/include/ajazz/` — what is installed to system
- Implementation lives in `src/*/src/*.cpp` — private to the library
- Namespace hierarchy: `ajazz::{core,app,devices,keyboard,mouse,streamdeck,plugins}`

**Header Inclusion Pattern:**

- Public header `#include "ajazz/core/logger.hpp"` is a thin facade
- Real implementation `#include "logger_impl.hpp"` is private (not in installed include dir)
- Tests include both public and internal headers

**Barrel Files:**

- Not heavily used; each subsystem exports its main interface explicitly (e.g., `ajazz/core/device.hpp`, `ajazz/core/capabilities.hpp`)

## Qt/QML-Specific Conventions

**Q_INVOKABLE Methods:**

- Bridged from C++ to QML via these method declarations
- Return type must be serializable to QVariant (primitives, QString, QUrl, etc.)
- Example: `pi_bridge.hpp:80` declares `Q_INVOKABLE QString getSettings(QString const& action)`

**Q_OBJECT & QML_SINGLETON:**

- `Q_OBJECT` macro required for signal/slot enabled classes
- `Q_DISABLE_COPY_MOVE` is the standard way to delete copy and move constructors/operators
- `QML_SINGLETON` requires pairing with `qmlRegisterSingletonInstance` at initialization; the bare macro creates duplicate instances per QML import (v1.0 bug, fixed by using the instance function)
- Co-locate `static_assert(!std::is_default_constructible_v<T>)` with `QML_SINGLETON` to catch misuse at compile time

**Property Types:**

- Const references passed to QML cannot be observed via properties; use value or shared_ptr if data binding is needed
- East-const style (const on right) applied throughout: `QString const&`, `int const value`

## Capability Interface Pattern

The codebase uses capability mix-ins to expose optional device features via dynamic polymorphism.

**Declaration Pattern (`capabilities.hpp`):**

```cpp
class IRgbCapable {
public:
    virtual ~IRgbCapable() = default;
    [[nodiscard]] virtual std::vector<RgbZone> rgbZones() const = 0;
    virtual void setRgbColor(std::string_view zone, Rgb const& color) = 0;
};
```

**Implementation Pattern (device backend):**

```cpp
class StreamDeck final : public IDevice, public IRgbCapable, public IMacroCapable {
    // ... implement IDevice methods ...
    [[nodiscard]] std::vector<RgbZone> rgbZones() const override { /* ... */ }
    void setRgbColor(std::string_view zone, Rgb const& color) override { /* ... */ }
};
```

**Query Pattern (UI/plugin layer):**

```cpp
auto* rgbCap = dynamic_cast<ajazz::core::IRgbCapable*>(device.get());
if (rgbCap) {
    auto zones = rgbCap->rgbZones();
    // ... apply color ...
}
```

Capability bits in `DeviceDescriptor.hasRgb`, `DeviceDescriptor.hasBattery`, etc., are used by the UI to pre-gate features without dynamic_cast.

## Schema as Source of Truth

**Critical Convention:**
The JSON schema (`docs/schemas/`) is the authoritative source for wire-format field names. When C++ field names and JSON keys differ (e.g., C++ `Profile::deviceCodename` but JSON `"device"`), the schema wins.

**Example (`Profile` serialization):**

```cpp
// C++ field: deviceCodename
// JSON key: "device" (from docs/schemas/profile.schema.json)
json["device"] = profile.deviceCodename;  // CORRECT: follow schema, not field name
```

Writers must always check the schema before aligning a serializer to a C++ field name.

## Conventional Commits

**Format:** `<type>(<scope>): <subject>`

**Types (enforced by pre-commit):**

- `feat` — new feature
- `fix` — bug fix
- `docs` — documentation
- `style` — code style (formatting, whitespace)
- `refactor` — code restructuring without feature change
- `perf` — performance improvement
- `test` — test additions or changes
- `build` — build system or dependencies
- `ci` — CI/CD changes
- `chore` — maintenance tasks
- `revert` — revert a previous commit

**Scope (optional):**

- Device family: `mouse`, `keyboard`, `streamdeck`
- Component: `app`, `core`, `plugins`, `ui`
- System: `ci`, `cmake`, `docs`
- Example: `fix(mouse): correct DPI stage count validation`

**Subject:**

- Imperative mood: "add", "fix", "update", not "adds", "fixed"
- No period at end
- Under 50 characters when possible

**Example Commits:**

```
feat(streamdeck): add per-key RGB brightness control
fix(mouse): handle battery poll timeout gracefully
docs: update plugin SDK examples for v1.1
test(action-engine): add navigation stack boundary tests
```

## Workflow: Atomic Commits

**Direct-to-main workflow:**

- Fetch + rebase before every push
- Each commit is one independently-revertable change
- Never bundle unrelated fixes
- Expect 3-5 remote commits per session

**No force-push to main** — the project convention is linear history on main with sequential rebases.

## Pre-commit Hooks

**Hooks that auto-fix:**

- `clang-format` — C++ formatting
- `ruff` (format + lint) — Python
- `cmake-format` — CMake
- `typos` — spelling corrections
- `mdformat` — Markdown (excluding auto-generated docs with AUTOGEN blocks)

**Hooks that fail on violation:**

- `check-test-names-ascii` — test case/section names must be ASCII only (Windows ctest filter compatibility)
- `reject-raw-captures` — blocks raw USB capture files (CAPTURE-01 / Pitfall 17)
- `conventional-pre-commit` — enforces Conventional Commits
- `clang-tidy` — static analysis (pre-push stage; manual pre-commit stage)

**Never skip hooks with `--no-verify`** except when the hook itself is broken (e.g., stash/restore failure under concurrent agents). Document the bypass in the commit message body.

______________________________________________________________________

*Convention analysis: 2026-05-22*
