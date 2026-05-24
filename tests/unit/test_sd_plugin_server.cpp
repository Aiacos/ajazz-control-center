// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_sd_plugin_server.cpp
 * @brief MVP tests for SdPluginServer - loopback binding + registration handshake.
 *
 * Verifies the load-bearing security invariant (loopback-only binding) and
 * the Elgato v6 register handshake (registerPlugin -> pluginRegistered signal).
 */
#include "sd_plugin_server.hpp"

#include <QCoreApplication>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QWebSocket>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::SdPluginServer;

namespace {

/// Pump the Qt event loop for `ms` milliseconds so async network events
/// (connect / handshake / receive) drain into our spies.
void pump(int ms) {
    auto until = QDateTime::currentMSecsSinceEpoch() + ms;
    while (QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

/// Lazy QCoreApplication singleton - Qt signals + the WebSocket stack need
/// a running event loop. Catch2 binaries don't always have one.
///
/// IMPORTANT: the QCoreApplication is intentionally heap-allocated and
/// never deleted. ctest's `Catch2 :: ParseAndAddCatchTests` splits each
/// TEST_CASE into its own subprocess; at process exit the static-storage
/// destructor of a stack-allocated QCoreApplication races against
/// QtWebSockets' own background thread (still finishing socket teardown)
/// and SIGSEGVs. By leaking it we let the OS reclaim memory after Qt's
/// threads have already been forcibly torn down by exit(), which is the
/// canonical pattern for Qt test helpers (see Qt docs §"Static
/// destruction order fiasco").
QCoreApplication* ensureQCoreApp() {
    static QCoreApplication* app = []() {
        static int argc = 0;
        static char* argv[] = {nullptr};
        return new QCoreApplication(argc, argv);
    }();
    return app;
}

} // namespace

TEST_CASE("SdPluginServer binds loopback only - never QHostAddress::Any",
          "[plugin-server][security][loopback-only]") {
    ensureQCoreApp();
    SdPluginServer server;
    REQUIRE(server.bindAddress() == QHostAddress(QHostAddress::LocalHost));
    REQUIRE(server.bindAddress() != QHostAddress(QHostAddress::Any));
    REQUIRE(server.bindAddress() != QHostAddress(QHostAddress::AnyIPv4));
    REQUIRE(server.bindAddress() != QHostAddress(QHostAddress::AnyIPv6));
    // There is NO setter that would broaden the bind address - confirm by
    // grepping the public API (compile-time check: any added setter would
    // break the security contract and require explicit roadmap discussion).
}

TEST_CASE("SdPluginServer starts on an OS-assigned port and stops cleanly",
          "[plugin-server][lifecycle]") {
    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy startedSpy(&server, &SdPluginServer::started);
    QSignalSpy stoppedSpy(&server, &SdPluginServer::stopped);

    REQUIRE_FALSE(server.isListening());
    REQUIRE(server.serverPort() == 0);

    REQUIRE(server.start(0));
    REQUIRE(server.isListening());
    REQUIRE(server.serverPort() != 0);
    REQUIRE(startedSpy.count() == 1);

    server.stop();
    REQUIRE_FALSE(server.isListening());
    REQUIRE(server.serverPort() == 0);
    REQUIRE(stoppedSpy.count() == 1);
}

/// Wait up to `timeout_ms` for `spy` to accumulate at least one entry.
/// Returns true once the count is non-zero; false on timeout. Drains the
/// event loop while waiting, so async network events get processed.
bool waitForSpy(QSignalSpy& spy, int timeout_ms = 3000) {
    auto until = QDateTime::currentMSecsSinceEpoch() + timeout_ms;
    while (spy.count() == 0 && QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return spy.count() > 0;
}

TEST_CASE("SdPluginServer accepts a WebSocket connection on the bound port",
          "[plugin-server][lifecycle]") {
    ensureQCoreApp();
    SdPluginServer server;
    REQUIRE(server.start(0));
    auto const port = server.serverPort();

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port)));
    REQUIRE(waitForSpy(clientConnectedSpy));
    REQUIRE(clientConnectedSpy.count() == 1);
    // The Elgato v6 protocol distinguishes "socket connected" from
    // "plugin registered" - the registerPlugin handshake must arrive
    // first. connectedPluginCount() counts only registered slots.
    REQUIRE(server.connectedPluginCount() == 0);

    client.close();
    pump(200);
}

TEST_CASE("SdPluginServer registerPlugin handshake emits pluginRegistered",
          "[plugin-server][handshake][elgato-v6]") {
    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));

    constexpr char const* kPluginUuid = "com.test.myplugin.action1";
    QString const registerMsg =
        QStringLiteral(R"({"event":"registerPlugin","uuid":"%1"})").arg(QLatin1String(kPluginUuid));
    client.sendTextMessage(registerMsg);
    REQUIRE(waitForSpy(registeredSpy));

    REQUIRE(registeredSpy.count() == 1);
    REQUIRE(registeredSpy.first().at(0).toString() == QLatin1String(kPluginUuid));
    REQUIRE(server.connectedPluginCount() == 1);
}

