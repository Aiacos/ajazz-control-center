// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_logger.cpp
 * @brief Unit tests for the application logger (level control and basic formatting).
 *
 * Verifies that setLogLevel()/logLevel() round-trip correctly and that
 * logf() does not throw for well-formed format strings.
 */
#include "ajazz/core/logger.hpp"

#include <catch2/catch_test_macros.hpp>

/// setLogLevel() must be immediately reflected by logLevel() with no intermediate state.
TEST_CASE("logger level round-trips", "[logger]") {
    using namespace ajazz::core;

    auto const previous = logLevel();
    setLogLevel(LogLevel::Debug);
    REQUIRE(logLevel() == LogLevel::Debug);
    setLogLevel(LogLevel::Error);
    REQUIRE(logLevel() == LogLevel::Error);
    setLogLevel(previous);
}

/// logf() must not throw for a valid format string and an integer argument.
TEST_CASE("logger format does not throw", "[logger]") {
    using namespace ajazz::core;
    REQUIRE_NOTHROW(logf(LogLevel::Info, "test", "value = {}", 42));
}
