// SPDX-License-Identifier: GPL-3.0-or-later
//
// WebUiHost — the `webui` UI-mode root: hosts the vendored OpenDeck Svelte SPA
// in a WebEngineView and bridges it to our C++ OpenDeckBridge over a
// QWebChannel (object name "opendeck", matching resources/opendeck-shim/
// tauri-shim.js). Selected at startup by main.cpp when AppUiMode == "webui"
// and Qt WebEngine is present. See docs/opendeck-ui/02-architecture.md.
//
// This file is only added to the QML module on the WebEngine build path
// (qt_target_qml_sources in CMakeLists, like PIWebView.qml), so the QtWebEngine
// / QtWebChannel imports always resolve here.
import QtQuick
import QtQuick.Controls
import QtWebEngine
import QtWebChannel

ApplicationWindow {
    id: root
    objectName: "WebUiHost"
    width: 1280
    height: 960
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("AJAZZ Control Center")
    color: "#262626" // OpenDeck neutral-800, avoids a white flash before the SPA paints

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
            console.warn("WebUiHost: OpenDeckBridgeObject missing — the SPA will have no backend.");
        }
    }

    WebEngineView {
        id: webUiView
        objectName: "webUiView"
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
                console.warn("WebUiHost: load failed:", req.errorString, req.url);
        }
    }

    // Clear placeholder when the SPA was not built into the binary.
    Label {
        objectName: "webUiPlaceholder"
        anchors.centerIn: parent
        visible: !root.hasBundle && root.devUrl === ""
        horizontalAlignment: Text.AlignHCenter
        color: "#d4d4d4"
        font.pixelSize: 16
        text: qsTr("OpenDeck web UI not built.\n\nConfigure with -DAJAZZ_BUILD_WEBUI=ON (requires Node.js),\nor set [ui] webui.devUrl in the config to a running dev server.\n\nThe native QML UI (ui.mode = qml) is the default.")
    }
}
