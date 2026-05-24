// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_verify_gate.cpp
 * @brief Implementation of the PLUGIN-14 Ed25519 verify gate.
 *
 * COD-031 boundary: this TU includes only
 *   - the local header (plugin_verify_gate.hpp) which re-exports the
 *     manifest_signer.hpp types
 *   - Qt headers for QString path conversion
 * No nlohmann::json, no ajazz_core public headers used for JSON.
 */
#include "plugin_verify_gate.hpp"

#include "ajazz/core/logger.hpp"

#include <QString>

namespace ajazz::app {

ajazz::plugins::ManifestSignerConfig makeSignerConfig() {
    ajazz::plugins::ManifestSignerConfig cfg;
    // pythonExecutable defaults to "python3" in the struct — keep that default.
    // AJAZZ_PLUGIN_VERIFIER_SCRIPT and AJAZZ_PLUGIN_TRUST_ROOTS are injected
    // by CMake (unconditionally from Phase 22, un-gated from AJAZZ_BUILD_PYTHON_HOST).
    // On a minimal build where those defs are absent, both paths stay empty
    // and verifyManifest fails closed (D-05 fail-closed contract).
#if defined(AJAZZ_PLUGIN_VERIFIER_SCRIPT)
    cfg.verifierScript = AJAZZ_PLUGIN_VERIFIER_SCRIPT;
#endif
#if defined(AJAZZ_PLUGIN_TRUST_ROOTS)
    cfg.trustedPublishersFile = AJAZZ_PLUGIN_TRUST_ROOTS;
#endif
    return cfg;
}

VerifyOutcome verifyStagedPlugin(QString const& stagedManifestJsonPath,
                                 ajazz::plugins::ManifestSignerConfig configOverride) {
    // Use the caller's override when it supplies a non-empty verifierScript
    // (test-seam injection); otherwise build the config from compile-time defs.
    ajazz::plugins::ManifestSignerConfig const cfg =
        configOverride.verifierScript.empty() ? makeSignerConfig() : std::move(configOverride);

    // Fail-closed: if the verifier script path is empty (build without
    // AJAZZ_PLUGIN_VERIFIER_SCRIPT, or a test injecting an empty path),
    // return Refused immediately — never silently trust (D-05 contract).
    if (cfg.verifierScript.empty()) {
        AJAZZ_LOG_WARN("plugin-verify-gate",
                       "verifyStagedPlugin: verifier script not available in this build"
                       " (AJAZZ_PLUGIN_VERIFIER_SCRIPT not defined) -- refusing");
        return VerifyOutcome{VerifyVerdict::Refused,
                             {},
                             QStringLiteral("signature verification unavailable in this build")};
    }

    auto const result = ajazz::plugins::verifyManifest(stagedManifestJsonPath.toStdString(), cfg);

    if (!result.valid) {
        AJAZZ_LOG_WARN("plugin-verify-gate",
                       "verifyStagedPlugin: '{}' -> refused (invalid/unsigned/tampered)",
                       stagedManifestJsonPath.toStdString());
        return VerifyOutcome{
            VerifyVerdict::Refused, {}, QStringLiteral("signature verification failed")};
    }

    if (result.publisherName.empty()) {
        AJAZZ_LOG_INFO("plugin-verify-gate",
                       "verifyStagedPlugin: '{}' -> self-signed (key not in trust roots)",
                       stagedManifestJsonPath.toStdString());
        return VerifyOutcome{VerifyVerdict::SelfSigned,
                             {},
                             QStringLiteral("self-signed (key not in trusted publishers)")};
    }

    AJAZZ_LOG_INFO("plugin-verify-gate",
                   "verifyStagedPlugin: '{}' -> trusted publisher '{}'",
                   stagedManifestJsonPath.toStdString(),
                   result.publisherName);
    return VerifyOutcome{
        VerifyVerdict::Trusted,
        QString::fromStdString(result.publisherName),
        QStringLiteral("trusted publisher: %1").arg(QString::fromStdString(result.publisherName))};
}

QString verdictToTrustLevel(VerifyVerdict verdict) {
    switch (verdict) {
    case VerifyVerdict::Trusted:
        return QStringLiteral("trusted");
    case VerifyVerdict::SelfSigned:
        return QStringLiteral("self-signed");
    case VerifyVerdict::Refused:
        return QStringLiteral("unsigned");
    }
    return QStringLiteral("unsigned"); // unreachable; silence -Wreturn-type
}

} // namespace ajazz::app
