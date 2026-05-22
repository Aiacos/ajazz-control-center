// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceImage.qml — product illustration for the selected device.
//
// Renders a real product photo for the device currently being edited inside a
// small rounded "thumbnail card". The card (a light, rounded, bordered tile) is
// deliberate: vendor product shots are studio photos on a white background, and
// white/light products (white keyboard, white mouse) cannot be colour-keyed to
// transparency without punching holes in the product. Putting every photo on a
// white card makes that white background read as an intentional product
// thumbnail on any app theme — and dark-device cutouts sit cleanly on it too.
//
// Photos are bundled in the repo (resources/devices/products/, downsized) and
// loaded via qrc — no runtime network dependency. When no per-model photo is
// curated for the codename, the card shows the bundled per-family device-type
// SVG instead.
//
// Adding a device (keep in sync — see docs/guides/ADDING_A_DEVICE.md):
//   1. Drop the downsized product image in resources/devices/products/ as
//      product-<codename>.{png,jpg} (CMake auto-globs it into the qrc).
//   2. Add a `"<codename>": _img("product-<codename>.<ext>")` line below, or a
//      prefix rule in _resolveProduct() for a variant of an existing product.
//
// Properties:
//   * `codename` — device codename (e.g. "akp05e"). Drives the curated lookup.
//   * `family`   — core DeviceFamily int (0=Unknown, 1=StreamDeck, 2=Keyboard,
//                  3=Mouse). Drives the per-family SVG fallback.
import QtQuick
import AjazzControlCenter

Item {
    id: root

    property string codename: ""
    property int    family: 0

    implicitWidth: 116
    implicitHeight: 84

    readonly property string _base: "qrc:/qt/qml/AjazzControlCenter/icons/devices/products/"
    function _img(name) { return root._base + name; }

    // ---- Curated per-codename product photos (bundled qrc) -----------------
    readonly property var _productByCodename: ({
        "akp05e":     _img("product-akp05e.png"),
        "akp05":      _img("product-akp05.png"),
        "mirabox_n4": _img("product-mirabox_n4.png"),
        "akp03":      _img("product-akp03.png"),
        "akp153":     _img("product-akp153.png"),
        "akp815":     _img("product-akp815.png"),
        "ak980pro":   _img("product-ak980pro.png"),
        "ak820pro":   _img("product-ak820pro.png"),
        "aj159":      _img("product-aj159.png"),
        "aj179":      _img("product-aj179.png"),
        "aj199":      _img("product-aj199.png")
    })

    // Resolve the bundled product photo for the current codename, or "" if none.
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

    // Per-family device-type SVG fallback. Same mapping + qrc scheme as DeviceRow.
    function _familySvg() {
        var base = "qrc:/qt/qml/AjazzControlCenter/icons/devices/";
        if (root.family === 2) return base + "device-keyboard.svg";
        if (root.family === 3) return base + "device-mouse.svg";
        return base + "device-streamdock.svg";
    }

    readonly property string _productUrl: _resolveProduct()
    readonly property bool   _hasProduct: _productUrl !== ""

    property bool _productFailed: false
    onCodenameChanged: root._productFailed = false

    // The thumbnail card. White so studio product shots (on white) blend in; a
    // hairline border + rounded corners make it read as an intentional product
    // tile rather than a stray white box on dark themes. The family SVG sits on
    // a transparent card instead (no white tile behind a flat icon).
    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radiusMd
        clip: true
        color: (root._hasProduct && !root._productFailed) ? "#ffffff" : "transparent"
        border.width: (root._hasProduct && !root._productFailed) ? 1 : 0
        border.color: Qt.rgba(0, 0, 0, 0.12)

        // Product photo, inset a little from the card edge.
        Image {
            id: productImage
            anchors.fill: parent
            anchors.margins: 5
            fillMode: Image.PreserveAspectFit
            sourceSize.width: 232
            sourceSize.height: 168
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

        // Per-family SVG fallback — visible when no product photo paints.
        Image {
            id: fallbackIcon
            anchors.fill: parent
            anchors.margins: 6
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
}
