// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_mirabox_shim.hpp
 * @brief Mirabox/Elgato WebSocket connect-function alias (PLUGIN-11).
 *
 * Provides:
 *
 *   - `kMiraboxShimSource` — the forwarding JS source as a `constexpr` C-string.
 *     Always available, even without Qt WebEngine, so it is unit-testable as a
 *     pure string without any DOM or event-loop dependency.
 *
 *   - `makeMiraboxShim()` — builds and returns a `QWebEngineScript` configured for
 *     `DocumentCreation` injection in `MainWorld`. Only declared when
 *     `AJAZZ_HAVE_WEBENGINE` is defined; gated by the same CMake probe that guards
 *     `property_inspector_controller.hpp`.
 *
 * ### Why DocumentCreation?
 *
 * Mirabox/AJAZZ HTML plugins call `window.connectMiraBoxSDSocket(...)` early in
 * their entry-point script.  Injecting via `runJavaScript` after page load would
 * race that call — the plugin's own script may fire before the alias is defined,
 * producing an uncatchable `ReferenceError` (Pitfall 4 in 18-RESEARCH.md).
 * `QWebEngineScript::DocumentCreation` guarantees the alias exists before any
 * in-page `<script>` runs (akp_plugin_sdk.md §9 compatibility-shim row).
 *
 * ### Security note (T-18-SHIM-WORLD)
 *
 * The shim runs in `MainWorld` — the same JS world as the untrusted plugin page —
 * because `connectElgatoStreamDeckSocket` (the function the alias forwards to) also
 * lives in `MainWorld`.  The shim itself has no privileged capability: it is a pure
 * forwarding wrapper that passes all arguments unchanged (Assumption A4).
 *
 * COD-031: no `nlohmann::json`; no `#include <nlohmann/json.hpp>`.
 *
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-03 (PLUGIN-11)
 */
#pragma once

// kMiraboxShimSource is intentionally NOT gated on AJAZZ_HAVE_WEBENGINE so it
// is reachable in non-WebEngine builds and can be unit-tested as a pure string.
namespace ajazz::app {

/**
 * @brief The JS source that aliases `window.connectMiraBoxSDSocket` to
 *        `window.connectElgatoStreamDeckSocket` via a pure forwarding wrapper.
 *
 * Shape (per akp_plugin_sdk.md §9 + 18-RESEARCH.md Pattern 2 + Assumption A4):
 *
 * ```js
 * window.connectMiraBoxSDSocket = function() {
 *   return window.connectElgatoStreamDeckSocket.apply(window, arguments);
 * };
 * ```
 *
 * The alias is a pure forwarding wrapper — same signature, no semantic change.
 * Tests assert both function names and the `.apply(window, arguments)` forwarder
 * shape.  The Wave-4 PluginManager (18-04) injects this into every per-plugin
 * `QQuickWebEngineProfile` via `profile->scripts()->insert(makeMiraboxShim())`.
 *
 * @note Apple Clang `-Wunused-const-variable` triggers on `inline constexpr` at
 *       file scope when the variable is NOT `[[maybe_unused]]`.  Adding the
 *       attribute keeps the declaration warning-clean on macOS CI (-Werror).
 */
[[maybe_unused]] inline constexpr char const* kMiraboxShimSource =
    "window.connectMiraBoxSDSocket = function() {"
    " return window.connectElgatoStreamDeckSocket.apply(window, arguments);"
    "};";

} // namespace ajazz::app

#if defined(AJAZZ_HAVE_WEBENGINE)
// QWebEngineScript lives in Qt6::WebEngineCore.  The include is gated so
// non-WebEngine builds do not drag in any WebEngine headers.
#include <QWebEngineScript>

namespace ajazz::app {

/**
 * @brief Build a `QWebEngineScript` that injects the Mirabox alias at `DocumentCreation`.
 *
 * Properties set:
 *   - `name`            = `"ajazz-mirabox-shim"`
 *   - `injectionPoint`  = `QWebEngineScript::DocumentCreation`
 *   - `worldId`         = `QWebEngineScript::MainWorld`
 *   - `runsOnSubFrames` = `true`  (sub-frames may also call `connectMiraBoxSDSocket`)
 *   - `sourceCode`      = `QString::fromUtf8(kMiraboxShimSource)`
 *
 * The Wave-4 PluginManager (18-04) calls:
 * ```cpp
 * profile->scripts()->insert(makeMiraboxShim());
 * ```
 * before loading the plugin's `index.html`, so the alias is guaranteed present
 * before any plugin-owned `<script>` executes (akp_plugin_sdk.md §9; Pitfall 4).
 *
 * @return A ready-to-insert `QWebEngineScript`.
 */
[[nodiscard]] QWebEngineScript makeMiraboxShim();

} // namespace ajazz::app
#endif // defined(AJAZZ_HAVE_WEBENGINE)
