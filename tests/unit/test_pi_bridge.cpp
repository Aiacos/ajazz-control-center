// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_pi_bridge.cpp
 * @brief Unit tests for the Property Inspector URL policy helpers and
 *        PIBridge persistence (PLUGIN-13 restart round-trip).
 *
 * The full @c PIBridge / @c PIUrlRequestInterceptor surface needs Qt
 * WebEngine to run end-to-end (a real @c QWebEnginePage with a URL
 * request, intercepted, blocked or allowed); but the policy decision is
 * isolated in the pure-C++ helpers @ref ajazz::app::isLoadUrlAllowed and
 * @ref ajazz::app::isOpenUrlAllowed, so we can pin the security contract
 * without booting WebEngine.
 *
 * The PIBridge persistence path (setSettings / getSettings / setGlobalSettings /
 * getGlobalSettings) uses only Qt6::Core (QSaveFile, QJsonDocument,
 * QStandardPaths) and can therefore be tested without WebEngine. The
 * tests below exercise:
 *
 *   URL policy:
 *   - file:// inside the PI directory                               -> allow
 *   - file:// outside the PI directory                              -> deny
 *   - file:// with `..` traversal that resolves outside the PI dir  -> deny
 *   - https:// to allowlist host (cdn.jsdelivr.net, unpkg.com)      -> allow
 *   - https:// to a host not in the allowlist                       -> deny
 *   - http:// always blocked (defence-in-depth)                     -> deny
 *   - qrc:// / blob: / data:                                        -> allow
 *   - exotic schemes (ftp:, file2:, ...)                            -> deny
 *   - openUrl: https -> allow, http / javascript / file / mailto -> deny
 *
 *   PIBridge persistence (PLUGIN-13):
 *   - Per-context settings survive a fresh PIBridge (restart round-trip)
 *   - Global settings survive a fresh PIBridge (restart round-trip)
 *   - Settings written under ctx1 are NOT returned by a bridge using ctx2
 *   - Path-traversal UUIDs are refused: no file written, getSettings emits "{}"
 */
#include "pi_bridge.hpp"
#include "pi_url_policy.hpp"
#include "qt_app_fixture.hpp"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QUrl>

#include <array>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::isLoadUrlAllowed;
using ajazz::app::isOpenUrlAllowed;
using ajazz::app::kPiHttpsCdnAllowlist;
using ajazz::app::UrlDecision;

namespace {

// Use a deterministic absolute path. We don't actually touch the
// filesystem — the policy is textual on cleaned absolute paths — so the
// directory does not need to exist.
QString const kPiDir = QStringLiteral("/var/lib/ajazz/plugins/com.example.foo/pi");

QUrl fileUrl(QString const& absPath) {
    return QUrl::fromLocalFile(absPath);
}

} // namespace

TEST_CASE("PI URL load policy: file:// inside PI dir is allowed", "[pi-bridge][policy]") {
    REQUIRE(isLoadUrlAllowed(fileUrl(kPiDir + "/index.html"), kPiDir) == UrlDecision::Allow);
    REQUIRE(isLoadUrlAllowed(fileUrl(kPiDir + "/styles/main.css"), kPiDir) == UrlDecision::Allow);
    REQUIRE(isLoadUrlAllowed(fileUrl(kPiDir + "/sub/dir/asset.png"), kPiDir) == UrlDecision::Allow);
    // The PI dir itself is harmless even though normally a fetch to a
    // directory would 404; the policy doesn't care about file existence.
    REQUIRE(isLoadUrlAllowed(fileUrl(kPiDir), kPiDir) == UrlDecision::Allow);
}

TEST_CASE("PI URL load policy: file:// outside PI dir is rejected", "[pi-bridge][policy]") {
    REQUIRE(isLoadUrlAllowed(fileUrl(QStringLiteral("/etc/passwd")), kPiDir) ==
            UrlDecision::DenyFileOutsidePiDir);
    // Sibling plugin dir — must NOT be readable across the boundary.
    REQUIRE(isLoadUrlAllowed(
                fileUrl(QStringLiteral("/var/lib/ajazz/plugins/com.example.bar/pi/index.html")),
                kPiDir) == UrlDecision::DenyFileOutsidePiDir);
    // Path-traversal: `<piDir>/../../../etc/passwd` must NOT escape.
    QString const traversal = kPiDir + QStringLiteral("/../../../etc/passwd");
    REQUIRE(isLoadUrlAllowed(fileUrl(traversal), kPiDir) == UrlDecision::DenyFileOutsidePiDir);
    // Suffix-look-alike: piDir is `/var/lib/ajazz/plugins/com.example.foo/pi`,
    // a request to `/var/lib/ajazz/plugins/com.example.foo/pi-attacker/...`
    // must NOT be accepted by a naive `startsWith` check.
    REQUIRE(isLoadUrlAllowed(fileUrl(QStringLiteral(
                                 "/var/lib/ajazz/plugins/com.example.foo/pi-attacker/evil.html")),
                             kPiDir) == UrlDecision::DenyFileOutsidePiDir);
}

