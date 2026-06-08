// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_win_plugin_classification.cpp
 * @brief WinPluginClassificationTest — unit tests for classifyWindowsPlugin()
 *        and supportsCurrentPlatform() (Phase 35, Plan 35-01 / WINPLG-01/02).
 *
 * Mirrors the test_plugin_manifest.cpp / test_plugin_verify_gate.cpp idiom:
 * ASCII-only TEST_CASE names tagged [win-plugin-classification]; PluginManifest
 * fixtures are built in-process (no JSON file needed); the PE-magic case writes a
 * 2-byte "MZ" file into a QTemporaryDir bundle.
 *
 * Coverage (every behavior from the plan):
 *   - no OS entry / no "windows" entry          -> NotWindowsOnly
 *   - win-only, CodePath .js/.html/.cjs, no PE  -> WsOnlyIpc
 *   - win-only, CodePath .exe/.dll              -> VendorDll
 *   - win-only, CodePath .js BUT bundle has MZ  -> VendorDll (corroborator overrides)
 *   - empty / non-existent bundleDir            -> no crash (suffix alone classifies)
 *   - supportsCurrentPlatform WsOnlyIpc         -> true on every platform
 *   - supportsCurrentPlatform VendorDll         -> true only on windows
 *   - supportsCurrentPlatform NotWindowsOnly    -> false everywhere
 */
#include "plugin_manifest.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::app;

