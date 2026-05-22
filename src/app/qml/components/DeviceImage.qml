// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceImage.qml — product illustration for the selected device.
//
// Renders a real product photo for the device currently being edited, sized to
// fit a fixed box next to the editor header. The photos are **bundled in the
// repo** (resources/devices/products/, downsized vendor shots) and loaded via
// qrc — no runtime network dependency, works offline. When no per-model photo
// is curated for the codename, the box falls back to the bundled per-family
// device-type SVG (the same clean-room icon set the sidebar DeviceRow uses).
// A broken-image placeholder is never shown.
//
// Adding a device (keep this in sync — see docs/guides/ADDING_A_DEVICE.md):
//   1. Drop the downsized product image in resources/devices/products/ as
//      product-<codename>.{png,jpg} (CMake auto-globs it into the qrc).
//   2. Add a `"<codename>": _img("product-<codename>.<ext>")` line below.
//   3. If the new codename is a variant of an existing product, add a prefix
//      rule in _resolveProduct() instead of a new image.
//
// Properties:
//   * `codename` — device codename (e.g. "akp05e"). Drives the curated lookup.
//   * `family`   — core DeviceFamily int (0=Unknown, 1=StreamDeck, 2=Keyboard,
//                  3=Mouse). Drives the per-family SVG fallback (same mapping
//                  DeviceRow uses).
import QtQuick

Item {
    id: root

    property string codename: ""
    property int    family: 0

    // Fixed display box next to the header title. PreserveAspectFit keeps the
    // aspect ratio inside this box regardless of source dimensions.
    implicitWidth: 104
    implicitHeight: 80

    readonly property string _base: "qrc:/qt/qml/AjazzControlCenter/icons/devices/products/"
    function _img(name) { return root._base + name; }

    // ---- Curated per-codename product photos (bundled qrc) -----------------
    // Keyed by the registry codename. Files live in resources/devices/products/
    // and are aliased into the qrc by src/app/CMakeLists.txt. Codenames absent
    // here fall through to the per-family SVG via _resolveProduct().
    readonly property var _productByCodename: ({
        "akp05e":     _img("product-akp05e.png"),
        "akp05":      _img("product-akp05.jpg"),
        "mirabox_n4": _img("product-mirabox_n4.png"),
        "akp03":      _img("product-akp03.jpg"),
        "akp153":     _img("product-akp153.jpg"),
        "akp815":     _img("product-akp815.jpg"),
        "ak980pro":   _img("product-ak980pro.png"),
        "ak820pro":   _img("product-ak820pro.jpg"),
        "aj159":      _img("product-aj159.jpg"),
        "aj179":      _img("product-aj179.jpg"),
        "aj199":      _img("product-aj199.png")
    })

    // Resolve the bundled product photo for the current codename, or "" if none.
    // Exact match first, then prefix/alias normalisation so the many registry
    // variants of one physical product reuse the same canonical photo.
    function _resolveProduct() {
        if (root.codename === "")
            return "";
        if (root._productByCodename[root.codename] !== undefined)
            return root._productByCodename[root.codename];
        var c = root.codename;
        // The 0x3151:0x5007 "ajazz_24g_8k" SKU is an AJ159 APEX (2.4G 8K).
        if (c === "ajazz_24g_8k") return root._productByCodename["aj159"];
        if (c.indexOf("aj159") === 0) return root._productByCodename["aj159"];
        if (c.indexOf("aj179") === 0) return root._productByCodename["aj179"];
        if (c.indexOf("aj199") === 0) return root._productByCodename["aj199"];
        if (c.indexOf("akp03") === 0) return root._productByCodename["akp03"];
        if (c.indexOf("akp153") === 0) return root._productByCodename["akp153"];
        if (c.indexOf("mirabox_n4") === 0) return root._productByCodename["mirabox_n4"];
        return "";
    }

    // Per-family device-type SVG fallback. Same mapping + qrc aliasing scheme as
    // DeviceRow.qml: 2→keyboard, 3→mouse, 1/0/else→streamdock.
    function _familySvg() {
        var base = "qrc:/qt/qml/AjazzControlCenter/icons/devices/";
        if (root.family === 2) return base + "device-keyboard.svg";
        if (root.family === 3) return base + "device-mouse.svg";
        return base + "device-streamdock.svg";
    }

    readonly property string _productUrl: _resolveProduct()

    // Guards against a (rare) bundled-image decode failure: swap to the SVG.
    property bool _productFailed: false
    onCodenameChanged: root._productFailed = false

    // The product photo. Hidden while loading / on failure so we never flash a
    // broken-image box; the SVG underlay shows through until it paints.
    Image {
        id: productImage
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        sourceSize.width: 208
        sourceSize.height: 160
        smooth: true
        mipmap: true
        source: (root._productUrl !== "" && !root._productFailed) ? root._productUrl : ""
        visible: status === Image.Ready && source.toString() !== ""
        onStatusChanged: {
            if (status === Image.Error)
                root._productFailed = true;
        }
        Accessible.ignored: true
    }

    // Family SVG fallback — always mounted underneath; visible whenever the
    // product photo is not currently painted (no curated photo or decode error).
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
