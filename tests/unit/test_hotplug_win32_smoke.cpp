// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_hotplug_win32_smoke.cpp
 * @brief Plan 04-06 Win32 smoke test for the WM_DEVICECHANGE → HotplugEvent
 *        translation path.
 *
 * Closes the Cross-Cutting pitfall (Linux-CI-blind Windows breakage) for
 * Phase 4 specifically. The 2026-05-12/13 hot-plug fix narrative
 * (HOTPLUG-07) included Windows-specific surprises — without this smoke,
 * future regressions could silently pass Linux CI.
 *
 * Two TEST_CASEs, both `_WIN32`-gated at the file level:
 *
 *   1. `Win32 device-path parses VID/PID/serial correctly` — drives
 *      `HotplugMonitor::parseDevicePathW` (AJAZZ_TESTING-gated test helper
 *      that mirrors the production `parseVidPid` + ev-build sequence
 *      inside `_WIN32` `wndProc`) with canonical AJAZZ Stream Dock
 *      device-path test vectors.
 *
 *   2. `Win32 injectEvent round-trips a synthetic event end-to-end` —
 *      exercises `HotplugMonitor::injectEvent` against the in-memory
 *      dispatch path, asserting the subscriber callback receives the
 *      injected event indistinguishably from a real OS event.
 *
 * Test vectors use Mirabox/AJAZZ vendor IDs (0x5548) with common Stream
 * Dock PIDs from the catalogue (0x6672 = AKP03, 0x6670 = AKP153) plus a
 * synthetic serial substring for the parser-extraction smoke.
 *
 * Build-system gating: tests/unit/CMakeLists.txt adds this file to the
 * `if(WIN32) ... target_sources(ajazz_unit_tests PRIVATE ...) endif()`
 * block ONLY, so Linux + macOS test binaries compile without it (the
 * `#if defined(_WIN32)` at the source level is belt-and-braces).
 */
#if defined(_WIN32)

#include "ajazz/core/hotplug_monitor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>

using ajazz::core::HotplugAction;
using ajazz::core::HotplugEvent;
using ajazz::core::HotplugMonitor;

TEST_CASE("Win32 device-path parses VID/PID/serial correctly",
          "[hotplug][win32]") {
    // Canonical AJAZZ AKP03 device path — vendor 0x5548, product 0x6672,
    // synthetic serial substring "7&deadbeef&0&0000" after the second '#'.
    HotplugEvent const ev =
        HotplugMonitor::parseDevicePathW(L"\\\\?\\HID#VID_5548&PID_6672#7&deadbeef&0&0000",
                                          HotplugAction::Arrived);
    REQUIRE(ev.vid == 0x5548);
    REQUIRE(ev.pid == 0x6672);
    REQUIRE(ev.action == HotplugAction::Arrived);
    REQUIRE(ev.serial.find("deadbeef") != std::string::npos);
}

TEST_CASE("Win32 device-path Removed action survives the parser",
          "[hotplug][win32]") {
    HotplugEvent const ev =
        HotplugMonitor::parseDevicePathW(L"\\\\?\\HID#VID_5548&PID_6670#1&abc&2&0",
                                          HotplugAction::Removed);
    REQUIRE(ev.vid == 0x5548);
    REQUIRE(ev.pid == 0x6670);
    REQUIRE(ev.action == HotplugAction::Removed);
}

TEST_CASE("Win32 device-path missing VID/PID returns 0/0 (parse failure sentinel)",
          "[hotplug][win32]") {
    HotplugEvent const ev =
        HotplugMonitor::parseDevicePathW(L"\\\\?\\USB#some-garbage-path",
                                          HotplugAction::Arrived);
    REQUIRE(ev.vid == 0);
    REQUIRE(ev.pid == 0);
}

TEST_CASE("Win32 injectEvent path round-trips a synthetic event end-to-end",
          "[hotplug][win32]") {
    HotplugEvent observed{};
    std::atomic<bool> fired{false};

    HotplugMonitor mon{[&](HotplugEvent const& ev) {
        observed = ev;
        fired.store(true, std::memory_order_release);
    }};
    // Do NOT call mon.start() — the injectEvent path operates on the
    // in-memory Callback regardless of whether the WND_PROC pump is
    // running. This is what makes the test runnable on a CI runner
    // without a real device.
    HotplugEvent const synthetic{HotplugAction::Removed, 0x5548, 0x6672, "deadbeef"};
    mon.injectEvent(synthetic);

    REQUIRE(fired.load(std::memory_order_acquire));
    REQUIRE(observed.vid == 0x5548);
    REQUIRE(observed.pid == 0x6672);
    REQUIRE(observed.action == HotplugAction::Removed);
    REQUIRE(observed.serial == "deadbeef");
}

#endif // _WIN32
