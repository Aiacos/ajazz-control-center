// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceSelector.qml -- OpenDeck-style device dropdown for the top nav.
//
// Mirrors OpenDeck's `DeviceSelector.svelte`: a compact dropdown in the
// top-left of the window that selects the device being edited. Replaces the
// former 320 px left DeviceList sidebar (OpenDeck has no device sidebar).
//
// Bound directly to the DeviceModel singleton. Only currently-connected
// devices are selectable; disconnected catalogue rows collapse to zero height
// in the popup (matching the connected-only philosophy of the old sidebar).
//
// Emits `deviceSelected(codename)` when the user picks a device. The parent
// (Main.qml) wires this to StreamDockControlService.setActiveDevice + the
// editor codename/capabilities, exactly as the old sidebar did.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AjazzControlCenter

RowLayout {
    id: root

    /// Emitted when the user activates a device entry; carries the codename.
    signal deviceSelected(string codename)

    spacing: Theme.spacingSm

    readonly property int materialTheme:
        ThemeService.effectiveMode === "light" ? Material.Light : Material.Dark

    /// Sync the dropdown selection to a codename (used on bootstrap / hot-plug).
    function selectCodename(cn) {
        combo.currentIndex = combo.indexOfValue(cn);
    }

    Text {
        text: qsTr("Device")
        color: Theme.fgMuted
        font.pixelSize: Theme.typeLabelSmall.pixelSize
        Layout.alignment: Qt.AlignVCenter
    }

    ComboBox {
        id: combo
        objectName: "deviceSelector"
        Layout.preferredWidth: 240
        Layout.alignment: Qt.AlignVCenter

        model: DeviceModel
        textRole: "model"
        valueRole: "codename"

        Material.theme: root.materialTheme
        Material.accent: Theme.accent
        Material.foreground: Theme.fgPrimary

        // Connected-only: disconnected catalogue rows collapse to zero height
        // so the popup lists just the plugged-in devices (same filter the old
        // sidebar applied via `visible: connected`).
        delegate: ItemDelegate {
            id: entry
            required property int    index
            required property string model      // ModelRole — human product name
            required property string codename
            required property bool   connected

            width: combo.width
            visible: connected
            height: connected ? implicitHeight : 0
            text: model
            highlighted: combo.highlightedIndex === entry.index

            Material.theme: root.materialTheme
            Material.accent: Theme.accent
        }

        // OpenDeck behaviour: selecting a device immediately activates it.
        onActivated: root.deviceSelected(combo.currentValue)

        Accessible.role: Accessible.ComboBox
        Accessible.name: qsTr("Device selector")
        Accessible.description: qsTr("Choose which connected device to edit")
    }
}
