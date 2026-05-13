// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceRow.qml — list-item row for the device sidebar.
//
// Replaces the inline Rectangle/MouseArea pair previously hard-coded in
// DeviceList.qml. Uses ItemDelegate so the row is keyboard-focusable and gets
// hover/press effects from QtQuick.Controls.
//
// Properties:
//   * `modelName`        — primary line (human-readable device model).
//   * `deviceCodename`   — secondary line component (machine identifier).
//   * `deviceConnected`  — secondary line component (online state).
//
// Emits:
//   * `clicked` — already provided by ItemDelegate.
//
// Naming note: `deviceCodename` / `deviceConnected` instead of the more
// natural `codename` / `connected` so that the consumer's delegate can
// declare `required property string codename` / `required property bool
// connected` (the model role names) without shadowing — `<role>: <role>`
// would otherwise self-bind to the component's own property and silently
// resolve to the default value (empty string / false). Was the cause of
// "all devices show offline" reported 2026-05-13.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

ItemDelegate {
    id: root

    property string modelName: ""
    property string deviceCodename: ""
    property bool deviceConnected: false

    // implicitHeight (not height) so the consumer can override —
    // DeviceList collapses non-connected rows to 0 via
    // `height: connected ? implicitHeight : 0`.
    implicitHeight: 56

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.hovered ? Theme.bgRowHover : Theme.tile
        border.width: root.activeFocus ? Theme.focusRingWidth : 0
        border.color: Theme.accent
    }

    contentItem: ColumnLayout {
        spacing: 2
        Text {
            Layout.fillWidth: true
            text: root.modelName
            color: Theme.fgPrimary
            font.pixelSize: Theme.fontMd
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: "%1 · %2".arg(root.deviceCodename)
                           .arg(root.deviceConnected ? qsTr("connected") : qsTr("offline"))
            color: Theme.fgMuted
            font.pixelSize: Theme.fontXs
            elide: Text.ElideRight
        }
    }

    Accessible.role: Accessible.ListItem
    Accessible.name: root.modelName
    Accessible.description: root.deviceConnected
        ? qsTr("Connected device %1").arg(root.deviceCodename)
        : qsTr("Offline device %1").arg(root.deviceCodename)
}