TEST_CASE("PI URL load policy: empty piDir rejects every file://", "[pi-bridge][policy]") {
    QString const noDir;
    REQUIRE(isLoadUrlAllowed(fileUrl(QStringLiteral("/var/lib/ajazz/plugins/foo/pi/index.html")),
                             noDir) == UrlDecision::DenyFileOutsidePiDir);
}

TEST_CASE("PI URL load policy: https:// to allowlist host is allowed",
          "[pi-bridge][policy][allowlist]") {
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("https://cdn.jsdelivr.net/npm/lodash@4")),
                             kPiDir) == UrlDecision::Allow);
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("https://unpkg.com/three@0.150/build/three.js")),
                             kPiDir) == UrlDecision::Allow);
}

TEST_CASE("PI URL load policy: https:// to other host is rejected",
          "[pi-bridge][policy][allowlist]") {
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("https://malicious.example/payload.js")),
                             kPiDir) == UrlDecision::DenyHttpsNotAllowlist);
    // Suffix attacks: `cdn.jsdelivr.net.attacker.example` must NOT match
    // `cdn.jsdelivr.net`. The policy is host-equality, not suffix.
    REQUIRE(
        isLoadUrlAllowed(QUrl(QStringLiteral("https://cdn.jsdelivr.net.attacker.example/foo.js")),
                         kPiDir) == UrlDecision::DenyHttpsNotAllowlist);
    REQUIRE(
        isLoadUrlAllowed(QUrl(QStringLiteral("https://attacker.example/cdn.jsdelivr.net/foo.js")),
                         kPiDir) == UrlDecision::DenyHttpsNotAllowlist);
}

TEST_CASE("PI URL load policy: http:// is always rejected", "[pi-bridge][policy]") {
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("http://cdn.jsdelivr.net/npm/lodash")), kPiDir) ==
            UrlDecision::DenyHttpRejected);
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("http://example.com/")), kPiDir) ==
            UrlDecision::DenyHttpRejected);
}

TEST_CASE("PI URL load policy: qrc / blob / data are allowed", "[pi-bridge][policy][qt-internal]") {
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("qrc:///qtwebchannel/qwebchannel.js")), kPiDir) ==
            UrlDecision::Allow);
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("blob:https://localhost/abc-123")), kPiDir) ==
            UrlDecision::Allow);
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("data:image/png;base64,iVBORw0KGgo=")), kPiDir) ==
            UrlDecision::Allow);
}

TEST_CASE("PI URL load policy: exotic schemes are rejected", "[pi-bridge][policy]") {
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("ftp://example.com/")), kPiDir) ==
            UrlDecision::DenySchemeBlocked);
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("javascript:alert(1)")), kPiDir) ==
            UrlDecision::DenySchemeBlocked);
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("ws://localhost:9000/")), kPiDir) ==
            UrlDecision::DenySchemeBlocked);
}

TEST_CASE("PI URL load policy: malformed URL is rejected", "[pi-bridge][policy]") {
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("")), kPiDir) == UrlDecision::DenyMalformed);
    // No scheme — relative path. QUrl marks this as schemeless; policy
    // refuses it rather than guessing.
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("foo/bar.html")), kPiDir) ==
            UrlDecision::DenyMalformed);
}

TEST_CASE("PI URL load policy: caller-supplied allowlist works", "[pi-bridge][policy]") {
    constexpr std::array<std::string_view, 1> custom{"example.test"};
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("https://example.test/foo")),
                             kPiDir,
                             std::span<std::string_view const>{custom}) == UrlDecision::Allow);
    // The default jsdelivr / unpkg allowlist is bypassed by the override.
    REQUIRE(isLoadUrlAllowed(QUrl(QStringLiteral("https://cdn.jsdelivr.net/foo")),
                             kPiDir,
                             std::span<std::string_view const>{custom}) ==
            UrlDecision::DenyHttpsNotAllowlist);
}

