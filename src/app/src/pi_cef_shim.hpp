// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pi_cef_shim.hpp
 * @brief cefQuery polyfill for the Property Inspector (PLUGIN-09 / 20-02).
 *
 * Provides:
 *
 *   - `kCefQueryShimSource` — the JS polyfill source as a `constexpr
 *     std::string_view`. Always available, even without Qt WebEngine, so it is
 *     unit-testable as a pure string without any DOM or event-loop dependency.
 *
 *   - `makeCefQueryShim()` — builds and returns a `QWebEngineScript` configured
 *     for `DocumentCreation` injection in `MainWorld`. Only declared when
 *     `AJAZZ_HAVE_WEBENGINE` is defined; mirrors the Mirabox shim pattern from
 *     `plugin_mirabox_shim.hpp` (18-03).
 *
 * ### JS ↔ C++ mapping (akp_plugin_sdk.md §8)
 *
 * Original CEF call:
 * ```js
 * cefQuery({request: JSON.stringify({event:"getSettings"}), onSuccess: fn, onFailure: fn})
 * ```
 * Maps to (Qt 6 / QWebChannel equivalent):
 * ```js
 * channel.objects["$SD"].invoke(json).then(onSuccess).catch(onFailure)
 * ```
 *
 * The PI HTML calls `window.cefQuery(...)` via the polyfill defined here, which
 * routes to `PIBridge::invoke(QString json)` — the generic dispatcher that fans
 * out to the existing typed `$SD` slots.
 *
 * ### Why DocumentCreation?
 *
 * PI HTML calls `window.cefQuery(...)` in its entry-point script. Injecting via
 * `runJavaScript` after page load would race that call — the PI's own `<script>`
 * may fire before the polyfill is defined, producing an uncatchable `ReferenceError`
 * (akp_plugin_sdk.md §8; 18-RESEARCH.md Pitfall 4).
 * `QWebEngineScript::DocumentCreation` guarantees the polyfill exists before any
 * in-page `<script>` runs.
 *
 * ### Security (T-20-SHIM)
 *
 * The shim runs in MainWorld (the same JS world as the untrusted PI page). It is a
 * pure forwarder to `channel.objects["$SD"]`; `PIBridge::invoke` fans out ONLY to
 * the fixed typed-slot set — no default-to-arbitrary path, no privileged capability
 * beyond the already-sandboxed `$SD` surface.
 *
 * COD-031: no `nlohmann::json` in this file.
 *
 * Phase: 20-property-inspector-settings / Plan 20-02 (PLUGIN-09)
 */
#pragma once

#include <string_view>

// kCefQueryShimSource is intentionally NOT gated on AJAZZ_HAVE_WEBENGINE so it
// is reachable in non-WebEngine builds and can be unit-tested as a pure string.
namespace ajazz::app {

/**
 * @brief The JS polyfill source that maps `window.cefQuery(...)` to the
 *        QWebChannel `$SD` bridge via `PIBridge::invoke`.
 *
 * Shape (per akp_plugin_sdk.md §8):
 *
 * ```js
 * window.cefQuery = function(opts) {
 *   try {
 *     new QWebChannel(qt.webChannelTransport, function(channel) {
 *       var sd = channel.objects["$SD"];
 *       sd.invoke(opts.request).then(opts.onSuccess).catch(opts.onFailure);
 *     });
 *   } catch(e) { if (opts.onFailure) opts.onFailure(String(e)); }
 * };
 * ```
 *
 * Tests assert: `window.cefQuery`, `onSuccess`, `onFailure`, and `$SD`.
 *
 * @note Apple Clang `-Wunused-const-variable` triggers on `inline constexpr` at
 *       file scope when the variable is NOT `[[maybe_unused]]`. The attribute
 *       keeps the declaration warning-clean on macOS CI (-Werror).
 */
[[maybe_unused]] inline constexpr std::string_view kCefQueryShimSource =
    R"JS(
window.cefQuery = function(opts) {
    // akp_plugin_sdk.md §8: cefQuery({request, onSuccess, onFailure}) -> channel $SD bridge.
    // Routing: opts.request -> PIBridge::invoke(json) -> typed $SD slot.
    // Security (T-20-SHIM): pure forwarder; no privileged path beyond the sandboxed $SD surface.
    try {
        new QWebChannel(qt.webChannelTransport, function(channel) {
            var sd = channel.objects["$SD"];
            if (!sd) {
                if (opts.onFailure) opts.onFailure("$SD bridge not available");
                return;
            }
            sd.invoke(opts.request).then(
                function(result) { if (opts.onSuccess) opts.onSuccess(result); }
            ).catch(
                function(err) { if (opts.onFailure) opts.onFailure(String(err)); }
            );
        });
    } catch(e) {
        if (opts.onFailure) opts.onFailure(String(e));
    }
};
)JS";

} // namespace ajazz::app

#if defined(AJAZZ_HAVE_WEBENGINE)
// QWebEngineScript lives in Qt6::WebEngineCore. The include is gated so
// non-WebEngine builds do not drag in any WebEngine headers.
#include <QWebEngineScript>

namespace ajazz::app {

/**
 * @brief Build a `QWebEngineScript` that injects the cefQuery polyfill at
 *        `DocumentCreation` in `MainWorld`.
 *
 * Properties set (mirrors makeMiraboxShim() from plugin_mirabox_shim.hpp):
 *   - `name`            = `"ajazz-cefquery-shim"`
 *   - `injectionPoint`  = `QWebEngineScript::DocumentCreation`
 *   - `worldId`         = `QWebEngineScript::MainWorld`
 *   - `runsOnSubFrames` = `true`
 *   - `sourceCode`      = `QString::fromUtf8(kCefQueryShimSource)`
 *
 * Insertion site in PropertyInspectorController::loadInspector():
 * ```cpp
 * profile->scripts()->insert(makeCefQueryShim());
 * ```
 *
 * @return A ready-to-insert `QWebEngineScript`.
 */
[[nodiscard]] QWebEngineScript makeCefQueryShim();

} // namespace ajazz::app
#endif // defined(AJAZZ_HAVE_WEBENGINE)
