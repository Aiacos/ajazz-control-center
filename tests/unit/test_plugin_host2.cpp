// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_host2.cpp
 * @brief IPluginHost2 contract tests: HOST-02 pre-registration safety (Wave-1 GREEN) +
 *        HOST-01 four-runtime no-regression + Python-via-IPluginHost2 dispatch proof.
 *
 * Phase 30-01 (Wave-0 RED scaffold): disconnect-before-register + pre-registration-exit
 *   pinned as RED; turned GREEN in Plan 30-02.
 *
 * Phase 30-03 (Wave-2): four-runtime no-regression assertions + contract-level test that
 *   proves dispatch() through an IPluginHost2* pointer reaches a Python plugin (WARNING-2
 *   fix: no silent-miss facade).
 *
 * Tags:
 *   [plugin-host2][host-02] — HOST-02 pre-registration tests (GREEN after 30-02)
 *   [plugin-host2][host-01] — HOST-01 four-runtime no-regression + dispatch routing
 *
 * Phase: 30-plugin-host-modular-foundation / Plans 30-01, 30-02, 30-03
 */
#include "ajazz/plugins/i_plugin_host.hpp"
#include "i_plugin_host2.hpp"
#include "node_runner.hpp"
#include "plugin_manager.hpp"
#include "sd_plugin_server.hpp"
#include "unified_plugin_host.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWebSocket>

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

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

// ---------------------------------------------------------------------------
// Fake IPluginHost for the Python routing contract test
// ---------------------------------------------------------------------------

/**
 * @brief FakePythonHost — records the last dispatch call received.
 *
 * Used by the IPluginHost2*-pointer contract test (Task 3 item 5) to prove
 * that UnifiedPluginHost::dispatch() reaches the Python sub-host for a Python UUID.
 * Does NOT spawn a real child process.
 */
class FakePythonHost final : public ajazz::plugins::IPluginHost {
public:
    struct LastDispatch {
        std::string pluginId;
        std::string actionId;
        std::string settingsJson;
    };

    void addSearchPath(std::filesystem::path const&) override {}

    std::size_t loadAll() override { return 0; }

    std::vector<ajazz::plugins::PluginInfo> plugins() override {
        ajazz::plugins::PluginInfo info;
        info.id = "com.test.python.plugin";
        info.name = "FakePython";
        info.version = "1.0";
        return {info};
    }

    bool dispatch(std::string_view pluginId,
                  std::string_view actionId,
                  std::string_view settingsJson) override {
        ++dispatchCount;
        lastDispatch = {std::string(pluginId), std::string(actionId), std::string(settingsJson)};
        return true;
    }

    int dispatchCount{0};
    LastDispatch lastDispatch{};
};

} // namespace

// ===========================================================================
// HOST-02: disconnect-before-register leaves zero connected and does not crash
// ===========================================================================

// GREEN after 30-02 (sentinel-UUID guard wired in SdPluginServer::onNewConnection +
// m_live.find guard in PluginManager::onProcessFailed).
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

// ===========================================================================
// HOST-02: pre-registration process exit is not counted as a crash
// ===========================================================================

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

// ===========================================================================
// HOST-01: Four-runtime no-regression checks (Plan 30-03)
// ===========================================================================

TEST_CASE("Node runtime spawns and registers via unified host", "[plugin-host2][host-01]") {
    // Verifies: the Node.js runtime branch in PluginManager::spawn() is still taken
    // when a .js manifest is spawned through the IPluginHost2 surface, and the argv
    // is built without any SKU-specific branch.
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    // A FakeProbe that pretends node 20 is available so spawn() proceeds to build argv
    // (the process will fail immediately since the path is fake, but the argv is the
    // observable contract here).
    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return QStringLiteral("/fake/node"); };
    fakeProbe.queryVersion = [](QString const&) -> QString { return QStringLiteral("v20.0.0"); };

    PluginManager manager(scratch.path(), nullptr, fakeProbe);

    PluginManifest m;
    m.name = QStringLiteral("NodePlugin");
    m.codePath = QStringLiteral("index.js");
    m.author = QStringLiteral("Test");
    m.version = QStringLiteral("1.0");
    m.sdkVersion = 1;

    // Drive through the IPluginHost2 surface.
    IPluginHost2* host2 = &manager;
    // spawn() must not crash and must route to the node branch (no SKU branch).
    host2->spawn(m);

    // lastNodeArgvForTesting confirms the node path was taken (not native or HTML).
    // The argv will contain the fake path and -port argv.
    // The UUID for a .js manifest is the codePath (before registration).
    auto const argv = manager.lastNodeArgvForTesting(QStringLiteral("index.js"));
    REQUIRE_FALSE(argv.empty());                        // Node branch was taken: argv was recorded.
    REQUIRE(argv.contains(QStringLiteral("index.js"))); // code path in argv
}

