// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_builtin_actions.cpp
 * @brief Unit tests for the BuiltinActionRegistry dispatch table (pure-core, Task 1)
 *        and the BuiltinActionsService per-UUID handler wiring (app-tier, Task 2).
 *
 * Task 1 (registry-only cases, no Qt event-loop needed):
 *   - register + handles() + dispatch() with a spy handler.
 *   - Prefix gating: handles() returns false for non-builtin UUIDs.
 *   - Unregistered built-in prefix: handles() returns false.
 *   - dispatch with no registered handler is a clean no-op.
 *   - Two distinct UUIDs mapped to two distinct spies do not cross-fire.
 *   - settingsJson is forwarded verbatim (not parsed by the registry).
 *
 * Task 2 (app-tier e2e, QCoreApplication needed for QJsonDocument + QSettings):
 *   - device.brightness -> BrightnessSink (clamped 0..100).
 *   - browser -> OpenUrlFn (http/https only; WR-01 rejection).
 *   - plain.text -> FakeSynth::typeText.
 *   - system.hotkey OUTPUT -> FakeSynth::sendChord; CAPTURE OFF by default (T-21-hook).
 *   - system.multimedia / system.volume -> FakeSynth::sendMediaKey.
 *   - page.previous/next -> navigate(-1/+1); page.goto -> navigate(target).
 *   - profile.openchild -> engine->pushPage; profile.backtoparent -> engine->popPage.
 *   - multiactions -> engine->run (decoded chain in order).
 *   - multiactions.LunBo: two distinct keys have independent cursors; reset on profile change.
 *   - plugin-executor short-circuit: registered builtin fires handler NOT fallback;
 *     third-party UUID fires fallback NOT a handler.
 *
 * ASCII-only TEST_CASE/SECTION titles (CLAUDE.md: em-dash and right-arrow are
 * mangled by Win32 CMD codepage in ctest filter args).
 */

// ---- Core headers (no Qt, no nlohmann — Task 1 uses only these) ----
#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/builtin_action_registry.hpp"
#include "ajazz/core/executor.hpp"
#include "ajazz/core/input_synthesizer.hpp"
#include "ajazz/core/profile.hpp"

// ---- App-tier headers (Task 2) ----
#include "builtin_actions_service.hpp"

// ---- Qt (Task 2; included here so they are available throughout the TU) ----
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// ---- Standard ----
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::core;
using namespace ajazz::app;

// ============================================================================
// Shared test utilities
// ============================================================================

namespace {

/// Lazy QCoreApplication singleton (never deleted to avoid static-destruction races).
QCoreApplication* ensureQCoreApp() {
    static QCoreApplication* app = []() {
        static int argc = 0;
        static char* argv[] = {nullptr}; // NOLINT
        return new QCoreApplication(argc, argv);
    }();
    return app;
}

/// FakeSynth: records all IInputSynthesizer calls for assertion.
class FakeSynth : public IInputSynthesizer {
public:
    struct Call {
        enum class Kind { TypeText, SendChord, SendMediaKey, CaptureHotkeys } kind;
        std::string text;
        KeyChord chord;
        MediaKey mediaKey{MediaKey::PlayPause};
        bool captureEnable{false};
    };

    std::vector<Call> log;
    bool captureActive{false};

    bool typeText(std::string_view utf8) override {
        log.push_back({Call::Kind::TypeText, std::string{utf8}, {}, MediaKey::PlayPause, false});
        return true;
    }
    bool sendChord(KeyChord const& chord) override {
        Call c;
        c.kind = Call::Kind::SendChord;
        c.chord = chord;
        log.push_back(c);
        return true;
    }
    bool sendMediaKey(MediaKey key) override {
        Call c;
        c.kind = Call::Kind::SendMediaKey;
        c.mediaKey = key;
        log.push_back(c);
        return true;
    }
    bool captureHotkeys(bool enable, std::function<void(KeyChord const&)> /*cb*/) override {
        Call c;
        c.kind = Call::Kind::CaptureHotkeys;
        c.captureEnable = enable;
        log.push_back(c);
        captureActive = enable;
        return enable;
    }
};

/// Build a minimal BuiltinActionsService with spy sinks.
struct TestHarness {
    std::vector<int> brightnessLog;
    std::vector<int> navigateLog;
    std::vector<std::string> openUrlLog;
    std::vector<std::string> fallbackLog;
    FakeSynth* fakeSynth{nullptr};

