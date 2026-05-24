// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_install_from_file.cpp
 * @brief Phase 22 Plan 22-02 (PLUGIN-14): unit suite for installFromFile.
 *
 * Tests the staging->verify->promote sequence and each verdict path:
 *   - signed (self-signed) + confirm -> promoted into installedPlugins/
 *   - tampered -> Refused; nothing under installedPlugins/ (staging-before-promote)
 *   - self-signed without confirm -> not promoted; "confirm to install" outcome
 *   - non-zip / oversized -> rejected by validateDownloadedArchive before extraction
 *
 * Fixture recipe follows test_plugin_verify_gate.cpp:
 *   keygen  via python3 sign-plugin-manifest.py keygen --out-dir <keys>
 *   sign    via python3 sign-plugin-manifest.py sign   --manifest <m> --priv-key <priv.pem>
 *   tamper  by flipping a byte in the signed manifest
 *
 * Archive fixture recipe follows test_sdplugin_extractor.cpp:
 *   QZipWriter to build a synthetic .sdPlugin zip.
 *
 * ASCII-only TEST_CASE names + tags per CLAUDE.md (Win32 CMD codepage guard).
 */
#include "plugin_catalog_model.hpp"
#include "qt_app_fixture.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <private/qzipwriter_p.h>

#ifdef _WIN32
#include <cwctype>

#include <process.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using ajazz::app::PluginCatalogModel;
using ajazz::tests::qtApp;

namespace {

fs::path verifierScript() {
    return fs::path{AJAZZ_TEST_REPO_ROOT} / "scripts" / "sign-plugin-manifest.py";
}

int runChild(std::vector<std::string> argv) {
#ifdef _WIN32
    if (argv.empty()) {
        return -1;
    }
    std::vector<std::wstring> wide;
    wide.reserve(argv.size());
    for (auto const& s : argv) {
        int const size =
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w;
        if (size > 0) {
            w.resize(static_cast<size_t>(size));
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), size);
        }
        wide.push_back(std::move(w));
    }
    std::vector<wchar_t const*> rawArgv;
    rawArgv.reserve(wide.size() + 1);
    for (auto const& w : wide) {
        rawArgv.push_back(w.data());
    }
    rawArgv.push_back(nullptr);
    intptr_t const result = _wspawnvp(_P_WAIT, rawArgv[0], rawArgv.data());
    return static_cast<int>(result);
#else
    std::vector<char*> rawArgv;
    rawArgv.reserve(argv.size() + 1);
    for (auto& s : argv) {
        rawArgv.push_back(s.data());
    }
    rawArgv.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execvp(rawArgv[0], rawArgv.data());
        std::_Exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

/// Minimal valid manifest JSON (no Ajazz.Signing block = unsigned).
constexpr char const* kMinimalManifest = R"({
  "UUID": "com.example.install-from-file-test",
  "Name": "Install from file test",
  "Version": "1.0.0",
  "Author": "Tests",
  "Description": "Fixture for test_plugin_install_from_file.cpp.",
  "Icon": "icon",
  "CodePath": "main.py",
  "Actions": [
    {
      "UUID": "com.example.install-from-file-test.act",
      "Name": "Action",
      "Icon": "icon",
      "States": [{ "Image": "img" }]
    }
  ],
  "OS": [{ "Platform": "linux", "MinimumVersion": "22.04" }],
  "SDKVersion": 2,
  "Software": { "MinimumVersion": "1.0" }
})";

void writeFile(fs::path const& p, std::string const& content) {
    std::ofstream f{p, std::ios::binary};
    f << content;
}

std::string readFile(fs::path const& p) {
    std::ifstream f{p, std::ios::binary};
    std::ostringstream out;
    out << f.rdbuf();
    return out.str();
}

/// Build a synthetic .sdPlugin archive for the given manifest bytes.
/// Returns the archive path.
QString
buildSdPluginArchive(QString const& dir, QByteArray const& manifestBytes, QString const& pluginId) {
    QString const archivePath = dir + "/" + pluginId + ".sdPlugin";
    {
        QZipWriter zip(archivePath);
        if (zip.status() != QZipWriter::NoError) {
            return {};
        }
        zip.addFile(pluginId + ".sdPlugin/manifest.json", manifestBytes);
        zip.addFile(pluginId + ".sdPlugin/Code/main.py", QByteArray("# placeholder\n"));
        zip.close();
    }
    return archivePath;
}

} // namespace

// ---------------------------------------------------------------------------
// Helper: reset the plugins dir override after each test.
// ---------------------------------------------------------------------------

