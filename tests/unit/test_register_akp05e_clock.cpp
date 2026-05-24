// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_register_akp05e_clock.cpp
 * @brief Descriptor honesty test for DEVICES-11 / ARCH-05.
 *
 * Asserts that the akp05e descriptor (0x0300:0x3004) advertises hasClock==false
 * so that the runtime Sync-button affordance is never shown for the Stream Dock
 * Plus (which has no firmware RTC per ARCH-05).
 *
 * Also guards against an over-broad flip by asserting that the mirabox_n4
 * descriptor still has hasClock==true (its change is deferred to a later
 * honesty sweep per DEVICES-11 scope note).
 *
 * Tags: [stream-dock-control][register]
 */
#include "ajazz/core/device_registry.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("akp05e_clock: descriptor advertises hasClock==false (DEVICES-11)",
          "[stream-dock-control][register]") {
    ajazz::core::DeviceRegistry registry;
    ajazz::streamdeck::registerAll(registry);

    auto const descriptors = registry.enumerate();

    SECTION("akp05e entry exists and has hasClock==false") {
        auto const it = std::find_if(descriptors.begin(), descriptors.end(), [](auto const& d) {
            return d.codename == "akp05e";
        });
        REQUIRE(it != descriptors.end());
        CHECK(!it->hasClock);
    }

    SECTION("mirabox_n4 entry still has hasClock==true (guard against over-broad flip)") {
        auto const it = std::find_if(descriptors.begin(), descriptors.end(), [](auto const& d) {
            return d.codename == "mirabox_n4";
        });
        REQUIRE(it != descriptors.end());
        CHECK(it->hasClock);
    }
}