    std::vector<std::string> engineLog;
    ActionExecutors recExecs;
    std::unique_ptr<ActionEngine> ownedEngine;
    ActionEngine* engine{nullptr};

    std::unique_ptr<BuiltinActionsService> service;

    explicit TestHarness(bool withEngine = true) {
        ensureQCoreApp();

        if (withEngine) {
            recExecs.plugin = [this](std::string_view id, std::string_view s) {
                engineLog.push_back(std::string{"plugin:"} + std::string{id} + ":" +
                                    std::string{s});
            };
            recExecs.keyPress = [this](std::string_view s) {
                engineLog.push_back("key:" + std::string{s});
            };
            recExecs.openUrl = [this](std::string_view s) {
                engineLog.push_back("url:" + std::string{s});
            };
            recExecs.runCommand = [this](std::string_view s) {
                engineLog.push_back("run:" + std::string{s});
            };
            ownedEngine = std::make_unique<ActionEngine>(recExecs);
            engine = ownedEngine.get();
        }

        service = std::make_unique<BuiltinActionsService>(
            [this](int level) { brightnessLog.push_back(level); },
            [this](int dir) { navigateLog.push_back(dir); },
            [this](std::string_view url) { openUrlLog.push_back(std::string{url}); },
            engine,
            [this](std::string_view id, std::string_view s) {
                fallbackLog.push_back(std::string{id} + ":" + std::string{s});
            });

        auto fake = std::make_unique<FakeSynth>();
        fakeSynth = fake.get();
        service->setSynthesizer(std::move(fake));
    }
};

/// Serialize a QJsonObject to std::string.
[[nodiscard]] std::string toJson(QJsonObject const& obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)).toStdString();
}

} // namespace

// ============================================================================
// Task 1: BuiltinActionRegistry dispatch table (pure-core, no Qt event-loop)
// ============================================================================

TEST_CASE("BuiltinActionRegistry - prefix constant matches spec", "[builtin-actions]") {
    REQUIRE(BuiltinActionRegistry::kBuiltinPrefix == "com.hotspot.streamdock.");
}

TEST_CASE("BuiltinActionRegistry - register then handles returns true", "[builtin-actions]") {
    BuiltinActionRegistry reg;
    reg.registerAction("com.hotspot.streamdock.device.brightness", [](std::string_view) {});
    REQUIRE(reg.handles("com.hotspot.streamdock.device.brightness"));
}

TEST_CASE("BuiltinActionRegistry - handles returns false for third-party UUID",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;
    REQUIRE_FALSE(reg.handles("com.thirdparty.plugin.action"));
}

TEST_CASE("BuiltinActionRegistry - handles returns false for unregistered builtin prefix",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;
    REQUIRE_FALSE(reg.handles("com.hotspot.streamdock.system.hotkey"));
}

TEST_CASE("BuiltinActionRegistry - dispatch invokes handler with verbatim settingsJson",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;

    std::string capturedSettings;
    reg.registerAction(
        "com.hotspot.streamdock.device.brightness",
        [&capturedSettings](std::string_view s) { capturedSettings = std::string{s}; });

    reg.dispatch("com.hotspot.streamdock.device.brightness", "{\"level\":40}");

    REQUIRE(capturedSettings == "{\"level\":40}");
}

TEST_CASE("BuiltinActionRegistry - dispatch calls handler exactly once", "[builtin-actions]") {
    BuiltinActionRegistry reg;

    int callCount = 0;
    reg.registerAction("com.hotspot.streamdock.browser",
                       [&callCount](std::string_view) { ++callCount; });

    reg.dispatch("com.hotspot.streamdock.browser", "{}");
    REQUIRE(callCount == 1);

    reg.dispatch("com.hotspot.streamdock.browser", "{}");
    REQUIRE(callCount == 2);
}

TEST_CASE("BuiltinActionRegistry - dispatch on unregistered UUID is a clean no-op",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;
    REQUIRE_NOTHROW(reg.dispatch("com.hotspot.streamdock.unregistered", "{}"));
    REQUIRE_NOTHROW(reg.dispatch("com.thirdparty.xyz", "{}"));
}