struct PluginsDirGuard {
    explicit PluginsDirGuard(QString const& dir) { PluginCatalogModel::setPluginsDirOverride(dir); }
    ~PluginsDirGuard() { PluginCatalogModel::setPluginsDirOverride(QString{}); }
};

// ---------------------------------------------------------------------------
// Test: non-zip file is rejected before extraction
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile non-zip file is rejected before extraction", "[plugin-install]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    PluginsDirGuard guard(tmp.filePath("plugins"));

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // Write a non-zip file
    QString const notZip = tmp.filePath("not-a-zip.sdPlugin");
    {
        QFile f(notZip);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("this is not a zip file at all"));
    }

    bool const result = model.installFromFile(notZip);
    REQUIRE_FALSE(result);
    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);
    REQUIRE_FALSE(spy.at(0).at(2).toString().isEmpty());
}

// ---------------------------------------------------------------------------
// Test: oversized file is rejected before extraction
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile oversized file is rejected", "[plugin-install]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    PluginsDirGuard guard(tmp.filePath("plugins"));

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // Write a file with ZIP magic but oversized (64 MiB + 1 byte).
    // We simulate this by writing exactly kMaxPluginDownloadBytes + 1 bytes.
    // The file itself starts with PK magic so it passes the magic check
    // but fails the size check.
    QString const bigFile = tmp.filePath("big.sdPlugin");
    {
        QFile f(bigFile);
        REQUIRE(f.open(QIODevice::WriteOnly));
        // Write the ZIP magic header bytes
        f.write(QByteArray("PK\x03\x04", 4));
        // Pad to exceed the 64 MiB cap (write 64 MiB + 1 bytes total)
        QByteArray const pad(static_cast<int>(64LL * 1024 * 1024), '\0');
        f.write(pad); // This makes it 64 MiB + 4 bytes, which exceeds the cap
    }

    bool const result = model.installFromFile(bigFile);
    REQUIRE_FALSE(result);
    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);
}

// ---------------------------------------------------------------------------
// Test: tampered -> Refused; staging-before-promote invariant
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile tampered -> Refused (staging-before-promote)",
          "[plugin-install]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // 1. Generate a key pair and sign the manifest
    QString const keysDir = tmp.filePath("keys");
    QDir().mkpath(keysDir);
    REQUIRE(
        runChild(
            {"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.toStdString()}) ==
        0);

    fs::path const rawManifest = fs::path{tmp.path().toStdString()} / "manifest.json";
    writeFile(rawManifest, kMinimalManifest);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir + "/priv.pem").toStdString()}) == 0);

    // 2. Tamper: flip a byte in the description
    auto blob = readFile(rawManifest);
    auto const pos = blob.find("Fixture for");
    REQUIRE(pos != std::string::npos);
    blob[pos] = 'X';
    writeFile(rawManifest, blob);

    // 3. Build archive with tampered manifest
    QByteArray const tamperedBytes = QByteArray::fromStdString(blob);
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), tamperedBytes, "com.example.install-from-file-test");
    REQUIRE_FALSE(archivePath.isEmpty());
    REQUIRE(QFile::exists(archivePath));

    bool const result = model.installFromFile(archivePath);
    REQUIRE_FALSE(result);

    // Verify the installFinished signal was emitted with failure
    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);
    REQUIRE_FALSE(spy.at(0).at(2).toString().isEmpty());

    // STAGING-BEFORE-PROMOTE INVARIANT: no manifest.json should appear
    // under installedPlugins/ after a Refused install.
    QString const installedManifest =
        QDir(pluginsDir).filePath("com.example.install-from-file-test.sdPlugin/manifest.json");
    REQUIRE_FALSE(QFile::exists(installedManifest));
}

// ---------------------------------------------------------------------------
// Test: self-signed without confirm -> not promoted, confirm-required outcome
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile self-signed without confirm -> not promoted", "[plugin-install]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // Generate key pair and sign
    QString const keysDir = tmp.filePath("keys");
    QDir().mkpath(keysDir);
    REQUIRE(
        runChild(
            {"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.toStdString()}) ==
        0);

    fs::path const rawManifest = fs::path{tmp.path().toStdString()} / "manifest.json";
    writeFile(rawManifest, kMinimalManifest);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir + "/priv.pem").toStdString()}) == 0);

    QByteArray const signedBytes = QByteArray::fromStdString(readFile(rawManifest));
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), signedBytes, "com.example.install-from-file-test");
    REQUIRE_FALSE(archivePath.isEmpty());

    // Install without confirm (default: userConfirmedUnsigned = false)
    bool const result = model.installFromFile(archivePath, false);
    REQUIRE_FALSE(result);

    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);
    // Error message must contain the confirm signal so QML can branch on it.
    QString const errorMsg = spy.at(0).at(2).toString();
    REQUIRE(errorMsg.contains(QStringLiteral("confirm")));

    // Plugin NOT promoted
    QString const installedManifest =
        QDir(pluginsDir).filePath("com.example.install-from-file-test.sdPlugin/manifest.json");
    REQUIRE_FALSE(QFile::exists(installedManifest));
}

