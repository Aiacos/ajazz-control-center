# Testing Patterns

**Analysis Date:** 2026-05-22

## Test Framework

**Runner:**

- C++: Catch2 v3.7.1 (fetched via FetchContent in `tests/CMakeLists.txt`)
- Python: pytest (configured in `pyproject.toml` with `testpaths` = `["tests", "python/ajazz_plugins/tests"]`)
- Coverage: pytest-cov for Python

**Assertion Library:**

- C++: Catch2 built-in assertions (REQUIRE, CHECK, REQUIRE_THROWS, etc.)
- Python: standard `assert` statements with explicit equality checks

**Run Commands:**

```bash
# C++ tests (preset-based)
ctest --preset linux-release      # Run all tests (~399 test cases)
ctest --preset linux-debug        # Debug build variant
ctest -R <pattern>                # Filter by regex (e.g., ctest -R "time_sync")

# Python tests
pytest python/ajazz_plugins/tests/
pytest -v python/ajazz_plugins/tests/test_plugin_api.py
pytest --cov=ajazz_plugins        # With coverage report

# Watch mode (C++)
# Not native to ctest, but CMake presets support incremental builds

# Coverage (C++)
# Built into ctest output with gcov/llvm-cov; exact report path depends on compiler
```

## Test File Organization

**Location (C++ tests):**

- Unit tests: `tests/unit/test_<component>.cpp`
- Integration tests: `tests/integration/test_<scenario>.cpp`
- Fixtures/helpers: `tests/unit/fixtures/*.hpp` (header-only for reuse)
- Mocks: `tests/unit/mock_*.hpp`

**Location (Python tests):**

- Plugin SDK tests: `python/ajazz_plugins/tests/test_*.py`
- Test infrastructure does not auto-discover; explicitly listed in `pyproject.toml`

**Naming Convention:**

- Test function: `test_<description>()` (C++: `TEST_CASE()`, Python: `def test_<name>()`)
- Section grouping (C++): `SECTION("description")` (optional, nested under TEST_CASE)
- **ASCII-only rule:** Test case and section titles must be ASCII-only (no em-dash, right-arrow, etc.). Windows CI ctest filter uses Win32 CMD codepage; Unicode gets mangled. Pre-commit hook `check-test-names-ascii` enforces this at commit time.

**Test Counts:**

- C++ test cases: ~399 TEST_CASE invocations across unit + integration
- Python test functions: 3 files (`test_plugin_api.py`, `test_manifest_signing.py`, `test_host_child_safety.py`)

## Test Structure

**Suite Organization (C++):**

```cpp
// tests/unit/test_action_engine.cpp
#include "ajazz/core/action_engine.hpp"
#include <catch2/catch_test_macros.hpp>

namespace {
    // Fixtures and helpers at file scope in anonymous namespace
    struct RecordingExecutors {
        std::vector<std::string> log;
        [[nodiscard]] ActionExecutors make() { /* ... */ }
    };
}

TEST_CASE("ActionEngine fires chains in order", "[action_engine]") {
    RecordingExecutors rec;
    ActionEngine engine(rec.make());

    // Arrange
    Profile p{};
    p.id = "p1";
    p.deviceCodename = "akp153";
    engine.setProfile(std::move(p));

    // Act
    ActionChain chain{ /* ... */ };
    engine.run(chain);

    // Assert
    REQUIRE(rec.log.size() == 3);
    REQUIRE(rec.log[0] == "plugin:obs.switch:scn");
}

TEST_CASE("ActionEngine honours Sleep steps", "[action_engine]") {
    // ... another test ...
}

TEST_CASE("ActionEngine pushes / pops navigation pages", "[action_engine][folders]") {
    ActionEngine engine;
    engine.setProfile(Profile{});

    REQUIRE(engine.currentPageId() == "root");

    engine.run(ActionChain{Action{.kind = ActionKind::OpenFolder, .id = "music"}});
    REQUIRE(engine.currentPageId() == "music");

    // Optional nested section
    SECTION("nested operations") {
        engine.run(ActionChain{Action{.kind = ActionKind::OpenFolder, .id = "playlists"}});
        REQUIRE(engine.currentPageId() == "playlists");
    }
}
```

**Patterns:**

