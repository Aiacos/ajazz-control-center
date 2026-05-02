// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file manifest_signer.hpp
 * @brief Ed25519 plugin-manifest verifier (SEC-003 follow-up #51).
 *
 * Wraps @c scripts/sign-plugin-manifest.py so the host and the
 * publisher tooling share canonical-form rules byte-for-byte. The
 * cross-language byte-equality contract is pinned by
 * @c python/ajazz_plugins/tests/test_manifest_signing.py (Python
 * side) and @c tests/unit/test_manifest_signer.cpp (C++ side, this
 * module).
 *
 * Implementations live in two sibling translation units, selected
 * by the `WIN32` CMake variable: `manifest_signer.cpp` (POSIX,
 * `fork`+`execvp`) and `manifest_signer_win32.cpp` (Windows,
 * `_wspawnvp`). Both files share this public header so callers
 * never need an `#ifdef`.
 *
 * Why subprocess-call into Python instead of linking libsodium:
 *   1. Zero new C++ deps. The plugins library is intentionally
 *      Qt-free + zero-3rd-party — adding crypto pulls in 600 KB and
 *      a CMake gymnastics for portability across Linux/macOS/Win.
 *   2. Impossible drift. The signer and verifier are literally the
 *      same Python module, exec'd with `verify` instead of `sign`.
 *   3. Already-required runtime: the OOP plugin host depends on
 *      python3 anyway, so no extra moving part on user systems.
 *
 * Cost: ~50 ms per plugin at discovery time, paid once at startup.
 * For the realistic plugin counts (≤ 20 in any user's catalog), this
 * is well within the perceived-instant budget.
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ajazz::plugins {

/**
 * @brief One entry in the bundled trust roots
 *        (@c resources/trusted_publishers.json).
 */
struct TrustedPublisher {
    std::string keyB64; ///< Ed25519 public key, base64, exactly 44 chars.
    std::string name;   ///< Friendly name shown in the UI.
};

/**
 * @brief Result of verifying one plugin manifest.
 *
 * Matches the semantics surfaced by @ref PluginInfo:
 *   - `valid == false`              → unsigned or tampered manifest.
 *   - `valid == true && publisher empty` → self-signed (key not in trust roots).
 *   - `valid == true && publisher set`   → key matches a trust-roots entry.
 */
struct ManifestVerifyResult {
    bool valid{false};
    std::string publisherKeyB64; ///< From the manifest's `Ed25519PublicKey`.
    std::string publisherName;   ///< Looked up from trust roots, or empty.
};

/**
 * @brief Configuration for @ref verifyManifest.
 *
 * Both paths are required and must exist; otherwise verification
 * fails closed (the manifest is treated as unsigned).
 */
struct ManifestSignerConfig {
    /// Path to a Python 3 interpreter (`python3` on PATH if empty).
    std::string pythonExecutable{"python3"};
    /// Absolute path to `scripts/sign-plugin-manifest.py`.
    std::filesystem::path verifierScript;
    /// Absolute path to `resources/trusted_publishers.json`.
    std::filesystem::path trustedPublishersFile;
};

/**
 * @brief Verify a single plugin manifest's Ed25519 signature.
 *
 * Spawns `<pythonExecutable> <verifierScript> verify --manifest <manifestPath>`.
 * Exit code 0 → signature valid. Then the manifest's
 * `Ajazz.Signing.Ed25519PublicKey` is parsed and looked up in the
 * trust-roots JSON; the lookup is exact-string match on the base64
 * representation.
 *
 * Errors (verifier script missing, Python binary missing, malformed
 * JSON in the manifest or the trust roots) all collapse to
 * `valid=false` — fail-closed by design.
 */
ManifestVerifyResult verifyManifest(std::filesystem::path const& manifestPath,
                                    ManifestSignerConfig const& config);

/**
 * @brief Load and parse @c trusted_publishers.json.
 *
 * Exposed for testing; @ref verifyManifest calls it internally.
 * Returns an empty vector on parse error or missing file (the
 * caller treats every signed manifest as self-signed in that case).
 */
std::vector<TrustedPublisher> loadTrustRoots(std::filesystem::path const& jsonPath);

} // namespace ajazz::plugins
