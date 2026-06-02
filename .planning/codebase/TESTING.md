# Testing Patterns

**Analysis Date:** 2026-06-02

## Test Framework

**C++ Unit & Integration Tests:**

- Framework: Catch2 v3.7.1
- Runner: ctest (CMake)
- Config: `CMakeLists.txt` in each test directory; fetch via FetchContent from GitHub
- Primary preset: `ctest --preset linux-release` (≈631+ cases across all categories)

**C++ Test Targets:**

- `ajazz_unit_tests`: Unit tests (439 Catch2 TEST_CASE cases in `tests/unit/`)
- `ajazz_integration_tests`: Integration tests (capture-replay, plugin host, time-sync; `tests/integration/`)
- `ajazz_qml_tests`: QML smoke-load harness (offscreen QML component load tests; `tests/qml/`) — requires `AJAZZ_BUILD_APP=ON`
- **Known gap:** `ajazz_qml_tests` has undefined-reference link issues (missing `sidecar_stream_dock_device.cpp` and plugin bridge slots); skip with `-E qml` if building without app

**Python Tests:**

- Framework: pytest
- Location: `python/ajazz_plugins/tests/`
- Test files: `test_*.py`
- Discover & run: `pytest python/ajazz_plugins/tests/`

**Rust Tests (Sidecar):**

- Framework: cargo test (standard Rust)
- Location: `streamdock-host/src/` (inline module tests)
- Wire-byte coverage for mirajazz sidecar lives in cargo tests (not C++ app tests)
- Run: `cd streamdock-host && cargo test`

## Test File Organization

**Location patterns:**

- C++ unit tests: `tests/unit/test_{component}.cpp` (co-located by module)
- C++ integration: `tests/integration/test_{flow}.cpp` (end-to-end scenarios)
- Fixtures: `tests/{unit,integration}/fixtures/` (header-only mocks, test data)
  - `fixtures/mock_transport.hpp` — header-only ITransport mock
  - `fixtures/fake_stream_dock_device.hpp` — in-process device fixture (no wire bytes, records capability calls)
  - `hex_loader.hpp` — binary fixture loader for capture-replay tests
- Python: `python/ajazz_plugins/tests/test_*.py`

**Naming:**

- Test files: `test_{feature}.cpp` or `test_{component}.cpp`
- Test cases: `TEST_CASE("description", "[tag1][tag2]")` — use tags for filtering (`ctest -R tag`)
- ASCII-only test names (no em-dashes `—` or right-arrows `→`; use `-` and `->`)

**Directory structure:**

```
tests/
├── CMakeLists.txt              # Main test CMake config
├── unit/
│   ├── CMakeLists.txt
│   ├── test_*.cpp              # Unit test files (~439 cases)
│   ├── fixtures/
│   │   ├── mock_transport.hpp
│   │   ├── fake_stream_dock_device.hpp
│   │   └── ...
│   ├── mock_hid_enumerator.hpp
│   └── qt_app_fixture.hpp
├── integration/
│   ├── CMakeLists.txt
│   ├── test_*.cpp              # Integration tests (~10+ tests)
│   ├── fixtures/
│   │   └── {device}/{event}.hex    # Binary capture-replay fixtures
│   └── hex_loader.hpp
├── qml/
│   ├── CMakeLists.txt          # Offscreen QML smoke harness
│   └── ...
└── fuzz/                        # libFuzzer harnesses (optional, Clang-only, -DAJAZZ_BUILD_FUZZ_TESTS=ON)
```

## Test Structure

**Catch2 suite organization:**

```cpp
TEST_CASE("ActionEngine fires chains in order", "[action_engine]") {
    // Arrange
    RecordingExecutors rec;
    ActionEngine engine(rec.make());
    Profile p{};
    p.id = "p1";
    p.deviceCodename = "akp153";
    engine.setProfile(std::move(p));

    // Act
    ActionChain chain{
        Action{.kind = ActionKind::Plugin, .id = "obs.switch", .settingsJson = "scn"},
        Action{.kind = ActionKind::KeyPress, .settingsJson = "F1"},
    };
    engine.run(chain);

    // Assert
    REQUIRE(rec.log.size() == 3);
    REQUIRE(rec.log[0] == "plugin:obs.switch:scn");
}
```

