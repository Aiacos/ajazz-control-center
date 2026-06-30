// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenDeckPane — the embedded OpenDeck Svelte SPA, mounted by ProfileEditor as
// the editor area when the active device is a STREAM CONTROLLER (streamdeck).
// Hosts the SPA in a WebEngineView bridged to our C++ OpenDeckBridge over a
// QWebChannel (object name "opendeck", matching resources/opendeck-shim/
// tauri-shim.js). Unlike WebUiHost (a top-level ApplicationWindow root) this is
// an embeddable Item that lives INSIDE the native Main.qml shell, alongside the
// mouse/keyboard editor panels. See docs/opendeck-ui/02-architecture.md.
//
// Added to the QML module only on the WebEngine build path (qt_target_qml_sources
// in CMakeLists, like PIWebView.qml / WebUiHost.qml), so the QtWebEngine /
// QtWebChannel imports always resolve here. ProfileEditor loads it via a string
// `source` so builds without WebEngine don't fault on a missing type.
import QtQuick
import QtWebEngine
import QtWebChannel

Item {
    id: root
    objectName: "openDeckPane"

    // Set by main.cpp: true when the SPA bundle was built into :/opendeck.
    readonly property bool hasBundle: (typeof AppHasWebUiBundle !== "undefined") && AppHasWebUiBundle
    // Optional dev override (config ui.conf [ui] webui.devUrl), e.g. a `vite dev` server.
    readonly property string devUrl: (typeof AppWebUiDevUrl !== "undefined") ? AppWebUiDevUrl : ""

    // QWebChannel exposing the C++ OpenDeckBridge (context property
    // OpenDeckBridgeObject, set in main.cpp) to the page as "opendeck".
    WebChannel {
        id: bridgeChannel
    }
    Component.onCompleted: {
        if (typeof OpenDeckBridgeObject !== "undefined" && OpenDeckBridgeObject) {
            bridgeChannel.registerObject("opendeck", OpenDeckBridgeObject);
        } else {
            console.warn("OpenDeckPane: OpenDeckBridgeObject missing — the SPA will have no backend.");
        }
    }

    WebEngineView {
        id: webView
        objectName: "openDeckWebView"
        anchors.fill: parent
        webChannel: bridgeChannel
        visible: root.hasBundle || root.devUrl !== ""
        // The bundled SPA is served via the custom `opendeck://app/` scheme
        // (registered in main.cpp) so SvelteKit routes from a clean origin and
        // Fetch works — loading qrc:/opendeck/index.html directly 404s the
        // SvelteKit router and blocks Fetch.
        url: root.devUrl !== ""
             ? root.devUrl
             : (root.hasBundle ? "opendeck://app/" : "about:blank")
        onLoadingChanged: function (req) {
            if (req.status === WebEngineView.LoadFailedStatus)
                console.warn("OpenDeckPane: load failed:", req.errorString, req.url);
        }
    }

    // Clear placeholder when the SPA was not built into the binary.
    Text {
        objectName: "openDeckPanePlaceholder"
        anchors.centerIn: parent
        visible: !root.hasBundle && root.devUrl === ""
        horizontalAlignment: Text.AlignHCenter
        color: "#d4d4d4"
        font.pixelSize: 16
        text: qsTr("OpenDeck web UI not built.\n\nConfigure with -DAJAZZ_BUILD_WEBUI=ON (requires Node.js)\nto edit Stream Dock devices.")
    }
}
