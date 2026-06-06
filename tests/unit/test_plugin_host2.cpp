// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_host2.cpp
 * @brief Wave-0 RED scaffold: pre-registration safety contract for IPluginHost2 (Phase 30).
 *
 * These two cases pin the HOST-02 contract BEFORE the implementation lands in Plan 02.
 * Both tests are expected to FAIL at this Wave-0 scaffold stage -- they turn GREEN in
 * Plan 30-02 when the sentinel-UUID guard and m_live pre-registration check are wired.
 *
 * A FAIL is the INTENDED, NON-BLOCKING output here.
 * RED_MISSING or UNEXPECTED_GREEN are the only real failures of this task.
 *
 * Phase: 30-plugin-host-modular-foundation / Plan 30-01 (HOST-02 Wave-0 scaffold)
 */
#include "node_runner.hpp"
#include "plugin_manager.hpp"
#include "sd_plugin_server.hpp"

#include <QCoreApplication>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWebSocket>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::app;

namespace {

/// Pump the Qt event loop for `ms` milliseconds so async network events drain.
void pump(int ms) {
    auto until = QDateTime::currentMSecsSinceEpoch() + ms;
    while (QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

/// Lazy QCoreApplication singleton (same pattern as test_sd_plugin_server.cpp).
QCoreApplication* ensureQCoreApp() {
    static QCoreApplication* app = []() {
        static int argc = 0;
        static char* argv[] = {nullptr};
        auto* a = new QCoreApplication(argc, argv);
        QCoreApplication::setOrganizationName(QStringLiteral("Aiacos"));
        QCoreApplication::setApplicationName(QStringLiteral("ajazz-control-center-tests-%1")
                                                 .arg(QCoreApplication::applicationPid()));
        return a;
    }();
    return app;
}

/// Wait up to timeout_ms for spy to accumulate at least one entry.
bool waitForSpy(QSignalSpy& spy, int timeout_ms = 3000) {
    auto until = QDateTime::currentMSecsSinceEpoch() + timeout_ms;
    while (spy.count() == 0 && QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return spy.count() > 0;
}

} // namespace

// ---------------------------------------------------------------------------
// HOST-02: disconnect-before-register leaves zero connected and does not crash
// ---------------------------------------------------------------------------

// GREEN after 30-02 (sentinel-UUID guard wired in SdPluginServer::onNewConnection +
// m_live.find guard in PluginManager::onProcessFailed).
//
// RED today because: when a plugin process fails before sending registerPlugin,
// PluginManager::onProcessFailed credits the crash unconditionally (no m_live guard),
// which means pre-registration exits count toward the 3-crash disable window even though
// the plugin never registered. The sentinel UUID (Plan 02) corrects both the server and
// manager sides.
TEST_CASE("disconnect-before-register leaves zero connected and does not crash",
          "[plugin-host2][host-02][red-scaffold]") {
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    qint64 fakeNow = 0;
    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return {}; };
    fakeProbe.queryVersion = [](QString const&) -> QString { return {}; };

    SdPluginServer server;
    QSignalSpy disconnectedSpy(&server, &SdPluginServer::pluginDisconnected);
    QSignalSpy registeredSpy(&server, &SdPluginServer::pluginRegistered);
    REQUIRE(server.start(0));

    PluginManager manager(
        scratch.path(), &server, fakeProbe, nullptr, [&fakeNow]() { return fakeNow; });
    QSignalSpy disabledSpy(&manager, &PluginManager::pluginDisabled);

    // Connect a real QWebSocket client to the server loopback port.
    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(clientConnectedSpy));

    // No registerPlugin sent -- the socket is pre-registration.
    // connectedPluginCount() must be 0 (registered count, not raw connection count).
    REQUIRE(server.connectedPluginCount() == 0);

    // Close without registering -- simulates a plugin that crashes before the handshake.
    client.close();
    pump(300);

    // pluginRegistered must never have fired.
    REQUIRE(registeredSpy.count() == 0);

    // pluginDisconnected must NOT have fired with a real (non-sentinel) UUID.
    // GREEN after 30-02: pluginDisconnected is suppressed for sentinel (pending) UUIDs.
    for (int i = 0; i < disconnectedSpy.count(); ++i) {
        QString const emittedUuid = disconnectedSpy.at(i).at(0).toString();
        // A sentinel UUID starts with "__pending__"; a real UUID must not be emitted.
        REQUIRE(emittedUuid.startsWith(QStringLiteral("__pending__")));
    }

    // After the disconnect, connectedPluginCount() must still be 0.
    REQUIRE(server.connectedPluginCount() == 0);

    // COMBINED HOST-02 assertion: simulate a plugin process failing before registerPlugin.
    // The pre-registration-exit must NOT count toward the 3-crash disable window.
    // The UUID "uuid-pre-reg" was never inserted into m_live (no registerPlugin arrived).
    // RED until 30-02 wires the m_live.find guard in onProcessFailed.
    QString const preRegUuid = QStringLiteral("uuid-pre-reg-disconnect");
    fakeNow = 0;
    manager.onProcessFailed(preRegUuid);
    fakeNow = 5000;
    manager.onProcessFailed(preRegUuid);
    fakeNow = 10000;
    manager.onProcessFailed(preRegUuid);
    pump(100);

    // pluginDisabled must NOT be emitted for a pre-registration exit.
    // RED until 30-02 wires the m_live.find guard.
    REQUIRE(disabledSpy.count() == 0);

    server.stop();
}

// ---------------------------------------------------------------------------
// HOST-02: pre-registration process exit is not counted as a crash
// ---------------------------------------------------------------------------

// RED until 30-02 wires the m_live.find guard in PluginManager::onProcessFailed.
TEST_CASE("pre-registration-exit is not counted as a crash",
          "[plugin-host2][host-02][red-scaffold]") {
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    qint64 fakeNow = 0;

    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return {}; };
    fakeProbe.queryVersion = [](QString const&) -> QString { return {}; };

    PluginManager manager(
        scratch.path(), nullptr, fakeProbe, nullptr, [&fakeNow]() { return fakeNow; });

    QSignalSpy disabledSpy(&manager, &PluginManager::pluginDisabled);

    // This UUID was never inserted into m_live (no registerPlugin arrived).
    // It simulates a plugin process that died before its WebSocket sent registerPlugin.
    QString const uuid = QStringLiteral("uuid-never-registered");

    // Call onProcessFailed three times within the 30s crash window.
    // GREEN target (Plan 30-02): the m_live.find guard returns early without
    // recording the crash, so isDisabled() stays false and pluginDisabled is
    // never emitted. Currently fails RED because the current onProcessFailed
    // credits the crash unconditionally regardless of m_live membership.
    fakeNow = 0;
    manager.onProcessFailed(uuid);
    fakeNow = 5000;
    manager.onProcessFailed(uuid);
    fakeNow = 10000;
    manager.onProcessFailed(uuid);

    // Pump the event loop to let any deferred QTimer::singleShot(0,...) callbacks fire.
    pump(100);

    // pluginDisabled must NOT have been emitted for a pre-registration exit.
    // RED until 30-02 wires the m_live.find guard.
    REQUIRE(disabledSpy.count() == 0);

    // isDisabled must return false for a UUID that never registered.
    // RED until 30-02 wires the m_live.find guard.
    REQUIRE_FALSE(manager.isDisabled(uuid));
}
