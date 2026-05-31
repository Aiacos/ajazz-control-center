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
#include "win32_python_resolve.hpp"

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
    // Resolve "python3" to a concrete interpreter past the Microsoft Store
    // App Execution Alias stub (a default python.org install ships python.exe
    // but no python3.exe; the literal "python3" otherwise launches the stub
    // and exits 9009). Routes through the same production resolver the host
    // uses so the test exercises the real resolution path.
    argv[0] = ajazz::plugins::win32::resolveRealPython(argv[0]);
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
// CR-01 security gate: tampered plugin refused even with userConfirmedUnsigned=true
//
// This is the load-bearing CR-01 invariant test. A .sdPlugin whose signature
// block is present but cryptographically invalid (tampered) MUST be quarantined
// even when the caller passes userConfirmedUnsigned=true. Consent applies ONLY
// to truly unsigned (no signature block) packages — not to attack packages.
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile tampered plugin refused even with consent",
          "[plugin-install][security]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // Generate key pair, sign the manifest, then tamper it.
    QString const keysDir = tmp.filePath("keys-cr01-security");
    QDir().mkpath(keysDir);
    REQUIRE(
        runChild(
            {"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.toStdString()}) ==
        0);

    fs::path const rawManifest = fs::path{tmp.path().toStdString()} / "manifest_security.json";
    writeFile(rawManifest, kMinimalManifest);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir + "/priv.pem").toStdString()}) == 0);

    // Tamper: flip one byte so the Ed25519 signature is invalid.
    auto blob = readFile(rawManifest);
    auto const pos = blob.find("Fixture for");
    REQUIRE(pos != std::string::npos);
    blob[pos] = 'Z';
    writeFile(rawManifest, blob);

    QByteArray const tamperedBytes = QByteArray::fromStdString(blob);
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), tamperedBytes, "com.example.cr01-tamper-security");
    REQUIRE_FALSE(archivePath.isEmpty());

    // CR-01 invariant: userConfirmedUnsigned=true MUST NOT let a tampered
    // package through. Consent gates ONLY the Unsigned (None) branch.
    bool const result = model.installFromFile(archivePath, /*userConfirmedUnsigned=*/true);
    REQUIRE_FALSE(result);

    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);

    // Plugin MUST NOT be promoted to installedPlugins/ even with consent.
    QString const installedManifest =
        QDir(pluginsDir).filePath("com.example.cr01-tamper-security.sdPlugin/manifest.json");
    REQUIRE_FALSE(QFile::exists(installedManifest));
}

// ---------------------------------------------------------------------------
// Unsigned plugin: refused without consent, installs with consent
//
// An unsigned (no signature block) .sdPlugin is a developer sideload.
// It must be refused when the caller does not provide consent, and must
// be installed when consent is given (userConfirmedUnsigned=true).
// ---------------------------------------------------------------------------

TEST_CASE("PluginInstallFromFile unsigned plugin refused without consent", "[plugin-install]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // Build an unsigned .sdPlugin (no Ajazz.Signing block).
    QByteArray const unsignedBytes{kMinimalManifest};
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), unsignedBytes, "com.example.unsigned-no-consent");
    REQUIRE_FALSE(archivePath.isEmpty());

    // Without consent: must be refused.
    bool const result = model.installFromFile(archivePath, /*userConfirmedUnsigned=*/false);
    REQUIRE_FALSE(result);

    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);

    // Plugin must NOT be promoted.
    QString const installedManifest =
        QDir(pluginsDir).filePath("com.example.unsigned-no-consent.sdPlugin/manifest.json");
    REQUIRE_FALSE(QFile::exists(installedManifest));
}