TEST_CASE("PI URL load policy: allowlist contains the documented Phase 1 hosts",
          "[pi-bridge][policy][allowlist]") {
    // Pinning this list in a test makes "I added a CDN entry" a visible
    // change in CI even when the security review is the actual gate.
    REQUIRE(kPiHttpsCdnAllowlist.size() == 2);
    REQUIRE(kPiHttpsCdnAllowlist[0] == std::string_view{"cdn.jsdelivr.net"});
    REQUIRE(kPiHttpsCdnAllowlist[1] == std::string_view{"unpkg.com"});
}

TEST_CASE("PI openUrl policy: https is allowed, all other schemes refused",
          "[pi-bridge][openurl]") {
    // Allow: https with a host.
    REQUIRE(isOpenUrlAllowed(QStringLiteral("https://docs.ajazz.example/help")) ==
            UrlDecision::Allow);
    REQUIRE(isOpenUrlAllowed(QStringLiteral("https://github.com/Aiacos/ajazz-control-center")) ==
            UrlDecision::Allow);

    // Deny: javascript: (the canonical XSS vector).
    REQUIRE(isOpenUrlAllowed(QStringLiteral("javascript:alert(1)")) ==
            UrlDecision::DenySchemeBlocked);

    // Deny: file:.
    REQUIRE(isOpenUrlAllowed(QStringLiteral("file:///etc/passwd")) ==
            UrlDecision::DenySchemeBlocked);

    // Deny: mailto: (opening external mail client needs the prompt
    // step that's flagged TODO(pi-prompt) in pi_bridge.cpp).
    REQUIRE(isOpenUrlAllowed(QStringLiteral("mailto:victim@example.com")) ==
            UrlDecision::DenySchemeBlocked);

    // Deny: http: — explicitly logged as http-rejected so audit logs see
    // the difference from a generic "scheme blocked".
    REQUIRE(isOpenUrlAllowed(QStringLiteral("http://example.com/")) ==
            UrlDecision::DenyHttpRejected);

    // Deny: empty / malformed.
    REQUIRE(isOpenUrlAllowed(QStringLiteral("")) == UrlDecision::DenyMalformed);
    REQUIRE(isOpenUrlAllowed(QStringLiteral("not a url")) == UrlDecision::DenyMalformed);
    REQUIRE(isOpenUrlAllowed(QStringLiteral("https://")) == UrlDecision::DenyMalformed); // no host
}

// ---------------------------------------------------------------------------
// PIBridge persistence tests (PLUGIN-13 restart round-trip)
//
// These cases use QStandardPaths::setTestModeEnabled(true) (enabled by
// qtApp()) so AppDataLocation resolves under a per-user sandbox tree rather
// than the developer's real config directory.  Each case uses a unique
// plugin/context UUID so cases are independent without needing QTemporaryDir
// indirection (which cannot redirect AppDataLocation cross-platform in
// test-mode). The emit from getSettings/getGlobalSettings is synchronous on
// the same thread (direct connection), so a plain lambda capture is
// sufficient — no event loop is needed.
// ---------------------------------------------------------------------------

TEST_CASE("PI settings round-trip survives a fresh PIBridge", "[pi-bridge][persistence]") {
    ajazz::tests::qtApp();

    // Write via first bridge (simulates first app run).
    {
        ajazz::app::PIBridge w(nullptr,
                               QStringLiteral("com.example.roundtrip-ctx"),
                               QStringLiteral("act-rt"),
                               QStringLiteral("ctx-rt-001"));
        w.setSettings(QStringLiteral(R"({"hello":"world"})"));
    } // first bridge destroyed — simulates app exit

    // Read via second bridge (simulates next app run with same UUIDs).
    ajazz::app::PIBridge r(nullptr,
                           QStringLiteral("com.example.roundtrip-ctx"),
                           QStringLiteral("act-rt"),
                           QStringLiteral("ctx-rt-001"));
    QString seen;
    QObject::connect(&r, &ajazz::app::PIBridge::didReceiveSettings, [&](QString j) { seen = j; });
    r.getSettings();

    // The persisted JSON must parse back to {"hello":"world"}.
    REQUIRE(!seen.isEmpty());
    REQUIRE(
        QJsonDocument::fromJson(seen.toUtf8()).object().value(QStringLiteral("hello")).toString() ==
        QStringLiteral("world"));
}

