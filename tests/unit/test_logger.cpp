// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/logger.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("logger level round-trips", "[logger]") {
    using namespace ajazz::core;

    auto const previous = logLevel();
    setLogLevel(LogLevel::Debug);
    REQUIRE(logLevel() == LogLevel::Debug);
    setLogLevel(LogLevel::Error);
    REQUIRE(logLevel() == LogLevel::Error);
    setLogLevel(previous);
}

TEST_CASE("logger format does not throw", "[logger]") {
    using namespace ajazz::core;
    REQUIRE_NOTHROW(logf(LogLevel::Info, "test", "value = {}", 42));
}
