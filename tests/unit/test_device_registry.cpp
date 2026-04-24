// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/device_registry.hpp"

#include "ajazz/keyboard/keyboard.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("device registry enumerates all three families", "[registry]") {
    ajazz::streamdeck::registerAll();
    ajazz::keyboard::registerAll();
    ajazz::mouse::registerAll();

    auto const descriptors = ajazz::core::DeviceRegistry::instance().enumerate();
    REQUIRE(descriptors.size() >= 7);

    int deckCount = 0, kbdCount = 0, mouseCount = 0;
    for (auto const& d : descriptors) {
        switch (d.family) {
            case ajazz::core::DeviceFamily::StreamDeck: ++deckCount;  break;
            case ajazz::core::DeviceFamily::Keyboard:   ++kbdCount;   break;
            case ajazz::core::DeviceFamily::Mouse:      ++mouseCount; break;
            default: break;
        }
    }
    REQUIRE(deckCount  >= 3);
    REQUIRE(kbdCount   >= 2);
    REQUIRE(mouseCount >= 4);
}