TEST_CASE("PI global settings round-trip survives a fresh PIBridge", "[pi-bridge][persistence]") {
    ajazz::tests::qtApp();

    // Write via first bridge.
    {
        ajazz::app::PIBridge w(nullptr,
                               QStringLiteral("com.example.roundtrip-global"),
                               QStringLiteral("act-rg"),
                               QStringLiteral("ctx-rg-001"));
        w.setGlobalSettings(QStringLiteral(R"({"version":42,"flag":true})"));
    }

    // Read via second bridge.
    ajazz::app::PIBridge r(nullptr,
                           QStringLiteral("com.example.roundtrip-global"),
                           QStringLiteral("act-rg"),
                           QStringLiteral("ctx-rg-001"));
    QString seen;
    QObject::connect(
        &r, &ajazz::app::PIBridge::didReceiveGlobalSettings, [&](QString j) { seen = j; });
    r.getGlobalSettings();

    QJsonObject const obj = QJsonDocument::fromJson(seen.toUtf8()).object();
    REQUIRE(obj.value(QStringLiteral("version")).toInt() == 42);
    REQUIRE(obj.value(QStringLiteral("flag")).toBool() == true);
}

TEST_CASE("PI settings are isolated per context", "[pi-bridge][persistence]") {
    ajazz::tests::qtApp();

    // Write settings under ctx-iso-A.
    {
        ajazz::app::PIBridge w(nullptr,
                               QStringLiteral("com.example.isolation"),
                               QStringLiteral("act-iso"),
                               QStringLiteral("ctx-iso-A"));
        w.setSettings(QStringLiteral(R"({"key":"only-in-A"})"));
    }

    // A fresh bridge using ctx-iso-B must return "{}" (no cross-context leak).
    ajazz::app::PIBridge r(nullptr,
                           QStringLiteral("com.example.isolation"),
                           QStringLiteral("act-iso"),
                           QStringLiteral("ctx-iso-B"));
    QString seen;
    QObject::connect(&r, &ajazz::app::PIBridge::didReceiveSettings, [&](QString j) { seen = j; });
    r.getSettings();

    // Must be empty JSON — ctx-iso-B was never written.
    REQUIRE(seen == QStringLiteral("{}"));
}

TEST_CASE("PI settings refuse path-traversal uuids", "[pi-bridge][persistence]") {
    ajazz::tests::qtApp();

    // Snapshot the plugins directory entry count before any malicious write.
    QString const pluginsRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                QStringLiteral("/plugins");
    QDir const pluginsDir(pluginsRoot);
    qsizetype const countBefore =
        pluginsDir.exists() ? pluginsDir.entryList(QDir::AllEntries).size() : 0;

    // Case 1: malicious pluginUuid "../evil" — must not write outside sandbox.
    {
        ajazz::app::PIBridge evil(nullptr,
                                  QStringLiteral("../evil"),
                                  QStringLiteral("act-trav"),
                                  QStringLiteral("ctx-trav-001"));
        evil.setSettings(QStringLiteral(R"({"attack":"yes"})"));

        // getSettings must emit "{}" for an invalid uuid.
        QString result;
        QObject::connect(
            &evil, &ajazz::app::PIBridge::didReceiveSettings, [&](QString j) { result = j; });
        evil.getSettings();
        REQUIRE(result == QStringLiteral("{}"));
    }

    // Case 2: malicious contextUuid "../ctx" — same guard.
    {
        ajazz::app::PIBridge evil2(nullptr,
                                   QStringLiteral("com.example.traversal-ctx"),
                                   QStringLiteral("act-trav"),
                                   QStringLiteral("../ctx"));
        evil2.setSettings(QStringLiteral(R"({"attack":"yes"})"));

        QString result;
        QObject::connect(
            &evil2, &ajazz::app::PIBridge::didReceiveSettings, [&](QString j) { result = j; });
        evil2.getSettings();
        REQUIRE(result == QStringLiteral("{}"));
    }

    // The plugins directory must not have grown (no file escaped the sandbox).
    qsizetype const countAfter =
        pluginsDir.exists() ? pluginsDir.entryList(QDir::AllEntries).size() : 0;
    REQUIRE(countAfter == countBefore);
}

// ---------------------------------------------------------------------------
// cefQuery shim tests (PLUGIN-09 / 20-02)
//
// kCefQueryShimSource is a pure constexpr string — no WebEngine needed.
// The dispatcher tests construct PIBridge(nullptr, ...) and verify that
// invoke() fans out to the existing typed slots. The WebEngine-gated
// injection-point case is compiled only when AJAZZ_HAVE_WEBENGINE is defined.
// ---------------------------------------------------------------------------
#include "pi_cef_shim.hpp"

TEST_CASE("cefQuery shim source contains required JS tokens", "[pi-bridge][cefquery]") {
    QString const src = QString::fromUtf8(ajazz::app::kCefQueryShimSource.data(),
                                          int(ajazz::app::kCefQueryShimSource.size()));
    REQUIRE(src.contains(QStringLiteral("window.cefQuery")));
    REQUIRE(src.contains(QStringLiteral("onSuccess")));
    REQUIRE(src.contains(QStringLiteral("onFailure")));
    REQUIRE(src.contains(QStringLiteral("$SD")));
}