**Patterns:**

- **Arrange-Act-Assert (AAA):** Set up fixtures → invoke code under test → assert outcomes
- **Sections for nested scenarios:**

```cpp
TEST_CASE("Stream Dock v1-API parser rejects malformed frames", "[integration][streamdeck-v1]") {
    using namespace ajazz::streamdeck::akp815;

    SECTION("truncated frame") {
        auto const bytes = loadHexFixture(fixture("malformed/short_frame.hex"));
        REQUIRE_FALSE(parseInputReport(bytes).has_value());
    }

    SECTION("invalid key index") {
        auto const bytes = loadHexFixture(fixture("malformed/invalid_key.hex"));
        REQUIRE_FALSE(parseInputReport(bytes).has_value());
    }
}
```

- **Setup/teardown:** Fixtures via local scope or Catch2's `class`-based approach (rare; lambda captures preferred)
- **Assertions:** `REQUIRE(condition)` (hard fail) vs. `CHECK(condition)` (soft fail, log and continue)

## Mocking

**Frameworks:**

- Manual mocks (no external mocking library): header-only test doubles
- Catch2 matchers for assertions (Catch2 built-in)

**Patterns:**

**MockTransport (header-only, `fixtures/mock_transport.hpp`):**

```cpp
auto transport = std::make_unique<ajazz::tests::MockTransport>();
auto* observer = transport.get();   // hold observer ptr
transport->open();

auto device = ajazz::mouse::makeAjSeriesWithTransport(
    descriptor, id, std::move(transport));  // ownership transfer
auto* dpi = dynamic_cast<ajazz::core::IMouseCapable*>(device.get());
dpi->setActiveDpiStage(0);

REQUIRE(observer->writeFeatureCount() == 1);
REQUIRE(observer->writes().at(0).size() == 64);
CHECK(observer->writes().at(0)[1] == 0x21);  // cmd byte
```

**FakeStreamDockDevice (header-only, `fixtures/fake_stream_dock_device.hpp`):**

- Records capability calls instead of producing wire bytes (mirajazz sidecar owns wire coverage)
- Injects input events via `injectEvent()`

```cpp
FakeStreamDockDevice device(descriptor, id);
device.injectEvent(DeviceEvent{...});  // synthetic input

REQUIRE(device.keyImages.size() == 2);
REQUIRE(device.brightnessCalls[0] == 50);
REQUIRE(device.flushCount > 0);
```

**MockHidEnumerator (header-only, `tests/unit/mock_hid_enumerator.hpp`):**

- Mock for device enumeration (which VID/PIDs are "currently connected")
- Used by device-registry tests to inject virtual devices

**What to mock:**

- **ITransport** (wire-level I/O) — use MockTransport for byte-level assertions
- **IDevice implementations** — use FakeStreamDockDevice for app-layer behavior (image/brightness calls)
- **Device enumeration** — use MockHidEnumerator for hot-plug scenarios
- **System calls** — use platform-specific mocks (e.g., for Win32EnvBlock, setfacl)

**What NOT to mock:**

- Core library value types (Profile, Action, etc.) — construct real instances
- EventBus, ActionEngine, Logger — these are simple enough to test directly
- Plugin API (SDK layer) — test via the real Python host child (integration tests only)

## Fixtures and Factories

**Test data (C++):**

```cpp
namespace {
    struct RecordingExecutors {
        std::vector<std::string> log;
        [[nodiscard]] ActionExecutors make() {
            return ActionExecutors{
                .plugin = [this](auto id, auto settings) {
                    log.emplace_back("plugin:" + std::string{id});
                },
                // ...
            };
        }
    };
}
```

**Hex fixtures (binary capture-replay):**

