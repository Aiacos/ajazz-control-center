// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_tray_battery.cpp
 * @brief Unit coverage for battery readings flowing into the system tray.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "app_icon.hpp"
#include "battery_service.hpp"
#include "qt_app_fixture.hpp"
#include "tray_controller.hpp"

#include <QIcon>
#include <QPixmap>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

class MockBatteryDevice : public ajazz::core::IDevice, public ajazz::core::IBatteryCapable {
public:
    explicit MockBatteryDevice(ajazz::core::DeviceDescriptor descriptor)
        : descriptor_(std::move(descriptor)) {}

    [[nodiscard]] ajazz::core::DeviceDescriptor const& descriptor() const noexcept override {
        return descriptor_;
    }
    [[nodiscard]] ajazz::core::DeviceId id() const noexcept override { return {}; }
    [[nodiscard]] std::string firmwareVersion() const override { return "mock"; }
    void open() override { open_ = true; }
    void close() override { open_ = false; }
    [[nodiscard]] bool isOpen() const noexcept override { return open_; }
    void onEvent(ajazz::core::EventCallback) override {}
    std::size_t poll() override { return 0; }
    [[nodiscard]] std::optional<std::uint8_t> batteryPercent() override { return next_; }

    std::optional<std::uint8_t> next_;

private:
    ajazz::core::DeviceDescriptor descriptor_;
    bool open_{false};
};

std::shared_ptr<MockBatteryDevice> makeBatteryDevice(QString const& codename) {
    return std::make_shared<MockBatteryDevice>(ajazz::core::DeviceDescriptor{
        .vendorId = 0x1234,
        .productId = 0x5678,
        .family = ajazz::core::DeviceFamily::Mouse,
        .model = "Mock Mouse",
        .codename = codename.toStdString(),
        .hasBattery = true,
    });
}

} // namespace

TEST_CASE("Tray battery state retains latest values and clears unavailable devices",
          "[tray][battery]") {
    ajazz::tests::qtGuiApp();
    ajazz::app::TrayController tray(nullptr);

    tray.setDeviceBattery(QStringLiteral("ajazz_mouse"), 68);
    tray.setDeviceBattery(QStringLiteral("other_mouse"), 42);
    REQUIRE(tray.batterySummary().contains(QStringLiteral("ajazz_mouse: 68%")));
    REQUIRE(tray.batterySummary().contains(QStringLiteral("other_mouse: 42%")));

    tray.setDeviceBattery(QStringLiteral("ajazz_mouse"), 73);
    REQUIRE(tray.batterySummary().contains(QStringLiteral("ajazz_mouse: 73%")));
    REQUIRE_FALSE(tray.batterySummary().contains(QStringLiteral("ajazz_mouse: 68%")));

    tray.setDeviceBattery(QStringLiteral("other_mouse"), 150);
    REQUIRE(tray.batterySummary().contains(QStringLiteral("other_mouse: 100%")));
    tray.setDeviceBattery(QStringLiteral("other_mouse"), 18);
    REQUIRE(tray.batterySummary().contains(QStringLiteral("Low battery, other_mouse: 18%")));

    tray.clearDeviceBattery(QStringLiteral("ajazz_mouse"));
    REQUIRE_FALSE(tray.batterySummary().contains(QStringLiteral("ajazz_mouse")));
    REQUIRE(tray.batterySummary().contains(QStringLiteral("other_mouse: 18%")));
}

TEST_CASE("Tray follows BatteryService readings and drops stale entries", "[tray][battery]") {
    ajazz::tests::qtGuiApp();
    auto mouse = makeBatteryDevice(QStringLiteral("ajazz_mouse"));
    std::vector<QString> connected{QStringLiteral("ajazz_mouse")};
    ajazz::app::BatteryService battery(
        [mouse](QString const& codename) -> std::shared_ptr<ajazz::core::IDevice> {
            return codename == QStringLiteral("ajazz_mouse") ? mouse : nullptr;
        },
        [&connected] { return connected; },
        nullptr);
    ajazz::app::TrayController tray(nullptr);
    tray.trackBatteryService(&battery);

    mouse->next_ = static_cast<std::uint8_t>(81);
    battery.pollAll();
    REQUIRE(tray.batterySummary().contains(QStringLiteral("ajazz_mouse: 81%")));

    mouse->next_ = std::nullopt;
    battery.pollAll();
    REQUIRE(tray.batterySummary().isEmpty());

    mouse->next_ = static_cast<std::uint8_t>(42);
    battery.pollAll();
    REQUIRE(tray.batterySummary().contains(QStringLiteral("ajazz_mouse: 42%")));
    connected.clear();
    battery.onDevicesChanged();
    CHECK(tray.batterySummary().isEmpty());
}

TEST_CASE("Tray battery badge reflects changes in charge level", "[tray][battery][icon]") {
    ajazz::tests::qtGuiApp();
    QPixmap source(32, 32);
    source.fill(Qt::black);
    QIcon const base(source);

    auto differsAtSize = [&base](int size) {
        auto const lower = ajazz::app::makeBatteryStatusIcon(base, 40).pixmap(size, size).toImage();
        auto const higher =
            ajazz::app::makeBatteryStatusIcon(base, 80).pixmap(size, size).toImage();
        for (int y = 0; y < lower.height(); ++y) {
            for (int x = 0; x < lower.width(); ++x) {
                if (lower.pixel(x, y) != higher.pixel(x, y)) {
                    return true;
                }
            }
        }
        return false;
    };

    CHECK(differsAtSize(16));
    CHECK(differsAtSize(22));
    CHECK(differsAtSize(32));
    CHECK(differsAtSize(64));
}
