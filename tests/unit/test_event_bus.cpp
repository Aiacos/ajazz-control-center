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
#include <chrono>
#include <thread>
#include <vector>

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

/// Concurrency: many publishers fan out to many subscribers without races.
/// Run under -fsanitize=thread (CI "Sanitizers · TSan" job, #41) to catch
/// torn reads / deadlocks in the snapshot-based dispatch path.
TEST_CASE("event bus is safe under concurrent publish/subscribe", "[eventbus][concurrency]") {
    using namespace std::chrono_literals;

    ajazz::core::EventBus bus;
    constexpr int kSubscribers = 8;
    constexpr int kPublishers = 4;
    constexpr int kEventsPerPublisher = 200;

    std::atomic<int> totalCalls{0};
    std::vector<ajazz::core::EventBus::Subscription> tokens;
    tokens.reserve(kSubscribers);
    for (int i = 0; i < kSubscribers; ++i) {
        tokens.push_back(bus.subscribe([&](auto const&, auto const&) { ++totalCalls; }));
    }

    std::vector<std::thread> publishers;
    publishers.reserve(kPublishers);
    for (int p = 0; p < kPublishers; ++p) {
        publishers.emplace_back([&]() {
            for (int e = 0; e < kEventsPerPublisher; ++e) {
                bus.publish({0x1234, 0x5678, "serial"}, {});
            }
        });
    }
    for (auto& t : publishers) {
        t.join();
    }

    REQUIRE(totalCalls == kSubscribers * kPublishers * kEventsPerPublisher);

    for (auto const tok : tokens) {
        bus.unsubscribe(tok);
    }
}

/// A handler that unsubscribes itself during dispatch must not deadlock
/// (the bus snapshots subscribers before invoking them).
TEST_CASE("event bus tolerates re-entrant unsubscribe", "[eventbus][concurrency]") {
    ajazz::core::EventBus bus;

    int hits = 0;
    ajazz::core::EventBus::Subscription token{};
    token = bus.subscribe([&](auto const&, auto const&) {
        ++hits;
        bus.unsubscribe(token);
    });

    bus.publish({}, {});
    bus.publish({}, {});
    REQUIRE(hits == 1); // Second publish() finds no subscriber.
}

/// A handler that subscribes a *new* listener during dispatch must not
/// invalidate iterators / corrupt the registry.
TEST_CASE("event bus tolerates re-entrant subscribe", "[eventbus][concurrency]") {
    ajazz::core::EventBus bus;

    int outer = 0;
    int inner = 0;
    bus.subscribe([&](auto const&, auto const&) {
        ++outer;
        if (outer == 1) {
            bus.subscribe([&](auto const&, auto const&) { ++inner; });
        }
    });

    bus.publish({}, {}); // Outer fires; inner is registered for the next publish.
    bus.publish({}, {}); // Both fire.
    REQUIRE(outer == 2);
    REQUIRE(inner == 1);
}