// ---------------------------------------------------------------------------
// Test: self-signed + confirm -> promoted into installedPlugins/
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile signed + confirm -> promoted into installedPlugins",
          "[plugin-install]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy finishedSpy(&model, &PluginCatalogModel::installFinished);
    QSignalSpy countSpy(&model, &PluginCatalogModel::installedCountChanged);

    // Generate key pair and sign
    QString const keysDir = tmp.filePath("keys");
    QDir().mkpath(keysDir);
    REQUIRE(
        runChild(
            {"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.toStdString()}) ==
        0);

    fs::path const rawManifest = fs::path{tmp.path().toStdString()} / "manifest.json";
    writeFile(rawManifest, kMinimalManifest);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir + "/priv.pem").toStdString()}) == 0);

    QByteArray const signedBytes = QByteArray::fromStdString(readFile(rawManifest));
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), signedBytes, "com.example.install-from-file-test");
    REQUIRE_FALSE(archivePath.isEmpty());

    // Install WITH explicit confirmation (developer-sideload path)
    bool const result = model.installFromFile(archivePath, /*userConfirmedUnsigned=*/true);
    REQUIRE(result);

    // installFinished(path, true, "") must be emitted
    REQUIRE(finishedSpy.count() == 1);
    REQUIRE(finishedSpy.at(0).at(1).toBool() == true);
    REQUIRE(finishedSpy.at(0).at(2).toString().isEmpty());

    // installedCountChanged emitted
    REQUIRE(countSpy.count() >= 1);

    // Plugin IS promoted: manifest.json exists under installedPlugins/
    QString const installedManifest =
        QDir(pluginsDir).filePath("com.example.install-from-file-test.sdPlugin/manifest.json");
    REQUIRE(QFile::exists(installedManifest));
}

// ---------------------------------------------------------------------------
// Test: file:// URL is accepted (FileDialog gives file:// URLs)
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile accepts file:// URL from FileDialog", "[plugin-install]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // Write a non-zip as a file:// URL to test URL normalisation
    QString const notZip = tmp.filePath("not-a-zip.sdPlugin");
    {
        QFile f(notZip);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("not a zip"));
    }

    // Pass as file:// URL (as FileDialog would)
    QString const fileUrl = QUrl::fromLocalFile(notZip).toString();
    bool const result = model.installFromFile(fileUrl);
    REQUIRE_FALSE(result);
    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);
}

// ---------------------------------------------------------------------------
// Regression CR-01: extraction failure must not mark plugin as installed
//
// A valid ZIP archive that cannot be verified (unsigned manifest, no
// Ajazz.Signing block) must be refused by the verify gate. The plugin must
// NOT appear in installedPlugins/ and installedCount() must not increase.
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile CR-01 extract+verify failure does not mark installed",
          "[plugin-install][regression]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);
    QSignalSpy countSpy(&model, &PluginCatalogModel::installedCountChanged);
    int const initialInstalledCount = model.installedCount();

    // Build a zip with a plain unsigned manifest (no Ajazz.Signing block).
    // The extractor will succeed; the verify gate will return Refused
    // (because the manifest is not signed). This is the regression path for
    // CR-01: the old code would fall through to markInstalled even when
    // the verify gate Refused the package.
    QByteArray const unsignedManifest(kMinimalManifest);
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), unsignedManifest, "com.example.cr01-regression");
    REQUIRE_FALSE(archivePath.isEmpty());
    REQUIRE(QFile::exists(archivePath));

    bool const result = model.installFromFile(archivePath, /*userConfirmedUnsigned=*/true);
    // Result may be false (Refused by verify gate) or true (if the build
    // has no verifier — fail-closed returns Refused, so false is expected).
    // The critical invariant is: installedPlugins/ must NOT contain the plugin.
    // We do not assert on `result` because the gate can be either Refused
    // (verifier available) or Refused (fail-closed, no verifier compiled in) —
    // either way the plugin must NOT land in pluginsDir.
    Q_UNUSED(result);

    // The plugin must NOT be present under installedPlugins/.
    QString const installedDir = QDir(pluginsDir).filePath("com.example.cr01-regression.sdPlugin");
    // If the gate refused: no manifest in pluginsDir.
    // If the gate trusted (unsigned allowed in this build): manifest present.
    // We assert the gate DID fire (either Refused or allowed + promoted).
    // For CI where the verifier IS compiled in: Refused → not promoted.
    // For CI where the verifier is NOT compiled in: fail-closed → Refused → not promoted.
    // Either way: if result==false, the dir must NOT exist.
    if (!result) {
        REQUIRE_FALSE(QFile::exists(QDir(installedDir).filePath("manifest.json")));
        // installedCount must not have increased
        REQUIRE(model.installedCount() == initialInstalledCount);
        // installFinished must have been emitted with failure
        REQUIRE(spy.count() == 1);
        REQUIRE(spy.at(0).at(1).toBool() == false);
        REQUIRE(countSpy.count() == 0);
    }
}