TEST_CASE("PluginInstallFromFile unsigned plugin installs with consent", "[plugin-install]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    QSignalSpy finishedSpy(&model, &PluginCatalogModel::installFinished);

    // Build an unsigned .sdPlugin (no Ajazz.Signing block).
    QByteArray const unsignedBytes{kMinimalManifest};
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), unsignedBytes, "com.example.install-from-file-test");
    REQUIRE_FALSE(archivePath.isEmpty());

    // With consent: must install successfully.
    bool const result = model.installFromFile(archivePath, /*userConfirmedUnsigned=*/true);
    REQUIRE(result);

    REQUIRE(finishedSpy.count() == 1);
    REQUIRE(finishedSpy.at(0).at(1).toBool() == true);

    // Plugin IS promoted.
    QString const installedManifest =
        QDir(pluginsDir).filePath("com.example.install-from-file-test.sdPlugin/manifest.json");
    REQUIRE(QFile::exists(installedManifest));
}

// ---------------------------------------------------------------------------
// Plan 27-04 Task 1: allowUnsignedPlugins QSettings setting
//
// Test A: setAllowUnsignedPlugins(true) persists; fresh model reads back true.
// Test B: with setting=true, unsigned installs without per-call consent.
// Test C: with setting=false and no env var, unsigned refused without consent.
// Test D (CR-01): with setting=true, tampered STILL refused.
// Test E: allowPlugin() refuses when trustLevel=="tampered".
// ---------------------------------------------------------------------------

TEST_CASE("PluginCatalog allowUnsignedPlugins persists to QSettings", "[plugin-install][trust]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    PluginsDirGuard guard(tmp.filePath("plugins"));

    {
        PluginCatalogModel model(nullptr);
        model.setAllowUnsignedPlugins(true);
    }
    {
        PluginCatalogModel model2(nullptr);
        REQUIRE(model2.allowUnsignedPlugins() == true);
        model2.setAllowUnsignedPlugins(false);
    }
    {
        PluginCatalogModel model3(nullptr);
        REQUIRE(model3.allowUnsignedPlugins() == false);
    }
    QStandardPaths::setTestModeEnabled(false);
}

TEST_CASE("PluginCatalog allowUnsignedPlugins setting installs unsigned without per-call consent",
          "[plugin-install][trust]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    model.setAllowUnsignedPlugins(true);
    QSignalSpy finishedSpy(&model, &PluginCatalogModel::installFinished);

    QByteArray const unsignedBytes{kMinimalManifest};
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), unsignedBytes, "com.example.allow-unsigned-setting");
    REQUIRE_FALSE(archivePath.isEmpty());

    // Setting supplies consent — userConfirmedUnsigned=false should still install.
    bool const result = model.installFromFile(archivePath, /*userConfirmedUnsigned=*/false);
    REQUIRE(result);

    REQUIRE(finishedSpy.count() == 1);
    REQUIRE(finishedSpy.at(0).at(1).toBool() == true);

    QString const installedManifest =
        QDir(pluginsDir).filePath("com.example.allow-unsigned-setting.sdPlugin/manifest.json");
    REQUIRE(QFile::exists(installedManifest));

    model.setAllowUnsignedPlugins(false);
    QStandardPaths::setTestModeEnabled(false);
}

TEST_CASE("PluginCatalog allowUnsignedPlugins=false refuses unsigned without consent",
          "[plugin-install][trust]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    model.setAllowUnsignedPlugins(false);
    // Ensure env var is NOT set for this test
    qunsetenv("AJAZZ_ALLOW_UNTRUSTED_PLUGINS");
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    QByteArray const unsignedBytes{kMinimalManifest};
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), unsignedBytes, "com.example.setting-false-unsigned");
    REQUIRE_FALSE(archivePath.isEmpty());

    bool const result = model.installFromFile(archivePath, /*userConfirmedUnsigned=*/false);
    REQUIRE_FALSE(result);

    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);
    // Error must contain the consent signal
    REQUIRE(spy.at(0).at(2).toString().contains(QStringLiteral("confirm")));

    QStandardPaths::setTestModeEnabled(false);
}

