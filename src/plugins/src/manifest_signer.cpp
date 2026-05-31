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

// Phase 7 / ARCH-01 / D-01: shared nlohmann-based loadTrustRoots impl. This
// TU `#include`s the common header to signal it routes through the single
// shared definition in `manifest_signer_common.cpp` — the mini-grep body
// that used to live here was deleted in the same atomic commit.
#include "manifest_signer_common.hpp"
#include "wire_protocol.hpp"

#include <array>
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

/// Resolve a program name to an absolute path against a vetted set of
/// system directories — NEVER via `$PATH` (CWE-426). This interpreter
/// runs the signature verifier that establishes plugin trust, so a
/// PATH-hijack (a writable dir prepended to `$PATH` by a `.desktop`
/// launcher, wrapper, or test harness) substituting a fake `python3`
/// that always exits 0 would forge a "valid signature" verdict. If the
/// caller supplied a path (contains `/`) we honour it after an X_OK
/// check; a bare name is resolved only from the vetted list. Returns an
/// empty string when nothing resolves, so the caller can fail closed.
std::string resolveTrustedExecutable(std::string const& nameOrPath) {
    if (nameOrPath.find('/') != std::string::npos) {
        return (::access(nameOrPath.c_str(), X_OK) == 0) ? nameOrPath : std::string{};
    }
    static constexpr std::array<std::string_view, 3> kVettedDirs{
        "/usr/bin",
        "/usr/local/bin",
        "/bin",
    };
    for (auto const& dir : kVettedDirs) {
        std::string candidate{dir};
        candidate += '/';
        candidate += nameOrPath;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }
    return {};
}

/// Spawn a child process, wait for it, return exit code or -1 on
/// fork/exec failure. Inherits stdin/stdout from the parent so the
/// verifier's `::error::` annotations show up in the host's logs.
/// argv[0] is an absolute path (resolved by @ref resolveTrustedExecutable),
/// so the `execvp` below performs no `$PATH` lookup.
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

// loadTrustRoots is now defined exactly once in `manifest_signer_common.cpp`
// (Phase 7 / ARCH-01 / D-01) — drift between this TU and `manifest_signer_win32.cpp`
// is structurally impossible by construction. The legacy mini-grep cursor walk that
// used to live here was deleted in the same atomic commit per ARCH-01 SC1.

/// Detect whether a manifest blob carries an Ed25519 signature block.
/// Returns true when BOTH Ed25519Signature AND Ed25519PublicKey fields are
/// present. Either field absent → no signature block (SignatureState::None).
bool hasSignatureBlock(std::string const& blob) {
    bool const hasSig = !wire::findStringField(blob, "Ed25519Signature").empty();
    bool const hasPub = !wire::findStringField(blob, "Ed25519PublicKey").empty();
    return hasSig && hasPub;
}

ManifestVerifyResult verifyManifest(std::filesystem::path const& manifestPath,
                                    ManifestSignerConfig const& config) {
    ManifestVerifyResult result;

    // Read the manifest blob first so we can classify signatureState on any
    // early-return path (fail-closed branches must still populate the field).
    auto const manifestBlob = readFile(manifestPath);
    bool const blockPresent = !manifestBlob.empty() && hasSignatureBlock(manifestBlob);

    if (config.verifierScript.empty() || !std::filesystem::exists(config.verifierScript)) {
        // Fail-closed: verifier script unavailable. Classify by presence of
        // signature block so the caller can distinguish None vs Invalid even
        // without running the verifier (CR-01 / T-27-FAILOPEN).
        result.signatureState = blockPresent ? SignatureState::Invalid : SignatureState::None;
        return result; // valid=false
    }
    if (!std::filesystem::exists(manifestPath)) {
        // Manifest missing: cannot read a blob, so cannot detect a signature block.
        result.signatureState = SignatureState::None;
        return result;
    }

    // If no signature block is present there is nothing to verify — return
    // SignatureState::None immediately. Spawning the verifier on an unsigned
    // manifest would exit 1 anyway and we already know the classification.
    if (!blockPresent) {
        result.signatureState = SignatureState::None;
        return result; // valid=false, signatureState=None
    }

    // Resolve the interpreter from a vetted absolute path, never via $PATH
    // (CWE-426). Fail closed if it cannot be resolved: a manifest we cannot
    // verify with a trusted interpreter is treated as unverified, not as
    // valid via a possibly-hijacked python.
    std::string const pythonExe = resolveTrustedExecutable(config.pythonExecutable);
    if (pythonExe.empty()) {
        // Interpreter unresolvable; signature block is present but unverifiable.
        result.signatureState = SignatureState::Invalid;
        return result; // valid=false
    }

    std::vector<std::string> const argv = {
        pythonExe,
        config.verifierScript.string(),
        "verify",
        "--manifest",
        manifestPath.string(),
    };
    int const rc = runChild(argv);
    if (rc != 0) {
        // Signature block present but verification failed → tampered.
        result.signatureState = SignatureState::Invalid;
        return result;
    }

    // Signature verified.
    result.publisherKeyB64 = extractPublicKey(manifestBlob);
    result.valid = true;
    result.signatureState = SignatureState::Valid;

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