TEST_CASE("PIBridge invoke dispatches getSettings to didReceiveSettings", "[pi-bridge][cefquery]") {
    ajazz::tests::qtApp();

    ajazz::app::PIBridge bridge(nullptr,
                                QStringLiteral("com.example.cefq"),
                                QStringLiteral("act-cefq"),
                                QStringLiteral("ctx-cefq-001"));
    // Seed some settings so getSettings emits a non-empty payload.
    bridge.setSettings(QStringLiteral(R"({"key":"cefq-value"})"));

    QString seen;
    QObject::connect(
        &bridge, &ajazz::app::PIBridge::didReceiveSettings, [&](QString j) { seen = j; });

    // invoke with a getSettings event body (sdk §8: event field selects the slot).
    QString const result = bridge.invoke(QStringLiteral(R"({"event":"getSettings"})"));

    REQUIRE(!seen.isEmpty());
    QJsonObject const obj = QJsonDocument::fromJson(seen.toUtf8()).object();
    REQUIRE(obj.value(QStringLiteral("key")).toString() == QStringLiteral("cefq-value"));
    // invoke must return a valid JSON string (empty object at minimum).
    REQUIRE(!result.isEmpty());
}

TEST_CASE("PIBridge invoke with unknown event returns {} and emits nothing",
          "[pi-bridge][cefquery]") {
    ajazz::tests::qtApp();

    ajazz::app::PIBridge bridge(nullptr,
                                QStringLiteral("com.example.cefq-unknown"),
                                QStringLiteral("act-cefq-unk"),
                                QStringLiteral("ctx-cefq-unk-001"));

    bool settingsFired = false;
    bool globalFired = false;
    QObject::connect(
        &bridge, &ajazz::app::PIBridge::didReceiveSettings, [&](QString) { settingsFired = true; });
    QObject::connect(&bridge, &ajazz::app::PIBridge::didReceiveGlobalSettings, [&](QString) {
        globalFired = true;
    });

    QString const result = bridge.invoke(QStringLiteral(R"({"event":"unknownEvent"})"));

    REQUIRE(result == QStringLiteral("{}"));
    REQUIRE(!settingsFired);
    REQUIRE(!globalFired);
}

#if defined(AJAZZ_HAVE_WEBENGINE)
TEST_CASE("cefQuery shim injects at DocumentCreation in MainWorld", "[pi-bridge][cefquery]") {
    auto const s = ajazz::app::makeCefQueryShim();
    REQUIRE(s.injectionPoint() == QWebEngineScript::DocumentCreation);
    REQUIRE(s.worldId() == QWebEngineScript::MainWorld);
    REQUIRE(s.runsOnSubFrames() == true);
    REQUIRE(s.name() == QStringLiteral("ajazz-cefquery-shim"));
}

// ---------------------------------------------------------------------------
// PropertyInspectorController interface tests (PLUGIN-09 / 20-03)
//
// Constructing a QQuickWebEngineProfile in a unit-test binary requires a
// fully initialised WebEngine runtime (QtWebEngineQuick::initialize() before
// QGuiApplication, plus a GPU/compositing surface). The unit-test binary
// uses a plain QCoreApplication from qtApp() — sufficient for the bridge
// persistence tests (Qt::Core only) and the mirabox/cefquery shim-string
// tests, but NOT for QQuickWebEngineProfile construction. Attempting it
// produces a SIGABRT from the Chromium-based renderer.
//
// Therefore: the loadInspector full-profile integration test lives in the
// QML offscreen harness (tests/qml/) or Phase 25 hardware verification,
// NOT in ajazz_unit_tests. We assert only the PURE-LOGIC aspects here:
//
//   - webEngineAvailable() is true (confirming the WebEngine link is active
//     and the macro is correctly propagated to the test binary)
//   - hasHtmlInspector() starts false on a fresh controller (the PIMPL is
//     constructed but no loadInspector has been called)
//   - closeInspector() on a never-loaded controller is a safe no-op
//
// The loadInspector -> hasHtmlInspector flip, activeUrl, activeProfile, and
// activeChannel are verified indirectly through the production app path and
// Phase 25 hardware verification as specified in the plan.
// ---------------------------------------------------------------------------
#include "property_inspector_controller.hpp"