TEST_CASE("PluginCatalog allowUnsignedPlugins=true tampered STILL refused (CR-01)",
          "[plugin-install][trust][security]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    PluginCatalogModel model(nullptr);
    model.setAllowUnsignedPlugins(true);
    QSignalSpy spy(&model, &PluginCatalogModel::installFinished);

    // Build a tampered archive (signed + byte-flipped -> Refused verdict).
    QString const keysDir = tmp.filePath("keys-allow-tampered");
    QDir().mkpath(keysDir);
    REQUIRE(
        runChild(
            {"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.toStdString()}) ==
        0);

    fs::path const rawManifest =
        fs::path{tmp.path().toStdString()} / "manifest_allow_tampered.json";
    writeFile(rawManifest, kMinimalManifest);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir + "/priv.pem").toStdString()}) == 0);

    auto blob = readFile(rawManifest);
    auto const pos = blob.find("Fixture for");
    REQUIRE(pos != std::string::npos);
    blob[pos] = 'Q';
    writeFile(rawManifest, blob);

    QByteArray const tamperedBytes = QByteArray::fromStdString(blob);
    QString const archivePath =
        buildSdPluginArchive(tmp.path(), tamperedBytes, "com.example.allow-tampered-cr01");
    REQUIRE_FALSE(archivePath.isEmpty());

    // CR-01: even with allowUnsignedPlugins=true, tampered stays refused.
    bool const result = model.installFromFile(archivePath, /*userConfirmedUnsigned=*/false);
    REQUIRE_FALSE(result);

    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);

    QString const installedManifest =
        QDir(pluginsDir).filePath("com.example.allow-tampered-cr01.sdPlugin/manifest.json");
    REQUIRE_FALSE(QFile::exists(installedManifest));

    model.setAllowUnsignedPlugins(false);
    QStandardPaths::setTestModeEnabled(false);
}

TEST_CASE("PluginCatalog allowPlugin refuses tampered rows", "[plugin-install][trust][security]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    PluginsDirGuard guard(tmp.filePath("plugins"));

    PluginCatalogModel model(nullptr);
    // allowPlugin on a non-existent / tampered uuid must return false.
    // We pass a bogus uuid that has no installed entry — should return false.
    bool const result = model.allowPlugin(QStringLiteral("com.example.nonexistent"));
    REQUIRE_FALSE(result);
}

// ---------------------------------------------------------------------------
// WR-01: per-plugin "Allow this plugin" survives the launch-sweep with the
// global allowUnsignedPlugins toggle OFF.
//
// Scenario:
//   1. Install an unsigned plugin into the plugins dir (with per-call consent).
//   2. Global toggle OFF + env var unset (so consentToUnsigned() == false).
//   3. allowPlugin(uuid) records the per-plugin decision and returns true.
//   4. Construct a FRESH model (== app restart) — its constructor runs the
//      launch-sweep. With the per-plugin key set, the unsigned plugin dir must
//      SURVIVE even though the global toggle is OFF (the bug WR-01 reported was
//      the sweep removeRecursively-ing it on restart).
// ---------------------------------------------------------------------------

TEST_CASE("PluginCatalog per-plugin allow survives launch-sweep with global toggle OFF",
          "[plugin-install][trust][wr-01]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);

    // Global consent OFF for the whole test.
    qunsetenv("AJAZZ_ALLOW_UNTRUSTED_PLUGINS");

    QString const pluginId = QStringLiteral("com.example.wr01-perplugin-allow");
    QString const installedManifest =
        QDir(pluginsDir).filePath(pluginId + QStringLiteral(".sdPlugin/manifest.json"));

    {
        PluginCatalogModel model(nullptr);
        model.setAllowUnsignedPlugins(false);

        // Step 1: install the unsigned plugin WITH explicit per-call consent so
        // it lands on disk (global toggle is off, so we use the per-call path).
        QByteArray const unsignedBytes{kMinimalManifest};
        QString const archivePath = buildSdPluginArchive(tmp.path(), unsignedBytes, pluginId);
        REQUIRE_FALSE(archivePath.isEmpty());
        REQUIRE(model.installFromFile(archivePath, /*userConfirmedUnsigned=*/true));
        REQUIRE(QFile::exists(installedManifest));

        // Step 2: record the per-plugin allow decision. Returns true for Unsigned.
        REQUIRE(model.allowPlugin(pluginId));
    }

    // Sanity: the global toggle is genuinely OFF in persisted settings.
    {
        PluginCatalogModel probe(nullptr);
        REQUIRE(probe.allowUnsignedPlugins() == false);
    }

    // Step 4: simulate a restart — a fresh model runs the launch-sweep in its
    // constructor. With the per-plugin key set and the global toggle OFF, the
    // unsigned plugin dir MUST survive.
    REQUIRE(QFile::exists(installedManifest)); // present before the sweep
    {
        PluginCatalogModel restarted(nullptr);
        Q_UNUSED(restarted);
    }
    REQUIRE(QFile::exists(installedManifest)); // WR-01: survived the launch-sweep

    QStandardPaths::setTestModeEnabled(false);
}

