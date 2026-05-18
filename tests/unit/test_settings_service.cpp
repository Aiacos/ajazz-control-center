// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_settings_service.cpp
 * @brief Pure-helper coverage for @ref ajazz::app::SettingsService::clampResponseLevel.
 *
 * The wider SettingsService surface (DeviceLookup resolution, signal
 * emission, transport error handling) is exercised indirectly through
 * the live device path on AK980 PRO + the existing
 * `test_ak980_settings_batch.cpp` byte-level coverage. This file
 * isolates the small helper added so QML can preview the persisted
 * value without round-tripping through HID: clampResponseLevel must
 * normalise 0 to the vendor default 3, pass [1..5] through unchanged,
 * and clamp anything above 5 down to 5.
 *
 * Mirrors the isolated-helper test style used by
 * `test_battery_service.cpp::lastKnownPercent`.
 */
#include "settings_service.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SettingsService::clampResponseLevel normalises 0 and clamps out-of-range",
          "[settings-service]") {
    using ajazz::app::SettingsService;

    // 0 -> vendor default 3 (matches ProprietaryKeyboard::buildSettingsBatch).
    REQUIRE(SettingsService::clampResponseLevel(0) == std::uint8_t{3});

    // In-range values pass through untouched.
    REQUIRE(SettingsService::clampResponseLevel(1) == std::uint8_t{1});
    REQUIRE(SettingsService::clampResponseLevel(2) == std::uint8_t{2});
    REQUIRE(SettingsService::clampResponseLevel(3) == std::uint8_t{3});
    REQUIRE(SettingsService::clampResponseLevel(4) == std::uint8_t{4});
    REQUIRE(SettingsService::clampResponseLevel(5) == std::uint8_t{5});

    // Above 5 clamps to 5 (matches the device-side clamp).
    REQUIRE(SettingsService::clampResponseLevel(6) == std::uint8_t{5});
    REQUIRE(SettingsService::clampResponseLevel(42) == std::uint8_t{5});
    REQUIRE(SettingsService::clampResponseLevel(255) == std::uint8_t{5});

    // Negative values are treated as 0 (defensive against bad QML input).
    REQUIRE(SettingsService::clampResponseLevel(-1) == std::uint8_t{3});
    REQUIRE(SettingsService::clampResponseLevel(-99) == std::uint8_t{3});
}
