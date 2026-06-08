// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file controller_token_test.cpp
 * @brief EVENT-04: Knob <-> Encoder controller-token normalization audit (Phase 34).
 *
 * Two assertions guard the "no encoder/touch event silently dropped" invariant
 * (T-34-02-02):
 *
 *  (1) Manifest boundary: affordanceMask() maps BOTH "Knob" and "Encoder" to the
 *      same encoder controller (Affordance::Dial). This is the ONLY sanctioned
 *      acceptance of the "Knob" token (plugin_manifest.cpp:144).
 *
 *  (2) Wire emission: a representative encoder emission (dialRotate) carries the
 *      NORMALIZED controller "Encoder" -- never the literal "Knob". Asserted against
 *      the REAL bridge envelope captured off a loopback WebSocket client.
 *
 * The verify step complements this with a grep gate asserting that the only "Knob"
 * literal under the src/app/src C++ sources is the manifest acceptance line.
 *
 * CLAUDE.md: ASCII-only TEST_CASE names/tags. Feature-token-prefixed titles so
 * `ctest -R controller_token` matches by name (Wave-1 idiom).
 */
#include "ajazz/core/device.hpp"
#include "plugin_device_bridge.hpp"
#include "plugin_manifest.hpp"
#include "sd_plugin_server.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QWebSocket>

#include <catch2/catch_test_macros.hpp>

namespace {

QCoreApplication* ensureQCoreApp() {
    static QCoreApplication* app = []() {
        static int argc = 0;
        static char* argv[] = {nullptr};
        return new QCoreApplication(argc, argv);
    }();
    return app;
}

void pump(int ms = 300) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, ms);
    QCoreApplication::processEvents(QEventLoop::AllEvents, ms);
}

bool waitForSpy(QSignalSpy& spy, int timeout_ms = 3000) {
    return spy.wait(timeout_ms) || spy.count() > 0;
}

bool connectAndRegister(QWebSocket& client,
                        ajazz::app::SdPluginServer& server,
                        QString const& pluginUuid,
                        QSignalSpy& registeredSpy) {
    QSignalSpy connSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    if (!waitForSpy(connSpy)) {
        return false;
    }
    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"%1"})").arg(pluginUuid));
    return waitForSpy(registeredSpy);
}

} // namespace

// ---------------------------------------------------------------------------
// EVENT-04 (1): manifest boundary maps Knob and Encoder to the same controller
// ---------------------------------------------------------------------------

TEST_CASE("controller_token manifest maps Knob and Encoder to the same encoder controller",
          "[event-04][controller_token][manifest]") {
    using ajazz::app::Affordance;
    using ajazz::app::affordanceMask;

    int const dialBit = static_cast<int>(Affordance::Dial);

    // Both vocabularies set the encoder (Dial) affordance bit.
    int const knobMask = affordanceMask(QStringList{QStringLiteral("Knob")});
    int const encoderMask = affordanceMask(QStringList{QStringLiteral("Encoder")});

    CHECK((knobMask & dialBit) != 0);
    CHECK((encoderMask & dialBit) != 0);

    // Knob and Encoder normalize to the IDENTICAL affordance bitmask.
    CHECK(knobMask == encoderMask);

    // Neither sets the Keypad or TouchZone bit (encoder-only tokens).
    CHECK((knobMask & static_cast<int>(Affordance::Key)) == 0);
    CHECK((knobMask & static_cast<int>(Affordance::TouchZone)) == 0);
}

// ---------------------------------------------------------------------------
// EVENT-04 (2): a representative encoder emission carries "Encoder", never "Knob"
// ---------------------------------------------------------------------------

TEST_CASE("controller_token encoder emission carries Encoder and never Knob on the wire",
          "[event-04][controller_token][plugin]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    ajazz::app::ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 0; // encoder index 0
    ctx.controller = QStringLiteral("Encoder");
    ctx.actionUUID = QStringLiteral("com.test.plug.enc.action");
    ctx.pluginUuid = QStringLiteral("com.test.plug");
    [[maybe_unused]] auto encCtxId = bridge->registry().registerContext(ctx);

    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    ajazz::core::DeviceEvent ev;
    ev.kind = ajazz::core::DeviceEvent::Kind::EncoderTurned;
    ev.index = 0;
    ev.value = -3; // signed delta (CCW)
    bridge->onDeviceEvent(QStringLiteral("akp05e"), ev);
    pump(500);

    QJsonObject dialRotate;
    for (auto const& args : msgSpy) {
        auto const obj = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("event")).toString() == QStringLiteral("dialRotate")) {
            dialRotate = obj;
            break;
        }
    }
    REQUIRE_FALSE(dialRotate.isEmpty());

    auto const payload = dialRotate.value(QStringLiteral("payload")).toObject();
    auto const controller = payload.value(QStringLiteral("controller")).toString();
    CHECK(controller == QStringLiteral("Encoder"));
    CHECK(controller != QStringLiteral("Knob"));
}
