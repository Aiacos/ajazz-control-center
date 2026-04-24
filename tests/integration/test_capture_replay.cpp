// SPDX-License-Identifier: GPL-3.0-or-later
//
// Capture-replay integration tests run the device backends against a mock
// ITransport that reads canned USB frames from `tests/integration/fixtures/`.
// This keeps the test suite hardware-free.
//
#include "akp153_protocol.hpp"

#include <array>

#include <catch2/catch_test_macros.hpp>

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