- Location: `tests/integration/fixtures/{device}/{event}.hex`
- Format: ASCII hex bytes (one per line or space-separated)
- Loaded via `ajazz::tests::loadHexFixture(path)` → `std::vector<uint8_t>`
- Examples: `akp153/key_press_07.hex`, `malformed/short_frame.hex`
- Injected into parsers for byte-level regression tests

**Qt application fixture (`tests/unit/qt_app_fixture.hpp`):**

- Sets up QCoreApplication (or QGuiApplication) for tests that need Qt
- Manages QML engine initialization for app-layer tests

**Location:**

- Header-only fixtures live in their test directory: `tests/unit/fixtures/` or `tests/unit/`
- Shared between multiple test files if needed; otherwise co-located with the test

## Coverage

**Requirements:** None enforced at build time; coverage runs on CI nightly

**View coverage:**

- Linux only (gcov/lcov integration via CMake preset)
- Build: `cmake --preset coverage && cmake --build build/coverage`
- Report: `lcov --list build/coverage/coverage.info`

**Test count (as of 2026-05-22):**

- ~631+ ctest cases total:
  - ≈439 Catch2 unit tests (`tests/unit/`)
  - ≈10+ integration tests (`tests/integration/`)
  - ≈1 QML smoke test (`tests/qml/`)
  - ~180 Python pytest cases (plugin SDK tests)
  - Rust cargo tests (sidecar, inline)

## Test Types

**Unit Tests:**

- **Scope:** Single module / component in isolation
- **Location:** `tests/unit/test_{component}.cpp`
- **Approach:** Mock external dependencies (ITransport, device enumeration)
- **Examples:** ActionEngine behavior, EventBus concurrency, Profile JSON I/O, logger filtering
- **Run:** `ctest --preset linux-release` (fastest; ~5 seconds)

**Integration Tests:**

- **Scope:** Multi-component workflows end-to-end
- **Location:** `tests/integration/test_{flow}.cpp`
- **Approach:** Real components (EventBus, ActionEngine) + fixtures for I/O boundaries (MockTransport, hex fixtures)
- **Examples:** Capture-replay (USB HID parsing), time-sync E2E with real clock, Win32 environment block round-trip
- **Run:** `ctest --preset linux-release` or `ctest -R integration` to filter

**E2E Tests (QML / App-layer):**

- **Scope:** Full UI rendering and QML component interaction
- **Location:** `tests/qml/` (offscreen harness) or via debug-control channel (live)
- **Approach:** Offscreen QML engine; debug-control for live verification
- **Examples:** Component load-time smoke tests, profile switching re-renders keys
- **Run:** `ctest -R qml` (currently has link issues; skip with `-E qml` if building without app)

**Python Plugin Tests:**

- **Scope:** Plugin SDK decorator registration, dispatch routing, manifest validation
- **Location:** `python/ajazz_plugins/tests/`
- **Approach:** Unit tests of the SDK surface; no integration with the C++ host
- **Run:** `pytest python/ajazz_plugins/tests/`

**Fuzzing (optional):**

- **Framework:** libFuzzer (Clang-only)
- **Build:** `-DAJAZZ_BUILD_FUZZ_TESTS=ON` (default OFF)
- **Location:** `tests/fuzz/`
- **Purpose:** Crash-finding for parsers (e.g., HID input report parsing)

## Common Patterns

**Async testing (sleep / threading):**

```cpp
TEST_CASE("ActionEngine defers Sleep steps", "[action_engine][async]") {
    RecordingExecutors rec;
    ActionEngine engine(rec.make());
    engine.setProfile(Profile{});

    ActionChain chain{
        Action{.kind = ActionKind::Sleep, .delayMs = 50},
        Action{.kind = ActionKind::Plugin, .id = "after"},
    };
    engine.run(chain);  // Returns immediately; sleep happens in continuation

    REQUIRE(rec.log[0] == "sleep:50");
    REQUIRE(rec.log[1] == "plugin:after:");
}
```

**Concurrency testing:**

