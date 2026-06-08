// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_system_did_wake.cpp
 * @brief systemDidWakeUp host dispatch reaches subscribed plugins (EVENT-03,
 *        Phase 34-04).
 *
 * A synthetic wake (the injectable seam Application::dispatchSystemWake wraps)
 * fans out systemDidWakeUp via SdPluginServer::sendEvent to each REGISTERED
 * plugin only — never broadcast, never caching the socket (Pitfall 4). This
 * drives the dispatchSystemWakeTo() helper against a live loopback server with a
 * registered plugin and asserts the eventSent signal carries systemDidWakeUp.
 *
 * Tags: [system_did_wake] — select with:
 *   ctest --preset linux-release -R system_did_wake
 */
#include "app_event_dispatch.hpp"
#include "sd_plugin_server.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QUrl>
#include <QWebSocket>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::app;

namespace {

void ensureQCoreApp() {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char arg0[] = "system_did_wake";
        static char* argv[] = {arg0, nullptr};
        static QCoreApplication app(argc, argv);
    }
}

bool waitForSpy(QSignalSpy& spy, int timeout_ms = 3000) {
    auto until = QDateTime::currentMSecsSinceEpoch() + timeout_ms;
    while (spy.count() == 0 && QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return spy.count() > 0;
}

} // namespace

TEST_CASE("system_did_wake dispatch reaches a subscribed plugin", "[system_did_wake]") {
    ensureQCoreApp();

    SdPluginServer server;
    QSignalSpy startedSpy(&server, &SdPluginServer::started);
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));
    REQUIRE(waitForSpy(startedSpy));

    // Register a loopback plugin.
    QWebSocket client;
    QSignalSpy connectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(connectedSpy));
    QString const uuid = QStringLiteral("com.test.wake");
    client.sendTextMessage(QStringLiteral(R"({"event":"registerPlugin","uuid":"%1"})").arg(uuid));
    REQUIRE(waitForSpy(registeredSpy));

    // Synthetic wake -> fan out to the registered set.
    QSignalSpy eventSpy(&server, &SdPluginServer::eventSent);
    int const attempted = dispatchSystemWakeTo(&server, QSet<QString>{uuid});
    CHECK(attempted == 1);
    REQUIRE(waitForSpy(eventSpy));

    bool sawWake = false;
    for (auto const& args : eventSpy) {
        if (args.at(0).toString() == uuid &&
            args.at(1).toString() == QStringLiteral("systemDidWakeUp")) {
            sawWake = true;
        }
    }
    CHECK(sawWake);

    client.close();
    server.stop();
}

TEST_CASE("system_did_wake dispatch is a no-op for an empty registered set", "[system_did_wake]") {
    ensureQCoreApp();

    SdPluginServer server;
    QSignalSpy startedSpy(&server, &SdPluginServer::started);
    REQUIRE(server.start(0));
    REQUIRE(waitForSpy(startedSpy));

    // No registered plugins -> nothing attempted, no crash.
    int const attempted = dispatchSystemWakeTo(&server, QSet<QString>{});
    CHECK(attempted == 0);

    // A null server is also a safe no-op (defensive).
    CHECK(dispatchSystemWakeTo(nullptr, QSet<QString>{QStringLiteral("x")}) == 0);

    server.stop();
}
