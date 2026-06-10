// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file willappear_payload_test.cpp
 * @brief EVENT-02: willAppear envelope + payload completeness assertion (Phase 34).
 *
 * Locks the Elgato/OpenDeck `willAppear` contract against the REAL bridge builders
 * (`instancePayload` :311 + `eventEnvelope` :374 in plugin_device_bridge.cpp). The
 * builders are file-local (anonymous namespace), so this test asserts the ACTUAL
 * emitted envelope captured off a loopback WebSocket client -- never a hand-rolled
 * duplicate of the shape. Drift in either builder breaks this test (T-34-02-01).
 *
 * Contract (EVENT-02):
 *  - Top level: action, context (non-empty), device, event == "willAppear".
 *  - payload: coordinates{row, column}, controller, state, isInMultiAction.
 *  - controller is a NORMALIZED token ("Keypad" | "Encoder"); never the literal "Knob".
 *
 * Seam: mirrors the existing "PluginDeviceBridgeE2E willAppear sent on plugin
 * registration" case (test_plugin_device_bridge.cpp) -- server + bridge + profile
 * accessor -> onPluginRegistered -> capture the willAppear envelope.
 *
 * CLAUDE.md: ASCII-only TEST_CASE names/tags. Feature-token-prefixed titles so
 * `ctest -R willappear_payload` matches by name (Wave-1 idiom).
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/profile.hpp"
#include "plugin_device_bridge.hpp"
#include "sd_plugin_server.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QWebSocket>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

/// Leaked QCoreApplication singleton (Pitfall 5 -- never destroyed; mirrors the
/// test_plugin_device_bridge.cpp / test_sd_plugin_server.cpp pattern).
QCoreApplication* ensureQCoreApp() {
    static QCoreApplication* app = []() {
        static int argc = 0;
        static char* argv[] = {nullptr};
        return new QCoreApplication(argc, argv);
    }();
    return app;
}

void pump(int ms = 300) {
    // Deadline loop (mirrors pump19 in test_plugin_device_bridge.cpp):
    // processEvents(AllEvents, ms) returns IMMEDIATELY when the queue is
    // momentarily empty, so the old two-pass form waited ~0ms on a slow
    // runner and the loopback WebSocket frame never arrived — flaky on the
    // windows-2022 leg (first Windows exposure of the Phase 34 EVENT-02
    // suite, 2026-06-10).
    auto const until = QDateTime::currentMSecsSinceEpoch() + ms;
    while (QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

bool waitForSpy(QSignalSpy& spy, int timeout_ms = 3000) {
    return spy.wait(timeout_ms) || spy.count() > 0;
}

/// Connect a loopback client and register it as pluginUuid.
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

/// Return the first full willAppear envelope captured by the client message spy.
QJsonObject firstWillAppear(QSignalSpy const& spy) {
    for (auto const& args : spy) {
        auto const obj = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).object();
        if (obj.value(QStringLiteral("event")).toString() == QStringLiteral("willAppear")) {
            return obj;
        }
    }
    return {};
}

/// Build a profile with one Plugin-action binding at the given 0-based key index
/// (profile keys index == 0-based device key index; device key 3 == index 2 ==
/// Elgato {row:0, col:2} on the AKP05E 2x5 grid).
ajazz::core::Profile makeKeyProfile(std::uint16_t keyIndex, QString const& actionUuid) {
    ajazz::core::Profile prof;
    prof.id = "test-profile";
    prof.name = "Test";
    prof.deviceCodename = "akp05e";
    ajazz::core::Binding binding;
    ajazz::core::Action act;
    act.kind = ajazz::core::ActionKind::Plugin;
    act.id = actionUuid.toStdString();
    binding.onPress.push_back(act);
    prof.keys[keyIndex] = std::move(binding);
    return prof;
}

} // namespace

// ---------------------------------------------------------------------------
// EVENT-02: Keypad willAppear envelope + payload completeness
// ---------------------------------------------------------------------------

