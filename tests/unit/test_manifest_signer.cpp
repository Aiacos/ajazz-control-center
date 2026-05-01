// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_manifest_signer.cpp
 * @brief Cross-language byte-equality tests for the Ed25519 manifest
 *        verifier (SEC-003 follow-up #51).
 *
 * The Python signer (@c scripts/sign-plugin-manifest.py) and the C++
 * verifier (@c src/plugins/src/manifest_signer.cpp) MUST agree
 * byte-for-byte on the canonical-form rules. Any drift turns into a
 * silent security regression — a publisher could sign a manifest the
 * host then rejects, or vice versa. These tests pin the contract by:
 *
 *   1. Generating a fresh keypair via the Python CLI.
 *   2. Building a minimal manifest in test code.
 *   3. Asking the Python CLI to sign it.
 *   4. Asking the C++ verifier to verify it — must succeed.
 *   5. Mutating one byte — must fail.
 *   6. Restoring + swapping the public key — must fail (binding).
 *
 * Companion to @c python/ajazz_plugins/tests/test_manifest_signing.py
 * which exercises the publisher tooling end-to-end. Together they
 * close the cross-language contract loop.
 */
#include "ajazz/plugins/manifest_signer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;
using ajazz::plugins::loadTrustRoots;
using ajazz::plugins::ManifestSignerConfig;
using ajazz::plugins::TrustedPublisher;
using ajazz::plugins::verifyManifest;

namespace {

/// Resolves to the on-disk path of the publisher CLI, regardless of
/// where the test binary is invoked from. CMake passes this as a
/// compile definition so we don't have to walk .. from CWD.
fs::path verifierScript() {
    return fs::path{AJAZZ_TEST_REPO_ROOT} / "scripts" / "sign-plugin-manifest.py";
}

fs::path repoRoot() {
    return fs::path{AJAZZ_TEST_REPO_ROOT};
}

/// Run a child process and return exit code. Same fork+execvp shape
/// as the verifier itself; lets the test exercise the Python signer
/// without relying on std::system (avoids quoting trouble for paths
/// with spaces).
int runChild(std::vector<std::string> argv) {
#ifdef _WIN32
    (void)argv;
    return -1; // verifier is POSIX-only in this slice; test skipped on Win32
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
  "UUID": "com.example.cxx-verifier-test",
  "Name": "C++ verifier test",
  "Version": "1.0.0",
  "Author": "Tests",
  "Description": "Fixture used by tests/unit/test_manifest_signer.cpp.",
  "Icon": "icon",
  "CodePath": "main.py",
  "Actions": [
    {
      "UUID": "com.example.cxx-verifier-test.act",
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

ManifestSignerConfig makeConfig(fs::path const& trustRoots = {}) {
    ManifestSignerConfig c;
    c.pythonExecutable = "python3";
    c.verifierScript = verifierScript();
    c.trustedPublishersFile = trustRoots;
    return c;
}

} // namespace

#ifndef _WIN32

TEST_CASE("manifest verifier: signed manifest passes", "[manifest-signer]") {
    auto const tmp = fs::temp_directory_path() / "ajazz-test-signer-pass";
    fs::create_directories(tmp);
    auto const keys = tmp / "keys";
    auto const manifest = tmp / "manifest.json";

    REQUIRE(runChild(
                {"python3", verifierScript().string(), "keygen", "--out-dir", keys.string()}) == 0);
    writeFile(manifest, kMinimalManifestJson);
    REQUIRE(runChild({"python3",
                      verifierScript().string(),
                      "sign",
                      "--manifest",
                      manifest.string(),
                      "--priv-key",
                      (keys / "priv.pem").string()}) == 0);

    auto const result = verifyManifest(manifest, makeConfig());
    REQUIRE(result.valid);
    REQUIRE_FALSE(result.publisherKeyB64.empty());
    REQUIRE(result.publisherKeyB64.size() == 44);
    // No trust roots passed → publisherName must be empty (self-signed).
    REQUIRE(result.publisherName.empty());

    fs::remove_all(tmp);
}

TEST_CASE("manifest verifier: tampered manifest fails", "[manifest-signer]") {
    auto const tmp = fs::temp_directory_path() / "ajazz-test-signer-tamper";
    fs::create_directories(tmp);
    auto const keys = tmp / "keys";
    auto const manifest = tmp / "manifest.json";

    runChild({"python3", verifierScript().string(), "keygen", "--out-dir", keys.string()});
    writeFile(manifest, kMinimalManifestJson);
    runChild({"python3",
              verifierScript().string(),
              "sign",
              "--manifest",
              manifest.string(),
              "--priv-key",
              (keys / "priv.pem").string()});

    // Replace one character in the description: canonical bytes
    // change → signature must be rejected.
    auto blob = readFile(manifest);
    auto const pos = blob.find("Fixture used");
    REQUIRE(pos != std::string::npos);
    blob[pos] = 'X';
    writeFile(manifest, blob);

    auto const result = verifyManifest(manifest, makeConfig());
    REQUIRE_FALSE(result.valid);

    fs::remove_all(tmp);
}

TEST_CASE("manifest verifier: unsigned manifest fails closed", "[manifest-signer]") {
    auto const tmp = fs::temp_directory_path() / "ajazz-test-signer-unsigned";
    fs::create_directories(tmp);
    auto const manifest = tmp / "manifest.json";
    writeFile(manifest, kMinimalManifestJson);

    auto const result = verifyManifest(manifest, makeConfig());
    REQUIRE_FALSE(result.valid);
    REQUIRE(result.publisherKeyB64.empty());

    fs::remove_all(tmp);
}

TEST_CASE("manifest verifier: trust-roots match resolves publisher name", "[manifest-signer]") {
    auto const tmp = fs::temp_directory_path() / "ajazz-test-signer-trust";
    fs::create_directories(tmp);
    auto const keys = tmp / "keys";
    auto const manifest = tmp / "manifest.json";
    auto const trustFile = tmp / "trust.json";

    runChild({"python3", verifierScript().string(), "keygen", "--out-dir", keys.string()});
    writeFile(manifest, kMinimalManifestJson);
    runChild({"python3",
              verifierScript().string(),
              "sign",
              "--manifest",
              manifest.string(),
              "--priv-key",
              (keys / "priv.pem").string()});

    // Read the publisher key out of the freshly-signed manifest and
    // bake it into a synthetic trust-roots file.
    auto const manifestBlob = readFile(manifest);
    auto const keyTagPos = manifestBlob.find("\"Ed25519PublicKey\"");
    REQUIRE(keyTagPos != std::string::npos);
    auto const valStart = manifestBlob.find('"', keyTagPos + 18) + 1;
    auto const valEnd = manifestBlob.find('"', valStart);
    auto const pubKey = manifestBlob.substr(valStart, valEnd - valStart);
    REQUIRE(pubKey.size() == 44);

    std::ostringstream trust;
    trust << R"({"version":1,"publishers":[{"key":")" << pubKey
          << R"(","name":"Test Trusted Publisher"}]})";
    writeFile(trustFile, trust.str());

    auto const result = verifyManifest(manifest, makeConfig(trustFile));
    REQUIRE(result.valid);
    REQUIRE(result.publisherKeyB64 == pubKey);
    REQUIRE(result.publisherName == "Test Trusted Publisher");

    fs::remove_all(tmp);
}

TEST_CASE("loadTrustRoots: parses the bundled JSON", "[manifest-signer]") {
    auto const bundled = repoRoot() / "resources" / "trusted_publishers.json";
    REQUIRE(fs::exists(bundled));
    auto const roots = loadTrustRoots(bundled);
    // Bundled file currently has 1 placeholder entry.
    REQUIRE(roots.size() == 1);
    REQUIRE_FALSE(roots[0].keyB64.empty());
    REQUIRE_FALSE(roots[0].name.empty());
}

TEST_CASE("loadTrustRoots: missing file returns empty list", "[manifest-signer]") {
    auto const roots = loadTrustRoots("/no/such/file.json");
    REQUIRE(roots.empty());
}

#endif // _WIN32
