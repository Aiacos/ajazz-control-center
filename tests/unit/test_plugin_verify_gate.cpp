// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_verify_gate.cpp
 * @brief Phase 22 Plan 22-01 (PLUGIN-14): unit suite for the Ed25519 verify gate.
 *
 * Tests the three-way verdict and the fail-closed contract:
 *   - signed (self-signed key) staged manifest -> SelfSigned
 *   - tampered manifest -> Refused
 *   - unsigned manifest -> Refused
 *   - signer unavailable (empty verifierScript) -> Refused (fail-closed, no crash)
 *   - verdictToTrustLevel vocabulary matches LoadedPluginsModel::trustLevelOf strings
 *
 * Fixture recipe follows test_manifest_signer.cpp:
 *   keygen  via python3 sign-plugin-manifest.py keygen --out-dir <keys>
 *   sign    via python3 sign-plugin-manifest.py sign   --manifest <m> --priv-key <keys/priv.pem>
 *   tamper  by flipping a byte in the signed manifest
 *   unsigned via a manifest with no Ajazz.Signing block
 *
 * Archive fixture recipe follows test_sdplugin_extractor.cpp:
 *   QZipWriter to build a synthetic .sdPlugin zip, then extractSdPluginArchive into tmp.
 *
 * ASCII-only TEST_CASE names + tags per CLAUDE.md (Win32 CMD codepage guard).
 */
#include "plugin_verify_gate.hpp"
#include "sdplugin_extractor.hpp"

#include <QDir>
#include <QFile>
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
using ajazz::app::makeSignerConfig;
using ajazz::app::verdictToTrustLevel;
using ajazz::app::verifyStagedPlugin;
using ajazz::app::VerifyVerdict;
using ajazz::plugins::ManifestSignerConfig;

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

constexpr char const* kMinimalManifestJson = R"({
  "UUID": "com.example.verify-gate-test",
  "Name": "Verify gate test",
  "Version": "1.0.0",
  "Author": "Tests",
  "Description": "Fixture for test_plugin_verify_gate.cpp.",
  "Icon": "icon",
  "CodePath": "main.py",
  "Actions": [
    {
      "UUID": "com.example.verify-gate-test.act",
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

/// Build a synthetic .sdPlugin zip and extract it into stagingDir.
/// Returns the path to the extracted manifest.json, or empty on failure.
QString buildAndExtractSdPlugin(QString const& stagingDir,
                                QByteArray const& manifestBytes,
                                QString const& pluginId) {
    QString const archivePath = stagingDir + "/" + pluginId + ".sdPlugin";
    {
        QZipWriter zip(archivePath);
        if (zip.status() != QZipWriter::NoError) {
            return {};
        }
        zip.addFile(pluginId + ".sdPlugin/manifest.json", manifestBytes);
        zip.close();
    }
    if (!ajazz::app::extractSdPluginArchive(archivePath, stagingDir, pluginId + ".sdPlugin")) {
        return {};
    }
    QFile::remove(archivePath);
    return stagingDir + "/" + pluginId + ".sdPlugin/manifest.json";
}

} // namespace

// ---------------------------------------------------------------------------
// verdictToTrustLevel vocabulary (pure logic, no filesystem)
// ---------------------------------------------------------------------------

TEST_CASE("PluginVerifyGate verdictToTrustLevel: Trusted maps to trusted", "[plugin-verify-gate]") {
    REQUIRE(verdictToTrustLevel(VerifyVerdict::Trusted) == QStringLiteral("trusted"));
}

TEST_CASE("PluginVerifyGate verdictToTrustLevel: SelfSigned maps to self-signed",
          "[plugin-verify-gate]") {
    REQUIRE(verdictToTrustLevel(VerifyVerdict::SelfSigned) == QStringLiteral("self-signed"));
}

TEST_CASE("PluginVerifyGate verdictToTrustLevel: Refused maps to unsigned",
          "[plugin-verify-gate]") {
    REQUIRE(verdictToTrustLevel(VerifyVerdict::Refused) == QStringLiteral("unsigned"));
}

// ---------------------------------------------------------------------------
// Signer unavailable (fail-closed) — no filesystem, no subprocess
// ---------------------------------------------------------------------------

TEST_CASE("PluginVerifyGate signer unavailable -> Refused no crash", "[plugin-verify-gate]") {
    // Inject an empty verifierScript to exercise the fail-closed branch even
    // on production builds where AJAZZ_PLUGIN_VERIFIER_SCRIPT is defined.
    // We construct a config with a non-empty verifierScript pointing at a
    // non-existent path so verifyStagedPlugin uses the caller's override
    // rather than makeSignerConfig(), then the verifyManifest call itself
    // fails closed (exit != 0 / script missing).
    ManifestSignerConfig emptyCfg;
    emptyCfg.verifierScript = "/nonexistent-path/no-such-script.py";
    emptyCfg.trustedPublishersFile = fs::path{};

    // verifyStagedPlugin with a non-existent script should return Refused.
    auto const outcome = verifyStagedPlugin("/nonexistent/manifest.json", emptyCfg);
    REQUIRE(outcome.verdict == VerifyVerdict::Refused);
    REQUIRE_FALSE(outcome.reason.isEmpty());
}

// ---------------------------------------------------------------------------
// Signed (self-signed) staged manifest -> SelfSigned
// ---------------------------------------------------------------------------

TEST_CASE("PluginVerifyGate signed manifest -> SelfSigned", "[plugin-verify-gate]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // 1. keygen
    fs::path const keysDir = tmp.filePath("keys").toStdString();
    fs::create_directories(keysDir);
    REQUIRE(
        runChild({"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.string()}) ==
        0);

    // 2. Build manifest, sign it
    fs::path const rawManifest = fs::path{tmp.path().toStdString()} / "manifest_raw.json";
    writeFile(rawManifest, kMinimalManifestJson);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir / "priv.pem").string()}) == 0);

    // 3. Build .sdPlugin zip with the signed manifest and extract it
    QByteArray const signedBytes = QByteArray::fromStdString(readFile(rawManifest));
    QString const manifestPath =
        buildAndExtractSdPlugin(tmp.path(), signedBytes, "com.example.verify-gate-signed");
    REQUIRE_FALSE(manifestPath.isEmpty());
    REQUIRE(QFile::exists(manifestPath));

    // 4. Verify via the gate — expect SelfSigned (placeholder trust roots,
    //    so the key is not in trusted_publishers.json = Pitfall 4)
    auto const outcome = verifyStagedPlugin(manifestPath);
    REQUIRE(outcome.verdict == VerifyVerdict::SelfSigned);
    REQUIRE_FALSE(outcome.reason.isEmpty());
}