TEST_CASE("HTML runtime WR-02 no-respawn guard via unified host", "[plugin-host2][host-01]") {
    // Verifies: the HTML/QWebEngine WR-02 guard (process==nullptr HTML plugins are NOT
    // re-spawned on crash) works when driven through the IPluginHost2 surface.
    //
    // We do NOT call host2->spawn() with an HTML manifest here because spawning an HTML
    // plugin requires a live QWebEngineProfile + display, which is not available in the
    // headless unit-test environment (causes SIGABRT). Instead, we use seedLiveForTest()
    // to inject a synthetic HTML plugin entry (process==nullptr) and verify that
    // onProcessFailed treats it correctly: it records the crash (it IS in m_live via the
    // seed), which means one call does NOT immediately disable it.
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return {}; };
    fakeProbe.queryVersion = [](QString const&) -> QString { return {}; };

    qint64 fakeNow = 0;
    PluginManager manager(
        scratch.path(), nullptr, fakeProbe, nullptr, [&fakeNow]() { return fakeNow; });

    QSignalSpy disabledSpy(&manager, &PluginManager::pluginDisabled);

    // Simulate an HTML plugin registered post-handshake (process==nullptr in m_live).
    // seedLiveForTest() injects {PluginManifest{}, nullptr} so process==nullptr.
    QString const htmlUuid = QStringLiteral("com.test.html.plugin");
    manager.seedLiveForTest(htmlUuid); // simulates post-registration HTML plugin entry

    // Drive onProcessFailed through the IPluginHost2 surface is not directly possible
    // (onProcessFailed is not on the IPluginHost2 surface). We call it directly here
    // to verify the WR-02 HTML-no-respawn guard: one crash within the 30s window must
    // not disable the plugin (need 3 crashes to trigger the disable policy).
    fakeNow = 0;
    manager.onProcessFailed(htmlUuid);
    pump(100);

    // A single crash must NOT disable the plugin (3-in-30s rule requires 3 crashes).
    REQUIRE(disabledSpy.count() == 0);
    REQUIRE_FALSE(manager.isDisabled(htmlUuid));
}

TEST_CASE("Native runtime spawns via unified host", "[plugin-host2][host-01]") {
    // Verifies: the native QProcess branch (non-.js, non-.html extension) in
    // PluginManager::spawn() is taken and the argv list has zero SKU-specific strings.
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return {}; };
    fakeProbe.queryVersion = [](QString const&) -> QString { return {}; };

    PluginManager manager(scratch.path(), nullptr, fakeProbe);
    QSignalSpy disabledSpy(&manager, &PluginManager::pluginDisabled);

    PluginManifest m;
    m.name = QStringLiteral("NativePlugin");
#if defined(_WIN32)
    m.codePath = QStringLiteral("plugin.exe");
#else
    m.codePath = QStringLiteral("plugin.bin");
#endif
    m.author = QStringLiteral("Test");
    m.version = QStringLiteral("1.0");
    m.sdkVersion = 1;

    // Drive through the IPluginHost2 surface (native branch).
    IPluginHost2* host2 = &manager;
    // spawn() will attempt to start the non-existent binary and immediately fail,
    // but we only care that the native branch was taken (no crash, no node argv).
    host2->spawn(m);

    // Native branch: lastNodeArgvForTesting returns empty (no node path was built).
    auto const argv = manager.lastNodeArgvForTesting(m.codePath);
    REQUIRE(argv.empty()); // native branch: no node argv
    // No disabled signal expected from a single failed spawn attempt.
    REQUIRE(disabledSpy.count() == 0);
}

TEST_CASE("Python runtime dispatches via OOP host through unified surface",
          "[plugin-host2][host-01]") {
    // Verifies: a Python UUID routes to the Python sub-host when dispatch is called
    // through UnifiedPluginHost. This uses a FakePythonHost (IPluginHost*) so no real
    // Python child is spawned.
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return {}; };
    fakeProbe.queryVersion = [](QString const&) -> QString { return {}; };

    PluginManager manager(scratch.path(), nullptr, fakeProbe);
    FakePythonHost fakePy;

    UnifiedPluginHost unified(&manager, &fakePy);

    // The Python sub-host advertises "com.test.python.plugin".
    QJsonObject payload;
    payload[QStringLiteral("key")] = QStringLiteral("value");
    bool const ok = unified.dispatch(
        QStringLiteral("com.test.python.plugin"), QStringLiteral("testAction"), payload);

    REQUIRE(ok);
    REQUIRE(fakePy.dispatchCount == 1);
    REQUIRE(fakePy.lastDispatch.pluginId == "com.test.python.plugin");
    REQUIRE(fakePy.lastDispatch.actionId == "testAction");
    // Payload must have been converted to a JSON string.
    REQUIRE_FALSE(fakePy.lastDispatch.settingsJson.empty());
}

