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
    // Core DeviceFamily int (0=Unknown, 1=StreamDeck, 2=Keyboard, 3=Mouse — see
    // src/core/include/ajazz/core/device.hpp). Drives the per-family leading icon.
    // Named `deviceFamily` (NOT `family`) to dodge the QML self-binding trap: if it
    // shared the model role name `family`, the consumer's `family: family` binding in
    // DeviceList.qml would resolve to this property's own default (0). Same naming
    // convention rationale as deviceCodename / deviceConnected.
    property int deviceFamily: 0
    // 2026-05-18 P3.d: gates the per-row BatteryIndicator chip. Bound by
    // DeviceList.qml from the DeviceModel role `deviceHasBattery` (which
    // mirrors DeviceDescriptor::hasBattery). Same self-binding-trap
    // naming pattern as `hasClockCapability` above: the role name
    // (`deviceHasBattery`) intentionally differs from this property
    // (`hasBatteryCapability`) so the consumer's
    // `hasBatteryCapability: deviceHasBattery` binding does not
    // self-reference.
    property bool hasBatteryCapability: false

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

        // Per-family device-type icon at the leading edge. Source selected by
        // deviceFamily (DeviceFamily int): 2→keyboard, 3→mouse, 1/0/else→streamdock
        // as a neutral default. The three SVGs are clean-room originals brought onto
        // this branch from main's icon set, aliased under icons/devices/ by the QML
        // module (qrc:/qt/qml/AjazzControlCenter/icons/devices/<name>.svg). 24px square,
        // vertically centred. Rendered at a smaller opacity for offline rows via the
        // row-level background opacity already applied to the whole contentItem.
        Image {
            id: familyIcon
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            fillMode: Image.PreserveAspectFit
            sourceSize.width: 24
            sourceSize.height: 24
            mipmap: true
            source: {
                var base = "qrc:/qt/qml/AjazzControlCenter/icons/devices/";
                if (root.deviceFamily === 2) return base + "device-keyboard.svg";
                if (root.deviceFamily === 3) return base + "device-mouse.svg";
                // 1=StreamDeck, 0=Unknown, and any future family fall back to the
                // generic Stream Dock illustration.
                return base + "device-streamdock.svg";
            }
            Accessible.ignored: true
        }

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
                text: root.deviceConnected ? qsTr("connected") : qsTr("offline")
                // Status colour: connected reads in the success green, offline
                // stays muted.
                color: root.deviceConnected ? Theme.successAccent : Theme.fgMuted
                font.pixelSize: Theme.fontXs
                elide: Text.ElideRight
            }
        }

        // (Per-device Time-sync moved to the device "Settings" tab — see
        // SettingsRow.qml. The sidebar row no longer carries a sync control.)

        // Battery indicator chip (2026-05-18 P3.d).
        //
        // Only mounted for rows whose device advertises Capability::Battery
        // (descriptor.hasBattery == true). The chip itself self-collapses
        // when no reading has been observed (percent < 0) or when the
        // device explicitly reports unavailable, so the row's right-side
        // stack stays uncluttered for devices that are battery-capable
        // but momentarily silent. Wired devices never see this branch
        // at all because their descriptor sets hasBattery=false.
        //
        // We keep the chip mounted (not just gated by `connected`) for
        // a battery-capable device that is currently offline so that, if
        // a future reconnect arrives, the chip's existing Connections
        // block is already wired and can paint on the next signal
        // without a re-creation hiccup. The chip's own `visible: percent
        // >= 0 && !unavailable` keeps the visual surface clean.
        BatteryIndicator {
            // Mounted for every battery-capable device: the chip reads "--%"
            // during connect / before the first reading, then fills in with the
            // charge level. (Don't gate on `percent >= 0` here — the slot must
            // stay put while connected so the row layout doesn't jitter.) The
            // chip's own `connected` binding below collapses it when offline.
            visible: root.hasBatteryCapability
            Layout.alignment: Qt.AlignVCenter
            codename: root.deviceCodename
            // Collapse the chip when the device is offline so it never shows a
            // stale charge after a disconnect that produced no batteryUnavailable
            // signal (the codename just drops out of BatteryService's poll).
            connected: root.deviceConnected
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

    // Maturity tier moved to the device "Settings" tab (see SettingsRow.qml).
    // The sidebar row no longer carries a hover tooltip for it.
}
