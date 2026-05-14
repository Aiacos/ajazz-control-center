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

    // implicitHeight (not height) so the consumer can override.
    // Phase 4 (HOTPLUG-02) keeps offline rows fully laid out — the
    // offline state is communicated via the "Offline" badge + a 72%
    // opacity on the background, NOT by collapsing the row to 0.
    implicitHeight: 56

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.hovered ? Theme.bgRowHover : Theme.tile
        border.width: root.activeFocus ? Theme.focusRingWidth : 0
        border.color: Theme.accent
        // Subtle visual reduction when offline: drop the entire row to
        // 72% opacity. Combined with the explicit "Offline" pill below,
        // this gives offline rows the right "still here, but not active"
        // affordance without crossing into the alarmist territory the
        // D-01 silent-badge policy explicitly forbids (no error red,
        // no warning amber).
        opacity: root.deviceConnected ? 1.0 : 0.72
    }

    contentItem: RowLayout {
        spacing: Theme.spacingSm

        ColumnLayout {
            Layout.fillWidth: true
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

        // Offline pill (HOTPLUG-02 + D-01 silent-badge policy).
        //
        // Renders only when the device is offline. Reuses the existing
        // Theme.fgMuted token for both the pill background tint and the
        // text colour so the visual stays inside the v1.0 palette
        // vocabulary — no new colour added. The rectangle uses
        // Theme.radiusSm + low-alpha fgMuted so the pill reads as
        // de-emphasised rather than alarmist.
        //
        // No animation, no toast, no modal — per D-01 the only visible
        // state change on hot-plug is this badge (and the row opacity
        // shift handled in `background` above). The "last seen N
        // minutes ago" tooltip is intentionally deferred per CONTEXT
        // Deferred Ideas.
        Rectangle {
            id: offlineBadge
            visible: !root.deviceConnected
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: badgeText.implicitWidth + Theme.spacingSm * 2
            implicitHeight: badgeText.implicitHeight + Theme.spacingXs
            radius: Theme.radiusSm
            color: Qt.rgba(Theme.fgMuted.r, Theme.fgMuted.g, Theme.fgMuted.b, 0.12)
            border.width: 1
            border.color: Qt.rgba(Theme.fgMuted.r, Theme.fgMuted.g, Theme.fgMuted.b, 0.30)

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: qsTr("Offline")
                color: Theme.fgMuted
                font.pixelSize: Theme.fontXs
            }
        }
    }

    Accessible.role: Accessible.ListItem
    Accessible.name: root.modelName
    Accessible.description: root.deviceConnected
        ? qsTr("Connected device %1").arg(root.deviceCodename)
        : qsTr("Offline device %1").arg(root.deviceCodename)
}
