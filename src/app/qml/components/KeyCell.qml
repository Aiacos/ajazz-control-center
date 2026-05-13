// SPDX-License-Identifier: GPL-3.0-or-later
//
// KeyCell.qml — single AKP key tile used by KeyDesigner.
//
// Renders the visual approximation of a Stream Dock LCD key: a square tile
// showing the user-chosen icon image with an optional overlay label. When
// no icon is set, the tile falls back to the keyboard-style placeholder
// (the 1-based key index) so the grid never looks empty during initial use.
//
// Exposed:
//   * `index`       — 0-based key index (used by the index-placeholder fallback).
//   * `iconSource`  — url for the icon image (file:// or qrc:/). Empty = placeholder.
//   * `label`       — overlay label text. Empty = no overlay (icon-only or placeholder).
//   * `selected`    — when true, draws an accent outline (F-26 selected outline).
//
// Emits:
//   * `clicked`    — when the user activates the cell (mouse or keyboard).
//
// Accessibility: uses ItemDelegate so focus + key handling come for free, and
// exposes Accessible.role/name/description.
import QtQuick
import QtQuick.Controls
import AjazzControlCenter

ItemDelegate {
    id: root

    // Marked `required` at the type level so the Repeater delegate's
    // model-role auto-binding takes effect directly — re-declaring
    // these as required on the delegate instance was hitting the
    // type-default-shadow trap (cells always rendered index "1").
    required property int index
    required property url iconSource
    required property string label
    property bool selected: false

    width: 96
    height: 96

    background: Rectangle {
        radius: Theme.radiusLg
        color: root.hovered ? Theme.tileHover : Theme.tile
        border.width: root.activeFocus || root.selected ? Theme.focusRingWidth : 1
        border.color: root.activeFocus || root.selected ? Theme.accent : Theme.borderSubtle
    }

    contentItem: Item {
        // Inner padding so the icon and label sit inside the focus ring.
        Item {
            anchors.fill: parent
            anchors.margins: 6

            // Icon layer. PreserveAspectCrop fills the cell — matches the
            // LCD-key visual where the image is the dominant element.
            Image {
                anchors.fill: parent
                source: root.iconSource
                fillMode: Image.PreserveAspectCrop
                smooth: true
                asynchronous: true
                visible: root.iconSource.toString() !== ""
            }

            // Overlay label. Always rendered when `label` is non-empty;
            // otherwise (for icon-less cells) falls back to the 1-based key
            // index so the grid never looks blank.
            Text {
                id: overlayText
                anchors.fill: parent
                text: root.label !== ""
                    ? root.label
                    : (root.iconSource.toString() === "" ? (root.index + 1).toString() : "")
                color: Theme.fgPrimary
                font.pixelSize: root.label !== "" ? Theme.fontSm : Theme.fontLg
                font.weight: root.label !== "" ? Font.DemiBold : Font.Normal
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: root.label !== "" ? Text.AlignBottom : Text.AlignVCenter
                wrapMode: Text.WordWrap

                // When the label sits on top of an icon, draw a subtle dark
                // shadow under the text so light foregrounds stay legible on
                // bright icons. Cheap drop-shadow via doubled Text.
                style: root.iconSource.toString() !== "" ? Text.Outline : Text.Normal
                styleColor: "#000000"
            }
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.label !== ""
        ? qsTr("Key %1: %2").arg(root.index + 1).arg(root.label)
        : qsTr("Key %1").arg(root.index + 1)
    Accessible.description: qsTr("Configures action bound to key %1").arg(root.index + 1)
}