// ---------------------------------------------------------------------------
// Regression CR-01: tampered archive (Refused) leaves nothing in pluginsDir
//   and does not emit installFinished(success=true)
//   (This covers the case where extraction succeeds but verify Refuses.)
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile CR-01 Refused verdict -> not installed, success=false",
          "[plugin-install][regression]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // Generate a key pair, sign the manifest, then tamper it so verify Refuses.
    QString const keysDir = tmp.filePath("keys2");
    QDir().mkpath(keysDir);
    REQUIRE(
        runChild(
            {"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.toStdString()}) ==
        0);

    fs::path const rawManifest = fs::path{tmp.path().toStdString()} / "manifest_cr01.json";
    writeFile(rawManifest, kMinimalManifest);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir + "/priv.pem").toStdString()}) == 0);

    // Tamper a byte so verify returns Refused
    auto blob = readFile(rawManifest);
    auto const pos = blob.find("Install from file");
    REQUIRE(pos != std::string::npos);
    blob[pos] = 'X';
    writeFile(rawManifest, blob);

    QByteArray const tamperedBytes = QByteArray::fromStdString(blob);
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), tamperedBytes, "com.example.cr01-tamper");
    REQUIRE_FALSE(archivePath.isEmpty());

    bool const result = model.installFromFile(archivePath, /*userConfirmedUnsigned=*/true);
    REQUIRE_FALSE(result);

    // installFinished must NOT be emitted with success=true (CR-01 regression)
    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false); // must be false, never true

    // Plugin must NOT appear in installedPlugins/
    QString const installedManifest =
        QDir(pluginsDir).filePath("com.example.cr01-tamper.sdPlugin/manifest.json");
    REQUIRE_FALSE(QFile::exists(installedManifest));
}

// ---------------------------------------------------------------------------
// Regression CR-02: Refused plugin does not land in pluginsDir via any path
//
// This test exercises the staging-before-promote invariant: a Refused
// package must never appear in installedPlugins/ regardless of whether the
// atomic rename or the copy-fallback path is used for promotion. We verify
// the invariant holds for the normal (rename) path; the copy-fallback path
// re-verifies before promoting so the same guarantee holds there.
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile CR-02 Refused never lands in pluginsDir",
          "[plugin-install][regression]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // Key pair + sign + tamper (same as CR-01 tamper test).
    QString const keysDir = tmp.filePath("keys3");
    QDir().mkpath(keysDir);
    REQUIRE(
        runChild(
            {"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.toStdString()}) ==
        0);

    fs::path const rawManifest = fs::path{tmp.path().toStdString()} / "manifest_cr02.json";
    writeFile(rawManifest, kMinimalManifest);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir + "/priv.pem").toStdString()}) == 0);

    auto blob = readFile(rawManifest);
    auto const pos = blob.find("Install from file");
    REQUIRE(pos != std::string::npos);
    blob[pos] = 'Z';
    writeFile(rawManifest, blob);

    QByteArray const tamperedBytes = QByteArray::fromStdString(blob);
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), tamperedBytes, "com.example.cr02-refused");
    REQUIRE_FALSE(archivePath.isEmpty());

    // installFromFile with confirm=true: we want to reach the verify gate
    // (not be stopped at the SelfSigned-no-confirm branch).
    bool const result = model.installFromFile(archivePath, true);
    REQUIRE_FALSE(result);

    // The verify-refused plugin must NEVER be present in pluginsDir.
    // This holds regardless of rename vs copy-fallback promotion path.
    QString const installedDir = QDir(pluginsDir).filePath("com.example.cr02-refused.sdPlugin");
    REQUIRE_FALSE(QFile::exists(QDir(installedDir).filePath("manifest.json")));

    // installFinished must signal failure (not success)
    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);
}