- **Arrange-Act-Assert:** Comments separate test phases (optional but encouraged)
- **Fixtures:** Defined in anonymous namespace at file scope; instantiated per test
- **Tags:** `[component]` tags in square brackets enable filtering; can be chained (e.g., `[action_engine][folders]`)
- **Cleanup:** RAII handles cleanup (destructors run at end of TEST_CASE scope); explicit teardown is rare

## Mocking

**Framework:** Custom mock classes (no external framework like GTest/gmock)

**Transport Mocking (COD-026):**

- Header-only fixture: `tests/unit/fixtures/mock_transport.hpp`
- Implements `ajazz::core::ITransport` interface fully
- Records every `write()` and `writeFeature()` call as byte vectors
- Inspection API: `writes()`, `writeCount()`, `writeFeatureCount()`, `reset()`
- Input injection: `enqueueRead()`, `enqueueReadFeature()` for canned responses

**Usage Example:**

```cpp
auto transport = std::make_unique<ajazz::tests::MockTransport>();
auto* observer = transport.get();
transport->open();

auto device = ajazz::mouse::makeAjSeriesWithTransport(descriptor, id, std::move(transport));
auto* dpi = dynamic_cast<ajazz::core::IMouseCapable*>(device.get());
dpi->setActiveDpiStage(0);

REQUIRE(observer->writeFeatureCount() == 1);
REQUIRE(observer->writes().at(0)[1] == 0x21);  // cmd byte
```

**Design Notes:**