TEST_CASE("PropertyInspectorController - webEngineAvailable is true in WebEngine builds",
          "[pi-bridge][inspector]") {
    ajazz::tests::qtApp();
    ajazz::app::PropertyInspectorController ctrl{nullptr};
    ajazz::app::PropertyInspectorController::registerInstance(&ctrl);
    REQUIRE(ctrl.webEngineAvailable() == true);
    REQUIRE(ctrl.hasHtmlInspector() == false); // no inspector loaded yet
    REQUIRE(ctrl.activeUrl().isEmpty());
    // closeInspector on a never-loaded controller is a safe no-op.
    REQUIRE_NOTHROW(ctrl.closeInspector());
    REQUIRE(ctrl.hasHtmlInspector() == false);
}
#endif // defined(AJAZZ_HAVE_WEBENGINE)

// ---------------------------------------------------------------------------
// Relay endpoint tests (PLUGIN-09 / 20-03)
//
// Confirms that sendToPropertyInspector is a connectable signal and that
// sendToPlugin is a safe no-op stub today (M5). The live bridge<->server
// connection is wired in the AJAZZ_HAVE_WEBSOCKETS block below (17-02
// landed SdPluginServer::sendEvent, so the condition is met per the 20-03
// STOP gate). A4 + Pitfall 5: the live plugin-process round-trip is gated
// to the phase that verifies on hardware (Phase 25).
// ---------------------------------------------------------------------------

TEST_CASE("PIBridge relay - sendToPropertyInspector is connectable and fires",
          "[pi-bridge][relay]") {
    ajazz::tests::qtApp();

    ajazz::app::PIBridge bridge(nullptr,
                                QStringLiteral("com.example.relay"),
                                QStringLiteral("act-relay"),
                                QStringLiteral("ctx-relay-001"));

    QString received;
    QObject::connect(&bridge, &ajazz::app::PIBridge::sendToPropertyInspector, [&](QString json) {
        received = json;
    });

    // Manually emit the signal (simulating a plugin pushing a payload to the PI).
    emit bridge.sendToPropertyInspector(
        QStringLiteral(R"({"event":"sendToPropertyInspector","payload":{"key":"val"}})"));

    REQUIRE(!received.isEmpty());
    REQUIRE(received.contains(QStringLiteral("sendToPropertyInspector")));
}

// 17-02-SUMMARY.md IS present: SdPluginServer::sendEvent is live. The Application
// wires bridge->toPluginRequested -> sendEvent via activeBridgeChanged (application.cpp).
// This loopback test confirms the PIBridge end of that relay: calling sendToPlugin emits
// toPluginRequested with the correct pluginUuid and raw JSON payload. The server-side
// dispatch (sendEvent) is an integration concern verified on hardware in Phase 25.
// A4 + Pitfall 5 (20-RESEARCH.md): the live plugin-process round-trip is Phase 25.
TEST_CASE("PIBridge relay - sendToPlugin emits toPluginRequested with correct uuid and payload",
          "[pi-bridge][relay]") {
    ajazz::tests::qtApp();

    ajazz::app::PIBridge bridge(nullptr,
                                QStringLiteral("com.example.relay-stub"),
                                QStringLiteral("act-relay-stub"),
                                QStringLiteral("ctx-relay-stub-001"));

    // Connect a spy to toPluginRequested — the Application wires this to sendEvent.
    QString capturedUuid;
    QString capturedJson;
    QObject::connect(
        &bridge, &ajazz::app::PIBridge::toPluginRequested, [&](QString uuid, QString json) {
            capturedUuid = uuid;
            capturedJson = json;
        });

    // Also guard: sendToPropertyInspector must NOT fire (opposite direction).
    bool oppositeDir = false;
    QObject::connect(&bridge, &ajazz::app::PIBridge::sendToPropertyInspector, [&](QString) {
        oppositeDir = true;
    });

    QString const payload = QStringLiteral(R"({"event":"sendToPlugin","payload":{"k":"v"}})");
    REQUIRE_NOTHROW(bridge.sendToPlugin(payload));

    REQUIRE(capturedUuid == QStringLiteral("com.example.relay-stub"));
    REQUIRE(capturedJson == payload);
    REQUIRE(!oppositeDir); // sendToPlugin must not echo back to the PI
}

// ---------------------------------------------------------------------------
// CR-01 regression: invoke("sendToPlugin") with a JSON-object payload must
// forward a non-empty string to toPluginRequested, not drop it.
// ---------------------------------------------------------------------------