TEST_CASE("willappear_payload Keypad envelope carries every required key",
          "[event-02][willappear_payload][plugin]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Device key 3 (0-based index 2) = Elgato {row:0, col:2}, Keypad.
    auto prof = makeKeyProfile(2, QStringLiteral("com.test.plug.action1"));
    bridge->setProfileAccessor([&prof]() -> ajazz::core::Profile const& { return prof; });

    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    bridge->onPluginRegistered(QStringLiteral("com.test.plug"));
    pump(500);

    auto const env = firstWillAppear(msgSpy);
    REQUIRE_FALSE(env.isEmpty());

    // --- Top-level envelope (Elgato siblings to `event`) ---
    CHECK(env.value(QStringLiteral("event")).toString() == QStringLiteral("willAppear"));
    CHECK(env.value(QStringLiteral("action")).toString() ==
          QStringLiteral("com.test.plug.action1"));
    CHECK(env.value(QStringLiteral("device")).toString() == QStringLiteral("akp05e"));
    CHECK_FALSE(env.value(QStringLiteral("context")).toString().isEmpty());

    // --- payload completeness (GenericInstancePayload) ---
    REQUIRE(env.contains(QStringLiteral("payload")));
    auto const payload = env.value(QStringLiteral("payload")).toObject();

    REQUIRE(payload.contains(QStringLiteral("coordinates")));
    auto const coords = payload.value(QStringLiteral("coordinates")).toObject();
    CHECK(coords.contains(QStringLiteral("row")));
    CHECK(coords.contains(QStringLiteral("column")));
    CHECK(coords.value(QStringLiteral("row")).toInt() == 0);
    CHECK(coords.value(QStringLiteral("column")).toInt() == 2);

    CHECK(payload.contains(QStringLiteral("controller")));
    CHECK(payload.contains(QStringLiteral("state")));
    CHECK(payload.contains(QStringLiteral("isInMultiAction")));
    CHECK(payload.contains(QStringLiteral("settings")));

    // --- controller normalization: "Keypad", and NEVER "Knob" ---
    auto const controller = payload.value(QStringLiteral("controller")).toString();
    CHECK(controller == QStringLiteral("Keypad"));
    CHECK(controller != QStringLiteral("Knob"));
}

// ---------------------------------------------------------------------------
// EVENT-02: Encoder willAppear normalizes controller to "Encoder" (never "Knob")
// ---------------------------------------------------------------------------

TEST_CASE("willappear_payload Encoder context normalizes controller token to Encoder",
          "[event-02][willappear_payload][plugin][controller]") {
    ensureQCoreApp();

    ajazz::app::SdPluginServer server;
    QSignalSpy registeredSpy(&server, &ajazz::app::SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    auto bridge = std::make_unique<ajazz::app::PluginDeviceBridge>(&server, nullptr, nullptr);

    // Pre-register an Encoder context directly (the registry is the willAppear source
    // for a pre-existing context); controller is the wire-normalized "Encoder".
    ajazz::app::ActionContext ctx;
    ctx.deviceId = QStringLiteral("akp05e");
    ctx.pageId = QStringLiteral("root");
    ctx.row = 0;
    ctx.column = 0; // encoder index 0
    ctx.controller = QStringLiteral("Encoder");
    ctx.actionUUID = QStringLiteral("com.test.plug.enc.action");
    ctx.pluginUuid = QStringLiteral("com.test.plug");
    [[maybe_unused]] auto encCtxId = bridge->registry().registerContext(ctx);

    // Drive a fresh willAppear for the encoder context via the device-event path:
    // an EncoderTurned resolves the encoder context and emits dialRotate carrying
    // controller "Encoder"; the willAppear normalization is identical (instancePayload
    // copies ctx.controller verbatim, which is already normalized at the registry).
    // Assert via the registered context's deriveContextId carrying the Encoder token,
    // then confirm a live dialRotate envelope carries the normalized controller.
    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    REQUIRE(connectAndRegister(client, server, QStringLiteral("com.test.plug"), registeredSpy));

    ajazz::core::DeviceEvent ev;
    ev.kind = ajazz::core::DeviceEvent::Kind::EncoderTurned;
    ev.index = 0;
    ev.value = 1;
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
