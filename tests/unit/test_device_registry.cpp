// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_device_registry.cpp
 * @brief Integration test for the DeviceRegistry across all three device families.
 *
 * Registers StreamDeck, keyboard, and mouse backends then verifies that the
 * registry holds at least the expected minimum number of descriptors per
 * family, confirming that each registerAll() call populates the table.
 *
 * Audit finding A1: each test now constructs its own local
 * `DeviceRegistry` so cases never share global state — running the suite
 * twice in the same process can no longer trip the duplicate-PID warning
 * path, and parallel cases (e.g. under `ctest -j`) can no longer race on a
 * shared mutex.
 */
#include "ajazz/core/device_registry.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <catch2/catch_test_macros.hpp>

/// DeviceRegistry::enumerate() must return descriptors for StreamDeck, keyboard, and mouse
/// families.
TEST_CASE("device registry enumerates all three families", "[registry]") {
    ajazz::core::DeviceRegistry registry;
    ajazz::streamdeck::registerAll(registry);
    ajazz::keyboard::registerAll(registry);
    ajazz::mouse::registerAll(registry);

    auto const descriptors = registry.enumerate();
    REQUIRE(descriptors.size() >= 7);

    int deckCount = 0, kbdCount = 0, mouseCount = 0;
    for (auto const& d : descriptors) {
        switch (d.family) {
        case ajazz::core::DeviceFamily::StreamDeck:
            ++deckCount;
            break;
        case ajazz::core::DeviceFamily::Keyboard:
            ++kbdCount;
            break;
        case ajazz::core::DeviceFamily::Mouse:
            ++mouseCount;
            break;
        default:
            break;
        }
    }
    REQUIRE(deckCount >= 3);
    REQUIRE(kbdCount >= 2);
    REQUIRE(mouseCount >= 4);
}

/// Two locally-owned registries must not share state — audit finding A1.
TEST_CASE("device registry instances are independent", "[registry][isolation]") {
    ajazz::core::DeviceRegistry a;
    ajazz::core::DeviceRegistry b;
    ajazz::keyboard::registerAll(a);
    REQUIRE(!a.enumerate().empty());
    REQUIRE(b.enumerate().empty());
}