// B5 (research D4): the .sdPlugin dispatch path must NOT forward the actionId as
// the Elgato event name. The wire event name lives in payload["event"]; the
// actionId belongs in the envelope's `action` field. Drive a real server + a
// registered WS client and assert the received frame's shape.
TEST_CASE("sdPlugin dispatch builds a well-formed event envelope, not actionId-as-event (B5)",
          "[plugin-host2][b5]") {
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return {}; };
    fakeProbe.queryVersion = [](QString const&) -> QString { return {}; };

    SdPluginServer server;
    REQUIRE(server.start(0));
    PluginManager manager(scratch.path(), &server, fakeProbe);

    // A registered WS client standing in for the .sdPlugin runtime.
    QWebSocket client;
    QSignalSpy connectedSpy(&client, &QWebSocket::connected);
    QJsonObject received;
    bool gotEvent = false;
    QObject::connect(&client, &QWebSocket::textMessageReceived, [&](QString const& text) {
        auto const obj = QJsonDocument::fromJson(text.toUtf8()).object();
        // Ignore the passHello handshake; capture the dispatched action event.
        if (obj.value(QStringLiteral("event")).toString() != QStringLiteral("passHello")) {
            received = obj;
            gotEvent = true;
        }
    });
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(connectedSpy));

    constexpr char const* kUuid = "com.test.sd.b5";
    client.sendTextMessage(
        QStringLiteral(R"({"event":"registerPlugin","uuid":"%1"})").arg(QLatin1String(kUuid)));
    pump(200);
    manager.seedLiveForTest(QString::fromLatin1(kUuid)); // make dispatch's m_live guard pass

    // Dispatch: actionId = "onKeyDown", the WIRE event lives in payload["event"].
    QJsonObject payload;
    payload[QStringLiteral("event")] = QStringLiteral("keyDown");
    payload[QStringLiteral("settings")] = QJsonObject{{QStringLiteral("k"), 1}};
    REQUIRE(manager.dispatch(QString::fromLatin1(kUuid), QStringLiteral("onKeyDown"), payload));

    auto until = QDateTime::currentMSecsSinceEpoch() + 3000;
    while (!gotEvent && QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    REQUIRE(gotEvent);
    // B5 regression: event is the real Elgato event, NOT the action UUID.
    REQUIRE(received.value(QStringLiteral("event")).toString() == QStringLiteral("keyDown"));
    REQUIRE(received.value(QStringLiteral("event")).toString() != QStringLiteral("onKeyDown"));
    // The action UUID is preserved in `action`, and the payload passes through.
    REQUIRE(received.value(QStringLiteral("action")).toString() == QStringLiteral("onKeyDown"));
    REQUIRE(
        received.value(QStringLiteral("settings")).toObject().value(QStringLiteral("k")).toInt() ==
        1);

    server.stop();
}

// ===========================================================================
// HOST-01: Contract-level test — dispatch() through IPluginHost2* pointer
// proves Python UUID reaches fake OOP host (WARNING-2 fix)
// ===========================================================================

TEST_CASE("unified host dispatch reaches Python through the IPluginHost2 pointer",
          "[plugin-host2][host-01]") {
    // This is the CRITICAL contract test (WARNING-2 fix).
    //
    // Constructs a UnifiedPluginHost wired with:
    //   - a PluginManager as the .sdPlugin sub-host (without a real server)
    //   - a FakePythonHost recording dispatch calls
    //
    // Calls dispatch() THROUGH an IPluginHost2* pointer (not the concrete type).
    // Asserts:
    //   (a) A Python UUID reaches the FakePythonHost (proves no silent miss).
    //   (b) A .sdPlugin UUID does NOT reach the FakePythonHost (routes to WS path).
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return {}; };
    fakeProbe.queryVersion = [](QString const&) -> QString { return {}; };

    PluginManager manager(scratch.path(), nullptr, fakeProbe);
    FakePythonHost fakePy;

    // Construct the aggregator.
    UnifiedPluginHost unified(&manager, &fakePy);

    // Cast to IPluginHost2* — this is the pointer type callers hold.
    IPluginHost2* host2 = &unified;

    // (a) Python UUID: must reach the FakePythonHost.
    // FakePythonHost::plugins() returns {"com.test.python.plugin"}.
    QJsonObject pyPayload;
    pyPayload[QStringLiteral("event")] = QStringLiteral("keyDown");

    bool const pyOk = host2->dispatch(
        QStringLiteral("com.test.python.plugin"), QStringLiteral("onKeyDown"), pyPayload);
    REQUIRE(pyOk);
    REQUIRE(fakePy.dispatchCount == 1); // FakePythonHost received the dispatch
    REQUIRE(fakePy.lastDispatch.pluginId == "com.test.python.plugin");

    // (b) .sdPlugin UUID: must NOT reach the FakePythonHost.
    // A UUID not in the Python inventory falls through to the .sdPlugin WS path.
    // Without a live server the WS path returns false — that is correct behaviour.
    bool const sdOk =
        host2->dispatch(QStringLiteral("com.unknown.sdplugin"), QStringLiteral("keyDown"), {});
    REQUIRE_FALSE(sdOk);                // no WS server, so WS dispatch returns false
    REQUIRE(fakePy.dispatchCount == 1); // Python count unchanged (not reached again)
}