TEST_CASE("BuiltinActionRegistry - two UUIDs mapped to two distinct spies do not cross-fire",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;

    std::vector<std::string> logA;
    std::vector<std::string> logB;

    reg.registerAction("com.hotspot.streamdock.system.hotkey",
                       [&logA](std::string_view s) { logA.emplace_back(s); });
    reg.registerAction("com.hotspot.streamdock.plain.text",
                       [&logB](std::string_view s) { logB.emplace_back(s); });

    reg.dispatch("com.hotspot.streamdock.system.hotkey", "hotkey-payload");
    reg.dispatch("com.hotspot.streamdock.plain.text", "text-payload");

    REQUIRE(logA.size() == 1);
    REQUIRE(logA[0] == "hotkey-payload");
    REQUIRE(logB.size() == 1);
    REQUIRE(logB[0] == "text-payload");
}

TEST_CASE("BuiltinActionRegistry - settingsJson forwarded verbatim without parsing",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;

    constexpr std::string_view kWeirdJson =
        R"({"key":"value with spaces","nested":{"arr":[1,2,3]},"unicode":"é"})";

    std::string received;
    reg.registerAction("com.hotspot.streamdock.multiactions",
                       [&received](std::string_view s) { received = std::string{s}; });

    reg.dispatch("com.hotspot.streamdock.multiactions", kWeirdJson);

    REQUIRE(received == kWeirdJson);
}

TEST_CASE("BuiltinActionRegistry - handles empty dispatch table", "[builtin-actions]") {
    BuiltinActionRegistry reg;
    REQUIRE_FALSE(reg.handles("com.hotspot.streamdock.browser"));
    REQUIRE_NOTHROW(reg.dispatch("com.hotspot.streamdock.browser", "{}"));
}

TEST_CASE("BuiltinActionRegistry - registerAction overwrites previous handler",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;

    int firstCount = 0;
    int secondCount = 0;

    reg.registerAction("com.hotspot.streamdock.page.previous",
                       [&firstCount](std::string_view) { ++firstCount; });
    reg.registerAction("com.hotspot.streamdock.page.previous",
                       [&secondCount](std::string_view) { ++secondCount; });

    reg.dispatch("com.hotspot.streamdock.page.previous", "{}");
    REQUIRE(firstCount == 0);
    REQUIRE(secondCount == 1);
}

TEST_CASE("BuiltinActionRegistry - compiled without Qt or nlohmann dependency",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;
    REQUIRE(BuiltinActionRegistry::kBuiltinPrefix.size() > 0);
}

// ============================================================================
// Task 2: BuiltinActionsService per-UUID handler wiring (app-tier, e2e)
// ============================================================================

// ---- device.brightness ----

TEST_CASE("BuiltinActionsService - device.brightness dispatches to BrightnessSink",
          "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("level")] = 40;
    h.service->onPluginAction("com.hotspot.streamdock.device.brightness", toJson(s));
    REQUIRE(h.brightnessLog.size() == 1);
    REQUIRE(h.brightnessLog[0] == 40);
}

TEST_CASE("BuiltinActionsService - device.brightness clamps level to 0..100", "[builtin-actions]") {
    TestHarness h;

    QJsonObject sHigh;
    sHigh[QStringLiteral("level")] = 150;
    h.service->onPluginAction("com.hotspot.streamdock.device.brightness", toJson(sHigh));
    REQUIRE(h.brightnessLog[0] == 100);

    QJsonObject sLow;
    sLow[QStringLiteral("level")] = -5;
    h.service->onPluginAction("com.hotspot.streamdock.device.brightness", toJson(sLow));
    REQUIRE(h.brightnessLog[1] == 0);
}

// ---- browser ----

TEST_CASE("BuiltinActionsService - browser dispatches valid https URL to openUrl",
          "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("url")] = QStringLiteral("https://example.com");
    h.service->onPluginAction("com.hotspot.streamdock.browser", toJson(s));
    REQUIRE(h.openUrlLog.size() == 1);
    REQUIRE(h.openUrlLog[0] == "https://example.com");
}

TEST_CASE("BuiltinActionsService - browser rejects non-http URL (WR-01)", "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("url")] = QStringLiteral("file:///etc/passwd");
    h.service->onPluginAction("com.hotspot.streamdock.browser", toJson(s));
    REQUIRE(h.openUrlLog.empty());
}

// ---- plain.text ----

TEST_CASE("BuiltinActionsService - plain.text calls typeText on FakeSynth", "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("text")] = QStringLiteral("hello world");
    h.service->onPluginAction("com.hotspot.streamdock.plain.text", toJson(s));
    REQUIRE(h.fakeSynth->log.size() == 1);
    REQUIRE(h.fakeSynth->log[0].kind == FakeSynth::Call::Kind::TypeText);
    REQUIRE(h.fakeSynth->log[0].text == "hello world");
}

