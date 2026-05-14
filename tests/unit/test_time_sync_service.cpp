// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_time_sync_service.cpp
 * @brief Unit tests for ajazz::app::TimeSyncService (Plan 05-04).
 *
 * The service is decoupled from DeviceRegistry via a DeviceLookup
 * functor, so we feed it hand-crafted IDevice mocks that either
 * implement IClockCapable (returning a fixed TimeSyncResult) or don't
 * implement it at all. Covers:
 *
 *   * Ok / NotImplemented / IoError → correct signal emission.
 *   * dynamic_cast<IClockCapable*> returns nullptr (mock device without
 *     IClockCapable) → syncFailed with "does not advertise a clock
 *     surface" (Pitfall 2 null-cast path).
 *   * autoSync persistence semantics (load + setter + autoSyncChanged
 *     emission idempotency).
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "qt_app_fixture.hpp"
#include "time_sync_service.hpp"

#include <QSettings>
#include <QSignalSpy>
#include <QString>

#include <chrono>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

/// Minimal IDevice mock — implements the 8 pure-virtuals as no-ops so
/// the test can exercise the dynamic_cast<IClockCapable*> path on a
/// device that does NOT advertise the clock capability.
class MockDevice : public ajazz::core::IDevice {
public:
    explicit MockDevice(ajazz::core::DeviceDescriptor d) : m_desc(std::move(d)) {}

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

private:
    ajazz::core::DeviceDescriptor m_desc;
    bool m_open{false};
};

/// IDevice + IClockCapable mock — `lastResult` is the value returned by
/// every setTime call; `callCount` tracks invocations.
class MockClockDevice : public MockDevice, public ajazz::core::IClockCapable {
public:
    using MockDevice::MockDevice;

    ajazz::core::TimeSyncResult lastResult{ajazz::core::TimeSyncResult::Ok};
    int callCount{0};

    [[nodiscard]] ajazz::core::TimeSyncResult
    setTime(std::chrono::system_clock::time_point) override {
        ++callCount;
        return lastResult;
    }
};

ajazz::core::DeviceDescriptor descFor(QString const& codename) {
    return ajazz::core::DeviceDescriptor{
        .vendorId = 0x1234,
        .productId = 0x5678,
        .family = ajazz::core::DeviceFamily::StreamDeck,
        .model = "Mock Device",
        .codename = codename.toStdString(),
        .hasClock = true,
    };
}

} // namespace

TEST_CASE("TimeSyncService: setSystemTimeOn emits syncSucceeded on Ok", "[time-sync]") {
    ajazz::tests::qtApp();

    auto mock = std::make_shared<MockClockDevice>(descFor("akp153"));
    mock->lastResult = ajazz::core::TimeSyncResult::Ok;

    ajazz::app::TimeSyncService svc(
        [mock](QString const& cn) -> std::shared_ptr<ajazz::core::IDevice> {
            return cn == QStringLiteral("akp153") ? std::shared_ptr<ajazz::core::IDevice>(mock)
                                                  : nullptr;
        },
        nullptr);

    QSignalSpy okSpy(&svc, &ajazz::app::TimeSyncService::syncSucceeded);
    QSignalSpy failSpy(&svc, &ajazz::app::TimeSyncService::syncFailed);

    svc.setSystemTimeOn(QStringLiteral("akp153"));

    REQUIRE(mock->callCount == 1);
    REQUIRE(okSpy.count() == 1);
    REQUIRE(failSpy.count() == 0);
    REQUIRE(okSpy.first().at(0).toString() == QStringLiteral("akp153"));
}

TEST_CASE("TimeSyncService: setSystemTimeOn emits syncFailed on NotImplemented", "[time-sync]") {
    ajazz::tests::qtApp();

    auto mock = std::make_shared<MockClockDevice>(descFor("akp03"));
    mock->lastResult = ajazz::core::TimeSyncResult::NotImplemented;

    ajazz::app::TimeSyncService svc(
        [mock](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return mock; }, nullptr);

    QSignalSpy okSpy(&svc, &ajazz::app::TimeSyncService::syncSucceeded);
    QSignalSpy failSpy(&svc, &ajazz::app::TimeSyncService::syncFailed);

    svc.setSystemTimeOn(QStringLiteral("akp03"));

    REQUIRE(mock->callCount == 1);
    REQUIRE(okSpy.count() == 0);
    REQUIRE(failSpy.count() == 1);
    REQUIRE(failSpy.first().at(0).toString() == QStringLiteral("akp03"));
    REQUIRE(failSpy.first().at(1).toString().contains(QStringLiteral("not yet implemented")));
}

