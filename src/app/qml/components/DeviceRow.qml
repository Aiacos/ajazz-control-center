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
    // Phase 5 Plan 05-06: gates the per-row "Sync time" ToolButton + glyph.
    // Bound by DeviceList.qml from `model.deviceHasClock` (DeviceModel
    // HasClockRole, Plan 05-05). The property name is `hasClockCapability`
    // (NOT `deviceHasClock`) to dodge the QML self-binding trap — if both
    // the model role and this property shared `deviceHasClock`, the binding
    // `deviceHasClock: deviceHasClock` in DeviceList.qml would resolve to
    // this property's own default (false) instead of the model role.
    // Same naming convention rationale as deviceCodename / deviceConnected.
    property bool hasClockCapability: false
    // Last sync state for the per-row glyph (Plan 05-06):
    //   ""              → no glyph (default).
    //   "success"       → checkmark glyph (manual sync OK; D-02 toast also fires).
    //   "not_implemented" → exclamation glyph + tooltip "not yet supported".
    //   "io_error"      → exclamation glyph + tooltip "HID write failed".
    //   "not_capable"   → exclamation glyph (rare — shouldn't happen if
    //                     deviceHasClock-gating works correctly).
    // Main.qml sets this via the Connections target: TimeSyncService block
    // (manual sync) AND on autoSync ticks (D-02: glyph-only, no toast).
    property string syncGlyphState: ""

    /// Emitted by the Sync time ToolButton when the user manually triggers
    /// a sync push for this row. Main.qml bubbles this up to
    /// TimeSyncService.setSystemTimeOn(codename).
    signal syncTimeRequested(string deviceCodename)

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

        // Phase 5 Plan 05-06: per-row "Sync time" ToolButton + glyph state.
        //
        // Visibility: `deviceHasClock` role from DeviceModel (Plan 05-05).
        // Rows without the capability hide the button entirely — the
        // descriptor-flag gate from Plan 05-01 keeps the surface tidy.
        //
        // Glyph: `syncGlyphState` (set by Main.qml on TimeSyncService signals)
        // overlays a small status icon ("✓" / "!" / "") at the trailing edge.
        //
        // D-02 contract: manual click here ALWAYS emits syncTimeRequested
        // (Main.qml fires the toast on syncSucceeded / syncFailed). Auto-sync
        // failures never invoke this button — they just mutate syncGlyphState
        // via the autoSync connection.
        ToolButton {
            id: syncButton
            visible: root.hasClockCapability
            Layout.alignment: Qt.AlignVCenter
            // QtQuick.Controls 6 Material icon names map cleanly when the
            // platform has icon-name resolution; otherwise we fall back on
            // a plain text "↻" glyph which keeps the affordance discoverable
            // without dragging in an extra qrc asset for v1.0.
            icon.name: "view-refresh"
            text: icon.name === "view-refresh" && icon.source.toString() === "" ? qsTr("↻") : ""
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Sync time to device")
            ToolTip.delay: 500
            onClicked: root.syncTimeRequested(root.deviceCodename)
            Accessible.name: qsTr("Sync time to %1").arg(root.modelName)
            Accessible.description: qsTr("Push the system time to this device's clock surface")
        }

        // Sync glyph (Plan 05-06 / D-02 surface).
        //
        // Renders only when syncGlyphState is non-empty. Reuses Theme palette
        // tokens — no new colours added. Tooltip text mirrors the toast
        // wording for manual-sync failures (NotImplemented / IoError); for
        // auto-sync the tooltip is the only surface (D-02: no toast).
        Text {
            id: syncGlyph
            visible: root.hasClockCapability && root.syncGlyphState !== ""
            Layout.alignment: Qt.AlignVCenter
            text: root.syncGlyphState === "success" ? qsTr("✓") : qsTr("!")
            color: root.syncGlyphState === "success" ? Theme.accent : Theme.fgMuted
            font.pixelSize: Theme.fontMd
            ToolTip.visible: hoverArea.containsMouse
            ToolTip.text: {
                if (root.syncGlyphState === "success") return qsTr("Last sync OK");
                if (root.syncGlyphState === "not_implemented") return qsTr("Time sync not yet supported on this device");
                if (root.syncGlyphState === "io_error") return qsTr("Time sync failed: HID write error");
                if (root.syncGlyphState === "not_capable") return qsTr("Device does not advertise a clock surface");
                return "";
            }
            ToolTip.delay: 500

            MouseArea {
                id: hoverArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton // tooltip only
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
