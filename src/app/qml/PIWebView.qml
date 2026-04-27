// SPDX-License-Identifier: GPL-3.0-or-later
//
// PIWebView.qml
// =============
//
// Renders a plugin-authored HTML Property Inspector page inside an
// isolated Qt WebEngine view. Only compiled into the QML module when
// CMake's `find_package(Qt6 ... WebEngineQuick WebChannelQuick)`
// succeeds (gate `AJAZZ_HAVE_WEBENGINE` in `src/app/CMakeLists.txt`);
// the bindings below import `QtWebEngine` and `QtWebChannel`, which
// are unavailable on minimal Qt installs and headless CI.
//
// Lifecycle: the view binds three singleton properties that move
// together on every `loadInspector()` call:
//
//   * `profile` — `QQuickWebEngineProfile`. Per-plugin, isolates
//     cookies / cache / storage. Reused across PI loads of the same
//     plugin so we don't churn the Chromium profile cache.
//   * `webChannel` — `QQmlWebChannel` with the `$SD` PI bridge already
//     registered. Re-created on every load so the next inspector sees
//     a fresh JS handler set.
//   * `url` — `file://` URL of the PI HTML entry. Empty when no
//     inspector is active.
//
// The earlier revision tried to set `WebEngineView.page` to a
// controller-owned `QWebEnginePage*`; `WebEngineView` does not actually
// expose a `page` property to QML (verified against
// `qquickwebengineview_p.h` in Qt 6.10), so that binding was inert at
// runtime and tripped a qmllint `[missing-property]` /
// `[unresolved-type]` pair. Switching to the QML-native trio
// (`profile` / `webChannel` / `url`) puts the rendering surface back in
// charge of its own page while keeping per-plugin isolation, the
// `$SD` bridge, and the URL interceptor (set on the profile in C++).
//
// Security posture (M2 baseline):
//   * Background colour matches the inspector pane so the WebEngine
//     surface doesn't flash white before the page paints.
//   * `WebEngineSettings` flags are configured declaratively below —
//     no remote-URL access from local content, no clipboard, no popups,
//     no plugins, no fullscreen, no screen capture, no insecure mixed
//     content. Identical to the C++-side baseline of the previous
//     revision; the per-profile `PIUrlRequestInterceptor` (set in
//     `PropertyInspectorController::loadInspector`) further bounds
//     network access to the PI directory + the CDN allowlist.
//   * No drag-and-drop into the view (`acceptedButtons: Qt.NoButton`)
//     prevents a plugin from receiving file drops the user intended for
//     the host UI.

pragma ComponentBehavior: Bound
import QtQuick 6
import QtWebEngine
import AjazzControlCenter

Item {
    id: root

    WebEngineView {
        id: webView
        anchors.fill: parent
        backgroundColor: Theme.bgSidebar
        // All three are nullptr / empty when no PI is active — the view
        // simply renders its background colour in that case.
        profile: PropertyInspectorController.activeProfile
        webChannel: PropertyInspectorController.activeChannel
        url: PropertyInspectorController.activeUrl

        settings.localContentCanAccessRemoteUrls: false
        settings.localContentCanAccessFileUrls: false
        settings.javascriptCanOpenWindows: false
        settings.javascriptCanAccessClipboard: false
        settings.allowRunningInsecureContent: false
        settings.pluginsEnabled: false
        settings.fullScreenSupportEnabled: false
        settings.screenCaptureEnabled: false
    }

    // Block all mouse-button input to the underlying page surface
    // (drag-and-drop in particular). The page is keyboard-accessible
    // via Tab from the surrounding QML focus chain.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
    }
}
