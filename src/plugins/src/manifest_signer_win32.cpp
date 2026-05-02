// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file manifest_signer_win32.cpp
 * @brief Windows implementation of the Ed25519 manifest verifier
 *        (SEC-003 follow-up #51, sibling of @c manifest_signer.cpp).
 *
 * Mirrors the POSIX backend method-for-method but substitutes the
 * `fork()` + `execvp()` + `waitpid()` triplet with the CRT
 * `_wspawnvp(_P_WAIT, …)` call. The verifier doesn't need pipe
 * plumbing or sandbox attribute lists — it inherits stdio from the
 * parent so the Python script's `::error::` annotations land in the
 * host's console — which is exactly what `_wspawnvp` does by default.
 *
 * Why @c _wspawnvp over @c CreateProcessW:
 *   1. The OOP plugin host already uses `CreateProcessW` because it
 *      needs `STARTUPINFOEX` for pipe redirection + AppContainer
 *      capabilities. The verifier has neither requirement.
 *   2. `_wspawnvp` does the command-line escaping internally, so we
 *      don't duplicate the `appendQuoted` / `buildCommandLine`
 *      helpers from `out_of_process_plugin_host_win32.cpp`.
 *   3. The signature `int runChild(args)` matches the POSIX backend
 *      one-for-one — exit code on success, -1 on spawn failure.
 *
 * Cross-platform contract: the C++ verifier and the Python signer
 * agree byte-for-byte on the canonical-form rules, regardless of
 * how the subprocess is spawned. The contract is pinned by
 * @c tests/unit/test_manifest_signer.cpp (compiled here too once
 * the test guards drop in slice (C)).
 */
#ifdef _WIN32

#include "ajazz/plugins/manifest_signer.hpp"
#include "wire_protocol.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <process.h>
#include <windows.h>

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

/// UTF-8 → UTF-16 conversion for paths and argv. The verifier deals
/// with publisher-supplied paths that may contain non-ASCII bytes,
/// so a naive `string`→`wstring` widen would corrupt them on a
/// non-CP_UTF8 console. Returns empty on conversion failure.
std::wstring utf8ToWide(std::string const& s) {
    if (s.empty()) {
        return {};
    }
    int const size =
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out;
    out.resize(static_cast<std::size_t>(size));
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), size);
    return out;
}

/// Spawn a child process synchronously, return its exit code, or -1
/// on spawn failure. `_wspawnvp(_P_WAIT, …)` returns the child's
/// exit code on success and -1 on failure (errno set), so the
/// returned value drops in for the POSIX `runChild` contract.
int runChild(std::vector<std::string> const& argv) {
    if (argv.empty()) {
        return -1;
    }
    std::vector<std::wstring> wide;
    wide.reserve(argv.size());
    for (auto const& s : argv) {
        wide.push_back(utf8ToWide(s));
    }
    std::vector<wchar_t const*> rawArgv;
    rawArgv.reserve(wide.size() + 1);
    for (auto const& w : wide) {
        rawArgv.push_back(w.c_str());
    }
    rawArgv.push_back(nullptr);

    intptr_t const rc = ::_wspawnvp(_P_WAIT, rawArgv[0], rawArgv.data());
    if (rc < 0) {
        std::fprintf(stderr, "spawn %s: errno %d\n", argv[0].c_str(), errno);
        return -1;
    }
    return static_cast<int>(rc);
}

/// Pull `Ajazz.Signing.Ed25519PublicKey` out of a manifest blob.
/// Same single-string-field grep as the POSIX backend.
std::string extractPublicKey(std::string_view manifestBlob) {
    return wire::findStringField(manifestBlob, "Ed25519PublicKey");
}

} // namespace

std::vector<TrustedPublisher> loadTrustRoots(std::filesystem::path const& jsonPath) {
    auto const blob = readFile(jsonPath);
    if (blob.empty()) {
        return {};
    }
    // Walk `"key":"…"` then `"name":"…"` pairs bounded by the entry's
    // closing `}` — duplicates the POSIX backend logic. Mirroring is
    // intentional: any drift would be a cross-platform contract bug
    // invisible to the test that runs on the dev's machine. See the
    // POSIX implementation for the full rationale on the `}` bound.
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
        auto const closeBrace = remaining.find('}', keyPos);
        auto const windowLen = closeBrace == std::string_view::npos
                                   ? std::min<std::size_t>(512, remaining.size() - keyPos)
                                   : std::min<std::size_t>(closeBrace - keyPos + 1, 512);
        auto const window = remaining.substr(keyPos, windowLen);
        auto const name = wire::findStringField(window, "name");
        if (!name.empty()) {
            out.push_back({key, name});
        }
        cursor += keyPos + 5; // past `"key"` token
    }
    return out;
}

ManifestVerifyResult verifyManifest(std::filesystem::path const& manifestPath,
                                    ManifestSignerConfig const& config) {
    ManifestVerifyResult result;

    if (config.verifierScript.empty() || !std::filesystem::exists(config.verifierScript)) {
        return result;
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
        return result;
    }

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
    result.publisherName.clear();
    return result;
}

} // namespace ajazz::plugins

#endif // _WIN32
