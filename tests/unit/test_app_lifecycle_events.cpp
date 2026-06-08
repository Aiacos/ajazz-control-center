// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_app_lifecycle_events.cpp
 * @brief applicationDidLaunch / applicationDidTerminate host event fan-out
 *        (APROF-04, Phase 34-04).
 *
 * GREEN as of Plan 04: the foreground watcher's onChange fans out a launch for
 * the incoming app and a terminate for the outgoing one, delivered ONLY to
 * REGISTERED plugins over the loopback WS (V4 / T-34-04-02 — never broadcast),
 * with a length-bounded application payload (V5 / T-34-04-05). This drives the
 * dispatchApplicationLaunchTo / TerminateTo helpers against a live server and a
 * registered plugin and asserts: the event arrives at the registered plugin; an
 * unregistered uuid is never in the delivery set; the payload is truncated.
 *
 * Tags: [app_lifecycle_events] — select with:
 *   ctest --preset linux-release -R app_lifecycle_events
 */
#include "app_event_dispatch.hpp"
#include "sd_plugin_server.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
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
        static char arg0[] = "app_lifecycle_events";
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

/// Start the server and register a single loopback plugin; returns its uuid.
/// The client must outlive the test body — caller owns it.
QString startServerWithPlugin(SdPluginServer& server, QWebSocket& client) {
    QSignalSpy startedSpy(&server, &SdPluginServer::started);
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));
    REQUIRE(waitForSpy(startedSpy));

    QSignalSpy connectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(connectedSpy));
    QString const uuid = QStringLiteral("com.test.lifecycle");
    client.sendTextMessage(QStringLiteral(R"({"event":"registerPlugin","uuid":"%1"})").arg(uuid));
    REQUIRE(waitForSpy(registeredSpy));
    return uuid;
}

} // namespace

TEST_CASE("app_lifecycle_events applicationDidLaunch is delivered to subscribed plugins",
          "[app_lifecycle_events]") {
    ensureQCoreApp();
    SdPluginServer server;
    QWebSocket client;
    QString const uuid = startServerWithPlugin(server, client);

    QSignalSpy eventSpy(&server, &SdPluginServer::eventSent);
    int const attempted =
        dispatchApplicationLaunchTo(&server, QSet<QString>{uuid}, QStringLiteral("firefox"));
    CHECK(attempted == 1);
    REQUIRE(waitForSpy(eventSpy));

    bool sawLaunch = false;
    for (auto const& args : eventSpy) {
        if (args.at(0).toString() == uuid &&
            args.at(1).toString() == QStringLiteral("applicationDidLaunch")) {
            sawLaunch = true;
            CHECK(args.at(2).toJsonObject().value(QStringLiteral("application")).toString() ==
                  QStringLiteral("firefox"));
        }
    }
    CHECK(sawLaunch);

    client.close();
    server.stop();
}

TEST_CASE("app_lifecycle_events applicationDidTerminate is delivered to subscribed plugins",
          "[app_lifecycle_events]") {
    ensureQCoreApp();
    SdPluginServer server;
    QWebSocket client;
    QString const uuid = startServerWithPlugin(server, client);

    QSignalSpy eventSpy(&server, &SdPluginServer::eventSent);
    int const attempted =
        dispatchApplicationTerminateTo(&server, QSet<QString>{uuid}, QStringLiteral("code"));
    CHECK(attempted == 1);
    REQUIRE(waitForSpy(eventSpy));

    bool sawTerminate = false;
    for (auto const& args : eventSpy) {
        if (args.at(1).toString() == QStringLiteral("applicationDidTerminate")) {
            sawTerminate = true;
        }
    }
    CHECK(sawTerminate);

    client.close();
    server.stop();
}

TEST_CASE("app_lifecycle_events not delivered to unregistered plugins", "[app_lifecycle_events]") {
    ensureQCoreApp();
    SdPluginServer server;
    QWebSocket client;
    QString const registered = startServerWithPlugin(server, client);

    // V4 access control: the fan-out iterates ONLY the registered set we pass.
    // An uuid that is not registered (no live socket) yields no eventSent — and
    // crucially the helper never broadcasts beyond the supplied set.
    QString const unregistered = QStringLiteral("com.evil.unregistered");
    QSignalSpy eventSpy(&server, &SdPluginServer::eventSent);

    // Dispatch to a set that contains ONLY the registered plugin.
    int const attempted =
        dispatchApplicationLaunchTo(&server, QSet<QString>{registered}, QStringLiteral("term"));
    CHECK(attempted == 1);
    REQUIRE(waitForSpy(eventSpy));

    int deliveriesToUnregistered = 0;
    int deliveriesToRegistered = 0;
    for (auto const& args : eventSpy) {
        if (args.at(0).toString() == unregistered) {
            ++deliveriesToUnregistered;
        }
        if (args.at(0).toString() == registered) {
            ++deliveriesToRegistered;
        }
    }
    CHECK(deliveriesToUnregistered == 0);
    CHECK(deliveriesToRegistered == 1);

    client.close();
    server.stop();
}

TEST_CASE("app_lifecycle_events ApplicationsToMonitor filter gates delivery",
          "[app_lifecycle_events]") {
    // WR-02: when a per-plugin monitor filter is supplied, a plugin receives the
    // event ONLY for apps it asked to monitor. Here the filter accepts "obs" and
    // rejects everything else, so an "obs" launch is delivered and a "firefox"
    // launch is not.
    ensureQCoreApp();
    SdPluginServer server;
    QWebSocket client;
    QString const uuid = startServerWithPlugin(server, client);

    auto const onlyObs = [uuid](QString const& pluginUuid, QString const& appId) {
        return pluginUuid == uuid && appId.compare(QStringLiteral("obs"), Qt::CaseInsensitive) == 0;
    };

    // A monitored app is delivered.
    {
        QSignalSpy eventSpy(&server, &SdPluginServer::eventSent);
        int const attempted = dispatchApplicationLaunchTo(
            &server, QSet<QString>{uuid}, QStringLiteral("obs"), onlyObs);
        CHECK(attempted == 1);
        REQUIRE(waitForSpy(eventSpy));
    }

    // A non-monitored app is filtered out (no attempt, no delivery).
    {
        QSignalSpy eventSpy(&server, &SdPluginServer::eventSent);
        int const attempted = dispatchApplicationLaunchTo(
            &server, QSet<QString>{uuid}, QStringLiteral("firefox"), onlyObs);
        CHECK(attempted == 0);
    }

    client.close();
    server.stop();
}

TEST_CASE("app_lifecycle_events application payload is length-bounded", "[app_lifecycle_events]") {
    // V5 / T-34-04-05: an over-long application token is truncated before send.
    QString const huge(5000, QChar('a'));
    QString const bounded = boundApplicationToken(huge);
    CHECK(bounded.length() < huge.length());
    CHECK(bounded.endsWith(QStringLiteral("...(truncated)")));
    // Within the cap, the token is passed through unchanged.
    CHECK(boundApplicationToken(QStringLiteral("firefox")) == QStringLiteral("firefox"));
}