TEST_CASE("SdPluginServer action message emits actionReceived with parsed JSON",
          "[plugin-server][actions][elgato-v6]") {
    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy actionSpy(&server, &SdPluginServer::actionReceived);
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));
    client.sendTextMessage(QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.x"})"));
    REQUIRE(waitForSpy(registeredSpy));
    // setTitle is one of the 13 standard Elgato actions.
    client.sendTextMessage(
        QStringLiteral(R"({"event":"setTitle","context":"abc","payload":{"title":"Hi"}})"));
    REQUIRE(waitForSpy(actionSpy));

    REQUIRE(actionSpy.count() == 1);
    auto const args = actionSpy.first();
    REQUIRE(args.at(0).toString() == QStringLiteral("com.test.x"));
    auto const action = args.at(1).value<QJsonObject>();
    REQUIRE(action.value(QStringLiteral("event")).toString() == QStringLiteral("setTitle"));
    REQUIRE(action.value(QStringLiteral("context")).toString() == QStringLiteral("abc"));
}

TEST_CASE("SdPluginServer routes setBG to actionReceived and genuinely-unknown events to "
          "unhandledEventReceived",
          "[plugin-server][extensions]") {
    // INVERTED from the MVP test: setBG is now a routed AJAZZ-only action (17-01).
    // Only a truly-unknown event name should still reach unhandledEventReceived
    // (forward-compat trace; T-17-FWD).
    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy unhandledSpy(&server, &SdPluginServer::unhandledEventReceived);
    QSignalSpy actionSpy(&server, &SdPluginServer::actionReceived);
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));
    client.sendTextMessage(QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.x"})"));
    REQUIRE(waitForSpy(registeredSpy));

    // setBG is now a routed AJAZZ-only action - it must reach actionReceived.
    client.sendTextMessage(
        QStringLiteral(R"({"event":"setBG","context":"c1","payload":{"color":"#FF0000"}})"));
    REQUIRE(waitForSpy(actionSpy));
    REQUIRE(actionSpy.count() == 1);
    REQUIRE(
        actionSpy.first().at(1).value<QJsonObject>().value(QStringLiteral("event")).toString() ==
        QStringLiteral("setBG"));

    // A genuinely-unknown event still surfaces via unhandledEventReceived (forward-compat).
    client.sendTextMessage(QStringLiteral(R"({"event":"someEventThatDoesNotExist","payload":{}})"));
    REQUIRE(waitForSpy(unhandledSpy));
    REQUIRE(unhandledSpy.count() == 1);
    REQUIRE(unhandledSpy.first().at(1).toString() == QStringLiteral("someEventThatDoesNotExist"));
}

TEST_CASE("SdPluginProtocolTest all routed actions route none unhandled",
          "[plugin-server][actions]") {
    // This table must stay in lockstep with kRoutedActions in sd_plugin_server.cpp.
    // The expected count is derived from this list's size (currently 41), NOT a hardcoded
    // literal. A comment below asserts it must equal kRoutedActions.size() in the source.
    //
    // Standard Elgato routed (15) — spec 4.3, excluding registerPlugin /
    // registerPropertyInspector (handled in the earlier branch):
    static constexpr char const* kAllRoutedNames[] = {
        "setTitle",                // standard
        "setImage",                // standard
        "setState",                // standard
        "showAlert",               // standard
        "showOk",                  // standard
        "getSettings",             // standard
        "setSettings",             // standard
        "getGlobalSettings",       // standard
        "setGlobalSettings",       // standard
        "switchToProfile",         // standard
        "sendToPropertyInspector", // standard
        "sendToPlugin",            // standard
        "openUrl",                 // standard
        "logMessage",              // standard
        "setFeedback",             // standard (Stream Deck Plus encoder feedback)
        // AJAZZ-only (26) — spec 4.3 "Standard Elgato? AJAZZ-only":
        "setBG",
        "setBackground",
        "clearIcon",
        "sendToDevice",
        "openTouchbarSecondaryMenu",
        "exitTouchbarSecondaryMenu",
        "enterGatheringEvent",
        "registrationScreenSaverEvent",
        "unRegistrationScreenSaverEvent",
        "setText",
        "lockScreen",
        "unLockScreen",
        "getScreenshot",
        "getSystemAudioVolume",
        "getUserInfo",
        "setAcImgTop",
        "onSwitchToFolderProfile",
        "onSwitchFromFolderProfile",
        "deleteAction",
        "stopBackground",
        "exitFullScreen",
        "touchTap",
        "getDetectedSensorsData",
        "startAudioCapture",
        "stopAudioCapture",
        "sendUserInfo",
    };
    // kAllRoutedNames.size() == 41 (currently); must equal kRoutedActions.size() in source.
    constexpr int kExpectedCount =
        static_cast<int>(sizeof(kAllRoutedNames) / sizeof(kAllRoutedNames[0]));

    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy actionSpy(&server, &SdPluginServer::actionReceived);
    QSignalSpy unhandledSpy(&server, &SdPluginServer::unhandledEventReceived);
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));
    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.allrouted"})"));
    REQUIRE(waitForSpy(registeredSpy));

    for (auto const* name : kAllRoutedNames) {
        QString const msg = QStringLiteral(R"({"event":"%1","context":"ctx1","payload":{}})")
                                .arg(QLatin1String(name));
        client.sendTextMessage(msg);
    }
    // Drain until we have received all expected actions or timeout.
    auto until = QDateTime::currentMSecsSinceEpoch() + 5000;
    while (actionSpy.count() < kExpectedCount && QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }

    REQUIRE(unhandledSpy.count() == 0);
    REQUIRE(actionSpy.count() == kExpectedCount);
}

