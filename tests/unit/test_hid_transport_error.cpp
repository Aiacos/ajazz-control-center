// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_hid_transport_error.cpp
 * @brief Tests for HID transport open-error diagnostics.
 */
#include "hid_transport_error.hpp"

#include <cerrno>
#include <string>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("HID open diagnostics include the system error", "[hid][transport]") {
    auto const message = ajazz::core::detail::hidOpenErrorMessage(ENOENT);

    REQUIRE(message.starts_with("hid_open failed: "));
    REQUIRE(message.find("check the installed AJAZZ udev rule") == std::string::npos);
}

TEST_CASE("HID access denied diagnostics include the udev hint", "[hid][transport]") {
    auto const message = ajazz::core::detail::hidOpenErrorMessage(EACCES);

    REQUIRE(message.starts_with("hid_open failed: "));
    REQUIRE(message.ends_with(" (check the installed AJAZZ udev rule and hidraw ACLs)"));
}

TEST_CASE("HID open diagnostics omit unavailable system error text", "[hid][transport]") {
    REQUIRE(ajazz::core::detail::hidOpenErrorMessage(0) == "hid_open failed");
}