TEST_CASE("PIBridge invoke sendToPlugin with object payload forwards non-empty payload",
          "[pi-bridge][cefquery][cr-01]") {
    ajazz::tests::qtApp();

    ajazz::app::PIBridge bridge(nullptr,
                                QStringLiteral("com.example.cr01"),
                                QStringLiteral("act-cr01"),
                                QStringLiteral("ctx-cr01-001"));

    QString capturedUuid;
    QString capturedJson;
    QObject::connect(
        &bridge, &ajazz::app::PIBridge::toPluginRequested, [&](QString uuid, QString json) {
            capturedUuid = uuid;
            capturedJson = json;
        });

    // Idiomatic SDK shape: payload is a JSON object, NOT a string.
    // Before the CR-01 fix, QJsonValue::toString() returned "" for objects,
    // causing the relay to fire with an empty payload (silent data drop).
    QString const invocation =
        QStringLiteral(R"({"event":"sendToPlugin","payload":{"key":"value","count":3}})");
    bridge.invoke(invocation);

    // The relay must have fired.
    REQUIRE(capturedUuid == QStringLiteral("com.example.cr01"));
    // The forwarded payload must be non-empty — the object was serialized.
    REQUIRE(!capturedJson.isEmpty());
    // The serialized payload must round-trip to the original object.
    QJsonDocument const doc = QJsonDocument::fromJson(capturedJson.toUtf8());
    REQUIRE(!doc.isNull());
    QJsonObject const obj = doc.object();
    REQUIRE(obj.value(QStringLiteral("key")).toString() == QStringLiteral("value"));
    REQUIRE(obj.value(QStringLiteral("count")).toInt() == 3);
}

TEST_CASE("PIBridge invoke sendToPlugin with string payload forwards the string unchanged",
          "[pi-bridge][cefquery][cr-01]") {
    ajazz::tests::qtApp();

    ajazz::app::PIBridge bridge(nullptr,
                                QStringLiteral("com.example.cr01-str"),
                                QStringLiteral("act-cr01-str"),
                                QStringLiteral("ctx-cr01-str-001"));

    QString capturedJson;
    QObject::connect(&bridge, &ajazz::app::PIBridge::toPluginRequested, [&](QString, QString json) {
        capturedJson = json;
    });

    // Some PIs may pass a pre-serialized string payload.
    QString const invocation =
        QStringLiteral(R"({"event":"sendToPlugin","payload":"raw-string-payload"})");
    bridge.invoke(invocation);

    REQUIRE(capturedJson == QStringLiteral("raw-string-payload"));
}

// ---------------------------------------------------------------------------
// WR-02: sendToPlugin and logMessage must reject payloads exceeding the cap.
// WR-03: readJsonOrEmpty (exercised via getSettings) must not buffer oversize files.
// ---------------------------------------------------------------------------

TEST_CASE("PIBridge sendToPlugin rejects oversized payload", "[pi-bridge][relay][wr-02]") {
    ajazz::tests::qtApp();

    ajazz::app::PIBridge bridge(nullptr,
                                QStringLiteral("com.example.cap-send"),
                                QStringLiteral("act-cap"),
                                QStringLiteral("ctx-cap-send-001"));

    bool relayFired = false;
    QObject::connect(&bridge, &ajazz::app::PIBridge::toPluginRequested, [&](QString, QString) {
        relayFired = true;
    });

    // Build a payload just over 1 MiB.
    QString const huge = QString(1024 * 1024 + 1, QLatin1Char('x'));
    REQUIRE_NOTHROW(bridge.sendToPlugin(huge));
    // The relay must NOT have fired for an oversized payload.
    REQUIRE(!relayFired);
}

TEST_CASE("PIBridge sendToPlugin accepts a payload at the boundary", "[pi-bridge][relay][wr-02]") {
    ajazz::tests::qtApp();

    ajazz::app::PIBridge bridge(nullptr,
                                QStringLiteral("com.example.cap-send-ok"),
                                QStringLiteral("act-cap-ok"),
                                QStringLiteral("ctx-cap-send-ok-001"));

    bool relayFired = false;
    QObject::connect(&bridge, &ajazz::app::PIBridge::toPluginRequested, [&](QString, QString) {
        relayFired = true;
    });

    // Exactly 1 MiB of ASCII — must pass.
    QString const boundary = QString(1024 * 1024, QLatin1Char('y'));
    REQUIRE_NOTHROW(bridge.sendToPlugin(boundary));
    REQUIRE(relayFired);
}

// ---------------------------------------------------------------------------
// sdpi.css helper + bundled resource tests (PLUGIN-09 / 20-02)
//
// isSdpiCssRequest is in pi_url_policy.cpp (already linked); no new link needed.
// The qrc presence test requires the test qrc to embed the CSS file under the
// same prefix used at runtime (see tests/unit/CMakeLists.txt qt_add_resources).
// ---------------------------------------------------------------------------
#include <QFile>

