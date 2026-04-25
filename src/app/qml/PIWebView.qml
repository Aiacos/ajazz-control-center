// SPDX-License-Identifier: GPL-3.0-or-later
//
// PIWebView.qml
// =============
//
// Renders a plugin-authored HTML Property Inspector page inside an
// isolated Qt WebEngine view. Only compiled into the QML module when
// CMake's `find_package(Qt6 ... WebEngineQuick)` succeeds (gate
// `AJAZZ_HAVE_WEBENGINE` in `src/app/CMakeLists.txt`); the bindings
// below import `QtWebEngine`, which is unavailable on minimal Qt
// installs and headless CI.
//
// Lifecycle: the view binds `page` to
// `propertyInspectorController.activePage`, a Q_PROPERTY exposing the
// `QWebEnginePage` the controller currently owns. The controller
// constructs a fresh page (and its scoped `QWebEngineProfile`) every
// `loadInspector(...)` call and clears it on `closeInspector()`; QML
// follows along reactively.
//
// Security posture (M2 baseline):
//   * Background colour matches the inspector pane so the WebEngine
//     surface doesn't flash white before the page paints.
//   * No drag-and-drop into the view (`acceptedButtons: Qt.NoButton`)
//     prevents a plugin from receiving file drops the user intended for
//     the host UI.
//   * Detailed `QWebEngineSettings` flags (no remote URL access from
//     local content, no clipboard, etc.) are set in C++ on the
//     `QWebEnginePage` before the URL load — see
//     `PropertyInspectorController::loadInspector` in M3.
//
// All bidirectional messaging happens via QWebChannel (configured on
// the page in C++, exposes `$SD` to the page's JS context). M3 wires
// up the JS bridge methods; this file just provides the rendering
// surface.

import QtQuick 6
import QtQuick.Controls 6
import QtWebEngine
import AjazzControlCenter

Item {
    id: root

    WebEngineView {
        id: webView
        anchors.fill: parent
        backgroundColor: Theme.bgSidebar
        // Bound from C++ — null when no PI is currently active.
        page: propertyInspectorController.activePage
    }

    // Block all mouse-button input to the underlying page surface
    // (drag-and-drop in particular). The page is keyboard-accessible
    // via Tab from the surrounding QML focus chain.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
    }
}
