// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_watcher_test.cpp
 * @brief Factory / recording-stub / injection tests for IActiveWindowWatcher
 *        (Phase 34 APROF-01 Wave 0).
 *
 * These cases are GREEN against the StubActiveWindowWatcher (the default
 * makeDefaultActiveWindowWatcher() result with no native backend). They mirror
 * the test_input_synth.cpp factory + recording-stub + injection idiom: drive the
 * watcher entirely through the injectForeground() seam (no real OS focus event).
 *
 * Coverage:
 *  - makeDefaultActiveWindowWatcher() returns a usable, non-null watcher
 *  - start() registers a callback; injectForeground fires it once per inject
 *  - the recorded log accumulates one "fg:<appId>" entry per inject
 *  - capabilityAvailable() reflects the settable stub state (APROF-03 degradation)
 *  - the [active_window_debounce] case documents the debounce contract the REAL
 *    backends (Plan 03) must satisfy; the stub itself does NOT debounce (it is
 *    deterministic for tests), so the case asserts the per-inject fire semantics
 *    the debounce is layered on top of in the live backend.
 *
 * Tags: [active_window] — select with: ctest --preset linux-release -R active_window
 */
#include "ajazz/core/active_window_watcher.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::core;

TEST_CASE("active_window makeDefaultActiveWindowWatcher returns non-null", "[active_window]") {
    auto watcher = makeDefaultActiveWindowWatcher();
    REQUIRE(watcher != nullptr);
}

TEST_CASE("active_window default watcher is the recording stub and starts cleanly",
          "[active_window]") {
    auto watcher = makeDefaultActiveWindowWatcher();
    REQUIRE(watcher != nullptr);
    auto* stub = dynamic_cast<StubActiveWindowWatcher*>(watcher.get());
    REQUIRE(stub != nullptr);
    REQUIRE(stub->log().empty());
    stub->start([](ActiveWindowInfo) {});
    SUCCEED("start() does not throw and leaves an empty log");
}

TEST_CASE("active_window injectForeground fires the start callback once per inject",
          "[active_window]") {
    StubActiveWindowWatcher stub;

    std::vector<std::string> seen;
    stub.start([&seen](ActiveWindowInfo info) { seen.push_back(info.appId); });

    stub.injectForeground(ActiveWindowInfo{"firefox", "Mozilla Firefox"});
    stub.injectForeground(ActiveWindowInfo{"code", "main.cpp - Code"});

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == "firefox");
    REQUIRE(seen[1] == "code");
}

TEST_CASE("active_window injectForeground records one log entry per change", "[active_window]") {
    StubActiveWindowWatcher stub;
    stub.start([](ActiveWindowInfo) {});

    stub.injectForeground(ActiveWindowInfo{"firefox", ""});
    stub.injectForeground(ActiveWindowInfo{"firefox", ""});
    stub.injectForeground(ActiveWindowInfo{"code", ""});

    REQUIRE(stub.log().size() == 3);
    REQUIRE(stub.log()[0] == "fg:firefox");
    REQUIRE(stub.log()[1] == "fg:firefox");
    REQUIRE(stub.log()[2] == "fg:code");
}

TEST_CASE("active_window injectForeground without start records but fires no callback",
          "[active_window]") {
    StubActiveWindowWatcher stub;
    // No start() -> no callback registered. Inject must still record and not crash.
    stub.injectForeground(ActiveWindowInfo{"firefox", ""});
    REQUIRE(stub.log().size() == 1);
    REQUIRE(stub.log()[0] == "fg:firefox");
}

TEST_CASE("active_window stop clears the callback so later injects do not fire it",
          "[active_window]") {
    StubActiveWindowWatcher stub;
    int count = 0;
    stub.start([&count](ActiveWindowInfo) { ++count; });
    stub.injectForeground(ActiveWindowInfo{"a", ""});
    REQUIRE(count == 1);

    stub.stop();
    stub.injectForeground(ActiveWindowInfo{"b", ""});
    REQUIRE(count == 1);             // callback no longer fires
    REQUIRE(stub.log().size() == 2); // but the change is still recorded
}

TEST_CASE("active_window clearLog empties the recorded change log", "[active_window]") {
    StubActiveWindowWatcher stub;
    stub.injectForeground(ActiveWindowInfo{"a", ""});
    REQUIRE(stub.log().size() == 1);
    stub.clearLog();
    REQUIRE(stub.log().empty());
}

TEST_CASE("active_window capabilityAvailable defaults true and is settable", "[active_window]") {
    StubActiveWindowWatcher stub;
    REQUIRE(stub.capabilityAvailable()); // default available

    stub.setCapabilityAvailable(false);
    REQUIRE_FALSE(stub.capabilityAvailable()); // APROF-03 degradation path

    stub.setCapabilityAvailable(true);
    REQUIRE(stub.capabilityAvailable());
}

TEST_CASE("active_window capabilityAvailable can be set via constructor", "[active_window]") {
    StubActiveWindowWatcher degraded(false);
    REQUIRE_FALSE(degraded.capabilityAvailable());

    StubActiveWindowWatcher ok(true);
    REQUIRE(ok.capabilityAvailable());
}

// The debounce contract belongs to the REAL backends (Plan 03): rapid foreground
// changes within the ~150-250ms window coalesce to a single emit. The stub does
// not debounce (it is deterministic for tests), so this case pins the building
// block the debounce sits on — each distinct injectForeground delivers exactly
// one callback with the final appId — which the Plan-03 QTimer debounce wraps.
TEST_CASE("active_window_debounce rapid injects each deliver the final appId",
          "[active_window][active_window_debounce]") {
    StubActiveWindowWatcher stub;
    ActiveWindowInfo last;
    int fires = 0;
    stub.start([&](ActiveWindowInfo info) {
        last = info;
        ++fires;
    });

    // Simulate a burst the live debouncer would coalesce.
    stub.injectForeground(ActiveWindowInfo{"transient1", ""});
    stub.injectForeground(ActiveWindowInfo{"transient2", ""});
    stub.injectForeground(ActiveWindowInfo{"settled", "Settled Window"});

    // Stub fires per-inject (no coalescing); the LAST appId is the settled one.
    REQUIRE(fires == 3);
    REQUIRE(last.appId == "settled");
    REQUIRE(last.title == "Settled Window");
}
