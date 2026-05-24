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
#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
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

// ---------------------------------------------------------------------------
// 17-02: host->plugin sendEvent seam (PLUGIN-04)
// ---------------------------------------------------------------------------

TEST_CASE("SdPluginProtocolTest roundTrip dialRotate carries ticks pressed controller",
          "[plugin-server][events][encoder]") {
    // RED: sendEvent does not exist yet — this fails to compile until 17-02 impl lands.
    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));

    // Register so the server has a live uuid->socket mapping.
    client.sendTextMessage(QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.x"})"));
    REQUIRE(waitForSpy(registeredSpy));

    // Spy on frames arriving at the client (host->plugin direction).
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);

    // Host sends a dialRotate event to the registered plugin.
    bool const sent =
        server.sendEvent(QStringLiteral("com.test.x"),
                         QStringLiteral("dialRotate"),
                         QJsonObject{{QStringLiteral("ticks"), 2},
                                     {QStringLiteral("pressed"), false},
                                     {QStringLiteral("controller"), QStringLiteral("Encoder")}});
    REQUIRE(sent);
    REQUIRE(waitForSpy(msgSpy));

    // Parse the received frame and check envelope shape.
    // Use msgSpy.last() so this test is resilient if a passHello frame
    // arrives before dialRotate once 17-03 lands.
    QString lastFrame;
    for (auto const& args : msgSpy) {
        auto const obj = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("event")).toString() == QStringLiteral("dialRotate")) {
            lastFrame = args.at(0).toString();
        }
    }
    REQUIRE_FALSE(lastFrame.isEmpty());
    auto const env = QJsonDocument::fromJson(lastFrame.toUtf8()).object();
    REQUIRE(env.value(QStringLiteral("event")).toString() == QStringLiteral("dialRotate"));
    auto const payload = env.value(QStringLiteral("payload")).toObject();
    REQUIRE(payload.value(QStringLiteral("ticks")).toInt() == 2);
    REQUIRE(payload.value(QStringLiteral("pressed")).toBool() == false);
    REQUIRE(payload.value(QStringLiteral("controller")).toString() == QStringLiteral("Encoder"));
}

TEST_CASE("SdPluginProtocolTest sendEvent returns false for unknown uuid",
          "[plugin-server][events]") {
    ensureQCoreApp();
    SdPluginServer server;
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));

    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);

    // No registerPlugin sent — uuid is unknown.
    bool const sent = server.sendEvent(
        QStringLiteral("com.not.registered"), QStringLiteral("keyDown"), QJsonObject{});
    REQUIRE_FALSE(sent);

    // Give any spurious frames a chance to arrive — none expected.
    pump(100);
    REQUIRE(msgSpy.count() == 0);
}

TEST_CASE("SdPluginProtocolTest host to plugin events arrive", "[plugin-server][events]") {
    // Representative subset of spec 4.4 host->plugin event names.
    // Each should arrive at the loopback client with the matching event field.
    static constexpr char const* kHostToPluginEvents[] = {
        "keyDown",
        "keyUp",
        "dialDown",
        "dialUp",
        "keyDownCord",
        "touchTap",
        "willAppear",
        "deviceDidConnect",
        "titleParametersDidChange",
        "didReceiveSettings",
    };

    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));

    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.eventsurface"})"));
    REQUIRE(waitForSpy(registeredSpy));

    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);

    constexpr int kCount =
        static_cast<int>(sizeof(kHostToPluginEvents) / sizeof(kHostToPluginEvents[0]));

    for (auto const* evtName : kHostToPluginEvents) {
        bool const sent = server.sendEvent(
            QStringLiteral("com.test.eventsurface"), QLatin1String(evtName), QJsonObject{});
        // All should return true for a registered uuid.
        REQUIRE(sent);
    }

    // Drain until all events arrive or timeout.
    auto const until = QDateTime::currentMSecsSinceEpoch() + 5000;
    while (msgSpy.count() < kCount && QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }

    // Collect received event names (filter by matching; passHello in 17-03 may arrive too).
    QSet<QString> receivedEvents;
    for (auto const& args : msgSpy) {
        auto const obj = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).object();
        receivedEvents.insert(obj.value(QStringLiteral("event")).toString());
    }

    for (auto const* evtName : kHostToPluginEvents) {
        REQUIRE(receivedEvents.contains(QLatin1String(evtName)));
    }
}

// ---------------------------------------------------------------------------
// 17-03: passHello / salt / challenge auth gate (PLUGIN-05)
// RED phase: these tests MUST FAIL until 17-03 implementation lands.
// ---------------------------------------------------------------------------

/// Helper: given a spy on textMessageReceived, find and return the first frame
/// whose "event" field matches the given name. Returns empty string on timeout.
static QString findFrameByEvent(QSignalSpy& spy, QString const& eventName, int timeout_ms = 3000) {
    auto const until = QDateTime::currentMSecsSinceEpoch() + timeout_ms;
    while (QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        for (auto const& args : spy) {
            auto const obj = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).object();
            if (obj.value(QStringLiteral("event")).toString() == eventName) {
                return args.at(0).toString();
            }
        }
    }
    return {};
}

