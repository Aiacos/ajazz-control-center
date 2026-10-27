// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_linux_udev_rules.cpp
 * @brief Regression checks for Linux HID access rules shipped with the project.
 */
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#ifndef AJAZZ_TEST_REPO_ROOT
#error "AJAZZ_TEST_REPO_ROOT must be defined by the test build"
#endif

namespace {

/// Read the tracked Linux udev rule file used by package installs.
[[nodiscard]] std::string readUdevRules() {
    std::ifstream input(std::string{AJAZZ_TEST_REPO_ROOT} + "/resources/linux/70-ajazz.rules");
    REQUIRE(input.good());
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

} // namespace

TEST_CASE("Linux udev rules grant AJ-series HID access", "[udev][linux]") {
    auto const rules = readUdevRules();
    REQUIRE(rules.find("ATTRS{idVendor}==\"3151\"") != std::string::npos);
    REQUIRE(rules.find("ATTRS{idVendor}==\"3151\", MODE=\"0660\", TAG+=\"uaccess\"") !=
            std::string::npos);
}