// ---------------------------------------------------------------------------
// Tampered manifest -> Refused
// ---------------------------------------------------------------------------

TEST_CASE("PluginVerifyGate tampered manifest -> Refused", "[plugin-verify-gate]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    fs::path const keysDir = tmp.filePath("keys").toStdString();
    fs::create_directories(keysDir);
    REQUIRE(
        runChild({"python3", verifierScript().string(), "keygen", "--out-dir", keysDir.string()}) ==
        0);

    fs::path const rawManifest = fs::path{tmp.path().toStdString()} / "manifest_tamper.json";
    writeFile(rawManifest, kMinimalManifestJson);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      rawManifest.string(),
                      "--priv-key",
                      (keysDir / "priv.pem").string()}) == 0);

    // Tamper: flip a byte in the description field after signing
    auto blob = readFile(rawManifest);
    auto const pos = blob.find("Fixture for");
    REQUIRE(pos != std::string::npos);
    blob[pos] = 'X';
    writeFile(rawManifest, blob);

    QByteArray const tamperedBytes = QByteArray::fromStdString(blob);
    QString const manifestPath =
        buildAndExtractSdPlugin(tmp.path(), tamperedBytes, "com.example.verify-gate-tampered");
    REQUIRE_FALSE(manifestPath.isEmpty());

    auto const outcome = verifyStagedPlugin(manifestPath);
    REQUIRE(outcome.verdict == VerifyVerdict::Refused);
}

// ---------------------------------------------------------------------------
// Unsigned manifest -> Refused
// ---------------------------------------------------------------------------

TEST_CASE("PluginVerifyGate unsigned manifest -> Refused", "[plugin-verify-gate]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // No sign step — use the raw manifest with no Ajazz.Signing block
    QByteArray const unsignedBytes{kMinimalManifestJson};
    QString const manifestPath =
        buildAndExtractSdPlugin(tmp.path(), unsignedBytes, "com.example.verify-gate-unsigned");
    REQUIRE_FALSE(manifestPath.isEmpty());

    auto const outcome = verifyStagedPlugin(manifestPath);
    REQUIRE(outcome.verdict == VerifyVerdict::Refused);
}
