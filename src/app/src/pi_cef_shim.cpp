// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pi_cef_shim.cpp
 * @brief Implementation of makeCefQueryShim() — `QWebEngineScript` builder (PLUGIN-09 / 20-02).
 *
 * `kCefQueryShimSource` is defined in the header as `inline constexpr std::string_view`; no
 * additional definition is needed here.
 *
 * `makeCefQueryShim()` is compiled only when `AJAZZ_HAVE_WEBENGINE` is defined (the same
 * CMake gate used by `property_inspector_controller.cpp` and `plugin_mirabox_shim.cpp`).
 * The `kCefQueryShimSource` constant remains reachable in non-WebEngine builds from the
 * inline declaration in `pi_cef_shim.hpp`.
 *
 * ### Why DocumentCreation + not runJavaScript-after-load? (18-03 Pitfall 4)
 *
 * PI HTML calls `window.cefQuery(...)` in its entry-point `<script>`. A
 * `QWebEnginePage::runJavaScript` call issued after the page loads would race that call:
 * the PI's own `<script>` may execute before the polyfill is defined, producing an
 * uncatchable `ReferenceError` (akp_plugin_sdk.md §8; 18-RESEARCH.md Pitfall 4).
 * `QWebEngineScript::DocumentCreation` injects before any in-page `<script>` runs.
 *
 * COD-031 boundary: no `nlohmann::json` in this file or its public header.
 *
 * Phase: 20-property-inspector-settings / Plan 20-02 (PLUGIN-09)
 */

#if defined(AJAZZ_HAVE_WEBENGINE)
#include "pi_cef_shim.hpp"

#include <QString>
#include <QWebEngineScript>

namespace ajazz::app {

QWebEngineScript makeCefQueryShim() {
    QWebEngineScript script;

    // Name is searchable via QWebEngineScriptCollection::findScript("ajazz-cefquery-shim").
    script.setName(QStringLiteral("ajazz-cefquery-shim"));

    // DocumentCreation: the script is injected before any in-page <script> runs, so
    // window.cefQuery exists when the PI's own entry-point script calls it.
    // Pitfall 4 (18-RESEARCH.md): runJavaScript-after-load races the PI script. Do not change.
    // Source: akp_plugin_sdk.md §8 + 18-03-PLAN.md <interfaces>.
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);

    // MainWorld: the PI's own JS code lives in MainWorld; the polyfill must be in the same
    // world to resolve `window.cefQuery` (T-20-SHIM-RACE: same-world injection required).
    script.setWorldId(QWebEngineScript::MainWorld);

    // Sub-frames (e.g. an <iframe> inside the PI page) may also call cefQuery; inject
    // into those as well (mirrors makeMiraboxShim() runsOnSubFrames disposition).
    script.setRunsOnSubFrames(true);

    // kCefQueryShimSource is the canonical polyfill JS from pi_cef_shim.hpp.
    // Converting via fromUtf8 + data()/size() matches how the test verifies it.
    script.setSourceCode(
        QString::fromUtf8(kCefQueryShimSource.data(), int(kCefQueryShimSource.size())));

    return script;
}

} // namespace ajazz::app
#endif // defined(AJAZZ_HAVE_WEBENGINE)