namespace {

/// Build a win-only manifest with the given effective code path.
/// When @p codePathWin is empty, @p codePath is the effective path.
PluginManifest makeWinManifest(QString const& codePath, QString const& codePathWin = {}) {
    PluginManifest m;
    m.name = QStringLiteral("WinPlugin");
    m.version = QStringLiteral("1.0.0");
    m.os.push_back(PluginOsRequirement{QStringLiteral("windows"), QString{}});
    m.codePath = codePath;
    m.codePathWin = codePathWin;
    return m;
}

/// Write @p bytes verbatim to <dir>/<name> (raw, no text translation).
void writeRaw(QString const& dir, QString const& name, QByteArray const& bytes) {
    QFile f(QDir(dir).filePath(name));
    REQUIRE(f.open(QIODevice::WriteOnly));
    REQUIRE(f.write(bytes) == bytes.size());
    f.close();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// classifyWindowsPlugin — NotWindowsOnly
// ---------------------------------------------------------------------------

TEST_CASE("WinPluginClassification empty OS array is NotWindowsOnly",
          "[win-plugin-classification]") {
    PluginManifest m;
    m.name = QStringLiteral("NoOsPlugin");
    m.codePath = QStringLiteral("plugin.js");
    // os is empty.
    CHECK(classifyWindowsPlugin(m, QString{}) == WinPluginClass::NotWindowsOnly);
}

TEST_CASE("WinPluginClassification mac-only OS is NotWindowsOnly", "[win-plugin-classification]") {
    PluginManifest m;
    m.name = QStringLiteral("MacOnlyPlugin");
    m.codePath = QStringLiteral("plugin.exe"); // suffix is .exe but no windows entry
    m.os.push_back(PluginOsRequirement{QStringLiteral("mac"), QString{}});
    CHECK(classifyWindowsPlugin(m, QString{}) == WinPluginClass::NotWindowsOnly);
}

// ---------------------------------------------------------------------------
// classifyWindowsPlugin — WsOnlyIpc
// ---------------------------------------------------------------------------

TEST_CASE("WinPluginClassification win-only js no PE is WsOnlyIpc", "[win-plugin-classification]") {
    QTemporaryDir bundle;
    REQUIRE(bundle.isValid());
    // Plain JS code file, no PE magic.
    writeRaw(bundle.path(), QStringLiteral("plugin.js"), QByteArray("console.log('hi');"));

    PluginManifest const m = makeWinManifest(QStringLiteral("plugin.js"));
    CHECK(classifyWindowsPlugin(m, bundle.path()) == WinPluginClass::WsOnlyIpc);
}

TEST_CASE("WinPluginClassification win-only html no PE is WsOnlyIpc",
          "[win-plugin-classification]") {
    QTemporaryDir bundle;
    REQUIRE(bundle.isValid());
    writeRaw(bundle.path(), QStringLiteral("index.html"), QByteArray("<html></html>"));

    PluginManifest const m = makeWinManifest(QStringLiteral("index.html"));
    CHECK(classifyWindowsPlugin(m, bundle.path()) == WinPluginClass::WsOnlyIpc);
}

// ---------------------------------------------------------------------------
// classifyWindowsPlugin — VendorDll via suffix
// ---------------------------------------------------------------------------

TEST_CASE("WinPluginClassification win-only exe suffix is VendorDll",
          "[win-plugin-classification]") {
    PluginManifest const m = makeWinManifest(QStringLiteral("host.exe"));
    // No bundle scan needed; suffix alone -> VendorDll. Empty dir, no crash.
    CHECK(classifyWindowsPlugin(m, QString{}) == WinPluginClass::VendorDll);
}

TEST_CASE("WinPluginClassification win-only dll suffix is VendorDll",
          "[win-plugin-classification]") {
    // CodePathWin override takes precedence over codePath as the effective path.
    PluginManifest const m =
        makeWinManifest(QStringLiteral("plugin.js"), QStringLiteral("native.dll"));
    CHECK(classifyWindowsPlugin(m, QString{}) == WinPluginClass::VendorDll);
}

TEST_CASE("WinPluginClassification exe suffix is case-insensitive", "[win-plugin-classification]") {
    PluginManifest const m = makeWinManifest(QStringLiteral("HOST.EXE"));
    CHECK(classifyWindowsPlugin(m, QString{}) == WinPluginClass::VendorDll);
}

// ---------------------------------------------------------------------------
// classifyWindowsPlugin — PE-magic corroborator overrides mislabeled manifest
// ---------------------------------------------------------------------------

TEST_CASE("WinPluginClassification PE magic in bundle overrides js suffix",
          "[win-plugin-classification]") {
    QTemporaryDir bundle;
    REQUIRE(bundle.isValid());
    // Manifest declares a .js code path (would be WsOnlyIpc) but the bundle ships
    // a file beginning with MZ -> the corroborator reclassifies to VendorDll.
    writeRaw(bundle.path(), QStringLiteral("plugin.js"), QByteArray("console.log('hi');"));
    writeRaw(bundle.path(), QStringLiteral("sneaky.bin"), QByteArray("MZ\x90\x00", 4));

    PluginManifest const m = makeWinManifest(QStringLiteral("plugin.js"));
    CHECK(classifyWindowsPlugin(m, bundle.path()) == WinPluginClass::VendorDll);
}

// ---------------------------------------------------------------------------
// classifyWindowsPlugin — robustness: empty / non-existent bundleDir
// ---------------------------------------------------------------------------

TEST_CASE("WinPluginClassification non-existent bundleDir does not crash",
          "[win-plugin-classification]") {
    PluginManifest const m = makeWinManifest(QStringLiteral("plugin.js"));
    // Non-existent path: no PE scan possible, suffix .js -> WsOnlyIpc, no crash.
    CHECK(classifyWindowsPlugin(m, QStringLiteral("/no/such/bundle/dir")) ==
          WinPluginClass::WsOnlyIpc);
}

TEST_CASE("WinPluginClassification empty bundleDir classifies by suffix",
          "[win-plugin-classification]") {
    PluginManifest const m = makeWinManifest(QStringLiteral("plugin.dll"));
    CHECK(classifyWindowsPlugin(m, QString{}) == WinPluginClass::VendorDll);
}

// ---------------------------------------------------------------------------
// supportsCurrentPlatform
// ---------------------------------------------------------------------------

TEST_CASE("WinPluginClassification supportsCurrentPlatform WsOnlyIpc runs everywhere",
          "[win-plugin-classification]") {
    PluginManifest const m = makeWinManifest(QStringLiteral("plugin.js"));
    CHECK(supportsCurrentPlatform(m, QStringLiteral("linux"), WinPluginClass::WsOnlyIpc) == true);
    CHECK(supportsCurrentPlatform(m, QStringLiteral("mac"), WinPluginClass::WsOnlyIpc) == true);
    CHECK(supportsCurrentPlatform(m, QStringLiteral("windows"), WinPluginClass::WsOnlyIpc) == true);
}

TEST_CASE("WinPluginClassification supportsCurrentPlatform VendorDll only on windows",
          "[win-plugin-classification]") {
    PluginManifest const m = makeWinManifest(QStringLiteral("host.exe"));
    // Wine detection is DEFERRED (WINPLG-03) -> false off Windows this phase.
    CHECK(supportsCurrentPlatform(m, QStringLiteral("linux"), WinPluginClass::VendorDll) == false);
    CHECK(supportsCurrentPlatform(m, QStringLiteral("mac"), WinPluginClass::VendorDll) == false);
    CHECK(supportsCurrentPlatform(m, QStringLiteral("windows"), WinPluginClass::VendorDll) == true);
}

TEST_CASE("WinPluginClassification supportsCurrentPlatform NotWindowsOnly is false",
          "[win-plugin-classification]") {
    PluginManifest m;
    m.name = QStringLiteral("LinuxPlugin");
    m.codePath = QStringLiteral("plugin.js");
    // NotWindowsOnly: caller uses manifestRunnableHere instead; this helper returns false.
    CHECK(supportsCurrentPlatform(m, QStringLiteral("linux"), WinPluginClass::NotWindowsOnly) ==
          false);
    CHECK(supportsCurrentPlatform(m, QStringLiteral("windows"), WinPluginClass::NotWindowsOnly) ==
          false);
}
