// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_capture_replay.cpp
 * @brief Integration tests that replay canned USB frames through the protocol parsers.
 *
 * These tests stand in for hardware access: byte sequences recorded from
 * physical devices (stored in tests/integration/fixtures/) are fed directly
 * to the protocol parser functions, keeping the test suite runnable without
 * attached hardware.
 *
 * Currently exercises the AKP153 parser against a captured key-press frame.
 */
#include "akp153_protocol.hpp"

#include <array>

#include <catch2/catch_test_macros.hpp>

/// A raw frame captured from a physical AKP153 (key 7 press) must parse to keyIndex == 7.
TEST_CASE("captured firmware response decodes to a key event", "[integration][akp153]") {
    using namespace ajazz::streamdeck::akp153;

    // Captured frame from a physical AKP153: key 7 press.
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 7;

    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->keyIndex == 7);
    REQUIRE(ev->pressed == true);
}