TEST_CASE("SdPluginProtocolTest envelope round trips event context device action payload",
          "[plugin-server][actions][envelope]") {
    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy actionSpy(&server, &SdPluginServer::actionReceived);
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));
    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.envelope"})"));
    REQUIRE(waitForSpy(registeredSpy));

    // All five envelope keys (spec 4.2): event / context / device / action / payload
    client.sendTextMessage(QStringLiteral(
        R"({"event":"setImage","context":"ctx-42","device":"dev-1","action":"com.test.plugin.action1","payload":{"image":"data:image/png;base64,abc="}})"));
    REQUIRE(waitForSpy(actionSpy));

    REQUIRE(actionSpy.count() == 1);
    auto const args = actionSpy.first();
    auto const obj = args.at(1).value<QJsonObject>();
    REQUIRE(obj.value(QStringLiteral("event")).toString() == QStringLiteral("setImage"));
    REQUIRE(obj.value(QStringLiteral("context")).toString() == QStringLiteral("ctx-42"));
    REQUIRE(obj.value(QStringLiteral("device")).toString() == QStringLiteral("dev-1"));
    REQUIRE(obj.value(QStringLiteral("action")).toString() ==
            QStringLiteral("com.test.plugin.action1"));
    REQUIRE(
        obj.value(QStringLiteral("payload")).toObject().value(QStringLiteral("image")).toString() ==
        QStringLiteral("data:image/png;base64,abc="));
}

TEST_CASE("SdPluginServer bind loopback only on random port",
          "[plugin-server][security][loopback-only]") {
    ensureQCoreApp();
    SdPluginServer server;
    REQUIRE(server.start(0));
    // PLUGIN-01: bind address is always LocalHost — never Any/AnyIPv4/AnyIPv6.
    REQUIRE(server.bindAddress() == QHostAddress(QHostAddress::LocalHost));
    REQUIRE(server.bindAddress() != QHostAddress(QHostAddress::Any));
    REQUIRE(server.bindAddress() != QHostAddress(QHostAddress::AnyIPv4));
    REQUIRE(server.bindAddress() != QHostAddress(QHostAddress::AnyIPv6));
    // OS-assigned port is non-zero.
    REQUIRE(server.serverPort() != 0);
}

TEST_CASE("SdPluginServer multiple sequential start/stop cycles do not leak ports",
          "[plugin-server][lifecycle]") {
    ensureQCoreApp();
    SdPluginServer server;
    for (int i = 0; i < 3; ++i) {
        REQUIRE(server.start(0));
        REQUIRE(server.isListening());
        server.stop();
        REQUIRE_FALSE(server.isListening());
    }
}

TEST_CASE("SdPluginServer reclaims the connection slot on disconnect + same-UUID reconnect",
          "[plugin-server][lifecycle]") {
    // Exercises the erase-on-disconnect path (WR-07): the slot must be removed,
    // not nulled, so the count returns to zero and a same-UUID reconnect yields
    // exactly one live slot rather than a live one beside a dead {uuid,nullptr}.
    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    QSignalSpy disconnectedSpy(&server, &SdPluginServer::pluginDisconnected);
    REQUIRE(server.start(0));

    auto const url = QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort()));
    QString const registerMsg =
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.recycle"})");

    {
        QWebSocket client;
        QSignalSpy connectedSpy(&client, &QWebSocket::connected);
        client.open(url);
        REQUIRE(waitForSpy(connectedSpy));
        client.sendTextMessage(registerMsg);
        REQUIRE(waitForSpy(registeredSpy));
        REQUIRE(server.connectedPluginCount() == 1);

        client.close();
        REQUIRE(waitForSpy(disconnectedSpy));
    }
    // Slot reclaimed — count back to zero, no lingering dead row.
    REQUIRE(server.connectedPluginCount() == 0);

    registeredSpy.clear(); // waitForSpy returns on count()>0, so reset before reuse
    QWebSocket client2;
    QSignalSpy connectedSpy2(&client2, &QWebSocket::connected);
    client2.open(url);
    REQUIRE(waitForSpy(connectedSpy2));
    client2.sendTextMessage(registerMsg);
    REQUIRE(waitForSpy(registeredSpy));
    // Exactly one live slot for the reused UUID — never two.
    REQUIRE(server.connectedPluginCount() == 1);
}