- Header-only by design (single translation unit includes; no separate CMake link entry)
- Single-threaded (parent `ITransport` doesn't promise thread safety)
- Not copyable or movable (inherits from `ITransport`)

**Log Mocking (test_logger.cpp):**

- Custom `CapturingSink` extends `ajazz::core::LogSink`
- Records `(level, module, message)` tuples from all accepted log calls
- Thread-safe (uses mutex like production sink)
- Installed via `setLogSink()` at test start; reset to nullptr at end

**Enumerator Mocking:**

- Parallel mock: `mock_hid_enumerator.hpp` (device enumeration layer)
- Pattern: fixture struct with builder method returning fake device list

## Fixtures and Factories

**Test Data (C++):**

- Inline construction: `Profile p{.id = "p1", .deviceCodename = "akp153"}`
- Struct aggregates: `.member = value` syntax preferred over constructor calls
- Factories: named functions like `makeAjSeriesWithTransport()` that return fully-initialized objects

**Test Data (Python):**

- Class fixtures: inline `MyPlugin()` instance
- Settings dict: `{"foo": "bar"}` passed as JSON string to dispatch

**Location:**

- Single-file fixtures: defined in anonymous namespace in the .cpp
- Multi-file fixtures: in `tests/unit/fixtures/` (e.g., `qt_app_fixture.hpp`, `mock_transport.hpp`)

## Coverage

**Requirements:**

- No minimum enforced; coverage metrics are informational
- Python: pytest-cov can generate reports (`pytest --cov=ajazz_plugins`)
- C++: gcov/llvm-cov integration available; exact invocation depends on preset

**View Coverage:**

```bash
# Python
pytest --cov=ajazz_plugins --cov-report=html
# HTML report at htmlcov/index.html

# C++ (example for Linux)
ctest --preset linux-debug   # Build with coverage
# Coverage files in build/_deps/ (compiler-specific location)
```

## Test Types

**Unit Tests:**

- Scope: Single component in isolation (e.g., ActionEngine, Logger, Profile I/O)
- Location: `tests/unit/test_*.cpp`
- Isolation: Achieved via dependency injection (MockTransport, capturing sink)
- Execution: \<15 sec for full suite

**Integration Tests:**

- Scope: Multi-component workflows (e.g., time-sync end-to-end, capture-replay)
- Location: `tests/integration/test_*.cpp`
- Isolation: May use real file I/O, real network (with timeouts)
- Execution: ~10 sec for full suite

**E2E Tests:**

- Scope: Not detected in unit/integration
- Framework: Manual testing or external test harness (beyond ctest scope)

**Smoke Tests (CI):**

- "Verify Windows hot-plug smoke ran" gate in CI ensures Windows environment works
- Hot-plug tests exercise device enumeration without hardware (virtual/mock)

## Common Patterns

**Async Testing (C++):**

- Catch2 supports synchronous testing; async code under test is called directly in the test
- Waits use busy-loop or condition variable with timeout (example: battery poll test in `test_battery_service.cpp` if it exists)
- Qt event loop may be spun manually if needed (see `qt_app_fixture.hpp` for Qt-aware test setup)

**Error Testing (C++):**

```cpp
TEST_CASE("throwsError on invalid input", "[component]") {
    REQUIRE_THROWS_AS(functionThatThrows(badInput), std::runtime_error);
}

TEST_CASE("catchesException and logs", "[component]") {
    // Use capturing sink to assert error was logged
    auto sink = std::make_shared<CapturingSink>();
    setLogSink(sink);

    functionThatThrowsButCatches();

    auto records = sink->snapshot();
    REQUIRE(records.size() == 1);
    CHECK(records[0].level == LogLevel::Error);
}
```

**Python Testing (pytest):**

```python
def test_dispatch_routes_to_method() -> None:
    """dispatch() deserialises settings JSON and passes ActionContext."""
    plugin = MyPlugin()
    plugin.dispatch("hello", '{"foo": "bar"}')
    assert plugin.calls == [("hello", {"foo": "bar"})]

def test_dispatch_raises_on_unknown_action() -> None:
    """dispatch() raises KeyError for unregistered action ids."""
    plugin = MyPlugin()
    try:
        plugin.dispatch("missing", "{}")
    except KeyError:
        pass
    else:
        raise AssertionError("expected KeyError")
```

**Wire-Format Testing (MockTransport Pattern):**

```cpp
TEST_CASE("battery poll sends correct opcode", "[battery]") {
    auto transport = std::make_unique<ajazz::tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();

    auto device = ajazz::mouse::makeAjSeriesWithTransport(descriptor, id, std::move(transport));
    auto* battery = dynamic_cast<ajazz::core::IBatteryCapable*>(device.get());

    battery->pollBattery();

    // Assert exact byte sequence
    REQUIRE(observer->writeFeatureCount() >= 1);
    auto const& sent = observer->writes().back();
    REQUIRE(sent.size() == 64);
    CHECK(sent[0] == 0x00);   // report id
    CHECK(sent[1] == 0x83);   // opcode
}
```

## CMake Integration

**Test Discovery:**

- CMake uses Catch2's `catch_discover_tests()` to enumerate TEST_CASE invocations at configure time
- Tests are added as CTest entries with tags for filtering
- Presets in `CMakePresets.json` define standard build + test configurations

**Preset Commands:**

```bash
ctest --preset linux-release    # Release build, run all tests
ctest --preset linux-debug      # Debug build with symbols
ctest --preset windows-release  # Windows MSVC build
ctest --preset macos-release    # macOS Clang build
```

**CI Matrix:**

- Runs on ubuntu-24.04 (Linux GCC + Clang), windows-2022 (MSVC), macos-14 (Apple Clang)
- Each platform matrix job builds Release variant only (saves CI time)
- Full matrix: 3 platforms × 1 build type = 3 jobs per commit

## Python Plugin Tests

**Scope:**

- `python/ajazz_plugins/tests/test_plugin_api.py` — @action decorator, dispatch routing, JSON handling
- `python/ajazz_plugins/tests/test_manifest_signing.py` — trust-roots loading, signature verification
- `python/ajazz_plugins/tests/test_host_child_safety.py` — IPC message handling

**Key Patterns:**

- Import helpers directly from implementation (e.g., `from ajazz_plugins import Plugin, action, ActionContext`)
- `__main__` guards in `_host_child.py` allow calling test helpers without subprocess
- No fixtures/mocking framework; plain pytest with inline test plugins
- Type hints throughout (enforced by mypy strict mode in `pyproject.toml`)

**Example (`test_plugin_api.py:17-38`):**

```python
class MyPlugin(Plugin):
    """Minimal test fixture."""
    id = "test.my-plugin"
    name = "Test plugin"

    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, object]]] = []

    @action(id="hello", label="Hello")
    def _hello(self, ctx: ActionContext) -> None:
        self.calls.append(("hello", dict(ctx.settings)))

def test_actions_are_discovered() -> None:
    plugin = MyPlugin()
    assert set(plugin.actions().keys()) == {"hello", "no-ctx"}
```

______________________________________________________________________

*Testing analysis: 2026-05-22*
