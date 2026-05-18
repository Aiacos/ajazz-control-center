// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_battery_service.cpp
 * @brief One-case coverage of @ref ajazz::app::BatteryService::lastKnownPercent.
 *
 * The full BatteryService surface (poll timer, QSettings persistence,
 * batteryQueried / batteryUnavailable signal emission) is exercised
 * indirectly through the live device path in the app target. This
 * isolated test just pins the small helper added in the 2026-05-18 P3.d
 * QML wire-up commit: lastKnownPercent must surface the last positive
 * reading and reset to -1 when the next query reports unavailable.
 *
 * Mirrors the IDevice/IBatteryCapable mock pattern from
 * @c test_time_sync_service.cpp so the two services stay shape-symmetric.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "battery_service.hpp"
#include "qt_app_fixture.hpp"

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

class MockBatteryDevice : public ajazz::core::IDevice, public ajazz::core::IBatteryCapable {
public:
    explicit MockBatteryDevice(ajazz::core::DeviceDescriptor d) : m_desc(std::move(d)) {}

    [[nodiscard]] ajazz::core::DeviceDescriptor const& descriptor() const noexcept override {
        return m_desc;
    }
    [[nodiscard]] ajazz::core::DeviceId id() const noexcept override { return {}; }
    [[nodiscard]] std::string firmwareVersion() const override { return "mock"; }

    void open() override { m_open = true; }
    void close() override { m_open = false; }
    [[nodiscard]] bool isOpen() const noexcept override { return m_open; }
    void onEvent(ajazz::core::EventCallback) override {}
    std::size_t poll() override { return 0; }

    // IBatteryCapable — returns whatever the test set.
    [[nodiscard]] std::optional<std::uint8_t> batteryPercent() override { return m_next; }

    std::optional<std::uint8_t> m_next{};

private:
    ajazz::core::DeviceDescriptor m_desc;
    bool m_open{false};
};

ajazz::core::DeviceDescriptor descFor(QString const& codename) {
    return ajazz::core::DeviceDescriptor{
        .vendorId = 0x1234,
        .productId = 0x5678,
        .family = ajazz::core::DeviceFamily::Keyboard,
        .model = "Mock Battery Device",
        .codename = codename.toStdString(),
        .hasBattery = true,
    };
}

} // namespace

TEST_CASE("BatteryService: lastKnownPercent caches successes and resets on unavailable",
          "[battery-service]") {
    ajazz::tests::qtApp();

    auto mock = std::make_shared<MockBatteryDevice>(descFor("ak980pro"));

    ajazz::app::BatteryService svc(
        [mock](QString const& cn) -> std::shared_ptr<ajazz::core::IDevice> {
            return cn == QStringLiteral("ak980pro") ? std::shared_ptr<ajazz::core::IDevice>(mock)
                                                    : nullptr;
        },
        []() -> std::vector<QString> { return {}; },
        nullptr);

    // Unknown codename — cache is empty.
    REQUIRE(svc.lastKnownPercent(QStringLiteral("ak980pro")) == -1);

    // Successful query — cache is populated with the percent.
    mock->m_next = static_cast<std::uint8_t>(73);
    svc.queryBattery(QStringLiteral("ak980pro"));
    REQUIRE(svc.lastKnownPercent(QStringLiteral("ak980pro")) == 73);

    // Subsequent unavailable result — cache is cleared.
    mock->m_next = std::nullopt;
    svc.queryBattery(QStringLiteral("ak980pro"));
    REQUIRE(svc.lastKnownPercent(QStringLiteral("ak980pro")) == -1);

    // Codenames the service has never seen always return -1.
    REQUIRE(svc.lastKnownPercent(QStringLiteral("never-queried")) == -1);
}
