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
import AjazzControlCenter
import "components"

Rectangle {
    id: root
    color: Theme.bgSidebar

    /// Emitted when the user activates a device row; carries the device codename.
    signal deviceSelected(string codename)

    /// Alias to the internal ListView model; set by the parent to supply devices.
    property alias model: list.model

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingXs
        clip: true
        focus: true
        keyNavigationEnabled: true
        // Hide the empty list entirely when there are no devices — the
        // EmptyState below takes over the sidebar real estate. Without
        // this branch the sidebar looks like the app is broken on first
        // launch (no devices plugged in yet).
        visible: count > 0

        delegate: DeviceRow {
            // F-08/COD-019: required model roles instead of parent.parent.model.
            required property string model
            required property string codename
            required property int    family
            required property bool   connected

            width: ListView.view ? ListView.view.width : implicitWidth
            modelName: model
            codename: codename
            connected: connected

            onClicked: root.deviceSelected(codename)
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
        visible: list.count === 0 && root.width >= 200
        title: qsTr("No devices yet")
        body: qsTr("Plug in an AJAZZ device — keyboard, Stream Dock, or mouse — and it will show up here automatically.")
    }
}