TEST_CASE("PluginAuthTest passHello carries salt after registerPlugin",
          "[plugin-server][auth][handshake]") {
    // After registerPlugin (default no-password), the client must receive a
    // passHello frame whose payload.authentication.salt is a non-empty string.
    // RED: fails until passHello emission + PluginConnection.salt land in 17-03.
    ensureQCoreApp();
    SdPluginServer server;
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));

    // Send registerPlugin.
    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.auth.salt"})"));
    REQUIRE(waitForSpy(registeredSpy));

    // Wait for passHello frame.
    QString const frame = findFrameByEvent(msgSpy, QStringLiteral("passHello"));
    REQUIRE_FALSE(frame.isEmpty());

    // Parse and assert NESTED authentication.salt (per CONTEXT.md -- spec
    // §4.5 top-level salt is SUPERSEDED by §4.4 + CONTEXT nested shape).
    auto const env = QJsonDocument::fromJson(frame.toUtf8()).object();
    REQUIRE(env.value(QStringLiteral("event")).toString() == QStringLiteral("passHello"));
    // passHello uses the "payload" key (sent via sendEvent which wraps in payload).
    auto const payload = env.value(QStringLiteral("payload")).toObject();
    auto const auth = payload.value(QStringLiteral("authentication")).toObject();
    REQUIRE_FALSE(auth.value(QStringLiteral("salt")).toString().isEmpty());
}

TEST_CASE("PluginAuthTest acceptsCorrectChallenge", "[plugin-server][auth]") {
    // With a password configured, a correct sha256(password+salt) challenge is
    // accepted (socket stays open, no disconnected signal).
    // RED: fails until authentication handling + setPasswordForTesting land.
    ensureQCoreApp();
    SdPluginServer server;
    // setPasswordForTesting: test-only setter so the auth path is exercisable;
    // production default is empty = no-password-accept.
    server.setPasswordForTesting(QStringLiteral("pw"));

    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    QSignalSpy clientDisconnectedSpy(&client, &QWebSocket::disconnected);
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));

    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.auth.accept"})"));
    REQUIRE(waitForSpy(registeredSpy));

    // Receive passHello and extract salt.
    QString const frame = findFrameByEvent(msgSpy, QStringLiteral("passHello"));
    REQUIRE_FALSE(frame.isEmpty());
    auto const payload = QJsonDocument::fromJson(frame.toUtf8())
                             .object()
                             .value(QStringLiteral("payload"))
                             .toObject();
    QString const saltHex = payload.value(QStringLiteral("authentication"))
                                .toObject()
                                .value(QStringLiteral("salt"))
                                .toString();
    REQUIRE_FALSE(saltHex.isEmpty());

    // Compute expected challenge = sha256(password + salt) in UTF-8 bytes.
    // This mirrors challengeFor() in the implementation (17-03).
    QString const expected = QString::fromLatin1(
        QCryptographicHash::hash(QByteArray("pw") + saltHex.toUtf8(), QCryptographicHash::Sha256)
            .toHex());

    // Send the correct authentication reply.
    QString const authMsg =
        QStringLiteral(R"({"event":"authentication","challenge":"%1"})").arg(expected);
    client.sendTextMessage(authMsg);

    // Give time to process -- socket must NOT be closed.
    pump(300);
    REQUIRE(clientDisconnectedSpy.count() == 0);
    REQUIRE(client.state() == QAbstractSocket::ConnectedState);
}

TEST_CASE("PluginAuthTest rejectsAfter5BadAttempts", "[plugin-server][auth]") {
    // With a password configured, sending 5 wrong challenges closes the socket.
    // Fewer than 5 attempts must NOT close (assert still-connected after 4).
    // RED: fails until authentication rejection + kMaxAuthAttempts=5 land.
    ensureQCoreApp();
    SdPluginServer server;
    server.setPasswordForTesting(QStringLiteral("pw"));

    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    QSignalSpy clientDisconnectedSpy(&client, &QWebSocket::disconnected);
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));

    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"com.test.auth.reject"})"));
    REQUIRE(waitForSpy(registeredSpy));

    // Wait for passHello to arrive (ensures server is ready to process auth).
    QString const frame = findFrameByEvent(msgSpy, QStringLiteral("passHello"));
    REQUIRE_FALSE(frame.isEmpty());

    constexpr int kMaxAttempts = 5;

    // Send 4 wrong challenges -- socket must stay open.
    for (int i = 0; i < kMaxAttempts - 1; ++i) {
        client.sendTextMessage(QStringLiteral(R"({"event":"authentication","challenge":"wrong"})"));
        pump(80);
        REQUIRE(clientDisconnectedSpy.count() == 0);
        REQUIRE(client.state() == QAbstractSocket::ConnectedState);
    }

    // 5th bad attempt -- server must close the socket.
    client.sendTextMessage(QStringLiteral(R"({"event":"authentication","challenge":"wrong"})"));
    REQUIRE(waitForSpy(clientDisconnectedSpy, 3000));
    REQUIRE(clientDisconnectedSpy.count() >= 1);
}
