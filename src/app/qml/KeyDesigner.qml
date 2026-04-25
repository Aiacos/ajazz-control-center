// SPDX-License-Identifier: GPL-3.0-or-later
//
// Key designer tab: renders an N×M grid of clickable key cells, where N comes
// from `device.gridColumns` and the total cell count from `device.keyCount`.
//
// Properties (set by the parent ProfileEditor):
//   * `keyCount`     — number of cells (default 0).
//   * `gridColumns`  — preferred grid column count (default 5).
//
// Emits:
//   * `keyActivated(int index)` — user clicked / activated a key.
//   * `keySelected(int index)`  — user moved focus to a key.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Item {
    id: root

    property int keyCount: 0
    property int gridColumns: 5
    property int selectedIndex: -1

    signal keyActivated(int index)
    signal keySelected(int index)

    EmptyState {
        anchors.centerIn: parent
        visible: root.keyCount === 0
        title: qsTr("No keys")
        body: qsTr("This device does not expose programmable LCD keys.")
    }

    GridLayout {
        anchors.centerIn: parent
        visible: root.keyCount > 0
        columns: Math.max(1, root.gridColumns)
        rowSpacing: Theme.spacingSm
        columnSpacing: Theme.spacingSm

        Repeater {
            model: root.keyCount
            delegate: KeyCell {
                required property int index
                index: index
                selected: root.selectedIndex === index
                onClicked: {
                    root.selectedIndex = index;
                    root.keySelected(index);
                    root.keyActivated(index);
                }
            }
        }
    }
}