TEST_CASE("TimeSyncService: setSystemTimeOn emits syncFailed on IoError", "[time-sync]") {
    ajazz::tests::qtApp();

    auto mock = std::make_shared<MockClockDevice>(descFor("akp05"));
    mock->lastResult = ajazz::core::TimeSyncResult::IoError;

    ajazz::app::TimeSyncService svc(
        [mock](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return mock; }, nullptr);

    QSignalSpy failSpy(&svc, &ajazz::app::TimeSyncService::syncFailed);

    svc.setSystemTimeOn(QStringLiteral("akp05"));

    REQUIRE(mock->callCount == 1);
    REQUIRE(failSpy.count() == 1);
    REQUIRE(failSpy.first().at(0).toString() == QStringLiteral("akp05"));
    REQUIRE(failSpy.first().at(1).toString().contains(QStringLiteral("HID write failed")));
}

TEST_CASE("TimeSyncService: setSystemTimeOn fails cleanly when device is offline", "[time-sync]") {
    ajazz::tests::qtApp();

    ajazz::app::TimeSyncService svc(
        [](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return nullptr; }, nullptr);

    QSignalSpy failSpy(&svc, &ajazz::app::TimeSyncService::syncFailed);
    svc.setSystemTimeOn(QStringLiteral("missing"));

    REQUIRE(failSpy.count() == 1);
    REQUIRE(failSpy.first().at(1).toString().contains(QStringLiteral("not currently connected")));
}

TEST_CASE("TimeSyncService: setSystemTimeOn fails cleanly when device has no IClockCapable",
          "[time-sync]") {
    ajazz::tests::qtApp();

    // Pitfall 2 null-cast case: dynamic_cast<IClockCapable*> returns
    // nullptr for a MockDevice that does NOT inherit IClockCapable. The
    // service must emit syncFailed with "does not advertise a clock
    // surface" and the cast site's null-check (within 3 lines below the
    // cast in time_sync_service.cpp) must short-circuit before any
    // setTime call.
    auto mock = std::make_shared<MockDevice>(descFor("not-capable"));

    ajazz::app::TimeSyncService svc(
        [mock](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return mock; }, nullptr);

    QSignalSpy okSpy(&svc, &ajazz::app::TimeSyncService::syncSucceeded);
    QSignalSpy failSpy(&svc, &ajazz::app::TimeSyncService::syncFailed);

    svc.setSystemTimeOn(QStringLiteral("not-capable"));

    REQUIRE(okSpy.count() == 0);
    REQUIRE(failSpy.count() == 1);
    REQUIRE(failSpy.first().at(1).toString().contains(
        QStringLiteral("does not advertise a clock surface")));
}

TEST_CASE("TimeSyncService: onDeviceArrived honours autoSync flag", "[time-sync]") {
    ajazz::tests::qtApp();

    auto mock = std::make_shared<MockClockDevice>(descFor("akp153"));
    ajazz::app::TimeSyncService svc(
        [mock](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return mock; }, nullptr);

    // Default is autoSync=false → arrival is a no-op.
    svc.setAutoSync(false);
    svc.onDeviceArrived(QStringLiteral("akp153"));
    REQUIRE(mock->callCount == 0);

    // Toggle on → arrival pushes setTime.
    svc.setAutoSync(true);
    svc.onDeviceArrived(QStringLiteral("akp153"));
    REQUIRE(mock->callCount == 1);

    // Restore default so the QSettings backing stays clean between TUs.
    svc.setAutoSync(false);
}

TEST_CASE("TimeSyncService: setAutoSync emits autoSyncChanged exactly once per change",
          "[time-sync]") {
    ajazz::tests::qtApp();

    ajazz::app::TimeSyncService svc(
        [](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return nullptr; }, nullptr);

    // Start from a known false baseline regardless of prior test order.
    svc.setAutoSync(false);

    QSignalSpy spy(&svc, &ajazz::app::TimeSyncService::autoSyncChanged);
    svc.setAutoSync(true);
    svc.setAutoSync(true); // idempotent — no second emission
    svc.setAutoSync(false);

    REQUIRE(spy.count() == 2);
    REQUIRE(spy.at(0).at(0).toBool() == true);
    REQUIRE(spy.at(1).at(0).toBool() == false);

    // Leave the QSettings backing clean.
    svc.setAutoSync(false);
}
