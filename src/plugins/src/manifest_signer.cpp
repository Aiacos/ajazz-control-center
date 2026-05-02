// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file manifest_signer.cpp
 * @brief POSIX implementation of the Ed25519 manifest verifier.
 *
 * Strategy: spawn `python3 scripts/sign-plugin-manifest.py verify
 * --manifest <path>` and treat exit code 0 as "valid signature". On
 * success, parse the manifest's `Ajazz.Signing.Ed25519PublicKey`
 * via the existing zero-dep JSON helpers and look the key up in
 * the bundled trust roots.
 *
 * Errors fail closed (`valid=false`):
 *   - python3 not on PATH or fails to start.
 *   - Verifier script missing.
 *   - Manifest malformed / no `Ed25519PublicKey` field.
 *   - Trust roots JSON missing or malformed (we still treat the
 *     signature as valid, just everyone becomes "self-signed").
 *
 * Win32 backend lands in a follow-up of #51 — the public header
 * contract is platform-agnostic so callers don't need an `#ifdef`.
 */
#ifndef _WIN32

#include "ajazz/plugins/manifest_signer.hpp"

#include "wire_protocol.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

extern "C" char** environ;

namespace ajazz::plugins {
namespace {

/// Read a small text file (≤ a few MB) into memory. Returns empty
/// string on any I/O error — verification fails closed.
std::string readFile(std::filesystem::path const& path) {
    std::ifstream f{path, std::ios::binary};
    if (!f) {
        return {};
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

/// Spawn a child process, wait for it, return exit code or -1 on
/// fork/exec failure. Inherits stdin/stdout from the parent so the
/// verifier's `::error::` annotations show up in the host's logs.
int runChild(std::vector<std::string> const& argv) {
    std::vector<char*> rawArgv;
    rawArgv.reserve(argv.size() + 1);
    for (auto const& s : argv) {
        rawArgv.push_back(const_cast<char*>(s.c_str()));
    }
    rawArgv.push_back(nullptr);

    auto const pid = ::fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        // Child: replace ourselves with python3.
        ::execvp(rawArgv[0], rawArgv.data());
        // execvp only returns on failure — print and bail with a
        // distinctive exit code so a debugger can tell exec failure
        // apart from a normal verifier rejection.
        std::fprintf(stderr, "exec %s: %s\n", rawArgv[0], std::strerror(errno));
        std::_Exit(127);
    }
    // Parent: block on the child.
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/// Pull `Ajazz.Signing.Ed25519PublicKey` out of a manifest blob.
/// The mini-JSON helper only matches the literal `"key":"value"`
/// pair, so a manifest with multiple `"Ed25519PublicKey":` strings
/// (e.g. one in a comment) would be ambiguous; the schema forbids
/// this so in practice the first match is the right one.
std::string extractPublicKey(std::string_view manifestBlob) {
    return wire::findStringField(manifestBlob, "Ed25519PublicKey");
}

} // namespace

std::vector<TrustedPublisher> loadTrustRoots(std::filesystem::path const& jsonPath) {
    auto const blob = readFile(jsonPath);
    if (blob.empty()) {
        return {};
    }
    // The publishers list is `"publishers":[ {…}, {…} ]`. The mini
    // JSON helper doesn't do nested objects, so we walk it manually:
    // for each `"key":"<value>"` followed by a `"name":"<value>"`
    // *within the same JSON object* (closing `}` boundary), emit a
    // TrustedPublisher. Bounding by `}` prevents a malformed entry
    // (key without name) from cross-pairing with the next entry's
    // name — well-formed input is unaffected, malformed input fails
    // closed (no entry emitted) instead of pairing across entries.
    std::vector<TrustedPublisher> out;
    std::string_view view{blob};
    std::size_t cursor = 0;
    while (cursor < view.size()) {
        auto const remaining = view.substr(cursor);
        auto const key = wire::findStringField(remaining, "key");
        if (key.empty()) {
            break;
        }
        auto const keyPos = remaining.find("\"key\"");
        if (keyPos == std::string_view::npos) {
            break;
        }
        // Window = from `"key"` to the next `}` (entry boundary), or
        // 512 bytes if `}` isn't found within that range. The 512-byte
        // cap exists for robustness against pathological input; a
        // single publisher entry is always far smaller.
        auto const closeBrace = remaining.find('}', keyPos);
        auto const windowLen = closeBrace == std::string_view::npos
                                   ? std::min<std::size_t>(512, remaining.size() - keyPos)
                                   : std::min<std::size_t>(closeBrace - keyPos + 1, 512);
        auto const window = remaining.substr(keyPos, windowLen);
        auto const name = wire::findStringField(window, "name");
        if (!name.empty()) {
            out.push_back({key, name});
        }
        // Advance past this entry to find the next "key".
        cursor += keyPos + 5; // past `"key"` token
    }
    return out;
}

ManifestVerifyResult verifyManifest(std::filesystem::path const& manifestPath,
                                    ManifestSignerConfig const& config) {
    ManifestVerifyResult result;

    if (config.verifierScript.empty() || !std::filesystem::exists(config.verifierScript)) {
        return result; // valid=false
    }
    if (!std::filesystem::exists(manifestPath)) {
        return result;
    }

    std::vector<std::string> const argv = {
        config.pythonExecutable,
        config.verifierScript.string(),
        "verify",
        "--manifest",
        manifestPath.string(),
    };
    int const rc = runChild(argv);
    if (rc != 0) {
        return result; // signature invalid OR exec failure
    }

    // Signature verified; now figure out the publisher.
    auto const manifestBlob = readFile(manifestPath);
    result.publisherKeyB64 = extractPublicKey(manifestBlob);
    result.valid = true;

    auto const trustRoots = loadTrustRoots(config.trustedPublishersFile);
    for (auto const& publisher : trustRoots) {
        if (publisher.keyB64 == result.publisherKeyB64) {
            result.publisherName = publisher.name;
            return result;
        }
    }
    // Verified, but key not in trust roots → self-signed.
    result.publisherName.clear();
    return result;
}

} // namespace ajazz::plugins

#endif // _WIN32