TEST_CASE("isSdpiCssRequest true for canonical sdpi.css paths", "[pi-bridge][sdpi]") {
    using ajazz::app::isSdpiCssRequest;

    // Standard Elgato PI reference: static/css/sdpi.css
    REQUIRE(isSdpiCssRequest(QUrl(QStringLiteral("file:///pi/static/css/sdpi.css"))) == true);
    // Direct reference by filename only
    REQUIRE(isSdpiCssRequest(QUrl(QStringLiteral("file:///pi/sdpi.css"))) == true);
    // https CDN variant (some PIs use an absolute URL)
    REQUIRE(isSdpiCssRequest(QUrl(QStringLiteral("https://cdn.example.com/sdpi.css"))) == true);
    // Nested path
    REQUIRE(isSdpiCssRequest(QUrl(QStringLiteral("file:///some/path/static/css/sdpi.css"))) ==
            true);
}

TEST_CASE("isSdpiCssRequest false for non-sdpi.css paths", "[pi-bridge][sdpi]") {
    using ajazz::app::isSdpiCssRequest;

    // Different CSS file
    REQUIRE(isSdpiCssRequest(QUrl(QStringLiteral("file:///pi/other.css"))) == false);
    // Suffix-look-alike: evil extension appended
    REQUIRE(isSdpiCssRequest(QUrl(QStringLiteral("file:///pi/sdpi.css.evil"))) == false);
    // Prefix match only (directory named sdpi.css)
    REQUIRE(isSdpiCssRequest(QUrl(QStringLiteral("file:///pi/sdpi.css/file.txt"))) == false);
    // Empty URL
    REQUIRE(isSdpiCssRequest(QUrl(QStringLiteral(""))) == false);
}

TEST_CASE("bundled sdpi.css qrc resource is present and non-empty", "[pi-bridge][sdpi]") {
    ajazz::tests::qtApp();
    QFile f(QStringLiteral(":/qt/qml/AjazzControlCenter/streamdock/sdpi.css"));
    REQUIRE(f.exists());
    REQUIRE(f.open(QIODevice::ReadOnly));
    QByteArray const data = f.readAll();
    REQUIRE(!data.isEmpty());
    // Must contain the canonical Elgato sdpi-css class selectors.
    REQUIRE(data.contains(".sdpi-item"));
    REQUIRE(data.contains(".sdpi-wrapper"));
}

// ---------------------------------------------------------------------------
// T-29-03 regression: isSafeComponent must accept '#' (wire context separator)
// and continue to reject the classic traversal / bad-char cases.
// These lock the plugin_settings_store::isSafeComponent contract so that
// wire context ids (device#root#Keypad#row#col) pass the sanitiser unchanged —
// required for PIBridge per-context settings (PLUGIN-22 / 29-02).
// ---------------------------------------------------------------------------
#include "plugin_settings_store.hpp"

TEST_CASE("plugin_settings_store::isSafeComponent accepts wire context id separators",
          "[plugin-settings][safe-component][t-29-03]") {
    using ajazz::app::plugin_settings_store::isSafeComponent;

    // Wire context id components: '#' (0x23) is printable ASCII in [0x20,0x7e]
    // and is not a path separator — it must pass through so the derived context
    // ids (device#root#Keypad#row#col) are usable as filename components.
    REQUIRE(isSafeComponent(QStringLiteral("akp05e#root#Keypad#1#2")));
    REQUIRE(isSafeComponent(QStringLiteral("akp05e#root#Keypad#0#0")));
    REQUIRE(isSafeComponent(QStringLiteral("akp05e#root#Keypad#1#4")));

    // Reverse-DNS plugin UUIDs must also be accepted (pre-existing behaviour).
    REQUIRE(isSafeComponent(QStringLiteral("com.elgato.cpu")));
    REQUIRE(isSafeComponent(QStringLiteral("com.example.my-plugin")));

    // Classic traversal attacks must still be rejected.
    REQUIRE(!isSafeComponent(QStringLiteral("../x")));
    REQUIRE(!isSafeComponent(QStringLiteral("a/b")));
    REQUIRE(!isSafeComponent(QStringLiteral("a\\b")));
    REQUIRE(!isSafeComponent(QStringLiteral("..")));
    REQUIRE(!isSafeComponent(QStringLiteral(".")));
    REQUIRE(!isSafeComponent(QStringLiteral("foo/../bar")));

    // Empty / oversized must be rejected.
    REQUIRE(!isSafeComponent(QStringLiteral("")));
}
