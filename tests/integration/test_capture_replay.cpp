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
 *
 * The fixtures under `akp153/` are v1-API HID input reports. The C++ AKP153
 * backend was removed in favour of the mirajazz sidecar, but the v1-API input
 * parser survives in-tree as the AKP815 carve-out's `akp815::parseInputReport`
 * (byte-identical framing: 512-byte "CRT" packets, 1-based key index at byte 9,
 * 1..15). These tests exercise that surviving shared parser.
 */
#include "akp815_wire.hpp"
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

TEST_CASE("Stream Dock v1-API parser - real key press fixture (key 7)",
          "[integration][streamdock-v1]") {
    using namespace ajazz::streamdeck::akp815;

    auto const bytes = ajazz::tests::loadHexFixture(fixture("akp153/key_press_07.hex"));
    auto const ev = parseInputReport(bytes);
    REQUIRE(ev.has_value());
    REQUIRE(ev->keyIndex == 7);
    REQUIRE(ev->pressed == true);
}

TEST_CASE("Stream Dock v1-API parser - boundary key (key 15)", "[integration][streamdock-v1]") {
    using namespace ajazz::streamdeck::akp815;

    auto const bytes = ajazz::tests::loadHexFixture(fixture("akp153/key_press_15.hex"));
    auto const ev = parseInputReport(bytes);
    REQUIRE(ev.has_value());
    REQUIRE(ev->keyIndex == 15);
}

TEST_CASE("Stream Dock v1-API parser - ACK frames are silently ignored",
          "[integration][streamdock-v1]") {
    using namespace ajazz::streamdeck::akp815;

    auto const bytes = ajazz::tests::loadHexFixture(fixture("akp153/ack_frame.hex"));
    auto const ev = parseInputReport(bytes);
    REQUIRE_FALSE(ev.has_value());
}

TEST_CASE("Stream Dock v1-API parser rejects malformed frames (SEC-007/008/009/010)",
          "[integration][streamdock-v1][security]") {
    using namespace ajazz::streamdeck::akp815;

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