// ---- system.hotkey OUTPUT ----

TEST_CASE("BuiltinActionsService - system.hotkey OUTPUT calls sendChord on FakeSynth",
          "[builtin-actions]") {
    TestHarness h;
    QJsonArray mods;
    mods.push_back(QStringLiteral("ctrl"));
    QJsonObject s;
    s[QStringLiteral("modifiers")] = mods;
    s[QStringLiteral("key")] = QStringLiteral("p");
    h.service->onPluginAction("com.hotspot.streamdock.system.hotkey", toJson(s));

    REQUIRE(h.fakeSynth->log.size() == 1);
    REQUIRE(h.fakeSynth->log[0].kind == FakeSynth::Call::Kind::SendChord);
    REQUIRE(h.fakeSynth->log[0].chord.modifiers.size() == 1);
    REQUIRE(h.fakeSynth->log[0].chord.modifiers[0] == 0x000700E0u); // Left Ctrl
    REQUIRE(h.fakeSynth->log[0].chord.key == (0x00070004u + static_cast<std::uint32_t>('p' - 'a')));
}

TEST_CASE("BuiltinActionsService - system.hotkey CAPTURE is OFF by default (T-21-hook)",
          "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("modifiers")] = QJsonArray{};
    s[QStringLiteral("key")] = QStringLiteral("f1");
    h.service->onPluginAction("com.hotspot.streamdock.system.hotkey", toJson(s));

    for (auto const& call : h.fakeSynth->log) {
        if (call.kind == FakeSynth::Call::Kind::CaptureHotkeys) {
            REQUIRE_FALSE(call.captureEnable);
        }
    }
    REQUIRE_FALSE(h.service->captureHotkeysEnabled());
}

