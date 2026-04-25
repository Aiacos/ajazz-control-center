// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sidebar panel showing connected and previously-seen AJAZZ devices.
// Exposes `model` (alias to the internal ListView model) for the parent to
// supply a device list model.  Emits `deviceSelected(codename)` when the
// user clicks (or activates with Enter / Space) a device row.
//
// Each row is a DeviceRow component (QtQuick.Controls ItemDelegate) so it is
// keyboard-focusable, hover-highlighted and exposes Accessible roles.
import QtQuick
import QtQuick.Controls
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
}
