// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceImage.qml — product illustration for the selected device.
//
// Renders a real product photo (fetched from the vendor CDN) for the device
// currently being edited, sized to fit a fixed box next to the editor's
// "Editing:" title. When no per-model photo is curated for the codename, or
// when the remote image fails to load (offline, 404, CDN hiccup), the box
// gracefully falls back to the bundled per-family device-type SVG — the same
// clean-room icon set the sidebar DeviceRow uses. A broken-image placeholder
// is never shown.
//
// Resolution strategy (kept deliberately simple + testable):
//   1. Look the `codename` up in `_remoteByCodename` — a curated map of stable
//      Shopify-CDN product-image URLs (ajazzstore.com / ajazzbrand.com /
//      mirabox.net / whatgeek.com / attackshark.com storefronts). These hosts
//      serve assets from `cdn.shopify.com`, whose object URLs are content-
//      addressed + versioned (`?v=...`) and therefore stable.
//   2. If the remote image errors out (Image.Error) we swap to the family SVG.
//   3. If the codename has no curated URL we use the family SVG immediately.
//
// Properties:
//   * `codename` — device codename (e.g. "akp05e"). Drives the curated lookup.
//   * `family`   — core DeviceFamily int (0=Unknown, 1=StreamDeck, 2=Keyboard,
//                  3=Mouse — see src/core/include/ajazz/core/device.hpp). Drives
//                  the per-family SVG fallback. Same mapping DeviceRow uses.
import QtQuick

Item {
    id: root

    property string codename: ""
    property int    family: 0

    // Fixed display box: ~80px tall, a touch wider so landscape product shots
    // fit without letterboxing too aggressively. PreserveAspectFit keeps the
    // aspect ratio inside this box regardless of the source dimensions.
    implicitWidth: 104
    implicitHeight: 80

    // ---- Curated per-codename product-image URLs ---------------------------
    // Keyed by the codename string the device registry uses. Values are stable
    // vendor-CDN URLs verified live (2026-05-22). Codenames absent from this
    // map fall through to the per-family SVG. Several registry codenames are
    // variants of the same physical product (e.g. akp03/akp03e/akp03r,
    // aj159_apex_wired/aj159_apex_24g), so `_resolveRemote()` also normalises a
    // few common prefixes onto the canonical product photo.
    readonly property var _remoteByCodename: ({
        "akp05e":     "https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AKP05E_PRO_1.png?v=1762225214",
        "akp05":      "https://cdn.shopify.com/s/files/1/0653/5875/8141/files/Ajazz_AKP05_All-in-one_Stream_Dock_13.jpg?v=1739773034",
        "mirabox_n4": "https://cdn.shopify.com/s/files/1/0369/7721/3576/files/027F7636-8762-4e59-AF8F-7B1BBF2955E7.png?v=1727164294",
        "akp03":      "https://cdn.shopify.com/s/files/1/0704/7210/6227/files/5_818e19b9-5b03-49fa-9189-45841b3c4f52.jpg?v=1765261192",
        "akp153":     "https://cdn.shopify.com/s/files/1/0823/5050/6282/files/1_62edf2a1-9020-40e3-b408-8e582a5fe83e.jpg?v=1712582671",
        "akp815":     "https://cdn.shopify.com/s/files/1/0554/6678/6869/products/6_2506e404-08d2-428c-9b75-a35279a2fdad.jpg?v=1707205672",
        "ak980pro":   "https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AK980_V2_Grey_01.png?v=1763567614",
        "ak820pro":   "https://cdn.shopify.com/s/files/1/0704/7210/6227/files/1_056a85ad-5245-4d41-ab48-f1771d08ac7c.jpg?v=1759975933",
        "aj159pro":   "https://cdn.shopify.com/s/files/1/0704/7210/6227/files/1_537057af-6b0b-467c-8768-93952f4bc0f8.jpg?v=1766200697",
        "aj159":      "https://cdn.shopify.com/s/files/1/0704/7210/6227/files/1_537057af-6b0b-467c-8768-93952f4bc0f8.jpg?v=1766200697",
        "aj179":      "https://cdn.shopify.com/s/files/1/0704/7210/6227/files/05_0760f116-3feb-4e38-b159-5ce061c7020f.jpg?v=1766735633",
        "aj199":      "https://cdn.shopify.com/s/files/1/0554/6678/6869/files/AJAZZ_AJ199_Carbon_Fiber_Mouse_Black_1.png?v=1773501722"
    })

    // Resolve the curated remote URL for the current codename, or "" if none.
    // Exact match first, then a small set of prefix normalisations so the many
    // registry variants of a single product reuse the same canonical photo.
    function _resolveRemote() {
        if (root.codename === "")
            return "";
        if (root._remoteByCodename[root.codename] !== undefined)
            return root._remoteByCodename[root.codename];
        // Prefix normalisation: variant codenames → canonical product photo.
        var c = root.codename;
        if (c.indexOf("aj159") === 0) return root._remoteByCodename["aj159"];
        if (c.indexOf("aj179") === 0) return root._remoteByCodename["aj179"];
        if (c.indexOf("aj199") === 0) return root._remoteByCodename["aj199"];
        if (c.indexOf("akp03") === 0) return root._remoteByCodename["akp03"];
        if (c.indexOf("akp153") === 0) return root._remoteByCodename["akp153"];
        if (c.indexOf("mirabox_n4") === 0) return root._remoteByCodename["mirabox_n4"];
        return "";
    }

    // Per-family device-type SVG fallback. Same mapping + qrc aliasing scheme
    // as DeviceRow.qml: 2→keyboard, 3→mouse, 1/0/else→streamdock.
    function _familySvg() {
        var base = "qrc:/qt/qml/AjazzControlCenter/icons/devices/";
        if (root.family === 2) return base + "device-keyboard.svg";
        if (root.family === 3) return base + "device-mouse.svg";
        return base + "device-streamdock.svg";
    }

    readonly property string _remoteUrl: _resolveRemote()

    // Tracks whether the remote image failed so we can swap to the SVG. Reset
    // whenever the device changes (a new selection should re-attempt its
    // remote photo even if the previous device's photo had errored).
    property bool _remoteFailed: false
    onCodenameChanged: root._remoteFailed = false

    // The product photo. Hidden (and not laid out) while loading so we never
    // flash a broken-image box; the SVG underlay shows through until the photo
    // arrives. On error we mark `_remoteFailed` and let the SVG stay visible.
    Image {
        id: productImage
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        // Cap the decode size to the box's device-independent footprint so we
        // don't decode a multi-MB CDN PNG at full resolution.
        sourceSize.width: 208
        sourceSize.height: 160
        cache: true
        asynchronous: true
        smooth: true
        mipmap: true
        source: (root._remoteUrl !== "" && !root._remoteFailed) ? root._remoteUrl : ""
        // Only paint once fully ready; while Loading we show nothing extra
        // (the SVG fallback underneath remains visible).
        visible: status === Image.Ready && source.toString() !== ""
        onStatusChanged: {
            if (status === Image.Error)
                root._remoteFailed = true;
        }
        Accessible.ignored: true
    }

    // Family SVG fallback. Always mounted underneath; visible whenever the
    // product photo is not currently painted (no curated URL, still loading,
    // or load failed). PreserveAspectFit + sourceSize keep it crisp.
    Image {
        id: fallbackIcon
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        sourceSize.width: 96
        sourceSize.height: 96
        smooth: true
        mipmap: true
        source: root._familySvg()
        visible: !productImage.visible
        Accessible.ignored: true
    }
}
