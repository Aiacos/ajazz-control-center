// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_event_bus.cpp
 * @brief Unit tests for the EventBus publish/subscribe mechanism.
 *
 * Verifies fan-out delivery to multiple subscribers and that unsubscribing
 * via the returned token stops further delivery without affecting other
 * active subscribers.
 */
#include "ajazz/core/event_bus.hpp"

#include <atomic>

#include <catch2/catch_test_macros.hpp>

/// Each publish() call must invoke every currently-subscribed callback once.
TEST_CASE("event bus delivers to every subscriber", "[eventbus]") {
    ajazz::core::EventBus bus;

    std::atomic<int> aCount{0};
    std::atomic<int> bCount{0};

    bus.subscribe([&](auto const&, auto const&) { ++aCount; });
    bus.subscribe([&](auto const&, auto const&) { ++bCount; });

    for (int i = 0; i < 5; ++i) {
        bus.publish({0x1234, 0x5678, "serial"}, {});
    }

    REQUIRE(aCount == 5);
    REQUIRE(bCount == 5);
}

/// After unsubscribe(token), subsequent publish() calls must not invoke that callback.
TEST_CASE("unsubscribing stops delivery", "[eventbus]") {
    ajazz::core::EventBus bus;

    int count = 0;
    auto const token = bus.subscribe([&](auto const&, auto const&) { ++count; });

    bus.publish({}, {});
    REQUIRE(count == 1);

    bus.unsubscribe(token);
    bus.publish({}, {});
    REQUIRE(count == 1);
}
