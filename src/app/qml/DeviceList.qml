// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sidebar panel showing connected and previously-seen AJAZZ devices.
// Exposes `model` (alias to the internal ListView model) for the parent to
// supply a device list model.  Emits `deviceSelected(codename)` when the
// user clicks (or activates with Enter / Space) a device row.
//
// Each row is a DeviceRow component (QtQuick.Controls ItemDelegate) so it is
// keyboard-focusable, hover-highlighted and exposes Accessible roles.
//
// `pragma ComponentBehavior: Bound` makes IDs from outer components
// (`root`) statically resolvable inside the delegate, so qmllint can
// type-check `root.deviceSelected(...)` instead of warning unqualified.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Rectangle {
    id: root
    color: Theme.bgSidebar

    /// Emitted when the user activates a device row; carries the device codename.
    signal deviceSelected(string codename)

    /// The device model. Set by the parent (Main.qml) to DeviceModel.
    /// We feed it to a Repeater inside a ColumnLayout (rather than a
    /// ListView) so non-connected delegates can be hidden via
    /// `visible: false` without ListView's spacing being applied to
    /// the gap. ColumnLayout natively skips `visible: false` items
    /// when computing positions, so the visible rows stay vertically
    /// flush with each other regardless of how many offline rows are
    /// interleaved between them in the underlying model.
    property var model: null

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        clip: true
        // Hide the entire scroll surface when nothing is connected — the
        // EmptyState below takes over the sidebar real estate. Without
        // this branch the sidebar looks like the app is broken on first
        // launch (no devices plugged in yet).
        visible: rows.visibleChildren.length > 0

        ColumnLayout {
            id: rows
            width: scroll.availableWidth
            spacing: Theme.spacingXs

            Repeater {
                model: root.model

                delegate: DeviceRow {
                    // F-08/COD-019: required model roles instead of parent.parent.model.
                    // Names match DeviceModel::roleNames() exactly. To avoid the QML
                    // self-binding trap (`connected: connected` would self-reference
                    // the DeviceRow's `connected` property and resolve to `false`),
                    // the consumer-facing properties on DeviceRow are namespaced
                    // (deviceCodename / deviceConnected). See the note at the top
                    // of DeviceRow.qml.
                    required property string model
                    required property string codename
                    required property int    family
                    required property bool   connected

                    Layout.fillWidth: true
                    modelName: model
                    deviceCodename: codename
                    deviceConnected: connected

                    // ColumnLayout skips `visible: false` items entirely
                    // when laying out children, so spacing isn't applied
                    // to the hidden gap. No ghost spacing → connected
                    // rows stay vertically flush with each other.
                    visible: connected

                    onClicked: root.deviceSelected(codename)
                }
            }
        }
    }

    // Onboarding hint shown when no devices are connected and not in
    // the collapsed-icon-only narrow layout (root.width < 200 covers the
    // 64 px collapsed mode that Main.qml falls into below 700 px window
    // width — drawing the title at that width clips badly).
    EmptyState {
        anchors.centerIn: parent
        anchors.margins: Theme.spacingLg
        width: parent.width - Theme.spacingLg * 2
        // Mirror the ScrollView visibility condition: hidden scroll
        // means zero connected rows, which is exactly when we want
        // the onboarding hint. Avoids the "scroll visible AND empty
        // state visible at the same time" double-display.
        visible: !scroll.visible && root.width >= 200
        title: qsTr("No devices yet")
        body: qsTr("Plug in an AJAZZ device — keyboard, Stream Dock, or mouse — and it will show up here automatically.")
    }
}
