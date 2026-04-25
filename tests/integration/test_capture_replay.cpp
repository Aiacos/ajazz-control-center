// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_capture_replay.cpp
 * @brief Integration tests that replay captured USB-HID frames through the
 *        protocol parsers.
 *
 * Real fixtures live under `tests/integration/fixtures/<device>/<event>.hex`
 * (see the README in that directory for the format). The build system exports
 * the fixtures path as `AJAZZ_FIXTURES_DIR`; tests resolve fixtures relative
 * to it so the binary can be relocated without breaking discovery.
 *
 * Until contributors capture real traffic, the seed fixtures are *synthetic*
 * and described as such in their headers. They still serve their primary
 * purpose: locking the parser's happy path and rejection logic into the
 * regression suite.
 */
#include "akp153_protocol.hpp"
#include "hex_loader.hpp"

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#ifndef AJAZZ_FIXTURES_DIR
// Fallback resolution; the CMake target normally injects an absolute path.
#define AJAZZ_FIXTURES_DIR "tests/integration/fixtures"
#endif

namespace {

/// Resolve a fixture path relative to the test fixtures directory.
[[nodiscard]] std::filesystem::path fixture(char const* relative) {
    return std::filesystem::path(AJAZZ_FIXTURES_DIR) / relative;
}

} // namespace

TEST_CASE("AKP153 - real key press fixture (key 7)", "[integration][akp153]") {
    using namespace ajazz::streamdeck::akp153;

    auto const bytes = ajazz::tests::loadHexFixture(fixture("akp153/key_press_07.hex"));
    auto const ev = parseInputReport(bytes);
    REQUIRE(ev.has_value());
    REQUIRE(ev->keyIndex == 7);
    REQUIRE(ev->pressed == true);
}

TEST_CASE("AKP153 - boundary key (key 15)", "[integration][akp153]") {
    using namespace ajazz::streamdeck::akp153;

    auto const bytes = ajazz::tests::loadHexFixture(fixture("akp153/key_press_15.hex"));
    auto const ev = parseInputReport(bytes);
    REQUIRE(ev.has_value());
    REQUIRE(ev->keyIndex == 15);
}

TEST_CASE("AKP153 - ACK frames are silently ignored", "[integration][akp153]") {
    using namespace ajazz::streamdeck::akp153;

    auto const bytes = ajazz::tests::loadHexFixture(fixture("akp153/ack_frame.hex"));
    auto const ev = parseInputReport(bytes);
    REQUIRE_FALSE(ev.has_value());
}

TEST_CASE("AKP153 rejects malformed frames (SEC-007/008/009/010)",
          "[integration][akp153][security]") {
    using namespace ajazz::streamdeck::akp153;

    SECTION("truncated frame") {
        auto const bytes = ajazz::tests::loadHexFixture(fixture("malformed/short_frame.hex"));
        REQUIRE_FALSE(parseInputReport(bytes).has_value());
    }

    SECTION("zero key index (no-key marker)") {
        auto const bytes = ajazz::tests::loadHexFixture(fixture("malformed/zero_key_index.hex"));
        REQUIRE_FALSE(parseInputReport(bytes).has_value());
    }

    SECTION("oversize key index 0xff") {
        auto const bytes =
            ajazz::tests::loadHexFixture(fixture("malformed/oversize_key_index.hex"));
        REQUIRE_FALSE(parseInputReport(bytes).has_value());
    }
}
