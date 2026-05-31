// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_verify_gate.hpp
 * @brief Ed25519 verify gate for staged `.sdPlugin` installations (PLUGIN-14).
 *
 * Wraps @ref ajazz::plugins::verifyManifest with the compile-time paths
 * baked in by CMake, and collapses the three-way result into a
 * @ref VerifyVerdict the install path can switch on.
 *
 * The gate is **fail-closed**: when the verifier script path is not
 * compiled in (i.e. the @c AJAZZ_PLUGIN_VERIFIER_SCRIPT define is absent)
 * @ref verifyStagedPlugin returns @c Refused without calling Python. This
 * preserves the D-05 honesty contract even on reduced builds
 * (@c AJAZZ_BUILD_PYTHON_HOST=OFF).
 *
 * COD-031: this header includes @c <ajazz/plugins/manifest_signer.hpp>
 * which is PRIVATE-linked via @c ajazz::plugins. No @c nlohmann header
 * appears here or in the paired .cpp.
 */
#pragma once

#include <QString>

#include <ajazz/plugins/manifest_signer.hpp>

namespace ajazz::app {

/// Four-way verdict for a staged plugin archive.
///
/// Maps to the same strings as @c LoadedPluginsModel::trustLevelOf so the
/// UI vocabulary is consistent:
///   - @c Trusted    → @c "trusted"
///   - @c SelfSigned → @c "self-signed"
///   - @c Unsigned   → @c "unsigned"
///   - @c Refused    → @c "tampered"
///
/// CR-01 invariant: @c Refused (Ed25519-invalid / tampered) ALWAYS quarantines,
/// even when @c userConfirmedUnsigned==true. Consent from the user gates ONLY
/// the @c Unsigned (no signature block) branch, NEVER the @c Refused branch.
/// A tampered package is an attack; an unsigned one is a developer sideload.
enum class VerifyVerdict {
    Trusted,    ///< Ed25519 signature valid; key in trusted_publishers.json.
    SelfSigned, ///< Ed25519 signature valid; key NOT in trusted_publishers.json.
    Unsigned,   ///< No signature block present — developer sideload, consent-installable.
    Refused,    ///< Signature block present but Ed25519-invalid (tampered) — always quarantine.
};

/// Full outcome from @ref verifyStagedPlugin.
struct VerifyOutcome {
    VerifyVerdict verdict{VerifyVerdict::Refused};
    QString publisherName; ///< Non-empty only when verdict == Trusted.
    QString reason;        ///< Human-readable reason string for logging / UI.
};

/// Build a @ref ajazz::plugins::ManifestSignerConfig from the
/// @c AJAZZ_PLUGIN_VERIFIER_SCRIPT / @c AJAZZ_PLUGIN_TRUST_ROOTS compile
/// defs. When those defs are absent (the fail-closed build), both paths are
/// left empty so @ref ajazz::plugins::verifyManifest fails closed.
///
/// Do NOT re-derive the paths at runtime — the CMake defs are the source of
/// truth (RESEARCH anti-pattern guard).
[[nodiscard]] ajazz::plugins::ManifestSignerConfig makeSignerConfig();

/// Verify the manifest at @p stagedManifestJsonPath using the Ed25519 gate.
///
/// The @p configOverride parameter exists for testing: pass an empty-path
/// config (verifierScript == "{}") to exercise the fail-closed branch
/// without touching the real filesystem. Callers in production always use
/// the default, which calls @ref makeSignerConfig().
///
/// Behaviour:
///   1. If @c AJAZZ_PLUGIN_VERIFIER_SCRIPT is not compiled in (or
///      @p configOverride has an empty @c verifierScript) → classify by
///      @c signatureState returned from @ref ajazz::plugins::verifyManifest
///      (None → @c Unsigned; Invalid → @c Refused).
///   2. Call @ref ajazz::plugins::verifyManifest with the config.
///   3. @c signatureState==None → @c Unsigned ("manifest is unsigned").
///   4. @c signatureState==Invalid → @c Refused ("signature verification failed — tampered").
///   5. @c valid==true && publisherName empty → @c SelfSigned.
///   6. @c valid==true && publisherName set → @c Trusted.
[[nodiscard]] VerifyOutcome
verifyStagedPlugin(QString const& stagedManifestJsonPath,
                   ajazz::plugins::ManifestSignerConfig configOverride = {});

/// Map a @ref VerifyVerdict to the @c trustLevelOf vocabulary used by
/// @ref LoadedPluginsModel and the QML trust-chip delegate.
///
/// Returns one of @c "trusted", @c "self-signed", @c "unsigned".
[[nodiscard]] QString verdictToTrustLevel(VerifyVerdict verdict);

} // namespace ajazz::app
