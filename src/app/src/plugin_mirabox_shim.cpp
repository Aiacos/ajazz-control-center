// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_mirabox_shim.cpp
 * @brief Implementation of makeMiraboxShim() — `QWebEngineScript` builder (PLUGIN-11).
 *
 * `kMiraboxShimSource` is defined in the header as `inline constexpr`; no additional
 * definition is needed here.
 *
 * `makeMiraboxShim()` is compiled only when `AJAZZ_HAVE_WEBENGINE` is defined (the same
 * CMake gate used by `property_inspector_controller.cpp`).  The `kMiraboxShimSource`
 * constant remains reachable in non-WebEngine builds from the inline declaration in
 * `plugin_mirabox_shim.hpp`.
 *
 * ### Why DocumentCreation + not runJavaScript-after-load?
 *
 * Source: akp_plugin_sdk.md §9 (compatibility-shim row) + 18-RESEARCH.md Pitfall 4.
 *
 * Mirabox/AJAZZ HTML plugins call `window.connectMiraBoxSDSocket(...)` at script
 * execution time — before any dynamic `DOMContentLoaded` / `window.onload` event fires.
 * A `QWebEnginePage::runJavaScript` call issued after the page loads would race that
 * call: the plugin's own `<script>` may execute before the injected alias is defined,
 * producing an uncatchable `ReferenceError`.
 *
 * `QWebEngineScript::DocumentCreation` injects before any in-page `<script>` runs
 * (https://doc.qt.io/qt-6/qwebenginescript.html#injectionPoint), so
 * `window.connectMiraBoxSDSocket` is guaranteed to exist when the plugin's entry point
 * runs.  This is the only correct injection point for an alias the plugin script depends
 * on.
 *
 * COD-031 boundary: no `nlohmann::json` in this file or its public header.
 *
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-03 (PLUGIN-11)
 */

#if defined(AJAZZ_HAVE_WEBENGINE)
#include "plugin_mirabox_shim.hpp"

#include <QWebEngineScript>
// QStringLiteral and QString::fromUtf8 come from <QString>, already included via the header
// transitively; spell it out for clarity.
#include <QString>

namespace ajazz::app {

QWebEngineScript makeMiraboxShim() {
    QWebEngineScript script;

    // Name is searchable via QWebEngineScriptCollection::findScript("ajazz-mirabox-shim").
    script.setName(QStringLiteral("ajazz-mirabox-shim"));

    // DocumentCreation: the script is injected before any in-page <script> runs, so the
    // connectMiraBoxSDSocket alias exists when the plugin's own entry-point script executes.
    // This is the only correct injection point (Pitfall 4; akp_plugin_sdk.md §9).
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);

    // MainWorld: the plugin's connectElgatoStreamDeckSocket call lives in MainWorld; the alias
    // must be in the same world to resolve (T-18-SHIM-WORLD, accept disposition: pure forwarder,
    // no privileged capability).
    script.setWorldId(QWebEngineScript::MainWorld);

    // Sub-frames (e.g. an <iframe> inside the plugin page) may also call
    // connectMiraBoxSDSocket; inject into those as well.
    script.setRunsOnSubFrames(true);

    // kMiraboxShimSource is the canonical forwarding wrapper from plugin_mirabox_shim.hpp.
    // Converting via fromUtf8 matches how the test verifies it (pure string assertions).
    script.setSourceCode(QString::fromUtf8(kMiraboxShimSource));

    return script;
}

} // namespace ajazz::app
#endif // defined(AJAZZ_HAVE_WEBENGINE)
