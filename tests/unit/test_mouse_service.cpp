// SPDX-License-Identifier: GPL-3.0-or-later
/** @file test_mouse_service.cpp @brief TDD coverage for MouseService failure contract. */
#include "mouse_service.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("MouseService returns a typed unavailable snapshot", "[mouse-service]") {
    ajazz::app::MouseService service(
        [](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return nullptr; }, nullptr);

    auto const snapshot = service.current(QStringLiteral("ajazz_24g_8k"));
    REQUIRE(snapshot.value(QStringLiteral("available")).toBool() == false);
    CHECK(snapshot.value(QStringLiteral("pollingRate")).toInt() == 125);
    CHECK(snapshot.value(QStringLiteral("liftOff")).toDouble() == 1.5);
    CHECK(snapshot.value(QStringLiteral("dpi")).toList().isEmpty());
}

TEST_CASE("MouseService rejects apply when the device is unavailable", "[mouse-service]") {
    ajazz::app::MouseService service(
        [](QString const&) -> std::shared_ptr<ajazz::core::IDevice> { return nullptr; }, nullptr);
    bool failed = false;
    QObject::connect(&service,
                     &ajazz::app::MouseService::failed,
                     [&](QString const&, QString const&) { failed = true; });

    CHECK_FALSE(service.apply(QStringLiteral("ajazz_24g_8k"), {800, 1200}, 1000, 2.0));
    CHECK(failed);
}