```cpp
TEST_CASE("event bus is safe under concurrent publish/subscribe", "[eventbus][concurrency]") {
    ajazz::core::EventBus bus;
    constexpr int kSubscribers = 8;
    constexpr int kPublishers = 4;

    std::atomic<int> totalCalls{0};
    std::vector<std::thread> publishers;
    for (int p = 0; p < kPublishers; ++p) {
        publishers.emplace_back([&]() {
            for (int e = 0; e < 200; ++e) {
                bus.publish({}, {});
            }
        });
    }
    for (auto& t : publishers) { t.join(); }

    REQUIRE(totalCalls == kSubscribers * kPublishers * 200);
}
```

- Run under `-fsanitize=thread` (ThreadSanitizer) in CI "Sanitizers · TSan" job
- Catch data races, deadlocks, torn reads

**Error testing:**

```cpp
TEST_CASE("profile I/O rejects malformed JSON", "[profile_io]") {
    auto path = std::filesystem::temp_directory_path() / "malformed.json";
    std::ofstream out{path};
    out << "{ invalid json }";
    out.close();

    REQUIRE_THROWS_AS(
        ajazz::core::readProfileFromDisk(path),
        ajazz::core::ProfileIoError
    );
}
```

**Capture-replay testing:**

```cpp
TEST_CASE("AJ Series mouse DPI button parsing", "[integration][capture-replay]") {
    using namespace ajazz::mouse;

    auto const bytes = ajazz::tests::loadHexFixture(
        std::filesystem::path(AJAZZ_FIXTURES_DIR) / "aj_series/dpi_button_press.hex");
    auto const ev = parseInputReport(bytes);

    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEventKind::DpiButton);
}
```

- Fixtures are sanitised USB captures (never raw `.pcap` files; see CAPTURE-01 policy)
- Use `AJAZZ_FIXTURES_DIR` CMake variable (injected at build time)

## Debug-Channel Verification Workflow

**Mandatory for every UI/behavioral change:**

1. **Build:** `cmake --build build/linux-release --preset linux-release`
1. **Launch with debug channel:** `AJAZZ_DEBUG_CONTROL=1 build/linux-release/src/app/ajazz-control-center &`
1. **Drive controls:** `scripts/ajazz-debug {method} --params '...'`
1. **Capture state:** `scripts/ajazz-debug screenshot > before.ppm`
1. **Verify:** Read the screenshot; confirm the real behavior matches expectation

**Available debug-control methods:**

- `ping` — sanity check (responds `{"event":"pong"}`)
- `state` — full app state dump
- `qml.tree` — QML object hierarchy
- `qml.get {objectName} {propertyName}` — read a property
- `qml.set {objectName} {propertyName} {value}` — write a property
- `qml.invoke {objectName} {methodName} --params '{...}'` — call a method
- `qml.click {objectName}` — emit `clicked()`
- `screenshot` — PPM screenshot (read visually, don't assert)
- `device.setActiveDevice {codename}` — select a device
- `device.renderTest --params '{"codename":"akp05e","count":10,"main":true,"encoders":true}'` — headless key+encoder render
- `input.key {index}` — inject key press
- `input.encoder {index} {delta}` — inject encoder turn
- `input.touch {x}` — inject touch input
- `profile.commitEncoderBinding {keyIndex} {actionJson}` — test encoder binding persistence
- `plugin.list` — enumerate loaded plugins
- `plugin.sendEvent {pluginId} {actionId} {settingsJson}` — dispatch an action

**Every new interactive control must:**

- Set `objectName:` in QML (for `qml.get/set/invoke/click` accessibility)
- Be testable via debug-control before claiming "done"
- Have an associated integration test that exercises the full pipeline

**Known harness gap:**

- `qml.invoke toggle` / `qml.click` do NOT fire `onToggled()` for Switch/CheckBox
- Workaround: expose a dedicated `Q_INVOKABLE` setter, or verify the C++ backing store separately (write value, relaunch, read binding)

______________________________________________________________________

*Testing analysis: 2026-06-02*
