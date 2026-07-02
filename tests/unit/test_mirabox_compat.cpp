// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_mirabox_compat.cpp
 * @brief CompatTest::miraboxSocket_aliasedToElgato — Mirabox/Elgato compat shim tests.
 *
 * Two test cases:
 *
 *   1. "CompatTest miraboxSocket_aliasedToElgato" (always compiled, no WebEngine dep):
 *      Asserts that kMiraboxShimSource contains both connect function names and uses
 *      .apply(window, arguments) — proving the forwarding shape (Assumption A4).
 *
 *   2. "CompatTest mirabox shim injects at document creation" (AJAZZ_HAVE_WEBENGINE only):
 *      Builds the QWebEngineScript via makeMiraboxShim() and asserts the injection point,
 *      world ID, and sourceCode match the expected values.
 *
 * DOM round-trip (fixture index.html calling connectMiraBoxSDSocket → reaching the bridge)
 * is OUT OF SCOPE here — it depends on a live QQuickWebEngineProfile + the 18-04 hosting
 * path + a live WebChannel session. The full live witness is deferred to Phase 25 (VERIFY-06).
 *
 * Source: akp_plugin_sdk.md §9 (target test name: CompatTest::miraboxSocket_aliasedToElgato),
 *         18-RESEARCH.md Pattern 2 (Mirabox alias via document-creation UserScript),
 *         Pitfall 4 (alias injected too late), Assumption A4 (pure forwarding wrapper).
 *
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-03 (PLUGIN-11)
 */

#include "plugin_mirabox_shim.hpp"

#include <QString>

#include <catch2/catch_test_macros.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Case 1 — always compiled; pure string assertions on kMiraboxShimSource.
// No WebEngine dependency; no Qt event loop; runs on every build configuration.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("CompatTest miraboxSocket_aliasedToElgato", "[mirabox-compat]") {
    // kMiraboxShimSource is declared as constexpr/extern char const* in plugin_mirabox_shim.hpp.
    // Convert to QString for convenient contains() checks.
    QString const src = QString::fromUtf8(ajazz::app::kMiraboxShimSource);

    SECTION("source defines connectMiraBoxSDSocket") {
        REQUIRE(src.contains(QStringLiteral("connectMiraBoxSDSocket")));
    }

    SECTION("source references connectElgatoStreamDeckSocket") {
        REQUIRE(src.contains(QStringLiteral("connectElgatoStreamDeckSocket")));
    }

    SECTION("source uses .apply(window, arguments) forwarding wrapper") {
        // This is the key shape: a pure forwarding wrapper that passes all arguments
        // unchanged (Assumption A4 — same signature, no semantic change).
        REQUIRE(src.contains(QStringLiteral("apply(window, arguments)")));
    }

    SECTION("source is non-empty") {
        REQUIRE(!src.isEmpty());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Case 2 — WebEngine-gated; asserts the QWebEngineScript built by makeMiraboxShim().
// Only compiled when Qt6::WebEngineCore is present (AJAZZ_HAVE_WEBENGINE).
// Assertion: DocumentCreation + MainWorld + sourceCode == kMiraboxShimSource.
// ─────────────────────────────────────────────────────────────────────────────

#if defined(AJAZZ_HAVE_WEBENGINE)
#include <QWebEngineScript>

TEST_CASE("CompatTest mirabox shim injects at document creation", "[mirabox-compat]") {
    QWebEngineScript const script = ajazz::app::makeMiraboxShim();

    SECTION("injection point is DocumentCreation") {
        // DocumentCreation is the only injection point that guarantees the alias
        // exists BEFORE the plugin's own script runs (Pitfall 4). runJavaScript-after-load
        // would race the plugin script.
        REQUIRE(script.injectionPoint() == QWebEngineScript::DocumentCreation);
    }

    SECTION("world ID is MainWorld") {
        // MainWorld is required because the plugin's own connect call lives in MainWorld.
        // The shim is a pure forwarder with no privileged capability (A4 / T-18-SHIM-WORLD).
        REQUIRE(script.worldId() == QWebEngineScript::MainWorld);
    }

    SECTION("sourceCode matches kMiraboxShimSource") {
        REQUIRE(script.sourceCode() == QString::fromUtf8(ajazz::app::kMiraboxShimSource));
    }

    SECTION("runsOnSubFrames is true") {
        // Sub-frames may also call connectMiraBoxSDSocket; the alias must be present there too.
        REQUIRE(script.runsOnSubFrames());
    }

    SECTION("name is ajazz-mirabox-shim") {
        REQUIRE(script.name() == QStringLiteral("ajazz-mirabox-shim"));
    }
}
#endif // defined(AJAZZ_HAVE_WEBENGINE)