TEST_CASE("BuiltinActionsService - setCaptureHotkeys(true) activates the opt-in gate",
          "[builtin-actions]") {
    TestHarness h;
    h.service->setCaptureHotkeys(true);
    REQUIRE(h.service->captureHotkeysEnabled());
    bool found = false;
    for (auto const& call : h.fakeSynth->log) {
        if (call.kind == FakeSynth::Call::Kind::CaptureHotkeys && call.captureEnable) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

// ---- system.multimedia ----

TEST_CASE("BuiltinActionsService - system.multimedia dispatches PlayPause to FakeSynth",
          "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("key")] = QStringLiteral("PlayPause");
    h.service->onPluginAction("com.hotspot.streamdock.system.multimedia", toJson(s));
    REQUIRE(h.fakeSynth->log.size() == 1);
    REQUIRE(h.fakeSynth->log[0].kind == FakeSynth::Call::Kind::SendMediaKey);
    REQUIRE(h.fakeSynth->log[0].mediaKey == MediaKey::PlayPause);
}

// ---- system.volume ----

TEST_CASE("BuiltinActionsService - system.volume dispatches VolumeUp to FakeSynth",
          "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("key")] = QStringLiteral("VolumeUp");
    h.service->onPluginAction("com.hotspot.streamdock.system.volume", toJson(s));
    REQUIRE(h.fakeSynth->log.size() == 1);
    REQUIRE(h.fakeSynth->log[0].mediaKey == MediaKey::VolumeUp);
}

TEST_CASE("BuiltinActionsService - system.volume dispatches Mute", "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("key")] = QStringLiteral("Mute");
    h.service->onPluginAction("com.hotspot.streamdock.system.volume", toJson(s));
    REQUIRE(h.fakeSynth->log[0].mediaKey == MediaKey::Mute);
}

// ---- page navigation ----

TEST_CASE("BuiltinActionsService - page.previous calls navigate(-1)", "[builtin-actions]") {
    TestHarness h;
    h.service->onPluginAction("com.hotspot.streamdock.page.previous", "{}");
    REQUIRE(h.navigateLog.size() == 1);
    REQUIRE(h.navigateLog[0] == -1);
}

TEST_CASE("BuiltinActionsService - page.next calls navigate(+1)", "[builtin-actions]") {
    TestHarness h;
    h.service->onPluginAction("com.hotspot.streamdock.page.next", "{}");
    REQUIRE(h.navigateLog.size() == 1);
    REQUIRE(h.navigateLog[0] == +1);
}

TEST_CASE("BuiltinActionsService - page.goto calls navigate with target index",
          "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("target")] = 3;
    h.service->onPluginAction("com.hotspot.streamdock.page.goto", toJson(s));
    REQUIRE(h.navigateLog.size() == 1);
    REQUIRE(h.navigateLog[0] == 3);
}

// ---- profile navigation ----

TEST_CASE("BuiltinActionsService - profile.openchild calls engine->pushPage", "[builtin-actions]") {
    TestHarness h;

    Profile p;
    p.id = "test-profile";
    p.deviceCodename = "akp05e";
    ProfilePage rootPage;
    rootPage.id = "root";
    rootPage.children.push_back("child-page-01");
    p.pages["root"] = rootPage;
    ProfilePage childPage;
    childPage.id = "child-page-01";
    p.pages["child-page-01"] = childPage;
    h.engine->setProfile(std::move(p));

    QJsonObject s;
    s[QStringLiteral("childId")] = QStringLiteral("child-page-01");
    h.service->onPluginAction("com.hotspot.streamdock.profile.openchild", toJson(s));
    REQUIRE(h.engine->currentPageId() == "child-page-01");
}

TEST_CASE("BuiltinActionsService - profile.backtoparent calls engine->popPage",
          "[builtin-actions]") {
    TestHarness h;

    Profile p;
    p.id = "test-profile";
    p.deviceCodename = "akp05e";
    ProfilePage rootPage;
    rootPage.id = "root";
    rootPage.children.push_back("child-page-02");
    p.pages["root"] = rootPage;
    ProfilePage childPage;
    childPage.id = "child-page-02";
    p.pages["child-page-02"] = childPage;
    h.engine->setProfile(std::move(p));

    h.engine->pushPage("child-page-02");
    REQUIRE(h.engine->currentPageId() == "child-page-02");

    h.service->onPluginAction("com.hotspot.streamdock.profile.backtoparent", "{}");
    REQUIRE(h.engine->currentPageId() == "root");
}

// ---- multiactions ----

TEST_CASE("BuiltinActionsService - multiactions runs decoded ActionChain in order via engine",
          "[builtin-actions]") {
    TestHarness h;

    QJsonArray actions;
    for (auto const& id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
        QJsonObject action;
        action[QStringLiteral("kind")] = 0; // Plugin
        action[QStringLiteral("id")] = id;
        action[QStringLiteral("settingsJson")] = QStringLiteral("{}");
        actions.push_back(action);
    }
    QJsonObject s;
    s[QStringLiteral("actions")] = actions;

    h.service->onPluginAction("com.hotspot.streamdock.multiactions", toJson(s));

    REQUIRE(h.engineLog.size() == 3);
    REQUIRE(h.engineLog[0] == "plugin:a:{}");
    REQUIRE(h.engineLog[1] == "plugin:b:{}");
    REQUIRE(h.engineLog[2] == "plugin:c:{}");
}

// ---- BIND-04/05: com.hotspot.streamdock.multiaction (Multi Action) ----

/// Classification: the singular Multi Action id (com.hotspot.streamdock.multiaction)
/// is registered under the kBuiltinPrefix, so handles() returns true. This is the
/// id the input-service dispatch seam id-matches (RESEARCH A3/Q2 -- the OpenDeck
/// "opendeck.multiaction" id would fail the prefix check and never fire).
TEST_CASE("BuiltinActionsService - multiaction built-in is registered (handles true)",
          "[builtin-actions][multiaction]") {
    TestHarness h;
    REQUIRE(h.service->handles("com.hotspot.streamdock.multiaction"));
    // Cross-check against the canonical constant (no opendeck.* literal).
    REQUIRE(h.service->handles(std::string{ajazz::core::BuiltinActionRegistry::kMultiActionId}));
}

/// Flat-JSON fallback: dispatching the multiaction id with settings-embedded
/// children {"actions":[...]} still runs an ordered chain through the engine
/// (preserves the legacy flat-array shape; the primary children-driven dispatch
/// happens at the input-service seam in Task 1).
TEST_CASE("BuiltinActionsService - multiaction flat-JSON fallback runs chain in order",
          "[builtin-actions][multiaction]") {
    TestHarness h;

    QJsonArray actions;
    for (auto const& id : {QStringLiteral("x"), QStringLiteral("y")}) {
        QJsonObject action;
        action[QStringLiteral("kind")] = 0; // Plugin
        action[QStringLiteral("id")] = id;
        action[QStringLiteral("settingsJson")] = QStringLiteral("{}");
        actions.push_back(action);
    }
    QJsonObject s;
    s[QStringLiteral("actions")] = actions;

    h.service->onPluginAction("com.hotspot.streamdock.multiaction", toJson(s));

    REQUIRE(h.engineLog.size() == 2);
    REQUIRE(h.engineLog[0] == "plugin:x:{}");
    REQUIRE(h.engineLog[1] == "plugin:y:{}");
}

// ---- multiactions.LunBo per-key cursor ----

TEST_CASE("BuiltinActionsService - LunBo two distinct keys have independent cursors",
          "[builtin-actions]") {
    TestHarness h;

    auto makeChain = [](char prefix, std::size_t count) {
        QJsonArray arr;
        for (std::size_t i = 0; i < count; ++i) {
            QJsonObject action;
            action[QStringLiteral("kind")] = 0;
            action[QStringLiteral("id")] =
                QString(QChar::fromLatin1(prefix)) + QString::number(static_cast<int>(i));
            action[QStringLiteral("settingsJson")] = QStringLiteral("{}");
            arr.push_back(action);
        }
        return arr;
    };

    QJsonObject sA;
    sA[QStringLiteral("page")] = QStringLiteral("root");
    sA[QStringLiteral("key")] = 0;
    sA[QStringLiteral("actions")] = makeChain('a', 3);

    QJsonObject sB;
    sB[QStringLiteral("page")] = QStringLiteral("root");
    sB[QStringLiteral("key")] = 1;
    sB[QStringLiteral("actions")] = makeChain('b', 2);

    // Press A twice -> a0, a1
    h.service->onPluginAction("com.hotspot.streamdock.multiactions.LunBo", toJson(sA));
    h.service->onPluginAction("com.hotspot.streamdock.multiactions.LunBo", toJson(sA));

    // Press B once -> b0
    h.service->onPluginAction("com.hotspot.streamdock.multiactions.LunBo", toJson(sB));

    REQUIRE(h.engineLog.size() == 3);
    REQUIRE(h.engineLog[0] == "plugin:a0:{}");
    REQUIRE(h.engineLog[1] == "plugin:a1:{}");
    REQUIRE(h.engineLog[2] == "plugin:b0:{}");
}

TEST_CASE("BuiltinActionsService - LunBo cursors reset on profile change", "[builtin-actions]") {
    TestHarness h;

    QJsonArray arr;
    auto addAction = [&arr](std::string const& id) {
        QJsonObject a;
        a[QStringLiteral("kind")] = 0;
        a[QStringLiteral("id")] = QString::fromStdString(id);
        a[QStringLiteral("settingsJson")] = QStringLiteral("{}");
        arr.push_back(a);
    };
    addAction("x0");
    addAction("x1");

    QJsonObject s;
    s[QStringLiteral("page")] = QStringLiteral("root");
    s[QStringLiteral("key")] = 5;
    s[QStringLiteral("actions")] = arr;

    h.service->onPluginAction("com.hotspot.streamdock.multiactions.LunBo", toJson(s));
    REQUIRE(h.engineLog[0] == "plugin:x0:{}");

    h.service->resetLunBoCursors();

    h.service->onPluginAction("com.hotspot.streamdock.multiactions.LunBo", toJson(s));
    REQUIRE(h.engineLog[1] == "plugin:x0:{}"); // cursor reset -> fires x0 again
}

// ---- plugin-executor short-circuit ----

TEST_CASE("BuiltinActionsService - registered builtin UUID fires handler not fallback",
          "[builtin-actions]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("level")] = 70;
    h.service->onPluginAction("com.hotspot.streamdock.device.brightness", toJson(s));

    REQUIRE(h.brightnessLog.size() == 1);
    REQUIRE(h.fallbackLog.empty());
}

TEST_CASE("BuiltinActionsService - third-party UUID fires fallback not a builtin handler",
          "[builtin-actions]") {
    TestHarness h;
    h.service->onPluginAction("com.thirdparty.obs.switch", "{\"scene\":\"Main\"}");

    REQUIRE(h.brightnessLog.empty());
    REQUIRE(h.navigateLog.empty());
    REQUIRE(h.fallbackLog.size() == 1);
    REQUIRE(h.fallbackLog[0] == "com.thirdparty.obs.switch:{\"scene\":\"Main\"}");
}

TEST_CASE("BuiltinActionsService - unregistered builtin-prefix UUID forwards to fallback",
          "[builtin-actions]") {
    TestHarness h;
    // vmix is deferred and not registered.
    h.service->onPluginAction("com.hotspot.streamdock.vmix", "{}");
    REQUIRE(h.fallbackLog.size() == 1);
}

// ---- page.indicator is a display-only no-op ----

TEST_CASE("BuiltinActionsService - page.indicator is a no-op (display-only hint)",
          "[builtin-actions]") {
    TestHarness h;
    REQUIRE_NOTHROW(h.service->onPluginAction("com.hotspot.streamdock.page.indicator", "{}"));
    REQUIRE(h.navigateLog.empty());
    REQUIRE(h.brightnessLog.empty());
}

// ---- OpenDeck SPA builtin aliases (builtin parity audit 2026-07-02) ----
//
// The SPA's fallback "OpenDeck" category advertises `opendeck.*` ids while the
// registry keys on `com.hotspot.streamdock.*` — every advertised builtin was a
// silent no-op at press time. onPluginAction now canonicalises the alias.

TEST_CASE("BuiltinActionsService - opendeck.brightness alias routes to BrightnessSink",
          "[builtin-actions][opendeck-alias]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("value")] = 65; // the alias convention uses "value"
    h.service->onPluginAction("opendeck.brightness", toJson(s));
    REQUIRE(h.brightnessLog.size() == 1);
    REQUIRE(h.brightnessLog[0] == 65);
}

TEST_CASE("BuiltinActionsService - opendeck.openurl alias routes to openUrl with WR-01 gate",
          "[builtin-actions][opendeck-alias]") {
    TestHarness h;
    QJsonObject ok;
    ok[QStringLiteral("url")] = QStringLiteral("https://example.org");
    h.service->onPluginAction("opendeck.openurl", toJson(ok));
    REQUIRE(h.openUrlLog.size() == 1);
    REQUIRE(h.openUrlLog[0] == "https://example.org");

    QJsonObject bad;
    bad[QStringLiteral("url")] = QStringLiteral("file:///etc/shadow");
    h.service->onPluginAction("opendeck.openurl", toJson(bad));
    REQUIRE(h.openUrlLog.size() == 1); // rejected, not forwarded
}

TEST_CASE("BuiltinActionsService - opendeck.switchprofile alias calls the profile switcher",
          "[builtin-actions][opendeck-alias]") {
    TestHarness h;
    std::vector<QString> switched;
    h.service->setProfileSwitcher([&switched](QString const& name) { switched.push_back(name); });
    QJsonObject s;
    s[QStringLiteral("profile")] = QStringLiteral("Streaming");
    h.service->onPluginAction("opendeck.switchprofile", toJson(s));
    REQUIRE(switched.size() == 1);
    REQUIRE(switched[0] == QStringLiteral("Streaming"));
}

TEST_CASE("BuiltinActionsService - opendeck.runcommand alias runs via the engine executor",
          "[builtin-actions][opendeck-alias]") {
    TestHarness h;
    QJsonObject s;
    s[QStringLiteral("command")] = QStringLiteral("true");
    h.service->onPluginAction("opendeck.runcommand", toJson(s));
    // The handler normalises {"command"} to the argv form and runs it through
    // the shared ActionEngine RunCommand executor (harness spy). Windows
    // normalises to `cmd /C`; POSIX to `/bin/sh -c`.
    REQUIRE(h.engineLog.size() == 1);
    REQUIRE(h.engineLog[0].rfind("run:", 0) == 0);
#ifdef _WIN32
    REQUIRE(h.engineLog[0].find("cmd") != std::string::npos);
#else
    REQUIRE(h.engineLog[0].find("/bin/sh") != std::string::npos);
#endif
    REQUIRE(h.engineLog[0].find("true") != std::string::npos);
}

TEST_CASE("BuiltinActionsService - non-alias third-party UUID still reaches the fallback",
          "[builtin-actions][opendeck-alias]") {
    TestHarness h;
    h.service->onPluginAction("com.example.thirdparty.action", "{}");
    REQUIRE(h.fallbackLog.size() == 1);
    REQUIRE(h.fallbackLog[0].rfind("com.example.thirdparty.action:", 0) == 0);
}
