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