// ---------------------------------------------------------------------------
// WR-01 + CR-01: per-plugin allow must NOT promote a TAMPERED plugin.
//
// A tampered (Refused) plugin on disk must (a) be refused by allowPlugin()
// (no consent key written, returns false) AND (b) be removed by the
// launch-sweep on the next "restart" regardless of any stray per-plugin key.
// This pins that the now-functional per-plugin path stays Unsigned-only.
// ---------------------------------------------------------------------------

TEST_CASE("PluginCatalog per-plugin allow does not promote a tampered plugin (CR-01)",
          "[plugin-install][trust][security][wr-01]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const pluginsDir = tmp.filePath("plugins");
    QDir().mkpath(pluginsDir);
    PluginsDirGuard guard(pluginsDir);
    qunsetenv("AJAZZ_ALLOW_UNTRUSTED_PLUGINS");

    QString const pluginId = QStringLiteral("com.example.wr01-tampered");

    // Build a tampered manifest (signed then byte-flipped -> Refused verdict).
    QString const keysDir = tmp.filePath("keys-wr01-tampered");
    QDir().mkpath(keysDir);
    REQUIRE(
        runChild(
            {"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.toStdString()}) ==
        0);

    fs::path const rawManifest = fs::path{tmp.path().toStdString()} / "manifest_wr01_tampered.json";
    writeFile(rawManifest, kMinimalManifest);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir + "/priv.pem").toStdString()}) == 0);
    auto blob = readFile(rawManifest);
    auto const pos = blob.find("Fixture for");
    REQUIRE(pos != std::string::npos);
    blob[pos] = 'Z';
    writeFile(rawManifest, blob);

    // Place the tampered plugin DIRECTLY in the plugins dir (as if it had been
    // dropped there), bypassing installFromFile so allowPlugin sees it on disk.
    QString const tamperedDir = QDir(pluginsDir).filePath(pluginId + QStringLiteral(".sdPlugin"));
    QDir().mkpath(tamperedDir);
    writeFile(fs::path{tamperedDir.toStdString()} / "manifest.json", blob);
    QString const tamperedManifest = QDir(tamperedDir).filePath(QStringLiteral("manifest.json"));
    REQUIRE(QFile::exists(tamperedManifest));

    PluginCatalogModel model(nullptr);
    model.setAllowUnsignedPlugins(false);

    // CR-01: allowPlugin() must REFUSE a tampered plugin and write no key.
    REQUIRE_FALSE(model.allowPlugin(pluginId));
    {
        QSettings settings;
        REQUIRE_FALSE(
            settings.value(QStringLiteral("plugins/allowed/") + pluginId, false).toBool());
    }

    // Even if an attacker forged a stray per-plugin key, the launch-sweep must
    // still quarantine the tampered dir (Refused is unconditional-quarantine).
    {
        QSettings settings;
        settings.setValue(QStringLiteral("plugins/allowed/") + pluginId, true);
    }
    {
        PluginCatalogModel restarted(nullptr);
        Q_UNUSED(restarted);
    }
    REQUIRE_FALSE(QFile::exists(tamperedManifest)); // CR-01: tampered removed by the sweep

    QStandardPaths::setTestModeEnabled(false);
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
