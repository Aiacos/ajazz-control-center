// SPDX-License-Identifier: GPL-3.0-or-later
//
// PropertyInspector.qml
// =====================
//
// Front door for the per-action settings panel. Two render paths:
//
//   * PIWebView (HTML, only available when AJAZZ_HAVE_WEBENGINE) renders a
//     plugin-authored HTML page in an isolated Qt WebEngine view, bridged
//     to C++ via QWebChannel for the Stream Deck SDK-2 `$SD` API. The
//     controller decides when this is active; QML follows.
//
//   * NativePropertyInspector renders the schema-driven QtQuick.Controls
//     form for every action whose settings are described by
//     docs/schemas/property_inspector.schema.json. Always available — this
//     is the fallback for builds without Qt WebEngine and the default for
//     non-plugin (built-in) actions.
//
// The wrapper here is a thin Loader-based switcher: it watches
// `propertyInspectorController.hasHtmlInspector` and `webEngineAvailable`
// and swaps the active component without leaking either implementation
// into the other's lifecycle. The schema/settings forwarders are kept on
// the wrapper so existing callers (Inspector.qml) keep working unchanged.

import QtQuick 6
import QtQuick.Layouts 6
import AjazzControlCenter

ColumnLayout {
    id: root

    // ---- API mirrored from NativePropertyInspector for backwards-compat ----

    /// Schema to render in the native fallback path. Ignored when the
    /// HTML PI is active.
    property var schema: ({ title: "", fields: [] })

    /// Live settings dictionary, bidirectional with the active renderer.
    property var settings: ({})

    /// Emitted whenever any field commits a new value, with the full
    /// settings dictionary serialised as JSON. The caller (Inspector.qml)
    /// persists this to `Action.settingsJson`. Named *settingsJsonChanged*
    /// (not just *settingsChanged*) because the `property var settings`
    /// declaration auto-generates a `settingsChanged()` notify signal —
    /// sharing the name causes a `[duplicated-name]` qmllint warning.
    signal settingsJsonChanged(string json)

    // ---- Renderer selection ---------------------------------------------

    spacing: 0

    Loader {
        id: rendererLoader
        Layout.fillWidth: true
        Layout.fillHeight: true

        // Pick HTML PI when (a) WebEngine is compiled in and (b) the
        // controller reports an active plugin PI page; otherwise the
        // schema-driven native renderer takes over. The check is cheap and
        // the bindings re-evaluate when the controller emits change signals.
        sourceComponent: {
            if (typeof propertyInspectorController !== "undefined"
                    && propertyInspectorController.webEngineAvailable
                    && propertyInspectorController.hasHtmlInspector) {
                return htmlComponent
            }
            return nativeComponent
        }
    }

    Component {
        id: nativeComponent
        NativePropertyInspector {
            schema: root.schema
            settings: root.settings
            onSettingsJsonChanged: function(json) { root.settingsJsonChanged(json) }
        }
    }

    // The HTML component is only ever instantiated when the controller
    // reports a live PI page. PIWebView.qml is only present in the QML
    // module when CMake found Qt6::WebEngineQuick at configure time; in
    // builds without WebEngine the Loader's `sourceComponent` selector
    // never lands on this branch, so the missing file is unreachable
    // and the QML compiler doesn't fault.
    Component {
        id: htmlComponent
        Loader {
            anchors.fill: parent
            // The string source spelling avoids a hard import that would
            // turn into a "module has no file PIWebView" compile error in
            // builds where Qt WebEngine is absent.
            source: "PIWebView.qml"
        }
    }
}
