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

// Phase 7 / ARCH-01 / D-01: shared nlohmann-based loadTrustRoots impl. This
// TU `#include`s the common header to signal it routes through the single
// shared definition in `manifest_signer_common.cpp` — the mini-grep body
// that used to live here was deleted in the same atomic commit.
#include "manifest_signer_common.hpp"
#include "win32_python_resolve.hpp"
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

// loadTrustRoots is now defined exactly once in `manifest_signer_common.cpp`
// (Phase 7 / ARCH-01 / D-01) — drift between this TU and `manifest_signer.cpp`
// is structurally impossible by construction. The legacy mini-grep cursor walk that
// used to live here (duplicated from the POSIX backend) was deleted in the same
// atomic commit per ARCH-01 SC1.

ManifestVerifyResult verifyManifest(std::filesystem::path const& manifestPath,
                                    ManifestSignerConfig const& config) {
    ManifestVerifyResult result;

    if (config.verifierScript.empty() || !std::filesystem::exists(config.verifierScript)) {
        return result;
    }
    if (!std::filesystem::exists(manifestPath)) {
        return result;
    }

    // Resolve "python3" to a real interpreter, skipping the Microsoft Store
    // App Execution Alias stub (see win32_python_resolve.hpp) so verification
    // works on a default python.org install (python.exe, no python3.exe).
    std::vector<std::string> const argv = {
        win32::resolveRealPython(config.pythonExecutable),
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
