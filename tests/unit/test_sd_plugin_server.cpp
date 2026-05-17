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
QCoreApplication* ensureQCoreApp() {
    static int argc = 0;
    static char* argv[] = {nullptr};
    static QCoreApplication app(argc, argv);
    return &app;
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
    QString const registerMsg = QStringLiteral(R"({"event":"registerPlugin","uuid":"%1"})")
                                    .arg(QLatin1String(kPluginUuid));
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
    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.x"})"));
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

TEST_CASE("SdPluginServer surfaces unknown events via unhandledEventReceived",
          "[plugin-server][extensions]") {
    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy unhandledSpy(&server, &SdPluginServer::unhandledEventReceived);
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));
    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.x"})"));
    REQUIRE(waitForSpy(registeredSpy));
    // setBG is an AJAZZ-only extension - MVP doesn't implement it, but the
    // server must surface it via the unhandled signal so the app layer can
    // log / extend without losing the event.
    client.sendTextMessage(
        QStringLiteral(R"({"event":"setBG","payload":{"color":"#FF0000"}})"));
    REQUIRE(waitForSpy(unhandledSpy));

    REQUIRE(unhandledSpy.count() == 1);
    REQUIRE(unhandledSpy.first().at(1).toString() == QStringLiteral("setBG"));
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
